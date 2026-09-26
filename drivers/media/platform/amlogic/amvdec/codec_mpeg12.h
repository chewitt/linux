/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * MPEG-2 request decoding with multi-instance firmware.
 */

#ifndef __MESON_AMVDEC_CODEC_MPEG12_H_
#define __MESON_AMVDEC_CODEC_MPEG12_H_

#include "amvdec.h"

extern struct amvdec_codec_ops meson_amvdec_codec_mpeg12_sl_ops;

int meson_amvdec_codec_mpeg12_sl_feed(struct amvdec_session *sess, struct vb2_buffer *vb,
			 const struct v4l2_ctrl_mpeg2_picture *pic,
			 u16 temporal_ref);
bool meson_amvdec_codec_mpeg12_sl_second_field(struct amvdec_session *sess,
				  const struct v4l2_ctrl_mpeg2_picture *pic,
				  u16 *temporal_ref);

#endif
