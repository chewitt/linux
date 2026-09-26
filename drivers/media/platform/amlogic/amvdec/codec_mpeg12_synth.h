/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * MPEG-2 header synthesis from request controls.
 */

#ifndef __MESON_AMVDEC_CODEC_MPEG12_SYNTH_H_
#define __MESON_AMVDEC_CODEC_MPEG12_SYNTH_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>

/* Worst case: seq hdr + 2x64 matrices + seq ext + pic hdr + pic coding ext. */
#define MPEG12_SYNTH_MAX_HEADERS	192

struct mpeg12_bw {
	u8	*buf;
	u32	cap;
	u32	byte;
	u8	bit;		/* next bit to write, 7..0 */
};

static inline void mpeg12_bw_init(struct mpeg12_bw *bw, u8 *buf, u32 cap)
{
	bw->buf = buf;
	bw->cap = cap;
	bw->byte = 0;
	bw->bit = 7;
	if (cap)
		buf[0] = 0;
}

static inline void mpeg12_bw_bit(struct mpeg12_bw *bw, u32 v)
{
	if (bw->byte >= bw->cap)
		return;
	if (v & 1)
		bw->buf[bw->byte] |= 1 << bw->bit;
	if (bw->bit-- == 0) {
		bw->bit = 7;
		if (++bw->byte < bw->cap)
			bw->buf[bw->byte] = 0;
	}
}

static inline void mpeg12_bw_u(struct mpeg12_bw *bw, u32 val, u32 bits)
{
	while (bits--)
		mpeg12_bw_bit(bw, (val >> bits) & 1);
}

/* Pad with zero bits to the next byte boundary. */
static inline void mpeg12_bw_align(struct mpeg12_bw *bw)
{
	while (bw->bit != 7)
		mpeg12_bw_bit(bw, 0);
}

static inline u32 mpeg12_bw_bytes(const struct mpeg12_bw *bw)
{
	return bw->bit == 7 ? bw->byte : bw->byte + 1;
}

static inline void mpeg12_bw_startcode(struct mpeg12_bw *bw, u8 code)
{
	mpeg12_bw_align(bw);
	mpeg12_bw_u(bw, 0x000001, 24);
	mpeg12_bw_u(bw, code, 8);
}

/*
 * Supply display metadata absent from request controls so firmware can
 * parse the synthesized headers.
 */
#define MPEG12_ASPECT_SQUARE	1
#define MPEG12_FRAME_RATE_CODE	4	/* 30000/1001 */
#define MPEG12_BIT_RATE_MAX	0x3ffff
#define MPEG12_VBV_DELAY_UNUSED	0xffff

static inline int
mpeg12_sequence_validate(const struct v4l2_ctrl_mpeg2_sequence *seq)
{
	if (!seq)
		return -EINVAL;
	if (!seq->horizontal_size || !seq->vertical_size)
		return -EINVAL;
	/* Require 4:2:0 chroma for the supported capture formats. */
	if (seq->chroma_format != 1)
		return -EINVAL;
	return 0;
}

static inline int
mpeg12_picture_validate(const struct v4l2_ctrl_mpeg2_picture *pic)
{
	if (!pic)
		return -EINVAL;
	if (pic->picture_coding_type < V4L2_MPEG2_PIC_CODING_TYPE_I ||
	    pic->picture_coding_type > V4L2_MPEG2_PIC_CODING_TYPE_B)
		return -EINVAL;
	if (pic->picture_structure < V4L2_MPEG2_PIC_TOP_FIELD ||
	    pic->picture_structure > V4L2_MPEG2_PIC_FRAME)
		return -EINVAL;
	return 0;
}

static inline int
mpeg12_synth_validate(const struct v4l2_ctrl_mpeg2_sequence *seq,
		      const struct v4l2_ctrl_mpeg2_picture *pic)
{
	int ret;

	ret = mpeg12_sequence_validate(seq);
	if (ret)
		return ret;
	return mpeg12_picture_validate(pic);
}

/**
 * mpeg12_synth_headers() - rebuild the headers a slice needs
 * @seq:  sequence control
 * @pic:  picture control
 * @quant: quantisation control, may be NULL (matrices then defaulted by the ucode)
 * @temporal_ref: picture counter, only used for display ordering metadata
 * @with_sequence: emit the sequence header and extension (first picture / after
 *                 a resolution change); pictures in between need only the
 *                 picture header and its extension
 * @buf: output
 * @cap: output capacity
 *
 * Returns the number of bytes written, or a negative errno.
 */
static inline int
mpeg12_synth_headers(const struct v4l2_ctrl_mpeg2_sequence *seq,
		     const struct v4l2_ctrl_mpeg2_picture *pic,
		     const struct v4l2_ctrl_mpeg2_quantisation *quant,
		     u16 temporal_ref, bool with_sequence,
		     u8 *buf, u32 cap)
{
	struct mpeg12_bw bw;
	int ret, i;

	ret = mpeg12_synth_validate(seq, pic);
	if (ret)
		return ret;
	if (cap < MPEG12_SYNTH_MAX_HEADERS)
		return -ENOSPC;

	mpeg12_bw_init(&bw, buf, cap);

	if (with_sequence) {
		/* sequence_header() */
		mpeg12_bw_startcode(&bw, 0xb3);
		mpeg12_bw_u(&bw, seq->horizontal_size & 0xfff, 12);
		mpeg12_bw_u(&bw, seq->vertical_size & 0xfff, 12);
		mpeg12_bw_u(&bw, MPEG12_ASPECT_SQUARE, 4);
		mpeg12_bw_u(&bw, MPEG12_FRAME_RATE_CODE, 4);
		mpeg12_bw_u(&bw, MPEG12_BIT_RATE_MAX, 18);
		mpeg12_bw_u(&bw, 1, 1);				/* marker_bit */
		mpeg12_bw_u(&bw, seq->vbv_buffer_size & 0x3ff, 10);
		mpeg12_bw_u(&bw, 0, 1);				/* constrained */

		if (quant) {
			mpeg12_bw_u(&bw, 1, 1);
			for (i = 0; i < 64; i++)
				mpeg12_bw_u(&bw, quant->intra_quantiser_matrix[i], 8);
			mpeg12_bw_u(&bw, 1, 1);
			for (i = 0; i < 64; i++)
				mpeg12_bw_u(&bw, quant->non_intra_quantiser_matrix[i], 8);
		} else {
			mpeg12_bw_u(&bw, 0, 1);
			mpeg12_bw_u(&bw, 0, 1);
		}

		/* sequence_extension() */
		mpeg12_bw_startcode(&bw, 0xb5);
		mpeg12_bw_u(&bw, 0x1, 4);			/* ext id */
		mpeg12_bw_u(&bw, seq->profile_and_level_indication, 8);
		mpeg12_bw_u(&bw, !!(seq->flags & V4L2_MPEG2_SEQ_FLAG_PROGRESSIVE), 1);
		mpeg12_bw_u(&bw, seq->chroma_format, 2);
		mpeg12_bw_u(&bw, (seq->horizontal_size >> 12) & 0x3, 2);
		mpeg12_bw_u(&bw, (seq->vertical_size >> 12) & 0x3, 2);
		mpeg12_bw_u(&bw, 0, 12);			/* bit_rate_ext */
		mpeg12_bw_u(&bw, 1, 1);				/* marker_bit */
		mpeg12_bw_u(&bw, (seq->vbv_buffer_size >> 10) & 0xff, 8);
		mpeg12_bw_u(&bw, 0, 1);				/* low_delay */
		mpeg12_bw_u(&bw, 0, 2);				/* frame_rate_ext_n */
		mpeg12_bw_u(&bw, 0, 5);				/* frame_rate_ext_d */
	}

	/* picture_header() */
	mpeg12_bw_startcode(&bw, 0x00);
	mpeg12_bw_u(&bw, temporal_ref & 0x3ff, 10);
	mpeg12_bw_u(&bw, pic->picture_coding_type, 3);
	mpeg12_bw_u(&bw, MPEG12_VBV_DELAY_UNUSED, 16);
	if (pic->picture_coding_type == V4L2_MPEG2_PIC_CODING_TYPE_P ||
	    pic->picture_coding_type == V4L2_MPEG2_PIC_CODING_TYPE_B) {
		mpeg12_bw_u(&bw, 0, 1);				/* full_pel_fwd */
		mpeg12_bw_u(&bw, 7, 3);				/* fwd_f_code, MPEG-2: 7 */
	}
	if (pic->picture_coding_type == V4L2_MPEG2_PIC_CODING_TYPE_B) {
		mpeg12_bw_u(&bw, 0, 1);				/* full_pel_bwd */
		mpeg12_bw_u(&bw, 7, 3);				/* bwd_f_code */
	}
	mpeg12_bw_u(&bw, 0, 1);					/* extra_bit_picture */

	/* picture_coding_extension() */
	mpeg12_bw_startcode(&bw, 0xb5);
	mpeg12_bw_u(&bw, 0x8, 4);				/* ext id */
	mpeg12_bw_u(&bw, pic->f_code[0][0], 4);
	mpeg12_bw_u(&bw, pic->f_code[0][1], 4);
	mpeg12_bw_u(&bw, pic->f_code[1][0], 4);
	mpeg12_bw_u(&bw, pic->f_code[1][1], 4);
	mpeg12_bw_u(&bw, pic->intra_dc_precision, 2);
	mpeg12_bw_u(&bw, pic->picture_structure, 2);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_TOP_FIELD_FIRST), 1);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_FRAME_PRED_DCT), 1);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_CONCEALMENT_MV), 1);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_Q_SCALE_TYPE), 1);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_INTRA_VLC), 1);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_ALT_SCAN), 1);
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_REPEAT_FIRST), 1);
	mpeg12_bw_u(&bw, 0, 1);					/* chroma_420_type */
	mpeg12_bw_u(&bw, !!(pic->flags & V4L2_MPEG2_PIC_FLAG_PROGRESSIVE), 1);
	mpeg12_bw_u(&bw, 0, 1);					/* composite_display */

	mpeg12_bw_align(&bw);

	if (bw.byte >= bw.cap)
		return -ENOSPC;

	return mpeg12_bw_bytes(&bw);
}

#endif
