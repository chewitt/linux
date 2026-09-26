/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * H.264 request decoding with multi-instance firmware.
 */

#ifndef __MESON_AMVDEC_CODEC_H264_H_
#define __MESON_AMVDEC_CODEC_H264_H_

#include "codec_h264_synth.h"
#include "amvdec.h"

/* H.264 request decoder with direct OUTPUT input. */
extern struct amvdec_codec_ops meson_amvdec_codec_h264_multi_ops;

/* True when this session decodes on the multi-instance firmware. */
static inline bool codec_h264_direct_input(struct amvdec_session *sess)
{
	return sess->fmt_out->pixfmt == V4L2_PIX_FMT_H264_SLICE &&
	       sess->fmt_out->codec_ops->direct_input;
}

bool meson_amvdec_codec_h264_multi_busy(struct amvdec_session *sess);

void meson_amvdec_codec_h264_multi_finish_picture(struct amvdec_session *sess);

void meson_amvdec_codec_h264_multi_discard_capture(struct amvdec_session *sess, u32 vb2_idx);

void meson_amvdec_codec_h264_multi_hold_src(struct amvdec_session *sess,
			       struct vb2_v4l2_buffer *vbuf);

const u8 *meson_amvdec_codec_h264_scan_copy(struct amvdec_session *sess, const u8 *src,
			       u32 len, u32 cap);
int meson_amvdec_codec_h264_multi_feed_buffer(struct amvdec_session *sess,
				 struct vb2_buffer *vb);

void meson_amvdec_codec_h264_multi_set_params(struct amvdec_session *sess,
				 const struct v4l2_ctrl_h264_sps *sps,
				 const struct v4l2_ctrl_h264_pps *pps,
				 const struct v4l2_ctrl_h264_decode_params *dec);

/*
 * Reference list modification for the picture being fed.  Frame-based mode
 * carries no per-slice controls, so the feed path reads these out of the
 * slice header and the list builder applies them.
 */
void meson_amvdec_codec_h264_multi_set_slice_refs(struct amvdec_session *sess,
				     const struct h264_slice_refs *sr);

/* Prepare in-band headers; HEADER_DONE commits the firmware cache. */
int meson_amvdec_codec_h264_multi_headers(struct amvdec_session *sess,
			     const struct v4l2_ctrl_h264_sps *sps,
			    const struct v4l2_ctrl_h264_pps *pps,
			    const struct v4l2_ctrl_h264_scaling_matrix *sm,
			    bool force, u8 *dst, u32 cap, u32 *len);

#endif
