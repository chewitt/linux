// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip video decoder hevc common functions
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include "rkvdec-hevc-common.h"
#include "rkvdec.h"

#define RKVDEC_HEVC_MAX_DEPTH_IN_BYTES		2

enum rkvdec_image_fmt rkvdec_hevc_get_image_fmt(struct rkvdec_ctx *ctx,
						struct v4l2_ctrl *ctrl)
{
	const struct v4l2_ctrl_hevc_sps *sps = ctrl->p_new.p_hevc_sps;

	if (ctrl->id != V4L2_CID_STATELESS_HEVC_SPS)
		return RKVDEC_IMG_FMT_ANY;

	if (sps->bit_depth_luma_minus8 == 0) {
		if (sps->chroma_format_idc == 2)
			return RKVDEC_IMG_FMT_422_8BIT;
		else
			return RKVDEC_IMG_FMT_420_8BIT;
	}

	return RKVDEC_IMG_FMT_ANY;
}

void compute_tiles_uniform(struct rkvdec_hevc_run *run, u16 log2_min_cb_size,
			   u16 width, u16 height, s32 pic_in_cts_width,
			   s32 pic_in_cts_height, u16 *column_width, u16 *row_height)
{
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	int i;

	for (i = 0; i < pps->num_tile_columns_minus1 + 1; i++)
		column_width[i] = ((i + 1) * pic_in_cts_width) /
				  (pps->num_tile_columns_minus1 + 1) -
				  (i * pic_in_cts_width) /
				  (pps->num_tile_columns_minus1 + 1);

	for (i = 0; i < pps->num_tile_rows_minus1 + 1; i++)
		row_height[i] = ((i + 1) * pic_in_cts_height) /
				(pps->num_tile_rows_minus1 + 1) -
				(i * pic_in_cts_height) /
				(pps->num_tile_rows_minus1 + 1);
}

void compute_tiles_non_uniform(struct rkvdec_hevc_run *run, u16 log2_min_cb_size,
			       u16 width, u16 height, s32 pic_in_cts_width,
			       s32 pic_in_cts_height, u16 *column_width, u16 *row_height)
{
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	s32 sum = 0;
	int i;

	for (i = 0; i < pps->num_tile_columns_minus1; i++) {
		column_width[i] = pps->column_width_minus1[i] + 1;
		sum += column_width[i];
	}
	column_width[i] = pic_in_cts_width - sum;

	sum = 0;
	for (i = 0; i < pps->num_tile_rows_minus1; i++) {
		row_height[i] = pps->row_height_minus1[i] + 1;
		sum += row_height[i];
	}
	row_height[i] = pic_in_cts_height - sum;
}

static void set_ref_poc(struct rkvdec_rps_short_term_ref_set *set, int poc, int value, int flag)
{
	switch (poc) {
	case 0:
		set->delta_poc0 = value;
		set->used_flag0 = flag;
		break;
	case 1:
		set->delta_poc1 = value;
		set->used_flag1 = flag;
		break;
	case 2:
		set->delta_poc2 = value;
		set->used_flag2 = flag;
		break;
	case 3:
		set->delta_poc3 = value;
		set->used_flag3 = flag;
		break;
	case 4:
		set->delta_poc4 = value;
		set->used_flag4 = flag;
		break;
	case 5:
		set->delta_poc5 = value;
		set->used_flag5 = flag;
		break;
	case 6:
		set->delta_poc6 = value;
		set->used_flag6 = flag;
		break;
	case 7:
		set->delta_poc7 = value;
		set->used_flag7 = flag;
		break;
	case 8:
		set->delta_poc8 = value;
		set->used_flag8 = flag;
		break;
	case 9:
		set->delta_poc9 = value;
		set->used_flag9 = flag;
		break;
	case 10:
		set->delta_poc10 = value;
		set->used_flag10 = flag;
		break;
	case 11:
		set->delta_poc11 = value;
		set->used_flag11 = flag;
		break;
	case 12:
		set->delta_poc12 = value;
		set->used_flag12 = flag;
		break;
	case 13:
		set->delta_poc13 = value;
		set->used_flag13 = flag;
		break;
	case 14:
		set->delta_poc14 = value;
		set->used_flag14 = flag;
		break;
	}
}

static void assemble_scalingfactor0(struct rkvdec_dev *rkvdec,
				    u8 *output,
				    const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	struct rkvdec_config *cfg = rkvdec->config;
	int offset = 0;

	cfg->flatten_matrices(output, (const u8 *)input->scaling_list_4x4, 6, 4);
	offset = 6 * 16 * sizeof(u8);
	cfg->flatten_matrices(output + offset, (const u8 *)input->scaling_list_8x8, 6, 8);
	offset += 6 * 64 * sizeof(u8);
	cfg->flatten_matrices(output + offset, (const u8 *)input->scaling_list_16x16, 6, 8);
	offset += 6 * 64 * sizeof(u8);

	/* Add a 128 byte padding with 0s between the two 32x32 matrices */
	cfg->flatten_matrices(output + offset, (const u8 *)input->scaling_list_32x32, 1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
	offset += 128 * sizeof(u8);
	cfg->flatten_matrices(output + offset,
			      (const u8 *)input->scaling_list_32x32 + (64 * sizeof(u8)), 1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
}

/*
 * Required layout:
 * A = scaling_list_dc_coef_16x16
 * B = scaling_list_dc_coef_32x32
 * 0 = Padding
 *
 * A, A, A, A, A, A, B, 0, 0, B, 0, 0
 */
static void assemble_scalingdc(u8 *output, const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	u8 list_32x32[6] = {0};

	memcpy(output, input->scaling_list_dc_coef_16x16, 6 * sizeof(u8));
	list_32x32[0] = input->scaling_list_dc_coef_32x32[0];
	list_32x32[3] = input->scaling_list_dc_coef_32x32[1];
	memcpy(output + 6 * sizeof(u8), list_32x32, 6 * sizeof(u8));
}

static void translate_scaling_list(struct rkvdec_dev *rkvdec, struct scaling_factor *output,
				   const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	assemble_scalingfactor0(rkvdec, output->scalingfactor0, input);
	memcpy(output->scalingfactor1, (const u8 *)input->scaling_list_4x4, 96);
	assemble_scalingdc(output->scalingdc, input);
	memset(output->reserved, 0, 4 * sizeof(u8));
}

void rkvdec_hevc_assemble_hw_scaling_list(struct rkvdec_dev *rkvdec,
					  struct rkvdec_hevc_run *run,
					  struct scaling_factor *scaling_list,
					  struct v4l2_ctrl_hevc_scaling_matrix *cache)
{
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling = run->scaling_matrix;

	if (!memcmp(cache, scaling,
		    sizeof(struct v4l2_ctrl_hevc_scaling_matrix)))
		return;

	translate_scaling_list(rkvdec, scaling_list, scaling);

	memcpy(cache, scaling,
	       sizeof(struct v4l2_ctrl_hevc_scaling_matrix));
}

void rkvdec_hevc_assemble_hw_rps(struct rkvdec_hevc_run *run, struct rkvdec_rps *rps)
{
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;

	memset(rps, 0, sizeof(*rps));

	if (!run->sps_rps_extended)
		return;

	for (int i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
		rps->refs[i].lt_ref_pic_poc_lsb =
			run->sps_rps_extended[i].lt_ref_pic_poc_lsb_sps;
		rps->refs[i].used_by_curr_pic_lt_flag =
			!!(run->sps_rps_extended[i].flags & V4L2_HEVC_EXT_SPS_RPS_FLAG_USED_LT);
	}

	for (int i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
		int poc = 0;
		int j = 0;
		const struct v4l2_ctrl_hevc_ext_sps_rps *set =
			&run->sps_rps_extended[i];

		rps->short_term_ref_sets[i].num_negative = set->num_negative_pics;
		rps->short_term_ref_sets[i].num_positive = set->num_positive_pics;

		for (; j < set->num_negative_pics; j++) {
			set_ref_poc(&rps->short_term_ref_sets[i], j,
				    set->delta_poc_s0[j], set->used_by_curr_pic_s0[j]);
		}
		poc = j;

		for (j = 0; j < set->num_positive_pics; j++) {
			set_ref_poc(&rps->short_term_ref_sets[i], poc + j,
				    set->delta_poc_s1[j], set->used_by_curr_pic_s1[j]);
		}
	}
}

struct vb2_buffer *get_ref_buf(struct rkvdec_ctx *ctx,
			       struct rkvdec_hevc_run *run,
			       unsigned int dpb_idx)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = run->decode_params;
	const struct v4l2_hevc_dpb_entry *dpb = decode_params->dpb;
	struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *buf = NULL;

	if (dpb_idx < decode_params->num_active_dpb_entries)
		buf = vb2_find_buffer(cap_q, dpb[dpb_idx].timestamp);

	/*
	 * If a DPB entry is unused or invalid, the address of current destination
	 * buffer is returned.
	 */
	if (!buf)
		return &run->base.bufs.dst->vb2_buf;

	return buf;
}

int rkvdec_hevc_adjust_fmt(struct rkvdec_ctx *ctx,
			   struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

	fmt->num_planes = 1;
	if (!fmt->plane_fmt[0].sizeimage)
		fmt->plane_fmt[0].sizeimage = fmt->width * fmt->height *
					      RKVDEC_HEVC_MAX_DEPTH_IN_BYTES;
	return 0;
}

int rkvdec_hevc_validate_sps(struct rkvdec_ctx *ctx,
			     const struct v4l2_ctrl_hevc_sps *sps)
{
	/* Only 4:2:0 is supported */
	if (sps->chroma_format_idc != 1)
		return -EINVAL;

	/* Luma and chroma bit depth mismatch */
	if (sps->bit_depth_luma_minus8 != sps->bit_depth_chroma_minus8)
		return -EINVAL;

	/* Only 8-bit and 10-bit are supported */
	if (sps->bit_depth_luma_minus8 != 0 && sps->bit_depth_luma_minus8 != 2)
		return -EINVAL;

	if (sps->pic_width_in_luma_samples > ctx->coded_fmt.fmt.pix_mp.width ||
	    sps->pic_height_in_luma_samples > ctx->coded_fmt.fmt.pix_mp.height)
		return -EINVAL;

	return 0;
}

void rkvdec_hevc_run_preamble(struct rkvdec_ctx *ctx,
			      struct rkvdec_hevc_run *run)
{
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_DECODE_PARAMS);
	run->decode_params = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SLICE_PARAMS);
	run->slice_params = ctrl ? ctrl->p_cur.p : NULL;
	run->num_slices = ctrl ? ctrl->new_elems : 0;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SPS);
	run->sps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_PPS);
	run->pps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SCALING_MATRIX);
	run->scaling_matrix = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_EXT_SPS_RPS);
	run->sps_rps_extended = ctrl ? ctrl->p_cur.p : NULL;

	rkvdec_run_preamble(ctx, &run->base);
}

