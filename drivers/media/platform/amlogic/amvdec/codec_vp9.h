/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * VP9 request decoding.
 */

#ifndef __MESON_AMVDEC_CODEC_VP9_H_
#define __MESON_AMVDEC_CODEC_VP9_H_

#include "amvdec.h"

void meson_amvdec_codec_vp9_request_input_ready(struct amvdec_session *sess, u32 end);
extern struct amvdec_codec_ops meson_amvdec_codec_vp9_ops;

void meson_amvdec_codec_vp9_arm_restream(struct amvdec_session *sess);
bool meson_amvdec_codec_vp9_restream_pending(struct amvdec_session *sess);
int meson_amvdec_codec_vp9_restream_reinit(struct amvdec_session *sess);

#endif
