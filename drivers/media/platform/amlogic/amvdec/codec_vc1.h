/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __MESON_AMVDEC_CODEC_VC1_H_
#define __MESON_AMVDEC_CODEC_VC1_H_

#include "amvdec.h"

extern struct amvdec_codec_ops meson_amvdec_codec_vc1_ops;

int meson_amvdec_codec_vc1_feed(struct amvdec_session *sess,
				struct vb2_buffer *vb);

#endif
