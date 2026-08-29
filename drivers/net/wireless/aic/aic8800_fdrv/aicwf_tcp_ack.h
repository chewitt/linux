/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_TCP_ACK_H_
#define _AICWF_TCP_ACK_H_

#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <net/tcp.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/in.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/tcp.h>
#include <linux/version.h>

#define TCP_ACK_NUM      32
#define TCP_ACK_EXIT_VAL 0x800
#define TCP_ACK_DROP_CNT 10

#define ACK_OLD_TIME     4000
#define U32_BEFORE(a, b) ((__s32)((__u32)(a) - (__u32)b) <= 0)

#define MAX_TCP_ACK 200
/*min window size in KB, it's 256KB*/
#define MIN_WIN     256
#define SIZE_KB     1024

struct msg_buf {
	struct sk_buff *skb;
	struct rwnx_vif *rwnx_vif;
};

struct tcp_ack_msg {
	__be16 source;
	__be16 dest;
	__be32 saddr;
	__be32 daddr;
	u32 seq;
	u16 win;
};

struct tcp_ack_info {
	int ack_info_num;
	int busy;
	int drop_cnt;
	int psh_flag;
	u32 psh_seq;
	u16 win_scale;
	/* seqlock for ack info */
	seqlock_t seqlock;
	unsigned long last_time;
	unsigned long timeout;
	struct timer_list timer;
	struct msg_buf *msgbuf;
	struct msg_buf *in_send_msg;
	struct tcp_ack_msg ack_msg;
};

struct tcp_ack_manage {
	atomic_t enable;
	int max_num;
	int free_index;
	unsigned long last_time;
	unsigned long timeout;
	atomic_t max_drop_cnt;
	/* lock for tcp ack alloc and free */
	spinlock_t lock;
	struct rwnx_hw *priv;
	struct tcp_ack_info ack_info[TCP_ACK_NUM];
	/*size in KB */
	unsigned int ack_winsize;
};

struct msg_buf *intf_tcp_alloc_msg(void);

void tcp_ack_init(struct rwnx_hw *priv);

void tcp_ack_deinit(struct rwnx_hw *priv);

int is_drop_tcp_ack(struct tcphdr *tcphdr, int tcp_tot_len,
		    unsigned short *win_scale);

int is_tcp_ack(struct sk_buff *skb, unsigned short *win_scale);

int filter_send_tcp_ack(struct rwnx_hw *priv, struct msg_buf *msgbuf,
			unsigned char *buf, unsigned int plen);

void filter_rx_tcp_ack(struct rwnx_hw *priv, unsigned char *buf, unsigned int plen);

void move_tcpack_msg(struct rwnx_hw *priv, struct msg_buf *msg);
void intf_tcp_drop_msg(struct rwnx_hw *priv, struct msg_buf *msg);
void tcp_ack_timeout(struct timer_list *t);
int tcp_check_quick_ack(unsigned char *buf, struct tcp_ack_msg *msg);
int tcp_check_ack(unsigned char *buf, struct tcp_ack_msg *msg,
		  unsigned short *win_scale);
int tcp_ack_match(struct tcp_ack_manage *ack_m, struct tcp_ack_msg *ack_msg);
void tcp_ack_update(struct tcp_ack_manage *ack_m);
int tcp_ack_alloc_index(struct tcp_ack_manage *ack_m);
int tcp_ack_handle(struct msg_buf *new_msgbuf, struct tcp_ack_manage *ack_m,
		   struct tcp_ack_info *ack_info, struct tcp_ack_msg *ack_msg,
				   int type);
int tcp_ack_handle_new(struct msg_buf *new_msgbuf, struct tcp_ack_manage *ack_m,
		       struct tcp_ack_info *ack_info,
					   struct tcp_ack_msg *ack_msg, int type);

#endif
