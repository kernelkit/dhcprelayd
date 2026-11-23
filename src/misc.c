/* dhcprelayd

   Copyright (c) 2004,2008 Simon Kelley
   Copyright (c) 2018 Westermo Teleindustri AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 dated June, 1991, or
   (at your option) version 3 dated 29 June, 2007.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "conf.h"
#include "logging.h"

#define NFT_IN_CHAIN  "dhcpr-input"
#define NFT_FWD_CHAIN "dhcpr-forward "

static char *mac_ntoa(unsigned char *ptr)
{
	static char address[30];

	sprintf(address, "%02X:%02X:%02X:%02X:%02X:%02X", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);

	return (address);
}

static int iface_is_bridged(int ifindex)
{
	char ifname[IF_NAMESIZE], path[sizeof("/sys/class/net//brport") + IF_NAMESIZE];

	if (!if_indextoname(ifindex, ifname))
		return 0;

	snprintf(path, sizeof(path), "/sys/class/net/%s", ifname);
	if (access(path, F_OK)) {
		syslog2(LOG_WARNING, "%s not in /sys, sysfs from another network namespace?", ifname);
		return 0;
	}

	snprintf(path, sizeof(path), "/sys/class/net/%s/brport", ifname);
	return access(path, F_OK) == 0;
}

/* Send a netlink request and wait for its ACK, returns errno or 0 */
static int nl_req(void *req, size_t len)
{
	struct {
		struct nlmsghdr nh;
		struct nlmsgerr err;
	} ack;
	ssize_t rc;
	int sd, err;

	sd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (sd < 0)
		return errno;

	if (send(sd, req, len, 0) < 0)
		err = errno;
	else if ((rc = recv(sd, &ack, sizeof(ack), 0)) < 0)
		err = errno;
	else if (rc < (ssize_t)sizeof(ack) || ack.nh.nlmsg_type != NLMSG_ERROR)
		err = EPROTO;
	else
		err = -ack.err.error;

	close(sd);
	return err;
}

/* Rule for bridge port @ifindex in the nft script @fp, returns 0 if it is not a bridge port */
static int nft_drop(FILE *fp, int ifindex, const char *match)
{
	char ifname[IFNAMSIZ];

	if (!iface_is_bridged(ifindex) || !if_indextoname(ifindex, ifname))
		return 0;

	if (strchr(ifname, '"')) {
		syslog2(LOG_ERR, "Cannot set up nftables for interface %s, quote in name", ifname);
		return 0;
	}
	fprintf(fp, "%siif \"%s\" ip protocol udp udp dport 67 drop\n", match, ifname);

	return 1;
}

int setup_nftables(conf_t *cfg)
{
	conf_group_t *group;
	conf_iface_t *iface;
	char file[128] = "/tmp/dhcprelayd.XXXXXX";
	char cmd[256];
	FILE *fp;
	int err = 0, fd, anybridged = 0;

	fd = mkstemp(file);
	if (fd == -1) {
		syslog2(LOG_ERR, "Could not generate tempfile");
		return 1;
	}

	fp = fdopen(fd, "w+");
	if (!fp) {
		close(fd);
		syslog2(LOG_ERR, "Could not open tempfile");
		err = 1;
		goto err_unlink;
	}

	fprintf(fp, "table bridge filter {\n");
	fprintf(fp, "chain " NFT_IN_CHAIN " {\n type filter hook input priority -200\n");
	TAILQ_FOREACH(group, &cfg->group_list, link) {
		TAILQ_FOREACH(iface, &group->iface_list, link)
			anybridged |= nft_drop(fp, iface->ifindex, "");
	}
	fprintf(fp, "}\n");

	fprintf(fp, "chain " NFT_FWD_CHAIN " {\n type filter hook forward priority -200\n");
	TAILQ_FOREACH(group, &cfg->group_list, link) {
		TAILQ_FOREACH(iface, &group->iface_list, link)
			anybridged |= nft_drop(fp, iface->ifindex, "pkttype broadcast ");
	}
	fprintf(fp, "}\n}");

	fprintf(fp, "\n");
	fclose(fp);
	if (anybridged) {
		snprintf(cmd, sizeof(cmd), "nft -f %s", file);
		if (system(cmd))
			syslog2(LOG_ERR, "Failed applying nftables rules");
	}
err_unlink:
	unlink(file);

	return err;
}

int cleanup_nftables(conf_t *cfg)
{
	conf_group_t *group;
	int anybridged = 0;

	TAILQ_FOREACH(group, &cfg->group_list, link) {
		conf_iface_t *iface;
		TAILQ_FOREACH(iface, &group->iface_list, link) {
			if (iface_is_bridged(iface->ifindex))
				anybridged = 1;
		}
	}

	if (anybridged) {
		syslog2(LOG_DEBUG, "Some interfaces is bridged, remove nftables rules");
		if (system("nft  delete chain bridge filter " NFT_IN_CHAIN))
			syslog2(LOG_ERR, "Failed deleting nftables input rules");
		if (system("nft  delete chain bridge filter " NFT_FWD_CHAIN))
			syslog2(LOG_ERR, "Failed deleting nftables forward rules");
	}
	return 0;
}

/**
   If there is an underlaying switchcore, it may implement 'DHCP-snooping',
   in this case it will most likely not learn the MAC-address correctly.

   To fix this, add a FDB entry in the bridge, if there are a switch under
   the FDB entry will be accelerated.
 */
int add_fdb_entry(int ifindex, unsigned char *mac)
{
	struct {
		struct nlmsghdr nh;
		struct ndmsg    ndm;
		struct rtattr   rta;
		unsigned char   lladdr[ETH_ALEN];
	} req;
	int err;

	if (!iface_is_bridged(ifindex)) {
		syslog2(LOG_DEBUG, "Interface %d is not bridged, skipping FDB entry", ifindex);
		return 0;
	}

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len    = sizeof(req);
	req.nh.nlmsg_type   = RTM_NEWNEIGH;
	req.nh.nlmsg_flags  = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
	req.ndm.ndm_family  = PF_BRIDGE;
	req.ndm.ndm_ifindex = ifindex;
	req.ndm.ndm_state   = NUD_REACHABLE;
	req.ndm.ndm_flags   = NTF_MASTER;
	req.rta.rta_type    = NDA_LLADDR;
	req.rta.rta_len     = RTA_LENGTH(ETH_ALEN);
	memcpy(req.lladdr, mac, ETH_ALEN);

	syslog2(LOG_DEBUG, "Adding MAC %s ifindex %d in bridge FDB", mac_ntoa(mac), ifindex);
	err = nl_req(&req, sizeof(req));
	if (err) {
		syslog2(LOG_ERR, "Failed adding FDB entry: %s", strerror(err));
		return 1;
	}

	return 0;
}

int add_arp_entry(int ifindex, unsigned char *mac, struct sockaddr_in saddr)
{
	struct arpreq req;
	struct sockaddr_in *sin;
	char ifname[IFNAMSIZ];
	int fd;

	if_indextoname(ifindex, ifname);

	syslog2(LOG_DEBUG, "Add arp entry for address %s, iface %s mac %s", inet_ntoa(saddr.sin_addr), ifname, mac_ntoa(mac));

	bzero(&req, sizeof(req));
	sin = (struct sockaddr_in *)&req.arp_pa;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = saddr.sin_addr.s_addr;

	memcpy(req.arp_ha.sa_data, mac, ETHER_ADDR_LEN);
	req.arp_flags = ATF_COM;
	strncpy(req.arp_dev, ifname, sizeof(req.arp_dev));

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 1) {
		syslog2(LOG_WARNING, "Failed opening socket to add arp entry");
		return -1;
	}
	if (ioctl(fd, SIOCSARP, &req) < 0) {
		syslog2(LOG_WARNING, "Failed adding arp entry for address %s, iface %s mac %s", inet_ntoa(saddr.sin_addr), ifname, mac_ntoa(mac));
		close(fd);
		return -1;
	}
	close(fd);

	syslog2(LOG_DEBUG, "ARP cache entry successfully added.");

	return 0;
}
