/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip video decoder VP9 common functions
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *	Boris Brezillon <boris.brezillon@collabora.com>
 * Copyright (C) 2021 Collabora, Ltd.
 *	Andrzej Pietrasiewicz <andrzej.p@collabora.com>
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 */

#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-vp9.h>

#include "rkvdec.h"

struct rkvdec_vp9_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_vp9_frame *decode_params;
};

struct rkvdec_vp9_intra_mode_probs {
	u8 y_mode[105];
	u8 uv_mode[23];
};

struct rkvdec_vp9_intra_only_frame_probs {
	u8 coef_intra[4][2][128];
	struct rkvdec_vp9_intra_mode_probs intra_mode[10];
};

struct rkvdec_vp9_inter_frame_probs {
	u8 y_mode[4][9];
	u8 comp_mode[5];
	u8 comp_ref[5];
	u8 single_ref[5][2];
	u8 inter_mode[7][3];
	u8 interp_filter[4][2];
	u8 padding0[11];
	u8 coef[2][4][2][128];
	u8 uv_mode_0_2[3][9];
	u8 padding1[5];
	u8 uv_mode_3_5[3][9];
	u8 padding2[5];
	u8 uv_mode_6_8[3][9];
	u8 padding3[5];
	u8 uv_mode_9[9];
	u8 padding4[7];
	u8 padding5[16];
	struct {
		u8 joint[3];
		u8 sign[2];
		u8 classes[2][10];
		u8 class0_bit[2];
		u8 bits[2][10];
		u8 class0_fr[2][2][3];
		u8 fr[2][3];
		u8 class0_hp[2];
		u8 hp[2];
		/* 32 bit alignment */
		u8 padding6[3];
	} mv;
};

struct rkvdec_vp9_probs {
	u8 partition[16][3];
	u8 pred[3];
	u8 tree[7];
	u8 skip[3];
	u8 tx32[2][3];
	u8 tx16[2][2];
	u8 tx8[2][1];
	u8 is_inter[4];
	/* 128 bit alignment */
	u8 padding0[3];
	union {
		struct rkvdec_vp9_inter_frame_probs inter;
		struct rkvdec_vp9_intra_only_frame_probs intra_only;
	};
	/* 128 bit alignment */
	u8 padding1[8];
};

void write_coeff_plane(const u8 coef[6][6][3], u8 *coeff_plane);

struct rkvdec_decoded_buffer *
get_ref_buf_vp9(struct rkvdec_ctx *ctx, struct vb2_v4l2_buffer *dst, u64 timestamp);

void update_dec_buf_info(struct rkvdec_decoded_buffer *buf,
			 const struct v4l2_ctrl_vp9_frame *dec_params);
