/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip video decoder VP9 common functions
 *
 * Copyright (C) 2025 Venkata Atchuta Bheemeswara Sarma Darbha <vdarbha0473@gmail.com>
 *
 */

#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"

struct rkvdec_vp9_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_vp9_frame *decode_params;
};

void write_coeff_plane(const u8 coef[6][6][3], u8 *coeff_plane);

struct rkvdec_decoded_buffer *get_ref_buf_vp9(struct rkvdec_ctx *ctx,
					      struct vb2_v4l2_buffer *dst,
					      u64 timestamp);

dma_addr_t get_mv_base_addr(struct rkvdec_decoded_buffer *buf);

void update_dec_buf_info(struct rkvdec_decoded_buffer *buf,
			 const struct v4l2_ctrl_vp9_frame *dec_params);
