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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>

#include "options.h"
#include "packet.h"
#include "logging.h"

#define OPTION_PAD               0
#define OPTION_AGENT_ID          82
#define OPTION_END               255

#define SUBOPT_CIRCUIT_ID        1
#define SUBOPT_REMOTE_ID         2
#define SUBOPT_SERVER_OR         11	/* RFC 5107 */

#define MAX_OPTION_SIZE          255

#define option_len(opt) ((int)(((unsigned char *)(opt))[1]))

typedef unsigned int u32;

struct dhcp_opt {
	int opt, len;
	unsigned char *val;
	struct dhcp_opt *next;
};

static unsigned char *option_find(unsigned char *p, unsigned char *end, int opt)
{
	while (1) {
		if (p >= end)
			return NULL;
		else if (*p == OPTION_END)
			return opt == OPTION_END ? p : NULL;
		else if (*p == OPTION_PAD)
			p++;
		else {
			int opt_len;

			if (p > end - 2)
				return NULL;	/* malformed packet */
			opt_len = option_len(p);
			if (p > end - (2 + opt_len))
				return NULL;	/* malformed packet */
			if (*p == opt)
				return p;
			p += opt_len + 2;
		}
	}
}

/* Add @opt as sub-options of option @encap at @p, returns the new end of the options */
static unsigned char *encap_opts(struct dhcp_opt *opt, int encap, unsigned char *p, unsigned char *end)
{
	struct dhcp_opt *o;
	int len = 0;

	for (o = opt; o; o = o->next)
		len += o->len + 2;

	/* Leave room for OPTION_END */
	if (len > MAX_OPTION_SIZE || p + 2 + len >= end) {
		syslog2(LOG_WARNING, "No room for option %d, %d bytes, in request", encap, len);
		return p;
	}

	*p++ = encap;
	*p++ = len;
	for (o = opt; o; o = o->next) {
		*p++ = o->opt;
		*p++ = o->len;
		memcpy(p, o->val, o->len);
		p += o->len;
	}

	return p;
}

static unsigned char *add_option82(unsigned char *p, unsigned char *end, conf_t *cfg, conf_iface_t *iface,
				   struct in_addr addr)
{
	unsigned char ifname[IF_NAMESIZE];
	char *giaddr;
	struct dhcp_opt opt_cid;
	struct dhcp_opt opt_rid;
	struct dhcp_opt opt_sor;
	struct dhcp_opt *opt_root;
	char host[HOST_NAME_MAX];

	memset(ifname, 0, IF_NAMESIZE);

	syslog2(LOG_DEBUG, "Adding option 82.");

	if (!if_indextoname(iface->ifindex, (char *)&ifname[0]))
		return p;

	opt_rid.opt = SUBOPT_REMOTE_ID;

	switch (cfg->opt82.rid_type) {
	case RID_TYPE_MANUAL:
		opt_rid.val = (unsigned char *)cfg->opt82.remote_id;
		opt_rid.len = cfg->opt82.remote_id_len;
		break;
	case RID_TYPE_GIADDR:
		giaddr = inet_ntoa(addr);
		opt_rid.val = (unsigned char *)giaddr;
		opt_rid.len = strlen(giaddr);
		break;

	case RID_TYPE_HOSTNAME:
		gethostname(host, sizeof(host));
		opt_rid.val = (unsigned char *)host;
		opt_rid.len = strlen(host);
		break;
	}
	opt_rid.next = NULL;

	opt_cid.opt = SUBOPT_CIRCUIT_ID;
	syslog2(LOG_DEBUG, "circuit_id: %p, len: %d\n", iface->circuit_id, iface->circuit_id_len);
	if (iface->circuit_id_len > 0) {
		opt_cid.val = (unsigned char *)iface->circuit_id;
		opt_cid.len = iface->circuit_id_len;
	} else {
		opt_cid.val = &ifname[0];
		opt_cid.len = strlen((char *)ifname);
	}
	opt_cid.next = &opt_rid;

	if (cfg->force_server_id) {
		opt_sor.opt = SUBOPT_SERVER_OR;
		opt_sor.val = (unsigned char *)&addr;
		opt_sor.len = 4;
		opt_sor.next = &opt_cid;
		opt_root = &opt_sor;
	} else
		opt_root = &opt_cid;

	return encap_opts(opt_root, OPTION_AGENT_ID, p, end);
}

/* Returns the new size of the message, or 0 if it is malformed */
size_t add_options(struct dhcp_packet_with_opts *mess, size_t sz, struct in_addr addr, conf_iface_t *iface, conf_t *cfg)
{
	unsigned char *start = &mess->options[0] + sizeof(u32);
	unsigned char *end;
	u32 cookie = htonl(DHCP_COOKIE);
	unsigned char *p;

	syslog2(LOG_DEBUG, "Adding options");

	if (!cfg->opt82.enabled)
		return sz;

	if (mess->header.op != BOOTREQUEST)
		return sz;

	/* From dnsmasq, don't add options if hardware type is 0. */
	if (mess->header.htype == 0 && mess->header.hlen != 0)
		return sz;

	/* BOOTP, no options to add ours to */
	if (sz < (size_t)(start - (unsigned char *)mess) || memcmp(mess->options, &cookie, sizeof(cookie)))
		return sz;

	sz = remove_options(mess, sz);

	p = option_find(start, (unsigned char *)mess + sz, OPTION_END);
	if (!p)
		return 0;
	end = (unsigned char *)mess + sz + DHCP_HEADROOM;
	memset(p, 0, end - p);

	/* Option 82 should always be added last */
	p = add_option82(p, end, cfg, iface, addr);
	*p++ = OPTION_END;

	sz = p - (unsigned char *)mess;

	return sz < MIN_PACKETSZ ? MIN_PACKETSZ : sz;
}

static unsigned char *find_option82(struct dhcp_packet_with_opts *mess, size_t sz)
{
	return option_find(&mess->options[0] + sizeof(u32), (unsigned char *)mess + sz, OPTION_AGENT_ID);
}

int has_option82(struct dhcp_packet_with_opts *mess, size_t sz)
{
	return find_option82(mess, sz) != NULL;
}

/* Remove every option 82, returns the new size of the message */
size_t remove_options(struct dhcp_packet_with_opts *mess, size_t sz)
{
	size_t len, orig = sz;
	unsigned char *opt;

	while ((opt = find_option82(mess, sz))) {
		unsigned char *end = (unsigned char *)mess + sz;

		len = option_len(opt) + 2;
		memmove(opt, opt + len, end - opt - len);
		memset(end - len, 0, len);
		sz -= len;
	}

	/* Keep the zero padding up to the BOOTP minimum size */
	if (sz < MIN_PACKETSZ)
		return orig < MIN_PACKETSZ ? orig : MIN_PACKETSZ;

	return sz;
}
