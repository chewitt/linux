/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __MESON_AMVDEC_CODEC_AV1_H_
#define __MESON_AMVDEC_CODEC_AV1_H_

#include "amvdec.h"

extern struct amvdec_codec_ops meson_amvdec_codec_av1_ops;

int meson_amvdec_codec_av1_prepare(struct amvdec_session *sess,
				   const struct v4l2_ctrl_av1_sequence *seq,
				   const struct v4l2_ctrl_av1_frame *f,
				   const struct v4l2_ctrl_av1_tile_group_entry *tg,
				   u32 num_tiles, u8 *data, u32 *payload,
				   u32 plane_size);
void meson_amvdec_codec_av1_request_input_ready(struct amvdec_session *sess,
						u32 end);
void meson_amvdec_codec_av1_arm_restream(struct amvdec_session *sess);
bool meson_amvdec_codec_av1_restream_pending(struct amvdec_session *sess);
int meson_amvdec_codec_av1_restream_reinit(struct amvdec_session *sess);

#endif
