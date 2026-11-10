/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AV1 sequence header and frame OBUs for the Amlogic AV1 firmware, which
 * parses the bitstream itself, rebuilt from the stateless controls.
 *
 * The headers use the syntax that needs no decoder history: explicit
 * frame sizes and reference indices, segmentation and loop-filter deltas
 * always sent in full, explicit tile sizes. Film grain is not signalled.
 */

#ifndef __MESON_AMVDEC_CODEC_AV1_SYNTH_H_
#define __MESON_AMVDEC_CODEC_AV1_SYNTH_H_

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define AV1_SYNTH_OBU_SEQUENCE_HEADER		1
#define AV1_SYNTH_OBU_TEMPORAL_DELIMITER	2
#define AV1_SYNTH_OBU_FRAME			6

#define AV1_SYNTH_PRIMARY_REF_NONE		7
#define AV1_SYNTH_SUPERRES_NUM		8
#define AV1_SYNTH_SUPERRES_DENOM_MIN		9
#define AV1_SYNTH_WARP_PREC_BITS	16

/* Bytes of each tile size field in the rebuilt tile group */
#define AV1_SYNTH_TILE_SIZE_BYTES	4

/* Room for the sequence header and the frame header of one request */
#define AV1_SYNTH_MAX_HEADERS		1024

struct av1_bw {
	u8 *buf;
	u32 size;
	u32 pos;
	bool overflow;
};

static inline void av1_put(struct av1_bw *bw, u32 bits, u32 val)
{
	while (bits--) {
		u32 byte = bw->pos >> 3;

		if (byte >= bw->size) {
			bw->overflow = true;
			return;
		}
		if (!(bw->pos & 7))
			bw->buf[byte] = 0;
		if ((val >> bits) & 1)
			bw->buf[byte] |= 0x80 >> (bw->pos & 7);
		bw->pos++;
	}
}

static inline void av1_put_flag(struct av1_bw *bw, bool flag)
{
	av1_put(bw, 1, flag);
}

/* su(n): n-bit two's complement */
static inline void av1_put_su(struct av1_bw *bw, u32 bits, s32 val)
{
	av1_put(bw, bits, (u32)val & (bits < 32 ? BIT(bits) - 1 : ~0u));
}

/* ns(n): non-symmetric unsigned value in [0, n) */
static inline void av1_put_ns(struct av1_bw *bw, u32 n, u32 v)
{
	u32 w = fls(n), m = BIT(w) - n;

	if (n <= 1)
		return;
	if (v < m) {
		av1_put(bw, w - 1, v);
	} else {
		av1_put(bw, w - 1, (v + m) >> 1);
		av1_put(bw, 1, (v + m) & 1);
	}
}

static inline void av1_byte_align(struct av1_bw *bw)
{
	while (bw->pos & 7)
		av1_put(bw, 1, 0);
}

static inline void av1_trailing_bits(struct av1_bw *bw)
{
	av1_put(bw, 1, 1);
	av1_byte_align(bw);
}

static inline u32 av1_leb128_size(u32 v)
{
	u32 n = 1;

	while (v >= 0x80) {
		v >>= 7;
		n++;
	}
	return n;
}

static inline u32 av1_put_leb128(u8 *out, u32 v)
{
	u32 n = 0;

	do {
		out[n] = v & 0x7f;
		v >>= 7;
		if (v)
			out[n] |= 0x80;
		n++;
	} while (v);
	return n;
}

static inline bool av1_seq_flag(const struct v4l2_ctrl_av1_sequence *seq, u32 flag)
{
	return !!(seq->flags & flag);
}

static inline bool av1_frame_flag(const struct v4l2_ctrl_av1_frame *f, u32 flag)
{
	return !!(f->flags & flag);
}

static inline bool av1_frame_is_intra(const struct v4l2_ctrl_av1_frame *f)
{
	return f->frame_type == V4L2_AV1_KEY_FRAME ||
	       f->frame_type == V4L2_AV1_INTRA_ONLY_FRAME;
}

static inline u32 av1_num_planes(const struct v4l2_ctrl_av1_sequence *seq)
{
	return av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_MONO_CHROME) ? 1 : 3;
}

static inline u32 av1_tile_log2(u32 blk, u32 target)
{
	u32 k = 0;

	while ((blk << k) < target)
		k++;
	return k;
}

static inline int av1_relative_dist(const struct v4l2_ctrl_av1_sequence *seq,
				    u32 a, u32 b)
{
	u32 bits = seq->order_hint_bits;
	s32 diff, m;

	if (!av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT) || !bits)
		return 0;
	diff = (s32)a - (s32)b;
	m = 1 << (bits - 1);
	return (diff & (m - 1)) - (diff & m);
}

/* Section 5.5: sequence_header_obu() payload */
static inline void av1_synth_seq(struct av1_bw *bw,
				 const struct v4l2_ctrl_av1_sequence *seq)
{
	u32 wbits = max_t(u32, fls(seq->max_frame_width_minus_1), 1);
	u32 hbits = max_t(u32, fls(seq->max_frame_height_minus_1), 1);
	bool order_hint = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT);
	bool mono = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_MONO_CHROME);
	bool ssx = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SUBSAMPLING_X);
	bool ssy = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SUBSAMPLING_Y);

	av1_put(bw, 3, seq->seq_profile);
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_STILL_PICTURE));
	av1_put(bw, 1, 0);	/* reduced_still_picture_header */
	av1_put(bw, 1, 0);	/* timing_info_present_flag */
	av1_put(bw, 1, 0);	/* initial_display_delay_present_flag */
	av1_put(bw, 5, 0);	/* operating_points_cnt_minus_1 */
	av1_put(bw, 12, 0);	/* operating_point_idc[0] */
	av1_put(bw, 5, 13);	/* seq_level_idx[0]: 5.1 */
	av1_put(bw, 1, 0);	/* seq_tier[0] */
	av1_put(bw, 4, wbits - 1);
	av1_put(bw, 4, hbits - 1);
	av1_put(bw, wbits, seq->max_frame_width_minus_1);
	av1_put(bw, hbits, seq->max_frame_height_minus_1);
	av1_put(bw, 1, 0);	/* frame_id_numbers_present_flag */
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_USE_128X128_SUPERBLOCK));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_FILTER_INTRA));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_INTRA_EDGE_FILTER));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_INTERINTRA_COMPOUND));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_MASKED_COMPOUND));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_WARPED_MOTION));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_DUAL_FILTER));
	av1_put_flag(bw, order_hint);
	if (order_hint) {
		av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_JNT_COMP));
		av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_REF_FRAME_MVS));
	}
	/* Screen content tools and integer MVs are chosen per frame. */
	av1_put(bw, 1, 1);	/* seq_choose_screen_content_tools */
	av1_put(bw, 1, 1);	/* seq_choose_integer_mv */
	if (order_hint)
		av1_put(bw, 3, seq->order_hint_bits - 1);
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_SUPERRES));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_CDEF));
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_RESTORATION));

	/* color_config() */
	av1_put_flag(bw, seq->bit_depth > 8);
	if (seq->seq_profile == 2 && seq->bit_depth > 8)
		av1_put_flag(bw, seq->bit_depth == 12);
	if (seq->seq_profile != 1)
		av1_put_flag(bw, mono);
	av1_put(bw, 1, 0);	/* color_description_present_flag */
	av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_COLOR_RANGE));
	if (!mono) {
		if (seq->seq_profile == 2 && seq->bit_depth == 12) {
			av1_put_flag(bw, ssx);
			if (ssx)
				av1_put_flag(bw, ssy);
		}
		if (ssx && ssy)
			av1_put(bw, 2, 0);	/* chroma_sample_position */
		av1_put_flag(bw, av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SEPARATE_UV_DELTA_Q));
	}

	av1_put(bw, 1, 0);	/* film_grain_params_present */
	av1_trailing_bits(bw);
}

struct av1_synth_tiles {
	u32 mi_cols;
	u32 mi_rows;
	u32 sb_cols;
	u32 sb_rows;
	u32 cols_log2;
	u32 rows_log2;
};

static inline void av1_tile_geometry(const struct v4l2_ctrl_av1_sequence *seq,
				     const struct v4l2_ctrl_av1_frame *f,
				     struct av1_synth_tiles *t)
{
	bool sb128 = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_USE_128X128_SUPERBLOCK);

	t->mi_cols = 2 * ((f->frame_width_minus_1 + 1 + 7) >> 3);
	t->mi_rows = 2 * ((f->frame_height_minus_1 + 1 + 7) >> 3);
	t->sb_cols = sb128 ? (t->mi_cols + 31) >> 5 : (t->mi_cols + 15) >> 4;
	t->sb_rows = sb128 ? (t->mi_rows + 31) >> 5 : (t->mi_rows + 15) >> 4;
}

/* Find the uniform layout that has the tiles of the control. */
static inline int av1_uniform_log2(u32 sbs, u32 min_log2, u32 max_log2,
				   u32 count, const u32 *sizes_minus_1)
{
	u32 log2, i, size, start;

	for (log2 = min_log2; log2 <= max_log2; log2++) {
		size = (sbs + BIT(log2) - 1) >> log2;
		for (i = 0, start = 0; start < sbs; i++, start += size)
			if (i >= count ||
			    sizes_minus_1[i] + 1 != min(size, sbs - start))
				break;
		if (start >= sbs && i == count)
			return log2;
		if (log2 == max_log2)
			break;
	}
	return -1;
}

/* Section 5.9.15: tile_info() */
static inline int av1_synth_tile_info(struct av1_bw *bw,
				      const struct v4l2_ctrl_av1_sequence *seq,
				      const struct v4l2_ctrl_av1_frame *f,
				      struct av1_synth_tiles *t)
{
	const struct v4l2_av1_tile_info *ti = &f->tile_info;
	bool sb128 = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_USE_128X128_SUPERBLOCK);
	u32 sb_size = sb128 ? 7 : 6;
	u32 max_w_sb = 4096 >> sb_size;
	u32 max_area_sb = (4096 * 2304) >> (2 * sb_size);
	u32 min_log2_cols, max_log2_cols, max_log2_rows, min_log2_tiles;
	u32 min_log2_rows, i, start, widest = 0, max_h_sb;
	int cols_log2 = -1, rows_log2 = -1;

	av1_tile_geometry(seq, f, t);
	if (!ti->tile_cols || !ti->tile_rows ||
	    ti->tile_cols > V4L2_AV1_MAX_TILE_COLS ||
	    ti->tile_rows > V4L2_AV1_MAX_TILE_ROWS)
		return -EINVAL;

	min_log2_cols = av1_tile_log2(max_w_sb, t->sb_cols);
	max_log2_cols = av1_tile_log2(1, min_t(u32, t->sb_cols, V4L2_AV1_MAX_TILE_COLS));
	max_log2_rows = av1_tile_log2(1, min_t(u32, t->sb_rows, V4L2_AV1_MAX_TILE_ROWS));
	min_log2_tiles = max(min_log2_cols,
			     av1_tile_log2(max_area_sb, t->sb_rows * t->sb_cols));

	if (ti->flags & V4L2_AV1_TILE_INFO_FLAG_UNIFORM_TILE_SPACING) {
		cols_log2 = av1_uniform_log2(t->sb_cols, min_log2_cols,
					     max_log2_cols, ti->tile_cols,
					     ti->width_in_sbs_minus_1);
		if (cols_log2 >= 0) {
			min_log2_rows = min_log2_tiles > cols_log2 ?
					min_log2_tiles - cols_log2 : 0;
			rows_log2 = av1_uniform_log2(t->sb_rows, min_log2_rows,
						     max_log2_rows, ti->tile_rows,
						     ti->height_in_sbs_minus_1);
		}
	}

	if (cols_log2 >= 0 && rows_log2 >= 0) {
		av1_put(bw, 1, 1);
		for (i = min_log2_cols; i < cols_log2; i++)
			av1_put(bw, 1, 1);
		if (cols_log2 < max_log2_cols)
			av1_put(bw, 1, 0);
		for (i = min_log2_rows; i < rows_log2; i++)
			av1_put(bw, 1, 1);
		if (rows_log2 < max_log2_rows)
			av1_put(bw, 1, 0);
		t->cols_log2 = cols_log2;
		t->rows_log2 = rows_log2;
	} else {
		av1_put(bw, 1, 0);
		for (i = 0, start = 0; start < t->sb_cols; i++) {
			u32 max_w = min(t->sb_cols - start, max_w_sb);
			u32 w = ti->width_in_sbs_minus_1[i] + 1;

			if (i >= ti->tile_cols || w > max_w)
				return -EINVAL;
			av1_put_ns(bw, max_w, w - 1);
			widest = max(widest, w);
			start += w;
		}
		if (i != ti->tile_cols)
			return -EINVAL;
		t->cols_log2 = av1_tile_log2(1, ti->tile_cols);
		max_area_sb = min_log2_tiles ?
			      (t->sb_rows * t->sb_cols) >> (min_log2_tiles + 1) :
			      t->sb_rows * t->sb_cols;
		max_h_sb = max_t(u32, max_area_sb / widest, 1);
		for (i = 0, start = 0; start < t->sb_rows; i++) {
			u32 max_h = min(t->sb_rows - start, max_h_sb);
			u32 h = ti->height_in_sbs_minus_1[i] + 1;

			if (i >= ti->tile_rows || h > max_h)
				return -EINVAL;
			av1_put_ns(bw, max_h, h - 1);
			start += h;
		}
		if (i != ti->tile_rows)
			return -EINVAL;
		t->rows_log2 = av1_tile_log2(1, ti->tile_rows);
	}

	if (t->cols_log2 || t->rows_log2) {
		av1_put(bw, t->rows_log2 + t->cols_log2, ti->context_update_tile_id);
		av1_put(bw, 2, AV1_SYNTH_TILE_SIZE_BYTES - 1);
	}
	return 0;
}

static inline void av1_put_delta_q(struct av1_bw *bw, s32 delta)
{
	av1_put_flag(bw, delta);
	if (delta)
		av1_put_su(bw, 7, delta);
}

static const u8 av1_seg_feature_bits[V4L2_AV1_SEG_LVL_MAX] = {
	8, 6, 6, 6, 6, 3, 0, 0
};

static const u8 av1_seg_feature_signed[V4L2_AV1_SEG_LVL_MAX] = {
	1, 1, 1, 1, 1, 0, 0, 0
};

/* Section 7.12.2: CodedLossless and AllLossless */
static inline void av1_lossless(const struct v4l2_ctrl_av1_frame *f,
				bool *coded, bool *all)
{
	const struct v4l2_av1_quantization *q = &f->quantization;
	const struct v4l2_av1_segmentation *s = &f->segmentation;
	bool seg = s->flags & V4L2_AV1_SEGMENTATION_FLAG_ENABLED;
	int i, qindex;

	*coded = true;
	for (i = 0; i < V4L2_AV1_MAX_SEGMENTS; i++) {
		qindex = q->base_q_idx;
		if (seg && (s->feature_enabled[i] &
			    V4L2_AV1_SEGMENT_FEATURE_ENABLED(V4L2_AV1_SEG_LVL_ALT_Q)))
			qindex = clamp(qindex + s->feature_data[i][V4L2_AV1_SEG_LVL_ALT_Q],
				       0, 255);
		if (qindex || q->delta_q_y_dc || q->delta_q_u_ac ||
		    q->delta_q_u_dc || q->delta_q_v_ac || q->delta_q_v_dc) {
			*coded = false;
			break;
		}
	}
	*all = *coded && f->frame_width_minus_1 + 1 == f->upscaled_width;
}

/* Section 7.20: is skip mode allowed */
static inline bool av1_skip_mode_allowed(const struct v4l2_ctrl_av1_sequence *seq,
					 const struct v4l2_ctrl_av1_frame *f)
{
	int fwd = -1, bwd = -1, fwd2 = -1, i;
	u32 fwd_hint = 0, bwd_hint = 0, fwd2_hint = 0, hint;

	if (av1_frame_is_intra(f) ||
	    !av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_REFERENCE_SELECT) ||
	    !av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT))
		return false;

	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
		hint = f->order_hints[V4L2_AV1_REF_LAST_FRAME + i];
		if (av1_relative_dist(seq, hint, f->order_hint) < 0) {
			if (fwd < 0 || av1_relative_dist(seq, hint, fwd_hint) > 0) {
				fwd = i;
				fwd_hint = hint;
			}
		} else if (av1_relative_dist(seq, hint, f->order_hint) > 0) {
			if (bwd < 0 || av1_relative_dist(seq, hint, bwd_hint) < 0) {
				bwd = i;
				bwd_hint = hint;
			}
		}
	}
	if (fwd < 0)
		return false;
	if (bwd >= 0)
		return true;
	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
		hint = f->order_hints[V4L2_AV1_REF_LAST_FRAME + i];
		if (av1_relative_dist(seq, hint, fwd_hint) < 0 &&
		    (fwd2 < 0 || av1_relative_dist(seq, hint, fwd2_hint) > 0)) {
			fwd2 = i;
			fwd2_hint = hint;
		}
	}
	return fwd2 >= 0;
}

/* Section 5.9.27/5.9.28 and their inverse: subexponential code */
static inline void av1_put_subexp(struct av1_bw *bw, u32 num_syms, u32 v)
{
	u32 i = 0, mk = 0, k = 3, b2, a;

	for (;;) {
		b2 = i ? k + i - 1 : k;
		a = BIT(b2);
		if (num_syms <= mk + 3 * a) {
			av1_put_ns(bw, num_syms - mk, v - mk);
			return;
		}
		if (v >= mk + a) {
			av1_put(bw, 1, 1);
			i++;
			mk += a;
		} else {
			av1_put(bw, 1, 0);
			av1_put(bw, b2, v - mk);
			return;
		}
	}
}

static inline u32 av1_recenter(u32 r, u32 v)
{
	if (v > 2 * r)
		return v;
	if (v >= r)
		return (v - r) << 1;
	return ((r - v) << 1) - 1;
}

static inline void av1_put_unsigned_subexp_with_ref(struct av1_bw *bw, u32 mx,
						    u32 r, u32 v)
{
	if ((r << 1) <= mx)
		av1_put_subexp(bw, mx, av1_recenter(r, v));
	else
		av1_put_subexp(bw, mx, av1_recenter(mx - 1 - r, mx - 1 - v));
}

static inline void av1_put_global_param(struct av1_bw *bw,
					const struct v4l2_ctrl_av1_frame *f,
					u32 type, s32 prev, s32 val, u32 idx)
{
	bool hp = av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_ALLOW_HIGH_PRECISION_MV);
	u32 abs_bits = 12, prec_bits = 15, prec_diff;
	s32 round, sub, mx, r, d;

	if (idx < 2) {
		if (type == V4L2_AV1_WARP_MODEL_TRANSLATION) {
			abs_bits = 9 - !hp;
			prec_bits = 3 - !hp;
		} else {
			abs_bits = 12;
			prec_bits = 6;
		}
	}
	prec_diff = AV1_SYNTH_WARP_PREC_BITS - prec_bits;
	round = (idx % 3) == 2 ? BIT(AV1_SYNTH_WARP_PREC_BITS) : 0;
	sub = (idx % 3) == 2 ? BIT(prec_bits) : 0;
	mx = BIT(abs_bits);
	r = (prev >> prec_diff) - sub;
	d = (val - round) >> prec_diff;
	d = clamp(d, -mx, mx);
	r = clamp(r, -mx, mx);
	av1_put_unsigned_subexp_with_ref(bw, 2 * mx + 1, r + mx, d + mx);
}

/* Section 5.9.24: global_motion_params() */
static inline void av1_synth_global_motion(struct av1_bw *bw,
					   const struct v4l2_ctrl_av1_frame *f,
					   const s32 (*prev)[6])
{
	static const s32 def[6] = { 0, 0, BIT(16), 0, 0, BIT(16) };
	const struct v4l2_av1_global_motion *gm = &f->global_motion;
	int ref, i;

	for (ref = V4L2_AV1_REF_LAST_FRAME; ref <= V4L2_AV1_REF_ALTREF_FRAME; ref++) {
		u32 type = gm->type[ref];
		const s32 *p = prev ? prev[ref] : def;

		av1_put_flag(bw, type != V4L2_AV1_WARP_MODEL_IDENTITY);
		if (type == V4L2_AV1_WARP_MODEL_IDENTITY)
			continue;
		av1_put_flag(bw, type == V4L2_AV1_WARP_MODEL_ROTZOOM);
		if (type != V4L2_AV1_WARP_MODEL_ROTZOOM)
			av1_put_flag(bw, type == V4L2_AV1_WARP_MODEL_TRANSLATION);
		if (type >= V4L2_AV1_WARP_MODEL_ROTZOOM) {
			av1_put_global_param(bw, f, type, p[2], gm->params[ref][2], 2);
			av1_put_global_param(bw, f, type, p[3], gm->params[ref][3], 3);
			if (type == V4L2_AV1_WARP_MODEL_AFFINE) {
				av1_put_global_param(bw, f, type, p[4],
						     gm->params[ref][4], 4);
				av1_put_global_param(bw, f, type, p[5],
						     gm->params[ref][5], 5);
			}
		}
		for (i = 0; i < 2; i++)
			av1_put_global_param(bw, f, type, p[i], gm->params[ref][i], i);
	}
}

static inline void av1_synth_frame_size(struct av1_bw *bw,
					const struct v4l2_ctrl_av1_sequence *seq,
					const struct v4l2_ctrl_av1_frame *f)
{
	u32 wbits = max_t(u32, fls(seq->max_frame_width_minus_1), 1);
	u32 hbits = max_t(u32, fls(seq->max_frame_height_minus_1), 1);
	bool superres = av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_USE_SUPERRES);
	bool render;

	/* frame_size_override_flag is always set */
	av1_put(bw, wbits, f->upscaled_width - 1);
	av1_put(bw, hbits, f->frame_height_minus_1);
	if (av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_SUPERRES)) {
		av1_put_flag(bw, superres);
		if (superres)
			av1_put(bw, 3, f->superres_denom - AV1_SYNTH_SUPERRES_DENOM_MIN);
	}

	/* render_size() */
	render = f->render_width_minus_1 + 1 != f->upscaled_width ||
		 f->render_height_minus_1 != f->frame_height_minus_1;
	av1_put_flag(bw, render);
	if (render) {
		av1_put(bw, 16, f->render_width_minus_1);
		av1_put(bw, 16, f->render_height_minus_1);
	}
}

/*
 * Section 5.9.2: uncompressed_header(), without the byte alignment or the
 * trailing bits that follow it. @prev_gm holds the global motion parameters
 * saved with the primary reference frame, or NULL for the defaults;
 * @slot_hints holds the order hint of the frame in each reference slot.
 */
static inline int av1_synth_frame_header(struct av1_bw *bw,
					 const struct v4l2_ctrl_av1_sequence *seq,
					 const struct v4l2_ctrl_av1_frame *f,
					 const s32 (*prev_gm)[6],
					 const u32 *slot_hints,
					 struct av1_synth_tiles *tiles)
{
	const struct v4l2_av1_quantization *q = &f->quantization;
	const struct v4l2_av1_segmentation *s = &f->segmentation;
	const struct v4l2_av1_loop_filter *lf = &f->loop_filter;
	const struct v4l2_av1_cdef *cdef = &f->cdef;
	const struct v4l2_av1_loop_restoration *lr = &f->loop_restoration;
	static const u8 lr_code[4] = { 0, 2, 3, 1 };
	bool intra = av1_frame_is_intra(f);
	bool show = av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_SHOW_FRAME);
	bool order_hint = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT);
	bool sb128 = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_USE_128X128_SUPERBLOCK);
	bool er = av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_ERROR_RESILIENT_MODE);
	bool sct = av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_ALLOW_SCREEN_CONTENT_TOOLS);
	bool intrabc = false, coded_lossless, all_lossless, uses_lr = false;
	bool uses_chroma_lr = false, delta_q, seg_on;
	u32 planes = av1_num_planes(seq);
	u32 refresh = f->refresh_frame_flags;
	u32 primary = f->primary_ref_frame;
	int i, j, ret;

	if (f->frame_type == V4L2_AV1_SWITCH_FRAME ||
	    (f->frame_type == V4L2_AV1_KEY_FRAME && show)) {
		er = true;
		refresh = 0xff;
	}
	if (intra || er)
		primary = AV1_SYNTH_PRIMARY_REF_NONE;
	if (primary == AV1_SYNTH_PRIMARY_REF_NONE)
		prev_gm = NULL;

	av1_put(bw, 1, 0);	/* show_existing_frame */
	av1_put(bw, 2, f->frame_type);
	av1_put_flag(bw, show);
	if (!show)
		av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_SHOWABLE_FRAME));
	if (!(f->frame_type == V4L2_AV1_SWITCH_FRAME ||
	      (f->frame_type == V4L2_AV1_KEY_FRAME && show)))
		av1_put_flag(bw, er);
	av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_DISABLE_CDF_UPDATE));
	av1_put_flag(bw, sct);	/* seq_force_screen_content_tools is SELECT */
	if (sct)
		av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_FORCE_INTEGER_MV));
	if (f->frame_type != V4L2_AV1_SWITCH_FRAME)
		av1_put(bw, 1, 1);	/* frame_size_override_flag */
	if (order_hint)
		av1_put(bw, seq->order_hint_bits, f->order_hint);
	if (!(intra || er))
		av1_put(bw, 3, primary);
	if (!(f->frame_type == V4L2_AV1_SWITCH_FRAME ||
	      (f->frame_type == V4L2_AV1_KEY_FRAME && show)))
		av1_put(bw, 8, refresh);
	if ((!intra || refresh != 0xff) && er && order_hint)
		for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++)
			av1_put(bw, seq->order_hint_bits, slot_hints[i]);

	if (intra) {
		av1_synth_frame_size(bw, seq, f);
		if (sct && f->upscaled_width == f->frame_width_minus_1 + 1) {
			intrabc = av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_ALLOW_INTRABC);
			av1_put_flag(bw, intrabc);
		}
	} else {
		if (order_hint)
			av1_put(bw, 1, 0);	/* frame_refs_short_signaling */
		for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
			if (f->ref_frame_idx[i] < 0 || f->ref_frame_idx[i] > 7)
				return -EINVAL;
			av1_put(bw, 3, f->ref_frame_idx[i]);
		}
		if (!er)
			for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++)
				av1_put(bw, 1, 0);	/* found_ref */
		av1_synth_frame_size(bw, seq, f);
		if (!av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_FORCE_INTEGER_MV))
			av1_put_flag(bw, av1_frame_flag(f,
							V4L2_AV1_FRAME_FLAG_ALLOW_HIGH_PRECISION_MV));
		av1_put_flag(bw, f->interpolation_filter ==
				 V4L2_AV1_INTERPOLATION_FILTER_SWITCHABLE);
		if (f->interpolation_filter != V4L2_AV1_INTERPOLATION_FILTER_SWITCHABLE)
			av1_put(bw, 2, f->interpolation_filter);
		av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_IS_MOTION_MODE_SWITCHABLE));
		if (!er && av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_REF_FRAME_MVS))
			av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_USE_REF_FRAME_MVS));
	}
	if (!av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_DISABLE_CDF_UPDATE))
		av1_put_flag(bw, av1_frame_flag(f,
						V4L2_AV1_FRAME_FLAG_DISABLE_FRAME_END_UPDATE_CDF));

	ret = av1_synth_tile_info(bw, seq, f, tiles);
	if (ret)
		return ret;

	/* quantization_params() */
	av1_put(bw, 8, q->base_q_idx);
	av1_put_delta_q(bw, q->delta_q_y_dc);
	if (planes > 1) {
		bool diff = q->flags & V4L2_AV1_QUANTIZATION_FLAG_DIFF_UV_DELTA;
		bool sep = av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SEPARATE_UV_DELTA_Q);

		if (sep)
			av1_put_flag(bw, diff);
		av1_put_delta_q(bw, q->delta_q_u_dc);
		av1_put_delta_q(bw, q->delta_q_u_ac);
		if (sep && diff) {
			av1_put_delta_q(bw, q->delta_q_v_dc);
			av1_put_delta_q(bw, q->delta_q_v_ac);
		}
	}
	av1_put_flag(bw, q->flags & V4L2_AV1_QUANTIZATION_FLAG_USING_QMATRIX);
	if (q->flags & V4L2_AV1_QUANTIZATION_FLAG_USING_QMATRIX) {
		av1_put(bw, 4, q->qm_y);
		av1_put(bw, 4, q->qm_u);
		if (av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SEPARATE_UV_DELTA_Q))
			av1_put(bw, 4, q->qm_v);
	}

	/* segmentation_params(): the data is always sent */
	seg_on = s->flags & V4L2_AV1_SEGMENTATION_FLAG_ENABLED;
	av1_put_flag(bw, seg_on);
	if (seg_on) {
		if (primary != AV1_SYNTH_PRIMARY_REF_NONE) {
			bool map = s->flags & V4L2_AV1_SEGMENTATION_FLAG_UPDATE_MAP;

			av1_put_flag(bw, map);
			if (map)
				av1_put_flag(bw, s->flags &
					     V4L2_AV1_SEGMENTATION_FLAG_TEMPORAL_UPDATE);
			av1_put(bw, 1, 1);	/* segmentation_update_data */
		}
		for (i = 0; i < V4L2_AV1_MAX_SEGMENTS; i++) {
			for (j = 0; j < V4L2_AV1_SEG_LVL_MAX; j++) {
				bool on = s->feature_enabled[i] &
					  V4L2_AV1_SEGMENT_FEATURE_ENABLED(j);

				av1_put_flag(bw, on);
				if (!on)
					continue;
				if (av1_seg_feature_signed[j])
					av1_put_su(bw, 1 + av1_seg_feature_bits[j],
						   s->feature_data[i][j]);
				else
					av1_put(bw, av1_seg_feature_bits[j],
						s->feature_data[i][j]);
			}
		}
	}

	/* delta_q_params() and delta_lf_params() */
	delta_q = q->base_q_idx &&
		  (q->flags & V4L2_AV1_QUANTIZATION_FLAG_DELTA_Q_PRESENT);
	if (q->base_q_idx) {
		av1_put_flag(bw, delta_q);
		if (delta_q)
			av1_put(bw, 2, q->delta_q_res);
	}
	if (delta_q && !intrabc) {
		bool present = lf->flags & V4L2_AV1_LOOP_FILTER_FLAG_DELTA_LF_PRESENT;

		av1_put_flag(bw, present);
		if (present) {
			av1_put(bw, 2, lf->delta_lf_res);
			av1_put_flag(bw, lf->flags & V4L2_AV1_LOOP_FILTER_FLAG_DELTA_LF_MULTI);
		}
	}

	av1_lossless(f, &coded_lossless, &all_lossless);

	/* loop_filter_params(): the deltas are always sent */
	if (!coded_lossless && !intrabc) {
		av1_put(bw, 6, lf->level[0]);
		av1_put(bw, 6, lf->level[1]);
		if (planes > 1 && (lf->level[0] || lf->level[1])) {
			av1_put(bw, 6, lf->level[2]);
			av1_put(bw, 6, lf->level[3]);
		}
		av1_put(bw, 3, lf->sharpness);
		av1_put_flag(bw, lf->flags & V4L2_AV1_LOOP_FILTER_FLAG_DELTA_ENABLED);
		if (lf->flags & V4L2_AV1_LOOP_FILTER_FLAG_DELTA_ENABLED) {
			av1_put(bw, 1, 1);	/* loop_filter_delta_update */
			for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++) {
				av1_put(bw, 1, 1);
				av1_put_su(bw, 7, lf->ref_deltas[i]);
			}
			for (i = 0; i < 2; i++) {
				av1_put(bw, 1, 1);
				av1_put_su(bw, 7, lf->mode_deltas[i]);
			}
		}
	}

	/* cdef_params() */
	if (!coded_lossless && !intrabc &&
	    av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_CDEF)) {
		av1_put(bw, 2, cdef->damping_minus_3);
		av1_put(bw, 2, cdef->bits);
		for (i = 0; i < BIT(cdef->bits); i++) {
			av1_put(bw, 4, cdef->y_pri_strength[i]);
			av1_put(bw, 2, cdef->y_sec_strength[i]);
			if (planes > 1) {
				av1_put(bw, 4, cdef->uv_pri_strength[i]);
				av1_put(bw, 2, cdef->uv_sec_strength[i]);
			}
		}
	}

	/* lr_params() */
	if (!all_lossless && !intrabc &&
	    av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_RESTORATION)) {
		for (i = 0; i < planes; i++) {
			u32 type = lr->frame_restoration_type[i] & 3;

			av1_put(bw, 2, lr_code[type]);
			if (type != V4L2_AV1_FRAME_RESTORE_NONE) {
				uses_lr = true;
				if (i)
					uses_chroma_lr = true;
			}
		}
		if (uses_lr) {
			if (sb128) {
				av1_put(bw, 1, lr->lr_unit_shift - 1);
			} else {
				av1_put_flag(bw, lr->lr_unit_shift);
				if (lr->lr_unit_shift)
					av1_put(bw, 1, lr->lr_unit_shift - 1);
			}
			if (av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SUBSAMPLING_X) &&
			    av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_SUBSAMPLING_Y) &&
			    uses_chroma_lr)
				av1_put(bw, 1, lr->lr_uv_shift);
		}
	}

	/* read_tx_mode() */
	if (!coded_lossless)
		av1_put_flag(bw, f->tx_mode == V4L2_AV1_TX_MODE_SELECT);

	/* frame_reference_mode() */
	if (!intra)
		av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_REFERENCE_SELECT));

	/* skip_mode_params() */
	if (av1_skip_mode_allowed(seq, f))
		av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_SKIP_MODE_PRESENT));

	if (!intra && !er &&
	    av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_WARPED_MOTION))
		av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_ALLOW_WARPED_MOTION));
	av1_put_flag(bw, av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_REDUCED_TX_SET));

	if (!intra)
		av1_synth_global_motion(bw, f, prev_gm);

	/* film_grain_params(): not present in the sequence */

	return bw->overflow ? -ENOSPC : 0;
}

/* OBU header and size field, then the payload is copied by the caller */
static inline u32 av1_obu_header(u8 *out, u32 type, u32 payload)
{
	out[0] = (type << 3) | BIT(1);
	return 1 + av1_put_leb128(out + 1, payload);
}

/* Amlogic framing ahead of each OBU of @obu_len bytes */
#define AV1_AMLV_PREFIX_SIZE	20

static inline void av1_amlv_prefix(u8 *out, u32 obu_len)
{
	u32 size = obu_len + 4;

	out[0] = (size + 4) >> 24;
	out[1] = (size + 4) >> 16;
	out[2] = (size + 4) >> 8;
	out[3] = size + 4;
	out[4] = out[0] ^ 0xff;
	out[5] = out[1] ^ 0xff;
	out[6] = out[2] ^ 0xff;
	out[7] = out[3] ^ 0xff;
	out[8] = 0;
	out[9] = 0;
	out[10] = 0;
	out[11] = 1;
	out[12] = 'A';
	out[13] = 'M';
	out[14] = 'L';
	out[15] = 'V';
	out[16] = (size & 0x7f) | 0x80;
	out[17] = ((size >> 7) & 0x7f) | 0x80;
	out[18] = ((size >> 14) & 0x7f) | 0x80;
	out[19] = (size >> 21) & 0x7f;
}

#endif
