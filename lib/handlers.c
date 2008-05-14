/*
 * lib/handlers.c	default netlink message handlers
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation version 2.1
 *	of the License.
 *
 * Copyright (c) 2003-2008 Thomas Graf <tgraf@suug.ch>
 */

/**
 * @ingroup nl
 * @defgroup cb Callbacks/Customization
 * @brief
 *
 * Callbacks and overwriting capabilities are provided to take influence
 * in various control flows inside the library. All callbacks are packed
 * together in struct nl_cb which is then attached to a netlink socket or
 * passed on to the respective functions directly.
 *
 * Callbacks can control the flow of the underlying layer by returning
 * the appropriate error codes:
 * @code
 * Action ID        | Description
 * -----------------+-------------------------------------------------------
 * NL_OK       | Proceed with whatever comes next.
 * NL_SKIP          | Skip message currently being processed and continue
 *                  | with next message.
 * NL_STOP          | Stop parsing and discard all remaining messages in
 *                  | this set of messages.
 * @endcode
 *
 * All callbacks are optional and a default action is performed if no 
 * application specific implementation is provided:
 *
 * @code
 * Callback ID       | Default Return Value
 * ------------------+----------------------
 * NL_CB_VALID       | NL_OK
 * NL_CB_FINISH      | NL_STOP
 * NL_CB_OVERRUN     | NL_STOP
 * NL_CB_SKIPPED     | NL_SKIP
 * NL_CB_ACK         | NL_STOP
 * NL_CB_MSG_IN      | NL_OK
 * NL_CB_MSG_OUT     | NL_OK
 * NL_CB_INVALID     | NL_STOP
 * NL_CB_SEQ_CHECK   | NL_OK
 * NL_CB_SEND_ACK    | NL_OK
 *                   |
 * Error Callback    | NL_STOP
 * @endcode
 *
 * In order to simplify typical usages of the library, different sets of
 * default callback implementations exist:
 * @code
 * NL_CB_DEFAULT: No additional actions
 * NL_CB_VERBOSE: Automatically print warning and error messages to a file
 *                descriptor as appropriate. This is useful for CLI based
 *                applications.
 * NL_CB_DEBUG:   Print informal debugging information for each message
 *                received. This will result in every message beint sent or
 *                received to be printed to the screen in a decoded,
 *                human-readable format.
 * @endcode
 *
 * @par 1) Setting up a callback set
 * @code
 * // Allocate a callback set and initialize it to the verbose default set
 * struct nl_cb *cb = nl_cb_alloc(NL_CB_VERBOSE);
 *
 * // Modify the set to call my_func() for all valid messages
 * nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, my_func, NULL);
 *
 * // Set the error message handler to the verbose default implementation
 * // and direct it to print all errors to the given file descriptor.
 * FILE *file = fopen(...);
 * nl_cb_err(cb, NL_CB_VERBOSE, NULL, file);
 * @endcode
 * @{
 */

#include <netlink-local.h>
#include <netlink/netlink.h>
#include <netlink/utils.h>
#include <netlink/msg.h>
#include <netlink/handlers.h>

static void print_header_content(FILE *ofd, struct nlmsghdr *n)
{
	char flags[128];
	char type[32];
	
	fprintf(ofd, "type=%s length=%u flags=<%s> sequence-nr=%u pid=%u",
		nl_nlmsgtype2str(n->nlmsg_type, type, sizeof(type)),
		n->nlmsg_len, nl_nlmsg_flags2str(n->nlmsg_flags, flags,
		sizeof(flags)), n->nlmsg_seq, n->nlmsg_pid);
}

static int nl_valid_handler_verbose(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stdout;

	fprintf(ofd, "-- Warning: unhandled valid message: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");

	return NL_OK;
}

static int nl_invalid_handler_verbose(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Error: Invalid message: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");

	return NL_STOP;
}

static int nl_overrun_handler_verbose(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Error: Netlink Overrun: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");
	
	return NL_STOP;
}

static int nl_error_handler_verbose(struct sockaddr_nl *who,
				    struct nlmsgerr *e, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Error received: %s\n-- Original message: ",
		strerror(-e->error));
	print_header_content(ofd, &e->msg);
	fprintf(ofd, "\n");

	return -nl_syserr2nlerr(e->error);
}

static int nl_valid_handler_debug(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Debug: Unhandled Valid message: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");

	return NL_OK;
}

static int nl_finish_handler_debug(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Debug: End of multipart message block: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");
	
	return NL_STOP;
}

static int nl_msg_in_handler_debug(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Debug: Received Message:\n");
	nl_msg_dump(msg, ofd);
	
	return NL_OK;
}

static int nl_msg_out_handler_debug(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Debug: Sent Message:\n");
	nl_msg_dump(msg, ofd);

	return NL_OK;
}

static int nl_skipped_handler_debug(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Debug: Skipped message: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");

	return NL_SKIP;
}

static int nl_ack_handler_debug(struct nl_msg *msg, void *arg)
{
	FILE *ofd = arg ? arg : stderr;

	fprintf(ofd, "-- Debug: ACK: ");
	print_header_content(ofd, nlmsg_hdr(msg));
	fprintf(ofd, "\n");

	return NL_STOP;
}

static nl_recvmsg_msg_cb_t cb_def[NL_CB_TYPE_MAX+1][NL_CB_KIND_MAX+1] = {
	[NL_CB_VALID] = {
		[NL_CB_VERBOSE]	= nl_valid_handler_verbose,
		[NL_CB_DEBUG]	= nl_valid_handler_debug,
	},
	[NL_CB_FINISH] = {
		[NL_CB_DEBUG]	= nl_finish_handler_debug,
	},
	[NL_CB_INVALID] = {
		[NL_CB_VERBOSE]	= nl_invalid_handler_verbose,
		[NL_CB_DEBUG]	= nl_invalid_handler_verbose,
	},
	[NL_CB_MSG_IN] = {
		[NL_CB_DEBUG]	= nl_msg_in_handler_debug,
	},
	[NL_CB_MSG_OUT] = {
		[NL_CB_DEBUG]	= nl_msg_out_handler_debug,
	},
	[NL_CB_OVERRUN] = {
		[NL_CB_VERBOSE]	= nl_overrun_handler_verbose,
		[NL_CB_DEBUG]	= nl_overrun_handler_verbose,
	},
	[NL_CB_SKIPPED] = {
		[NL_CB_DEBUG]	= nl_skipped_handler_debug,
	},
	[NL_CB_ACK] = {
		[NL_CB_DEBUG]	= nl_ack_handler_debug,
	},
};

static nl_recvmsg_err_cb_t cb_err_def[NL_CB_KIND_MAX+1] = {
	[NL_CB_VERBOSE]	= nl_error_handler_verbose,
	[NL_CB_DEBUG]	= nl_error_handler_verbose,
};

/**
 * @name Callback Handle Management
 * @{
 */

/**
 * Allocate a new callback handle
 * @arg kind		callback kind to be used for initialization
 * @return Newly allocated callback handle or NULL
 */
struct nl_cb *nl_cb_alloc(enum nl_cb_kind kind)
{
	int i;
	struct nl_cb *cb;

	if (kind < 0 || kind > NL_CB_KIND_MAX)
		return NULL;

	cb = calloc(1, sizeof(*cb));
	if (!cb)
		return NULL;

	cb->cb_refcnt = 1;

	for (i = 0; i <= NL_CB_TYPE_MAX; i++)
		nl_cb_set(cb, i, kind, NULL, NULL);

	nl_cb_err(cb, kind, NULL, NULL);

	return cb;
}

/**
 * Clone an existing callback handle
 * @arg orig		original callback handle
 * @return Newly allocated callback handle being a duplicate of
 *         orig or NULL
 */
struct nl_cb *nl_cb_clone(struct nl_cb *orig)
{
	struct nl_cb *cb;
	
	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb)
		return NULL;

	memcpy(cb, orig, sizeof(*orig));
	cb->cb_refcnt = 1;

	return cb;
}

struct nl_cb *nl_cb_get(struct nl_cb *cb)
{
	cb->cb_refcnt++;

	return cb;
}

void nl_cb_put(struct nl_cb *cb)
{
	if (!cb)
		return;

	cb->cb_refcnt--;

	if (cb->cb_refcnt < 0)
		BUG();

	if (cb->cb_refcnt <= 0)
		free(cb);
}

/** @} */

/**
 * @name Callback Setup
 * @{
 */

/**
 * Set up a callback 
 * @arg cb		callback set
 * @arg type		callback to modify
 * @arg kind		kind of implementation
 * @arg func		callback function (NL_CB_CUSTOM)
 * @arg arg		argument passed to callback
 *
 * @return 0 on success or a negative error code
 */
int nl_cb_set(struct nl_cb *cb, enum nl_cb_type type, enum nl_cb_kind kind,
	      nl_recvmsg_msg_cb_t func, void *arg)
{
	if (type < 0 || type > NL_CB_TYPE_MAX)
		return -NLE_RANGE;

	if (kind < 0 || kind > NL_CB_KIND_MAX)
		return -NLE_RANGE;

	if (kind == NL_CB_CUSTOM) {
		cb->cb_set[type] = func;
		cb->cb_args[type] = arg;
	} else {
		cb->cb_set[type] = cb_def[type][kind];
		cb->cb_args[type] = arg;
	}

	return 0;
}

/**
 * Set up a all callbacks
 * @arg cb		callback set
 * @arg kind		kind of callback
 * @arg func		callback function
 * @arg arg		argument to be passwd to callback function
 *
 * @return 0 on success or a negative error code
 */
int nl_cb_set_all(struct nl_cb *cb, enum nl_cb_kind kind,
		  nl_recvmsg_msg_cb_t func, void *arg)
{
	int i, err;

	for (i = 0; i <= NL_CB_TYPE_MAX; i++) {
		err = nl_cb_set(cb, i, kind, func, arg);
		if (err < 0)
			return err;
	}

	return 0;
}

/**
 * Set up an error callback
 * @arg cb		callback set
 * @arg kind		kind of callback
 * @arg func		callback function
 * @arg arg		argument to be passed to callback function
 */
int nl_cb_err(struct nl_cb *cb, enum nl_cb_kind kind,
	      nl_recvmsg_err_cb_t func, void *arg)
{
	if (kind < 0 || kind > NL_CB_KIND_MAX)
		return -NLE_RANGE;

	if (kind == NL_CB_CUSTOM) {
		cb->cb_err = func;
		cb->cb_err_arg = arg;
	} else {
		cb->cb_err = cb_err_def[kind];
		cb->cb_err_arg = arg;
	}

	return 0;
}

/** @} */

/**
 * @name Overwriting
 * @{
 */

/**
 * Overwrite internal calls to nl_recvmsgs()
 * @arg cb		callback set
 * @arg func		replacement callback for nl_recvmsgs()
 */
void nl_cb_overwrite_recvmsgs(struct nl_cb *cb,
			      int (*func)(struct nl_handle *, struct nl_cb *))
{
	cb->cb_recvmsgs_ow = func;
}

/**
 * Overwrite internal calls to nl_recv()
 * @arg cb		callback set
 * @arg func		replacement callback for nl_recv()
 */
void nl_cb_overwrite_recv(struct nl_cb *cb,
			  int (*func)(struct nl_handle *, struct sockaddr_nl *,
				      unsigned char **, struct ucred **))
{
	cb->cb_recv_ow = func;
}

/**
 * Overwrite internal calls to nl_send()
 * @arg cb		callback set
 * @arg func		replacement callback for nl_send()
 */
void nl_cb_overwrite_send(struct nl_cb *cb,
			  int (*func)(struct nl_handle *, struct nl_msg *))
{
	cb->cb_send_ow = func;
}

/** @} */

/** @} */
