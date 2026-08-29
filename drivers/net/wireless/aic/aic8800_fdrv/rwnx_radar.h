/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_RADAR_H_
#define _RWNX_RADAR_H_

#include <linux/nl80211.h>

struct rwnx_vif;
struct rwnx_hw;

enum rwnx_radar_chain { RWNX_RADAR_RIU = 0, RWNX_RADAR_FCU, RWNX_RADAR_LAST };

enum rwnx_radar_detector {
	/* Ignore radar pulses */
	RWNX_RADAR_DETECT_DISABLE = 0,
	/*
	 * Process pattern detection but do not
	 * report radar to upper layer (for test)
	 */
	RWNX_RADAR_DETECT_ENABLE = 1,
	/*
	 * Process pattern detection and report
	 * radar to upper layer.
	 */
	RWNX_RADAR_DETECT_REPORT = 2
};

#ifdef CONFIG_RWNX_RADAR
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define RWNX_RADAR_PULSE_MAX 32

/**
 * struct rwnx_radar_pulses - List of pulses reported by HW
 * @index: write index
 * @count: number of valid pulses
 * @buffer: buffer of pulses
 */
struct rwnx_radar_pulses {
	/* Last radar pulses received */
	int index;
	int count;
	u32 buffer[RWNX_RADAR_PULSE_MAX];
};

/**
 * struct dfs_pattern_detector - DFS pattern detector
 * @enabled: whether radar detection is enabled
 * @region: active DFS region, NL80211_DFS_UNSET until set
 * @num_radar_types: number of different radar types
 * @last_pulse_ts: time stamp of last valid pulse in usecs
 * @prev_jiffies: jiffies value recorded for the previous pulse
 * @radar_spec: array of radar detection specifications
 * @detectors: per-channel detector pointers
 */
struct dfs_pattern_detector {
	u8 enabled;
	enum nl80211_dfs_regions region;
	u8 num_radar_types;
	u64 last_pulse_ts;
	u32 prev_jiffies;
	const struct radar_detector_specs *radar_spec;
	struct list_head detectors[];
};

#define NX_NB_RADAR_DETECTED 4

/**
 * struct rwnx_radar_detected - List of radar detected
 * @index: next entry to replace
 * @count: number of valid entries
 * @time: detection timestamps
 * @freq: detection frequencies
 */
struct rwnx_radar_detected {
	u16 index;
	u16 count;
	s64 time[NX_NB_RADAR_DETECTED];
	s16 freq[NX_NB_RADAR_DETECTED];
};

struct rwnx_radar {
	struct rwnx_radar_pulses pulses[RWNX_RADAR_LAST];
	struct dfs_pattern_detector *dpd[RWNX_RADAR_LAST];
	struct rwnx_radar_detected detected[RWNX_RADAR_LAST];
	struct work_struct detection_work; /* Work used to process radar pulses */
	spinlock_t lock;                   /* lock for pulses processing */

	/* In softmac cac is handled by mac80211 */
#ifdef CONFIG_RWNX_FULLMAC
	struct delayed_work cac_work; /* Work used to handle CAC */
	struct rwnx_vif *cac_vif;     /* vif on which we started CAC */
#endif
};

/*
 * Type of radar waveform:
 * RADAR_WAVEFORM_SHORT : waveform defined by
 *  - pulse width
 *  - pulse interval in a burst (pri)
 *  - number of pulses in a burst (ppb)
 *
 * RADAR_WAVEFORM_WEATHER :
 *   same than SHORT except that ppb is dependent of pri
 *
 * RADAR_WAVEFORM_INTERLEAVED :
 *   same than SHORT except there are several value of pri (interleaved)
 *
 * RADAR_WAVEFORM_LONG :
 *
 */
enum radar_waveform_type {
	RADAR_WAVEFORM_SHORT,
	RADAR_WAVEFORM_WEATHER,
	RADAR_WAVEFORM_INTERLEAVED,
	RADAR_WAVEFORM_LONG
};

/**
 * struct radar_detector_specs - detector specs for a radar pattern type
 * @type_id: pattern type, as defined by regulatory
 * @width_min: minimum radar pulse width in [us]
 * @width_max: maximum radar pulse width in [us]
 * @pri_min: minimum pulse repetition interval in [us] (including tolerance)
 * @pri_max: minimum pri in [us] (including tolerance)
 * @num_pri: maximum number of different pri for this type
 * @ppb: pulses per bursts for this type
 * @ppb_thresh: number of pulses required to trigger detection
 * @max_pri_tolerance: pulse time stamp tolerance on both sides [us]
 * @type: Type of radar waveform
 */
struct radar_detector_specs {
	u8 type_id;
	u8 width_min;
	u8 width_max;
	u16 pri_min;
	u16 pri_max;
	u8 num_pri;
	u8 ppb;
	u8 ppb_thresh;
	u8 max_pri_tolerance;
	enum radar_waveform_type type;
};

/**
 * struct pri_sequence - sequence of pulses matching one PRI
 * @head: list_head
 * @pri: pulse repetition interval (PRI) in usecs
 * @dur: duration of sequence in usecs
 * @count: number of pulses in this sequence
 * @count_falses: number of not matching pulses in this sequence
 * @first_ts: time stamp of first pulse in usecs
 * @last_ts: time stamp of last pulse in usecs
 * @deadline_ts: deadline when this sequence becomes invalid (first_ts + dur)
 * @ppb_thresh: Number of pulses to validate detection
 *              (need for weather radar whose value depends of pri)
 */
struct pri_sequence {
	struct list_head head;
	u32 pri;
	u32 dur;
	u32 count;
	u32 count_falses;
	u64 first_ts;
	u64 last_ts;
	u64 deadline_ts;
	u8 ppb_thresh;
};

/**
 * struct pulse_elem - elements in pulse queue
 * @head: list node
 * @ts: time stamp in usecs
 */
struct pulse_elem {
	struct list_head head;
	u64 ts;
};

/**
 * struct pri_detector - PRI detector element for a dedicated radar type
 * @head: list node
 * @rs: detector specs for this detector element
 * @last_ts: last pulse time stamp considered for this element in usecs
 * @sequences: list_head holding potential pulse sequences
 * @pulses: list connecting pulse_elem objects
 * @count: number of pulses in queue
 * @max_count: maximum number of pulses to be queued
 * @window_size: window size back from newest pulse time stamp in usecs
 * @ops: detector operations
 * @freq: channel frequency in MHz
 */
struct pri_detector {
	struct list_head head;
	const struct radar_detector_specs *rs;
	u64 last_ts;
	struct list_head sequences;
	struct list_head pulses;
	u32 count;
	u32 max_count;
	u32 window_size;
	struct pri_detector_ops *ops;
	u16 freq;
};

struct pri_detector *pri_detector_init(struct dfs_pattern_detector *dpd,
				       u16 radar_type, u16 freq);

bool rwnx_radar_detection_init(struct rwnx_radar *radar);
void rwnx_radar_detection_deinit(struct rwnx_radar *radar);
bool rwnx_radar_set_domain(struct rwnx_radar *radar,
			   enum nl80211_dfs_regions region);
void rwnx_radar_detection_enable(struct rwnx_radar *radar, u8 enable, u8 chain);
bool rwnx_radar_detection_is_enable(struct rwnx_radar *radar, u8 chain);
void rwnx_radar_start_cac(struct rwnx_radar *radar, u32 cac_time_ms,
			  struct rwnx_vif *vif);
void rwnx_radar_cancel_cac(struct rwnx_radar *radar);
void rwnx_radar_detection_enable_on_cur_channel(struct rwnx_hw *rwnx_hw);
int rwnx_radar_dump_pattern_detector(char *buf, size_t len,
				     struct rwnx_radar *radar, u8 chain);
int rwnx_radar_dump_radar_detected(char *buf, size_t len,
				   struct rwnx_radar *radar, u8 chain);

#else

struct rwnx_radar {
};

static inline bool rwnx_radar_detection_init(struct rwnx_radar *radar)
{
	return true;
}

static inline void rwnx_radar_detection_deinit(struct rwnx_radar *radar)
{
}

static inline bool rwnx_radar_set_domain(struct rwnx_radar *radar,
					 enum nl80211_dfs_regions region)
{
	return true;
}

static inline void rwnx_radar_detection_enable(struct rwnx_radar *radar,
					       u8 enable, u8 chain)
{
}

static inline bool rwnx_radar_detection_is_enable(struct rwnx_radar *radar,
						  u8 chain)
{
	return false;
}

static inline void rwnx_radar_start_cac(struct rwnx_radar *radar,
					u32 cac_time_ms, struct rwnx_vif *vif)
{
}

static inline void rwnx_radar_cancel_cac(struct rwnx_radar *radar)
{
}

static inline void
rwnx_radar_detection_enable_on_cur_channel(struct rwnx_hw *rwnx_hw)
{
}

static inline int rwnx_radar_dump_pattern_detector(char *buf, size_t len,
						   struct rwnx_radar *radar,
						   u8 chain)
{
	return 0;
}

static inline int rwnx_radar_dump_radar_detected(char *buf, size_t len,
						 struct rwnx_radar *radar,
						 u8 chain)
{
	return 0;
}

#endif /* CONFIG_RWNX_RADAR */

#endif // _RWNX_RADAR_H_
