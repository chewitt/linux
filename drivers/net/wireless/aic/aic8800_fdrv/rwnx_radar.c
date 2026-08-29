// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_radar.c
 *
 * @brief Functions to handle radar detection
 * Radar detection is copied (and adapted) from ath driver source code.
 *
 ******************************************************************************
 */
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <net/mac80211.h>

#include "rwnx_compat.h"
#include "rwnx_defs.h"
#include "rwnx_events.h"
#include "rwnx_msg_tx.h"
#include "rwnx_radar.h"

/*
 * tolerated deviation of radar time stamp in usecs on both sides
 * TODO: this might need to be HW-dependent
 */
#define PRI_TOLERANCE 16

/**
 * struct radar_types - contains array of patterns defined for one DFS domain
 * @region: DFS regulatory domain
 * @num_radar_types: number of radar types to follow
 * @spec_riu: radar patterns for the RIU detector
 * @spec_fcu: radar patterns for the FCU detector
 */
struct radar_types {
	enum nl80211_dfs_regions region;
	u32 num_radar_types;
	const struct radar_detector_specs *spec_riu;
	const struct radar_detector_specs *spec_fcu;
};

/* percentage on ppb threshold to trigger detection */
#define MIN_PPB_THRESH  50
#define PPB_THRESH(PPB) (((PPB) * MIN_PPB_THRESH + 50) / 100)
//#define PRF2PRI(PRF)    ((1000000 + (PRF) / 2) / (PRF))
#define PRF2PRI(PRF) \
	({ \
		typeof(PRF) _prf = (PRF); \
		((1000000 + (_prf) / 2) / (_prf)); \
	})

/* width tolerance */
#define WIDTH_TOLERANCE 2
#define WIDTH_LOWER(X)  (X)
#define WIDTH_UPPER(X)  (X)

static inline struct radar_detector_specs
	ETSI_PATTERN_SHORT(u8 ID, u8 WMIN, u8 WMAX, u16 PMIN, u16 PMAX, u8 PPB)
{
	struct radar_detector_specs spec = {
		.type_id = ID,
		.width_min = WIDTH_LOWER(WMIN),
		.width_max = WIDTH_UPPER(WMAX),
		.pri_min = (PRF2PRI(PMAX) - PRI_TOLERANCE),
		.pri_max = (PRF2PRI(PMIN) + PRI_TOLERANCE),
		.num_pri = 1,
		.ppb = PPB,
		.ppb_thresh = PPB_THRESH(PPB),
		.max_pri_tolerance = PRI_TOLERANCE,
		.type = RADAR_WAVEFORM_SHORT
	};
	return spec;
}

static inline struct radar_detector_specs
	ETSI_PATTERN_INTERLEAVED(u8 ID, u8 WMIN, u8 WMAX, u16 PMIN, u16 PMAX,
				 u16 PRFMIN, u16 PRFMAX, u8 PPB)
{
	struct radar_detector_specs spec = {
		.type_id = ID,
		.width_min = WIDTH_LOWER(WMIN),
		.width_max = WIDTH_UPPER(WMAX),
		.pri_min = (PRF2PRI(PMAX) * (PRFMIN) - PRI_TOLERANCE),
		.pri_max = (PRF2PRI(PMIN) * (PRFMAX) + PRI_TOLERANCE),
		.num_pri = PRFMAX,
		.ppb = (PPB) * (PRFMAX),
		.ppb_thresh = PPB_THRESH(PPB),
		.max_pri_tolerance = PRI_TOLERANCE,
		.type = RADAR_WAVEFORM_INTERLEAVED
	};
	return spec;
}

/* radar types as defined by ETSI EN-301-893 v1.7.1 */
static const struct radar_detector_specs etsi_radar_ref_types_v17_riu[] = {
	{0,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (700) / 2) / (700)) - PRI_TOLERANCE),
	 (((1000000 + (700) / 2) / (700)) + PRI_TOLERANCE),
	 1,
	 18,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{1,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(10),
	 (((1000000 + (1000) / 2) / (1000)) - PRI_TOLERANCE),
	 (((1000000 + (200) / 2) / (200)) + PRI_TOLERANCE),
	 1,
	 10,
	 PPB_THRESH(10),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{2,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(22),
	 (((1000000 + (1600) / 2) / (1600)) - PRI_TOLERANCE),
	 (((1000000 + (200) / 2) / (200)) + PRI_TOLERANCE),
	 1,
	 15,
	 PPB_THRESH(15),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{3,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(22),
	 (((1000000 + (4000) / 2) / (4000)) - PRI_TOLERANCE),
	 (((1000000 + (2300) / 2) / (2300)) + PRI_TOLERANCE),
	 1,
	 25,
	 PPB_THRESH(25),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{4,
	 WIDTH_LOWER(20),
	 WIDTH_UPPER(38),
	 (((1000000 + (4000) / 2) / (4000)) - PRI_TOLERANCE),
	 (((1000000 + (2000) / 2) / (2000)) + PRI_TOLERANCE),
	 1,
	 20,
	 PPB_THRESH(20),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{5,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (400) / 2) / (400)) * 2 - PRI_TOLERANCE),
	 (((1000000 + (300) / 2) / (300)) * 3 + PRI_TOLERANCE),
	 3,
	 10 * 3,
	 PPB_THRESH(10),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_INTERLEAVED},
	{6,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (1200) / 2) / (1200)) * 2 - PRI_TOLERANCE),
	 (((1000000 + (400) / 2) / (400)) * 3 + PRI_TOLERANCE),
	 3,
	 15 * 3,
	 PPB_THRESH(15),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_INTERLEAVED},
};

static const struct radar_detector_specs etsi_radar_ref_types_v17_fcu[] = {
	{0,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (700) / 2) / (700)) - PRI_TOLERANCE),
	 (((1000000 + (700) / 2) / (700)) + PRI_TOLERANCE),
	 1,
	 18,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{1,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (1000) / 2) / (1000)) - PRI_TOLERANCE),
	 (((1000000 + (200) / 2) / (200)) + PRI_TOLERANCE),
	 1,
	 10,
	 PPB_THRESH(10),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{2,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(16),
	 (((1000000 + (1600) / 2) / (1600)) - PRI_TOLERANCE),
	 (((1000000 + (200) / 2) / (200)) + PRI_TOLERANCE),
	 1,
	 15,
	 PPB_THRESH(15),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{3,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(16),
	 (((1000000 + (4000) / 2) / (4000)) - PRI_TOLERANCE),
	 (((1000000 + (2300) / 2) / (2300)) + PRI_TOLERANCE),
	 1,
	 25,
	 PPB_THRESH(25),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{4,
	 WIDTH_LOWER(20),
	 WIDTH_UPPER(34),
	 (((1000000 + (4000) / 2) / (4000)) - PRI_TOLERANCE),
	 (((1000000 + (2000) / 2) / (2000)) + PRI_TOLERANCE),
	 1,
	 20,
	 PPB_THRESH(20),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{5,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (400) / 2) / (400)) * 2 - PRI_TOLERANCE),
	 (((1000000 + (300) / 2) / (300)) * 3 + PRI_TOLERANCE),
	 3,
	 10 * 3,
	 PPB_THRESH(10),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_INTERLEAVED},
	{6,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 (((1000000 + (1200) / 2) / (1200)) * 2 - PRI_TOLERANCE),
	 (((1000000 + (400) / 2) / (400)) * 3 + PRI_TOLERANCE),
	 3,
	 15 * 3,
	 PPB_THRESH(15),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_INTERLEAVED},
};

static const struct radar_types etsi_radar_types_v17 = {
	.region = NL80211_DFS_ETSI,
	.num_radar_types = ARRAY_SIZE(etsi_radar_ref_types_v17_riu),
	.spec_riu = etsi_radar_ref_types_v17_riu,
	.spec_fcu = etsi_radar_ref_types_v17_fcu,
};

static inline struct radar_detector_specs
	FCC_PATTERN(u8 ID, u8 WMIN, u8 WMAX, u16 PMIN, u16 PMAX,
		    u16 PRF, u8 PPB, enum radar_waveform_type TYPE)
{
	struct radar_detector_specs spec = {
		.type_id = ID,
		.width_min = WIDTH_LOWER(WMIN),
		.width_max = WIDTH_UPPER(WMAX),
		.pri_min = (PMIN) - PRI_TOLERANCE,
		.pri_max = (PMAX) * (PRF) + PRI_TOLERANCE,
		.num_pri = PRF,
		.ppb = (PPB) * (PRF),
		.ppb_thresh = PPB_THRESH(PPB),
		.max_pri_tolerance = PRI_TOLERANCE,
		.type = TYPE
	};
	return spec;
}

static const struct radar_detector_specs fcc_radar_ref_types_riu[] = {
	{0,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 1428 - PRI_TOLERANCE,
	 1428 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{1,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 518 - PRI_TOLERANCE,
	 3066 * 1 + PRI_TOLERANCE,
	 1,
	 102 * 1,
	 PPB_THRESH(102),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_WEATHER},
	{2,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 150 - PRI_TOLERANCE,
	 230 * 1 + PRI_TOLERANCE,
	 1,
	 23 * 1,
	 PPB_THRESH(23),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{3,
	 WIDTH_LOWER(6),
	 WIDTH_UPPER(20),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 16 * 1,
	 PPB_THRESH(16),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{4,
	 WIDTH_LOWER(10),
	 WIDTH_UPPER(28),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 12 * 1,
	 PPB_THRESH(12),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{5,
	 WIDTH_LOWER(50),
	 WIDTH_UPPER(110),
	 1000 - PRI_TOLERANCE,
	 2000 * 1 + PRI_TOLERANCE,
	 1,
	 8 * 1,
	 PPB_THRESH(8),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_LONG},
	{6,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 333 - PRI_TOLERANCE,
	 333 * 1 + PRI_TOLERANCE,
	 1,
	 9 * 1,
	 PPB_THRESH(9),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
};

static const struct radar_detector_specs fcc_radar_ref_types_fcu[] = {
	{0,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 1428 - PRI_TOLERANCE,
	 1428 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{1,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 518 - PRI_TOLERANCE,
	 3066 * 1 + PRI_TOLERANCE,
	 1,
	 102 * 1,
	 PPB_THRESH(102),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_WEATHER},
	{2,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 150 - PRI_TOLERANCE,
	 230 * 1 + PRI_TOLERANCE,
	 1,
	 23 * 1,
	 PPB_THRESH(23),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{3,
	 WIDTH_LOWER(6),
	 WIDTH_UPPER(12),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 16 * 1,
	 PPB_THRESH(16),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{4,
	 WIDTH_LOWER(10),
	 WIDTH_UPPER(22),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 12 * 1,
	 PPB_THRESH(12),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{5,
	 WIDTH_LOWER(50),
	 WIDTH_UPPER(104),
	 1000 - PRI_TOLERANCE,
	 2000 * 1 + PRI_TOLERANCE,
	 1,
	 8 * 1,
	 PPB_THRESH(8),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_LONG},
	{6,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 333 - PRI_TOLERANCE,
	 333 * 1 + PRI_TOLERANCE,
	 1,
	 9 * 1,
	 PPB_THRESH(9),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
};

static const struct radar_types fcc_radar_types = {
	.region = NL80211_DFS_FCC,
	.num_radar_types = ARRAY_SIZE(fcc_radar_ref_types_riu),
	.spec_riu = fcc_radar_ref_types_riu,
	.spec_fcu = fcc_radar_ref_types_fcu,
};

#define JP_PATTERN FCC_PATTERN
static const struct radar_detector_specs jp_radar_ref_types_riu[] = {
	{0,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 1428 - PRI_TOLERANCE,
	 1428 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{1,
	 WIDTH_LOWER(2),
	 WIDTH_UPPER(8),
	 3846 - PRI_TOLERANCE,
	 3846 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{2,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 1388 - PRI_TOLERANCE,
	 1388 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{3,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 4000 - PRI_TOLERANCE,
	 4000 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{4,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 150 - PRI_TOLERANCE,
	 230 * 1 + PRI_TOLERANCE,
	 1,
	 23 * 1,
	 PPB_THRESH(23),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{5,
	 WIDTH_LOWER(6),
	 WIDTH_UPPER(20),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 16 * 1,
	 PPB_THRESH(16),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{6,
	 WIDTH_LOWER(10),
	 WIDTH_UPPER(28),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 12 * 1,
	 PPB_THRESH(12),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{7,
	 WIDTH_LOWER(50),
	 WIDTH_UPPER(110),
	 1000 - PRI_TOLERANCE,
	 2000 * 1 + PRI_TOLERANCE,
	 1,
	 8 * 1,
	 PPB_THRESH(8),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_LONG},
	{8,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 333 - PRI_TOLERANCE,
	 333 * 1 + PRI_TOLERANCE,
	 1,
	 9 * 1,
	 PPB_THRESH(9),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
};

static const struct radar_detector_specs jp_radar_ref_types_fcu[] = {
	{0,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 1428 - PRI_TOLERANCE,
	 1428 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{1,
	 WIDTH_LOWER(2),
	 WIDTH_UPPER(6),
	 3846 - PRI_TOLERANCE,
	 3846 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{2,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 1388 - PRI_TOLERANCE,
	 1388 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{3,
	 WIDTH_LOWER(2),
	 WIDTH_UPPER(2),
	 4000 - PRI_TOLERANCE,
	 4000 * 1 + PRI_TOLERANCE,
	 1,
	 18 * 1,
	 PPB_THRESH(18),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{4,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 150 - PRI_TOLERANCE,
	 230 * 1 + PRI_TOLERANCE,
	 1,
	 23 * 1,
	 PPB_THRESH(23),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{5,
	 WIDTH_LOWER(6),
	 WIDTH_UPPER(12),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 16 * 1,
	 PPB_THRESH(16),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{6,
	 WIDTH_LOWER(10),
	 WIDTH_UPPER(22),
	 200 - PRI_TOLERANCE,
	 500 * 1 + PRI_TOLERANCE,
	 1,
	 12 * 1,
	 PPB_THRESH(12),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
	{7,
	 WIDTH_LOWER(50),
	 WIDTH_UPPER(104),
	 1000 - PRI_TOLERANCE,
	 2000 * 1 + PRI_TOLERANCE,
	 1,
	 8 * 1,
	 PPB_THRESH(8),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_LONG},
	{8,
	 WIDTH_LOWER(0),
	 WIDTH_UPPER(8),
	 333 - PRI_TOLERANCE,
	 333 * 1 + PRI_TOLERANCE,
	 1,
	 9 * 1,
	 PPB_THRESH(9),
	 PRI_TOLERANCE,
	 RADAR_WAVEFORM_SHORT},
};

static const struct radar_types jp_radar_types = {
	.region = NL80211_DFS_JP,
	.num_radar_types = ARRAY_SIZE(jp_radar_ref_types_riu),
	.spec_riu = jp_radar_ref_types_riu,
	.spec_fcu = jp_radar_ref_types_fcu,
};

static const struct radar_types *dfs_domains[] = {
	&etsi_radar_types_v17,
	&fcc_radar_types,
	&jp_radar_types,
};

/**
 * struct pri_detector_ops - PRI detector ops (dependent of waveform type)
 * @init : Initialize pri_detector structure
 * @add_pulse : Add a pulse to the pri-detector
 * @reset_on_pri_overflow : Should the pri_detector be resetted when pri
 * overflow
 */
struct pri_detector_ops {
	void (*init)(struct pri_detector *pde);
	struct pri_sequence *(*add_pulse)(struct pri_detector *pde, u16 len, u64 ts,
					  u16 pri);
	int reset_on_pri_overflow;
};

/******************************************************************************
 * PRI (pulse repetition interval) sequence detection
 *****************************************************************************/
/*
 * Singleton Pulse and Sequence Pools
 *
 * Instances of pri_sequence and pulse_elem are kept in singleton pools to
 * reduce the number of dynamic allocations. They are shared between all
 * instances and grow up to the peak number of simultaneously used objects.
 *
 * Memory is freed after all references to the pools are released.
 */
static u32 singleton_pool_references;
static LIST_HEAD(pulse_pool);
static LIST_HEAD(pseq_pool);
static DEFINE_SPINLOCK(pool_lock);

static void pool_register_ref(void)
{
	spin_lock_bh(&pool_lock);
	singleton_pool_references++;
	spin_unlock_bh(&pool_lock);
}

static void pool_deregister_ref(void)
{
	spin_lock_bh(&pool_lock);
	singleton_pool_references--;
	if (singleton_pool_references == 0) {
		/* free singleton pools with no references left */
		struct pri_sequence *ps, *ps0;
		struct pulse_elem *p, *p0;

		list_for_each_entry_safe(p, p0, &pulse_pool, head) {
			list_del(&p->head);
			kfree(p);
		}
		list_for_each_entry_safe(ps, ps0, &pseq_pool, head) {
			list_del(&ps->head);
			kfree(ps);
		}
	}
	spin_unlock_bh(&pool_lock);
}

static void pool_put_pulse_elem(struct pulse_elem *pe)
{
	spin_lock_bh(&pool_lock);
	list_add(&pe->head, &pulse_pool);
	spin_unlock_bh(&pool_lock);
}

static void pool_put_pseq_elem(struct pri_sequence *pse)
{
	spin_lock_bh(&pool_lock);
	list_add(&pse->head, &pseq_pool);
	spin_unlock_bh(&pool_lock);
}

static struct pri_sequence *pool_get_pseq_elem(void)
{
	struct pri_sequence *pse = NULL;

	spin_lock_bh(&pool_lock);
	if (!list_empty(&pseq_pool)) {
		pse = list_first_entry(&pseq_pool, struct pri_sequence, head);
		list_del(&pse->head);
	}
	spin_unlock_bh(&pool_lock);

	if (!pse)
		pse = kmalloc_obj(*pse, GFP_ATOMIC);
	return pse;
}

static struct pulse_elem *pool_get_pulse_elem(void)
{
	struct pulse_elem *pe = NULL;

	spin_lock_bh(&pool_lock);
	if (!list_empty(&pulse_pool)) {
		pe = list_first_entry(&pulse_pool, struct pulse_elem, head);
		list_del(&pe->head);
	}
	spin_unlock_bh(&pool_lock);
	return pe;
}

static struct pulse_elem *pulse_queue_get_tail(struct pri_detector *pde)
{
	struct list_head *l = &pde->pulses;

	if (list_empty(l))
		return NULL;
	return list_entry(l->prev, struct pulse_elem, head);
}

static bool pulse_queue_dequeue(struct pri_detector *pde)
{
	struct pulse_elem *p = pulse_queue_get_tail(pde);

	if (p) {
		list_del_init(&p->head);
		pde->count--;
		/* give it back to pool */
		pool_put_pulse_elem(p);
	}
	return (pde->count > 0);
}

/**
 * pulse_queue_check_window - remove pulses older than window
 * @pde: pointer on pri_detector
 *
 *  dequeue pulse that are too old.
 */
static void pulse_queue_check_window(struct pri_detector *pde)
{
	u64 min_valid_ts;
	struct pulse_elem *p;

	/* there is no delta time with less than 2 pulses */
	if (pde->count < 2)
		return;

	if (pde->last_ts <= pde->window_size)
		return;

	min_valid_ts = pde->last_ts - pde->window_size;
	while ((p = pulse_queue_get_tail(pde)) != NULL) {
		if (p->ts >= min_valid_ts)
			return;
		pulse_queue_dequeue(pde);
	}
}

/*
 * pulse_queue_enqueue - Queue one pulse
 * @pde: pointer on pri_detector
 *
 * Add one pulse to the list. If the maximum number of pulses
 * if reached, remove oldest one.
 */
static bool pulse_queue_enqueue(struct pri_detector *pde, u64 ts)
{
	struct pulse_elem *p = pool_get_pulse_elem();

	if (!p) {
		p = kmalloc_obj(*p, GFP_ATOMIC);
		if (!p)
			return false;
	}
	INIT_LIST_HEAD(&p->head);
	p->ts = ts;
	list_add(&p->head, &pde->pulses);
	pde->count++;
	pde->last_ts = ts;
	pulse_queue_check_window(pde);
	if (pde->count >= pde->max_count)
		pulse_queue_dequeue(pde);

	return true;
}

/***************************************************************************
 * Short waveform
 **************************************************************************/
/*
 * pde_get_multiple() - get number of multiples considering a given tolerance
 * @return factor if abs(val - factor*fraction) <= tolerance, 0 otherwise
 */
static u32 pde_get_multiple(u32 val, u32 fraction, u32 tolerance)
{
	u32 remainder;
	u32 factor;
	u32 delta;

	if (fraction == 0)
		return 0;

	delta = (val < fraction) ? (fraction - val) : (val - fraction);

	if (delta <= tolerance)
		/* val and fraction are within tolerance */
		return 1;

	factor = val / fraction;
	remainder = val % fraction;
	if (remainder > tolerance) {
		/* no exact match */
		if ((fraction - remainder) <= tolerance)
			/* remainder is within tolerance */
			factor++;
		else
			factor = 0;
	}
	return factor;
}

/**
 * pde_short_create_sequences - create_sequences function for
 *                              SHORT/WEATHER/INTERLEAVED radar waveform
 * @pde: pointer on pri_detector
 * @ts: timestamp of the pulse
 * @min_count: Minimum number of pulse to be present in the sequence.
 *             (With this pulse there is already a sequence with @min_count
 *              pulse, so if we can't create a sequence with more pulse don't
 *              create it)
 * @return: false if an error occurred (memory allocation) true otherwise
 *
 * For each pulses queued check if we can create a sequence with
 * pri = (ts - pulse_queued.ts) which contains more than @min_count pulses.
 *
 */
static bool pde_short_create_sequences(struct pri_detector *pde, u64 ts,
				       u32 min_count)
{
	struct pulse_elem *p;
	u16 pulse_idx = 0;

	list_for_each_entry(p, &pde->pulses, head) {
		struct pri_sequence ps, *new_ps;
		struct pulse_elem *p2;
		u32 tmp_false_count;
		u64 min_valid_ts;
		u32 delta_ts = ts - p->ts;

		pulse_idx++;

		if (delta_ts < pde->rs->pri_min)
			/* ignore too small pri */
			continue;

		if (delta_ts > pde->rs->pri_max)
			/* stop on too large pri (sorted list) */
			break;

		/* build a new sequence with new potential pri */
		ps.count = 2;
		ps.count_falses = pulse_idx - 1;
		ps.first_ts = p->ts;
		ps.last_ts = ts;
		ps.pri = ts - p->ts;
		ps.dur = ps.pri * (pde->rs->ppb - 1) + 2 * pde->rs->max_pri_tolerance;

		p2 = p;
		tmp_false_count = 0;
		if (ps.dur > ts)
			min_valid_ts = 0;
		else
			min_valid_ts = ts - ps.dur;
		/* check which past pulses are candidates for new sequence */
		list_for_each_entry_continue(p2, &pde->pulses, head) {
			u32 factor;

			if (p2->ts < min_valid_ts)
				/* stop on crossing window border */
				break;
			/* check if pulse match (multi)PRI */
			factor = pde_get_multiple(ps.last_ts - p2->ts, ps.pri,
						  pde->rs->max_pri_tolerance);
			if (factor > 0) {
				ps.count++;
				ps.first_ts = p2->ts;
				/*
				 * on match, add the intermediate falses
				 * and reset counter
				 */
				ps.count_falses += tmp_false_count;
				tmp_false_count = 0;
			} else {
				/* this is a potential false one */
				tmp_false_count++;
			}
		}
		if (ps.count <= min_count) {
			/* did not reach minimum count, drop sequence */
			continue;
		}
		/* this is a valid one, add it */
		ps.deadline_ts = ps.first_ts + ps.dur;
		if (pde->rs->type == RADAR_WAVEFORM_WEATHER) {
			ps.ppb_thresh = 19000000 / (360 * ps.pri);
			ps.ppb_thresh = PPB_THRESH(ps.ppb_thresh);
		} else {
			ps.ppb_thresh = pde->rs->ppb_thresh;
		}

		new_ps = pool_get_pseq_elem();
		if (!new_ps)
			return false;
		memcpy(new_ps, &ps, sizeof(ps));
		INIT_LIST_HEAD(&new_ps->head);
		list_add(&new_ps->head, &pde->sequences);
	}
	return true;
}

/**
 * pde_short_add_to_existing_seqs - add_to_existing_seqs function for
 *                                  SHORT/WEATHER/INTERLEAVED radar waveform
 * @pde: pointer on pri_detector
 * @ts: timestamp of the pulse
 *
 * Check all sequemces created for this pde.
 *  - If the sequence is too old delete it.
 *  - Else if the delta with the previous pulse match the pri of the sequence
 *    add the pulse to this sequence. If the pulse cannot be added it is added
 *    to the false pulses for this sequence
 *
 * Return: Length of the longest sequence to which the pulse was added.
 */
static u32 pde_short_add_to_existing_seqs(struct pri_detector *pde, u64 ts)
{
	u32 max_count = 0;
	struct pri_sequence *ps, *ps2;

	list_for_each_entry_safe(ps, ps2, &pde->sequences, head) {
		u32 delta_ts;
		u32 factor;

		/* first ensure that sequence is within window */
		if (ts > ps->deadline_ts) {
			list_del_init(&ps->head);
			pool_put_pseq_elem(ps);
			continue;
		}

		delta_ts = ts - ps->last_ts;
		factor =
			pde_get_multiple(delta_ts, ps->pri, pde->rs->max_pri_tolerance);

		if (factor > 0) {
			ps->last_ts = ts;
			ps->count++;

			if (max_count < ps->count)
				max_count = ps->count;
		} else {
			ps->count_falses++;
		}
	}
	return max_count;
}

/**
 * pde_short_check_detection - check_detection function for
 *                             SHORT/WEATHER/INTERLEAVED radar waveform
 * @pde: pointer on pri_detector
 *
 * Check all sequemces created for this pde.
 *  - If a sequence contains more pulses than the threshold and more matching
 *    that false pulses.
 *
 * Return: The first complete sequence, or %NULL if none is complete.
 */
static struct pri_sequence *pde_short_check_detection(struct pri_detector *pde)
{
	struct pri_sequence *ps;

	if (list_empty(&pde->sequences))
		return NULL;

	list_for_each_entry(ps, &pde->sequences, head) {
		/*
		 * we assume to have enough matching confidence if we
		 * 1) have enough pulses
		 * 2) have more matching than false pulses
		 */
		if (ps->count >= ps->ppb_thresh &&
		    ps->count * pde->rs->num_pri > ps->count_falses) {
			return ps;
		}
	}
	return NULL;
}

/**
 * pde_short_init - init function for
 *                  SHORT/WEATHER/INTERLEAVED radar waveform
 * @pde: pointer on pri_detector
 *
 * Initialize pri_detector window size to the maximun size of one burst
 * for the radar specification associated.
 */
static void pde_short_init(struct pri_detector *pde)
{
	pde->window_size = pde->rs->pri_max * pde->rs->ppb * pde->rs->num_pri;
	pde->max_count = pde->rs->ppb * 2;
}

static void pri_detector_reset(struct pri_detector *pde, u64 ts);
/**
 *  pde_short_add_pulse - Add pulse to a pri_detector for
 *                        SHORT/WEATHER/INTERLEAVED radar waveform
 *
 * @pde : pointer on pri_detector
 * @len : width of the pulse
 * @ts  : timestamp of the pulse received
 * @pri : Delta in us with the previous pulse.
 *        (0 means that delta in bigger than 65535 us)
 *
 * Process on pulse within this pri_detector
 * - First try to add it to existing sequence
 * - Then try to create a new and longest sequence
 * - Check if this pulse complete a sequence
 * - If not save this pulse in the list
 *
 * Return: The completed sequence, or %NULL if the pulse did not complete one.
 */
static struct pri_sequence *pde_short_add_pulse(struct pri_detector *pde,
						u16 len, u64 ts, u16 pri)
{
	u32 max_updated_seq;
	struct pri_sequence *ps;
	const struct radar_detector_specs *rs = pde->rs;

	if (pde->count == 0) {
		/* This is the first pulse after reset, no need to check sequences */
		pulse_queue_enqueue(pde, ts);
		return NULL;
	}

	if ((ts - pde->last_ts) < rs->max_pri_tolerance) {
		/* if delta to last pulse is too short, don't use this pulse */
		return NULL;
	}

	max_updated_seq = pde_short_add_to_existing_seqs(pde, ts);

	if (!pde_short_create_sequences(pde, ts, max_updated_seq)) {
		pri_detector_reset(pde, ts);
		return NULL;
	}

	ps = pde_short_check_detection(pde);

	if (!ps)
		pulse_queue_enqueue(pde, ts);

	return ps;
}

/*
 * pri detector ops to detect short radar waveform
 * A Short waveform is defined by :
 *   The width of pulses.
 *   The interval between two pulses inside a burst (called pri)
 *   (some waveform may have or 2/3 interleaved pri)
 *   The number of pulses per burst (ppb)
 */
static struct pri_detector_ops pri_detector_short = {
	.init = pde_short_init,
	.add_pulse = pde_short_add_pulse,
	.reset_on_pri_overflow = 1,
};

/***************************************************************************
 * Long waveform
 **************************************************************************/
#define LONG_RADAR_DURATION           12000000
#define LONG_RADAR_BURST_MIN_DURATION (12000000 / 20)
#define LONG_RADAR_MAX_BURST          20

/**
 * pde_long_init - init function for LONG radar waveform
 * @pde: pointer on pri_detector
 *
 * Initialize pri_detector window size to the long waveform radar
 * waveform (ie. 12s) and max_count
 */
static void pde_long_init(struct pri_detector *pde)
{
	pde->window_size = LONG_RADAR_DURATION;
	pde->max_count = LONG_RADAR_MAX_BURST; /* only count burst not pulses */
}

/**
 *  pde_long_add_pulse - Add pulse to a pri_detector for
 *                       LONG radar waveform
 *
 * @pde : pointer on pri_detector
 * @len : width of the pulse
 * @ts  : timestamp of the pulse received
 * @pri : Delta in us with the previous pulse.
 *
 *
 * For long pulse we only handle one sequence. Since each burst
 * have a different set of parameters (number of pulse, pri) than
 * the previous one we only use pulse width to add the pulse in the
 * sequence.
 * We only queue one pulse per burst and valid the radar when enough burst
 * has been detected.
 *
 * Return: The completed sequence, or %NULL if radar was not detected.
 */
static struct pri_sequence *pde_long_add_pulse(struct pri_detector *pde,
					       u16 len, u64 ts, u16 pri)
{
	struct pri_sequence *ps;
	const struct radar_detector_specs *rs = pde->rs;

	if (list_empty(&pde->sequences)) {
		/* First pulse, create a new sequence */
		ps = pool_get_pseq_elem();
		if (!ps)
			return NULL;

		/*For long waveform, "count" represents the number of burst detected */
		ps->count = 1;
		/*"count_false" represents the number of pulse in the current burst */
		ps->count_falses = 1;
		ps->first_ts = ts;
		ps->last_ts = ts;
		ps->deadline_ts = ts + pde->window_size;
		ps->pri = 0;
		INIT_LIST_HEAD(&ps->head);
		list_add(&ps->head, &pde->sequences);
		pulse_queue_enqueue(pde, ts);
	} else {
		u32 delta_ts;

		ps = (struct pri_sequence *)pde->sequences.next;

		delta_ts = ts - ps->last_ts;
		ps->last_ts = ts;

		if (delta_ts < rs->pri_max) {
			/* ignore pulse too close from previous one */
		} else if ((delta_ts >= rs->pri_min) && (delta_ts <= rs->pri_max)) {
			/*
			 * this is a new pulse in the current burst, ignore it
			 * (i.e don't queue it)
			 */
			ps->count_falses++;
		} else if ((ps->count > 2) &&
				   (ps->dur + delta_ts) < LONG_RADAR_BURST_MIN_DURATION) {
			/* not enough time between burst, ignore pulse */
		} else {
			/* a new burst */
			ps->count++;
			ps->count_falses = 1;

			/* reset the start of the sequence if deadline reached */
			if (ts > ps->deadline_ts) {
				struct pulse_elem *p;
				u64 min_valid_ts;

				min_valid_ts = ts - pde->window_size;
				while ((p = pulse_queue_get_tail(pde)) != NULL) {
					if (p->ts >= min_valid_ts) {
						ps->first_ts = p->ts;
						ps->deadline_ts = p->ts + pde->window_size;
						break;
					}
					pulse_queue_dequeue(pde);
					ps->count--;
				}
			}

			/*
			 * valid radar if enough burst detected and delta with first burst
			 * is at least duration/2
			 */
			if (ps->count > pde->rs->ppb_thresh &&
			    (ts - ps->first_ts) > (pde->window_size / 2)) {
				return ps;
			}
			pulse_queue_enqueue(pde, ts);
			ps->dur = delta_ts;
		}
	}
	return NULL;
}

/*
 * pri detector ops to detect long radar waveform
 */
static struct pri_detector_ops pri_detector_long = {
	.init = pde_long_init,
	.add_pulse = pde_long_add_pulse,
	.reset_on_pri_overflow = 0,
};

/***************************************************************************
 * PRI detector init/reset/exit/get
 **************************************************************************/
/**
 * pri_detector_init - Create a new PRI detector
 *
 * @dpd: dfs_pattern_detector instance pointer
 * @radar_type: index of radar pattern
 * @freq: Frequency of the pri detector
 *
 * Return: The allocated detector, or %NULL if allocation failed.
 */
struct pri_detector *pri_detector_init(struct dfs_pattern_detector *dpd,
				       u16 radar_type, u16 freq)
{
	struct pri_detector *pde;

	pde = kzalloc_obj(*pde, GFP_ATOMIC);
	if (!pde)
		return NULL;

	INIT_LIST_HEAD(&pde->sequences);
	INIT_LIST_HEAD(&pde->pulses);
	INIT_LIST_HEAD(&pde->head);
	list_add(&pde->head, &dpd->detectors[radar_type]);

	pde->rs = &dpd->radar_spec[radar_type];
	pde->freq = freq;

	if (pde->rs->type == RADAR_WAVEFORM_LONG) {
		/* for LONG WAVEFORM */
		pde->ops = &pri_detector_long;
	} else {
		/* for SHORT, WEATHER and INTERLEAVED */
		pde->ops = &pri_detector_short;
	}

	/* Init dependent of specs */
	pde->ops->init(pde);

	pool_register_ref();
	return pde;
}

/**
 * pri_detector_reset - Reset pri_detector
 *
 * @pde: pointer on pri_detector
 * @ts: New ts reference for the pri_detector
 *
 * free pulse queue and sequences list and give objects back to pools
 */
static void pri_detector_reset(struct pri_detector *pde, u64 ts)
{
	struct pri_sequence *ps, *ps0;
	struct pulse_elem *p, *p0;

	list_for_each_entry_safe(ps, ps0, &pde->sequences, head) {
		list_del_init(&ps->head);
		pool_put_pseq_elem(ps);
	}
	list_for_each_entry_safe(p, p0, &pde->pulses, head) {
		list_del_init(&p->head);
		pool_put_pulse_elem(p);
	}
	pde->count = 0;
	pde->last_ts = ts;
}

/**
 *  pri_detector_exit - Delete pri_detector
 *
 *  @pde: pointer on pri_detector
 */
static void pri_detector_exit(struct pri_detector *pde)
{
	pri_detector_reset(pde, 0);
	pool_deregister_ref();
	list_del(&pde->head);
	kfree(pde);
}

/**
 * pri_detector_get - Get a PRI detector for a given frequency and type
 * @dpd: dfs_pattern_detector instance pointer
 * @freq: frequency in MHz
 * @radar_type: index of radar pattern
 *
 * Return existing pri detector for the given frequency or return a
 * newly create one.
 * Pri detector are "merged" by frequency so that if a pri detector for a freq
 * of +/- 2Mhz already exists don't create a new one.
 *
 * Maybe will need to adapt frequency merge for pattern with chirp.
 *
 * Return: An existing or newly allocated detector, or %NULL on allocation
 * failure.
 */
static struct pri_detector *pri_detector_get(struct dfs_pattern_detector *dpd,
					     u16 freq, u16 radar_type)
{
	struct pri_detector *pde, *cur = NULL;

	list_for_each_entry(pde, &dpd->detectors[radar_type], head) {
		if (pde->freq == freq) {
			if (pde->count)
				return pde;
			cur = pde;
		} else if (pde->freq - 2 == freq && pde->count) {
			return pde;
		} else if (pde->freq + 2 == freq && pde->count) {
			return pde;
		}
	}

	if (cur)
		return cur;
	else
		return pri_detector_init(dpd, radar_type, freq);
}

/******************************************************************************
 * DFS Pattern Detector
 *****************************************************************************/
/**
 * dfs_pattern_detector_reset() - reset all channel detectors
 *
 * @dpd: dfs_pattern_detector
 */
static void dfs_pattern_detector_reset(struct dfs_pattern_detector *dpd)
{
	struct pri_detector *pde;
	int i;

	for (i = 0; i < dpd->num_radar_types; i++) {
		if (!list_empty(&dpd->detectors[i]))
			list_for_each_entry(pde, &dpd->detectors[i], head)
				pri_detector_reset(pde, dpd->last_pulse_ts);
	}

	dpd->last_pulse_ts = 0;
	dpd->prev_jiffies = jiffies;
}

/*
 * dfs_pattern_detector_reset() - delete all channel detectors
 *
 * @dpd: dfs_pattern_detector
 */
static void dfs_pattern_detector_exit(struct dfs_pattern_detector *dpd)
{
	struct pri_detector *pde, *pde0;
	int i;

	for (i = 0; i < dpd->num_radar_types; i++) {
		if (!list_empty(&dpd->detectors[i]))
			list_for_each_entry_safe(pde, pde0, &dpd->detectors[i], head)
				pri_detector_exit(pde);
	}

	kfree(dpd);
}

/**
 * dfs_pattern_detector_pri_overflow - reset all channel detectors on pri
 *                                     overflow
 * @dpd: dfs_pattern_detector
 */
static void dfs_pattern_detector_pri_overflow(struct dfs_pattern_detector *dpd)
{
	struct pri_detector *pde;
	int i;

	for (i = 0; i < dpd->num_radar_types; i++) {
		if (!list_empty(&dpd->detectors[i]))
			list_for_each_entry(pde, &dpd->detectors[i], head)
				if (pde->ops->reset_on_pri_overflow)
					pri_detector_reset(pde, dpd->last_pulse_ts);
	}
}

/**
 * dfs_pattern_detector_add_pulse - Process one pulse
 *
 * @dpd: dfs_pattern_detector
 * @chain: Chain that correspond to this pattern_detector (only for debug)
 * @freq: frequency of the pulse
 * @pri: Delta with previous pulse. (0 if delta is too big for u16)
 * @len: width of the pulse
 * @now: jiffies value when pulse was received
 *
 * Get (or create) the channel_detector for this frequency. Then add the pulse
 * in each pri_detector created in this channel_detector.
 *
 *
 * Return: %true if the pulse completes a radar pattern, otherwise %false.
 */
static bool dfs_pattern_detector_add_pulse(struct dfs_pattern_detector *dpd,
					   enum rwnx_radar_chain chain,
					   u16 freq, u16 pri, u16 len, u32 now)
{
	u32 i;

	/*
	 * pulses received for a non-supported or un-initialized
	 * domain are treated as detected radars for fail-safety
	 */
	if (dpd->region == NL80211_DFS_UNSET)
		return true;

	/* Compute pulse time stamp */
	if (pri == 0) {
		u32 delta_jiffie;

		if (unlikely(now < dpd->prev_jiffies))
			delta_jiffie = 0xffffffff - dpd->prev_jiffies + now;
		else
			delta_jiffie = now - dpd->prev_jiffies;
		dpd->last_pulse_ts += jiffies_to_usecs(delta_jiffie);
		dpd->prev_jiffies = now;
		dfs_pattern_detector_pri_overflow(dpd);
	} else {
		dpd->last_pulse_ts += pri;
	}

	for (i = 0; i < dpd->num_radar_types; i++) {
		struct pri_sequence *ps;
		struct pri_detector *pde;
		const struct radar_detector_specs *rs = &dpd->radar_spec[i];

		/* no need to look up for pde if len is not within range */
		if (rs->width_min > len || rs->width_max < len)
			continue;

		pde = pri_detector_get(dpd, freq, i);
		ps = pde->ops->add_pulse(pde, len, dpd->last_pulse_ts, pri);

		if (ps) {
#ifdef AIC8800_CREATE_TRACE_POINTS
			trace_radar_detected(chain, dpd->region, pde->freq, i, ps->pri);
#endif
			// reset everything instead of just the channel detector
			dfs_pattern_detector_reset(dpd);
			return true;
		}
	}

	return false;
}

/*
 * get_dfs_domain_radar_types() - get radar types for a given DFS domain
 * @param domain DFS domain
 * @return radar_types ptr on success, NULL if DFS domain is not supported
 */
static const struct radar_types *
get_dfs_domain_radar_types(enum nl80211_dfs_regions region)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dfs_domains); i++) {
		if (dfs_domains[i]->region == region)
			return dfs_domains[i];
	}
	return NULL;
}

/**
 * get_dfs_max_radar_types - Get the maximum number of radar types
 *
 * Return: Maximum number of radar patterns supported by any region.
 */
static u16 get_dfs_max_radar_types(void)
{
	u32 i;
	u16 max = 0;

	for (i = 0; i < ARRAY_SIZE(dfs_domains); i++) {
		if (dfs_domains[i]->num_radar_types > max)
			max = dfs_domains[i]->num_radar_types;
	}
	return max;
}

/*
 * dfs_pattern_detector_set_domain - set DFS domain
 *
 * @dpd: dfs_pattern_detector
 * @region: DFS region
 *
 * set DFS domain, resets detector lines upon domain changes
 */
static bool dfs_pattern_detector_set_domain(struct dfs_pattern_detector *dpd,
					    enum nl80211_dfs_regions region,
					    u8 chain)
{
	const struct radar_types *rt;
	struct pri_detector *pde, *pde0;
	int i;

	if (dpd->region == region)
		return true;

	dpd->region = NL80211_DFS_UNSET;

	rt = get_dfs_domain_radar_types(region);
	if (!rt)
		return false;

	/* delete all pri detectors for previous DFS domain */
	for (i = 0; i < dpd->num_radar_types; i++) {
		if (!list_empty(&dpd->detectors[i]))
			list_for_each_entry_safe(pde, pde0, &dpd->detectors[i], head)
				pri_detector_exit(pde);
	}

	if (chain == RWNX_RADAR_RIU)
		dpd->radar_spec = rt->spec_riu;
	else
		dpd->radar_spec = rt->spec_fcu;
	dpd->num_radar_types = rt->num_radar_types;

	dpd->region = region;
	return true;
}

/*
 * dfs_pattern_detector_init - Initialize dfs_pattern_detector
 *
 * @region: DFS region
 * @return: pointer on dfs_pattern_detector
 *
 */
static struct dfs_pattern_detector *
dfs_pattern_detector_init(enum nl80211_dfs_regions region, u8 chain)
{
	struct dfs_pattern_detector *dpd;
	u16 i, max_radar_type = get_dfs_max_radar_types();

	dpd = kmalloc(sizeof(*dpd) + max_radar_type * sizeof(dpd->detectors[0]),
		      GFP_KERNEL);
	if (!dpd)
		return NULL;

	dpd->region = NL80211_DFS_UNSET;
	dpd->enabled = RWNX_RADAR_DETECT_DISABLE;
	dpd->last_pulse_ts = 0;
	dpd->prev_jiffies = jiffies;
	dpd->num_radar_types = 0;
	for (i = 0; i < max_radar_type; i++)
		INIT_LIST_HEAD(&dpd->detectors[i]);

	if (dfs_pattern_detector_set_domain(dpd, region, chain))
		return dpd;

	kfree(dpd);
	return NULL;
}

/******************************************************************************
 * driver interface
 *****************************************************************************/
static u16 rwnx_radar_get_center_freq(struct rwnx_hw *rwnx_hw, u8 chain)
{
	if (chain == RWNX_RADAR_FCU)
		return rwnx_hw->phy.sec_chan.center_freq1;

	if (chain == RWNX_RADAR_RIU) {
#ifdef CONFIG_RWNX_FULLMAC
		if (!rwnx_chanctx_valid(rwnx_hw, rwnx_hw->cur_chanctx)) {
			WARN(1, "Radar pulse without channel information");
		} else {
			return rwnx_hw->chanctx_table[rwnx_hw->cur_chanctx]
				.chan_def.center_freq1;
		}
#endif /* CONFIG_RWNX_FULLMAC */
	}

	return 0;
}

static void rwnx_radar_detected(struct rwnx_hw *rwnx_hw)
{
#ifdef CONFIG_RWNX_FULLMAC
	struct cfg80211_chan_def chan_def;

	if (!rwnx_chanctx_valid(rwnx_hw, rwnx_hw->cur_chanctx)) {
		WARN(1, "Radar detected without channel information");
		return;
	}

	/*
	 * recopy chan_def in local variable because rwnx_radar_cancel_cac may
	 * clean the variable (if in CAC and it's the only vif using this context)
	 * and CAC should be aborted before reporting the radar.
	 */
	chan_def = rwnx_hw->chanctx_table[rwnx_hw->cur_chanctx].chan_def;

	rwnx_radar_cancel_cac(&rwnx_hw->radar);
	cfg80211_radar_event(rwnx_hw->wiphy, &chan_def, GFP_KERNEL);

#endif /* CONFIG_RWNX_FULLMAC */
}

static void rwnx_radar_process_pulse(struct work_struct *ws)
{
	struct rwnx_radar *radar =
		container_of(ws, struct rwnx_radar, detection_work);
	struct rwnx_hw *rwnx_hw = container_of(radar, struct rwnx_hw, radar);
	int chain;
	u32 pulses[RWNX_RADAR_LAST][RWNX_RADAR_PULSE_MAX];
	u16 pulses_count[RWNX_RADAR_LAST];
	u32 now =
		jiffies; /* would be better to store jiffies value in IT handler */

	/* recopy pulses locally to avoid too long spin_lock */
	spin_lock_bh(&radar->lock);
	for (chain = RWNX_RADAR_RIU; chain < RWNX_RADAR_LAST; chain++) {
		int start, count;

		count = radar->pulses[chain].count;
		start = radar->pulses[chain].index - count;
		if (start < 0)
			start += RWNX_RADAR_PULSE_MAX;

		pulses_count[chain] = count;
		if (count == 0)
			continue;

		if ((start + count) > RWNX_RADAR_PULSE_MAX) {
			u16 count1 = (RWNX_RADAR_PULSE_MAX - start);

			memcpy(&pulses[chain][0], &radar->pulses[chain].buffer[start],
			       count1 * sizeof(struct radar_pulse));
			memcpy(&pulses[chain][count1], &radar->pulses[chain].buffer[0],
			       (count - count1) * sizeof(struct radar_pulse));
		} else {
			memcpy(&pulses[chain][0], &radar->pulses[chain].buffer[start],
			       count * sizeof(struct radar_pulse));
		}
		radar->pulses[chain].count = 0;
	}
	spin_unlock_bh(&radar->lock);

	/* now process pulses */
	for (chain = RWNX_RADAR_RIU; chain < RWNX_RADAR_LAST; chain++) {
		int i;
		u16 freq;

		if (pulses_count[chain] == 0)
			continue;

		freq = rwnx_radar_get_center_freq(rwnx_hw, chain);

		for (i = 0; i < pulses_count[chain]; i++) {
			struct radar_pulse *p = (struct radar_pulse *)&pulses[chain][i];
#ifdef AIC8800_CREATE_TRACE_POINTS
			trace_radar_pulse(chain, p);
#endif
			if (dfs_pattern_detector_add_pulse(radar->dpd[chain], chain,
							   (s16)freq + (2 * p->freq),
							   p->rep, (p->len * 2), now)) {
				u16 idx = radar->detected[chain].index;

				if (chain == RWNX_RADAR_RIU) {
					/* operating chain, inform upper layer to change channel */
					if (radar->dpd[chain]->enabled ==
						RWNX_RADAR_DETECT_REPORT) {
						rwnx_radar_detected(rwnx_hw);
						/*
						 * no need to report new radar until upper layer
						 * set a new channel. This prevent warning if a
						 * new radar is	detected while mac80211 is
						 * changing channel
						 */
						rwnx_radar_detection_enable
							(radar, RWNX_RADAR_DETECT_DISABLE, chain);
						/*
						 * purge any event received since the beginning
						 * of the function (we are sure not to interfer
						 * with tasklet as we disable detection just
						 * before)
						 */
						radar->pulses[chain].count = 0;
					}
				}

				radar->detected[chain].freq[idx] = (s16)freq + (2 * p->freq);
				radar->detected[chain].time[idx] = ktime_get_real_seconds();
				radar->detected[chain].index =
					((idx + 1) % NX_NB_RADAR_DETECTED);
				radar->detected[chain].count++;
				/* no need to process next pulses for this chain */
				break;
			}
		}
	}
}

#ifdef CONFIG_RWNX_FULLMAC
static void rwnx_radar_cac_work(struct work_struct *ws)
{
	struct delayed_work *dw = container_of(ws, struct delayed_work, work);
	struct rwnx_radar *radar = container_of(dw, struct rwnx_radar, cac_work);
	struct rwnx_hw *rwnx_hw = container_of(radar, struct rwnx_hw, radar);
	struct rwnx_chanctx *ctxt;

	if (!radar->cac_vif) {
		WARN(1, "CAC finished but no vif set");
		return;
	}

	ctxt = &rwnx_hw->chanctx_table[radar->cac_vif->ch_index];
	cfg80211_cac_event(radar->cac_vif->ndev, &ctxt->chan_def,
			   NL80211_RADAR_CAC_FINISHED, GFP_KERNEL, 0);
	rwnx_send_apm_stop_cac_req(rwnx_hw, radar->cac_vif);
	rwnx_chanctx_unlink(radar->cac_vif);

	radar->cac_vif = NULL;
}
#endif /* CONFIG_RWNX_FULLMAC */

bool rwnx_radar_detection_init(struct rwnx_radar *radar)
{
	spin_lock_init(&radar->lock);

	radar->dpd[RWNX_RADAR_RIU] =
		dfs_pattern_detector_init(NL80211_DFS_UNSET, RWNX_RADAR_RIU);
	if (!radar->dpd[RWNX_RADAR_RIU])
		return false;

	radar->dpd[RWNX_RADAR_FCU] =
		dfs_pattern_detector_init(NL80211_DFS_UNSET, RWNX_RADAR_FCU);
	if (!radar->dpd[RWNX_RADAR_FCU]) {
		rwnx_radar_detection_deinit(radar);
		return false;
	}

	INIT_WORK(&radar->detection_work, rwnx_radar_process_pulse);
#ifdef CONFIG_RWNX_FULLMAC
	INIT_DELAYED_WORK(&radar->cac_work, rwnx_radar_cac_work);
	radar->cac_vif = NULL;
#endif /* CONFIG_RWNX_FULLMAC */
	return true;
}

void rwnx_radar_detection_deinit(struct rwnx_radar *radar)
{
	if (radar->dpd[RWNX_RADAR_RIU]) {
		dfs_pattern_detector_exit(radar->dpd[RWNX_RADAR_RIU]);
		radar->dpd[RWNX_RADAR_RIU] = NULL;
	}
	if (radar->dpd[RWNX_RADAR_FCU]) {
		dfs_pattern_detector_exit(radar->dpd[RWNX_RADAR_FCU]);
		radar->dpd[RWNX_RADAR_FCU] = NULL;
	}
}

bool rwnx_radar_set_domain(struct rwnx_radar *radar,
			   enum nl80211_dfs_regions region)
{
	if (!radar->dpd[0])
		return false;
#ifdef AIC8800_CREATE_TRACE_POINTS
	trace_radar_set_region(region);
#endif
	return (dfs_pattern_detector_set_domain(radar->dpd[RWNX_RADAR_RIU], region,
						RWNX_RADAR_RIU) &&
		dfs_pattern_detector_set_domain(radar->dpd[RWNX_RADAR_FCU], region,
						RWNX_RADAR_FCU));
}

void rwnx_radar_detection_enable(struct rwnx_radar *radar, u8 enable, u8 chain)
{
	if (chain < RWNX_RADAR_LAST) {
#ifdef AIC8800_CREATE_TRACE_POINTS
		trace_radar_enable_detection(radar->dpd[chain]->region, enable, chain);
#endif
		spin_lock_bh(&radar->lock);
		radar->dpd[chain]->enabled = enable;
		spin_unlock_bh(&radar->lock);
	}
}

bool rwnx_radar_detection_is_enable(struct rwnx_radar *radar, u8 chain)
{
	return radar->dpd[chain]->enabled != RWNX_RADAR_DETECT_DISABLE;
}

#ifdef CONFIG_RWNX_FULLMAC
void rwnx_radar_start_cac(struct rwnx_radar *radar, u32 cac_time_ms,
			  struct rwnx_vif *vif)
{
	WARN(radar->cac_vif, "CAC already in progress");
	radar->cac_vif = vif;
	schedule_delayed_work(&radar->cac_work, msecs_to_jiffies(cac_time_ms));
}

void rwnx_radar_cancel_cac(struct rwnx_radar *radar)
{
	struct rwnx_hw *rwnx_hw = container_of(radar, struct rwnx_hw, radar);

	if (!radar->cac_vif)
		return;

	if (cancel_delayed_work(&radar->cac_work)) {
		struct rwnx_chanctx *ctxt;

		ctxt = &rwnx_hw->chanctx_table[radar->cac_vif->ch_index];
		rwnx_send_apm_stop_cac_req(rwnx_hw, radar->cac_vif);
		cfg80211_cac_event(radar->cac_vif->ndev, &ctxt->chan_def,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL, 0);
		rwnx_chanctx_unlink(radar->cac_vif);
	}

	radar->cac_vif = NULL;
}

void rwnx_radar_detection_enable_on_cur_channel(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_chanctx *ctxt;

	/* If no information on current channel do nothing */
	if (!rwnx_chanctx_valid(rwnx_hw, rwnx_hw->cur_chanctx))
		return;

	ctxt = &rwnx_hw->chanctx_table[rwnx_hw->cur_chanctx];
	if (ctxt->chan_def.chan->flags & IEEE80211_CHAN_RADAR) {
		rwnx_radar_detection_enable(&rwnx_hw->radar, RWNX_RADAR_DETECT_REPORT,
					    RWNX_RADAR_RIU);
	} else {
		rwnx_radar_detection_enable(&rwnx_hw->radar, RWNX_RADAR_DETECT_DISABLE,
					    RWNX_RADAR_RIU);
	}
}
#endif /* CONFIG_RWNX_FULLMAC */

/*****************************************************************************
 * Debug functions
 *****************************************************************************/
static int rwnx_radar_dump_pri_detector(char *buf, size_t len,
					struct pri_detector *pde)
{
	char freq_info[] = "Freq = %3.dMhz\n";
	char seq_info[] = " pri    | count | false\n";
	struct pri_sequence *seq;
	int res, write = 0;

	if (list_empty(&pde->sequences))
		return 0;

	if (!buf) {
		int nb_seq = 1;

		list_for_each_entry(seq, &pde->sequences, head) {
			nb_seq++;
		}

		return (sizeof(freq_info) + nb_seq * sizeof(seq_info));
	}

	res = scnprintf(buf, len, freq_info, pde->freq);
	write += res;
	len -= res;

	res = scnprintf(&buf[write], len, "%s", seq_info);
	write += res;
	len -= res;

	list_for_each_entry(seq, &pde->sequences, head) {
		res = scnprintf(&buf[write], len, " %6.d |   %2.d  |    %.2d\n",
				seq->pri, seq->count, seq->count_falses);
		write += res;
		len -= res;
	}

	return write;
}

int rwnx_radar_dump_pattern_detector(char *buf, size_t len,
				     struct rwnx_radar *radar, u8 chain)
{
	struct dfs_pattern_detector *dpd = radar->dpd[chain];
	char info[] = "Type = %3.d\n";
	struct pri_detector *pde;
	int i, res, write = 0;

	/* if buf is NULL return size needed for dump */
	if (!buf) {
		int size_needed = 0;

		for (i = 0; i < dpd->num_radar_types; i++) {
			list_for_each_entry(pde, &dpd->detectors[i], head) {
				size_needed += rwnx_radar_dump_pri_detector(NULL, 0, pde);
			}
			size_needed += sizeof(info);

			return size_needed;
		}
	}

	/* */
	for (i = 0; i < dpd->num_radar_types; i++) {
		res = scnprintf(&buf[write], len, info, i);

		write += res;
		len -= res;

		list_for_each_entry(pde, &dpd->detectors[i], head) {
			res = rwnx_radar_dump_pri_detector(&buf[write], len, pde);
			write += res;
			len -= res;
		}
	}

	return write;
}

int rwnx_radar_dump_radar_detected(char *buf, size_t len,
				   struct rwnx_radar *radar, u8 chain)
{
	struct rwnx_radar_detected *detect = &radar->detected[chain];
	char info[] = "2001/02/02 - 02:20 5126MHz\n";
	int idx, i, res, write = 0;
	int count = detect->count;

	if (count > NX_NB_RADAR_DETECTED)
		count = NX_NB_RADAR_DETECTED;

	if (!buf)
		return (count * sizeof(info)) + 1;

	idx = (detect->index - detect->count) % NX_NB_RADAR_DETECTED;

	for (i = 0; i < count; i++) {
		struct tm tm;

		time64_to_tm(detect->time[idx], 0, &tm);
		res =
			scnprintf(&buf[write], len, "%.4d/%.2d/%.2d - %.2d:%.2d %4.4dMHz\n",
				  (int)tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				  tm.tm_hour, tm.tm_min, detect->freq[idx]);
		write += res;
		len -= res;

		idx = (idx + 1) % NX_NB_RADAR_DETECTED;
	}

	return write;
}
