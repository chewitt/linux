/* SPDX-License-Identifier: GPL-2.0 */
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_tx.h
 *
 * @brief tx handle
 *
 ******************************************************************************
 */
#ifndef _RWNX_TX_H_
#define _RWNX_TX_H_

#include "hal_desc.h"
#include "ipc_shared.h"
#include "lmac_types.h"
#include "rwnx_txq.h"
#include "aicwf_tcp_ack.h"
#include <linux/ieee80211.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

#define RWNX_HWQ_BK      0
#define RWNX_HWQ_BE      1
#define RWNX_HWQ_VI      2
#define RWNX_HWQ_VO      3
#define RWNX_HWQ_BCMC    4
#define RWNX_HWQ_NB      NX_TXQ_CNT
#define RWNX_HWQ_ALL_ACS (RWNX_HWQ_BK | RWNX_HWQ_BE | RWNX_HWQ_VI | RWNX_HWQ_VO)
#define RWNX_HWQ_ALL_ACS_BIT                                                   \
	(BIT(RWNX_HWQ_BK) | BIT(RWNX_HWQ_BE) | BIT(RWNX_HWQ_VI) | BIT(RWNX_HWQ_VO))

#define RWNX_TX_LIFETIME_MS 1000
#define RWNX_TX_MAX_RATES   NX_TX_MAX_RATES

#define RWNX_SWTXHDR_ALIGN_SZ  4
#define RWNX_SWTXHDR_ALIGN_MSK (RWNX_SWTXHDR_ALIGN_SZ - 1)
#define RWNX_SWTXHDR_ALIGN_PADS(x)                                             \
	((RWNX_SWTXHDR_ALIGN_SZ - ((x) & RWNX_SWTXHDR_ALIGN_MSK)) &                \
	 RWNX_SWTXHDR_ALIGN_MSK)
#if RWNX_SWTXHDR_ALIGN_SZ & RWNX_SWTXHDR_ALIGN_MSK
#error bad RWNX_SWTXHDR_ALIGN_SZ
#endif

#define AMSDU_PADDING(x) ((4 - ((x) & 0x3)) & 0x3)

#define TXU_CNTRL_RETRY       BIT(0)
#define TXU_CNTRL_MORE_DATA   BIT(2)
#define TXU_CNTRL_MGMT        BIT(3)
#define TXU_CNTRL_MGMT_NO_CCK BIT(4)
#define TXU_CNTRL_AMSDU       BIT(6)
#define TXU_CNTRL_MGMT_ROBUST BIT(7)
#define TXU_CNTRL_USE_4ADDR   BIT(8)
#define TXU_CNTRL_EOSP        BIT(9)
#define TXU_CNTRL_MESH_FWD    BIT(10)
#define TXU_CNTRL_TDLS        BIT(11)

extern const int rwnx_tid2hwq[IEEE80211_NUM_TIDS];

/**
 * struct rwnx_amsdu_txhdr - Header for a noninitial A-MSDU subframe
 *
 * @list: Node in the list of additional A-MSDU subframes
 * @map_len: Number of bytes mapped for this subframe
 * @dma_addr: DMA address of the mapped subframe
 * @skb: Socket buffer containing the subframe
 * @pad: Padding before this subframe when dismantling the A-MSDU
 * @msdu_len: MSDU size in bytes, excluding padding and the A-MSDU header
 */
struct rwnx_amsdu_txhdr {
	struct list_head list;
	size_t map_len;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	u16 pad;
	u16 msdu_len;
};

/**
 * struct rwnx_amsdu - State used while building an A-MSDU
 *
 * @hdrs: List of additional subframes
 * @len: Current A-MSDU size excluding padding, or 0 when none is in progress
 * @nb: Number of subframes in the A-MSDU
 * @pad: Padding required before the next subframe
 */
struct rwnx_amsdu {
	struct list_head hdrs;
	u16 len;
	u8 nb;
	u8 pad;
};

/**
 * struct rwnx_sw_txhdr - Software part of tx header
 *
 * @rwnx_sta: Destination STA
 * @rwnx_vif: VIF transmitting the buffer
 * @txq: TX queue used to send the buffer
 * @hw_queue: Hardware queue index used to push the buffer
 * @frame_len: Frame size excluding the MAC header
 * @cookie: cfg80211 management TX cookie
 * @headroom: Headroom added for &struct rwnx_txhdr
 * @amsdu: A-MSDU state when this buffer contains the first subframe
 * @need_cfm: Whether the firmware must confirm this transmission
 * @skb: Socket buffer being transmitted
 * @map_len: Number of bytes mapped for DMA
 * @dma_addr: DMA address of the mapped data
 * @desc: Descriptor copied to firmware-visible shared memory
 */
struct rwnx_sw_txhdr {
	struct rwnx_sta *rwnx_sta;
	struct rwnx_vif *rwnx_vif;
	struct rwnx_txq *txq;
	u8 hw_queue;
	u16 frame_len;
	u64 cookie;
	u16 headroom;
#ifdef CONFIG_RWNX_AMSDUS_TX
	struct rwnx_amsdu amsdu;
#endif
	u32 need_cfm;
	struct sk_buff *skb;

	size_t map_len;
	dma_addr_t dma_addr;
	struct txdesc_api desc;
};

/**
 * struct rwnx_txhdr - Structure to control transimission of packet
 * (Added in skb headroom)
 *
 * @sw_hdr: Information from driver
 * @cache_guard:
 * @hw_hdr: Information for/from hardware
 */
struct rwnx_txhdr {
	struct rwnx_sw_txhdr *sw_hdr;
	char cache_guard[L1_CACHE_BYTES];
	struct rwnx_hw_txhdr hw_hdr;
};

u16 rwnx_select_txq(struct rwnx_vif *rwnx_vif, struct sk_buff *skb);
netdev_tx_t rwnx_start_xmit(struct sk_buff *skb, struct net_device *dev);

int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
			 struct cfg80211_mgmt_tx_params *params, bool offchan,
			 u64 cookie);

int rwnx_txdatacfm(void *pthis, void *host_id);

struct rwnx_hw;
struct rwnx_sta;
void rwnx_set_traffic_status(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
			     bool available, u8 ps_id);

void rwnx_ps_bh_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
		       bool enable);

void rwnx_ps_bh_traffic_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
			    u16 pkt_req, u8 ps_id);

void rwnx_switch_vif_sta_txq(struct rwnx_sta *sta, struct rwnx_vif *old_vif,
			     struct rwnx_vif *new_vif);

void rwnx_txq_credit_update(struct rwnx_hw *rwnx_hw, int sta_idx, u8 tid,
			    s8 update);

void rwnx_tx_push(struct rwnx_hw *rwnx_hw, struct rwnx_txhdr *txhdr, int flags);

#ifdef CONFIG_AIC8800_FILTER_TCP_ACK
int intf_tx(struct rwnx_hw *priv, struct msg_buf *msg);
#endif

#endif /* _RWNX_TX_H_ */
