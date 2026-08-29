/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_RX_PREALLOC_H_
#define _AICWF_RX_PREALLOC_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <linux/atomic.h>

#ifdef CONFIG_PREALLOC_RX_SKB
struct rx_buff {
	struct list_head queue;
	unsigned char *data;
	u32 len;
	u8 *start;
	u8 *end;
	u8 *read;
};

struct aicwf_rx_buff_list {
	struct list_head rxbuff_list;
	atomic_t rxbuff_list_len;
};

extern struct aicwf_rx_buff_list aic_rx_buff_list;

struct rx_buff *aicwf_prealloc_rxbuff_alloc(spinlock_t *lock);
void aicwf_prealloc_rxbuff_free(struct rx_buff *rxbuff, spinlock_t *lock);
int aicwf_prealloc_init(void);
void aicwf_prealloc_exit(void);
#endif
#endif /* _AICWF_RX_PREALLOC_H_ */
