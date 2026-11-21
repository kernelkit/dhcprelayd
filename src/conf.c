/* dhcprelayd

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

#include <confuse.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include "conf.h"
#include "logging.h"

static int parse_policy(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result)
{
	if (!strcmp(value, "replace"))
		*(long *)result = OPT82_REPLACE;
	else if (!strcmp(value, "keep"))
		*(long *)result = OPT82_KEEP;
	else if (!strcmp(value, "drop"))
		*(long *)result = OPT82_DROP;
	else {
		cfg_error(cfg, "invalid %s %s, must be replace, keep, or drop", opt->name, value);
		return -1;
	}

	return 0;
}

static int parse_rid_type(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result)
{
	if (!strcmp(value, "hostname"))
		*(long *)result = RID_TYPE_HOSTNAME;
	else if (!strcmp(value, "giaddr"))
		*(long *)result = RID_TYPE_GIADDR;
	else {
		cfg_error(cfg, "invalid %s %s, must be hostname or giaddr", opt->name, value);
		return -1;
	}

	return 0;
}

static cfg_opt_t server_opts[] = {
	CFG_INT("port", 67, CFGF_NONE),
	CFG_END()
};

static cfg_opt_t option82_opts[] = {
	CFG_INT_CB("policy", OPT82_REPLACE, CFGF_NONE, parse_policy),
	CFG_INT_CB("remote-id-type", RID_TYPE_HOSTNAME, CFGF_NONE, parse_rid_type),
	CFG_STR("remote-id", NULL, CFGF_NONE),
	CFG_STR("remote-id-hex", NULL, CFGF_NONE),
	CFG_END()
};

static cfg_opt_t port_opts[] = {
	CFG_STR("circuit-id", NULL, CFGF_NONE),
	CFG_STR("circuit-id-hex", NULL, CFGF_NONE),
	CFG_END()
};

static cfg_opt_t interface_opts[] = {
	CFG_STR("giaddr", "auto", CFGF_NONE),
	CFG_SEC("server", server_opts, CFGF_MULTI | CFGF_TITLE | CFGF_NO_TITLE_DUPES),
	CFG_SEC("port", port_opts, CFGF_MULTI | CFGF_TITLE | CFGF_NO_TITLE_DUPES),
	CFG_END()
};

static cfg_opt_t opts[] = {
	CFG_SEC("server", server_opts, CFGF_MULTI | CFGF_TITLE | CFGF_NO_TITLE_DUPES),
	CFG_BOOL("force-server-identity", cfg_false, CFGF_NONE),
	CFG_INT("udp-listen-port", 67, CFGF_NONE),
	CFG_SEC("option82", option82_opts, CFGF_NODEFAULT),
	CFG_SEC("interface", interface_opts, CFGF_MULTI | CFGF_TITLE | CFGF_NO_TITLE_DUPES),
	CFG_END()
};

static void conf_error(cfg_t *cfg, const char *fmt, va_list ap)
{
	char buf[256];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	if (cfg && cfg->filename)
		syslog2(LOG_ERR, "%s:%d: %s", cfg->filename, cfg->line, buf);
	else
		syslog2(LOG_ERR, "%s", buf);
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Colon separated pairs of hex digits, e.g. c0:ff:ee, returns length or -1 */
static int parse_hex(const char *str, char *buf, size_t size)
{
	size_t len = 0;

	while (1) {
		int hi = hexval(str[0]), lo = hi < 0 ? -1 : hexval(str[1]);

		if (len == size || lo < 0)
			return -1;
		buf[len++] = hi << 4 | lo;
		str += 2;
		if (!*str)
			return len;
		if (*str++ != ':')
			return -1;
	}
}

static int valid_port(cfg_t *cfg, cfg_opt_t *opt)
{
	long port = cfg_opt_getnint(opt, cfg_opt_size(opt) - 1);

	if (port < 1 || port > 65535) {
		cfg_error(cfg, "invalid %s %ld", opt->name, port);
		return -1;
	}

	return 0;
}

static int valid_id(cfg_t *cfg, cfg_opt_t *opt)
{
	const char *val = cfg_opt_getnstr(opt, 0);
	char buf[OPT82_ID_LEN];
	size_t len = strlen(val);

	if (strstr(opt->name, "-hex")) {
		if (parse_hex(val, buf, sizeof(buf)) < 0) {
			cfg_error(cfg, "invalid %s %s, 1-%zu bytes as xx:xx:...", opt->name, val, sizeof(buf));
			return -1;
		}
	} else if (len == 0 || len > sizeof(buf)) {
		cfg_error(cfg, "invalid %s \"%s\", 1-%zu characters", opt->name, val, sizeof(buf));
		return -1;
	}

	return 0;
}

static int valid_address(cfg_t *cfg, const char *what, const char *addr)
{
	struct in_addr ina;

	if (inet_pton(AF_INET, addr, &ina) != 1) {
		cfg_error(cfg, "invalid %s %s", what, addr);
		return -1;
	}

	return 0;
}

/* The last section of @opt, the one just parsed */
static cfg_t *last_sec(cfg_opt_t *opt)
{
	return cfg_opt_getnsec(opt, cfg_opt_size(opt) - 1);
}

static int is_set(cfg_t *sec, const char *name)
{
	return cfg_getopt(sec, name)->flags & CFGF_MODIFIED;
}

/* At most one of the options in the NULL terminated list */
static int valid_choice(cfg_t *cfg, cfg_t *sec, const char *name, ...)
{
	const char *first = NULL;
	va_list ap;

	va_start(ap, name);
	for (; name; name = va_arg(ap, const char *)) {
		if (!is_set(sec, name))
			continue;
		if (first) {
			cfg_error(cfg, "set %s or %s, not both", first, name);
			va_end(ap);
			return -1;
		}
		first = name;
	}
	va_end(ap);

	return 0;
}

static int valid_server(cfg_t *cfg, cfg_opt_t *opt)
{
	return valid_address(cfg, "server address", cfg_title(last_sec(opt)));
}

static int valid_option82(cfg_t *cfg, cfg_opt_t *opt)
{
	return valid_choice(cfg, last_sec(opt), "remote-id-type", "remote-id", "remote-id-hex", NULL);
}

static int valid_port_sec(cfg_t *cfg, cfg_opt_t *opt)
{
	return valid_choice(cfg, last_sec(opt), "circuit-id", "circuit-id-hex", NULL);
}

static int valid_interface(cfg_t *cfg, cfg_opt_t *opt)
{
	const char *giaddr = cfg_getstr(last_sec(opt), "giaddr");

	if (!strcmp(giaddr, "auto"))
		return 0;

	return valid_address(cfg, "giaddr", giaddr);
}

/* Text from @key, or bytes from @hexkey, both already validated */
static int read_id(cfg_t *sec, const char *key, const char *hexkey, char *buf)
{
	const char *val;
	int len;

	val = cfg_getstr(sec, key);
	if (val) {
		len = strlen(val);
		memcpy(buf, val, len);
		return len;
	}

	val = cfg_getstr(sec, hexkey);
	if (val)
		return parse_hex(val, buf, OPT82_ID_LEN);

	return 0;
}

static void read_option82(cfg_t *cfg, conf_t *conf)
{
	cfg_t *sec;
	int len;

	if (cfg_size(cfg, "option82") == 0)
		return;
	sec = cfg_getsec(cfg, "option82");

	conf->opt82.enabled = 1;
	conf->opt82.policy = cfg_getint(sec, "policy");
	conf->opt82.rid_type = cfg_getint(sec, "remote-id-type");

	len = read_id(sec, "remote-id", "remote-id-hex", conf->opt82.remote_id);
	if (len > 0) {
		conf->opt82.rid_type = RID_TYPE_MANUAL;
		conf->opt82.remote_id_len = len;
	}
}

static int read_servers(cfg_t *cfg, struct server_list *list)
{
	unsigned int i;

	for (i = 0; i < cfg_size(cfg, "server"); i++) {
		cfg_t *sec = cfg_getnsec(cfg, "server", i);
		conf_server_t *server;

		server = calloc(1, sizeof(*server));
		if (!server) {
			syslog2(LOG_ERR, "Failed allocating memory: %s", strerror(errno));
			return 1;
		}
		TAILQ_INSERT_TAIL(list, server, link);

		inet_pton(AF_INET, cfg_title(sec), &server->addr);
		server->port = cfg_getint(sec, "port");
	}

	return 0;
}

static conf_iface_t *add_iface(conf_group_t *group, int ifindex)
{
	conf_iface_t *iface;

	iface = calloc(1, sizeof(*iface));
	if (!iface) {
		syslog2(LOG_ERR, "Failed allocating memory: %s", strerror(errno));
		return NULL;
	}
	TAILQ_INSERT_TAIL(&group->iface_list, iface, link);
	iface->ifindex = ifindex;

	return iface;
}

static int read_ports(cfg_t *cfg, conf_group_t *group)
{
	unsigned int i;

	/* A routed port, or a bridge used as one */
	if (cfg_size(cfg, "port") == 0)
		return add_iface(group, group->ifindex) ? 0 : 1;

	for (i = 0; i < cfg_size(cfg, "port"); i++) {
		cfg_t *sec = cfg_getnsec(cfg, "port", i);
		conf_iface_t *iface;
		int ifindex;

		ifindex = if_nametoindex(cfg_title(sec));
		if (!ifindex) {
			syslog2(LOG_ERR, "Unknown interface %s", cfg_title(sec));
			return 1;
		}

		iface = add_iface(group, ifindex);
		if (!iface)
			return 1;
		iface->circuit_id_len = read_id(sec, "circuit-id", "circuit-id-hex", iface->circuit_id);
	}

	return 0;
}

/* First IPv4 address of @ifname */
static int find_giaddr(struct ifaddrs *ifap, const char *ifname, int *giaddr)
{
	struct ifaddrs *ifa;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (strcmp(ifa->ifa_name, ifname))
			continue;

		*giaddr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
		return 0;
	}

	return 1;
}

static int read_interface(cfg_t *sec, conf_t *conf, struct ifaddrs *ifap)
{
	const char *ifname = cfg_title(sec);
	const char *giaddr = cfg_getstr(sec, "giaddr");
	conf_group_t *group;

	group = calloc(1, sizeof(*group));
	if (!group) {
		syslog2(LOG_ERR, "Failed allocating memory: %s", strerror(errno));
		return 1;
	}
	TAILQ_INIT(&group->iface_list);
	TAILQ_INIT(&group->server_list);
	TAILQ_INSERT_TAIL(&conf->group_list, group, link);

	group->ifindex = if_nametoindex(ifname);
	if (!group->ifindex) {
		syslog2(LOG_ERR, "Unknown interface %s", ifname);
		return 1;
	}

	if (strcmp(giaddr, "auto"))
		inet_pton(AF_INET, giaddr, &group->giaddr);
	else if (find_giaddr(ifap, ifname, &group->giaddr)) {
		syslog2(LOG_ERR, "No IPv4 address on %s to use as giaddr", ifname);
		return 1;
	}

	if (read_ports(sec, group) || read_servers(sec, &group->server_list))
		return 1;

	if (TAILQ_EMPTY(&group->server_list) && TAILQ_EMPTY(&conf->server_list)) {
		syslog2(LOG_ERR, "No server for interface %s", ifname);
		return 1;
	}

	return 0;
}

static int read_interfaces(cfg_t *cfg, conf_t *conf)
{
	struct ifaddrs *ifap;
	unsigned int i;
	int rc = 0;

	if (cfg_size(cfg, "interface") == 0) {
		syslog2(LOG_ERR, "No interface to relay requests from");
		return 1;
	}

	if (getifaddrs(&ifap)) {
		syslog2(LOG_ERR, "Failed reading interface addresses: %s", strerror(errno));
		return 1;
	}

	for (i = 0; i < cfg_size(cfg, "interface") && !rc; i++)
		rc = read_interface(cfg_getnsec(cfg, "interface", i), conf, ifap);

	freeifaddrs(ifap);

	return rc;
}

/* A request to a gateway address would reach our own socket for replies */
static int is_giaddr(conf_t *conf, struct server_list *list)
{
	conf_server_t *server;
	conf_group_t *group;

	TAILQ_FOREACH(server, list, link) {
		TAILQ_FOREACH(group, &conf->group_list, link) {
			struct in_addr ina = { .s_addr = server->addr };

			if (server->addr != group->giaddr)
				continue;
			syslog2(LOG_ERR, "Server %s is the gateway address of an interface, use 127.0.0.1",
				inet_ntoa(ina));
			return 1;
		}
	}

	return 0;
}

static int check_servers(conf_t *conf)
{
	conf_group_t *group;

	if (is_giaddr(conf, &conf->server_list))
		return 1;
	TAILQ_FOREACH(group, &conf->group_list, link) {
		if (is_giaddr(conf, &group->server_list))
			return 1;
	}

	return 0;
}

conf_t *conf_read(const char *file)
{
	conf_t *conf;
	cfg_t *cfg;
	int rc = 1;

	conf = calloc(1, sizeof(*conf));
	if (!conf) {
		syslog2(LOG_ERR, "Failed allocating memory: %s", strerror(errno));
		return NULL;
	}
	TAILQ_INIT(&conf->server_list);
	TAILQ_INIT(&conf->group_list);

	cfg = cfg_init(opts, CFGF_NONE);
	if (!cfg) {
		syslog2(LOG_ERR, "Failed initializing configuration parser: %s", strerror(errno));
		free(conf);
		return NULL;
	}
	cfg_set_error_function(cfg, conf_error);
	cfg_set_validate_func(cfg, "udp-listen-port", valid_port);
	cfg_set_validate_func(cfg, "server", valid_server);
	cfg_set_validate_func(cfg, "server|port", valid_port);
	cfg_set_validate_func(cfg, "option82", valid_option82);
	cfg_set_validate_func(cfg, "option82|remote-id", valid_id);
	cfg_set_validate_func(cfg, "option82|remote-id-hex", valid_id);
	cfg_set_validate_func(cfg, "interface", valid_interface);
	cfg_set_validate_func(cfg, "interface|server", valid_server);
	cfg_set_validate_func(cfg, "interface|server|port", valid_port);
	cfg_set_validate_func(cfg, "interface|port", valid_port_sec);
	cfg_set_validate_func(cfg, "interface|port|circuit-id", valid_id);
	cfg_set_validate_func(cfg, "interface|port|circuit-id-hex", valid_id);

	switch (cfg_parse(cfg, file)) {
	case CFG_FILE_ERROR:
		syslog2(LOG_ERR, "Cannot read %s: %s", file, strerror(errno));
		goto done;
	case CFG_PARSE_ERROR:
		goto done;
	}

	conf->force_server_id = cfg_getbool(cfg, "force-server-identity");
	conf->udp_listen_port = cfg_getint(cfg, "udp-listen-port");
	read_option82(cfg, conf);

	if (read_servers(cfg, &conf->server_list) || read_interfaces(cfg, conf) || check_servers(conf))
		goto done;

	rc = 0;
done:
	cfg_free(cfg);
	if (rc) {
		conf_free(conf);
		return NULL;
	}

	return conf;
}

void conf_free(conf_t *conf)
{
	conf_group_t *group, *itemp;
	conf_server_t *server, *stemp;

	if (!conf)
		return;

	TAILQ_FOREACH_SAFE(group, &conf->group_list, link, itemp) {
		conf_iface_t *iface, *ptemp;

		TAILQ_FOREACH_SAFE(server, &group->server_list, link, stemp) {
			TAILQ_REMOVE(&group->server_list, server, link);
			free(server);
		}
		TAILQ_FOREACH_SAFE(iface, &group->iface_list, link, ptemp) {
			TAILQ_REMOVE(&group->iface_list, iface, link);
			free(iface);
		}
		TAILQ_REMOVE(&conf->group_list, group, link);
		free(group);
	}

	TAILQ_FOREACH_SAFE(server, &conf->server_list, link, stemp) {
		TAILQ_REMOVE(&conf->server_list, server, link);
		free(server);
	}

	free(conf);
}
