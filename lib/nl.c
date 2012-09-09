/*
 * lib/nl.c		Core Netlink Interface
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation version 2.1
 *	of the License.
 *
 * Copyright (c) 2003-2012 Thomas Graf <tgraf@suug.ch>
 */

/**
 * @defgroup core Core Library (libnl)
 *
 * Socket handling, connection management, sending and receiving of data,
 * message construction and parsing, object caching system, ...
 *
 * This is the API reference of the core library. It is not meant as a guide
 * but as a reference. Please refer to the core library guide for detailed
 * documentation on the library architecture and examples:
 *
 * * @ref_asciidoc{core,_,Netlink Core Library Development Guide}
 *
 *
 * @{
 */

#include <netlink-local.h>
#include <netlink/netlink.h>
#include <netlink/utils.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

/**
 * @defgroup core_types Data Types
 *
 * Core library data types
 * @{
 * @}
 *
 * @defgroup send_recv Send & Receive Data
 *
 * Connection management, sending & receiving of data
 *
 * Related sections in the development guide:
 * - @core_doc{core_send_recv, Sending & Receiving}
 * - @core_doc{core_sockets, Sockets}
 *
 * @{
 *
 * Header
 * ------
 * ~~~~{.c}
 * #include <netlink/netlink.h>
 * ~~~~
 */

/**
 * @name Connection Management
 * @{
 */

/**
 * Create and connect netlink socket.
 * @arg sk		Netlink socket.
 * @arg protocol	Netlink protocol to use.
 *
 * Creates a netlink socket using the specified protocol, binds the socket
 * and issues a connection attempt.
 *
 * This function fail if socket is already connected.
 *
 * @note SOCK_CLOEXEC is set on the socket if available.
 *
 * @return 0 on success or a negative error code.
 */
int nl_connect(struct nl_sock *sk, int protocol)
{
	int err, flags = 0;
	socklen_t addrlen;

#ifdef SOCK_CLOEXEC
	flags |= SOCK_CLOEXEC;
#endif

        if (sk->s_fd != -1)
                return -NLE_BAD_SOCK;

	sk->s_fd = socket(AF_NETLINK, SOCK_RAW | flags, protocol);
	if (sk->s_fd < 0) {
		err = -nl_syserr2nlerr(errno);
		goto errout;
	}

	if (!(sk->s_flags & NL_SOCK_BUFSIZE_SET)) {
		err = nl_socket_set_buffer_size(sk, 0, 0);
		if (err < 0)
			goto errout;
	}

	err = bind(sk->s_fd, (struct sockaddr*) &sk->s_local,
		   sizeof(sk->s_local));
	if (err < 0) {
		err = -nl_syserr2nlerr(errno);
		goto errout;
	}

	addrlen = sizeof(sk->s_local);
	err = getsockname(sk->s_fd, (struct sockaddr *) &sk->s_local,
			  &addrlen);
	if (err < 0) {
		err = -nl_syserr2nlerr(errno);
		goto errout;
	}

	if (addrlen != sizeof(sk->s_local)) {
		err = -NLE_NOADDR;
		goto errout;
	}

	if (sk->s_local.nl_family != AF_NETLINK) {
		err = -NLE_AF_NOSUPPORT;
		goto errout;
	}

	sk->s_proto = protocol;

	return 0;
errout:
        if (sk->s_fd != -1) {
    		close(sk->s_fd);
    		sk->s_fd = -1;
        }

	return err;
}

/**
 * Close/Disconnect netlink socket.
 * @arg sk		Netlink socket.
 */
void nl_close(struct nl_sock *sk)
{
	if (sk->s_fd >= 0) {
		close(sk->s_fd);
		sk->s_fd = -1;
	}

	sk->s_proto = 0;
}

/** @} */

/**
 * @name Send
 * @{
 */

/**
 * Send raw data over netlink socket.
 * @arg sk		Netlink socket.
 * @arg buf		Data buffer.
 * @arg size		Size of data buffer.
 * @return Number of characters written on success or a negative error code.
 */
int nl_sendto(struct nl_sock *sk, void *buf, size_t size)
{
	int ret;

	ret = sendto(sk->s_fd, buf, size, 0, (struct sockaddr *)
		     &sk->s_peer, sizeof(sk->s_peer));
	if (ret < 0)
		return -nl_syserr2nlerr(errno);

	return ret;
}

/**
 * Send netlink message with control over sendmsg() message header.
 * @arg sk		Netlink socket.
 * @arg msg		Netlink message to be sent.
 * @arg hdr		Sendmsg() message header.
 * @return Number of characters sent on sucess or a negative error code.
 */
int nl_sendmsg(struct nl_sock *sk, struct nl_msg *msg, struct msghdr *hdr)
{
	struct nl_cb *cb;
	int ret;

	nlmsg_set_src(msg, &sk->s_local);

	cb = sk->s_cb;
	if (cb->cb_set[NL_CB_MSG_OUT])
		if ((ret = nl_cb_call(cb, NL_CB_MSG_OUT, msg)) != NL_OK)
			return ret;

	ret = sendmsg(sk->s_fd, hdr, 0);
	if (ret < 0)
		return -nl_syserr2nlerr(errno);

	NL_DBG(4, "sent %d bytes\n", ret);
	return ret;
}


/**
 * Send netlink message.
 * @arg sk		Netlink socket.
 * @arg msg		Netlink message to be sent.
 * @arg iov		iovec to be sent.
 * @arg iovlen		number of struct iovec to be sent.
 * @see nl_sendmsg()
 * @return Number of characters sent on success or a negative error code.
 */
int nl_send_iovec(struct nl_sock *sk, struct nl_msg *msg, struct iovec *iov, unsigned iovlen)
{
	struct sockaddr_nl *dst;
	struct ucred *creds;
	struct msghdr hdr = {
		.msg_name = (void *) &sk->s_peer,
		.msg_namelen = sizeof(struct sockaddr_nl),
		.msg_iov = iov,
		.msg_iovlen = iovlen,
	};

	/* Overwrite destination if specified in the message itself, defaults
	 * to the peer address of the socket.
	 */
	dst = nlmsg_get_dst(msg);
	if (dst->nl_family == AF_NETLINK)
		hdr.msg_name = dst;

	/* Add credentials if present. */
	creds = nlmsg_get_creds(msg);
	if (creds != NULL) {
		char buf[CMSG_SPACE(sizeof(struct ucred))];
		struct cmsghdr *cmsg;

		hdr.msg_control = buf;
		hdr.msg_controllen = sizeof(buf);

		cmsg = CMSG_FIRSTHDR(&hdr);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_CREDENTIALS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
		memcpy(CMSG_DATA(cmsg), creds, sizeof(struct ucred));
	}

	return nl_sendmsg(sk, msg, &hdr);
}



/**
* Send netlink message.
* @arg sk		Netlink socket.
* @arg msg		Netlink message to be sent.
* @see nl_sendmsg()
* @return Number of characters sent on success or a negative error code.
*/
int nl_send(struct nl_sock *sk, struct nl_msg *msg)
{
	struct iovec iov = {
		.iov_base = (void *) nlmsg_hdr(msg),
		.iov_len = nlmsg_hdr(msg)->nlmsg_len,
	};

	return nl_send_iovec(sk, msg, &iov, 1);
}

void nl_complete_msg(struct nl_sock *sk, struct nl_msg *msg)
{
	struct nlmsghdr *nlh;

	nlh = nlmsg_hdr(msg);
	if (nlh->nlmsg_pid == NL_AUTO_PORT)
		nlh->nlmsg_pid = sk->s_local.nl_pid;

	if (nlh->nlmsg_seq == NL_AUTO_SEQ)
		nlh->nlmsg_seq = sk->s_seq_next++;

	if (msg->nm_protocol == -1)
		msg->nm_protocol = sk->s_proto;

	nlh->nlmsg_flags |= NLM_F_REQUEST;

	if (!(sk->s_flags & NL_NO_AUTO_ACK))
		nlh->nlmsg_flags |= NLM_F_ACK;
}

void nl_auto_complete(struct nl_sock *sk, struct nl_msg *msg)
{
	nl_complete_msg(sk, msg);
}

/**
 * Automatically complete and send a netlink message
 * @arg sk		Netlink socket.
 * @arg msg		Netlink message to be sent.
 *
 * This function takes a netlink message and passes it on to
 * nl_auto_complete() for completion.
 *
 * Checks the netlink message \c nlh for completness and extends it
 * as required before sending it out. Checked fields include pid,
 * sequence nr, and flags.
 *
 * @see nl_send()
 * @return Number of characters sent or a negative error code.
 */
int nl_send_auto(struct nl_sock *sk, struct nl_msg *msg)
{
	struct nl_cb *cb = sk->s_cb;

	nl_complete_msg(sk, msg);

	if (cb->cb_send_ow)
		return cb->cb_send_ow(sk, msg);
	else
		return nl_send(sk, msg);
}

int nl_send_auto_complete(struct nl_sock *sk, struct nl_msg *msg)
{
	return nl_send_auto(sk, msg);
}

/**
 * Send netlink message and wait for response (sync request-response)
 * @arg sk		Netlink socket
 * @arg msg		Netlink message to be sent
 *
 * This function takes a netlink message and sends it using nl_send_auto().
 * It will then wait for the response (ACK or error message) to be
 * received. Threfore this function will block until the operation has
 * been completed.
 *
 * @note Disabling auto-ack (nl_socket_disable_auto_ack()) will cause
 *       this function to return immediately after sending. In this case,
 *       it is the responsibility of the caller to handle any eventual
 *       error messages returned.
 *
 * @see nl_send_auto().
 *
 * @return 0 on success or a negative error code.
 */
int nl_send_sync(struct nl_sock *sk, struct nl_msg *msg)
{
	int err;

	err = nl_send_auto(sk, msg);
	nlmsg_free(msg);
	if (err < 0)
		return err;

	return wait_for_ack(sk);
}

/**
 * Send simple netlink message using nl_send_auto_complete()
 * @arg sk		Netlink socket.
 * @arg type		Netlink message type.
 * @arg flags		Netlink message flags.
 * @arg buf		Data buffer.
 * @arg size		Size of data buffer.
 *
 * Builds a netlink message with the specified type and flags and
 * appends the specified data as payload to the message.
 *
 * @see nl_send_auto_complete()
 * @return Number of characters sent on success or a negative error code.
 */
int nl_send_simple(struct nl_sock *sk, int type, int flags, void *buf,
		   size_t size)
{
	int err;
	struct nl_msg *msg;

	msg = nlmsg_alloc_simple(type, flags);
	if (!msg)
		return -NLE_NOMEM;

	if (buf && size) {
		err = nlmsg_append(msg, buf, size, NLMSG_ALIGNTO);
		if (err < 0)
			goto errout;
	}
	

	err = nl_send_auto_complete(sk, msg);
errout:
	nlmsg_free(msg);

	return err;
}

/** @} */

/**
 * @name Receive
 * @{
 */

/**
 * Receive data from netlink socket
 * @arg sk		Netlink socket.
 * @arg nla		Destination pointer for peer's netlink address.
 * @arg buf		Destination pointer for message content.
 * @arg creds		Destination pointer for credentials.
 *
 * Receives a netlink message, allocates a buffer in \c *buf and
 * stores the message content. The peer's netlink address is stored
 * in \c *nla. The caller is responsible for freeing the buffer allocated
 * in \c *buf if a positive value is returned.  Interrupted system calls
 * are handled by repeating the read. The input buffer size is determined
 * by peeking before the actual read is done.
 *
 * A non-blocking sockets causes the function to return immediately with
 * a return value of 0 if no data is available.
 *
 * @return Number of octets read, 0 on EOF or a negative error code.
 */
int nl_recv(struct nl_sock *sk, struct sockaddr_nl *nla,
	    unsigned char **buf, struct ucred **creds)
{
	ssize_t n;
	int flags = 0;
	static int page_size = 0;
	struct iovec iov;
	struct msghdr msg = {
		.msg_name = (void *) nla,
		.msg_namelen = sizeof(struct sockaddr_nl),
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};
	struct cmsghdr *cmsg;

	memset(nla, 0, sizeof(*nla));

	if (sk->s_flags & NL_MSG_PEEK)
		flags |= MSG_PEEK | MSG_TRUNC;

	if (page_size == 0)
		page_size = getpagesize();

	iov.iov_len = sk->s_bufsize ? : page_size;
	iov.iov_base = *buf = malloc(iov.iov_len);

	if (sk->s_flags & NL_SOCK_PASSCRED) {
		msg.msg_controllen = CMSG_SPACE(sizeof(struct ucred));
		msg.msg_control = calloc(1, msg.msg_controllen);
	}
retry:

	n = recvmsg(sk->s_fd, &msg, flags);
	if (!n)
		goto abort;

	if (n < 0) {

		if (errno == EINTR) {
			NL_DBG(3, "recvmsg() returned EINTR, retrying\n");
			goto retry;
		}

                if (errno == EAGAIN) {
			NL_DBG(3, "recvmsg() returned EAGAIN, aborting\n");
			goto abort;
		}

		free(msg.msg_control);
		free(*buf);
		return -nl_syserr2nlerr(errno);
	}

	if (msg.msg_flags & MSG_CTRUNC) {
		msg.msg_controllen *= 2;
		msg.msg_control = realloc(msg.msg_control, msg.msg_controllen);
		goto retry;
	}

        if (iov.iov_len < n || msg.msg_flags & MSG_TRUNC) {
		/* Provided buffer is not long enough, enlarge it
		 * to size of n (which should be total length of the message)
		 * and try again. */
		iov.iov_len = n;
		iov.iov_base = *buf = realloc(*buf, iov.iov_len);
		flags = 0;
		goto retry;
	}

        if (flags != 0) {
		/* Buffer is big enough, do the actual reading */
		flags = 0;
		goto retry;
	}

	if (msg.msg_namelen != sizeof(struct sockaddr_nl)) {
		free(msg.msg_control);
		free(*buf);
		return -NLE_NOADDR;
	}

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_CREDENTIALS) {
			if (creds) {
				*creds = calloc(1, sizeof(struct ucred));
				memcpy(*creds, CMSG_DATA(cmsg), sizeof(struct ucred));
			}
			break;
		}
	}

	free(msg.msg_control);
	return n;

abort:
	free(msg.msg_control);
	free(*buf);
	return 0;
}

/** @cond SKIP */
#define NL_CB_CALL(cb, type, msg) \
do { \
	err = nl_cb_call(cb, type, msg); \
	switch (err) { \
	case NL_OK: \
		err = 0; \
		break; \
	case NL_SKIP: \
		goto skip; \
	case NL_STOP: \
		goto stop; \
	default: \
		goto out; \
	} \
} while (0)
/** @endcond */

static int recvmsgs(struct nl_sock *sk, struct nl_cb *cb)
{
	int n, err = 0, multipart = 0, interrupted = 0, nrecv = 0;
	unsigned char *buf = NULL;
	struct nlmsghdr *hdr;

	/*
	nla is passed on to not only to nl_recv() but may also be passed
	to a function pointer provided by the caller which may or may not
	initialize the variable. Thomas Graf.
	*/
	struct sockaddr_nl nla = {0};
	struct nl_msg *msg = NULL;
	struct ucred *creds = NULL;

continue_reading:
	NL_DBG(3, "Attempting to read from %p\n", sk);
	if (cb->cb_recv_ow)
		n = cb->cb_recv_ow(sk, &nla, &buf, &creds);
	else
		n = nl_recv(sk, &nla, &buf, &creds);

	if (n <= 0)
		return n;

	NL_DBG(3, "recvmsgs(%p): Read %d bytes\n", sk, n);

	hdr = (struct nlmsghdr *) buf;
	while (nlmsg_ok(hdr, n)) {
		NL_DBG(3, "recvmsgs(%p): Processing valid message...\n", sk);

		nlmsg_free(msg);
		msg = nlmsg_convert(hdr);
		if (!msg) {
			err = -NLE_NOMEM;
			goto out;
		}

		nlmsg_set_proto(msg, sk->s_proto);
		nlmsg_set_src(msg, &nla);
		if (creds)
			nlmsg_set_creds(msg, creds);

		nrecv++;

		/* Raw callback is the first, it gives the most control
		 * to the user and he can do his very own parsing. */
		if (cb->cb_set[NL_CB_MSG_IN])
			NL_CB_CALL(cb, NL_CB_MSG_IN, msg);

		/* Sequence number checking. The check may be done by
		 * the user, otherwise a very simple check is applied
		 * enforcing strict ordering */
		if (cb->cb_set[NL_CB_SEQ_CHECK]) {
			NL_CB_CALL(cb, NL_CB_SEQ_CHECK, msg);

		/* Only do sequence checking if auto-ack mode is enabled */
		} else if (!(sk->s_flags & NL_NO_AUTO_ACK)) {
			if (hdr->nlmsg_seq != sk->s_seq_expect) {
				if (cb->cb_set[NL_CB_INVALID])
					NL_CB_CALL(cb, NL_CB_INVALID, msg);
				else {
					err = -NLE_SEQ_MISMATCH;
					goto out;
				}
			}
		}

		if (hdr->nlmsg_type == NLMSG_DONE ||
		    hdr->nlmsg_type == NLMSG_ERROR ||
		    hdr->nlmsg_type == NLMSG_NOOP ||
		    hdr->nlmsg_type == NLMSG_OVERRUN) {
			/* We can't check for !NLM_F_MULTI since some netlink
			 * users in the kernel are broken. */
			sk->s_seq_expect++;
			NL_DBG(3, "recvmsgs(%p): Increased expected " \
			       "sequence number to %d\n",
			       sk, sk->s_seq_expect);
		}

		if (hdr->nlmsg_flags & NLM_F_MULTI)
			multipart = 1;

		if (hdr->nlmsg_flags & NLM_F_DUMP_INTR) {
			if (cb->cb_set[NL_CB_DUMP_INTR])
				NL_CB_CALL(cb, NL_CB_DUMP_INTR, msg);
			else {
				/*
				 * We have to continue reading to clear
				 * all messages until a NLMSG_DONE is
				 * received and report the inconsistency.
				 */
				interrupted = 1;
			}
		}
	
		/* Other side wishes to see an ack for this message */
		if (hdr->nlmsg_flags & NLM_F_ACK) {
			if (cb->cb_set[NL_CB_SEND_ACK])
				NL_CB_CALL(cb, NL_CB_SEND_ACK, msg);
			else {
				/* FIXME: implement */
			}
		}

		/* messages terminates a multpart message, this is
		 * usually the end of a message and therefore we slip
		 * out of the loop by default. the user may overrule
		 * this action by skipping this packet. */
		if (hdr->nlmsg_type == NLMSG_DONE) {
			multipart = 0;
			if (cb->cb_set[NL_CB_FINISH])
				NL_CB_CALL(cb, NL_CB_FINISH, msg);
		}

		/* Message to be ignored, the default action is to
		 * skip this message if no callback is specified. The
		 * user may overrule this action by returning
		 * NL_PROCEED. */
		else if (hdr->nlmsg_type == NLMSG_NOOP) {
			if (cb->cb_set[NL_CB_SKIPPED])
				NL_CB_CALL(cb, NL_CB_SKIPPED, msg);
			else
				goto skip;
		}

		/* Data got lost, report back to user. The default action is to
		 * quit parsing. The user may overrule this action by retuning
		 * NL_SKIP or NL_PROCEED (dangerous) */
		else if (hdr->nlmsg_type == NLMSG_OVERRUN) {
			if (cb->cb_set[NL_CB_OVERRUN])
				NL_CB_CALL(cb, NL_CB_OVERRUN, msg);
			else {
				err = -NLE_MSG_OVERFLOW;
				goto out;
			}
		}

		/* Message carries a nlmsgerr */
		else if (hdr->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *e = nlmsg_data(hdr);

			if (hdr->nlmsg_len < nlmsg_size(sizeof(*e))) {
				/* Truncated error message, the default action
				 * is to stop parsing. The user may overrule
				 * this action by returning NL_SKIP or
				 * NL_PROCEED (dangerous) */
				if (cb->cb_set[NL_CB_INVALID])
					NL_CB_CALL(cb, NL_CB_INVALID, msg);
				else {
					err = -NLE_MSG_TRUNC;
					goto out;
				}
			} else if (e->error) {
				/* Error message reported back from kernel. */
				if (cb->cb_err) {
					err = cb->cb_err(&nla, e,
							   cb->cb_err_arg);
					if (err < 0)
						goto out;
					else if (err == NL_SKIP)
						goto skip;
					else if (err == NL_STOP) {
						err = -nl_syserr2nlerr(e->error);
						goto out;
					}
				} else {
					err = -nl_syserr2nlerr(e->error);
					goto out;
				}
			} else if (cb->cb_set[NL_CB_ACK])
				NL_CB_CALL(cb, NL_CB_ACK, msg);
		} else {
			/* Valid message (not checking for MULTIPART bit to
			 * get along with broken kernels. NL_SKIP has no
			 * effect on this.  */
			if (cb->cb_set[NL_CB_VALID])
				NL_CB_CALL(cb, NL_CB_VALID, msg);
		}
skip:
		err = 0;
		hdr = nlmsg_next(hdr, &n);
	}
	
	nlmsg_free(msg);
	free(buf);
	free(creds);
	buf = NULL;
	msg = NULL;
	creds = NULL;

	if (multipart) {
		/* Multipart message not yet complete, continue reading */
		goto continue_reading;
	}
stop:
	err = 0;
out:
	nlmsg_free(msg);
	free(buf);
	free(creds);

	if (interrupted)
		err = -NLE_DUMP_INTR;

	if (!err)
		err = nrecv;

	return err;
}

/**
 * Receive a set of messages from a netlink socket and report parsed messages
 * @arg sk		Netlink socket.
 * @arg cb		set of callbacks to control behaviour.
 *
 * This function is identical to nl_recvmsgs() to the point that it will
 * return the number of parsed messages instead of 0 on success.
 *
 * @see nl_recvmsgs()
 *
 * @return Number of received messages or a negative error code from nl_recv().
 */
int nl_recvmsgs_report(struct nl_sock *sk, struct nl_cb *cb)
{
	if (cb->cb_recvmsgs_ow)
		return cb->cb_recvmsgs_ow(sk, cb);
	else
		return recvmsgs(sk, cb);
}

/**
 * Receive a set of messages from a netlink socket.
 * @arg sk		Netlink socket.
 * @arg cb		set of callbacks to control behaviour.
 *
 * Repeatedly calls nl_recv() or the respective replacement if provided
 * by the application (see nl_cb_overwrite_recv()) and parses the
 * received data as netlink messages. Stops reading if one of the
 * callbacks returns NL_STOP or nl_recv returns either 0 or a negative error code.
 *
 * A non-blocking sockets causes the function to return immediately if
 * no data is available.
 *
 * @see nl_recvmsgs_report()
 *
 * @return 0 on success or a negative error code from nl_recv().
 */
int nl_recvmsgs(struct nl_sock *sk, struct nl_cb *cb)
{
	int err;

	if ((err = nl_recvmsgs_report(sk, cb)) > 0)
		err = 0;

	return err;
}

/**
 * Receive a set of message from a netlink socket using handlers in nl_sock.
 * @arg sk		Netlink socket.
 *
 * Calls nl_recvmsgs() with the handlers configured in the netlink socket.
 */
int nl_recvmsgs_default(struct nl_sock *sk)
{
	return nl_recvmsgs(sk, sk->s_cb);

}

static int ack_wait_handler(struct nl_msg *msg, void *arg)
{
	return NL_STOP;
}

/**
 * Wait for ACK.
 * @arg sk		Netlink socket.
 * @pre The netlink socket must be in blocking state.
 *
 * Waits until an ACK is received for the latest not yet acknowledged
 * netlink message.
 */
int nl_wait_for_ack(struct nl_sock *sk)
{
	int err;
	struct nl_cb *cb;

	cb = nl_cb_clone(sk->s_cb);
	if (cb == NULL)
		return -NLE_NOMEM;

	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_wait_handler, NULL);
	err = nl_recvmsgs(sk, cb);
	nl_cb_put(cb);

	return err;
}

/** @cond SKIP */
struct pickup_param
{
	int (*parser)(struct nl_cache_ops *, struct sockaddr_nl *,
		      struct nlmsghdr *, struct nl_parser_param *);
	struct nl_object *result;
};

static int __store_answer(struct nl_object *obj, struct nl_parser_param *p)
{
	struct pickup_param *pp = p->pp_arg;
	/*
	 * the parser will put() the object at the end, expecting the cache
	 * to take the reference.
	 */
	nl_object_get(obj);
	pp->result =  obj;

	return 0;
}

static int __pickup_answer(struct nl_msg *msg, void *arg)
{
	struct pickup_param *pp = arg;
	struct nl_parser_param parse_arg = {
		.pp_cb = __store_answer,
		.pp_arg = pp,
	};

	return pp->parser(NULL, &msg->nm_src, msg->nm_nlh, &parse_arg);
}

/** @endcond */

/**
 * Pickup netlink answer, parse is and return object
 * @arg sk		Netlink socket
 * @arg parser		Parser function to parse answer
 * @arg result		Result pointer to return parsed object
 *
 * @return 0 on success or a negative error code.
 */
int nl_pickup(struct nl_sock *sk,
	      int (*parser)(struct nl_cache_ops *, struct sockaddr_nl *,
			    struct nlmsghdr *, struct nl_parser_param *),
	      struct nl_object **result)
{
	struct nl_cb *cb;
	int err;
	struct pickup_param pp = {
		.parser = parser,
	};

	cb = nl_cb_clone(sk->s_cb);
	if (cb == NULL)
		return -NLE_NOMEM;

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, __pickup_answer, &pp);

	err = nl_recvmsgs(sk, cb);
	if (err < 0)
		goto errout;

	*result = pp.result;
errout:
	nl_cb_put(cb);

	return err;
}

/** @} */

/** @} */

/** @} */
