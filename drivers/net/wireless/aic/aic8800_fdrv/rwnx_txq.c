// SPDX-License-Identifier: GPL-2.0
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_txq.c
 *
 * @brief tx queue function
 *
 ****************************************************************************************
 */

#include "ipc_host.h"
#include "rwnx_defs.h"
#include "rwnx_events.h"
#include "rwnx_tx.h"

/******************************************************************************
 * Utils functions
 *****************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
const int nx_tid_prio[NX_NB_TID_PER_STA] = {7, 6, 5, 4, 3, 0, 2, 1};

static inline int rwnx_txq_sta_idx(struct rwnx_sta *sta, u8 tid)
{
	if (is_multicast_sta(sta->sta_idx)) {
		if (g_rwnx_plat->sdiodev->rwnx_hw->chip_ops->is_old_ic)
			return NX_FIRST_VIF_TXQ_IDX_FOR_OLD_IC + sta->vif_idx;

		return NX_FIRST_VIF_TXQ_IDX + sta->vif_idx;
	}

	return (sta->sta_idx * NX_NB_TXQ_PER_STA) + tid;
}

static inline int rwnx_txq_vif_idx(struct rwnx_vif *vif, u8 type)
{
	if (g_rwnx_plat->sdiodev->rwnx_hw->chip_ops->is_old_ic)
		return NX_FIRST_VIF_TXQ_IDX_FOR_OLD_IC + master_vif_idx(vif) +
		       (type * NX_VIRT_DEV_MAX);

	return NX_FIRST_VIF_TXQ_IDX + master_vif_idx(vif) +
	       (type * NX_VIRT_DEV_MAX);
}

struct rwnx_txq *rwnx_txq_sta_get(struct rwnx_sta *sta, u8 tid,
				  struct rwnx_hw *rwnx_hw)
{
	if (tid >= NX_NB_TXQ_PER_STA)
		tid = 0;

	return &rwnx_hw->txq[rwnx_txq_sta_idx(sta, tid)];
}

struct rwnx_txq *rwnx_txq_vif_get(struct rwnx_vif *vif, u8 type)
{
	if (type > NX_UNK_TXQ_TYPE)
		type = NX_BCMC_TXQ_TYPE;

	return &vif->rwnx_hw->txq[rwnx_txq_vif_idx(vif, type)];
}

static inline struct rwnx_sta *rwnx_txq_2_sta(struct rwnx_txq *txq)
{
	return txq->sta;
}

#endif /* CONFIG_RWNX_FULLMAC */

/******************************************************************************
 * Init/Deinit functions
 *****************************************************************************/
/*
 * rwnx_txq_init - Initialize a TX queue
 *
 * @txq: TX queue to be initialized
 * @idx: TX queue index
 * @status: TX queue initial status
 * @hwq: Associated HW queue
 * @ndev: Net device this queue belongs to
 *        (may be null for non netdev txq)
 *
 * Each queue is initialized with the credit of @NX_TXQ_INITIAL_CREDITS.
 */
static void rwnx_txq_init(struct rwnx_txq *txq, int idx, u8 status,
			  struct rwnx_hwq *hwq, int tid,
#ifdef CONFIG_RWNX_FULLMAC
	struct rwnx_sta *sta, struct net_device *ndev
#endif
)
{
	int i;
	int nx_first_unk_txq_idx = NX_FIRST_UNK_TXQ_IDX;
	int nx_bcmc_txq_ndev_idx = NX_BCMC_TXQ_NDEV_IDX;
	int nx_first_vif_txq_idx = NX_FIRST_VIF_TXQ_IDX;

	if (g_rwnx_plat->sdiodev->rwnx_hw->chip_ops->is_old_ic) {
		nx_first_unk_txq_idx = NX_FIRST_UNK_TXQ_IDX_FOR_OLD_IC;
		nx_bcmc_txq_ndev_idx = NX_BCMC_TXQ_NDEV_IDX_FOR_OLD_IC;
		nx_first_vif_txq_idx = NX_FIRST_VIF_TXQ_IDX_FOR_OLD_IC;
	}

	txq->idx = idx;
	txq->status = status;
	txq->credits = NX_TXQ_INITIAL_CREDITS;
	txq->pkt_sent = 0;
	skb_queue_head_init(&txq->sk_list);
	txq->last_retry_skb = NULL;
	txq->nb_retry = 0;
	txq->hwq = hwq;
	txq->sta = sta;
	for (i = 0; i < CONFIG_USER_MAX; i++)
		txq->pkt_pushed[i] = 0;
	txq->push_limit = 0;
	txq->tid = tid;
#ifdef CONFIG_MAC80211_TXQ
	txq->nb_ready_mac80211 = 0;
#endif
#ifdef CONFIG_RWNX_FULLMAC
	txq->ps_id = LEGACY_PS_ID;
	if (idx < nx_first_vif_txq_idx) {
		int sta_idx = sta->sta_idx;
		int tid = idx - (sta_idx * NX_NB_TXQ_PER_STA);

		if (tid < NX_NB_TID_PER_STA)
			txq->ndev_idx = NX_STA_NDEV_IDX(tid, sta_idx);
		else
			txq->ndev_idx = NDEV_NO_TXQ;
	} else if (idx < nx_first_unk_txq_idx) {
		txq->ndev_idx = nx_bcmc_txq_ndev_idx;
	} else {
		txq->ndev_idx = NDEV_NO_TXQ;
	}
	txq->ndev = ndev;
#ifdef CONFIG_RWNX_AMSDUS_TX
	txq->amsdu = NULL;
	txq->amsdu_len = 0;
#endif /* CONFIG_RWNX_AMSDUS_TX */
#endif /* CONFIG_RWNX_FULLMAC */
}

/**
 * rwnx_txq_flush - Flush all buffers queued for a TXQ
 *
 * @rwnx_hw: main driver data
 * @txq: txq to flush
 */
void rwnx_txq_flush(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&txq->sk_list)) != NULL) {
		struct rwnx_sw_txhdr *sw_txhdr =
			((struct rwnx_txhdr *)skb->data)->sw_hdr;

#ifdef CONFIG_RWNX_AMSDUS_TX
		if (sw_txhdr->desc.host.packet_cnt > 1) {
			struct rwnx_amsdu_txhdr *amsdu_txhdr;

			list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
				dev_kfree_skb_any(amsdu_txhdr->skb);
			}
		}
#endif
		kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);

#ifdef CONFIG_RWNX_FULLMAC
		dev_kfree_skb_any(skb);
#endif /* CONFIG_RWNX_FULLMAC */
	}
}

/**
 * rwnx_txq_deinit - De-initialize a TX queue
 *
 * @rwnx_hw: Driver main data
 * @txq: TX queue to be de-initialized
 * Any buffer stuck in a queue will be freed.
 */
static void rwnx_txq_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq)
{
	if (txq->idx == TXQ_INACTIVE)
		return;

	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_del_from_hw_list(txq);
	txq->idx = TXQ_INACTIVE;
	spin_unlock_bh(&rwnx_hw->tx_lock);

	rwnx_txq_flush(rwnx_hw, txq);
}

/**
 * rwnx_txq_vif_init - Initialize all TXQ linked to a vif
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: Pointer on VIF
 * @status: Initial txq status
 *
 * Softmac : 1 VIF TXQ per HWQ
 *
 * Fullmac : 1 VIF TXQ for BC/MC
 *           1 VIF TXQ for MGMT to unknown STA
 */
void rwnx_txq_vif_init(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		       u8 status)
{
	struct rwnx_txq *txq;
	int idx;

#ifdef CONFIG_RWNX_FULLMAC
	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[RWNX_HWQ_BE], 0,
		      &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index], rwnx_vif->ndev);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL,
		      rwnx_vif->ndev);

#endif /* CONFIG_RWNX_FULLMAC */
}

/**
 * rwnx_txq_vif_deinit - Deinitialize all TXQ linked to a vif
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_vif_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

#ifdef CONFIG_RWNX_FULLMAC
	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_hw, txq);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_hw, txq);

#endif /* CONFIG_RWNX_FULLMAC */
}

/**
 * rwnx_txq_sta_init - Initialize TX queues for a STA
 *
 * @rwnx_hw: Main driver data
 * @rwnx_sta: STA for which tx queues need to be initialized
 * @status: Initial txq status
 *
 * This function initialize all the TXQ associated to a STA.
 * Softmac : 1 TXQ per TID
 *
 * Fullmac : 1 TXQ per TID (limited to 8)
 *           1 TXQ for MGMT
 */
void rwnx_txq_sta_init(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta,
		       u8 status)
{
	struct rwnx_txq *txq;
	int tid, idx;

#ifdef CONFIG_RWNX_FULLMAC
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[rwnx_sta->vif_idx];

	idx = rwnx_txq_sta_idx(rwnx_sta, 0);

#ifdef CONFIG_MAC80211_TXQ
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0); tid < NX_NB_TXQ_PER_STA;
	     tid++, txq = rwnx_txq_sta_get(rwnx_sta, tid))
#elif defined(CONFIG_RWNX_FULLMAC)
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0, rwnx_hw);
	     tid < (is_multicast_sta(rwnx_sta->sta_idx) ? 1 : NX_NB_TXQ_PER_STA);
	     tid++, txq++)
#endif
	{
		rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[rwnx_tid2hwq[tid]], tid,
			      rwnx_sta, rwnx_vif->ndev);
		txq->ps_id =
			rwnx_sta->uapsd_tids & (1 << tid) ? UAPSD_ID : LEGACY_PS_ID;
		idx++;
	}

#endif /* CONFIG_RWNX_FULLMAC */
}

/**
 * rwnx_txq_sta_deinit - Deinitialize TX queues for a STA
 *
 * @rwnx_hw: Main driver data
 * @rwnx_sta: STA for which tx queues need to be deinitialized
 */
void rwnx_txq_sta_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta)
{
	struct rwnx_txq *txq;
	int tid;

#ifdef CONFIG_MAC80211_TXQ
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0); tid < NX_NB_TXQ_PER_STA;
	     tid++, txq = rwnx_txq_sta_get(rwnx_sta, tid))
#elif defined(CONFIG_RWNX_FULLMAC)
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0, rwnx_hw);
	     tid < (is_multicast_sta(rwnx_sta->sta_idx) ? 1 : NX_NB_TXQ_PER_STA);
	     tid++, txq++)
#endif
	{
		rwnx_txq_deinit(rwnx_hw, txq);
	}
}

#ifdef CONFIG_RWNX_FULLMAC
/**
 * rwnx_txq_unk_vif_init - Initialize TXQ for unknown STA linked to a vif
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_unk_vif_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int idx;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_init(txq, idx, 0, &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL,
		      rwnx_vif->ndev);
}

/**
 * rwnx_txq_unk_vif_deinit - Deinitialize the unknown-STA TX queue for a VIF
 *
 * @rwnx_vif: VIF that owns the queue
 */
void rwnx_txq_unk_vif_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_vif->rwnx_hw, txq);
}

/**
 * rwnx_txq_offchan_init - Initialize the off-channel TX queue
 *
 * @rwnx_vif: VIF that owns the queue
 *
 * The off-channel queue is active only for the duration of remain-on-channel.
 */
void rwnx_txq_offchan_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX;

	if (g_rwnx_plat->sdiodev->rwnx_hw->chip_ops->is_old_ic)
		nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX_FOR_OLD_IC;

	txq = &rwnx_hw->txq[nx_off_chan_txq_idx];
	rwnx_txq_init(txq, nx_off_chan_txq_idx, RWNX_TXQ_STOP_CHAN,
		      &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL, rwnx_vif->ndev);
}

/**
 * rwnx_txq_offchan_deinit - Deinitialize the off-channel TX queue
 *
 * @rwnx_vif: Interface that owns the queue
 *
 * Any buffer remaining in the queue is freed.
 */
void rwnx_txq_offchan_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	int nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX;

	if (g_rwnx_plat->sdiodev->rwnx_hw->chip_ops->is_old_ic)
		nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX_FOR_OLD_IC;

	txq = &rwnx_vif->rwnx_hw->txq[nx_off_chan_txq_idx];
	rwnx_txq_deinit(rwnx_vif->rwnx_hw, txq);
}

/**
 * rwnx_txq_tdls_vif_init - Initialize TXQ vif for TDLS
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_tdls_vif_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (!(rwnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return;

	rwnx_txq_unk_vif_init(rwnx_vif);
}

/**
 * rwnx_txq_tdls_vif_deinit - Deinitialize TXQ vif for TDLS
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_tdls_vif_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (!(rwnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return;

	rwnx_txq_unk_vif_deinit(rwnx_vif);
}
#endif

/******************************************************************************
 * Start/Stop functions
 *****************************************************************************/
/**
 * rwnx_txq_add_to_hw_list - Add TX queue to a HW queue schedule list.
 *
 * @txq: TX queue to add
 *
 * Add the TX queue if not already present in the HW queue list.
 * To be called with tx_lock hold
 */
void rwnx_txq_add_to_hw_list(struct rwnx_txq *txq)
{
	if (!(txq->status & RWNX_TXQ_IN_HWQ_LIST)) {
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_txq_add_to_hw(txq);
#endif
		txq->status |= RWNX_TXQ_IN_HWQ_LIST;
		list_add_tail(&txq->sched_list, &txq->hwq->list);
		txq->hwq->need_processing = true;
	}
}

/**
 * rwnx_txq_del_from_hw_list - Delete TX queue from a HW queue schedule list.
 *
 * @txq: TX queue to delete
 *
 * Remove the TX queue from the HW queue list if present.
 * To be called with tx_lock hold
 */
void rwnx_txq_del_from_hw_list(struct rwnx_txq *txq)
{
	if (txq->status & RWNX_TXQ_IN_HWQ_LIST) {
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_txq_del_from_hw(txq);
#endif
		txq->status &= ~RWNX_TXQ_IN_HWQ_LIST;
		list_del(&txq->sched_list);
	}
}

/**
 * rwnx_txq_skb_ready - Check if skb are available for the txq
 *
 * @txq: Pointer on txq
 * Return: %true if buffers are ready to be pushed, otherwise %false.
 */
static inline bool rwnx_txq_skb_ready(struct rwnx_txq *txq)
{
#ifdef CONFIG_MAC80211_TXQ
	if (txq->nb_ready_mac80211 != NOT_MAC80211_TXQ)
		return ((txq->nb_ready_mac80211 > 0) ||
			!skb_queue_empty(&txq->sk_list));
	else
#endif
		return !skb_queue_empty(&txq->sk_list);
}

/**
 * rwnx_txq_start - Try to Start one TX queue
 *
 * @txq: TX queue to start
 * @reason: reason why the TX queue is started (among RWNX_TXQ_STOP_xxx)
 *
 * Re-start the TX queue for one reason.
 * If after this the txq is no longer stopped and some buffers are ready,
 * the TX queue is also added to HW queue list.
 * To be called with tx_lock hold
 */
void rwnx_txq_start(struct rwnx_txq *txq, u16 reason)
{
	WARN_ON_ONCE(!txq);
	if (txq->idx != TXQ_INACTIVE && (txq->status & reason)) {
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_txq_start(txq, reason);
#endif
		txq->status &= ~reason;
		if (!rwnx_txq_is_stopped(txq) && rwnx_txq_skb_ready(txq))
			rwnx_txq_add_to_hw_list(txq);
	}
}

/**
 * rwnx_txq_stop - Stop one TX queue
 *
 * @txq: TX queue to stop
 * @reason: reason why the TX queue is stopped (among RWNX_TXQ_STOP_xxx)
 *
 * Stop the TX queue. It will remove the TX queue from HW queue list
 * To be called with tx_lock hold
 */
void rwnx_txq_stop(struct rwnx_txq *txq, u16 reason)
{
	WARN_ON_ONCE(!txq);
	if (txq->idx != TXQ_INACTIVE) {
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_txq_stop(txq, reason);
#endif
		txq->status |= reason;
		rwnx_txq_del_from_hw_list(txq);
	}
}

/**
 * rwnx_txq_sta_start - Start all TX queues linked to a STA
 *
 * @rwnx_sta: STA whose TX queues must be re-started
 * @reason: Stop-reason bits to clear
 * @rwnx_hw: Driver main data
 *
 * Restart all the TX queues of the STA for the specified reason:
 * - RWNX_TXQ_STOP_STA_PS: the STA is no longer in power save mode
 * - RWNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - RWNX_TXQ_STOP_CHAN: the STA's VIF is now on the current active channel
 *
 * A queue with a ready buffer and no other stop reason is added to its hardware
 * queue list.
 *
 * Context: Called with the TX lock held.
 */
void rwnx_txq_sta_start(struct rwnx_sta *rwnx_sta, u16 reason
#ifdef CONFIG_RWNX_FULLMAC
						,
						struct rwnx_hw *rwnx_hw
#endif
)
{
	struct rwnx_txq *txq;
	int tid;
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_sta_start(rwnx_sta->sta_idx);
#endif
#ifdef CONFIG_MAC80211_TXQ
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0); tid < NX_NB_TXQ_PER_STA;
	     tid++, txq = rwnx_txq_sta_get(rwnx_sta, tid))
#elif defined(CONFIG_RWNX_FULLMAC)
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0, rwnx_hw);
	     tid < (is_multicast_sta(rwnx_sta->sta_idx) ? 1 : NX_NB_TXQ_PER_STA);
	     tid++, txq++)
#endif
	{
		rwnx_txq_start(txq, reason);
	}
}

/**
 * rwnx_txq_sta_stop - Stop all TX queues linked to a STA
 *
 * @rwnx_sta: STA whose TX queues must be stopped
 * @reason: Stop-reason bits to set
 * @rwnx_hw: Driver main data
 *
 * Stop all the TX queues of the STA for the specified reason:
 * - RWNX_TXQ_STOP_STA_PS: the STA is in power save mode
 * - RWNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - RWNX_TXQ_STOP_CHAN: the STA's VIF is not on the current active channel
 *
 * Affected queues are removed from their hardware queue lists.
 *
 * Context: Called with the TX lock held.
 */
void rwnx_txq_sta_stop(struct rwnx_sta *rwnx_sta, u16 reason
#ifdef CONFIG_RWNX_FULLMAC
					   ,
					   struct rwnx_hw *rwnx_hw
#endif
)
{
	struct rwnx_txq *txq;
	int tid;

	if (!rwnx_sta)
		return;
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_sta_stop(rwnx_sta->sta_idx);
#endif
#ifdef CONFIG_MAC80211_TXQ
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0); tid < NX_NB_TXQ_PER_STA;
	     tid++, txq = rwnx_txq_sta_get(rwnx_sta, tid))
#elif defined(CONFIG_RWNX_FULLMAC)
	for (tid = 0, txq = rwnx_txq_sta_get(rwnx_sta, 0, rwnx_hw);
	     tid < (is_multicast_sta(rwnx_sta->sta_idx) ? 1 : NX_NB_TXQ_PER_STA);
	     tid++, txq++)
#endif
	{
		rwnx_txq_stop(txq, reason);
	}
}

#ifdef CONFIG_RWNX_FULLMAC
void rwnx_txq_tdls_sta_start(struct rwnx_vif *rwnx_vif, u16 reason,
			     struct rwnx_hw *rwnx_hw)
{
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_vif_start(rwnx_vif->vif_index);
#endif
	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_vif->sta.tdls_sta)
		rwnx_txq_sta_start(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);

	spin_unlock_bh(&rwnx_hw->tx_lock);
}
#endif

#ifdef CONFIG_RWNX_FULLMAC
void rwnx_txq_tdls_sta_stop(struct rwnx_vif *rwnx_vif, u16 reason,
			    struct rwnx_hw *rwnx_hw)
{
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_vif_stop(rwnx_vif->vif_index);
#endif
	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_vif->sta.tdls_sta)
		rwnx_txq_sta_stop(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);

	spin_unlock_bh(&rwnx_hw->tx_lock);
}
#endif

#ifdef CONFIG_RWNX_FULLMAC
static inline void
rwnx_txq_vif_for_each_sta(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			  void (*f)(struct rwnx_sta *, u16, struct rwnx_hw *),
			  u16 reason) {
	switch (RWNX_VIF_TYPE(rwnx_vif)) {
#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
	case NL80211_IFTYPE_P2P_CLIENT:
#endif
	case NL80211_IFTYPE_STATION: {
		if (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)
			f(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);
		if (!WARN_ON(!rwnx_vif->sta.ap))
			f(rwnx_vif->sta.ap, reason, rwnx_hw);
		break;
	}
#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
	case NL80211_IFTYPE_AP_VLAN:
		rwnx_vif = rwnx_vif->ap_vlan.master;
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_GO: {
		struct rwnx_sta *sta;

		list_for_each_entry(sta, &rwnx_vif->ap.sta_list, list) {
			f(sta, reason, rwnx_hw);
		}
		break;
	}
#endif
	default:
		//WARN_ON_ONCE();
		break;
	}
}
#endif

/**
 * rwnx_txq_vif_start - Start the TX queues associated with a VIF
 *
 * @rwnx_vif: VIF whose queues should be started
 * @reason: Stop-reason bits to clear
 * @rwnx_hw: Driver main data
 *
 * Iterate over all STAs associated with the VIF and restart their queues for
 * @reason. This function takes the TX lock.
 */
void rwnx_txq_vif_start(struct rwnx_vif *rwnx_vif, u16 reason,
			struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_vif_start(rwnx_vif->vif_index);
#endif
	spin_lock_bh(&rwnx_hw->tx_lock);

#ifdef CONFIG_RWNX_FULLMAC
	// Reject if monitor interface
	if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
		goto end;

	if (rwnx_vif->roc_tdls && rwnx_vif->sta.tdls_sta &&
	    rwnx_vif->sta.tdls_sta->tdls.chsw_en) {
		rwnx_txq_sta_start(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);
	}
	if (!rwnx_vif->roc_tdls) {
		rwnx_txq_vif_for_each_sta(rwnx_hw, rwnx_vif, rwnx_txq_sta_start,
					  reason);
	}

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_start(txq, reason);
	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_start(txq, reason);

end:
#endif /* CONFIG_RWNX_FULLMAC */

	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_txq_vif_stop - Stop the TX queues associated with a VIF
 *
 * @rwnx_vif: VIF whose queues should be stopped
 * @reason: Stop-reason bits to set
 * @rwnx_hw: Driver main data
 *
 * Iterate over all STAs associated with the VIF and stop their queues for
 * @reason. This function takes the TX lock.
 */
void rwnx_txq_vif_stop(struct rwnx_vif *rwnx_vif, u16 reason,
		       struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_vif_stop(rwnx_vif->vif_index);
#endif
	spin_lock_bh(&rwnx_hw->tx_lock);

#ifdef CONFIG_RWNX_FULLMAC
	// Reject if monitor interface
	if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
		goto end;

	rwnx_txq_vif_for_each_sta(rwnx_hw, rwnx_vif, rwnx_txq_sta_stop, reason);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_stop(txq, reason);
	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_stop(txq, reason);

end:
#endif /* CONFIG_RWNX_FULLMAC */

	spin_unlock_bh(&rwnx_hw->tx_lock);
}

#ifdef CONFIG_RWNX_FULLMAC

/**
 * rwnx_txq_offchan_start - Start the TX queue for off-channel frames
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_offchan_start(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
	int nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX;

	if (g_rwnx_plat->sdiodev->rwnx_hw->chip_ops->is_old_ic)
		nx_off_chan_txq_idx = NX_OFF_CHAN_TXQ_IDX_FOR_OLD_IC;

	txq = &rwnx_hw->txq[nx_off_chan_txq_idx];
	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_start(txq, RWNX_TXQ_STOP_CHAN);
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_txq_sta_switch_vif - Associate TXQs linked to a STA with a new VIF
 *
 * @sta: STA whose TX queues must be switched
 * @old_vif: VIF currently associated with the STA (may no longer be active)
 * @new_vif: New VIF to associate with the STA
 *
 * This function will switch the vif (i.e. the netdev) associated to all STA's
 * TX queues. This is used when AP_VLAN interfaces are created or removed.
 */
void rwnx_txq_sta_switch_vif(struct rwnx_sta *sta, struct rwnx_vif *old_vif,
			     struct rwnx_vif *new_vif)
{
	struct rwnx_hw *rwnx_hw = new_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int i;

	/* start TXQ on the new interface, and update ndev field in txq */
	if (!netif_carrier_ok(new_vif->ndev))
		netif_carrier_on(new_vif->ndev);
	txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
	for (i = 0; i < NX_NB_TID_PER_STA; i++, txq++) {
		txq->ndev = new_vif->ndev;
		netif_wake_subqueue(txq->ndev, txq->ndev_idx);
	}
}
#endif /* CONFIG_RWNX_FULLMAC */

/******************************************************************************
 * TXQ queue/schedule functions
 *****************************************************************************/
/**
 * rwnx_txq_queue_skb - Queue a buffer in a TX queue
 *
 * @skb: Buffer to queue
 * @txq: TX Queue in which the buffer must be added
 * @rwnx_hw: Driver main data
 * @retry: Should it be queued in the retry list
 *
 * @return: Return 1 if txq has been added to hwq list, 0 otherwise
 *
 * Add a buffer in the buffer list of the TX queue
 * and add this TX queue in the HW queue list if the txq is not stopped.
 * If this is a retry packet it is added after the last retry packet or at the
 * beginning if there is no retry packet queued.
 *
 * If the STA is in PS mode and this is the first packet queued for this txq
 * update TIM.
 *
 * To be called with tx_lock hold
 */
int rwnx_txq_queue_skb(struct sk_buff *skb, struct rwnx_txq *txq,
		       struct rwnx_hw *rwnx_hw, bool retry)
{
#ifdef CONFIG_RWNX_FULLMAC
	if (unlikely(txq->sta && txq->sta->ps.active)) {
		txq->sta->ps.pkt_ready[txq->ps_id]++;
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_ps_queue(txq->sta);
#endif
		if (txq->sta->ps.pkt_ready[txq->ps_id] == 1)
			rwnx_set_traffic_status(rwnx_hw, txq->sta, true, txq->ps_id);
	}
#endif

	if (!retry) {
		/* add buffer in the sk_list */
		skb_queue_tail(&txq->sk_list, skb);
	} else {
		if (txq->last_retry_skb)
			skb_append(txq->last_retry_skb, skb, &txq->sk_list);
		else
			skb_queue_head(&txq->sk_list, skb);

		txq->last_retry_skb = skb;
		txq->nb_retry++;
	}
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_txq_queue_skb(skb, txq, retry);
#endif
	/* Flowctrl corresponding netdev queue if needed */
#ifdef CONFIG_RWNX_FULLMAC
#ifndef CONFIG_AIC8800_ONE_TXQ

#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	if (txq->ndev_idx != NDEV_NO_TXQ &&
	    ((skb_queue_len(&txq->sk_list) > RWNX_NDEV_FLOW_CTRL_STOP) &&
	    !rwnx_hw->sdiodev->flowctrl)) {
//        (atomic_read(&rwnx_hw->sdiodev->tx_priv->tx_pktcnt) >=
//        tx_fc_high_water))) {
#else
	/* If too many buffer are queued for this TXQ stop netdev queue */
	if (txq->ndev_idx != NDEV_NO_TXQ &&
	    (skb_queue_len(&txq->sk_list) > RWNX_NDEV_FLOW_CTRL_STOP)) {
#endif

		txq->status |= RWNX_TXQ_NDEV_FLOW_CTRL;
		netif_stop_subqueue(txq->ndev, txq->ndev_idx);
#ifdef CREATE_TRACE_POINT
		trace_txq_flowctrl_stop(txq);
#endif
	}
#endif /* CONFIG_AIC8800_ONE_TXQ */
#else  /* ! CONFIG_RWNX_FULLMAC */

	if (!retry && ++txq->hwq->len == txq->hwq->len_stop) {
		trace_hwq_flowctrl_stop(txq->hwq->id);
		ieee80211_stop_queue(rwnx_hw->hw, txq->hwq->id);
		rwnx_hw->stats.queues_stops++;
	}
#endif /* CONFIG_RWNX_FULLMAC */

	/* add it in the hwq list if not stopped and not yet present */
	if (!rwnx_txq_is_stopped(txq)) {
		rwnx_txq_add_to_hw_list(txq);
		return 1;
	}

	return 0;
}

/**
 * rwnx_txq_confirm_any - Process buffer confirmed by fw
 *
 * @rwnx_hw: Driver main data
 * @txq: TX Queue
 * @hwq: HW Queue
 * @sw_txhdr: software descriptor of the confirmed packet
 *
 * Process a buffer returned by the fw. It doesn't check buffer status
 * and only does systematic counter update:
 * - hw credit
 * - buffer pushed to fw
 *
 * To be called with tx_lock hold
 */
void rwnx_txq_confirm_any(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq,
			  struct rwnx_hwq *hwq, struct rwnx_sw_txhdr *sw_txhdr)
{
	int user = 0;

#ifdef CONFIG_RWNX_MUMIMO_TX
	user = RWNX_MUMIMO_INFO_POS_ID(sw_txhdr->desc.host.mumimo_info);

	if (txq->idx != TXQ_INACTIVE && txq->pkt_pushed[user] == 1 &&
	    txq->status & RWNX_TXQ_STOP_MU_POS)
		rwnx_txq_start(txq, RWNX_TXQ_STOP_MU_POS);

#endif /* CONFIG_RWNX_MUMIMO_TX */

	if (txq->pkt_pushed[user])
		txq->pkt_pushed[user]--;

	hwq->need_processing = true;
	rwnx_hw->stats.cfm_balance[hwq->id]--;
}

/******************************************************************************
 * HWQ processing
 *****************************************************************************/
static inline bool rwnx_txq_take_mu_lock(struct rwnx_hw *rwnx_hw)
{
	bool res = false;
#ifdef CONFIG_RWNX_MUMIMO_TX
	if (rwnx_hw->mod_params->mutx)
		res = (down_trylock(&rwnx_hw->mu.lock) == 0);
#endif /* CONFIG_RWNX_MUMIMO_TX */
	return res;
}

static inline void rwnx_txq_release_mu_lock(struct rwnx_hw *rwnx_hw)
{
#ifdef CONFIG_RWNX_MUMIMO_TX
	up(&rwnx_hw->mu.lock);
#endif /* CONFIG_RWNX_MUMIMO_TX */
}

static inline void rwnx_txq_set_mu_info(struct rwnx_hw *rwnx_hw,
					struct rwnx_txq *txq, int group_id,
					int pos)
{
#ifdef CONFIG_RWNX_MUMIMO_TX
	trace_txq_select_mu_group(txq, group_id, pos);
	if (group_id) {
		txq->mumimo_info = group_id | (pos << 6);
		rwnx_mu_set_active_group(rwnx_hw, group_id);
	} else {
		txq->mumimo_info = 0;
	}
#endif /* CONFIG_RWNX_MUMIMO_TX */
}

static inline s8 rwnx_txq_get_credits(struct rwnx_txq *txq)
{
	s8 cred = txq->credits;
	/*
	 * if destination is in PS mode, push_limit indicates the maximum
	 * number of packet that can be pushed on this txq.
	 */
	if (txq->push_limit && cred > txq->push_limit)
		cred = txq->push_limit;
	return cred;
}

/**
 * skb_queue_extract - Extract buffer from skb list
 *
 * @list: List of skb to extract from
 * @head: List of skb to append to
 * @nb_elt: Number of skb to extract
 *
 * extract the first @nb_elt of @list and append them to @head
 * It is assume that:
 * - @list contains more that @nb_elt
 * - There is no need to take @list nor @head lock to modify them
 */
static inline void skb_queue_extract(struct sk_buff_head *list,
				     struct sk_buff_head *head, int nb_elt)
{
	int i;
	struct sk_buff *first, *last, *ptr;

	ptr = list->next;
	first = ptr;
	for (i = 0; i < nb_elt; i++)
		ptr = ptr->next;

	last = ptr->prev;

	/* unlink nb_elt in list */
	list->qlen -= nb_elt;
	list->next = ptr;
	ptr->prev = (struct sk_buff *)list;

	/* append nb_elt at end of head */
	head->qlen += nb_elt;
	last->next = (struct sk_buff *)head;
	head->prev->next = first;
	first->prev = head->prev;
	head->prev = last;
}

#ifdef CONFIG_MAC80211_TXQ
/**
 * rwnx_txq_mac80211_dequeue - Dequeue buffer from mac80211 txq and
 *                             add them to push list
 *
 * @rwnx_hw: Main driver data
 * @sk_list: List of buffer to push (initialized without lock)
 * @txq: TXQ to dequeue buffers from
 * @max: Max number of buffer to dequeue
 *
 * Dequeue buffer from mac80211 txq, prepare them for transmission and chain
 * them to the list of buffer to push.
 *
 * Return: %true if no buffers remain in the mac80211 TX queue, otherwise
 * %false.
 */
static bool rwnx_txq_mac80211_dequeue(struct rwnx_hw *rwnx_hw,
				      struct sk_buff_head *sk_list,
				      struct rwnx_txq *txq, int max)
{
	struct ieee80211_txq *mac_txq;
	struct sk_buff *skb;
	unsigned long mac_txq_len;

	if (txq->nb_ready_mac80211 == NOT_MAC80211_TXQ)
		return true;

	mac_txq = container_of((void *)txq, struct ieee80211_txq, drv_priv);

	for (; max > 0; max--) {
		skb = rwnx_tx_dequeue_prep(rwnx_hw, mac_txq);
		if (!skb)
			return true;

		__skb_queue_tail(sk_list, skb);
	}

	/*
	 * re-read mac80211 txq current length.
	 * It is mainly for debug purpose to trace dropped packet. There is no
	 * problems to have nb_ready_mac80211 != actual mac80211 txq length
	 */
	ieee80211_txq_get_depth(mac_txq, &mac_txq_len, NULL);
	if (txq->nb_ready_mac80211 > mac_txq_len)
		trace_txq_drop(txq, txq->nb_ready_mac80211 - mac_txq_len);
	txq->nb_ready_mac80211 = mac_txq_len;

	return (txq->nb_ready_mac80211 == 0);
}
#endif

/**
 * rwnx_txq_get_skb_to_push - Get list of buffer to push for one txq
 *
 * @rwnx_hw: main driver data
 * @hwq: HWQ on wich buffers will be pushed
 * @txq: TXQ to get buffers from
 * @user: user position to use
 * @sk_list_push: list to update
 *
 *
 * This function will returned a list of buffer to push for one txq.
 * It will take into account the number of credit of the HWQ for this user
 * position and TXQ (and push_limit).
 * This allow to get a list that can be pushed without having to test for
 * hwq/txq status after each push
 *
 * If a MU group has been selected for this txq, it will also update the
 * counter for the group
 *
 * Return: %true if the TX queue has no ready buffer after this batch,
 * otherwise %false.
 */
static bool rwnx_txq_get_skb_to_push(struct rwnx_hw *rwnx_hw,
				     struct rwnx_hwq *hwq, struct rwnx_txq *txq,
				     int user,
				     struct sk_buff_head *sk_list_push)
{
	int nb_ready = skb_queue_len(&txq->sk_list);
	int credits = rwnx_txq_get_credits(txq);
	bool res = false;

	__skb_queue_head_init(sk_list_push);

	if (credits >= nb_ready) {
		skb_queue_splice_init(&txq->sk_list, sk_list_push);
#ifdef CONFIG_MAC80211_TXQ
		res = rwnx_txq_mac80211_dequeue(rwnx_hw, sk_list_push, txq,
						credits - nb_ready);
		credits = skb_queue_len(sk_list_push);
#else
		res = true;
		credits = nb_ready;
#endif
	} else {
		skb_queue_extract(&txq->sk_list, sk_list_push, credits);

		/*
		 * When processing PS service period (i.e. push_limit != 0), no longer
		 * process this txq if the buffers extracted will complete the SP for
		 * this txq
		 */
		if (txq->push_limit && credits == txq->push_limit)
			res = true;
	}

	rwnx_mu_set_active_sta(rwnx_hw, rwnx_txq_2_sta(txq), credits);

	return res;
}

/**
 * rwnx_txq_select_user - Select User queue for a txq
 *
 * @rwnx_hw: main driver data
 * @mu_lock: true is MU lock is taken
 * @txq: TXQ to select MU group for
 * @hwq: HWQ for the TXQ
 * @user: Updated with user position selected
 *
 * Return: %true if the TX queue can be processed, otherwise %false.
 *
 * This function selects the MU group to use for a TXQ.
 * The selection is done as follow:
 *
 * - return immediately for STA that don't belongs to any group and select
 *   group 0 / user 0
 *
 * - If MU tx is disabled (by user mutx_on, or because mu group are being
 *   updated !mu_lock), select group 0 / user 0
 *
 * - Use the best group selected by @rwnx_mu_group_sta_select.
 *
 *   Each time a group is selected (except for the first case where sta
 *   doesn't belongs to a MU group), the function checks that no buffer is
 *   pending for this txq on another user position. If this is the case stop
 *   the txq (RWNX_TXQ_STOP_MU_POS) and return false.
 *
 */
static bool rwnx_txq_select_user(struct rwnx_hw *rwnx_hw, bool mu_lock,
				 struct rwnx_txq *txq, struct rwnx_hwq *hwq,
				 int *user)
{
	int pos = 0;
#ifdef CONFIG_RWNX_MUMIMO_TX
	int id, group_id = 0;
	struct rwnx_sta *sta = rwnx_txq_2_sta(txq);

	/* for sta that belong to no group return immediately */
	if (!sta || !sta->group_info.cnt)
		goto end;

	/* If MU is disabled, need to check user */
	if (!rwnx_hw->mod_params->mutx_on || !mu_lock)
		goto check_user;

	/* Use the "best" group selected */
	group_id = sta->group_info.group;

	if (group_id > 0)
		pos = rwnx_mu_group_sta_get_pos(rwnx_hw, sta, group_id);

check_user:
	/* check that we can push on this user position */
#if CONFIG_USER_MAX == 2
	id = (pos + 1) & 0x1;
	if (txq->pkt_pushed[id]) {
		rwnx_txq_stop(txq, RWNX_TXQ_STOP_MU_POS);
		return false;
	}

#else
	for (id = 0; id < CONFIG_USER_MAX; id++) {
		if (id != pos && txq->pkt_pushed[id]) {
			rwnx_txq_stop(txq, RWNX_TXQ_STOP_MU_POS);
			return false;
		}
	}
#endif

end:
	rwnx_txq_set_mu_info(rwnx_hw, txq, group_id, pos);
#endif /* CONFIG_RWNX_MUMIMO_TX */

	*user = pos;
	return true;
}

#define ALL_HWQ_MASK ((1 << CONFIG_USER_MAX) - 1)

/**
 * rwnx_hwq_process - Process one HW queue list
 *
 * @rwnx_hw: Driver main data
 * @hwq: HW queue to process
 *
 * Iterate over all TX queues linked to this hardware queue and push as many
 * buffers as possible. A TX queue on the list has at least one buffer.
 * - If a TX queue has no buffer, remove it and process the next queue.
 * - If a TX queue has no credit or reaches its push limit, remove it and
 *   process the next queue.
 * - If the hardware queue is full, preserve fair ordering for the next call.
 *
 * To be called when HW queue list is modified:
 * - when a buffer is pushed on a TX queue
 * - when new credits are received
 * - when a STA returns from Power Save mode or receives traffic request.
 * - when Channel context change
 *
 * Context: Called with the TX lock held.
 */
void rwnx_hwq_process(struct rwnx_hw *rwnx_hw, struct rwnx_hwq *hwq)
{
	struct rwnx_txq *txq, *next;
	int user;
	bool mu_enable;
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_process_hw_queue(hwq);
#endif
	hwq->need_processing = false;

	mu_enable = rwnx_txq_take_mu_lock(rwnx_hw);

	list_for_each_entry_safe(txq, next, &hwq->list, sched_list) {
		struct rwnx_txhdr *txhdr = NULL;
		struct sk_buff_head sk_list_push;
		struct sk_buff *skb;
		bool txq_empty;
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_process_txq(txq);
#endif
		/* sanity check for debug */
		WARN_ON_ONCE(!(txq->status & RWNX_TXQ_IN_HWQ_LIST));
		if (txq->idx == TXQ_INACTIVE) {
			AICWFDBG(LOGDEBUG, "%s txq->idx == TXQ_INACTIVE \r\n", __func__);
			rwnx_txq_del_from_hw_list(txq);
			rwnx_txq_flush(rwnx_hw, txq);
			continue;
		}
		WARN_ON_ONCE(txq->idx == TXQ_INACTIVE);
		WARN_ON_ONCE(txq->credits <= 0);
		WARN_ON_ONCE(!rwnx_txq_skb_ready(txq));

		if (!rwnx_txq_select_user(rwnx_hw, mu_enable, txq, hwq, &user)) {
			AICWFDBG(LOGDEBUG, "select user:%d\n", user);
			continue;
		}

		txq_empty =
			rwnx_txq_get_skb_to_push(rwnx_hw, hwq, txq, user, &sk_list_push);
		while ((skb = __skb_dequeue(&sk_list_push)) != NULL) {
			txhdr = (struct rwnx_txhdr *)skb->data;
			rwnx_tx_push(rwnx_hw, txhdr, 0);
		}

		if (txq_empty) {
			rwnx_txq_del_from_hw_list(txq);
			txq->pkt_sent = 0;
		} else if (rwnx_txq_is_scheduled(txq)) {
			/* txq not empty,
			 * To avoid starving need to process other txq in the list
			 * For better aggregation, need to send "as many consecutive
			 * pkt as possible" for he same txq
			 * ==> Add counter to trigger txq switch
			 */
			if (txq->pkt_sent > hwq->size) {
				txq->pkt_sent = 0;
				list_rotate_left(&hwq->list);
			}
		}

#ifdef CONFIG_RWNX_FULLMAC
		/* Unable to complete PS traffic request because of hwq credit */
		if (txq->push_limit && txq->sta) {
			if (txq->ps_id == LEGACY_PS_ID) {
				/* for legacy PS abort SP and wait next ps-poll */
				txq->sta->ps.sp_cnt[txq->ps_id] -= txq->push_limit;
				txq->push_limit = 0;
			}
			/* for u-apsd need to complete the SP to send EOSP frame */
		}
#ifndef CONFIG_AIC8800_ONE_TXQ
		/* restart netdev queue if number of queued buffer is below threshold */
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
		if (unlikely(txq->status & RWNX_TXQ_NDEV_FLOW_CTRL) &&
		    (skb_queue_len(&txq->sk_list) < RWNX_NDEV_FLOW_CTRL_RESTART)) {
#else
		if (unlikely(txq->status & RWNX_TXQ_NDEV_FLOW_CTRL) &&
		    skb_queue_len(&txq->sk_list) < RWNX_NDEV_FLOW_CTRL_RESTART) {
#endif

			txq->status &= ~RWNX_TXQ_NDEV_FLOW_CTRL;
			netif_wake_subqueue(txq->ndev, txq->ndev_idx);
#ifdef AIC8800_CREATE_TRACE_POINTS
			trace_txq_flowctrl_restart(txq);
#endif
		}
#endif /* CONFIG_AIC8800_ONE_TXQ */
#endif /* CONFIG_RWNX_FULLMAC */
	}

	if (mu_enable)
		rwnx_txq_release_mu_lock(rwnx_hw);
}

/**
 * rwnx_hwq_process_all - Process all HW queue list
 *
 * @rwnx_hw: Driver main data
 *
 * Loop over all HWQ, and process them if needed
 * To be called with tx_lock hold
 */
void rwnx_hwq_process_all(struct rwnx_hw *rwnx_hw)
{
	int id;

	rwnx_mu_group_sta_select(rwnx_hw);

	for (id = ARRAY_SIZE(rwnx_hw->hwq) - 1; id >= 0; id--) {
		if (rwnx_hw->hwq[id].need_processing)
			rwnx_hwq_process(rwnx_hw, &rwnx_hw->hwq[id]);
	}
}

/**
 * rwnx_hwq_init - Initialize all hwq structures
 *
 * @rwnx_hw: Driver main data
 *
 */
void rwnx_hwq_init(struct rwnx_hw *rwnx_hw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rwnx_hw->hwq); i++) {
		struct rwnx_hwq *hwq = &rwnx_hw->hwq[i];

		hwq->id = i;
		hwq->size = nx_txdesc_cnt[i];
		INIT_LIST_HEAD(&hwq->list);
	}
}
