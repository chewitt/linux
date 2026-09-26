/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * H.264 header synthesis and slice parsing.
 */

#ifndef __MESON_AMVDEC_CODEC_H264_SYNTH_H_
#define __MESON_AMVDEC_CODEC_H264_SYNTH_H_

#ifdef __KERNEL__
#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef long long s64;
#include <linux/v4l2-controls.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#endif

#define H264_SYNTH_MAX_HEADERS 512

#include "codec_h264_scratch.h"

struct h264_bw {
	u8 *buf;
	u32 cap;
	u32 bitpos;
	int err;
};

static inline void h264_bw_init(struct h264_bw *bw, u8 *buf, u32 cap)
{
	bw->buf = buf;
	bw->cap = cap;
	bw->bitpos = 0;
	bw->err = 0;
	memset(buf, 0, cap);
}

static inline void h264_bw_bit(struct h264_bw *bw, u32 bit)
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

static inline void h264_bw_u(struct h264_bw *bw, u32 val, u32 bits)
{
	while (bits--)
		h264_bw_bit(bw, (val >> bits) & 1);
}

static inline void h264_bw_ue(struct h264_bw *bw, u32 val)
{
	u32 code = val + 1;
	u32 bits = 32 - __builtin_clz(code);
	u32 i;

	for (i = 1; i < bits; i++)
		h264_bw_bit(bw, 0);
	h264_bw_u(bw, code, bits);
}

static inline void h264_bw_se(struct h264_bw *bw, s32 val)
{
	u32 code = val <= 0 ? (u32)(-2 * val) : (u32)(2 * val - 1);

	h264_bw_ue(bw, code);
}

static inline void h264_bw_trailing(struct h264_bw *bw)
{
	h264_bw_bit(bw, 1);
	while (bw->bitpos & 7)
		h264_bw_bit(bw, 0);
}

static inline u32 h264_bw_bytes(const struct h264_bw *bw)
{
	return (bw->bitpos + 7) >> 3;
}

static inline int h264_epb_copy(u8 *dst, u32 cap, const u8 *src, u32 len,
				u32 *out_len)
{
	u32 zeros = 0, out = 0, i;

	for (i = 0; i < len; i++) {
		if (zeros >= 2 && src[i] <= 3) {
			if (out >= cap)
				return -ENOSPC;
			dst[out++] = 3;
			zeros = 0;
		}
		if (out >= cap)
			return -ENOSPC;
		dst[out++] = src[i];
		zeros = src[i] == 0 ? zeros + 1 : 0;
	}
	*out_len = out;
	return 0;
}

static inline int h264_emit_nal(u8 *dst, u32 cap, u8 header,
				const u8 *rbsp, u32 rbsp_len, u32 *out_len)
{
	u32 epb_len;
	int ret;

	if (cap < 5)
		return -ENOSPC;
	dst[0] = 0;
	dst[1] = 0;
	dst[2] = 0;
	dst[3] = 1;
	dst[4] = header;
	ret = h264_epb_copy(dst + 5, cap - 5, rbsp, rbsp_len, &epb_len);
	if (ret)
		return ret;
	*out_len = 5 + epb_len;
	return 0;
}

/*
 * Profiles whose SPS carries the chroma/bit-depth/scaling-matrix block
 * (7.3.2.1.1).  Emitting an SPS without it for these profiles produces a
 * stream the firmware cannot parse.
 */
static inline bool h264_profile_has_chroma_ext(u8 profile_idc)
{
	switch (profile_idc) {
	case 100: case 110: case 122: case 244:
	case 44: case 83: case 86:
	case 118: case 128: case 138: case 139: case 134: case 135:
		return true;
	default:
		return false;
	}
}

static inline int h264_sps_validate(const struct v4l2_ctrl_h264_sps *sps)
{
	const u32 unsupported_sps =
		V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE |
		V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS;

	/*
	 * Accept supported Baseline, Main and High syntax with 4:2:0 8-bit samples.
	 * Extended streams asserting constraint_set1 conform to Main (A.2.2).
	 */
	if (sps->profile_idc != 66 && sps->profile_idc != 77 &&
	    !(sps->profile_idc == 88 &&
	      (sps->constraint_set_flags & V4L2_H264_SPS_CONSTRAINT_SET1_FLAG)) &&
	    !h264_profile_has_chroma_ext(sps->profile_idc))
		return -EOPNOTSUPP;
	if (sps->chroma_format_idc != 1 || sps->bit_depth_luma_minus8 ||
	    sps->bit_depth_chroma_minus8)
		return -EOPNOTSUPP;
	/*
	 * Frame geometry uses map-unit height; field-picture admission is checked
	 * from the slice header.
	 */
	if (sps->flags & unsupported_sps)
		return -EOPNOTSUPP;
	if (sps->pic_order_cnt_type > 2)
		return -EINVAL;
	return 0;
}

static inline bool h264_scaling_matrix_flat(const struct v4l2_ctrl_h264_sps *sps,
					    const struct v4l2_ctrl_h264_pps *pps,
						 const struct v4l2_ctrl_h264_scaling_matrix *sm)
{
	unsigned int i, j, n8;

	if (!(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT))
		return true;
	if (!sm)
		return false;
	for (i = 0; i < ARRAY_SIZE(sm->scaling_list_4x4); i++)
		for (j = 0; j < ARRAY_SIZE(sm->scaling_list_4x4[0]); j++)
			if (sm->scaling_list_4x4[i][j] != 16)
				return false;
	n8 = sps->chroma_format_idc == 3 ?
	     ARRAY_SIZE(sm->scaling_list_8x8) : 2;
	for (i = 0; i < n8; i++)
		for (j = 0; j < ARRAY_SIZE(sm->scaling_list_8x8[0]); j++)
			if (sm->scaling_list_8x8[i][j] != 16)
				return false;
	return true;
}

static inline int h264_synth_validate(const struct v4l2_ctrl_h264_sps *sps,
				      const struct v4l2_ctrl_h264_pps *pps,
				      const struct v4l2_ctrl_h264_scaling_matrix *sm)
{
	const u16 unsupported_pps = 0;
	int ret;

	ret = h264_sps_validate(sps);
	if (ret)
		return ret;
	if (pps->num_slice_groups_minus1 || (pps->flags & unsupported_pps))
		return -EOPNOTSUPP;
	return 0;
}

static const u8 h264_zigzag_4x4[16] = {
	0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15,
};

static const u8 h264_zigzag_8x8[64] = {
	0, 1, 8, 16, 9, 2, 3, 10,
	17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63,
};

static inline void h264_bw_scaling_list(struct h264_bw *bw, const u8 *list,
					const u8 *scan, u32 size)
{
	s32 last = 8;
	u32 i;

	for (i = 0; i < size; i++) {
		s32 next = list[scan[i]];

		/* delta_scale is signed 8-bit; nextScale wraps modulo 256. */
		h264_bw_se(bw, ((next - last + 128) & 255) - 128);
		last = next;
	}
}

static inline int h264_synth_sps(const struct v4l2_ctrl_h264_sps *sps,
				 u8 *dst, u32 cap, u32 *len)
{
	u8 rbsp[256];
	struct h264_bw bw;
	u32 constraint_byte = 0;
	u32 i;

	h264_bw_init(&bw, rbsp, sizeof(rbsp));
	h264_bw_u(&bw, sps->profile_idc, 8);
	/* V4L2 numbers constraint_set0..5 from the LSB; the RBSP stores
	 * constraint_set0 first, at bit 7, followed by two reserved zero bits.
	 */
	for (i = 0; i < 6; i++)
		if (sps->constraint_set_flags & (1 << i))
			constraint_byte |= 1 << (7 - i);
	h264_bw_u(&bw, constraint_byte, 8);
	h264_bw_u(&bw, sps->level_idc, 8);
	h264_bw_ue(&bw, sps->seq_parameter_set_id);
	if (h264_profile_has_chroma_ext(sps->profile_idc)) {
		h264_bw_ue(&bw, sps->chroma_format_idc);
		if (sps->chroma_format_idc == 3)
			h264_bw_bit(&bw, !!(sps->flags &
				V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE));
		h264_bw_ue(&bw, sps->bit_depth_luma_minus8);
		h264_bw_ue(&bw, sps->bit_depth_chroma_minus8);
		h264_bw_bit(&bw, !!(sps->flags &
			V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS));
		/* Effective lists, including SPS inheritance, are emitted in PPS. */
		h264_bw_bit(&bw, 0); /* seq_scaling_matrix_present_flag */
	}
	h264_bw_ue(&bw, sps->log2_max_frame_num_minus4);
	h264_bw_ue(&bw, sps->pic_order_cnt_type);
	if (sps->pic_order_cnt_type == 0) {
		h264_bw_ue(&bw, sps->log2_max_pic_order_cnt_lsb_minus4);
	} else if (sps->pic_order_cnt_type == 1) {
		h264_bw_bit(&bw, !!(sps->flags &
				       V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO));
		h264_bw_se(&bw, sps->offset_for_non_ref_pic);
		h264_bw_se(&bw, sps->offset_for_top_to_bottom_field);
		h264_bw_ue(&bw, sps->num_ref_frames_in_pic_order_cnt_cycle);
		for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
			h264_bw_se(&bw, sps->offset_for_ref_frame[i]);
	}
	h264_bw_ue(&bw, sps->max_num_ref_frames);
	h264_bw_bit(&bw, !!(sps->flags &
			       V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED));
	h264_bw_ue(&bw, sps->pic_width_in_mbs_minus1);
	h264_bw_ue(&bw, sps->pic_height_in_map_units_minus1);
	h264_bw_bit(&bw, !!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY));
	if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY))
		h264_bw_bit(&bw, !!(sps->flags &
				    V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD));
	h264_bw_bit(&bw, !!(sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE));
	h264_bw_bit(&bw, 0); /* frame_cropping_flag: absent from the V4L2 control */
	h264_bw_bit(&bw, 0); /* vui_parameters_present_flag: absent from the control */
	h264_bw_trailing(&bw);
	if (bw.err)
		return bw.err;
	return h264_emit_nal(dst, cap, 0x67, rbsp, h264_bw_bytes(&bw), len);
}

static inline int h264_synth_pps(const struct v4l2_ctrl_h264_sps *sps,
				 const struct v4l2_ctrl_h264_pps *pps,
				 const struct v4l2_ctrl_h264_scaling_matrix *sm,
				 u8 *dst, u32 cap, u32 *len)
{
	u8 rbsp[512];
	struct h264_bw bw;
	bool extension, emit_scaling;
	u32 i;

	h264_bw_init(&bw, rbsp, sizeof(rbsp));
	h264_bw_ue(&bw, pps->pic_parameter_set_id);
	h264_bw_ue(&bw, pps->seq_parameter_set_id);
	h264_bw_bit(&bw, !!(pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE));
	h264_bw_bit(&bw, !!(pps->flags &
			       V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT));
	h264_bw_ue(&bw, pps->num_slice_groups_minus1);
	h264_bw_ue(&bw, pps->num_ref_idx_l0_default_active_minus1);
	h264_bw_ue(&bw, pps->num_ref_idx_l1_default_active_minus1);
	h264_bw_bit(&bw, !!(pps->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED));
	h264_bw_u(&bw, pps->weighted_bipred_idc, 2);
	h264_bw_se(&bw, pps->pic_init_qp_minus26);
	h264_bw_se(&bw, pps->pic_init_qs_minus26);
	h264_bw_se(&bw, pps->chroma_qp_index_offset);
	h264_bw_bit(&bw, !!(pps->flags &
			       V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT));
	h264_bw_bit(&bw, !!(pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED));
	h264_bw_bit(&bw, !!(pps->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT));
	/* Emit explicit scaling lists only when they differ from flat defaults. */
	emit_scaling = sm &&
		       (pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT) &&
		       !h264_scaling_matrix_flat(sps, pps, sm);
	extension = emit_scaling ||
		    (pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE) ||
		    pps->second_chroma_qp_index_offset !=
		    pps->chroma_qp_index_offset;
	if (extension) {
		h264_bw_bit(&bw, !!(pps->flags &
				    V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE));
		h264_bw_bit(&bw, emit_scaling); /* pic_scaling_matrix_present_flag */
		if (emit_scaling) {
			for (i = 0; i < ARRAY_SIZE(sm->scaling_list_4x4); i++) {
				h264_bw_bit(&bw, 1);
				h264_bw_scaling_list(&bw, sm->scaling_list_4x4[i],
						     h264_zigzag_4x4,
						      ARRAY_SIZE(h264_zigzag_4x4));
			}
			for (i = 0;
			     (pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE) &&
			     i < (sps->chroma_format_idc == 3 ? 6 : 2); i++) {
				h264_bw_bit(&bw, 1);
				h264_bw_scaling_list(&bw, sm->scaling_list_8x8[i],
						     h264_zigzag_8x8,
						      ARRAY_SIZE(h264_zigzag_8x8));
			}
		}
		h264_bw_se(&bw, pps->second_chroma_qp_index_offset);
	}
	h264_bw_trailing(&bw);
	if (bw.err)
		return bw.err;
	return h264_emit_nal(dst, cap, 0x68, rbsp, h264_bw_bytes(&bw), len);
}

/*
 * Parse slice-header reference-list modifications for frame-based requests;
 * the host supplies final lists to the firmware.
 */

#define H264_MAX_RPLM	32

struct h264_rplm_op {
	u8  idc;	/* modification_of_pic_nums_idc */
	u32 arg;	/* abs_diff_pic_num_minus1 or long_term_pic_num */
};

struct h264_slice_refs {
	u32 first_mb;
	u32 pps_id;
	u32 idr_pic_id;
	u32 poc_lsb;
	s32 delta_poc_bottom, delta_poc[2];
	u8 nal_ref_idc;
	bool idr;
	u32 refs_header_bits; /* End of parsed prefix, before I-field marking. */
	u32 initial_mb_count; /* CAVLC skip run plus the first coded macroblock. */
	u32 slice_type;			/* 0 P, 1 B, 2 I, 5/6/7 same mod 5 */
	u32 frame_num;
	u32 num_ref_idx_active[2];
	bool field_pic;
	bool bottom_field;
	bool mbaff;
	struct h264_rplm_op mod[2][H264_MAX_RPLM];
	u32 n_mod[2];
	bool valid;
};

struct h264_br {
	const u8 *buf;
	u32 len;
	u32 bit;
	int err;
};

static inline u32 h264_br_u1(struct h264_br *r)
{
	u32 byte = r->bit >> 3;

	if (byte >= r->len) {
		r->err = -EINVAL;
		return 0;
	}
	return (r->buf[byte] >> (7 - (r->bit++ & 7))) & 1;
}

static inline u32 h264_br_u(struct h264_br *r, u32 n)
{
	u32 v = 0;

	while (n--)
		v = (v << 1) | h264_br_u1(r);
	return v;
}

static inline u32 h264_br_ue(struct h264_br *r)
{
	u32 zeros = 0;

	while (!r->err && !h264_br_u1(r)) {
		if (++zeros > 31) {
			r->err = -EINVAL;
			return 0;
		}
	}
	if (r->err || !zeros)
		return 0;
	return (1u << zeros) - 1 + h264_br_u(r, zeros);
}

static inline s32 h264_br_se(struct h264_br *r)
{
	u32 k = h264_br_ue(r);

	return (k & 1) ? (s32)((k + 1) >> 1) : -(s32)(k >> 1);
}

/* Copy a bounded RBSP prefix to @dst, removing emulation-prevention bytes. */
static inline u32 h264_unescape(const u8 *src, u32 len, u8 *dst, u32 want)
{
	u32 i = 0, o = 0, zeros = 0;

	while (i < len && o < want) {
		u8 b = src[i++];

		if (zeros >= 2 && b == 0x03) {
			zeros = 0;
			continue;
		}
		zeros = b ? 0 : zeros + 1;
		dst[o++] = b;
	}
	return o;
}

static inline int h264_skip_pred_weight_table(struct h264_br *r,
					      const struct v4l2_ctrl_h264_sps *sps,
		const struct h264_slice_refs *sr)
{
	u32 lx, i, j, lists = (sr->slice_type % 5) == 1 ? 2 : 1;
	bool chroma = sps->chroma_format_idc != 0;

	if (h264_br_ue(r) > 7)
		return -EINVAL;
	if (chroma && h264_br_ue(r) > 7)
		return -EINVAL;
	for (lx = 0; lx < lists; lx++) {
		for (i = 0; i < sr->num_ref_idx_active[lx]; i++) {
			if (h264_br_u1(r)) {
				h264_br_se(r); /* luma_weight_lX */
				h264_br_se(r); /* luma_offset_lX */
			}
			if (!chroma)
				continue;
			if (!h264_br_u1(r))
				continue;
			for (j = 0; j < 2; j++) {
				h264_br_se(r); /* chroma_weight_lX */
				h264_br_se(r); /* chroma_offset_lX */
			}
		}
	}
	return r->err;
}

static inline int h264_skip_dec_ref_pic_marking(struct h264_br *r,
						const struct h264_slice_refs *sr)
{
	u32 count = 0;

	if (!sr->nal_ref_idc)
		return 0;
	if (sr->idr) {
		h264_br_u1(r); /* no_output_of_prior_pics_flag */
		h264_br_u1(r); /* long_term_reference_flag */
		return r->err;
	}
	if (!h264_br_u1(r)) /* adaptive_ref_pic_marking_mode_flag */
		return r->err;
	while (!r->err) {
		u32 op = h264_br_ue(r);

		if (!op)
			break;
		if (++count > H264_MAX_RPLM) {
			r->err = -EINVAL;
			break;
		}
		switch (op) {
		case 1:
			h264_br_ue(r); /* difference_of_pic_nums_minus1 */
			break;
		case 2:
			h264_br_ue(r); /* long_term_pic_num */
			break;
		case 3:
			h264_br_ue(r); /* difference_of_pic_nums_minus1 */
			h264_br_ue(r); /* long_term_frame_idx */
			break;
		case 4:
			h264_br_ue(r); /* max_long_term_frame_idx_plus1 */
			break;
		case 5:
			break;
		case 6:
			h264_br_ue(r); /* long_term_frame_idx */
			break;
		default:
			r->err = -EINVAL;
			break;
		}
	}
	return r->err;
}

static inline int h264_validate_slice_header_tail(struct h264_br *r,
						  const struct v4l2_ctrl_h264_sps *sps,
		const struct v4l2_ctrl_h264_pps *pps,
		struct h264_slice_refs *sr)
{
	u32 type = sr->slice_type % 5;
	s32 qp;

	if (((pps->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED) && type == 0) ||
	    (pps->weighted_bipred_idc == 1 && type == 1)) {
		int ret = h264_skip_pred_weight_table(r, sps, sr);

		if (ret)
			return ret;
	}
	sr->refs_header_bits = r->bit;
	if (h264_skip_dec_ref_pic_marking(r, sr))
		return r->err;
	if ((pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE) && type != 2) {
		if (h264_br_ue(r) > 2) /* cabac_init_idc */
			return -EINVAL;
	}
	qp = 26 + pps->pic_init_qp_minus26 + h264_br_se(r);
	if (qp < 0 || qp > 51)
		return -EINVAL;
	if (pps->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) {
		u32 disable = h264_br_ue(r);

		if (disable > 2)
			return -EINVAL;
		if (disable != 1) {
			s32 alpha = h264_br_se(r);
			s32 beta = h264_br_se(r);

			if (alpha < -6 || alpha > 6 || beta < -6 || beta > 6)
				return -EINVAL;
		}
	}
	return r->err;
}

/* Locate the first VCL NAL (type 1 or 5) in an Annex B access unit. */
static inline const u8 *h264_find_first_slice(const u8 *buf, u32 len, u32 *nal_len)
{
	u32 i = 0, start = 0;
	bool have = false;

	while (i < len && len - i >= 3) {
		if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1) {
			u32 payload = i + 3;

			if (have) {
				u32 end = i;

				while (end > start && buf[end - 1] == 0)
					end--;
				*nal_len = end - start;
				return buf + start;
			}
			if (payload < len) {
				u8 t = buf[payload] & 0x1f;

				if (t == 1 || t == 5) {
					start = payload;
					have = true;
				}
			}
			i = payload;
			continue;
		}
		i++;
	}
	if (have) {
		*nal_len = len - start;
		return buf + start;
	}
	return NULL;
}

/* Reject unsupported syntax before any part of an access unit is fed. */
static inline int h264_request_preflight(const u8 *buf, u32 len,
					 const struct v4l2_ctrl_h264_pps *pps)
{
	const u8 *nal;
	u32 cursor = 0, count = 0, nal_len;

	if (pps->num_slice_groups_minus1)
		return -EOPNOTSUPP;
	if (!buf)
		return -EINVAL;
	while (cursor < len &&
	       (nal = h264_find_first_slice(buf + cursor, len - cursor, &nal_len))) {
		u8 rbsp[16];
		struct h264_br r = { .buf = rbsp };
		u32 type;

		r.len = h264_unescape(nal + 1, nal_len - 1, rbsp, sizeof(rbsp));
		h264_br_ue(&r); /* first_mb_in_slice */
		type = h264_br_ue(&r);
		if (r.err)
			return r.err;
		/* The same slice_type domain as h264_parse_slice_refs(). */
		if (type > 9 || type % 5 > 2)
			return -EOPNOTSUPP;
		cursor = (u32)(nal - buf) + nal_len;
		count++;
	}
	return count ? 0 : -EINVAL;
}

/*
 * Parse one slice header far enough to recover the reference list
 * modification commands.  @nal points at the NAL header byte.
 */
static inline int h264_parse_slice_refs_mode(const u8 *nal, u32 len,
					     const struct v4l2_ctrl_h264_sps *sps,
					     const struct v4l2_ctrl_h264_pps *pps,
					     bool allow_inter_field,
					     struct h264_slice_refs *out,
					     struct h264_parse_scratch *scratch)
{
	u8 *rbsp = scratch->rbsp;
	struct h264_br r;
	u32 type, lx, n;
	bool idr;
	int ret;

	memset(out, 0, sizeof(*out));
	if (!len)
		return -EINVAL;

	idr = (nal[0] & 0x1f) == 5;
	r.buf = rbsp;
	r.len = h264_unescape(nal + 1, len - 1, rbsp, sizeof(scratch->rbsp));
	r.bit = 0;
	r.err = 0;

	out->first_mb = h264_br_ue(&r);
	type = h264_br_ue(&r);
	out->slice_type = type;
	if (type > 9 || type % 5 > 2)
		return -EOPNOTSUPP;
	out->idr = idr;
	out->nal_ref_idc = (nal[0] >> 5) & 3;
	if (nal[0] & 0x80)
		return -EINVAL;
	type %= 5;
	out->pps_id = h264_br_ue(&r);
	if (sps->flags & V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE)
		h264_br_u(&r, 2);		/* colour_plane_id */
	out->frame_num = h264_br_u(&r, sps->log2_max_frame_num_minus4 + 4);

	if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY)) {
		out->field_pic = h264_br_u1(&r);
		if (out->field_pic) {
			out->bottom_field = h264_br_u1(&r);
			/* Inter fields are admitted only by the per-field path. */
			if (type != 2 && !allow_inter_field)
				return -EOPNOTSUPP;
		}
	}
	if (idr)
		out->idr_pic_id = h264_br_ue(&r);

	if (sps->pic_order_cnt_type == 0) {
		out->poc_lsb = h264_br_u(&r, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
		if (!out->field_pic &&
		    (pps->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT))
			out->delta_poc_bottom = h264_br_se(&r);
	} else if (sps->pic_order_cnt_type == 1 &&
		   !(sps->flags & V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO)) {
		out->delta_poc[0] = h264_br_se(&r);
		if (!out->field_pic &&
		    (pps->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT))
			out->delta_poc[1] = h264_br_se(&r);
	}
	if (pps->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT)
		h264_br_ue(&r);			/* redundant_pic_cnt */
	if (type == 1)
		h264_br_u1(&r);			/* direct_spatial_mv_pred_flag */

	out->num_ref_idx_active[0] = pps->num_ref_idx_l0_default_active_minus1 + 1;
	out->num_ref_idx_active[1] = pps->num_ref_idx_l1_default_active_minus1 + 1;
	if (type == 0 || type == 1 || type == 3) {
		if (h264_br_u1(&r)) {	/* num_ref_idx_active_override_flag */
			out->num_ref_idx_active[0] = h264_br_ue(&r) + 1;
			if (type == 1)
				out->num_ref_idx_active[1] = h264_br_ue(&r) + 1;
		}
	}

	/* ref_pic_list_modification(): l0 for P/SP/B, l1 for B only. */
	for (lx = 0; lx < 2; lx++) {
		if (lx == 0 && !(type == 0 || type == 1 || type == 3))
			continue;
		if (lx == 1 && type != 1)
			continue;
		if (!h264_br_u1(&r))
			continue;
		n = 0;
		while (!r.err) {
			u32 idc = h264_br_ue(&r);

			if (idc == 3)
				break;
			if (idc > 2 || n >= H264_MAX_RPLM) {
				r.err = -EINVAL;
				break;
			}
			out->mod[lx][n].idc = idc;
			out->mod[lx][n].arg = h264_br_ue(&r);
			n++;
		}
		out->n_mod[lx] = n;
	}

	if (r.err)
		return r.err;
	ret = h264_validate_slice_header_tail(&r, sps, pps, out);
	if (ret)
		return ret;
	if (r.err)
		return r.err;
	if (!out->field_pic && (type == 0 || type == 1) &&
	    !(pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)) {
		u32 skip = h264_br_ue(&r);
		u32 end = r.len * 8;

		if (r.err)
			return r.err;
		/*
		 * The no-FMO PPS rules out slice_group_change_cycle here.
		 * Only P/B headers end at mb_skip_run; SP/SI have extra fields.
		 * The last set bit is rbsp_stop_one_bit. Any preceding bits
		 * after mb_skip_run belong to a coded macroblock. The bounded
		 * RBSP copy may omit the tail; that only lowers this minimum.
		 */
		while (end > r.bit &&
		       !(r.buf[(end - 1) >> 3] & (1U << (7 - ((end - 1) & 7)))))
			end--;
		if (end > r.bit + 1) {
			if (skip == ~0U)
				return -EINVAL;
			skip++;
		}
		out->initial_mb_count = skip;
	}
	out->mbaff = !out->field_pic && !(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) &&
		     !!(sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD);
	out->valid = true;
	return 0;
}

/* Use the frame and single-field parser subset. */
static inline int h264_parse_slice_refs(const u8 *nal, u32 len,
					const struct v4l2_ctrl_h264_sps *sps,
					const struct v4l2_ctrl_h264_pps *pps,
					struct h264_slice_refs *out,
					struct h264_parse_scratch *scratch)
{
	return h264_parse_slice_refs_mode(nal, len, sps, pps, false, out, scratch);
}

/* Advance over exactly one VCL NAL; non-VCL NALs and start codes are skipped. */
static inline int h264_next_slice_refs_mode(const u8 *buf, u32 len, u32 *cursor,
					    const struct v4l2_ctrl_h264_sps *sps,
					    const struct v4l2_ctrl_h264_pps *pps,
					    bool allow_inter_field,
					    struct h264_slice_refs *out,
					    struct h264_parse_scratch *scratch)
{
	const u8 *nal;
	u32 nal_len = 0;
	int ret;

	if (!buf || *cursor > len)
		return -EINVAL;
	nal = h264_find_first_slice(buf + *cursor, len - *cursor, &nal_len);
	if (!nal)
		return -ENOENT;
	ret = h264_parse_slice_refs_mode(nal, nal_len, sps, pps, allow_inter_field, out, scratch);
	if (ret)
		return ret;
	*cursor = (u32)(nal - buf) + nal_len;
	return 0;
}

static inline int h264_next_slice_refs(const u8 *buf, u32 len, u32 *cursor,
				       const struct v4l2_ctrl_h264_sps *sps,
				       const struct v4l2_ctrl_h264_pps *pps,
				       struct h264_slice_refs *out,
				       struct h264_parse_scratch *scratch)
{
	return h264_next_slice_refs_mode(buf, len, cursor, sps, pps, false, out, scratch);
}

/* Slice type/counts/lists may change inside one coded picture. */
static inline bool h264_same_frame_picture(const struct h264_slice_refs *a,
					   const struct h264_slice_refs *b)
{
	return a->frame_num == b->frame_num && a->pps_id == b->pps_id &&
	       a->field_pic == b->field_pic &&
	       (!a->field_pic || a->bottom_field == b->bottom_field) &&
	       a->idr == b->idr &&
	       (!a->idr || a->idr_pic_id == b->idr_pic_id) &&
	       !!a->nal_ref_idc == !!b->nal_ref_idc &&
	       a->poc_lsb == b->poc_lsb &&
	       a->delta_poc_bottom == b->delta_poc_bottom &&
	       a->delta_poc[0] == b->delta_poc[0] &&
	       a->delta_poc[1] == b->delta_poc[1];
}

/* Validate each frame slice before firmware sees the request. */
static inline int h264_frame_refs_preflight(const u8 *buf, u32 len,
					    const struct v4l2_ctrl_h264_sps *sps,
		const struct v4l2_ctrl_h264_pps *pps,
		const struct h264_slice_refs *request,
		struct h264_parse_scratch *scratch)
{
	struct h264_slice_refs slice;
	u32 cursor = 0, count = 0, previous_mb = 0, type_mask = 0;
	u32 previous_end = 0;
	u32 units;
	bool uniform = false;
	int ret;

	if (request->field_pic)
		return 0;
	if (!buf)
		return -EINVAL;
	units = (sps->pic_width_in_mbs_minus1 + 1) *
		(sps->pic_height_in_map_units_minus1 + 1);
	if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) &&
	    !(sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD))
		units *= 2;
	if (!units)
		return -EINVAL;
	while (!(ret = h264_next_slice_refs(buf, len, &cursor, sps, pps, &slice, scratch))) {
		u32 type = slice.slice_type % 5;
		u32 type_bit = 1U << type;
		u32 start_mb, total_mbs;

		/* Inspect every actual slice, not the request's first-slice type flag. */
		if (slice.field_pic)
			return -EOPNOTSUPP;
		if (!h264_same_frame_picture(&slice, request) ||
		    slice.first_mb >= units || (!count && slice.first_mb) ||
		    (count && slice.first_mb <= previous_mb))
			return -EINVAL;
		start_mb = slice.first_mb << slice.mbaff;
		total_mbs = units << slice.mbaff;
		if (start_mb < previous_end ||
		    slice.initial_mb_count > total_mbs - start_mb)
			return -EBADMSG;
		previous_end = start_mb + slice.initial_mb_count;
		if (type != 2 &&
		    (!slice.num_ref_idx_active[0] ||
		     slice.num_ref_idx_active[0] > V4L2_H264_REF_LIST_LEN))
			return -EINVAL;
		if (type == 1 &&
		    (!slice.num_ref_idx_active[1] ||
		     slice.num_ref_idx_active[1] > V4L2_H264_REF_LIST_LEN))
			return -EINVAL;
		if ((uniform || slice.slice_type >= 5) && (type_mask & ~type_bit))
			return -EINVAL;
		uniform |= slice.slice_type >= 5;
		type_mask |= type_bit;
		previous_mb = slice.first_mb;
		count++;
	}
	return ret == -ENOENT && count ? 0 : (ret == -ENOENT ? -EINVAL : ret);
}

/*
 * Consume dec_ref_pic_marking(). The client has already applied these MMCOs
 * to the DPB control; this validates and advances over the in-band copy that
 * the firmware will process independently.
 */
static inline int h264_field_marking(const u8 *nal, u32 len,
				     const struct h264_slice_refs *sr,
					  struct h264_parse_scratch *scratch)
{
	u8 *rbsp = scratch->rbsp;
	struct h264_br r = { .buf = rbsp, .bit = sr->refs_header_bits };
	u32 count = 0;

	if (!sr->nal_ref_idc)
		return 0;
	r.len = h264_unescape(nal + 1, len - 1, rbsp, sizeof(scratch->rbsp));
	if (sr->idr) {
		h264_br_u1(&r); /* no_output_of_prior_pics_flag */
		h264_br_u1(&r); /* long_term_reference_flag */
		return r.err;
	}
	if (!h264_br_u1(&r)) /* adaptive_ref_pic_marking_mode_flag */
		return r.err;
	while (!r.err) {
		u32 op = h264_br_ue(&r);

		if (!op)
			break;
		if (++count > H264_MAX_RPLM) {
			r.err = -EINVAL;
			break;
		}
		switch (op) {
		case 1:
			h264_br_ue(&r); /* difference_of_pic_nums_minus1 */
			break;
		case 2:
			h264_br_ue(&r); /* long_term_pic_num */
			break;
		case 3:
			h264_br_ue(&r); /* difference_of_pic_nums_minus1 */
			h264_br_ue(&r); /* long_term_frame_idx */
			break;
		case 4:
			h264_br_ue(&r); /* max_long_term_frame_idx_plus1 */
			break;
		case 5:
			break;
		case 6:
			h264_br_ue(&r); /* long_term_frame_idx */
			break;
		default:
			r.err = -EINVAL;
			break;
		}
	}
	return r.err;
}

/* One I/P/B field picture, POC0/1, with ordered slices and no weighting.
 * Return the number of slices, retaining the first header for mate identity.
 */
static inline int h264_parse_single_field(const u8 *buf, u32 len,
					  const struct v4l2_ctrl_h264_sps *sps,
		const struct v4l2_ctrl_h264_pps *pps,
		const struct h264_slice_refs *request, struct h264_slice_refs *out,
		struct h264_parse_scratch *scratch)
{
	struct h264_slice_refs sr;
	u32 i = 0, count = 0, previous_mb = 0, type_mask = 0;
	u32 units;
	bool uniform = false;
	int ret;

	if (!request->field_pic)
		return 0;
	if (!buf)
		return -EINVAL;
	if (sps->pic_order_cnt_type > 1 ||
	    sps->log2_max_frame_num_minus4 > 12 ||
	    sps->log2_max_pic_order_cnt_lsb_minus4 > 12 ||
	    sps->chroma_format_idc != 1 || sps->bit_depth_luma_minus8 ||
	    sps->bit_depth_chroma_minus8 ||
	    (sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) ||
	    pps->num_slice_groups_minus1 ||
	    (pps->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT) ||
	    sps->pic_width_in_mbs_minus1 > 254 ||
	    sps->pic_height_in_map_units_minus1 > 255)
		return -EOPNOTSUPP;
	units = (sps->pic_width_in_mbs_minus1 + 1) *
		(sps->pic_height_in_map_units_minus1 + 1);
	if (units > 0x7fff)
		return -EOPNOTSUPP;
	while (i + 3 <= len) {
		u32 start, end, type, type_bit;

		if (buf[i] || buf[i + 1] || buf[i + 2] != 1) {
			i++;
			continue;
		}
		start = i + 3;
		if (start == len)
			return -EINVAL;
		end = start;
		while (end < len && len - end >= 3 &&
		       (buf[end] || buf[end + 1] || buf[end + 2] != 1))
			end++;
		if (len - end < 3)
			end = len;
		i = end;
		type = buf[start] & 31;
		if (type != 1 && type != 5) {
			if (count || (type != 6 && type != 7 && type != 8 && type != 9))
				return -EOPNOTSUPP;
			continue;
		}
		ret = h264_parse_slice_refs_mode(buf + start, end - start,
						 sps, pps, true, &sr, scratch);
		if (ret)
			return ret;
		if (!sr.field_pic || !h264_same_frame_picture(&sr, request) ||
		    sr.first_mb >= units || (!count && sr.first_mb) ||
		    (count && sr.first_mb <= previous_mb) ||
		    sr.slice_type % 5 > 2 ||
		    (sr.slice_type % 5 != 2 && (sr.idr ||
		     !sr.num_ref_idx_active[0] ||
		     sr.num_ref_idx_active[0] > V4L2_H264_REF_LIST_LEN)) ||
		    (sr.slice_type % 5 == 1 &&
		     (!sr.num_ref_idx_active[1] ||
		      sr.num_ref_idx_active[1] > V4L2_H264_REF_LIST_LEN)))
			return -EOPNOTSUPP;
		type_bit = 1U << (sr.slice_type % 5);
		if ((uniform || sr.slice_type >= 5) && (type_mask & ~type_bit))
			return -EINVAL;
		uniform |= sr.slice_type >= 5;
		type_mask |= type_bit;
		ret = h264_field_marking(buf + start, end - start, &sr, scratch);
		if (ret)
			return ret;
		if (!count)
			*out = sr;
		previous_mb = sr.first_mb;
		count++;
	}
	return count ? (int)count : -EINVAL;
}

static inline int h264_synth_headers(const struct v4l2_ctrl_h264_sps *sps,
				     const struct v4l2_ctrl_h264_pps *pps,
				     const struct v4l2_ctrl_h264_scaling_matrix *sm,
				     u8 *dst, u32 cap, u32 *len)
{
	u32 sps_len, pps_len;
	int ret;

	ret = h264_synth_validate(sps, pps, sm);
	if (ret)
		return ret;
	ret = h264_synth_sps(sps, dst, cap, &sps_len);
	if (ret)
		return ret;
	ret = h264_synth_pps(sps, pps, sm, dst + sps_len, cap - sps_len,
			     &pps_len);
	if (ret)
		return ret;
	*len = sps_len + pps_len;
	return 0;
}

#endif
