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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/filter.h>
#include <linux/udp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ev.h>

#include "conf.h"
#include "packet.h"
#include "logging.h"
#include "config.h"
#include "misc.h"

static int sd_separate_in = 0;

static ev_signal sighup_watcher;
static ev_signal sigint_watcher;
static ev_signal sigquit_watcher;
static ev_signal sigterm_watcher;

static ev_io io_upstreams_separate_listen;
static ev_io io_netlink;
static ev_timer netlink_timer;

static char config_file[128] = "/etc/dhcprelayd.conf";

static int get_ifindex(struct msghdr *msg)
{
	struct cmsghdr *cmptr;
	int ifindex = 0;

	for (cmptr = CMSG_FIRSTHDR(msg); cmptr; cmptr = CMSG_NXTHDR(msg, cmptr)) {
		if (cmptr->cmsg_level == SOL_IP && cmptr->cmsg_type == IP_PKTINFO) {
			union {
				unsigned char *c;
				struct in_pktinfo *p;
			} p;

			p.c = CMSG_DATA(cmptr);
			ifindex = p.p->ipi_ifindex;
		}
	}

	return ifindex;
}

static struct dhcp_packet_with_opts *packet_rcv_udp(int sd, unsigned char *buf, int sz, int *len, int *ifindex,
						    struct in_addr *src)
{
	union {
		struct cmsghdr align;	/* this ensures alignment */
		char control[CMSG_SPACE(sizeof(struct in_pktinfo))];
	} control_u;
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in saddr;

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = control_u.control;
	msg.msg_controllen = sizeof(control_u);
	msg.msg_name = &saddr;
	msg.msg_namelen = sizeof(saddr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	iov.iov_base = buf;	// packet;
	iov.iov_len = sz;
	*len = recvmsg(sd, &msg, 0);

	*ifindex = get_ifindex(&msg);

	if (*len < 1) {
		syslog2(LOG_WARNING, "Received udp packet size %d.", *len);
		return NULL;
	}

	if (!*ifindex) {
		syslog2(LOG_WARNING, "Invalid ifindex for received udp packet (%d).", *ifindex);
		return NULL;
	}
	*src = saddr.sin_addr;

	return (struct dhcp_packet_with_opts *)buf;
}

static struct dhcp_packet_with_opts *packet_rcv_raw(int sd, unsigned char *buf, int sz, int *len,
						    unsigned char **mac, in_addr_t giaddr)
{
	struct sockaddr_ll sl;
	struct ethhdr *eth_hdr = (struct ethhdr *)buf;
	struct iphdr *ip = (struct iphdr *)(eth_hdr + 1);
	struct udphdr *udp;
	int hlen, ulen;

	socklen_t salen = sizeof sl;

	*len = recvfrom(sd, buf, sz, 0, (struct sockaddr *)&sl, &salen);
	if (*len < 1) {
		syslog2(LOG_WARNING, "Received raw packet size %d.", *len);
		return NULL;
	}

	/* Not interested in outgoing packets. */
	if (sl.sll_pkttype & PACKET_OUTGOING)
		return NULL;

	/* The filter only passes IPv4 without options */
	hlen = sizeof(struct ethhdr) + sizeof(struct iphdr);
	if (*len < hlen + (int)sizeof(struct udphdr)) {
		syslog2(LOG_DEBUG, "Received truncated raw packet (%d bytes).", *len);
		return NULL;
	}

	/* Unicast to a server or another relay agent, left to the bridge */
	if (!(eth_hdr->h_dest[0] & 1) && ip->daddr != giaddr)
		return NULL;

	udp = (struct udphdr *)(buf + hlen);

	ulen = ntohs(udp->len);
	if (ulen < (int)sizeof(struct udphdr) || hlen + ulen > *len) {
		syslog2(LOG_DEBUG, "Received raw packet with invalid UDP length (%d).", ulen);
		return NULL;
	}
	*len = ulen - sizeof(struct udphdr);
	*mac = eth_hdr->h_source;

	return (struct dhcp_packet_with_opts *)(udp + 1);
}

static void packet_cb_raw(struct ev_loop *loop __attribute__ ((unused)), ev_io *w, int revents __attribute__ ((unused)))
{
	static unsigned char buf[2048];	/* MAXIMUM_PACKET_SIZE */
	conf_iface_t *iface = w->data;
	conf_t *cfg = ev_userdata(loop);
	struct dhcp_packet_with_opts *packet;
	unsigned char *mac;
	int len;

	ev_io_stop(loop, w);

	/* Replies are read from the UDP socket */
	packet = packet_rcv_raw(w->fd, buf, sizeof(buf) - DHCP_HEADROOM, &len, &mac, iface->group->giaddr);
	if (packet && packet->header.op == BOOTREQUEST)
		handle_request(cfg, iface, &packet->header, len, mac);

	ev_io_start(loop, w);
}

static void udp_recv(struct ev_loop *loop, ev_io *w, int replies_only)
{
	static unsigned char buf[2048];	/* MAXIMUM_PACKET_SIZE */
	conf_t *cfg = ev_userdata(loop);
	struct dhcp_packet_with_opts *packet;
	conf_iface_t *iface;
	struct in_addr src;
	int ifindex;
	int len;

	ev_io_stop(loop, w);

	packet = packet_rcv_udp(w->fd, buf, sizeof(buf) - DHCP_HEADROOM, &len, &ifindex, &src);
	if (packet && packet->header.op == BOOTREPLY)
		handle_reply(cfg, &packet->header, len, src);
	else if (packet && packet->header.op == BOOTREQUEST && !replies_only) {
		iface = find_iface(cfg, ifindex);
		if (iface)
			handle_request(cfg, iface, &packet->header, len, NULL);
		else
			syslog2(LOG_ERR, "Received packet on wrong interface (ifindex: %d), discarding it.", ifindex);
	}
	ev_io_start(loop, w);
}

static void packet_cb_udp(struct ev_loop *loop, ev_io *w, int revents __attribute__ ((unused)))
{
	udp_recv(loop, w, 0);
}

/* Requests to port 67 are read from the raw sockets */
static void packet_cb_upstream(struct ev_loop *loop, ev_io *w, int revents __attribute__ ((unused)))
{
	udp_recv(loop, w, 1);
}

/*
 * UDP to port 67 in an IPv4 packet without options, that is not a
 * fragment, tagged with VLAN @vid, 0 for untagged or priority tagged.
 */
static int add_filter(int sd, int vid)
{
	struct sock_fprog fprog;

	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_AD_OFF + SKF_AD_VLAN_TAG),
		BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xfff),
		BPF_STMT(BPF_MISC | BPF_TAX, 0),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT),
		BPF_STMT(BPF_ALU | BPF_MUL | BPF_X, 0),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, vid, 0, 11),
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, 9),
		BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 14),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x45, 0, 7),
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),
		BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x3fff, 5, 0),
		BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_UDP, 0, 3),
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 36),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, DHCP_SERVER_PORT, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, 0x40000),
		BPF_STMT(BPF_RET | BPF_K, 0),
	};
	fprog.len = sizeof filter / sizeof(filter[0]);
	fprog.filter = filter;

	if ((setsockopt(sd, SOL_SOCKET, SO_ATTACH_FILTER, &fprog, sizeof(fprog))) < 0) {
		syslog2(LOG_ERR, "Failed to set socket filter: %m");
		return 1;
	}
	return 0;
}

static int bind_to_interface(int sd, int ifindex)
{
	struct sockaddr_ll sll;

	bzero(&sll, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = htons(ETH_P_ALL);
	sll.sll_ifindex = ifindex;
	if ((bind(sd, (struct sockaddr *)&sll, sizeof(sll))) == -1) {
		syslog2(LOG_ERR, "Failed to bind socket to interface: %m");
		return 1;
	}
	return 0;
}

static int setup_sockets_raw(struct ev_loop *loop, conf_t *cfg)
{
	conf_group_t *group;
	conf_iface_t *iface;

	TAILQ_FOREACH(group, &cfg->group_list, link) {
		TAILQ_FOREACH(iface, &group->iface_list, link) {
			/* No protocol, nothing is received before the filter and the bind */
			iface->sd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, 0);
			if (iface->sd == -1) {
				syslog2(LOG_ERR, "Failed creating raw socket: %m");
				return 1;
			}
			if (add_filter(iface->sd, iface->vid) || bind_to_interface(iface->sd, iface->ifindex))
				return 1;
			ev_io_init(&iface->io, packet_cb_raw, iface->sd, EV_READ);
			iface->io.data = iface;
			ev_io_start(loop, &iface->io);
		}
	}
	return 0;
}


static int setup_socket_udp_separate_listen(struct ev_loop *loop, conf_t *cfg)
{
	int udp_listen_port = cfg->udp_listen_port;
	int oneopt = 1;

	if (sd_separate_in > 0)
		return 0;

	/* Port 67 is already read by the per interface sockets */
	if (udp_listen_port == DHCP_SERVER_PORT)
		return 0;

	sd_separate_in = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
	if (sd_separate_in == -1) {
		perror("Unable to create in socket");
		return 1;
	}
	if (setsockopt(sd_separate_in, SOL_IP, IP_PKTINFO, &oneopt, sizeof(oneopt)) == -1 ||
	    setsockopt(sd_separate_in, SOL_SOCKET, SO_BROADCAST, &oneopt, sizeof(oneopt)) == -1) {
		perror("Unable to set socket option, SO_BROADCAST");
		close(sd_separate_in);
		return 1;
	}

	if (setsockopt(sd_separate_in, SOL_SOCKET, SO_REUSEPORT, &oneopt, sizeof(oneopt)) == -1) {
		perror("Unable to set socket option, SO_REUSEPORT");
		close(sd_separate_in);
		return 1;
	}

	struct sockaddr_in saddr;

	memset(&saddr, 0, sizeof(saddr));

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(udp_listen_port);
	saddr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sd_separate_in, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in))) {
		perror(PACKAGE_NAME ": cannot bind DHCP high level socket");
		return -1;
	}

	ev_io_init(&io_upstreams_separate_listen, packet_cb_udp, sd_separate_in, EV_READ);
	ev_io_start(loop, &io_upstreams_separate_listen);

	return 0;
}

/* Bound to giaddr, leaves 0.0.0.0:67 to a DHCP server on the same system */
static int setup_sockets_udp(struct ev_loop *loop, conf_t *cfg)
{
	conf_group_t *group;
	int oneopt = 1;

	TAILQ_FOREACH(group, &cfg->group_list, link) {
		struct sockaddr_in saddr = {
			.sin_family = AF_INET,
			.sin_port = htons(DHCP_SERVER_PORT),
			.sin_addr.s_addr = group->giaddr,
		};

		group->sd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
		if (group->sd == -1) {
			syslog2(LOG_ERR, "Failed creating UDP socket: %m");
			return 1;
		}

		if (setsockopt(group->sd, SOL_IP, IP_PKTINFO, &oneopt, sizeof(oneopt)) ||
		    setsockopt(group->sd, SOL_IP, IP_FREEBIND, &oneopt, sizeof(oneopt)) ||
		    setsockopt(group->sd, SOL_SOCKET, SO_BROADCAST, &oneopt, sizeof(oneopt)) ||
		    setsockopt(group->sd, SOL_SOCKET, SO_REUSEADDR, &oneopt, sizeof(oneopt))) {
			syslog2(LOG_ERR, "Failed setting UDP socket options: %m");
			return 1;
		}

		if (bind(group->sd, (struct sockaddr *)&saddr, sizeof(saddr))) {
			syslog2(LOG_ERR, "Failed binding to %s:%d: %m",
				inet_ntoa(saddr.sin_addr), DHCP_SERVER_PORT);
			return 1;
		}

		ev_io_init(&group->io, packet_cb_upstream, group->sd, EV_READ);
		ev_io_start(loop, &group->io);
	}

	return 0;
}

static void cleanup(struct ev_loop *loop)
{
	conf_group_t *group;
	conf_t *cfg = ev_userdata(loop);

	if (!cfg)
		return;

	TAILQ_FOREACH(group, &cfg->group_list, link) {
		conf_iface_t *iface;

		if (group->sd > 0) {
			ev_io_stop(loop, &group->io);
			close(group->sd);
			group->sd = 0;
		}

		TAILQ_FOREACH(iface, &group->iface_list, link) {
			ev_io_stop(loop, &iface->io);
			syslog2(LOG_DEBUG, "clean-up ifindex %d", iface->ifindex);
			if (iface->sd > 0) {
				close(iface->sd);
				iface->sd = 0;
			}
		}
	}
	/* If we have a non standard listen port, we need to "restart" that UDP
	 * socket if the value have possibly been reconfigured. */
	if (sd_separate_in > 0) {
		ev_io_stop(loop, &io_upstreams_separate_listen);
		close(sd_separate_in);
		sd_separate_in = 0;
	}

	cleanup_nftables(cfg);
	conf_free(cfg);
	ev_set_userdata(loop, NULL);
}

static int status;

/* Replace the running configuration with @cfg */
static int init(struct ev_loop *loop, conf_t *cfg)
{
	cleanup(loop);
	ev_set_userdata(loop, cfg);

	if (setup_sockets_raw(loop, cfg) || setup_sockets_udp(loop, cfg) ||
	    setup_socket_udp_separate_listen(loop, cfg)) {
		syslog2(LOG_ERR, "Failed setting up required sockets.");
		return 1;
	}

	if (setup_nftables(cfg)) {
		syslog2(LOG_ERR, "Failed setting up nftables.");
		return 1;
	}

	return 0;
}

static void sigcb(struct ev_loop *loop, ev_signal *w, int rev __attribute__ ((unused)))
{
	conf_t *cfg;

	switch (w->signum) {
	case SIGHUP:
		cfg = conf_read(config_file, 0);
		if (!cfg) {
			syslog2(LOG_ERR, "Failed reading configuration file %s, keeping the old one", config_file);
			break;
		}
		if (init(loop, cfg)) {
			status = 1;
			ev_break(loop, EVBREAK_ALL);
		}
		break;

	default:
		/* Currently graceful shutdown on all other signals */
		ev_break(loop, EVBREAK_ALL);
		break;
	}
}

/* Interfaces and addresses have settled, relay from the ones that changed */
static void netlink_timer_cb(struct ev_loop *loop, ev_timer *w, int revents __attribute__ ((unused)))
{
	conf_t *cfg;

	ev_timer_stop(loop, w);
	cfg = conf_read(config_file, 1);
	if (!cfg)
		return;

	if (conf_same(cfg, ev_userdata(loop))) {
		conf_free(cfg);
		return;
	}

	syslog2(LOG_INFO, "Interfaces or addresses changed, reloading");
	if (init(loop, cfg)) {
		status = 1;
		ev_break(loop, EVBREAK_ALL);
	}
}

static void netlink_cb(struct ev_loop *loop, ev_io *w, int revents __attribute__ ((unused)))
{
	char buf[4096];

	while (recv(w->fd, buf, sizeof(buf), MSG_DONTWAIT) > 0)
		;

	ev_timer_again(loop, &netlink_timer);
}

/* Link and IPv4 address events, for interfaces that show up or change after start */
static int init_netlink(struct ev_loop *loop)
{
	struct sockaddr_nl sa = {
		.nl_family = AF_NETLINK,
		.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR,
	};
	int sd;

	sd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (sd == -1 || bind(sd, (struct sockaddr *)&sa, sizeof(sa))) {
		syslog2(LOG_ERR, "Failed setting up netlink socket: %m");
		return 1;
	}

	ev_timer_init(&netlink_timer, netlink_timer_cb, 0., 1.);
	ev_io_init(&io_netlink, netlink_cb, sd, EV_READ);
	ev_io_start(loop, &io_netlink);

	return 0;
}

static struct ev_loop *init_signals(struct ev_loop *loop)
{
	ev_signal_init(&sigterm_watcher, sigcb, SIGTERM);
	ev_signal_start(loop, &sigterm_watcher);
	ev_signal_init(&sigint_watcher, sigcb, SIGINT);
	ev_signal_start(loop, &sigint_watcher);
	ev_signal_init(&sigquit_watcher, sigcb, SIGQUIT);
	ev_signal_start(loop, &sigquit_watcher);
	ev_signal_init(&sighup_watcher, sigcb, SIGHUP);
	ev_signal_start(loop, &sighup_watcher);

	return loop;
}

static int version(void)
{
	printf("%s v%s\n", PACKAGE_NAME, PACKAGE_VERSION);

	return 0;
}

static int usage(void)
{
	printf("Usage: %s [-hvd] -f <file>\n\n"
	       "  -f <file>   Read config file\n"
	       "  -d          Log all levels to stdout, for debugging.\n"
	       "  -v          Show program version\n"
	       "  -h          Show this help text\n"
	       "\n", PACKAGE_NAME);

	return 0;
}

int main(int argc, char **argv)
{
	struct ev_loop *loop;
	conf_t *cfg;
	int c;

	while ((c = getopt(argc, argv, "f:vdh")) != EOF) {
		switch (c) {
		case 'f':
			strncpy(config_file, optarg, sizeof(config_file));
			config_file[sizeof(config_file)-1] = '\0';
			break;

		case 'v':
			return version();

		case 'd':
			log_console(1);
			break;

		case 'h':
		default:
			return usage();
		}
	}

	if (access(config_file, F_OK)) {
		fprintf(stderr, "Configuration file (%s) not found!\n", config_file);

		return 1;
	}

	setlogmask(LOG_UPTO(LOG_INFO));
	syslog2(LOG_INFO, "Starting " PACKAGE_NAME);

	loop = ev_default_loop(0);

	init_signals(loop);

	cfg = conf_read(config_file, 0);
	if (!cfg) {
		syslog2(LOG_ERR, "Failed reading configuration file %s", config_file);
		status = 1;
	} else if (init(loop, cfg) || init_netlink(loop))
		status = 1;
	else
		ev_run(loop, 0);

	cleanup(loop);

	return status;
}
