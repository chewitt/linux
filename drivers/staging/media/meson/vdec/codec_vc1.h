/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 */

#ifndef __MESON_VDEC_CODEC_VC1_H_
#define __MESON_VDEC_CODEC_VC1_H_

#include "vdec.h"

extern struct amvdec_codec_ops codec_vc1_ops;
u32 codec_vc1_prepare_input(struct amvdec_session *sess, struct vb2_buffer *vb);

int codec_vc1_queue_eos(struct amvdec_session *sess, u32 offset);

#endif
