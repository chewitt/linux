/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_PROF_H_
#define _RWNX_PROF_H_

#include "reg_access.h"
#include "rwnx_platform.h"
#include "rwnx_radar.h"

static inline void rwnx_prof_set(struct rwnx_hw *rwnx_hw, int val)
{
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;

	RWNX_REG_WRITE(val, rwnx_plat, RWNX_ADDR_SYSTEM,
		       NXMAC_SW_SET_PROFILING_ADDR);
}

static inline void rwnx_prof_clear(struct rwnx_hw *rwnx_hw, int val)
{
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;

	RWNX_REG_WRITE(val, rwnx_plat, RWNX_ADDR_SYSTEM,
		       NXMAC_SW_CLEAR_PROFILING_ADDR);
}

enum {
	SW_PROF_HOSTBUF_IDX = 12,
	/****** IPC IRQs related signals ******/
	/* E2A direction */
	SW_PROF_IRQ_E2A_RXDESC =
		16, // to make sure we let 16 bits available for LMAC FW
	SW_PROF_IRQ_E2A_TXCFM,
	SW_PROF_IRQ_E2A_DBG,
	SW_PROF_IRQ_E2A_MSG,
	SW_PROF_IPC_MSGPUSH,
	SW_PROF_MSGALLOC,
	SW_PROF_MSGIND,
	SW_PROF_DBGIND,

	/* A2E direction */
	SW_PROF_IRQ_A2E_TXCFM_BACK,

	/****** Driver functions related signals ******/
	SW_PROF_WAIT_QUEUE_STOP,
	SW_PROF_WAIT_QUEUE_WAKEUP,
	SW_PROF_RWNXDATAIND,
	SW_PROF_RWNX_IPC_IRQ_HDLR,
	SW_PROF_RWNX_IPC_THR_IRQ_HDLR,
	SW_PROF_IEEE80211RX,
	SW_PROF_RWNX_PATTERN,
	SW_PROF_MAX
};

// [LT]For debug purpose only
#if (0)
#define SW_PROF_CHAN_CTXT_CFM_HDL_BIT    (21)
#define SW_PROF_CHAN_CTXT_CFM_BIT        (22)
#define SW_PROF_CHAN_CTXT_CFM_SWDONE_BIT (23)
#define SW_PROF_CHAN_CTXT_PUSH_BIT       (24)
#define SW_PROF_CHAN_CTXT_QUEUE_BIT      (25)
#define SW_PROF_CHAN_CTXT_TX_BIT         (26)
#define SW_PROF_CHAN_CTXT_TX_PAUSE_BIT   (27)
#define SW_PROF_CHAN_CTXT_PSWTCH_BIT     (28)
#define SW_PROF_CHAN_CTXT_SWTCH_BIT      (29)

#else
#define SW_PROF_CHAN_CTXT_CFM_HDL_BIT    (0)
#define SW_PROF_CHAN_CTXT_CFM_BIT        (0)
#define SW_PROF_CHAN_CTXT_CFM_SWDONE_BIT (0)
#define SW_PROF_CHAN_CTXT_PUSH_BIT       (0)
#define SW_PROF_CHAN_CTXT_QUEUE_BIT      (0)
#define SW_PROF_CHAN_CTXT_TX_BIT         (0)
#define SW_PROF_CHAN_CTXT_TX_PAUSE_BIT   (0)
#define SW_PROF_CHAN_CTXT_PSWTCH_BIT     (0)
#define SW_PROF_CHAN_CTXT_SWTCH_BIT      (0)
#endif

static inline void REG_SW_SET_PROFILING_CHAN(void *env, int bit)
{
#if (0)
	rwnx_prof_clear((struct rwnx_hw *)env, BIT(bit));
#else

#endif
}

static inline void REG_SW_CLEAR_PROFILING_CHAN(void *env, int bit)
{
#if (0)
	rwnx_prof_clear((struct rwnx_hw *)env, BIT(bit));
#else

#endif
}

#ifdef CONFIG_RWNX_SW_PROFILING
/* Macros for SW PRofiling registers access */
static inline void REG_SW_SET_PROFILING(void *env, int bit)
{
	rwnx_prof_set((struct rwnx_hw *)env, BIT(bit));
}

static inline void REG_SW_SET_HOSTBUF_IDX_PROFILING(void *env, int val)
{
	rwnx_prof_set((struct rwnx_hw *)env, (val) << (SW_PROF_HOSTBUF_IDX));
}

static inline void REG_SW_CLEAR_PROFILING(void *env, int bit)
{
	rwnx_prof_clear((struct rwnx_hw *)env, BIT(bit));
}

static inline void REG_SW_CLEAR_HOSTBUF_IDX_PROFILING(void *env)
{
	rwnx_prof_clear((struct rwnx_hw *)env, 0x0F << (SW_PROF_HOSTBUF_IDX));
}

#else
static inline void REG_SW_SET_PROFILING(void *env, int value)
{
	(void)env;
	(void)value;
}

static inline void REG_SW_CLEAR_PROFILING(void *env, int value)
{
	(void)env;
	(void)value;
}

static inline void REG_SW_SET_HOSTBUF_IDX_PROFILING(void *env, int val)
{
	(void)env;
	(void)val;
}

static inline void REG_SW_CLEAR_HOSTBUF_IDX_PROFILING(void *env)
{
	(void)env;
}
#endif

#endif /* _RWNX_PROF_H_ */
