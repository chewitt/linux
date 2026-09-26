/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * HEVC request decoding.
 */

#ifndef __MESON_AMVDEC_CODEC_HEVC_H_
#define __MESON_AMVDEC_CODEC_HEVC_H_

#include "amvdec.h"

extern struct amvdec_codec_ops meson_amvdec_codec_hevc_ops;

void meson_amvdec_codec_hevc_request_input_ready(struct amvdec_session *sess, u64 ts, u32 end);
void meson_amvdec_codec_hevc_workspace_release(void);

void meson_amvdec_codec_hevc_arm_restream(struct amvdec_session *sess);
bool meson_amvdec_codec_hevc_restream_pending(struct amvdec_session *sess);
int meson_amvdec_codec_hevc_restream_reinit(struct amvdec_session *sess);

#endif
