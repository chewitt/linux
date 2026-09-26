/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * HEVC header synthesis from request controls.
 */

#ifndef __MESON_AMVDEC_CODEC_HEVC_SYNTH_H_
#define __MESON_AMVDEC_CODEC_HEVC_SYNTH_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/bits.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#else
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif
#include <linux/v4l2-controls.h>
#endif

#include "codec_hevc_rps.h"

#define HEVC_SYNTH_MAX_HEADERS 8192
#define HEVC_SYNTH_SPS_RBSP_SIZE 4096
/* 992 scaling coefficients of up to 17 bits each, and change */
#define HEVC_SYNTH_PPS_RBSP_SIZE 2560

struct hevc_bw {
	u8 *buf;
	u32 cap;
	u32 bitpos;
	int err;
};

static inline void hevc_bw_init(struct hevc_bw *bw, u8 *buf, u32 cap)
{
	bw->buf = buf;
	bw->cap = cap;
	bw->bitpos = 0;
	bw->err = 0;
	memset(buf, 0, cap);
}

static inline void hevc_bw_bit(struct hevc_bw *bw, u32 bit)
{
	u32 byte = bw->bitpos >> 3;

	if (bw->err)
		return;
	if (byte >= bw->cap) {
		bw->err = -ENOSPC;
		return;
	}
	if (bit)
		bw->buf[byte] |= 1 << (7 - (bw->bitpos & 7));
	bw->bitpos++;
}

static inline void hevc_bw_u(struct hevc_bw *bw, u64 val, u32 bits)
{
	while (bits--)
		hevc_bw_bit(bw, (val >> bits) & 1);
}

static inline void hevc_bw_ue(struct hevc_bw *bw, u32 val)
{
	u32 code = val + 1;
	u32 bits = 0;
	u32 tmp = code;

	while (tmp) {
		bits++;
		tmp >>= 1;
	}
	while (--bits)
		hevc_bw_bit(bw, 0);
	hevc_bw_u(bw, code, 32 - __builtin_clz(code));
}

static inline void hevc_bw_se(struct hevc_bw *bw, int val)
{
	u32 code = val <= 0 ? (u32)(-2 * val) : (u32)(2 * val - 1);

	hevc_bw_ue(bw, code);
}

static inline void hevc_bw_trailing(struct hevc_bw *bw)
{
	hevc_bw_bit(bw, 1);
	while (bw->bitpos & 7)
		hevc_bw_bit(bw, 0);
}

static inline u32 hevc_bw_bytes(const struct hevc_bw *bw)
{
	return (bw->bitpos + 7) >> 3;
}

static inline int hevc_epb_copy(u8 *dst, u32 dst_cap, const u8 *src, u32 src_len,
				u32 *out_len)
{
	u32 z = 0, o = 0, i;

	for (i = 0; i < src_len; i++) {
		if (z >= 2 && src[i] <= 3) {
			if (o >= dst_cap)
				return -ENOSPC;
			dst[o++] = 3;
			z = 0;
		}
		if (o >= dst_cap)
			return -ENOSPC;
		dst[o++] = src[i];
		if (src[i] == 0)
			z++;
		else
			z = 0;
	}
	*out_len = o;
	return 0;
}

static inline int hevc_emit_nal(u8 *dst, u32 dst_cap, u8 nal_type,
				const u8 *rbsp, u32 rbsp_len, u32 *out_len)
{
	u32 epb_len;
	int ret;

	if (dst_cap < 6)
		return -ENOSPC;
	dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 1;
	dst[4] = nal_type << 1;
	dst[5] = 1;
	ret = hevc_epb_copy(dst + 6, dst_cap - 6, rbsp, rbsp_len, &epb_len);
	if (ret)
		return ret;
	*out_len = 6 + epb_len;
	return 0;
}

static inline void hevc_ptl_write(struct hevc_bw *bw, const struct v4l2_ctrl_hevc_sps *sps)
{
	int i;

	hevc_bw_u(bw, 0, 2); /* general_profile_space */
	hevc_bw_u(bw, 0, 1); /* general_tier_flag */
	hevc_bw_u(bw, sps->bit_depth_luma_minus8 == 2 ? 2 : 1, 5); /* Main 10 / Main */
	for (i = 0; i < 32; i++)
		hevc_bw_u(bw, i == 2 || (!sps->bit_depth_luma_minus8 && i == 1), 1);
	hevc_bw_u(bw, 1, 1); /* progressive_source */
	hevc_bw_u(bw, 0, 1); /* interlaced_source */
	hevc_bw_u(bw, 0, 1); /* non_packed_constraint */
	hevc_bw_u(bw, 1, 1); /* frame_only_constraint */
	hevc_bw_u(bw, 0, 44);
	/* Signal level 4 for Main; use level 4.1 or 5.1 for Main 10 by coded size. */
	hevc_bw_u(bw, sps->bit_depth_luma_minus8 == 2 ?
		  (sps->pic_width_in_luma_samples > 1920 ||
		   sps->pic_height_in_luma_samples > 1080 ? 153 : 123) : 120, 8);
	for (i = 0; i < sps->sps_max_sub_layers_minus1; i++) {
		hevc_bw_u(bw, 0, 1);
		hevc_bw_u(bw, 0, 1);
	}
	if (sps->sps_max_sub_layers_minus1)
		for (i = 0; i < 8 - sps->sps_max_sub_layers_minus1; i++)
			hevc_bw_u(bw, 0, 2);
}

/* Refuse single-column pictures with multiple CTB rows: reconstruction is inexact. */
static inline bool hevc_sps_bad_geometry(const struct v4l2_ctrl_hevc_sps *sps)
{
	u32 ctb_log2 = sps->log2_min_luma_coding_block_size_minus3 + 3 +
		       sps->log2_diff_max_min_luma_coding_block_size;

	return ctb_log2 >= 4 && ctb_log2 <= 6 &&
	       sps->pic_width_in_luma_samples <= (1U << ctb_log2) &&
	       sps->pic_height_in_luma_samples > (1U << ctb_log2);
}

static inline int hevc_sps_validate(const struct v4l2_ctrl_hevc_sps *sps)
{
	const u64 unsupported_sps = V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE;

	if (hevc_sps_bad_geometry(sps))
		return -EOPNOTSUPP;

	if (sps->bit_depth_luma_minus8 != sps->bit_depth_chroma_minus8 ||
	    (sps->bit_depth_luma_minus8 != 0 && sps->bit_depth_luma_minus8 != 2))
		return -EOPNOTSUPP;
	if (sps->chroma_format_idc != 1)
		return -EOPNOTSUPP;
	if (sps->flags & unsupported_sps)
		return -EOPNOTSUPP;
	/* Reject SPS long-term candidate tables, which stall the firmware parser. */
	if (sps->num_long_term_ref_pics_sps)
		return -EOPNOTSUPP;
	/* H.265 7.4.3.2.1: PCM depth within the coded depth, blocks of 8 to 32 */
	if ((sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED) &&
	    (sps->pcm_sample_bit_depth_luma_minus1 > 7 + sps->bit_depth_luma_minus8 ||
	     sps->pcm_sample_bit_depth_chroma_minus1 > 7 + sps->bit_depth_chroma_minus8 ||
	     sps->log2_min_pcm_luma_coding_block_size_minus3 > 2 ||
	     sps->log2_min_pcm_luma_coding_block_size_minus3 +
	     sps->log2_diff_max_min_pcm_luma_coding_block_size > 2))
		return -EINVAL;
	if (sps->num_short_term_ref_pic_sets > HEVC_RPS_MAX_SETS ||
	    sps->sps_max_sub_layers_minus1 > 6)
		return -EINVAL;
	return 0;
}

/*
 * The firmware's PPS parser spins forever on a tile count it has no case
 * for: more than five explicitly sized tiles on an axis, or a uniform count
 * other than 1-5 and 8.
 */
static inline bool hevc_tile_count_ok(u32 count, bool uniform)
{
	return count <= 5 || (uniform && count == 8);
}

/*
 * One axis of the tile grid, in CTBs.  Explicit sizes must leave a positive
 * final tile.  The firmware's eight-way uniform split gives every tile but
 * the last floor(extent / 8), which is the H.265 (6-3)/(6-4) split only
 * when the remainder is 0 or 1.
 */
static inline bool hevc_tile_axis_ok(u32 extent, u32 count, bool uniform,
				     const u8 *size_minus1)
{
	u32 i, sum = 0;

	if (!count || count > extent)
		return false;
	if (uniform)
		return count != 8 || extent % 8 <= 1;
	for (i = 0; i + 1 < count; i++)
		sum += size_minus1[i] + 1;
	return sum < extent;
}

static inline int hevc_pps_validate(const struct v4l2_ctrl_hevc_pps *pps)
{
	if (pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED) {
		bool uniform = pps->flags & V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING;

		if (!hevc_tile_count_ok(pps->num_tile_columns_minus1 + 1, uniform) ||
		    !hevc_tile_count_ok(pps->num_tile_rows_minus1 + 1, uniform))
			return -EOPNOTSUPP;
	}
	/* H.265 7.4.3.3.1: both offsets are in -6..6 */
	if ((pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) &&
	    (pps->pps_beta_offset_div2 < -6 || pps->pps_beta_offset_div2 > 6 ||
	     pps->pps_tc_offset_div2 < -6 || pps->pps_tc_offset_div2 > 6))
		return -EINVAL;
	return 0;
}

static inline int hevc_synth_validate(const struct v4l2_ctrl_hevc_sps *sps,
				      const struct v4l2_ctrl_hevc_pps *pps)
{
	int ret;

	ret = hevc_sps_validate(sps);
	if (ret)
		return ret;
	ret = hevc_pps_validate(pps);
	if (ret)
		return ret;

	/* Refuse wavefront synchronization on single-column pictures with multiple rows. */
	if (pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED) {
		u32 ctb_log2 = sps->log2_min_luma_coding_block_size_minus3 + 3 +
			       sps->log2_diff_max_min_luma_coding_block_size;

		if (sps->pic_width_in_luma_samples <= (1U << ctb_log2))
			return -EOPNOTSUPP;
	}

	return 0;
}

static inline int hevc_synth_vps(const struct v4l2_ctrl_hevc_sps *sps,
				 u8 *dst, u32 cap, u32 *len)
{
	u8 rbsp[64];
	struct hevc_bw bw;
	u32 rbsp_len;
	int ret;

	hevc_bw_init(&bw, rbsp, sizeof(rbsp));
	hevc_bw_u(&bw, sps->video_parameter_set_id, 4);
	hevc_bw_u(&bw, 1, 1);
	hevc_bw_u(&bw, 1, 1);
	hevc_bw_u(&bw, 0, 6);
	hevc_bw_u(&bw, sps->sps_max_sub_layers_minus1, 3);
	hevc_bw_u(&bw, 1, 1);
	hevc_bw_u(&bw, 0xffff, 16);
	hevc_ptl_write(&bw, sps);
	/* Only the highest-layer tuple exists in the SPS control; lower
	 * layers inherit it when ordering_info_present_flag is zero.
	 */
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_ue(&bw, sps->sps_max_dec_pic_buffering_minus1);
	hevc_bw_ue(&bw, sps->sps_max_num_reorder_pics);
	hevc_bw_ue(&bw, sps->sps_max_latency_increase_plus1);
	hevc_bw_u(&bw, 0, 6);
	hevc_bw_ue(&bw, 0);
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_trailing(&bw);
	if (bw.err)
		return bw.err;
	rbsp_len = hevc_bw_bytes(&bw);
	ret = hevc_emit_nal(dst, cap, 32, rbsp, rbsp_len, len);
	return ret;
}

static inline int hevc_synth_sps(const struct v4l2_ctrl_hevc_sps *sps,
				 const struct hevc_rps *rps,
				 u8 *dst, u32 cap, u32 *len)
{
#ifdef __KERNEL__
	u8 *rbsp = kmalloc(HEVC_SYNTH_SPS_RBSP_SIZE, GFP_KERNEL);
#else
	u8 *rbsp = malloc(HEVC_SYNTH_SPS_RBSP_SIZE);
#endif
	struct hevc_bw bw;
	u32 rbsp_len;
	int ret;

	unsigned int i, j;

	if (!rbsp)
		return -ENOMEM;
	if (sps->num_short_term_ref_pic_sets > HEVC_RPS_MAX_SETS ||
	    (sps->num_short_term_ref_pic_sets && !rps)) {
		ret = -EINVAL;
		goto out;
	}
	hevc_bw_init(&bw, rbsp, HEVC_SYNTH_SPS_RBSP_SIZE);
	hevc_bw_u(&bw, sps->video_parameter_set_id, 4);
	hevc_bw_u(&bw, sps->sps_max_sub_layers_minus1, 3);
	hevc_bw_u(&bw, 1, 1);
	hevc_ptl_write(&bw, sps);
	hevc_bw_ue(&bw, sps->seq_parameter_set_id);
	hevc_bw_ue(&bw, sps->chroma_format_idc);
	hevc_bw_ue(&bw, sps->pic_width_in_luma_samples);
	hevc_bw_ue(&bw, sps->pic_height_in_luma_samples);
	hevc_bw_u(&bw, 0, 1); /* conformance_window_flag */
	hevc_bw_ue(&bw, sps->bit_depth_luma_minus8);
	hevc_bw_ue(&bw, sps->bit_depth_chroma_minus8);
	hevc_bw_ue(&bw, sps->log2_max_pic_order_cnt_lsb_minus4);
	/* Only the highest-layer tuple exists in the SPS control; lower
	 * layers inherit it when ordering_info_present_flag is zero.
	 */
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_ue(&bw, sps->sps_max_dec_pic_buffering_minus1);
	hevc_bw_ue(&bw, sps->sps_max_num_reorder_pics);
	hevc_bw_ue(&bw, sps->sps_max_latency_increase_plus1);
	hevc_bw_ue(&bw, sps->log2_min_luma_coding_block_size_minus3);
	hevc_bw_ue(&bw, sps->log2_diff_max_min_luma_coding_block_size);
	hevc_bw_ue(&bw, sps->log2_min_luma_transform_block_size_minus2);
	hevc_bw_ue(&bw, sps->log2_diff_max_min_luma_transform_block_size);
	hevc_bw_ue(&bw, sps->max_transform_hierarchy_depth_inter);
	hevc_bw_ue(&bw, sps->max_transform_hierarchy_depth_intra);
	/* sps_scaling_list_data_present_flag = 0: the PPS carries the lists */
	hevc_bw_u(&bw, (sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED) ? 2 : 0,
		  (sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED) ? 2 : 1);
	hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED), 1);
	hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET), 1);
	hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED), 1);
	if (sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED) {
		hevc_bw_u(&bw, sps->pcm_sample_bit_depth_luma_minus1, 4);
		hevc_bw_u(&bw, sps->pcm_sample_bit_depth_chroma_minus1, 4);
		hevc_bw_ue(&bw, sps->log2_min_pcm_luma_coding_block_size_minus3);
		hevc_bw_ue(&bw, sps->log2_diff_max_min_pcm_luma_coding_block_size);
		hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED), 1);
	}
	hevc_bw_ue(&bw, sps->num_short_term_ref_pic_sets);
	for (i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
		const struct hevc_rps *r = rps + i;
		int prev = 0;

		if (i)
			hevc_bw_bit(&bw, 0); /* explicit; keep original set ordinal */
		hevc_bw_ue(&bw, r->negative);
		hevc_bw_ue(&bw, r->positive);
		for (j = 0; j < r->negative; j++) {
			hevc_bw_ue(&bw, prev - r->delta[j] - 1);
			hevc_bw_bit(&bw, !!(r->used & (1U << j)));
			prev = r->delta[j];
		}
		prev = 0;
		for (; j < r->negative + r->positive; j++) {
			hevc_bw_ue(&bw, r->delta[j] - prev - 1);
			hevc_bw_bit(&bw, !!(r->used & (1U << j)));
			prev = r->delta[j];
		}
	}
	hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT), 1);
	if (sps->flags & V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT)
		hevc_bw_ue(&bw, 0); /* num_long_term_ref_pics_sps */
	hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED), 1);
	hevc_bw_u(&bw, !!(sps->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED), 1);
	/* VUI uses square pixels, a video-signal description and 1/30 timing. */
	hevc_bw_u(&bw, 1, 1);
	hevc_bw_u(&bw, 1, 1); hevc_bw_u(&bw, 1, 8);
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 1, 1); hevc_bw_u(&bw, 5, 3); hevc_bw_u(&bw, 0, 1); hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 0, 1); hevc_bw_u(&bw, 0, 1); hevc_bw_u(&bw, 0, 1); hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 1, 1); hevc_bw_u(&bw, 1, 32); hevc_bw_u(&bw, 30, 32);
	hevc_bw_u(&bw, 0, 1); hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_u(&bw, 0, 1); /* sps_extension_present_flag */
	hevc_bw_trailing(&bw);
	if (bw.err) {
		ret = bw.err;
		goto out;
	}
	rbsp_len = hevc_bw_bytes(&bw);
	ret = hevc_emit_nal(dst, cap, 33, rbsp, rbsp_len, len);
out:
#ifdef __KERNEL__
	kfree(rbsp);
#else
	free(rbsp);
#endif
	return ret;
}

/* H.265 6.5.3 up-right diagonal scan, as raster positions */
static const u8 hevc_diag_scan4x4[16] = {
	 0,  4,  1,  8,  5,  2, 12,  9,
	 6,  3, 13, 10,  7, 14, 11, 15,
};

static const u8 hevc_diag_scan8x8[64] = {
	 0,  8,  1, 16,  9,  2, 24, 17,
	10,  3, 32, 25, 18, 11,  4, 40,
	33, 26, 19, 12,  5, 48, 41, 34,
	27, 20, 13,  6, 56, 49, 42, 35,
	28, 21, 14,  7, 57, 50, 43, 36,
	29, 22, 15, 58, 51, 44, 37, 30,
	23, 59, 52, 45, 38, 31, 60, 53,
	46, 39, 61, 54, 47, 62, 55, 63,
};

static inline void hevc_synth_scaling_list(struct hevc_bw *bw, const u8 *list,
					   u32 num, int dc)
{
	const u8 *scan = num == 16 ? hevc_diag_scan4x4 : hevc_diag_scan8x8;
	int next = 8, delta;
	u32 i;

	hevc_bw_u(bw, 1, 1); /* scaling_list_pred_mode_flag */
	if (dc >= 0) {
		if (dc < 1 || dc > 255)
			bw->err = -EINVAL;
		hevc_bw_se(bw, dc - 8);
		next = dc;
	}
	for (i = 0; i < num; i++) {
		int coef = list[scan[i]];

		if (!coef)
			bw->err = -EINVAL;
		delta = coef - next;
		if (delta > 127)
			delta -= 256;
		else if (delta < -128)
			delta += 256;
		hevc_bw_se(bw, delta);
		next = coef;
	}
}

/*
 * Emit the effective scaling matrices explicitly, including inherited and
 * default values, so firmware need not derive them.
 */
static inline void
hevc_synth_scaling_list_data(struct hevc_bw *bw,
			     const struct v4l2_ctrl_hevc_scaling_matrix *sm)
{
	u32 m;

	for (m = 0; m < 6; m++)
		hevc_synth_scaling_list(bw, sm->scaling_list_4x4[m], 16, -1);
	for (m = 0; m < 6; m++)
		hevc_synth_scaling_list(bw, sm->scaling_list_8x8[m], 64, -1);
	for (m = 0; m < 6; m++)
		hevc_synth_scaling_list(bw, sm->scaling_list_16x16[m], 64,
					sm->scaling_list_dc_coef_16x16[m]);
	for (m = 0; m < 2; m++)
		hevc_synth_scaling_list(bw, sm->scaling_list_32x32[m], 64,
					sm->scaling_list_dc_coef_32x32[m]);
}

static inline int hevc_synth_pps(const struct v4l2_ctrl_hevc_sps *sps,
				 const struct v4l2_ctrl_hevc_pps *pps,
				 const struct v4l2_ctrl_hevc_scaling_matrix *sm,
				 u8 *dst, u32 cap, u32 *len)
{
	bool tiles = pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED;
	bool uniform = pps->flags & V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING;
	bool scaling = sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED;
#ifdef __KERNEL__
	u8 *rbsp = kmalloc(HEVC_SYNTH_PPS_RBSP_SIZE, GFP_KERNEL);
#else
	u8 *rbsp = malloc(HEVC_SYNTH_PPS_RBSP_SIZE);
#endif
	struct hevc_bw bw;
	u32 rbsp_len, i;
	int ret;

	if (!rbsp)
		return -ENOMEM;
	if (scaling && !sm) {
		ret = -EINVAL;
		goto out;
	}

	if (tiles) {
		u32 ctb_log2 = sps->log2_min_luma_coding_block_size_minus3 + 3 +
			       sps->log2_diff_max_min_luma_coding_block_size;
		u32 ctb = 1U << ctb_log2;
		u32 w = (sps->pic_width_in_luma_samples + ctb - 1) >> ctb_log2;
		u32 h = (sps->pic_height_in_luma_samples + ctb - 1) >> ctb_log2;

		if (!hevc_tile_count_ok(pps->num_tile_columns_minus1 + 1, uniform) ||
		    !hevc_tile_count_ok(pps->num_tile_rows_minus1 + 1, uniform) ||
		    !hevc_tile_axis_ok(w, pps->num_tile_columns_minus1 + 1, uniform,
				       pps->column_width_minus1) ||
		    !hevc_tile_axis_ok(h, pps->num_tile_rows_minus1 + 1, uniform,
				       pps->row_height_minus1)) {
			ret = -EOPNOTSUPP;
			goto out;
		}
	}
	hevc_bw_init(&bw, rbsp, HEVC_SYNTH_PPS_RBSP_SIZE);
	hevc_bw_ue(&bw, pps->pic_parameter_set_id);
	hevc_bw_ue(&bw, sps->seq_parameter_set_id);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT), 1);
	hevc_bw_u(&bw, pps->num_extra_slice_header_bits, 3);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT), 1);
	hevc_bw_ue(&bw, pps->num_ref_idx_l0_default_active_minus1);
	hevc_bw_ue(&bw, pps->num_ref_idx_l1_default_active_minus1);
	hevc_bw_se(&bw, pps->init_qp_minus26);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED), 1);
	if (pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED)
		hevc_bw_ue(&bw, pps->diff_cu_qp_delta_depth);
	hevc_bw_se(&bw, pps->pps_cb_qp_offset);
	hevc_bw_se(&bw, pps->pps_cr_qp_offset);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED), 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED), 1);
	hevc_bw_u(&bw, tiles, 1);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED), 1);
	if (tiles) {
		hevc_bw_ue(&bw, pps->num_tile_columns_minus1);
		hevc_bw_ue(&bw, pps->num_tile_rows_minus1);
		hevc_bw_u(&bw, uniform, 1);
		if (!uniform) {
			for (i = 0; i < pps->num_tile_columns_minus1; i++)
				hevc_bw_ue(&bw, pps->column_width_minus1[i]);
			for (i = 0; i < pps->num_tile_rows_minus1; i++)
				hevc_bw_ue(&bw, pps->row_height_minus1[i]);
		}
		hevc_bw_u(&bw,
			  !!(pps->flags & V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED), 1);
	}
	hevc_bw_u(&bw,
		  !!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED), 1);
	/*
	 * The slices are fed as coded, so every PPS flag that gates slice
	 * header syntax has to be the stream's own: the firmware parses the
	 * deblocking override and the list modification from those slices.
	 */
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT), 1);
	if (pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) {
		bool disabled = pps->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER;

		hevc_bw_u(&bw,
			  !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED),
			  1);
		hevc_bw_u(&bw, disabled, 1);
		if (!disabled) {
			hevc_bw_se(&bw, pps->pps_beta_offset_div2);
			hevc_bw_se(&bw, pps->pps_tc_offset_div2);
		}
	}
	hevc_bw_u(&bw, scaling, 1); /* pps_scaling_list_data_present_flag */
	if (scaling)
		hevc_synth_scaling_list_data(&bw, sm);
	hevc_bw_u(&bw, !!(pps->flags & V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT), 1);
	hevc_bw_ue(&bw, pps->log2_parallel_merge_level_minus2);
	hevc_bw_u(&bw,
		  !!(pps->flags & V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT), 1);
	hevc_bw_u(&bw, 0, 1);
	hevc_bw_trailing(&bw);
	if (bw.err) {
		ret = bw.err;
		goto out;
	}
	rbsp_len = hevc_bw_bytes(&bw);
	ret = hevc_emit_nal(dst, cap, 34, rbsp, rbsp_len, len);
out:
#ifdef __KERNEL__
	kfree(rbsp);
#else
	free(rbsp);
#endif
	return ret;
}

static inline int hevc_synth_headers(const struct v4l2_ctrl_hevc_sps *sps,
				     const struct hevc_rps *rps,
				     const struct v4l2_ctrl_hevc_pps *pps,
				     const struct v4l2_ctrl_hevc_scaling_matrix *sm,
				     u8 *dst, u32 cap, u32 *len)
{
	u32 l, off = 0;
	int ret;

	ret = hevc_synth_validate(sps, pps);
	if (ret)
		return ret;
	ret = hevc_synth_vps(sps, dst + off, cap - off, &l);
	if (ret)
		return ret;
	off += l;
	ret = hevc_synth_sps(sps, rps, dst + off, cap - off, &l);
	if (ret)
		return ret;
	off += l;
	ret = hevc_synth_pps(sps, pps, sm, dst + off, cap - off, &l);
	if (ret)
		return ret;
	off += l;
	*len = off;
	return 0;
}

#endif
