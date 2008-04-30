/*
 * src/utils.c		Utilities
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation version 2.1
 *	of the License.
 *
 * Copyright (c) 2003-2006 Thomas Graf <tgraf@suug.ch>
 */

#include "utils.h"

#include <stdlib.h>

void fatal(int err, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "Error: ");

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf(stderr, "\n");

	exit(err);
}

int nltool_init(int argc, char *argv[])
{
	return 0;
}

int nltool_connect(struct nl_handle *nlh, int protocol)
{
	int err;

	err = nl_connect(nlh, protocol);
	if (err < 0)
		fatal(err, "Unable to connect netlink socket: %s",
			nl_geterror());

	return err;
}

struct nl_handle *nltool_alloc_handle(void)
{
	struct nl_handle *sock;

	if (!(sock = nl_handle_alloc()))
		fatal(ENOBUFS, "Unable to allocate netlink socket");

	return sock;
}

struct nl_addr *nltool_addr_parse(const char *str)
{
	struct nl_addr *addr;

	addr = nl_addr_parse(str, AF_UNSPEC);
	if (!addr)
		fprintf(stderr, "Unable to parse address \"%s\": %s\n",
			str, nl_geterror());

	return addr;
}

int nltool_parse_dumptype(const char *str)
{
	if (!strcasecmp(str, "brief"))
		return NL_DUMP_BRIEF;
	else if (!strcasecmp(str, "details") || !strcasecmp(str, "detailed"))
		return NL_DUMP_FULL;
	else if (!strcasecmp(str, "stats"))
		return NL_DUMP_STATS;
	else if (!strcasecmp(str, "xml"))
		return NL_DUMP_XML;
	else if (!strcasecmp(str, "env"))
		return NL_DUMP_ENV;
	else {
		fprintf(stderr, "Invalid dump type \"%s\".\n", str);
		return -1;
	}
}

struct nl_cache *nltool_alloc_link_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_link_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve link cache: %s",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_addr_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_addr_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve address cache: %s\n",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_neigh_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_neigh_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve neighbour cache: %s\n",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_neightbl_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_neightbl_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve neighbour table "
				"cache: %s", nl_geterror());
	else
		nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_route_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_route_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve route cache: %s\n",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_rule_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_rule_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve rule cache: %s\n",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_qdisc_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = rtnl_qdisc_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve qdisc cache: %s\n",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}

struct nl_cache *nltool_alloc_genl_family_cache(struct nl_handle *nlh)
{
	struct nl_cache *cache;

	cache = genl_ctrl_alloc_cache(nlh);
	if (!cache)
		fatal(nl_get_errno(), "Unable to retrieve genl family cache: %s\n",
			nl_geterror());

	nl_cache_mngt_provide(cache);

	return cache;
}
