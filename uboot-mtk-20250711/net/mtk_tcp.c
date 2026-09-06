// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Simple TCP stack implementation
 */

#include <command.h>
#include <div64.h>
#include <errno.h>
#include <env.h>
#include <malloc.h>
#include <net.h>
#include <linux/list.h>
#include <asm/global_data.h>
/*
 * On MIPS, arch/mips/include/asm/regdef.h #defines sp as $29.
 * Undefine it before mtk_tcp.h is parsed so struct field 'sp' stays intact.
 */
#ifdef __mips__
#undef sp /* MIPS $29 register macro collides with struct field */
#endif
#include "mtk_tcp.h"
#include "arp.h"

DECLARE_GLOBAL_DATA_PTR;

/**
 * mtk_tcp_is_verbose() - check if verbose logging is enabled
 *
 * Returns true if verbose logging should be enabled. Priority:
 * 1. "mtk_tcp_verbose" environment variable (if set)
 * 2. CONFIG_MTK_TCP_VERBOSE Kconfig option
 *
 * Environment variable values: "1", "true", "yes", "on" (case-insensitive)
 */
static bool mtk_tcp_is_verbose(void)
{
	const char *val = env_get("mtk_tcp_verbose");

	if (val && val[0])
		return !strcmp(val, "1") ||
		       !strcasecmp(val, "true") ||
		       !strcasecmp(val, "yes") ||
		       !strcasecmp(val, "on");

	return IS_ENABLED(CONFIG_MTK_TCP_VERBOSE);
}

#define mtk_tcp_log(fmt, ...) \
	do { if (mtk_tcp_is_verbose()) printf(fmt, ##__VA_ARGS__); } while (0)

struct mtk_tcp_conn {
	struct list_head node;

	enum mtk_tcp_state status;

	struct in_addr ip_remote;
	__be16 port_remote;
	__be16 port_local;

	u32 peer_seq;
	u32 local_seq;
	u32 local_seq_last;
	u32 local_seq_acked;

	u32 ts;
	u32 ts_rexmit;
	u32 num_rexmit;

	u8 ethaddr[6];
	u16 mss;

	const u8 *tx;
	u32 txlen;

	mtk_tcp_conn_cb cb;
	int close_flag;
	int ack_flag;

	int rexmit_mode;
	int rexmit_wait_ack;

	int zw_mode;
	u32 peer_wnd;
	u32 peer_ws;

	u32 srtt;
	u32 rttvar;
	u32 rto;
	u32 ts_rtt;
	u32 ack_calc_rtt;

	void *pdata;
};

struct mtk_tcp_listen {
	struct list_head node;

	__be16 port;
	mtk_tcp_conn_cb cb;
};

static int mtk_tcp_send_packet_opt(struct mtk_tcp_conn *c, u16 flags, u32 seq, u32 ack,
			       void *opt, int opt_size, const void *payload,
			       int payload_len);
static int mtk_tcp_send_packet(struct mtk_tcp_conn *c, u16 flags, u32 seq, u32 ack,
			   const void *payload, int payload_len);
static int mtk_tcp_send_packet_ctrl(struct mtk_tcp_conn *c, u16 flags);
static void mtk_tcp_conn_finish(struct mtk_tcp_conn *c,
				enum mtk_tcp_cb_status status);

static LIST_HEAD(listen_head);
static LIST_HEAD(conn_head);

static int mtk_tcp_stop;
static u16 mtk_tcp_port_seq = 50000;

/*
 * Pending out-of-band abort request.  See mtk_tcp_abort_* in mtk_tcp.h.
 *
 * Only ever set while a console session is executing a command (guarded by
 * the session's own executing/busy flag), and only ever consumed by the
 * net_loop() of that very command, so it cannot abort unrelated traffic.
 */
static bool mtk_tcp_abort_flag;

void mtk_tcp_abort_request(void)
{
	mtk_tcp_abort_flag = true;
}

void mtk_tcp_abort_clear(void)
{
	mtk_tcp_abort_flag = false;
}

bool mtk_tcp_abort_pending(void)
{
	bool abort = mtk_tcp_abort_flag;

	mtk_tcp_abort_flag = false;
	return abort;
}

void mtk_tcp_start(void)
{
	/*
	 * A previous session may have left an ARP resolution pending, for
	 * example a connection that was destroyed while its peer MAC was
	 * still unknown.  The web failsafe pumps eth_rx() from its own poll
	 * loop and never runs arp_timeout_check(), so that state does not
	 * expire on its own: it would survive into the new session, keep
	 * pointing at the dead connection and make every later ARP reply
	 * write through a dangling pointer.  Start from a clean state.
	 */
	if (arp_is_waiting())
		arp_wait_clear();

	mtk_tcp_stop = 0;
	mtk_tcp_log("[MTK_TCP] mtk_tcp_start: stop=%d\n", mtk_tcp_stop);
}

static struct mtk_tcp_listen *mtk_tcp_listen_find(__be16 port)
{
	struct list_head *lh;
	struct mtk_tcp_listen *l;

	list_for_each(lh, &listen_head) {
		l = list_entry(lh, struct mtk_tcp_listen, node);
		if (l->port == port)
			return l;
	}

	return NULL;
}

int mtk_tcp_listen(__be16 port, mtk_tcp_conn_cb cb)
{
	struct mtk_tcp_listen *l;

	if (!port || !cb)
		return -EINVAL;

	if (mtk_tcp_listen_find(port))
		return -EADDRINUSE;

	l = malloc(sizeof(struct mtk_tcp_listen));
	if (!l)
		return -ENOMEM;

	l->port = port;
	l->cb = cb;

	list_add_tail(&l->node, &listen_head);

	mtk_tcp_log("[MTK_TCP] listen: port %d registered, stop=%d\n",
		    ntohs(port), mtk_tcp_stop);

	return 0;
}

int mtk_tcp_listen_stop(__be16 port)
{
	struct mtk_tcp_listen *l;

	l = mtk_tcp_listen_find(port);
	if (l) {
		mtk_tcp_log("[MTK_TCP] listen_stop: port %d removed\n", ntohs(port));
		list_del(&l->node);
		free(l);
		return 0;
	}

	return -1;
}

static struct mtk_tcp_conn *mtk_tcp_conn_find(__be32 remoteip, __be16 remoteport,
	__be16 localport)
{
	struct list_head *lh;
	struct mtk_tcp_conn *c;

	list_for_each(lh, &conn_head) {
		c = list_entry(lh, struct mtk_tcp_conn, node);

		if (c->ip_remote.s_addr == remoteip &&
			c->port_remote == remoteport &&
			c->port_local == localport)
			return c;
	}

	return NULL;
}

static void mtk_tcp_conn_del(struct mtk_tcp_conn *c)
{
	mtk_tcp_log("[MTK_TCP] conn_del: %pI4:%d -> %d, status=%d\n",
		    &c->ip_remote, ntohs(c->port_remote), ntohs(c->port_local),
		    c->status);
	list_del(&c->node);
	free(c);
}

static u16 mtk_tcp_checksum_compute(void *data, int len, __be32 sip, __be32 dip)
{
	int sum, oddbyte, phlen;
	u16 *ptr;

	struct {
		__be32 sip;
		__be32 dip;
		u8 zero;
		u8 prot;
		__be16 len;
	} mtk_tcp_pseudo_hdr = { sip, dip, 0, IPPROTO_TCP , htons(len & 0xffff) };

	sum = 0;

	phlen = sizeof(mtk_tcp_pseudo_hdr);
	ptr = (u16 *)&mtk_tcp_pseudo_hdr;

	while (phlen > 1) {
		sum += *ptr++;
		phlen -= 2;
	}

	ptr = (u16 *)data;

	while (len > 1) {
		sum += *ptr++;
		len -= 2;
	}

	if (len == 1) {
		oddbyte = 0;
		((u8 *)&oddbyte)[0] = *(u8 *)ptr;
		((u8 *)&oddbyte)[1] = 0;
		sum += oddbyte;
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	sum = ~sum & 0xffff;

	return sum;
}

static int mtk_tcp_seq_sub(ulong a, ulong b)
{
	if (time_after(a, b)) {
		if (a >= b)
			return a - b;
		else
			return a + (~b) + 1;
	} else {
		if (a <= b)
			return - (int)(b - a);
		else
			return - (int)(b + (~a) + 1);
	}
}

static void rtt_calc(struct mtk_tcp_conn *c)
{
	u32 rtt = get_timer(c->ts_rtt);
	u32 sub;

	if (rtt > c->srtt)
		sub = rtt - c->srtt;
	else
		sub = c->srtt - rtt;

	c->rttvar = c->rttvar - (c->rttvar >> MTK_TCP_RTT_ALPHA) +
		(sub >> MTK_TCP_RTT_ALPHA);
	c->srtt = c->srtt - (c->srtt >> MTK_TCP_RTT_BETA) + (rtt >> MTK_TCP_RTT_BETA);
	c->rto = c->srtt + max((u32)MTK_TCP_RTT_G, MTK_TCP_RTT_K * c->rttvar);
}

static void mtk_tcp_set_mss_opt(u8 *opt, u16 mss)
{
	opt[0] = MTK_TCP_OPT_MSS;
	opt[1] = 4;
	opt[2] = (mss >> 8) & 0xff;
	opt[3] = mss & 0xff;
	opt[4] = MTK_TCP_OPT_WS;
	opt[5] = 3;
	opt[6] = 0;
	opt[7] = MTK_TCP_OPT_EOL;
}

static struct mtk_tcp_conn *mtk_tcp_conn_create(__be32 remoteip, struct mtk_tcp_hdr *tcp,
					u32 tcphdr_len, u8 *ethaddr,
					mtk_tcp_conn_cb cb)
{
	struct mtk_tcp_conn *c, tmp_c;
	u8 *optend;
	u8 opt[8];
	u16 mss;
	u8 *o;

	mtk_tcp_log("[MTK_TCP] conn_create: SYN from %pI4:%d -> %d\n",
		    &remoteip, ntohs(tcp->src), ntohs(tcp->dst));

	c = malloc(sizeof(struct mtk_tcp_conn));
	if (!c) {
		/* malloc fail, use tmp_c as buffer to finish error handling */
		mtk_tcp_log("[MTK_TCP] conn_create: malloc failed!\n");
		c = &tmp_c;
	}

	memset(c, 0, sizeof(struct mtk_tcp_conn));

	list_add_tail(&c->node, &conn_head);

	c->status = SYN_RCVD;
	memcpy(c->ethaddr, ethaddr, 6);
	c->ip_remote.s_addr = remoteip;
	c->port_remote = tcp->src;
	c->port_local = tcp->dst;
	c->peer_seq = ntohl(net_read_u32(&tcp->seq));
	c->local_seq = (u32)lldiv(64000ULL * get_timer(0), 500);
	c->peer_wnd = ntohs(tcp->wnd);
	c->peer_ws = 0;
	c->mss = 1460;
	c->cb = cb;

	if (c == &tmp_c) {
		/* malloc fail, try to send MTK_TCP_RST to peer and return NULL */
		mtk_tcp_send_packet(c, MTK_TCP_RST | MTK_TCP_ACK, 1, c->peer_seq, NULL, 0);

		return NULL;
	}

	/* parse tcp options */
	if (tcphdr_len > sizeof(struct mtk_tcp_hdr)) {
		o = (u8 *)tcp + sizeof(struct mtk_tcp_hdr);
		optend = o + tcphdr_len - sizeof(struct mtk_tcp_hdr);

		while (o < optend) {
			if (*o == MTK_TCP_OPT_EOL)
				break;

			if (*o == MTK_TCP_OPT_NOP) {
				o++;
				continue;
			}

			switch (*o) {
			case MTK_TCP_OPT_MSS:
				mss = ((u16)o[2] << 8) | o[3];
				if (mss < c->mss)
					c->mss = mss;
				break;
			case MTK_TCP_OPT_WS:
				c->peer_ws = min((u32)o[2], 14U);
				break;
			}

			o += o[1];
		}
	}

	/* send first SYN ACK packet */
	mtk_tcp_set_mss_opt(opt, c->mss);

	mtk_tcp_send_packet_opt(c, MTK_TCP_SYN | MTK_TCP_ACK, c->local_seq, c->peer_seq + 1,
			    opt, sizeof(opt), NULL, 0);
	c->ts_rtt = get_timer(0);
	c->ts = get_timer(0);
	c->ts_rexmit = get_timer(0);
	c->num_rexmit = 0;

	mtk_tcp_log("[MTK_TCP] conn_create: SYN_RCVD -> %pI4:%d, seq=%u\n",
		    &c->ip_remote, ntohs(c->port_remote), c->local_seq);

	return c;
}

int mtk_tcp_connect(__be32 destip, __be16 port, mtk_tcp_conn_cb cb, void *pdata)
{
	struct mtk_tcp_conn *c;
	u16 localport;

	if (!destip || !port || !cb)
		return -EINVAL;

	c = calloc(1, sizeof(struct mtk_tcp_conn));
	if (!c)
		return -ENOMEM;

	list_add_tail(&c->node, &conn_head);

	if (mtk_tcp_port_seq >= 65535)
		mtk_tcp_port_seq = 50000;

	if (mtk_tcp_port_seq == 50000) {
		srand((u32)get_ticks());
		mtk_tcp_port_seq += (rand() * rand()) % 10000;
	}

	localport = mtk_tcp_port_seq++;

	c->status = CONNECT;
	memset(c->ethaddr, 0, 6);
	c->ip_remote.s_addr = destip;
	c->port_remote = port;
	c->port_local = htons(localport);
	c->peer_seq = 0;
	c->local_seq = (u32)lldiv(64000ULL * get_timer(0), 500);
	c->peer_wnd = 0;
	c->peer_ws = 0;
	c->mss = 1460;
	c->cb = cb;
	c->pdata = pdata;

	return 0;
}

static void mtk_tcp_conn_fill(struct mtk_tcp_conn *c, struct mtk_tcp_hdr *tcp,
			      u32 tcphdr_len, u8 *ethaddr)
{
	u8 *optend;
	u16 mss;
	u8 *o;

	c->peer_seq = ntohl(net_read_u32(&tcp->seq));
	c->peer_wnd = ntohs(tcp->wnd);

	/* parse tcp options */
	if (tcphdr_len > sizeof(struct mtk_tcp_hdr)) {
		o = (u8 *)tcp + sizeof(struct mtk_tcp_hdr);
		optend = o + tcphdr_len - sizeof(struct mtk_tcp_hdr);

		while (o < optend) {
			if (*o == MTK_TCP_OPT_EOL)
				break;

			if (*o == MTK_TCP_OPT_NOP) {
				o++;
				continue;
			}

			switch (*o) {
			case MTK_TCP_OPT_MSS:
				mss = ((u16)o[2] << 8) | o[3];
				if (mss < c->mss)
					c->mss = mss;
				break;
			case MTK_TCP_OPT_WS:
				c->peer_ws = min((u32)o[2], 14U);
				break;
			}

			o += o[1];
		}
	}

	c->ts_rtt = get_timer(0);
	c->ts = get_timer(0);
	c->ts_rexmit = get_timer(0);
	c->num_rexmit = 0;
}

bool mtk_receive_tcp(struct ip_hdr *ip, int len, struct ethernet_hdr *et)
{
	struct mtk_tcp_hdr *tcp;
	struct mtk_tcp_listen *l;
	struct mtk_tcp_conn *c;
	u32 iphdr_len, tcphdr_len, data_size;
	u8 *data;
	u32 seq, ack, tmp;
	u16 flags, chksum;
	struct mtk_tcp_cb_data cbd = {};

	iphdr_len = (ip->ip_hl_v & 0x0f) * 4;

	tcp = (struct mtk_tcp_hdr *)((uintptr_t)ip + iphdr_len);

	chksum = mtk_tcp_checksum_compute(tcp, len - iphdr_len,
		net_read_ip(&ip->ip_src).s_addr,
		net_read_ip(&ip->ip_dst).s_addr);
	if (chksum) {
		debug("bad tcp checksum\n");
		return false;
	}

	flags = ntohs(tcp->flags);
	tcphdr_len = ((flags >> MTK_TCP_HDR_LEN_SHIFT) & MTK_TCP_HDR_LEN_MASK) * 4;
	if (tcphdr_len < sizeof(struct mtk_tcp_hdr)) {
		debug("bad tcp header size\n");
		return false;
	}

	data = (u8 *)((uintptr_t)tcp + tcphdr_len);
	if (len < iphdr_len + tcphdr_len) {
		debug("incomplete tcp data\n");
		return false;
	}

	data_size = len - iphdr_len - tcphdr_len;

	mtk_tcp_log("[MTK_TCP] rx: %pI4:%d -> %pI4:%d flags=0x%03x len=%u stop=%d\n",
		    &ip->ip_src, ntohs(tcp->src),
		    &ip->ip_dst, ntohs(tcp->dst),
		    flags, data_size, mtk_tcp_stop);

	/* discard urgent data */
	if (flags & MTK_TCP_URG) {
		data += ntohs(tcp->urg);
		data_size -= ntohs(tcp->urg);
	}

	seq = ntohl(net_read_u32(&tcp->seq));
	ack = ntohl(net_read_u32(&tcp->ack));

	/* find existing connection */
	c = mtk_tcp_conn_find(net_read_ip(&ip->ip_src).s_addr, tcp->src, tcp->dst);

	/*
	 * If a SYN arrives on an existing connection (not in handshake
	 * state), the client is attempting to reconnect while the old
	 * connection is still tracked.  This commonly happens after a
	 * network command (tftp/ping via net_loop()) runs: eth_halt()
	 * briefly interrupts TCP traffic, the client's keep-alive or
	 * AJAX polling fails, and the browser sends a fresh SYN.
	 *
	 * RFC 793: "If the connection is in a synchronized state
	 * (ESTABLISHED, FIN-WAIT-1, FIN-WAIT-2, CLOSE-WAIT, CLOSING,
	 * LAST-ACK, TIME-WAIT), any unacceptable segment (out of
	 * window, etc.) elicits ... a reset."
	 *
	 * We send RST on the old connection, delete it, and fall
	 * through to create a new one for the incoming SYN.
	 */
	if (c && (flags & MTK_TCP_FLAG_MASK) == MTK_TCP_SYN &&
	    c->status != SYN_SENT && c->status != SYN_RCVD) {
		mtk_tcp_log("[MTK_TCP] rx: SYN on existing conn %pI4:%d status=%d, resetting\n",
			    &c->ip_remote, ntohs(c->port_remote), c->status);
		mtk_tcp_send_packet(c, MTK_TCP_RST | MTK_TCP_ACK,
				    c->local_seq, c->peer_seq, NULL, 0);
		mtk_tcp_conn_del(c);
		c = NULL;
	}

	if (!c) {
		/* whether to create new connection */
		if ((flags & MTK_TCP_FLAG_MASK) == MTK_TCP_SYN) {
			if ((l = mtk_tcp_listen_find(tcp->dst))) {
				if (mtk_tcp_stop) {
					mtk_tcp_log("[MTK_TCP] rx: SYN rejected, stop=%d\n",
						    mtk_tcp_stop);
					return false;
				}

				/* create new connection */
				mtk_tcp_conn_create(
					net_read_ip(&ip->ip_src).s_addr,
					tcp, tcphdr_len, et->et_src, l->cb);
				return true;
			}
		}

		return false;
	}

	/* Prepare for callback data */
	cbd.conn = c;
	cbd.sip = c->ip_remote.s_addr;
	cbd.sp = tcp->src;
	cbd.dp = tcp->dst;
	cbd.pdata = c->pdata;

	if (flags & MTK_TCP_RST) {
		mtk_tcp_log("[MTK_TCP] rx: RST from %pI4:%d, status=%d\n",
			    &c->ip_remote, ntohs(c->port_remote), c->status);
		if (c->status != SYN_RCVD) {
			/* The peer has reset the connection */
			mtk_tcp_conn_finish(c, MTK_TCP_CB_REMOTE_CLOSED);
			return true;
		}

		mtk_tcp_conn_del(c);
		return true;
	}

	if (!(flags & MTK_TCP_ACK)) {
		switch (c->status) {
		case FIN_WAIT_1:
		case FIN_WAIT_2:
			break;
		default:
			/* Needs ACK bit */
			return true;
		}
	}

	switch (c->status) {
	case SYN_SENT:
		if (!(flags & MTK_TCP_SYN)) {
			mtk_tcp_send_packet(c, MTK_TCP_RST, c->local_seq,
					    c->peer_seq, NULL, 0);

			cbd.status = MTK_TCP_CB_CLOSED;
			assert((size_t)c->cb > gd->ram_base);
			c->cb(&cbd);

			mtk_tcp_conn_del(c);
			break;
		}

		mtk_tcp_conn_fill(c, tcp, tcphdr_len, et->et_src);

		/* fall through */
	case SYN_RCVD:
		/* Calculate first RTO */
		c->srtt = get_timer(c->ts_rtt);
		c->rttvar = c->srtt / 2;
		c->rto = c->srtt + max((u32)MTK_TCP_RTT_G,
			MTK_TCP_RTT_K * c->rttvar);

		c->peer_seq++;
		c->local_seq++;
		c->local_seq_last = c->local_seq;
		c->local_seq_acked = c->local_seq;

		if (c->status == SYN_SENT) {
			mtk_tcp_send_packet(c, MTK_TCP_ACK, c->local_seq,
					    c->peer_seq, NULL, 0);
		}

		mtk_tcp_log("[MTK_TCP] state: %pI4:%d %s -> ESTABLISHED\n",
			    &c->ip_remote, ntohs(c->port_remote),
			    c->status == SYN_SENT ? "SYN_SENT" : "SYN_RCVD");
		c->status = ESTABLISHED;

		cbd.status = MTK_TCP_CB_NEW_CONN;
		assert((size_t)c->cb > gd->ram_base);
		c->cb(&cbd);

		if (!data_size)
			break;

		/* If there is incoming data, fall through */
	case ESTABLISHED:
		/* Update window */
		tmp = ntohs(tcp->wnd) << c->peer_ws;
		if (tmp > c->mss && tmp > c->peer_wnd)
			c->zw_mode = 0;
		c->peer_wnd = tmp;

		if (mtk_tcp_seq_sub(seq, c->peer_seq) < 0) {
			if (mtk_tcp_seq_sub(seq + data_size, c->peer_seq) > 0) {
				/*
				 * This packet contains payload of which we
				 * have partially ACKed.
				 * Extract only the part we've not ACKed.
				 */
				tmp = seq + data_size - c->peer_seq;
				data += data_size - tmp;
				data_size = tmp;
			} else {
				/*
				 * Totally duplicated packet.
				 * Discard the payload and send an ACK to
				 * indicate our current ACK number
				 */
				c->ack_flag++;
				data_size = 0;
			}
		} else if (mtk_tcp_seq_sub(seq, c->peer_seq) > 0) {
			/*
			 * Incoming packet loss.
			 * Discard the payload and send an ACK to request
			 * retransmission.
			 */
			data_size = 0;
			c->ack_flag++;
		}

		if (mtk_tcp_seq_sub(ack, c->local_seq_acked) > 0) {
			/* clear retransmission state */
			c->rexmit_mode = 0;
			c->rexmit_wait_ack = 0;

			/* The peer has ACKed new data */
			c->local_seq_acked = ack;

			if (mtk_tcp_seq_sub(ack, c->ack_calc_rtt) >= 0 &&
				c->ack_calc_rtt) {
				/* Calculate new RTO */
				c->ack_calc_rtt = 0;
				rtt_calc(c);
			}

			if (mtk_tcp_seq_sub(c->local_seq_acked,
				c->local_seq_last) == c->txlen) {
				/* Current data chunk has been fully sent */
				c->tx = NULL;
				c->txlen = 0;
				cbd.status = MTK_TCP_CB_DATA_SENT;
				assert((size_t) c->cb > gd->ram_base);
				c->cb(&cbd);
			}
		} else {
			/*
			 * If the peer ACKed the data chunk we've already
			 * fully sent, just ignore it
			 */
			if (mtk_tcp_seq_sub(ack, c->local_seq_last) < 0)
				break;

			/* Currently no more unACKed data */
			if (c->local_seq == c->local_seq_acked)
				goto check_new_data;

			/*
			 * Record the transmission position the peer required,
			 * and enter the retransmission mode
			 */
			c->local_seq_acked = ack;
			c->rexmit_mode = 1;
		}

	check_new_data:
		if (data_size) {
			/*
			 * We have new data received
			 * Increase the next expected peer SEQ number
			 * Send an ACK to acknowledge the peer
			 */
			c->peer_seq += data_size;
			c->ack_flag++;

			cbd.status = MTK_TCP_CB_DATA_RCVD;
			cbd.data = data;
			cbd.datalen = data_size;
			assert((size_t)c->cb > gd->ram_base);
			c->cb(&cbd);
		}

		if (flags & MTK_TCP_FIN) {
			/* The peer is closing the connection */
			mtk_tcp_log("[MTK_TCP] rx: FIN from %pI4:%d, -> CLOSE_WAIT\n",
				    &c->ip_remote, ntohs(c->port_remote));
			c->status = CLOSE_WAIT;
			cbd.status = MTK_TCP_CB_REMOTE_CLOSING;
			assert((size_t)c->cb > gd->ram_base);
			c->cb(&cbd);
		}

		break;
	case LAST_ACK:
		if (mtk_tcp_seq_sub(ack, c->local_seq + 1))
			return true;

		mtk_tcp_log("[MTK_TCP] rx: LAST_ACK done for %pI4:%d, -> CLOSED\n",
			    &c->ip_remote, ntohs(c->port_remote));
		c->status = CLOSE_WAIT;
		mtk_tcp_conn_finish(c, MTK_TCP_CB_REMOTE_CLOSED);
		break;
	case FIN_WAIT_1:
		if (flags & MTK_TCP_ACK) {
			if (mtk_tcp_seq_sub(ack, c->local_seq + 1))
				return true;
		}

		if ((flags & (MTK_TCP_ACK | MTK_TCP_FIN)) == (MTK_TCP_ACK | MTK_TCP_FIN)) {
			mtk_tcp_log("[MTK_TCP] rx: FIN_WAIT_1 + ACK+FIN -> TIME_WAIT\n");
			c->status = TIME_WAIT;
			c->local_seq++;
		} else if (flags & MTK_TCP_ACK) {
			mtk_tcp_log("[MTK_TCP] rx: FIN_WAIT_1 + ACK -> FIN_WAIT_2\n");
			c->status = FIN_WAIT_2;
			c->local_seq++;
		} else if (flags & MTK_TCP_FIN) {
			mtk_tcp_log("[MTK_TCP] rx: FIN_WAIT_1 + FIN -> CLOSING\n");
			c->status = CLOSING;
		}
		break;
	case FIN_WAIT_2:
		if (flags & MTK_TCP_FIN) {
			mtk_tcp_log("[MTK_TCP] rx: FIN_WAIT_2 + FIN -> TIME_WAIT\n");
			c->status = TIME_WAIT;
		}
		break;
	case CLOSING:
		if (mtk_tcp_seq_sub(ack, c->local_seq + 1))
			return true;

		mtk_tcp_log("[MTK_TCP] rx: CLOSING + ACK -> TIME_WAIT\n");
		c->status = TIME_WAIT;
		c->local_seq++;
		break;
	default:
		break;
	}

	return true;
}

static void mtk_tcp_rexmit_init(struct mtk_tcp_conn *c)
{
	c->ts_rexmit = get_timer(0);
	c->ts = get_timer(0);
	c->num_rexmit = 0;
}

static void mtk_tcp_rexmit_reset(struct mtk_tcp_conn *c)
{
	c->ts = get_timer(0);
	c->num_rexmit++;
}

static int mtk_tcp_rexmit_check(struct mtk_tcp_conn *c, struct mtk_tcp_cb_data *cbd)
{
	ulong curts, timeout;

	if (get_timer(c->ts_rexmit) > MTK_TCP_REXMIT_MAX_CONN_DELAY) {
		/*
		* Reached maximum retransmission count,
		* resetting the connection
		*/
		mtk_tcp_send_packet(c, MTK_TCP_RST | MTK_TCP_ACK, c->local_seq,
				c->peer_seq, NULL, 0);

		/* Callback if the connection has established */
		switch (c->status) {
		case ESTABLISHED:
			cbd->status = MTK_TCP_CB_REMOTE_CLOSED;
			cbd->timedout = true;
			assert((size_t)c->cb > gd->ram_base);
			c->cb(cbd);
			break;
		case FIN_WAIT_1:
		case FIN_WAIT_2:
		case CLOSING:
		case CLOSE_WAIT:
			cbd->status = MTK_TCP_CB_CLOSED;
			assert((size_t)c->cb > gd->ram_base);
			c->cb(cbd);
			break;
		default:;
		}

		mtk_tcp_conn_del(c);
		return -1;
	}

	/* Calculate RTO * 2^N */
	timeout = c->rto << c->num_rexmit;
	timeout = min(timeout, (ulong)MTK_TCP_REXMIT_MAX_SEG_DELAY);
	timeout += c->ts;

	curts = get_timer(0);

	if (time_after(curts, timeout))
		/* Timed out, retransmission needed */
		return 1;

	return 0;
}

static void mtk_tcp_connect_rexmit_reset(struct mtk_tcp_conn *c)
{
	c->ts = get_timer(0);
	c->num_rexmit++;
}

static int mtk_tcp_connect_rexmit_check(struct mtk_tcp_conn *c,
					struct mtk_tcp_cb_data *cbd)
{
	ulong curts, timeout;

	if (get_timer(c->ts_rexmit) > MTK_TCP_REXMIT_MAX_CONN_DELAY) {
		/*
		* Reached maximum retransmission count,
		* resetting the connection
		*/
		mtk_tcp_send_packet(c, MTK_TCP_RST, c->local_seq, c->peer_seq,
				    NULL, 0);

		cbd->status = MTK_TCP_CB_REMOTE_CLOSED;
		cbd->timedout = true;
		assert((size_t)c->cb > gd->ram_base);
		c->cb(cbd);

		mtk_tcp_conn_del(c);
		return -1;
	}

	timeout = c->ts + (MTK_TCP_CONNECT_INIT_DELAY << c->num_rexmit);

	curts = get_timer(0);

	if (time_after(curts, timeout))
		/* Timed out, retransmission needed */
		return 1;

	return 0;
}

static void mtk_tcp_conn_check(struct mtk_tcp_conn *c)
{
	const u8 *data = NULL;
	u32 datalen = 0;
	u8 flags = MTK_TCP_ACK;
	u32 sendseq;
	u32 datalen_sent, datalen_acked;
	u8 opt[8];
	struct mtk_tcp_cb_data cbd = {};

	/* Update timer */
	get_timer(0);

	cbd.conn = c;
	cbd.sip = c->ip_remote.s_addr;
	cbd.sp = c->port_remote;
	cbd.dp = c->port_local;
	cbd.pdata = c->pdata;

	switch (c->status) {
	case CONNECT:
		/* send first SYN packet */
		mtk_tcp_set_mss_opt(opt, c->mss);

		mtk_tcp_send_packet_opt(c, MTK_TCP_SYN, c->local_seq,
					c->peer_seq, opt, sizeof(opt), NULL, 0);

		c->ts = get_timer(0);
		c->ts_rexmit = get_timer(0);
		c->num_rexmit = 0;

		c->status = SYN_SENT;
		break;

	case SYN_SENT:
		switch (mtk_tcp_connect_rexmit_check(c, &cbd)) {
		case -1:
			return;
		case 1:
			mtk_tcp_set_mss_opt(opt, c->mss);
			mtk_tcp_send_packet_opt(c, MTK_TCP_SYN, c->local_seq,
						c->peer_seq, opt, sizeof(opt),
						NULL, 0);
			mtk_tcp_connect_rexmit_reset(c);
		}

		break;

	case SYN_RCVD:
		switch (mtk_tcp_rexmit_check(c, &cbd)) {
		case -1:
			return;
		case 1:
			mtk_tcp_set_mss_opt(opt, c->mss);
			mtk_tcp_send_packet_opt(c, MTK_TCP_SYN | MTK_TCP_ACK,
					    c->local_seq, c->peer_seq + 1,
					    opt, sizeof(opt), NULL, 0);
			mtk_tcp_rexmit_reset(c);
		}

		break;
	case ESTABLISHED:
		sendseq = c->local_seq;

		datalen_sent = mtk_tcp_seq_sub(c->local_seq, c->local_seq_last);
		datalen_acked = mtk_tcp_seq_sub(c->local_seq_acked,
					    c->local_seq_last);

		if (!c->rexmit_mode) {
			if (c->txlen > datalen_sent) {
				/* There is still data to be sent */
				data = c->tx + datalen_sent;
				datalen = min(c->txlen - datalen_sent,
					      (u32)c->mss);

				if (datalen >= c->peer_wnd) {
					/* The peer may be zero-window */
					if (!c->zw_mode)
						mtk_tcp_rexmit_init(c);
					/* Enter zero-window mode */
					c->zw_mode = 1;
					/* And send no data */
					datalen = 0;
				} else {
					c->local_seq += datalen;
					c->peer_wnd -= datalen;
					c->ack_flag++;
					if (datalen == c->txlen - datalen_sent)
						flags |= MTK_TCP_PSH;
					mtk_tcp_rexmit_init(c);
				}
			} else if (c->txlen > datalen_acked) {
				/* There is data to be ACKed */
				switch (mtk_tcp_rexmit_check(c, &cbd)) {
				case -1:
					return;
				case 1:
					/*
					 * Timed out waiting for ACK, entering
					 * retransmission mode
					 */
					c->rexmit_mode = 1;
					c->rexmit_wait_ack = 0;
				}
			}
		}

		if (c->rexmit_mode) {
			if (c->rexmit_wait_ack) {
				switch (mtk_tcp_rexmit_check(c, &cbd)) {
				case -1:
					return;
				case 1:
					/*
					 * Timed out waiting for ACK of
					 * retransmitted segment, start for
					 * the next trial
					 */
					c->rexmit_wait_ack = 0;
				}
			}

			if (!c->rexmit_wait_ack) {
				sendseq = c->local_seq_acked;

				if (!c->zw_mode) {
					/*
					 * Send data if not in zero-window mode
					 */
					data = c->tx + datalen_acked;
					datalen = min(c->txlen - datalen_acked,
						      (u32)c->mss);
					c->ack_flag++;
					c->rexmit_wait_ack = 1;
					mtk_tcp_rexmit_reset(c);
				}

				if (get_timer(c->ts_rtt) > MTK_TCP_RTT_INTERVAL) {
					/* Start calculating RTO */
					c->ack_calc_rtt = c->local_seq_acked +
							  datalen;
					c->ts_rtt = get_timer(0);
				}
			}
		}

		if (!c->peer_wnd || c->zw_mode) {
			if (!c->zw_mode) {
				/* Enter zero-window mode */
				c->zw_mode = 1;
				mtk_tcp_rexmit_init(c);
			}

			if (c->txlen > datalen_sent) {
				/* Doing zero window probing */
				switch (mtk_tcp_rexmit_check(c, &cbd)) {
				case -1:
					return;
				case 1:
					/*
					 * If we have data unsend or unacked,
					 * send one byte to probe
					 */
					datalen = 1;
					sendseq = c->local_seq_acked;
					data = c->tx + datalen_acked;
					if (c->local_seq == c->local_seq_acked)
						c->local_seq += datalen;
					c->ack_flag++;
					mtk_tcp_rexmit_reset(c);
				}
			}
		}

		if (c->ack_flag) {
			c->ack_flag = 0;
			mtk_tcp_send_packet(c, flags, sendseq, c->peer_seq,
					data, datalen);
		}

		if (c->close_flag) {
			if (!c->tx || !c->txlen) {
				/*
				 * Close the connection after we've sent
				 * current data chunk
				 */
				mtk_tcp_log("[MTK_TCP] conn_check: %pI4:%d -> FIN_WAIT_1 (close_flag)\n",
					    &c->ip_remote, ntohs(c->port_remote));
				mtk_tcp_send_packet(c, MTK_TCP_FIN | MTK_TCP_ACK,
						c->local_seq, c->peer_seq,
						NULL, 0);
				c->status = FIN_WAIT_1;
				mtk_tcp_rexmit_init(c);

				cbd.status = MTK_TCP_CB_CLOSING;
				assert((size_t)c->cb > gd->ram_base);
				c->cb(&cbd);
			}
		}

		break;
	case CLOSE_WAIT:
		mtk_tcp_log("[MTK_TCP] conn_check: %pI4:%d CLOSE_WAIT -> LAST_ACK\n",
			    &c->ip_remote, ntohs(c->port_remote));
		mtk_tcp_send_packet_ctrl(c, MTK_TCP_FIN | MTK_TCP_ACK);
		c->status = LAST_ACK;
		mtk_tcp_rexmit_init(c);
		break;
	case LAST_ACK:
		switch (mtk_tcp_rexmit_check(c, &cbd)) {
		case -1:
			return;
		case 1:
			mtk_tcp_send_packet_ctrl(c, MTK_TCP_FIN | MTK_TCP_ACK);
			mtk_tcp_rexmit_reset(c);
		}

		break;
	case FIN_WAIT_1:
		switch (mtk_tcp_rexmit_check(c, &cbd)) {
		case -1:
			return;
		case 1:
			mtk_tcp_send_packet(c, MTK_TCP_FIN | MTK_TCP_ACK,
				c->local_seq, c->peer_seq, NULL, 0);
			mtk_tcp_rexmit_reset(c);
		}

		break;
	case FIN_WAIT_2:
	case CLOSING:
		/*
		 * Half-closed, waiting for the last ACK/FIN of the peer.
		 * Nothing is retransmitted in these states, but the
		 * connection must not stay here forever when the peer never
		 * answers: upper layers wait for MTK_TCP_CB_CLOSED before
		 * acting (the web failsafe reboot, for one), and
		 * mtk_tcp_periodic_check() only reports "done" once every
		 * connection is gone.
		 */
		switch (mtk_tcp_rexmit_check(c, &cbd)) {
		case -1:
			return;
		default:
			break;
		}

		break;
	case TIME_WAIT:
		mtk_tcp_log("[MTK_TCP] conn_check: %pI4:%d TIME_WAIT -> CLOSED\n",
			    &c->ip_remote, ntohs(c->port_remote));
		mtk_tcp_send_packet_ctrl(c, MTK_TCP_ACK);
		mtk_tcp_conn_finish(c, MTK_TCP_CB_CLOSED);
		break;
	default:
		return;
	}
}

/*
 * Issue MTK_TCP_CB_POLL for connections that are able to accept a new data
 * chunk right now.
 *
 * This is the pump that lets upper layers stream console output while a
 * blocking command (tftp, ping, dhcp, ...) is still executing: the inner
 * net_loop() keeps calling mtk_tcp_periodic_check(), so the callback is
 * reached even though run_command() has not returned yet.
 */
static void mtk_tcp_conn_poll(struct mtk_tcp_conn *c)
{
	struct mtk_tcp_cb_data cbd = {};
	u32 datalen_acked;

	if (!c->cb || c->close_flag)
		return;

	if (c->status != ESTABLISHED)
		return;

	/* Only one unacknowledged chunk is supported per connection */
	datalen_acked = mtk_tcp_seq_sub(c->local_seq_acked, c->local_seq_last);
	if (c->tx && c->txlen > datalen_acked)
		return;

	cbd.conn = c;
	cbd.sip = c->ip_remote.s_addr;
	cbd.sp = c->port_remote;
	cbd.dp = c->port_local;
	cbd.pdata = c->pdata;
	cbd.status = MTK_TCP_CB_POLL;

	assert((size_t)c->cb > gd->ram_base);
	c->cb(&cbd);
}

int mtk_tcp_periodic_check(void)
{
	struct list_head *lh, *n;
	struct mtk_tcp_conn *c;
	int num = 0;

	list_for_each_safe(lh, n, &conn_head) {
		c = list_entry(lh, struct mtk_tcp_conn, node);
		/*
		 * Run the poll callback BEFORE the connection state
		 * machine: mtk_tcp_conn_check() may free @c (TIME_WAIT
		 * -> CLOSED), after which @c must not be touched.
		 */
		mtk_tcp_conn_poll(c);
		mtk_tcp_conn_check(c);
		num++;
	}

	if (list_empty(&listen_head)) {
		if (!mtk_tcp_stop)
			mtk_tcp_log("[MTK_TCP] periodic_check: listen_head empty, setting stop=1\n");
		mtk_tcp_stop = 1;
	}

	mtk_tcp_log("[MTK_TCP] periodic_check: stop=%d, conns=%d, listen_empty=%d\n",
		    mtk_tcp_stop, num, list_empty(&listen_head));

	/*
	 * "Done" means: no listeners AND no active connections.
	 * Using mtk_tcp_stop alone is unsafe because it is a module-level
	 * static that can carry over from a previous session (e.g. the
	 * failsafe's mtk_tcp_close_all_conn() on exit, or a prior wget
	 * that auto-set it via the list_empty() check above). The caller
	 * may not have reset it before the first poll, which caused the
	 * failsafe loop to terminate immediately on entry. Anchoring the
	 * exit condition to listen_head ensures sessions with registered
	 * listeners (httpd/telnetd) never spuriously exit.
	 */
	return (list_empty(&listen_head) && !num) ? 1 : 0;
}

static int mtk_tcp_send_packet_opt(struct mtk_tcp_conn *c, u16 flags, u32 seq, u32 ack,
			       void *opt, int opt_size, const void *payload,
			       int payload_len)
{
	int eth_hdr_size, pkt_hdr_size;
	struct mtk_tcp_hdr *tcp;
	struct ip_hdr *ip;
	uchar *pkt;
	u32 len;

	seq = htonl(seq);
	ack = htonl(ack);

	pkt = (uchar *)net_tx_packet;

	eth_hdr_size = net_set_ether(pkt, c->ethaddr, PROT_IP);
	pkt += eth_hdr_size;

	ip = (struct ip_hdr *)pkt;
	len = IP_HDR_SIZE + MTK_TCP_HDR_SIZE + opt_size + payload_len;
	net_set_ip_header(pkt, c->ip_remote, net_ip, len, IPPROTO_TCP);
	pkt += IP_HDR_SIZE;

	tcp = (struct mtk_tcp_hdr *)pkt;
	tcp->src = c->port_local;
	tcp->dst = c->port_remote;
	tcp->flags = htons((((MTK_TCP_HDR_SIZE + opt_size) / 4) <<
		MTK_TCP_HDR_LEN_SHIFT) | (flags & MTK_TCP_FLAG_MASK));
	memcpy(&tcp->seq, &seq, 4);
	memcpy(&tcp->ack, &ack, 4);
	tcp->wnd = htons(MTK_TCP_RCV_WND);
	/* avoid compiler's optimization leading to an unaligned access */
	memset(&tcp->urg, 0, sizeof(tcp->urg));
	tcp->chksum = 0;

	pkt += MTK_TCP_HDR_SIZE;

	if (opt_size) {
		memcpy(pkt, opt, opt_size);
		pkt += opt_size;
	}

	memcpy(pkt, payload, payload_len);

	tcp->chksum = mtk_tcp_checksum_compute(tcp, MTK_TCP_HDR_SIZE + opt_size +
		payload_len, net_ip.s_addr, c->ip_remote.s_addr);

	pkt_hdr_size = eth_hdr_size + IP_HDR_SIZE + MTK_TCP_HDR_SIZE + opt_size;

	/* if MAC address was not discovered yet, do an ARP request */
	if (memcmp(c->ethaddr, net_null_ethaddr, 6) == 0) {
		/* save the ip and eth addr for the packet to send after arp */
		net_arp_wait_packet_ip = c->ip_remote;
		arp_wait_packet_ethaddr = c->ethaddr;

		/* size of the waiting packet */
		arp_wait_tx_packet_size = pkt_hdr_size + payload_len;

		/* and do the ARP request */
		arp_wait_try = 1;
		arp_wait_timer_start = get_timer(0);
		arp_request();
		return 0;	/* waiting */
	} else {
		return eth_send(net_tx_packet, pkt_hdr_size + payload_len);
	}
}

static int mtk_tcp_send_packet(struct mtk_tcp_conn *c, u16 flags, u32 seq, u32 ack,
			   const void *payload, int payload_len)
{
	return mtk_tcp_send_packet_opt(c, flags, seq, ack, NULL, 0,
		payload, payload_len);
}

static int mtk_tcp_send_packet_ctrl(struct mtk_tcp_conn *c, u16 flags)
{
	return mtk_tcp_send_packet_opt(c, flags, c->local_seq, c->peer_seq + 1,
		NULL, 0, NULL, 0);
}

void *mtk_tcp_conn_set_pdata(const void *conn, void *pdata)
{
	struct mtk_tcp_conn *c = (struct mtk_tcp_conn *)conn;
	void *prev_pdata;

	if (!c)
		return NULL;

	prev_pdata = c->pdata;
	c->pdata = pdata;

	return prev_pdata;
}

int mtk_tcp_send_data(const void *conn, const void *data, u32 size)
{
	struct mtk_tcp_conn *c = (struct mtk_tcp_conn *)conn;
	u32 datalen_acked;

	if (!c || !data || !size)
		return -EINVAL;

	if (c->close_flag)
		return 1;

	datalen_acked = mtk_tcp_seq_sub(c->local_seq_acked, c->local_seq_last);

	if (c->tx && (c->txlen > datalen_acked))
		return 1;

	c->tx = data;
	c->txlen = size;
	c->local_seq_last = c->local_seq;
	c->local_seq_acked = c->local_seq;

	return 0;
}

/*
 * Deliver the final notification for @c and release it.
 *
 * The connection is unlinked from conn_head and stripped of its callback
 * and private data BEFORE the callback runs.  A callback is free to call
 * back into this stack -- an upper layer's CLOSED handler may call
 * mtk_tcp_close_all_conn().  If @c were still linked here, that call would
 * find @c again and re-enter the very same callback: the web failsafe's
 * reboot handler recursed this way, printing its "Rebooting now" line once
 * per level and never reaching do_reset().  Clearing c->pdata likewise
 * stops a second (nested) delivery from handing already-freed private data
 * to the handler.
 *
 * @c must not be touched afterwards.
 */
static void mtk_tcp_conn_finish(struct mtk_tcp_conn *c,
				enum mtk_tcp_cb_status status)
{
	struct mtk_tcp_cb_data cbd = {};
	mtk_tcp_conn_cb cb;
	void *pdata;

	list_del(&c->node);
	cb = c->cb;
	pdata = c->pdata;
	c->cb = NULL;
	c->pdata = NULL;

	if (cb && pdata) {
		cbd.conn = c;
		cbd.sip = c->ip_remote.s_addr;
		cbd.sp = c->port_remote;
		cbd.dp = c->port_local;
		cbd.pdata = pdata;
		cbd.status = status;

		assert((size_t)cb > gd->ram_base);
		cb(&cbd);
	}

	free(c);
}

/*
 * Tear a connection down immediately: reset the peer, notify the upper layer
 * so it can free its private data, and drop the connection from conn_head.
 *
 * This is the only safe way to dispose of a connection once the networking
 * session is ending.  mtk_tcp_close_conn(c, 0) only raises close_flag and
 * relies on a FIN/ACK round trip to reach TIME_WAIT before the connection is
 * deleted, but the caller is about to (or already has) call eth_halt(), so
 * that round trip never completes and the connection stays on conn_head
 * forever with close_flag set.
 *
 * Such a leftover is found again by mtk_tcp_conn_find() as soon as the peer
 * reconnects from the same source port -- which is exactly what a browser
 * does when the web failsafe is stopped and started again.  The next session
 * then hands freed per-connection data back to the callback, and the first
 * packet that arrives on the stale connection dereferences uri handlers that
 * httpd_free_instance() has already released.
 */
static void mtk_tcp_conn_teardown(struct mtk_tcp_conn *c)
{
	/*
	 * Only reset when the peer MAC is known.  Otherwise
	 * mtk_tcp_send_packet_opt() would kick off an ARP request for a
	 * connection that is being destroyed right now, leaving the ARP wait
	 * state pointing into freed memory.
	 */
	if (memcmp(c->ethaddr, net_null_ethaddr, 6))
		mtk_tcp_send_packet(c, MTK_TCP_RST | MTK_TCP_ACK,
				    c->local_seq, c->peer_seq, NULL, 0);

	mtk_tcp_conn_finish(c, MTK_TCP_CB_CLOSED);
}

int mtk_tcp_close_conn(const void *conn, int rst)
{
	struct mtk_tcp_conn *c = (struct mtk_tcp_conn *)conn;

	if (!c)
		return -EINVAL;

	if (rst) {
		mtk_tcp_conn_teardown(c);
	} else {
		c->close_flag = 1;
	}

	return 0;
}

void mtk_tcp_close_all_conn(void)
{
	struct list_head *lh, *n;
	struct mtk_tcp_conn *c;
	struct mtk_tcp_listen *l;
	static bool tearing_down;

	mtk_tcp_log("[MTK_TCP] mtk_tcp_close_all_conn: stop=%d\n", mtk_tcp_stop);

	/*
	 * A connection callback invoked below may legitimately call this
	 * function again.  The outer list_for_each_safe() has already
	 * stashed the "next" pointer, so a nested run would free it and
	 * make the outer loop walk freed memory.  The outer run disposes of
	 * every connection anyway, so the nested one has nothing to do.
	 */
	if (tearing_down)
		return;

	tearing_down = true;

	list_for_each_safe(lh, n, &conn_head) {
		c = list_entry(lh, struct mtk_tcp_conn, node);
		mtk_tcp_conn_teardown(c);
	}

	/*
	 * Remove all listeners so that mtk_tcp_periodic_check() can
	 * return 1 (list_empty(&listen_head) && !num) and the polling
	 * loop in failsafe can exit cleanly.  This is needed for
	 * initramfs boot where we must leave net_loop / poll loop
	 * without a board reset.
	 */
	list_for_each_safe(lh, n, &listen_head) {
		l = list_entry(lh, struct mtk_tcp_listen, node);
		mtk_tcp_log("[MTK_TCP] close_all_conn: removing listener port %d\n",
			    ntohs(l->port));
		list_del(&l->node);
		free(l);
	}

	/*
	 * A connection that sent through mtk_tcp_send_packet_opt() with an
	 * unresolved peer MAC is referenced by the ARP wait state
	 * (arp_wait_packet_ethaddr points into the connection).  The
	 * connections are gone now, so drop that state too -- otherwise an
	 * ARP reply arriving in the next session writes through a dangling
	 * pointer and transmits a stale net_tx_packet.
	 */
	if (arp_is_waiting())
		arp_wait_clear();

	tearing_down = false;

	mtk_tcp_stop = 1;
}

void mtk_tcp_close_conn_by_port(__be16 port)
{
	struct list_head *lh, *n;
	struct mtk_tcp_conn *c;

	/*
	 * Dispose of every connection still tracked for a local port.
	 *
	 * Used when an upper layer (httpd) tears down its instance: the
	 * per-connection data of the still-tracked connections references
	 * resources owned by that instance, so they must go away before the
	 * instance is freed.  Without this, restarting the web failsafe
	 * leaves the connections of the previous session behind and the next
	 * packet from the peer runs the callback on freed state.
	 */
	list_for_each_safe(lh, n, &conn_head) {
		c = list_entry(lh, struct mtk_tcp_conn, node);
		if (c->port_local == port)
			mtk_tcp_conn_teardown(c);
	}
}

void mtk_tcp_reset_all_conn(void)
{
	struct list_head *lh, *n;
	struct mtk_tcp_conn *c;

	mtk_tcp_log("[MTK_TCP] mtk_tcp_reset_all_conn: stop=%d\n", mtk_tcp_stop);

	list_for_each_safe(lh, n, &conn_head) {
		c = list_entry(lh, struct mtk_tcp_conn, node);
		mtk_tcp_close_conn(c, 1);
	}

	mtk_tcp_stop = 1;
}

int mtk_tcp_conn_is_alive(const void *conn)
{
	struct mtk_tcp_conn *c = (struct mtk_tcp_conn *)conn;

	return c->status == ESTABLISHED;
}
