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

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "logging.h"
#include "packet.h"
#include "options.h"
#include "conf.h"
#include <unistd.h>
#include "queue.h"
#include "misc.h"

union {
	struct cmsghdr align;	/* this ensures alignment */
	char control[CMSG_SPACE(sizeof(struct in_pktinfo))];
} control_u;

/* Search for group by iface index. 
 * @Return  NULL if not found. */
conf_group_t *find_group(conf_t *cfg, int ifindex)
{
	conf_group_t *entry;
	conf_iface_t *entry2;

	TAILQ_FOREACH(entry, &cfg->group_list, link) {
		TAILQ_FOREACH(entry2, &entry->iface_list, link) {
			int index = entry2->ifindex;

			if (index == ifindex)
				return entry;
		}
	}
	return NULL;
}

/* Search for group by giaddr. 
 * @Return  NULL if not found. */
conf_group_t *find_group_by_giaddr(conf_t *cfg, struct in_addr giaddr)
{
	conf_group_t *entry;
	conf_iface_t *entry2;

	TAILQ_FOREACH(entry, &cfg->group_list, link) {
		TAILQ_FOREACH(entry2, &entry->iface_list, link) {
			if ((in_addr_t)entry->giaddr == giaddr.s_addr)
				return entry;
		}
	}
	return NULL;
}

/* Is already gatewayed by us? 
 * TODO: Is this needed? Inherited from original dhcp-helper */
static int is_local_gw(conf_t *cfg, in_addr_t s_addr)
{
	conf_group_t *entry;

	TAILQ_FOREACH(entry, &cfg->group_list, link) {
		if (s_addr == (in_addr_t)entry->giaddr)
			return 1;
	}
	return 0;
}

/* The servers of @group, or the global ones */
static struct server_list *servers(conf_t *cfg, conf_group_t *group)
{
	return TAILQ_EMPTY(&group->server_list) ? &cfg->server_list : &group->server_list;
}

/*
 * A server on this system, at 127.0.0.1, replies from the gateway address.
 * The kernel drops that source address from the network, as a martian.
 */
static int from_server(conf_t *cfg, conf_group_t *group, in_addr_t src)
{
	conf_server_t *server;

	TAILQ_FOREACH(server, servers(cfg, group), link) {
		if ((in_addr_t)server->addr == src)
			return 1;
		if (ntohl(server->addr) >> IN_CLASSA_NSHIFT == IN_LOOPBACKNET && src == (in_addr_t)group->giaddr)
			return 1;
	}

	return 0;
}

static int handle_request(conf_t *cfg, struct dhcp_packet *packet, int ifindex, ssize_t sz)
{
	struct sockaddr_in saddr;
	conf_group_t *group;
	conf_server_t *server;

	memset(&saddr, 0, sizeof(saddr));

	syslog2(LOG_DEBUG, "Received a request");

	group = find_group(cfg, ifindex);
	if (!group) {
		syslog2(LOG_ERR, "Received packet on wrong interface (ifindex: %d), discarding it.", ifindex);
		return 1;
	}
	/* already relayed ? */
	if (packet->giaddr.s_addr) {
		syslog2(LOG_DEBUG, "Already relayed by %s", inet_ntoa(packet->giaddr));

		if (is_local_gw(cfg, packet->giaddr.s_addr)) {
			/* RFC3046, 2.1.1, discard if spoofed */
			syslog2(LOG_NOTICE, "Already relayed by this relay agent or spoofed!");
			return 0;
		}

		/* RFC3046, 2.1.1, simply forward if giaddr set, 
		 * without changing giaddr or adding option82.*/
	} else {
		struct dhcp_packet_with_opts *mess = (struct dhcp_packet_with_opts *)packet;
		int add = cfg->opt82.enabled;
		struct in_addr addr;

		/* Replace is done by add_options() */
		if (add && cfg->opt82.policy != OPT82_REPLACE && has_option82(mess, sz)) {
			if (cfg->opt82.policy == OPT82_DROP) {
				syslog2(LOG_NOTICE, "Dropping request with option 82 from client on ifindex %d", ifindex);
				return 0;
			}
			add = 0;
		}

		syslog2(LOG_DEBUG, "Will relay");
		/* plug in our address */

		addr.s_addr = packet->giaddr.s_addr = group->giaddr;
		if (add) {
			sz = add_options(mess, sz, ifindex, addr, group, cfg);
			if (!sz) {
				syslog2(LOG_NOTICE, "Dropping malformed request on ifindex %d", ifindex);
				return 0;
			}
		}
	}

	TAILQ_FOREACH(server, servers(cfg, group), link) {
		saddr.sin_addr.s_addr = server->addr;
		saddr.sin_port = htons(server->port);
		syslog2(LOG_DEBUG, "Forward request to %s!", inet_ntoa(saddr.sin_addr));
		while (sendto(group->sd, packet, sz, 0, (struct sockaddr *)&saddr, sizeof(saddr)) == -1 && errno == EINTR) ;
	}
	if (add_fdb_entry(ifindex, packet->chaddr))
		return 1;

	return 0;
}

static int handle_reply(conf_t *cfg, struct dhcp_packet *packet, int ifindex, ssize_t sz, struct in_addr src)
{
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in saddr;
	struct in_pktinfo *pkt;
	conf_group_t *group;
	struct cmsghdr *cmptr;
	int result = 0;

	syslog2(LOG_DEBUG, "Received a reply on ifindex %d", ifindex);
	/* packet from server send back to client */

	group = find_group_by_giaddr(cfg, packet->giaddr);
	if (!group) {
		syslog2(LOG_NOTICE, "Dropping reply for another relay agent, %s", inet_ntoa(packet->giaddr));
		return 0;
	}
	if (!from_server(cfg, group, src.s_addr)) {
		syslog2(LOG_NOTICE, "Dropping reply from %s, not a server", inet_ntoa(src));
		return 0;
	}

	memset(&saddr, 0, sizeof(saddr));

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = control_u.control;
	msg.msg_controllen = sizeof(control_u);
	msg.msg_name = &saddr;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	saddr.sin_port = htons(DHCP_CLIENT_PORT);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = sizeof(saddr);

	/* I'm the gateway, remove options (option 82) and reset giaddr */
	sz = remove_options((struct dhcp_packet_with_opts *)packet, sz);
	packet->giaddr.s_addr = 0;

	iov.iov_base = packet;
	iov.iov_len = sz;
	if (packet->ciaddr.s_addr)
		saddr.sin_addr = packet->ciaddr;
	else if (ntohs(packet->flags) & 0x8000 || !packet->yiaddr.s_addr || packet->hlen > 14) {
		syslog2(LOG_DEBUG, "Send broadcast reply.");
		/* broadcast to 255.255.255.255 */
		msg.msg_controllen = sizeof(control_u);
		msg.msg_control = control_u.control;
		cmptr = CMSG_FIRSTHDR(&msg);
		saddr.sin_addr.s_addr = INADDR_BROADCAST;
		pkt = (struct in_pktinfo *)CMSG_DATA(cmptr);
		if (!pkt)
			return 1;
		pkt->ipi_ifindex = group->ifindex;
		pkt->ipi_spec_dst.s_addr = 0;
		msg.msg_controllen = cmptr->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
		cmptr->cmsg_level = SOL_IP;
		cmptr->cmsg_type = IP_PKTINFO;
	} else {
		/* client not configured and cannot reply to ARP. 
		   Insert arp entry direct. */
		saddr.sin_addr = packet->yiaddr;

		msg.msg_name = &saddr;
		syslog2(LOG_DEBUG, "Send unicast reply to %s.", inet_ntoa(saddr.sin_addr));
		add_arp_entry(group->ifindex, packet->chaddr, saddr);
	}

	do {
		result = sendmsg(group->sd, &msg, 0);
		if (result < 0)
			syslog2(LOG_WARNING, "Sending error: %m");
	} while (result == -1 && errno == EINTR);

	return 0;
}

int handle_packet(conf_t *cfg, struct dhcp_packet *packet, ssize_t sz, int ifindex, struct in_addr src)
{
	if (sz < (ssize_t)sizeof(*packet))
		return 1;

	/* rfc1542 says discard if > 16 */
	if ((packet->hops++) > 16)
		return 1;

	if (packet->hlen > DHCP_CHADDR_MAX)
		return 1;

	if (packet->op == BOOTREQUEST)
		handle_request(cfg, packet, ifindex, sz);
	else if (packet->op == BOOTREPLY)
		handle_reply(cfg, packet, ifindex, sz, src);

	return 0;
}
