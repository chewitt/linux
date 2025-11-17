// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip video decoder VP9 common functions
 *
 * Copyright (C) 2025 Venkata Atchuta Bheemeswara Sarma Darbha <vdarbha0473@gmail.com>
 */
#include <linux/v4l2-common.h>
#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-vp9-common.h"

void write_coeff_plane(const u8 coef[6][6][3], u8 *coeff_plane)
{
	unsigned int idx = 0, byte_count = 0;
	int k, m, n;
	u8 p;

	for (k = 0; k < 6; k++) {
		for (m = 0; m < 6; m++) {
			for (n = 0; n < 3; n++) {
				p = coef[k][m][n];
				coeff_plane[idx++] = p;
				byte_count++;
				if (byte_count == 27) {
					idx += 5;
					byte_count = 0;
				}
			}
		}
	}
}

struct rkvdec_decoded_buffer *get_ref_buf_vp9(struct rkvdec_ctx *ctx,
					      struct vb2_v4l2_buffer *dst,
					      u64 timestamp)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *buf;

	/*
	 * If a ref is unused or invalid, address of current destination
	 * buffer is returned.
	 */
	buf = vb2_find_buffer(cap_q, timestamp);
	if (!buf)
		buf = &dst->vb2_buf;

	return vb2_to_rkvdec_decoded_buf(buf);
}

dma_addr_t get_mv_base_addr(struct rkvdec_decoded_buffer *buf)
{
	unsigned int aligned_pitch, aligned_height, yuv_len;

	aligned_height = round_up(buf->vp9.height, 64);
	aligned_pitch = round_up(buf->vp9.width * buf->vp9.bit_depth, 512) / 8;
	yuv_len = (aligned_height * aligned_pitch * 3) / 2;

	return vb2_dma_contig_plane_dma_addr(&buf->base.vb.vb2_buf, 0) +
	       yuv_len;
}

void update_dec_buf_info(struct rkvdec_decoded_buffer *buf,
			 const struct v4l2_ctrl_vp9_frame *dec_params)
{
	buf->vp9.width = dec_params->frame_width_minus_1 + 1;
	buf->vp9.height = dec_params->frame_height_minus_1 + 1;
	buf->vp9.bit_depth = dec_params->bit_depth;
}
