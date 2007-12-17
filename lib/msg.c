/*
 * lib/msg.c		Netlink Messages Interface
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation version 2.1
 *	of the License.
 *
 * Copyright (c) 2003-2006 Thomas Graf <tgraf@suug.ch>
 */

/**
 * @ingroup nl
 * @defgroup msg Messages
 * Netlink Message Construction/Parsing Interface
 * 
 * The following information is partly extracted from RFC3549
 * (ftp://ftp.rfc-editor.org/in-notes/rfc3549.txt)
 *
 * @par Message Format
 * Netlink messages consist of a byte stream with one or multiple
 * Netlink headers and an associated payload.  If the payload is too big
 * to fit into a single message it, can be split over multiple Netlink
 * messages, collectively called a multipart message.  For multipart
 * messages, the first and all following headers have the \c NLM_F_MULTI
 * Netlink header flag set, except for the last header which has the
 * Netlink header type \c NLMSG_DONE.
 *
 * @par
 * The Netlink message header (\link nlmsghdr struct nlmsghdr\endlink) is shown below.
 * @code   
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Length                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            Type              |           Flags              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Sequence Number                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Process ID (PID)                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * @endcode
 *
 * @par
 * The netlink message header and payload must be aligned properly:
 * @code
 *  <------- NLMSG_ALIGN(hlen) ------> <---- NLMSG_ALIGN(len) --->
 * +----------------------------+- - -+- - - - - - - - - - -+- - -+
 * |           Header           | Pad |       Payload       | Pad |
 * |      struct nlmsghdr       |     |                     |     |
 * +----------------------------+- - -+- - - - - - - - - - -+- - -+
 * @endcode
 * @par
 * Message Format:
 * @code
 *    <--- nlmsg_total_size(payload)  --->
 *    <-- nlmsg_msg_size(payload) ->
 *   +----------+- - -+-------------+- - -+-------- - -
 *   | nlmsghdr | Pad |   Payload   | Pad | nlmsghdr
 *   +----------+- - -+-------------+- - -+-------- - -
 *   nlmsg_data(nlh)---^                   ^
 *   nlmsg_next(nlh)-----------------------+
 * @endcode
 * @par
 * The payload may consist of arbitary data but may have strict
 * alignment and formatting rules depening on the specific netlink
 * families.
 * @par
 * @code
 *    <---------------------- nlmsg_len(nlh) --------------------->
 *    <------ hdrlen ------>       <- nlmsg_attrlen(nlh, hdrlen) ->
 *   +----------------------+- - -+--------------------------------+
 *   |     Family Header    | Pad |           Attributes           |
 *   +----------------------+- - -+--------------------------------+
 *   nlmsg_attrdata(nlh, hdrlen)---^
 * @endcode
 * @par The ACK Netlink Message
 * This message is actually used to denote both an ACK and a NACK.
 * Typically, the direction is from FEC to CPC (in response to an ACK
 * request message).  However, the CPC should be able to send ACKs back
 * to FEC when requested.
 * @code
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       Netlink message header                  |
 * |                       type = NLMSG_ERROR                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Error code                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       OLD Netlink message header              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * @endcode
 *
 * @par 1) Creating a new netlink message
 * @code
 * // Netlink messages can be allocated in various ways, you may
 * // allocate an empty netlink message by using nlmsg_alloc():
 * struct nl_msg *msg = nlmsg_alloc();
 *
 * // Very often, the message type and message flags are known
 * // at allocation time while the other fields are auto generated:
 * struct nl_msg *msg = nlmsg_alloc_simple(MY_TYPE, MY_FLAGS);
 *
 * // Alternatively an existing netlink message header can be used
 * // to inherit header values from:
 * struct nlmsghdr hdr = {
 * 	.nlmsg_type = MY_TYPE,
 * 	.nlmsg_flags = MY_FLAGS,
 * };
 * struct nl_msg *msg = nlmsg_inherit(&hdr);
 *
 * // Last but not least, netlink messages received from netlink sockets
 * // can be converted into nl_msg objects using nlmsg_convert():
 * struct nl_msg *msg = nlmsg_convert(nlh_from_nl_sock);
 *
 * // The header can later be retrieved with nlmsg_hdr() and changed again:
 * nlmsg_hdr(msg)->nlmsg_flags |= YET_ANOTHER_FLAG;
 * @endcode
 *
 * @par 2) Appending data to the message
 * @code
 * // Payload may be added to the message via nlmsg_append(). The fourth
 * // parameter specifies the number of alignment bytes the data should
 * // be padding with at the end. Common values are 0 to disable it or
 * // NLMSG_ALIGNTO to ensure proper netlink message padding.
 * nlmsg_append(msg, &mydata, sizeof(mydata), 0);
 *
 * // Sometimes it may be necessary to reserve room for data but defer
 * // the actual copying to a later point, nlmsg_reserve() can be used
 * // for this purpose:
 * void *data = nlmsg_reserve(msg, sizeof(mydata), NLMSG_ALIGNTO);
 * @endcode
 *
 * @par 3) Cleaning up message construction
 * @code
 * // After successful use of the message, the memory must be freed
 * // using nlmsg_free()
 * nlmsg_free(msg);
 * @endcode
 * 
 * @par 4) Parsing messages
 * @code
 * int n;
 * unsigned char *buf;
 * struct nlmsghdr *hdr;
 *
 * n = nl_recv(handle, NULL, &buf);
 * 
 * hdr = (struct nlmsghdr *) buf;
 * while (nlmsg_ok(hdr, n)) {
 * 	// Process message here...
 * 	hdr = nlmsg_next(hdr, &n);
 * }
 * @endcode
 * @{
 */

#include <netlink-local.h>
#include <netlink/netlink.h>
#include <netlink/utils.h>
#include <netlink/cache.h>
#include <netlink/attr.h>
#include <linux/socket.h>

/**
 * @name Size Calculations
 * @{
 */

/**
 * length of netlink message not including padding
 * @arg payload		length of message payload
 */
int nlmsg_msg_size(int payload)
{
	return NLMSG_HDRLEN + payload;
}

/**
 * length of netlink message including padding
 * @arg payload		length of message payload
 */
int nlmsg_total_size(int payload)
{
	return NLMSG_ALIGN(nlmsg_msg_size(payload));
}

/**
 * length of padding at the message's tail
 * @arg payload		length of message payload
 */
int nlmsg_padlen(int payload)
{
	return nlmsg_total_size(payload) - nlmsg_msg_size(payload);
}

/** @} */

/**
 * @name Payload Access
 * @{
 */

/**
 * head of message payload
 * @arg nlh		netlink messsage header
 */
void *nlmsg_data(const struct nlmsghdr *nlh)
{
	return (unsigned char *) nlh + NLMSG_HDRLEN;
}

void *nlmsg_tail(const struct nlmsghdr *nlh)
{
	return (unsigned char *) nlh + NLMSG_ALIGN(nlh->nlmsg_len);
}

/**
 * length of message payload
 * @arg nlh		netlink message header
 */
int nlmsg_len(const struct nlmsghdr *nlh)
{
	return nlh->nlmsg_len - NLMSG_HDRLEN;
}

/** @} */

/**
 * @name Attribute Access
 * @{
 */

/**
 * head of attributes data
 * @arg nlh		netlink message header
 * @arg hdrlen		length of family specific header
 */
struct nlattr *nlmsg_attrdata(const struct nlmsghdr *nlh, int hdrlen)
{
	unsigned char *data = nlmsg_data(nlh);
	return (struct nlattr *) (data + NLMSG_ALIGN(hdrlen));
}

/**
 * length of attributes data
 * @arg nlh		netlink message header
 * @arg hdrlen		length of family specific header
 */
int nlmsg_attrlen(const struct nlmsghdr *nlh, int hdrlen)
{
	return nlmsg_len(nlh) - NLMSG_ALIGN(hdrlen);
}

/** @} */

/**
 * @name Message Parsing
 * @{
 */

/**
 * check if the netlink message fits into the remaining bytes
 * @arg nlh		netlink message header
 * @arg remaining	number of bytes remaining in message stream
 */
int nlmsg_ok(const struct nlmsghdr *nlh, int remaining)
{
	return (remaining >= sizeof(struct nlmsghdr) &&
		nlh->nlmsg_len >= sizeof(struct nlmsghdr) &&
		nlh->nlmsg_len <= remaining);
}

/**
 * next netlink message in message stream
 * @arg nlh		netlink message header
 * @arg remaining	number of bytes remaining in message stream
 *
 * @returns the next netlink message in the message stream and
 * decrements remaining by the size of the current message.
 */
struct nlmsghdr *nlmsg_next(struct nlmsghdr *nlh, int *remaining)
{
	int totlen = NLMSG_ALIGN(nlh->nlmsg_len);

	*remaining -= totlen;

	return (struct nlmsghdr *) ((unsigned char *) nlh + totlen);
}

/**
 * parse attributes of a netlink message
 * @arg nlh		netlink message header
 * @arg hdrlen		length of family specific header
 * @arg tb		destination array with maxtype+1 elements
 * @arg maxtype		maximum attribute type to be expected
 * @arg policy		validation policy
 *
 * See nla_parse()
 */
int nlmsg_parse(struct nlmsghdr *nlh, int hdrlen, struct nlattr *tb[],
		int maxtype, struct nla_policy *policy)
{
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return nl_errno(EINVAL);

	return nla_parse(tb, maxtype, nlmsg_attrdata(nlh, hdrlen),
			 nlmsg_attrlen(nlh, hdrlen), policy);
}

/**
 * nlmsg_find_attr - find a specific attribute in a netlink message
 * @arg nlh		netlink message header
 * @arg hdrlen		length of familiy specific header
 * @arg attrtype	type of attribute to look for
 *
 * Returns the first attribute which matches the specified type.
 */
struct nlattr *nlmsg_find_attr(struct nlmsghdr *nlh, int hdrlen, int attrtype)
{
	return nla_find(nlmsg_attrdata(nlh, hdrlen),
			nlmsg_attrlen(nlh, hdrlen), attrtype);
}

/**
 * nlmsg_validate - validate a netlink message including attributes
 * @arg nlh		netlinket message header
 * @arg hdrlen		length of familiy specific header
 * @arg maxtype		maximum attribute type to be expected
 * @arg policy		validation policy
 */
int nlmsg_validate(struct nlmsghdr *nlh, int hdrlen, int maxtype,
		   struct nla_policy *policy)
{
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return nl_errno(EINVAL);

	return nla_validate(nlmsg_attrdata(nlh, hdrlen),
			    nlmsg_attrlen(nlh, hdrlen), maxtype, policy);
}

/** @} */

/**
 * @name Message Building/Access
 * @{
 */

static struct nl_msg *__nlmsg_alloc(size_t len)
{
	struct nl_msg *nm;

	nm = calloc(1, sizeof(*nm));
	if (!nm)
		goto errout;

	nm->nm_nlh = calloc(1, len);
	if (!nm->nm_nlh)
		goto errout;

	nm->nm_protocol = -1;
	nm->nm_nlh->nlmsg_len = len;

	NL_DBG(2, "msg %p: Allocated new message, nlmsg_len=%zu\n", nm, len);

	return nm;
errout:
	free(nm);
	nl_errno(ENOMEM);
	return NULL;
}

/**
 * Allocate a new netlink message
 *
 * Allocates a new netlink message without any further payload.
 *
 * @return Newly allocated netlink message or NULL.
 */
struct nl_msg *nlmsg_alloc(void)
{
	return __nlmsg_alloc(nlmsg_total_size(0));
}

/**
 * Allocate a new netlink message and inherit netlink message header
 * @arg hdr		Netlink message header template
 *
 * Allocates a new netlink message with a tailroom for the netlink
 * message header. If \a hdr is not NULL it will be used as a
 * template for the netlink message header, otherwise the header
 * is left blank.
 * 
 * @return Newly allocated netlink message or NULL
 */ 
struct nl_msg *nlmsg_inherit(struct nlmsghdr *hdr)
{
	struct nl_msg *nm;

	nm = nlmsg_alloc();
	if (nm && hdr) {
		struct nlmsghdr *new = nm->nm_nlh;

		new->nlmsg_type = hdr->nlmsg_type;
		new->nlmsg_flags = hdr->nlmsg_flags;
		new->nlmsg_seq = hdr->nlmsg_seq;
		new->nlmsg_pid = hdr->nlmsg_pid;
	}

	return nm;
}

/**
 * Allocate a new netlink message
 * @arg nlmsgtype	Netlink message type
 * @arg flags		Message flags.
 *
 * @return Newly allocated netlink message or NULL.
 */
struct nl_msg *nlmsg_alloc_simple(int nlmsgtype, int flags)
{
	struct nl_msg *msg;
	struct nlmsghdr nlh = {
		.nlmsg_type = nlmsgtype,
		.nlmsg_flags = flags,
	};

	msg = nlmsg_inherit(&nlh);
	if (msg)
		NL_DBG(2, "msg %p: Allocated new simple message\n", msg);

	return msg;
}

/**
 * Convert a netlink message received from a netlink socket to a nl_msg
 * @arg hdr		Netlink message received from netlink socket.
 *
 * Allocates a new netlink message and copies all of the data pointed to
 * by \a hdr into the new message object.
 *
 * @return Newly allocated netlink message or NULL.
 */
struct nl_msg *nlmsg_convert(struct nlmsghdr *hdr)
{
	struct nl_msg *nm;

	nm = __nlmsg_alloc(NLMSG_ALIGN(hdr->nlmsg_len));
	if (!nm)
		goto errout;

	memcpy(nm->nm_nlh, hdr, hdr->nlmsg_len);

	return nm;
errout:
	nlmsg_free(nm);
	return NULL;
}

/**
 * Reserve room for additional data in a netlink message
 * @arg n		netlink message
 * @arg len		length of additional data to reserve room for
 * @arg pad		number of bytes to align data to
 *
 * Reserves room for additional data at the tail of the an
 * existing netlink message. Eventual padding required will
 * be zeroed out.
 *
 * @note All existing pointers into the old data section may have
 *       become obsolete and illegal to reference after this call.
 *
 * @return Pointer to start of additional data tailroom or NULL.
 */
void *nlmsg_reserve(struct nl_msg *n, size_t len, int pad)
{
	void *tmp;
	size_t tlen;

	tlen = pad ? ((len + (pad - 1)) & ~(pad - 1)) : len;

	tmp = realloc(n->nm_nlh, n->nm_nlh->nlmsg_len + tlen);
	if (!tmp) {
		nl_errno(ENOMEM);
		return NULL;
	}

	n->nm_nlh = tmp;
	tmp += n->nm_nlh->nlmsg_len;
	n->nm_nlh->nlmsg_len += tlen;

	if (tlen > len)
		memset(tmp + len, 0, tlen - len);

	NL_DBG(2, "msg %p: Reserved %zu bytes, pad=%d, nlmsg_len=%d\n",
		  n, len, pad, n->nm_nlh->nlmsg_len);

	return tmp;
}

/**
 * Append data to tail of a netlink message
 * @arg n		netlink message
 * @arg data		data to add
 * @arg len		length of data
 * @arg pad		Number of bytes to align data to.
 *
 * Extends the netlink message as needed and appends the data of given
 * length to the message. 
 *
 * @note All existing pointers into the old data section may have
 *       become obsolete and illegal to reference after this call.
 *
 * @return 0 on success or a negative error code
 */
int nlmsg_append(struct nl_msg *n, void *data, size_t len, int pad)
{
	void *tmp;

	tmp = nlmsg_reserve(n, len, pad);
	if (tmp == NULL)
		return nl_errno(ENOMEM);

	memcpy(tmp, data, len);
	NL_DBG(2, "msg %p: Appended %zu bytes with padding %d\n", n, len, pad);

	return 0;
}

/**
 * Add a netlink message header to a netlink message
 * @arg n		netlink message
 * @arg pid		netlink process id or NL_AUTO_PID
 * @arg seq		sequence number of message or NL_AUTO_SEQ
 * @arg type		message type
 * @arg payload		length of message payload
 * @arg flags		message flags
 *
 * Adds or overwrites the netlink message header in an existing message
 * object. If \a payload is greater-than zero additional room will be
 * reserved, f.e. for family specific headers. It can be accesed via
 * nlmsg_data().
 *
 * @return A pointer to the netlink message header or NULL.
 */
struct nlmsghdr *nlmsg_put(struct nl_msg *n, uint32_t pid, uint32_t seq,
			   int type, int payload, int flags)
{
	struct nlmsghdr *nlh;

	if (n->nm_nlh->nlmsg_len < NLMSG_HDRLEN)
		BUG();

	nlh = (struct nlmsghdr *) n->nm_nlh;
	nlh->nlmsg_type = type;
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;

	NL_DBG(2, "msg %p: Added netlink header type=%d, flags=%d, pid=%d, "
		  "seq=%d\n", n, type, flags, pid, seq);

	if (payload > 0 &&
	    nlmsg_reserve(n, payload, NLMSG_ALIGNTO) == NULL)
		return NULL;

	return nlh;
}

/**
 * Return actual netlink message
 * @arg n		netlink message
 * 
 * Returns the actual netlink message casted to the type of the netlink
 * message header.
 * 
 * @return A pointer to the netlink message.
 */
struct nlmsghdr *nlmsg_hdr(struct nl_msg *n)
{
	return n->nm_nlh;
}

/**
 * Free a netlink message
 * @arg n		netlink message
 *
 * Destroys a netlink message and frees up all used memory.
 *
 * @pre The message must be unused.
 */
void nlmsg_free(struct nl_msg *n)
{
	if (!n)
		return;

	free(n->nm_nlh);
	free(n);
	NL_DBG(2, "msg %p: Freed\n", n);
}

/** @} */

/**
 * @name Attributes
 * @{
 */

void nlmsg_set_proto(struct nl_msg *msg, int protocol)
{
	msg->nm_protocol = protocol;
}

int nlmsg_get_proto(struct nl_msg *msg)
{
	return msg->nm_protocol;
}

void nlmsg_set_src(struct nl_msg *msg, struct sockaddr_nl *addr)
{
	memcpy(&msg->nm_src, addr, sizeof(*addr));
}

struct sockaddr_nl *nlmsg_get_src(struct nl_msg *msg)
{
	return &msg->nm_src;
}

void nlmsg_set_dst(struct nl_msg *msg, struct sockaddr_nl *addr)
{
	memcpy(&msg->nm_dst, addr, sizeof(*addr));
}

struct sockaddr_nl *nlmsg_get_dst(struct nl_msg *msg)
{
	return &msg->nm_dst;
}

void nlmsg_set_creds(struct nl_msg *msg, struct ucred *creds)
{
	memcpy(&msg->nm_creds, creds, sizeof(*creds));
	msg->nm_flags |= NL_MSG_CRED_PRESENT;
}

struct ucred *nlmsg_get_creds(struct nl_msg *msg)
{
	if (msg->nm_flags & NL_MSG_CRED_PRESENT)
		return &msg->nm_creds;
	return NULL;
}

/** @} */

/**
 * @name Netlink Message Type Translations
 * @{
 */

static struct trans_tbl nl_msgtypes[] = {
	__ADD(NLMSG_NOOP,NOOP)
	__ADD(NLMSG_ERROR,ERROR)
	__ADD(NLMSG_DONE,DONE)
	__ADD(NLMSG_OVERRUN,OVERRUN)
};

char *nl_nlmsgtype2str(int type, char *buf, size_t size)
{
	return __type2str(type, buf, size, nl_msgtypes,
			  ARRAY_SIZE(nl_msgtypes));
}

int nl_str2nlmsgtype(const char *name)
{
	return __str2type(name, nl_msgtypes, ARRAY_SIZE(nl_msgtypes));
}

/** @} */

/**
 * @name Netlink Message Flags Translations
 * @{
 */

char *nl_nlmsg_flags2str(int flags, char *buf, size_t len)
{
	memset(buf, 0, len);

#define PRINT_FLAG(f) \
	if (flags & NLM_F_##f) { \
		flags &= ~NLM_F_##f; \
		strncat(buf, #f, len - strlen(buf) - 1); \
		if (flags) \
			strncat(buf, ",", len - strlen(buf) - 1); \
	}
	
	PRINT_FLAG(REQUEST);
	PRINT_FLAG(MULTI);
	PRINT_FLAG(ACK);
	PRINT_FLAG(ECHO);
	PRINT_FLAG(ROOT);
	PRINT_FLAG(MATCH);
	PRINT_FLAG(ATOMIC);
	PRINT_FLAG(REPLACE);
	PRINT_FLAG(EXCL);
	PRINT_FLAG(CREATE);
	PRINT_FLAG(APPEND);

	if (flags) {
		char s[32];
		snprintf(s, sizeof(s), "0x%x", flags);
		strncat(buf, s, len - strlen(buf) - 1);
	}
#undef PRINT_FLAG

	return buf;
}

/** @} */

/**
 * @name Direct Parsing
 * @{
 */

/** @cond SKIP */
struct dp_xdata {
	void (*cb)(struct nl_object *, void *);
	void *arg;
};
/** @endcond */

static int parse_cb(struct nl_object *obj, struct nl_parser_param *p)
{
	struct dp_xdata *x = p->pp_arg;

	x->cb(obj, x->arg);
	return 0;
}

int nl_msg_parse(struct nl_msg *msg, void (*cb)(struct nl_object *, void *),
		 void *arg)
{
	struct nl_cache_ops *ops;
	struct nl_parser_param p = {
		.pp_cb = parse_cb
	};
	struct dp_xdata x = {
		.cb = cb,
		.arg = arg,
	};

	ops = nl_cache_ops_associate(nlmsg_get_proto(msg),
				     nlmsg_hdr(msg)->nlmsg_type);
	if (ops == NULL)
		return nl_error(ENOENT, "Unknown message type %d",
				nlmsg_hdr(msg)->nlmsg_type);
	p.pp_arg = &x;

	return nl_cache_parse(ops, NULL, nlmsg_hdr(msg), &p);
}

/** @} */

/**
 * @name Dumping
 * @{
 */

static void prefix_line(FILE *ofd, int prefix)
{
	int i;

	for (i = 0; i < prefix; i++)
		fprintf(ofd, "  ");
}

static inline void dump_hex(FILE *ofd, char *start, int len, int prefix)
{
	int i, a, c, limit;
	char ascii[21] = {0};

	limit = 18 - (prefix * 2);
	prefix_line(ofd, prefix);
	fprintf(ofd, "    ");

	for (i = 0, a = 0, c = 0; i < len; i++) {
		int v = *(uint8_t *) (start + i);

		fprintf(ofd, "%02x ", v);
		ascii[a++] = isprint(v) ? v : '.';

		if (c == limit-1) {
			fprintf(ofd, "%s\n", ascii);
			if (i < (len - 1)) {
				prefix_line(ofd, prefix);
				fprintf(ofd, "    ");
			}
			a = c = 0;
			memset(ascii, 0, sizeof(ascii));
		} else
			c++;
	}

	if (c != 0) {
		for (i = 0; i < (limit - c); i++)
			fprintf(ofd, "   ");
		fprintf(ofd, "%s\n", ascii);
	}
}

static void print_hdr(FILE *ofd, struct nl_msg *msg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nl_cache_ops *ops;
	struct nl_msgtype *mt;
	char buf[128];

	fprintf(ofd, "    .nlmsg_len = %d\n", nlh->nlmsg_len);

	ops = nl_cache_ops_associate(nlmsg_get_proto(msg), nlh->nlmsg_type);
	if (ops) {
		mt = nl_msgtype_lookup(ops, nlh->nlmsg_type);
		if (!mt)
			BUG();

		snprintf(buf, sizeof(buf), "%s::%s", ops->co_name, mt->mt_name);
	} else
		nl_nlmsgtype2str(nlh->nlmsg_type, buf, sizeof(buf));

	fprintf(ofd, "    .nlmsg_type = %d <%s>\n", nlh->nlmsg_type, buf);
	fprintf(ofd, "    .nlmsg_flags = %d <%s>\n", nlh->nlmsg_flags,
		nl_nlmsg_flags2str(nlh->nlmsg_flags, buf, sizeof(buf)));
	fprintf(ofd, "    .nlmsg_seq = %d\n", nlh->nlmsg_seq);
	fprintf(ofd, "    .nlmsg_pid = %d\n", nlh->nlmsg_pid);

}

static void dump_attrs(FILE *ofd, struct nlattr *attrs, int attrlen,
		       int prefix)
{
	int rem;
	struct nlattr *nla;

	nla_for_each_attr(nla, attrs, attrlen, rem) {
		int padlen, alen = nla_len(nla);

		prefix_line(ofd, prefix);
		fprintf(ofd, "  [ATTR %02d%s] %d octets\n", nla_type(nla),
			nla->nla_type & NLA_F_NESTED ? " NESTED" : "",
			alen);

		if (nla->nla_type & NLA_F_NESTED)
			dump_attrs(ofd, nla_data(nla), alen, prefix+1);
		else
			dump_hex(ofd, nla_data(nla), alen, prefix);

		padlen = nla_padlen(alen);
		if (padlen > 0) {
			prefix_line(ofd, prefix);
			fprintf(ofd, "  [PADDING] %d octets\n",
				padlen);
			dump_hex(ofd, nla_data(nla) + alen,
				 padlen, prefix);
		}
	}

	if (rem) {
		prefix_line(ofd, prefix);
		fprintf(ofd, "  [LEFTOVER] %d octets\n", rem);
	}
}

/**
 * Dump message in human readable format to file descriptor
 * @arg msg		Message to print
 * @arg ofd		File descriptor.
 */
void nl_msg_dump(struct nl_msg *msg, FILE *ofd)
{
	struct nlmsghdr *hdr = nlmsg_hdr(msg);
	
	fprintf(ofd, 
	"--------------------------   BEGIN NETLINK MESSAGE "
	"---------------------------\n");

	fprintf(ofd, "  [HEADER] %Zu octets\n", sizeof(struct nlmsghdr));
	print_hdr(ofd, msg);

	if (hdr->nlmsg_type == NLMSG_ERROR &&
	    hdr->nlmsg_len >= nlmsg_msg_size(sizeof(struct nlmsgerr))) {
		struct nl_msg *errmsg;
		struct nlmsgerr *err = nlmsg_data(hdr);

		fprintf(ofd, "  [ERRORMSG] %Zu octets\n", sizeof(*err));
		fprintf(ofd, "    .error = %d \"%s\"\n", err->error,
			strerror(-err->error));
		fprintf(ofd, "  [ORIGINAL MESSAGE] %Zu octets\n", sizeof(*hdr));

		errmsg = nlmsg_inherit(&err->msg);
		print_hdr(ofd, errmsg);
		nlmsg_free(errmsg);
	} else if (nlmsg_len(hdr) > 0) {
		struct nl_cache_ops *ops;
		int payloadlen = nlmsg_len(hdr);
		int attrlen = 0;

		ops = nl_cache_ops_associate(nlmsg_get_proto(msg),
					     hdr->nlmsg_type);
		if (ops) {
			attrlen = nlmsg_attrlen(hdr, ops->co_hdrsize);
			payloadlen -= attrlen;
		}

		fprintf(ofd, "  [PAYLOAD] %d octets\n", payloadlen);
		dump_hex(ofd, nlmsg_data(hdr), payloadlen, 0);

		if (attrlen) {
			struct nlattr *attrs;
			int attrlen;
			
			attrs = nlmsg_attrdata(hdr, ops->co_hdrsize);
			attrlen = nlmsg_attrlen(hdr, ops->co_hdrsize);
			dump_attrs(ofd, attrs, attrlen, 0);
		}
	}

	fprintf(ofd, 
	"---------------------------  END NETLINK MESSAGE   "
	"---------------------------\n");
}

/** @} */

/** @} */
