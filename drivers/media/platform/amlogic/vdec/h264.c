// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include <media/v4l2-h264.h>
#include <linux/vmalloc.h>
#include "aml_vdec.h"
#include "aml_vdec_hw.h"
#include "h264.h"

#define INVALID_POC 0xffffffff

#define H264_SLICE_HEADER_DONE		0x1
#define H264_SLICE_DATA_DONE		0x2

#define H264_MAX_COL_BUF			32
#define H264_MAX_CANVAS_POS			26

#define DECODER_TIMEOUT_MS			500

#define COL_BUFFER_MARGIN			2
#define COL_SIZE_FOR_ONE_MB			96

struct vdec_h264_stateless_ctrl_ref {
	const struct v4l2_ctrl_h264_decode_params *decode;
	const struct v4l2_ctrl_h264_scaling_matrix *scaling;
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
};

enum SliceType {
	P_SLICE = 0,
	B_SLICE = 1,
	I_SLICE = 2,
	SP_SLICE = 3,
	SI_SLICE = 4,
	MAX_SLICE_TYPES = 5
};

#define I_Slice                               2
#define P_Slice                               5
#define B_Slice                               6
#define P_Slice_0                             0
#define B_Slice_1                             1
#define I_Slice_7                             7

/* Used by firmware */
union param {
	struct {
		unsigned short data[RPM_END - RPM_BEGIN];
	} l;
	struct {
		unsigned short dump[DPB_OFFSET];
		unsigned short dpb_base[FRAME_IN_DPB << 3];

		unsigned short dpb_max_buffer_frame;
		unsigned short actual_dpb_size;

		unsigned short colocated_buf_status;

		unsigned short num_forward_short_term_reference_pic;
		unsigned short num_short_term_reference_pic;
		unsigned short num_reference_pic;

		unsigned short current_dpb_index;
		unsigned short current_decoded_frame_num;
		unsigned short current_reference_frame_num;

		unsigned short l0_size;
		unsigned short l1_size;
		/**
		 * [6:5] : nal_ref_idc
		 * [4:0] : nal_unit_type
		 */
		unsigned short NAL_info_mmco;
		/**
		 * [1:0] : 00 - top field, 01 - bottom field,
		 *         10 - frame, 11 - mbaff frame
		 */
		unsigned short picture_structure_mmco;
		unsigned short frame_num;
		unsigned short pic_order_cnt_lsb;

		unsigned short num_ref_idx_l0_active_minus1;
		unsigned short num_ref_idx_l1_active_minus1;

		unsigned short PrevPicOrderCntLsb;
		unsigned short PreviousFrameNum;

		/* 32 bits variables */
		unsigned short delta_pic_order_cnt_bottom[2];
		unsigned short delta_pic_order_cnt_0[2];
		unsigned short delta_pic_order_cnt_1[2];

		unsigned short PrevPicOrderCntMsb[2];
		unsigned short PrevFrameNumOffset[2];

		unsigned short frame_pic_order_cnt[2];
		unsigned short top_field_pic_order_cnt[2];
		unsigned short bottom_field_pic_order_cnt[2];

		unsigned short colocated_mv_addr_start[2];
		unsigned short colocated_mv_addr_end[2];
		unsigned short colocated_mv_wr_addr[2];
	} dpb;
	struct {
		unsigned short dump[MMCO_OFFSET];

		/* array base address for offset_for_ref_frame */
		unsigned short offset_for_ref_frame_base[128];

		/**
		 * 0 - Index in DPB
		 * 1 - Picture Flag
		 *  [2] : 0 - short term reference,
		 *			1 - long term reference
		 *  [1] : bottom field
		 *  [0] : top field
		 * 2 - Picture Number (short term or long term) low 16 bits
		 * 3 - Picture Number (short term or long term) high 16 bits
		 */
		unsigned short reference_base[128];

		/* command and parameter, until command is 3 */
		unsigned short l0_reorder_cmd[REORDER_CMD_MAX];
		unsigned short l1_reorder_cmd[REORDER_CMD_MAX];

		/* command and parameter, until command is 0 */
		unsigned short mmco_cmd[44];

		unsigned short l0_base[40];
		unsigned short l1_base[40];
	} mmco;
	struct {
		/* from ucode lmem, do not change this struct */
	} p;
};

struct h264_canvas {
	u32 canvas_pos;
	int poc;
};

struct h264_decode_buf_spec {
	struct v4l2_h264_dpb_entry *dpb;
	u32 canvas_pos;
	u32 dpb_index;
	int poc;
	int col_buf_index;
	u8 y_canvas_index;
	u8 u_canvas_index;
	u8 v_canvas_index;
	u8 used;
	u8 long_term_flag;
	dma_addr_t y_dma_addr;
	dma_addr_t c_dma_addr;
};

#define REORDERING_COMMAND_MAX_SIZE	33
struct slice {
	int frame_num;
	/*modification */
	int slice_type;
	int num_ref_idx_l0;
	int num_ref_idx_l1;
	int first_mb_in_slice;
	int ref_pic_list_reordering_flag[2];
	int modification_of_pic_nums_idc[2][REORDERING_COMMAND_MAX_SIZE];
	int abs_diff_pic_num_minus1[2][REORDERING_COMMAND_MAX_SIZE];
	int long_term_pic_idx[2][REORDERING_COMMAND_MAX_SIZE];
	unsigned char dec_ref_pic_marking_buffer_valid;
};

struct aml_h264_ctx {
	struct aml_vdec_ctx *v4l2_ctx;
	u8 init_flag;
	u8 new_pic_flag;
	u8 mc_cpu_loaded;
	u8 param_set;
	u8 colocated_buf_num;
	u8 reg_iqidct_control_init_flag;
	u32 reg_iqidct_control;
	u32 reg_vcop_ctrl_reg;
	u32 reg_rv_ai_mb_count;
	u32 vld_dec_control;
	u32 save_avscratch_f;
	u32 seq_info;
	u32 decode_pic_count;
	union param dpb_param;
	u32 dec_status;
	struct slice mslice;
	struct h264_decode_buf_spec curr_spec;
	struct h264_decode_buf_spec ref_list0[V4L2_H264_NUM_DPB_ENTRIES + 1];
	struct h264_decode_buf_spec ref_list1[V4L2_H264_NUM_DPB_ENTRIES + 1];
	struct h264_decode_buf_spec ref_list0_unreordered[V4L2_H264_NUM_DPB_ENTRIES + 1];
	struct h264_decode_buf_spec ref_list1_unreordered[V4L2_H264_NUM_DPB_ENTRIES + 1];
	u8 list_size[2];
	u32 canvas_pos_map;
	struct h264_canvas ref_canvas[V4L2_H264_NUM_DPB_ENTRIES + 1];
	dma_addr_t lmem_phy_addr;
	void *lmem_addr;
	dma_addr_t mc_cpu_paddr;
	void *mc_cpu_vaddr;
	dma_addr_t cma_alloc_addr;
	void *cma_alloc_vaddr;
	dma_addr_t collated_cma_addr;
	dma_addr_t collated_cma_addr_end;
	void *collated_cma_vaddr;
	dma_addr_t workspace_offset;
	void *workspace_vaddr;
	u32 col_buf_alloc_size;
	u32 one_col_buf_size;
	u32 colocated_buf_map;
	int colocated_buf_poc[H264_MAX_COL_BUF];

	u32 frame_width;
	u32 frame_height;
	u32 mb_width;
	u32 mb_height;
	u32 mb_total;
	u32 max_num_ref_frames;

	struct vdec_h264_stateless_ctrl_ref ctrl_ref;
};

static inline int get_flag(u32 flag, u32 mask)
{
	return (flag & mask) ? 1 : 0;
}

static inline void write_lmem(unsigned short *base, u32 offset, u32 value)
{
	base[offset] = value;
}

static inline uint32_t spec2canvas(struct h264_decode_buf_spec *buf_spec)
{
	return (buf_spec->v_canvas_index << 16) |
		(buf_spec->u_canvas_index << 8) |
		(buf_spec->y_canvas_index << 0);
}

static struct h264_decode_buf_spec *find_spec_by_dpb_index(struct aml_h264_ctx
							   *h264_ctx, int index, int list)
{
	int i;
	int size;
	struct h264_decode_buf_spec *ref_list;

	size = h264_ctx->list_size[list];
	if (list == 0)
		ref_list = &h264_ctx->ref_list0[0];
	else
		ref_list = &h264_ctx->ref_list1[0];

	for (i = 0; i < size; i++) {
		if (index == ref_list[i].dpb_index)
			return &ref_list[i];
	}

	return NULL;
}

static int h264_prepare_input(struct aml_vdec_ctx *ctx)
{
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);
	struct vb2_v4l2_buffer *src;
	struct vb2_buffer *vb;
	dma_addr_t src_dma;
	u32 payload_size;
	int dummy;

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!src) {
		dev_info(hw->dev, "no input buffer available!\n");
		return -1;
	}
	vb = &src->vb2_buf;
	payload_size = vb2_get_plane_payload(vb, 0);
	src_dma = vb2_dma_contig_plane_dma_addr(vb, 0);

	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_CONTROL, 0);
	/* reset VLD fifo for all vdec */
	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0,
		     (1 << 5) | (1 << 4) | (1 << 3));
	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0);
	regmap_write(hw->map[DOS_BUS], POWER_CTL_VLD, 1 << 4);

	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_START_PTR, src_dma);
	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_END_PTR,
		     (src_dma + payload_size));
	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_CURR_PTR,
		     round_down(src_dma, VDEC_FIFO_ALIGN));

	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_CONTROL, 1);
	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_CONTROL, 0);
	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_BUF_CNTL, 2);

	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_RP,
		     round_down(src_dma, VDEC_FIFO_ALIGN));
	dummy = payload_size + VLD_PADDING_SIZE;
	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_WP,
		     round_down((src_dma + dummy), VDEC_FIFO_ALIGN));

	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_BUF_CNTL, 3);
	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_BUF_CNTL, 2);

	regmap_write(hw->map[DOS_BUS], VLD_MEM_VIFIFO_CONTROL,
		     (0x11 << 16) | (1 << 10) | (7 << 3));

	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_1, 0x0);
	regmap_write(hw->map[DOS_BUS], H264_DECODE_INFO, (1 << 13));
	regmap_write(hw->map[DOS_BUS], H264_DECODE_SIZE, payload_size);
	regmap_write(hw->map[DOS_BUS], VIFF_BIT_CNT, payload_size * 8);

	return 0;
}

static void config_sps_params(struct aml_h264_ctx *h264_ctx,
			      unsigned short *sps_base,
			      const struct v4l2_ctrl_h264_sps *sps)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);
	u32 cfg_tmp = 0;
	u32 frame_size;
	u32 offset = 0;
	unsigned short data_tmp[0x100];
	int i, ii;

	memset(sps_base, 0, 0x100);

	h264_ctx->frame_width = (sps->pic_width_in_mbs_minus1 + 1) << 4;
	h264_ctx->frame_height = (sps->pic_height_in_map_units_minus1 + 1) << 4;

	data_tmp[offset] = PARAM_BASE_VAL;
	offset += 2;

	data_tmp[offset++] = GET_SPS_PROFILE_IDC(sps->profile_idc);

	data_tmp[offset++] = GET_SPS_SEQ_PARAM_SET_ID(sps->seq_parameter_set_id) |
	    GET_SPS_LEVEL_IDC(sps->level_idc);

	if (sps->profile_idc >= 100) {
		data_tmp[offset++] = GET_SPS_CHROMA_FORMAT_IDC(sps->chroma_format_idc);

		data_tmp[offset++] = ((sps->chroma_format_idc ^ 1) << 1);
	}

	data_tmp[offset++] = GET_SPS_LOG2_MAX_FRAME_NUM(sps->log2_max_frame_num_minus4);
	data_tmp[offset++] = GET_SPS_PIC_ORDER_TYPE(sps->pic_order_cnt_type);

	if (sps->pic_order_cnt_type == 0) {
		data_tmp[offset++] =
			GET_SPS_PIC_ORDER_CNT_LSB(sps->log2_max_pic_order_cnt_lsb_minus4);
	} else if (sps->pic_order_cnt_type == 1) {
		data_tmp[offset++] =
			get_flag(sps->flags,
				 V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO);
		data_tmp[offset++] =
			GET_SPS_OFFSET_FOR_NONREF_PIC_LOW(sps->offset_for_non_ref_pic);
		data_tmp[offset++] =
			GET_SPS_OFFSET_FOR_NONREF_PIC_HIGH(sps->offset_for_non_ref_pic);
		data_tmp[offset++] =
			GET_SPS_OFFSET_FOR_TOP_BOT_FIELD_LOW(sps->offset_for_top_to_bottom_field);
		data_tmp[offset++] =
			GET_SPS_OFFSET_FOR_TOP_BOT_FIELD_HIGH(sps->offset_for_top_to_bottom_field);
		data_tmp[offset++] = sps->num_ref_frames_in_pic_order_cnt_cycle;
	}

	data_tmp[offset++] = GET_SPS_NUM_REF_FRAMES(sps->max_num_ref_frames) |
	    GET_SPS_GAPS_ALLOWED_FLAG(get_flag(sps->flags,
					       V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED));

	data_tmp[offset++] = GET_SPS_PIC_WIDTH_IN_MBS(sps->pic_width_in_mbs_minus1);

	data_tmp[offset++] = GET_SPS_PIC_HEIGHT_IN_MBS(sps->pic_height_in_map_units_minus1);
	data_tmp[offset++] =
		GET_SPS_DIRECT_8X8_FLAGS
				(get_flag(sps->flags,
					  V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE)) |
	    GET_SPS_MB_ADAPTIVE_FRAME_FIELD_FLAGS
				(get_flag(sps->flags,
					  V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD)) |
	    GET_SPS_FRAME_MBS_ONLY_FLAGS(get_flag(sps->flags,
						  V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY));

	for (i = 0; i < 0x100; i += 4) {
		for (ii = 0; ii < 4; ii++)
			sps_base[i + 3 - ii] = data_tmp[i + ii];
	}

	frame_size = (sps->pic_width_in_mbs_minus1 + 1) * (sps->pic_height_in_map_units_minus1 + 1);
	cfg_tmp = (get_flag(sps->flags, V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) << 31) |
				(sps->max_num_ref_frames << 24) | (frame_size << 8) |
				(sps->pic_width_in_mbs_minus1 + 1);
	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_1, cfg_tmp);
	h264_ctx->seq_info = cfg_tmp;

	cfg_tmp = 0;
	cfg_tmp = (get_flag(sps->flags, V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE) << 15) |
				(sps->chroma_format_idc);
	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_2, cfg_tmp);

	cfg_tmp = 0;
	cfg_tmp = (sps->max_num_ref_frames << 8) | (sps->level_idc);
	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_B, cfg_tmp);

	cfg_tmp = ((sps->level_idc & 0xff) << 7) |
	    (get_flag(sps->flags, V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) << 2);
	regmap_write(hw->map[DOS_BUS], NAL_SEARCH_CTL,
		     read_dos_reg(hw, NAL_SEARCH_CTL) | cfg_tmp);

	h264_ctx->mb_width = (sps->pic_width_in_mbs_minus1 + 4) & 0xfffffffc;
	h264_ctx->mb_height = (sps->pic_height_in_map_units_minus1 + 4) & 0xfffffffc;
	h264_ctx->mb_total = h264_ctx->mb_width * h264_ctx->mb_height;
	h264_ctx->max_num_ref_frames = sps->max_num_ref_frames;
}

static void config_pps_params(struct aml_h264_ctx *h264_ctx,
			      unsigned short *pps_base,
			      const struct v4l2_ctrl_h264_pps *pps)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);
	u32 offset = 0;
	unsigned short data_tmp[0x100];
	u32 max_reference_size = V4L2_H264_NUM_DPB_ENTRIES;
	u32 max_list_size;
	int i, ii;

	memset(pps_base, 0, 0x100);

	data_tmp[offset++] = PARAM_BASE_VAL;

	data_tmp[offset++] =
		GET_PPS_PIC_PARAM_SET_ID(pps->pic_parameter_set_id) |
	    GET_PPS_SEQ_PARAM_SET_ID(pps->seq_parameter_set_id) |
	    GET_PPS_ENTROPY_CODING_MODE_FLAG
			(get_flag(pps->flags,
				  V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)) |
	    GET_PPS_PIC_ORDER_PRESENT_FLAG
			(get_flag(pps->flags,
				  V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT));

	data_tmp[offset++] =
		GET_PPS_WEIGHTED_BIPRED_IDC(pps->weighted_bipred_idc) |
		GET_PPS_WEIGHTED_PRED_FLAG(get_flag(pps->flags,
						    V4L2_H264_PPS_FLAG_WEIGHTED_PRED)) |
		GET_PPS_NUM_IDX_REF_L1_MINUS1(pps->num_ref_idx_l1_default_active_minus1) |
		GET_PPS_NUM_IDX_REF_L0_MINUS1(pps->num_ref_idx_l0_default_active_minus1);

	data_tmp[offset++] = GET_PPS_INIT_QS_MINUS26(pps->pic_init_qs_minus26) |
	    GET_PPS_INIT_QP_MINUS26(pps->pic_init_qp_minus26);

	data_tmp[offset] =
	    GET_PPS_CHROMA_QP_INDEX_OFFSET(pps->chroma_qp_index_offset) |
	    GET_PPS_DEBLOCK_FILTER_CTRL_PRESENT_FLAG
				(get_flag(pps->flags,
					  V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT)) |
	    GET_PPS_CONSTRAIN_INTRA_PRED_FLAG
				(get_flag(pps->flags,
					  V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED)) |
	    GET_PPS_REDUNDANT_PIC_CNT_PRESENT_FLAG
	    (get_flag(pps->flags,
		      V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT));
	if (get_flag(pps->flags, V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE |
	     V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT))
		data_tmp[offset] |= (1 << 11);
	offset++;

	data_tmp[offset++] =
		GET_PPS_SCALING_MATRIX_PRESENT_FLAG(get_flag
						    (pps->flags,
						    V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT)) |
		GET_PPS_TRANSFORM_8X8_FLAG(get_flag(pps->flags,
						    V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE));

	data_tmp[offset++] =
		GET_PPS_GET_SECOND_CHROMA_QP_OFFSET(pps->second_chroma_qp_index_offset);

	max_list_size = (pps->num_ref_idx_l1_default_active_minus1 + 1) +
	    (pps->num_ref_idx_l0_default_active_minus1 + 1);

	h264_ctx->max_num_ref_frames = max_list_size > h264_ctx->max_num_ref_frames ?
						max_list_size : h264_ctx->max_num_ref_frames;

	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_0,
		     ((h264_ctx->max_num_ref_frames + 1) << 24) |
		     (max_reference_size << 16) | (max_reference_size << 8));

	for (i = 0; i < 0x100; i += 4) {
		for (ii = 0; ii < 4; ii++)
			pps_base[i + 3 - ii] = data_tmp[i + ii];
	}
}

static void h264_config_params(struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)ctx->codec_priv;
	unsigned short *p_sps_base, *p_pps_base;
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;
	const struct v4l2_ctrl_h264_sps *sps = ctrls->sps;
	const struct v4l2_ctrl_h264_pps *pps = ctrls->pps;

	p_sps_base = (unsigned short *)(h264_ctx->workspace_vaddr +
		MEM_SPS_BASE + sps->seq_parameter_set_id * 0x400);
	p_pps_base = (unsigned short *)(h264_ctx->workspace_vaddr +
		MEM_PPS_BASE + pps->pic_parameter_set_id * 0x200);

	dev_dbg(&ctx->dev->plat_dev->dev, "%s sps id %d, pps id %d\n",
		__func__, sps->seq_parameter_set_id, pps->pic_parameter_set_id);

	config_sps_params(h264_ctx, p_sps_base, sps);
	config_pps_params(h264_ctx, p_pps_base, pps);
}

static void config_decode_canvas(struct aml_vdec_hw *hw,
				 struct h264_decode_buf_spec *buf_spec,
				 u32 mb_width, u32 mb_height)
{
	int canvas_alloc_result = 0;
	int blkmode = 0x0;

	canvas_alloc_result = meson_canvas_alloc(hw->canvas, &buf_spec->y_canvas_index);
	canvas_alloc_result = meson_canvas_alloc(hw->canvas, &buf_spec->u_canvas_index);
	buf_spec->v_canvas_index = buf_spec->u_canvas_index;

	if (!canvas_alloc_result) {
		/* config y canvas */
		meson_canvas_config(hw->canvas,
				    buf_spec->y_canvas_index, buf_spec->y_dma_addr,
				    mb_width << 4, mb_height << 4,
				    MESON_CANVAS_WRAP_NONE, MESON_CANVAS_BLKMODE_LINEAR,
				    MESON_CANVAS_ENDIAN_SWAP64);
		regmap_write(hw->map[DOS_BUS], VDEC_ASSIST_CANVAS_BLK32,
			     (1 << 11) | /* canvas_blk32_wr */
			     (blkmode << 10) |	/* canvas_blk32 */
			     (1 << 8) |	/* canvas_index_wr */
			     (buf_spec->y_canvas_index << 0)	/* canvas index */
		    );

		/* config uv canvas */
		meson_canvas_config(hw->canvas,
				    buf_spec->u_canvas_index, buf_spec->c_dma_addr,
				    mb_width << 4, mb_height << 3,
				    MESON_CANVAS_WRAP_NONE, MESON_CANVAS_BLKMODE_LINEAR,
				    MESON_CANVAS_ENDIAN_SWAP64);
		regmap_write(hw->map[DOS_BUS], VDEC_ASSIST_CANVAS_BLK32,
			     (1 << 11) | /* canvas_blk32_wr */
			     (blkmode << 10) |	/* canvas_blk32 */
			     (1 << 8) |	/* canvas_index_wr */
			     (buf_spec->u_canvas_index << 0)	/* canvas index */
		    );

		regmap_write(hw->map[DOS_BUS], ANC0_CANVAS_ADDR + (buf_spec->canvas_pos << 2),
			     spec2canvas(buf_spec));
	}
}

static int allocate_colocate_buf(struct aml_h264_ctx *h264_ctx, int poc)
{
	int i;
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;

	for (i = 0; i < h264_ctx->colocated_buf_num; i++) {
		if (((h264_ctx->colocated_buf_map >> i) & 0x1) == 0) {
			h264_ctx->colocated_buf_map |= (1 << i);
			break;
		}
	}

	if (i == h264_ctx->colocated_buf_num)
		return -1;

	h264_ctx->colocated_buf_poc[i] = poc;
	dev_dbg(&ctx->dev->plat_dev->dev, "%s colocated_buf_index %d poc %d\n",
		__func__, i, h264_ctx->colocated_buf_poc[i]);

	return i;
}

static void release_colocate_buf(struct aml_h264_ctx *h264_ctx, int index)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;

	if (index >= 0) {
		if (index >= h264_ctx->colocated_buf_num) {
			dev_dbg
			    (&ctx->dev->plat_dev->dev,
				 "%s error, index %d is bigger than buf count %d\n",
			     __func__, index, h264_ctx->max_num_ref_frames);
		} else {
			if (h264_ctx->colocated_buf_poc[index] != INVALID_POC &&
			    ((h264_ctx->colocated_buf_map >> index) & 0x1) == 0x1) {
				h264_ctx->colocated_buf_map &= (~(1 << index));
				dev_dbg
				    (&ctx->dev->plat_dev->dev,
				     "%s colocated_buf_index %d released poc %d\n",
				     __func__, index,
				     h264_ctx->colocated_buf_poc[index]);
			}
			h264_ctx->colocated_buf_poc[index] = INVALID_POC;
		}
	}
}

static int get_col_buf_index_by_poc(struct aml_h264_ctx *h264_ctx, int poc)
{
	int idx;

	for (idx = 0; idx < h264_ctx->colocated_buf_num; idx++) {
		if (h264_ctx->colocated_buf_poc[idx] == poc)
			break;
	}

	if (idx == h264_ctx->colocated_buf_num)
		idx = -1;

	return idx;
}

static int alloc_colocate_cma(struct aml_h264_ctx *h264_ctx,
			      struct aml_vdec_ctx *ctx)
{
	int alloc_size = 0;
	int i;
	struct aml_vdec_hw *hw;

	if (h264_ctx->collated_cma_vaddr)
		return 0;

	hw = vdec_get_hw(ctx->dev);
	if (!hw)
		return -1;

	/* 96 :col buf size for each mb */
	h264_ctx->one_col_buf_size = h264_ctx->mb_total * 96;
	alloc_size = PAGE_ALIGN(h264_ctx->one_col_buf_size *
			(h264_ctx->max_num_ref_frames + COL_BUFFER_MARGIN));
	h264_ctx->collated_cma_vaddr = dma_alloc_coherent(hw->dev, alloc_size,
							  &h264_ctx->collated_cma_addr, GFP_KERNEL);
	if (!h264_ctx->collated_cma_vaddr)
		return -ENOMEM;

	dev_dbg
	    (&ctx->dev->plat_dev->dev,
	    "collated_cma_addr = 0x%llx, one_col_buf_size = %x alloc_size = %x\n",
	     h264_ctx->collated_cma_addr, h264_ctx->one_col_buf_size,
	     alloc_size);
	h264_ctx->collated_cma_addr_end =
	    h264_ctx->collated_cma_addr + alloc_size;
	memset(h264_ctx->collated_cma_vaddr, 0, alloc_size);
	h264_ctx->col_buf_alloc_size = alloc_size;
	h264_ctx->colocated_buf_map = 0;
	h264_ctx->colocated_buf_num = h264_ctx->max_num_ref_frames + COL_BUFFER_MARGIN;

	for (i = 0; i < H264_MAX_COL_BUF; i++)
		h264_ctx->colocated_buf_poc[i] = INVALID_POC;

	return 0;
}

static void config_p_reflist(struct aml_h264_ctx *h264_ctx,
			     struct v4l2_h264_reference *v4l2_p0_reflist,
			     u32 list_size)
{
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;
	struct v4l2_ctrl_h264_decode_params *decode =
	    (struct v4l2_ctrl_h264_decode_params *)ctrls->decode;
	struct v4l2_h264_dpb_entry *dpb = decode->dpb;
	u8 index;
	int i;

	for (i = 0; i < list_size; i++) {
		index = v4l2_p0_reflist[i].index;
		h264_ctx->ref_list0[i].used = 1;
		h264_ctx->ref_list0[i].dpb = &dpb[index];
		h264_ctx->ref_list0[i].poc = dpb[index].top_field_order_cnt;
		h264_ctx->ref_list0[i].long_term_flag =
		    dpb[index].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM ? true : false;
		h264_ctx->ref_list0[i].dpb_index = index;
	}
	h264_ctx->list_size[0] = list_size;
}

static void config_b_reflist(struct aml_h264_ctx *h264_ctx,
			     struct v4l2_h264_reference *v4l2_b0_reflist,
			     struct v4l2_h264_reference *v4l2_b1_reflist,
			     u32 list_size)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;
	struct v4l2_ctrl_h264_decode_params *decode =
	    (struct v4l2_ctrl_h264_decode_params *)ctrls->decode;
	struct v4l2_h264_dpb_entry *dpb = decode->dpb;
	u8 index;
	int i, j;

	h264_ctx->list_size[0] = list_size;
	for (i = 0; i < list_size; i++) {
		index = v4l2_b0_reflist[i].index;
		h264_ctx->ref_list0[i].used = 1;
		h264_ctx->ref_list0[i].dpb = &dpb[index];
		h264_ctx->ref_list0[i].poc = dpb[index].top_field_order_cnt;
		h264_ctx->ref_list0[i].long_term_flag =
		    dpb[index].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM ? true : false;
		h264_ctx->ref_list0[i].col_buf_index =
		    get_col_buf_index_by_poc(h264_ctx, dpb[index].top_field_order_cnt);
		h264_ctx->ref_list0[i].dpb_index = index;
	}

	h264_ctx->list_size[1] = list_size;
	for (j = 0; j < list_size; j++) {
		index = v4l2_b1_reflist[j].index;
		h264_ctx->ref_list1[j].used = 1;
		h264_ctx->ref_list1[j].dpb = &dpb[index];
		h264_ctx->ref_list1[j].poc = dpb[index].top_field_order_cnt;
		h264_ctx->ref_list1[j].long_term_flag =
		    dpb[index].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM ? true : false;
		h264_ctx->ref_list1[j].col_buf_index =
			get_col_buf_index_by_poc(h264_ctx, dpb[index].top_field_order_cnt);
		h264_ctx->ref_list1[j].dpb_index = index;
	}

	if ((h264_ctx->list_size[1] + h264_ctx->list_size[0]) < list_size)
		dev_info(&ctx->dev->plat_dev->dev, "ref list incorrect list0 %d list0 %d list_size%d\n",
			 h264_ctx->list_size[0], h264_ctx->list_size[1], list_size);
}

static int poc_is_in_dpb(int poc, const struct v4l2_h264_dpb_entry *dpb)
{
	int i;
	int ret = 0;

	for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
		if (poc == dpb[i].top_field_order_cnt) {
			ret = 1;
			break;
		}
	}

	return ret;
}

static int get_ref_list_size(struct aml_h264_ctx *h264_ctx, int cur_list)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	unsigned short override_flag = h264_ctx->dpb_param.l.data[REF_IDC_OVERRIDE_FLAG];
	int num_ref_idx_lx_active_minus1;

	if (cur_list == 0) {
		num_ref_idx_lx_active_minus1 =
			h264_ctx->ctrl_ref.pps->num_ref_idx_l0_default_active_minus1;
		if (override_flag)
			num_ref_idx_lx_active_minus1 =
				h264_ctx->dpb_param.dpb.num_ref_idx_l0_active_minus1;
	} else {
		num_ref_idx_lx_active_minus1 =
			h264_ctx->ctrl_ref.pps->num_ref_idx_l1_default_active_minus1;
	}
	dev_dbg(&ctx->dev->plat_dev->dev, "%s get list %d size %d\n",
		__func__, cur_list, num_ref_idx_lx_active_minus1 + 1);

	return num_ref_idx_lx_active_minus1 + 1;
}

static int get_refidx_by_picnum(struct aml_h264_ctx *h264_ctx, int pic_num,
				int curr_list)
{
	int i;
	struct h264_decode_buf_spec *ref_list;

	if (curr_list == 0)
		ref_list = &h264_ctx->ref_list0[0];
	else
		ref_list = &h264_ctx->ref_list1[0];

	for (i = 0; ref_list[i].dpb; i++) {
		if (pic_num == ref_list[i].dpb->pic_num)
			return i;
	}

	return -1;
}

static struct h264_decode_buf_spec *get_st_refpic_by_num(struct aml_h264_ctx *h264_ctx,
							 int pic_num, int curr_list)
{
	int i;
	struct h264_decode_buf_spec *ref_list;

	if (curr_list == 0)
		ref_list = &h264_ctx->ref_list0_unreordered[0];
	else
		ref_list = &h264_ctx->ref_list1_unreordered[0];

	for (i = 0; ref_list[i].dpb; i++) {
		if (pic_num == ref_list[i].dpb->pic_num && ref_list[i].long_term_flag == 0)
			return &ref_list[i];
	}

	return NULL;
}

static struct h264_decode_buf_spec *get_lt_refpic_by_num(struct aml_h264_ctx *h264_ctx,
							 int pic_num, int curr_list)
{
	int i;
	struct h264_decode_buf_spec *ref_list;

	if (curr_list == 0)
		ref_list = &h264_ctx->ref_list0_unreordered[0];
	else
		ref_list = &h264_ctx->ref_list1_unreordered[0];

	for (i = 0; ref_list[i].dpb; i++) {
		if (pic_num == ref_list[i].dpb->pic_num && ref_list[i].long_term_flag == 1)
			return &ref_list[i];
	}

	return NULL;
}

static void reorder_short_term(struct slice *curr_slice, int cur_list,
			       int pic_num_lx, int *ref_idx_lx)
{
	struct aml_h264_ctx *h264_ctx =
	    container_of(curr_slice, struct aml_h264_ctx, mslice);
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	int c_idx, n_idx;
	int num_ref_idx_lx_active;
	struct h264_decode_buf_spec *pic_lx = NULL;
	struct h264_decode_buf_spec *ref_list_reordered;

	if (cur_list == 0)
		ref_list_reordered = &h264_ctx->ref_list0[0];
	else
		ref_list_reordered = &h264_ctx->ref_list1[0];

	num_ref_idx_lx_active = get_ref_list_size(h264_ctx, cur_list);

	/* find short-term ref frame with pic_num is pic_num_lx */
	pic_lx = get_st_refpic_by_num(h264_ctx, pic_num_lx, cur_list);
	if (!pic_lx) {
		dev_dbg(&ctx->dev->plat_dev->dev, "cannot find st pic_lx for %d\n", pic_num_lx);
		return;
	}

	if (*ref_idx_lx == get_refidx_by_picnum(h264_ctx, pic_num_lx, cur_list)) {
		dev_dbg(&ctx->dev->plat_dev->dev, "no need to move pic lx %d\n", *ref_idx_lx);
		*ref_idx_lx = *ref_idx_lx + 1;
		return;
	}

	for (c_idx = num_ref_idx_lx_active; c_idx > *ref_idx_lx; c_idx--)
		memcpy(&ref_list_reordered[c_idx], &ref_list_reordered[c_idx - 1],
		       sizeof(struct h264_decode_buf_spec));

	memcpy(&ref_list_reordered[*ref_idx_lx], pic_lx, sizeof(struct h264_decode_buf_spec));
	dev_dbg(&ctx->dev->plat_dev->dev, "%s : RefPicListX[%d ] = pic %p pic_num(%d)\n", __func__,
		*ref_idx_lx, pic_lx, ref_list_reordered[*ref_idx_lx].dpb->pic_num);
	*ref_idx_lx = *ref_idx_lx + 1;

	n_idx = *ref_idx_lx;
	for (c_idx = *ref_idx_lx; c_idx <= num_ref_idx_lx_active; c_idx++) {
		if (ref_list_reordered[c_idx].long_term_flag || !ref_list_reordered[c_idx].dpb ||
		    ref_list_reordered[c_idx].dpb->pic_num != pic_num_lx)
			memcpy(&ref_list_reordered[n_idx++], &ref_list_reordered[c_idx],
			       sizeof(struct h264_decode_buf_spec));
	}

	h264_ctx->list_size[cur_list] = num_ref_idx_lx_active;
}

static void reorder_long_term(struct slice *curr_slice, int cur_list,
			      int lt_pic_num, int *ref_idx_lx)
{
	struct aml_h264_ctx *h264_ctx =
	    container_of(curr_slice, struct aml_h264_ctx, mslice);
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	int num_ref_idx_lx_active;
	int c_idx, n_idx;
	struct h264_decode_buf_spec *ref_list;
	struct h264_decode_buf_spec *pic_lt = NULL;

	if (cur_list == 0)
		ref_list = &h264_ctx->ref_list0[0];
	else
		ref_list = &h264_ctx->ref_list1[0];

	num_ref_idx_lx_active = get_ref_list_size(h264_ctx, cur_list);

	/* find long-term ref frame with pic_num is lt_pic_num */
	pic_lt = get_lt_refpic_by_num(h264_ctx, lt_pic_num, cur_list);
	if (!pic_lt) {
		dev_dbg(&ctx->dev->plat_dev->dev, "cannot find lt pic_lx for %d\n", lt_pic_num);
		return;
	}

	if (*ref_idx_lx == get_refidx_by_picnum(h264_ctx, lt_pic_num, cur_list)) {
		dev_dbg(&ctx->dev->plat_dev->dev, "no need to move pic lx %d\n", *ref_idx_lx);
		*ref_idx_lx = *ref_idx_lx + 1;
		return;
	}

	for (c_idx = num_ref_idx_lx_active; c_idx > *ref_idx_lx; c_idx--)
		memcpy(&ref_list[c_idx], &ref_list[c_idx - 1], sizeof(struct h264_decode_buf_spec));

	memcpy(&ref_list[*ref_idx_lx], pic_lt, sizeof(struct h264_decode_buf_spec));
	dev_dbg(&ctx->dev->plat_dev->dev, "%s : RefPicListX[%d ] = pic %p pic_num(%d)\n", __func__,
		*ref_idx_lx, pic_lt, ref_list[*ref_idx_lx].dpb->pic_num);
	*ref_idx_lx = *ref_idx_lx + 1;

	n_idx = *ref_idx_lx;
	/* Pointer dpb is NULL means this is a dummy frame store */
	for (c_idx = *ref_idx_lx; c_idx <= num_ref_idx_lx_active; c_idx++) {
		if (!ref_list[c_idx].long_term_flag || !ref_list[c_idx].dpb ||
		    ref_list[c_idx].dpb->pic_num != lt_pic_num)
			memcpy(&ref_list[n_idx++], &ref_list[c_idx],
			       sizeof(struct h264_decode_buf_spec));
	}

	h264_ctx->list_size[cur_list] = num_ref_idx_lx_active;
}

static void get_modification_cmd(unsigned short *reorder_cmd,
				 struct slice *curr_slice, int list)
{
	int i, j, val;

	val = curr_slice->ref_pic_list_reordering_flag[list];
	if (val) {
		i = 0;
		j = 0;
		do {
			curr_slice->modification_of_pic_nums_idc[list][i] =
			    reorder_cmd[j++];
			if (j >= REORDER_CMD_MAX) {
				curr_slice->modification_of_pic_nums_idc[list][i] = 0;
				break;
			}

			val = curr_slice->modification_of_pic_nums_idc[list][i];
			if (val == 0 || val == 1)
				curr_slice->abs_diff_pic_num_minus1[list][i] = reorder_cmd[j++];
			else if (val == 2)
				curr_slice->long_term_pic_idx[list][i] = reorder_cmd[j++];

			i++;

			if (i >= REORDERING_COMMAND_MAX_SIZE) {
				curr_slice->ref_pic_list_reordering_flag[list] = 0;
				break;
			};
			if (j > REORDER_CMD_MAX) {
				curr_slice->ref_pic_list_reordering_flag[list] = 0;
				break;
			};
		} while (val != 3);
	}
}

static void reorder_pics(struct aml_h264_ctx *h264_ctx,
			 struct slice *curr_slice, int cur_list)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	int *modification_of_pic_nums_idc =
		curr_slice->modification_of_pic_nums_idc[cur_list];
	int *abs_diff_pic_num_minus1 =
		curr_slice->abs_diff_pic_num_minus1[cur_list];
	int *long_term_pic_idx = curr_slice->long_term_pic_idx[cur_list];
	int pic_num_lx_nowarp, pic_num_lx_pred, pic_num_lx;
	int curr_pic_num = curr_slice->frame_num;
	int max_pic_num =
	    1 << (4 + h264_ctx->ctrl_ref.sps->log2_max_frame_num_minus4);
	int ref_idx_lx = 0;
	int nowarp_tmp = 0;
	int i;

	pic_num_lx_pred = curr_pic_num;
	for (i = 0; i < REORDERING_COMMAND_MAX_SIZE && modification_of_pic_nums_idc[i] != 3; i++) {
		if (modification_of_pic_nums_idc[i] > 3) {
			dev_info(&ctx->dev->plat_dev->dev, "error, Invalid modification_of_pic_nums_idc command\n");
			break;
		}

		if (modification_of_pic_nums_idc[i] < 2) {
			if (modification_of_pic_nums_idc[i] == 0) {
				nowarp_tmp = pic_num_lx_pred - (abs_diff_pic_num_minus1[i] + 1);
				pic_num_lx_nowarp = nowarp_tmp + (nowarp_tmp < 0 ? max_pic_num : 0);
			} else if (modification_of_pic_nums_idc[i] == 1) {
				nowarp_tmp = pic_num_lx_pred + (abs_diff_pic_num_minus1[i] + 1);
				pic_num_lx_nowarp = nowarp_tmp -
					(nowarp_tmp > max_pic_num ? max_pic_num : 0);
			}
			pic_num_lx_pred = pic_num_lx_nowarp;
			if (pic_num_lx_nowarp > curr_pic_num)
				pic_num_lx = pic_num_lx_nowarp - max_pic_num;
			else
				pic_num_lx = pic_num_lx_nowarp;

			reorder_short_term(curr_slice, cur_list, pic_num_lx, &ref_idx_lx);
		} else {
			reorder_long_term(curr_slice, cur_list, long_term_pic_idx[i], &ref_idx_lx);
		}
	}
}

static void copy_ref_list(struct aml_h264_ctx *h264_ctx, int curr_list)
{
	if (curr_list == 0)
		memcpy(h264_ctx->ref_list0_unreordered, h264_ctx->ref_list0,
		       sizeof(h264_ctx->ref_list0));
	else
		memcpy(h264_ctx->ref_list1_unreordered, h264_ctx->ref_list0,
		       sizeof(h264_ctx->ref_list1));
}

static void h264_reorder_reflists(struct aml_h264_ctx *h264_ctx)
{
	unsigned short *reorder_cmd;
	struct slice *curr_slice = &h264_ctx->mslice;

	if (curr_slice->slice_type != I_SLICE && curr_slice->slice_type != SI_SLICE) {
		reorder_cmd = &h264_ctx->dpb_param.mmco.l0_reorder_cmd[0];
		/* 3:parsed by ucode, means no reorder needed */
		if (reorder_cmd[0] != 3)
			curr_slice->ref_pic_list_reordering_flag[0] = 1;
		else
			curr_slice->ref_pic_list_reordering_flag[0] = 0;

		get_modification_cmd(reorder_cmd, curr_slice, 0);
	}

	if (curr_slice->slice_type == B_SLICE) {
		reorder_cmd = &h264_ctx->dpb_param.mmco.l1_reorder_cmd[0];
		/* 3:parsed by ucode, means no reorder needed */
		if (reorder_cmd[0] != 3)
			curr_slice->ref_pic_list_reordering_flag[1] = 1;
		else
			curr_slice->ref_pic_list_reordering_flag[1] = 0;

		get_modification_cmd(reorder_cmd, curr_slice, 1);
	}

	if (curr_slice->slice_type != I_SLICE &&
	    curr_slice->slice_type != SI_SLICE &&
	    curr_slice->ref_pic_list_reordering_flag[0] != 0) {
		copy_ref_list(h264_ctx, 0);
		reorder_pics(h264_ctx, curr_slice, 0);
	}

	if (curr_slice->slice_type == B_SLICE &&
	    curr_slice->ref_pic_list_reordering_flag[1] != 0) {
		copy_ref_list(h264_ctx, 1);
		reorder_pics(h264_ctx, curr_slice, 1);
	}
}

static void h264_config_ref_lists(struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)ctx->codec_priv;
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;
	struct v4l2_ctrl_h264_decode_params *decode =
	    (struct v4l2_ctrl_h264_decode_params *)ctrls->decode;
	struct v4l2_ctrl_h264_sps *sps =
	    (struct v4l2_ctrl_h264_sps *)ctrls->sps;
	const struct v4l2_h264_dpb_entry *dpb = decode->dpb;
	struct v4l2_h264_reflist_builder builder;
	struct v4l2_h264_reference v4l2_p0_reflist[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference v4l2_b0_reflist[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference v4l2_b1_reflist[V4L2_H264_REF_LIST_LEN];
	struct slice *curr_slice = &h264_ctx->mslice;

	if (decode->flags == V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC)
		return;

	v4l2_h264_init_reflist_builder(&builder, decode, sps, dpb);
	dev_dbg(&ctx->dev->plat_dev->dev, "%s num_valid = %d", __func__,
		builder.num_valid);

	if (curr_slice->slice_type == P_SLICE &&
	    (decode->flags & V4L2_H264_DECODE_PARAM_FLAG_PFRAME)) {
		v4l2_h264_build_p_ref_list(&builder, v4l2_p0_reflist);
		config_p_reflist(h264_ctx, v4l2_p0_reflist, builder.num_valid);
	} else if (curr_slice->slice_type == B_SLICE &&
		  (decode->flags & V4L2_H264_DECODE_PARAM_FLAG_BFRAME)) {
		v4l2_h264_build_b_ref_lists(&builder, v4l2_b0_reflist, v4l2_b1_reflist);
		config_b_reflist(h264_ctx, v4l2_b0_reflist, v4l2_b1_reflist,
				 builder.num_valid);
	}
}

static int allocate_canvas_pos(struct aml_h264_ctx *h264_ctx, int poc)
{
	int i;
	int ret = -1;
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;

	for (i = 0; i < (V4L2_H264_NUM_DPB_ENTRIES + 1); i++) {
		if (((h264_ctx->canvas_pos_map >> i) & 0x1) == 0) {
			h264_ctx->canvas_pos_map |= (1 << i);
			h264_ctx->ref_canvas[i].poc = poc;
			h264_ctx->ref_canvas[i].canvas_pos = i;
			ret = i;

			dev_dbg(&ctx->dev->plat_dev->dev,
				"%s i %d pos_poc %d\n", __func__, i,
				h264_ctx->ref_canvas[i].poc);
			break;
		}
	}

	return ret;
}

static void release_canvas_pos(struct aml_h264_ctx *h264_ctx, int index)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;

	if (index >= 0) {
		if (index > V4L2_H264_NUM_DPB_ENTRIES) {
			dev_dbg(&ctx->dev->plat_dev->dev,
				"%s error, index %d is bigger than buf count %d\n",
				__func__, index, h264_ctx->max_num_ref_frames);
		} else {
			if (h264_ctx->ref_canvas[index].poc != INVALID_POC &&
			    ((h264_ctx->canvas_pos_map >> index) & 0x1) ==
			    0x1) {
				h264_ctx->canvas_pos_map &= (~(1 << index));
				dev_dbg(&ctx->dev->plat_dev->dev,
					"%s canvas_pos index %d released poc %d, canvas_pos_map 0x%x\n",
					__func__, index, h264_ctx->ref_canvas[index].poc,
					h264_ctx->canvas_pos_map);
				h264_ctx->ref_canvas[index].poc = INVALID_POC;
				h264_ctx->ref_canvas[index].canvas_pos = -1;
			}
		}
	}
}

static int get_canvas_pos_by_poc(struct aml_h264_ctx *h264_ctx, int poc)
{
	int i;
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	int ret_pos = -1;

	for (i = 0; i < (V4L2_H264_NUM_DPB_ENTRIES + 1); i++) {
		if (h264_ctx->ref_canvas[i].poc == poc) {
			ret_pos = h264_ctx->ref_canvas[i].canvas_pos;
			dev_dbg(&ctx->dev->plat_dev->dev, "%s canvas_pos %d\n",
				__func__, ret_pos);
			return ret_pos;
		}
	}

	dev_dbg(&ctx->dev->plat_dev->dev,
		"%s error, no find canvas pos %d, poc %d\n", __func__, ret_pos, poc);

	return ret_pos;
}

static void clear_unused_col_buf(struct aml_h264_ctx *h264_ctx,
				 struct v4l2_ctrl_h264_decode_params *decode)
{
	int i, col_poc;

	/* flush all col buffers when IDR */
	if (decode->flags == V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC) {
		/* 32 : max index of co-locate buffer */
		for (i = 0; i < 32; i++)
			release_colocate_buf(h264_ctx, i);
		for (i = 0; i < (V4L2_H264_NUM_DPB_ENTRIES + 1); i++)
			release_canvas_pos(h264_ctx, i);
		return;
	}

	for (i = 0; i < h264_ctx->colocated_buf_num; i++) {
		col_poc = h264_ctx->colocated_buf_poc[i];
		if (col_poc != INVALID_POC &&
		    (poc_is_in_dpb(col_poc, decode->dpb) != 1))
			release_colocate_buf(h264_ctx, i);
	}

	for (i = 0; i < (V4L2_H264_NUM_DPB_ENTRIES + 1); i++) {
		col_poc = h264_ctx->ref_canvas[i].poc;
		if (col_poc != INVALID_POC &&
		    (poc_is_in_dpb(col_poc, decode->dpb) != 1))
			release_canvas_pos(h264_ctx, i);
	}
}

static void h264_config_decode_spec(struct aml_vdec_hw *hw, struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)hw->curr_ctx;
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;
	struct v4l2_ctrl_h264_decode_params *decode =
	    (struct v4l2_ctrl_h264_decode_params *)ctrls->decode;
	struct h264_decode_buf_spec *buf_spec_l0, *buf_spec_l1;
	struct vb2_buffer *vb;
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct vb2_queue *vq;
	int i;

	clear_unused_col_buf(h264_ctx, decode);

	vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	vb = &vb2_v4l2->vb2_buf;

	h264_ctx->curr_spec.y_dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	if (ctx->pic_info.plane_num > 1)
		h264_ctx->curr_spec.c_dma_addr =
		    vb2_dma_contig_plane_dma_addr(vb, 1);
	else
		h264_ctx->curr_spec.c_dma_addr =
		    h264_ctx->curr_spec.y_dma_addr + ctx->pic_info.fb_size[0];
	h264_ctx->curr_spec.canvas_pos =
	    allocate_canvas_pos(h264_ctx, decode->top_field_order_cnt);
	if (h264_ctx->curr_spec.canvas_pos < 0)
		dev_err(&ctx->dev->plat_dev->dev, "curr_spec.canvas error\n");

	if (decode->nal_ref_idc)
		h264_ctx->curr_spec.col_buf_index =
		    allocate_colocate_buf(h264_ctx,
					  decode->top_field_order_cnt);
	else
		h264_ctx->curr_spec.col_buf_index = -1;
	h264_ctx->curr_spec.poc = decode->top_field_order_cnt;

	h264_config_ref_lists(ctx);

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
		struct v4l2_h264_dpb_entry *dpb = &decode->dpb[i];

		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			break;

		buf_spec_l0 = find_spec_by_dpb_index(h264_ctx, i, 0);
		if (buf_spec_l0) {
			buf_spec_l0->canvas_pos =
			    get_canvas_pos_by_poc(h264_ctx,
						  dpb->top_field_order_cnt);
			if (buf_spec_l0->canvas_pos < 0) {
				dev_err(&ctx->dev->plat_dev->dev,
					"l0 canvas_pos %d error\n",
					buf_spec_l0->canvas_pos);
				continue;
			}
			vb = vb2_find_buffer(vq, dpb->reference_ts);
			if (!vb) {
				dev_err(&ctx->dev->plat_dev->dev,
					"ref pic for ts %llu lost\n", dpb->reference_ts);
				continue;
			}

			buf_spec_l0->y_dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, 0);
			if (ctx->pic_info.plane_num > 1)
				buf_spec_l0->c_dma_addr =
				    vb2_dma_contig_plane_dma_addr(vb, 1);
			else
				buf_spec_l0->c_dma_addr =
				    buf_spec_l0->y_dma_addr +
				    ctx->pic_info.fb_size[0];
			dev_dbg(&ctx->dev->plat_dev->dev,
				"config canvas for poc %d canvas %d y_dma_addr 0x%llx c_dma_addr 0x%llx\n",
				buf_spec_l0->dpb->top_field_order_cnt,
				buf_spec_l0->canvas_pos,
				buf_spec_l0->y_dma_addr,
				buf_spec_l0->c_dma_addr);
		}

		buf_spec_l1 = find_spec_by_dpb_index(h264_ctx, i, 1);
		if (!buf_spec_l0 && buf_spec_l1) {
			buf_spec_l1->canvas_pos =
			    get_canvas_pos_by_poc(h264_ctx,
						  dpb->top_field_order_cnt);
			if (buf_spec_l1->canvas_pos < 0) {
				dev_err(&ctx->dev->plat_dev->dev,
					"l1 canvas_pos %d error\n",
					buf_spec_l1->canvas_pos);
				continue;
			}
			vb = vb2_find_buffer(vq, dpb->reference_ts);
			if (!vb) {
				dev_err(&ctx->dev->plat_dev->dev,
					"ref pic for ts %llu lost\n", dpb->reference_ts);
				continue;
			}

			buf_spec_l1->y_dma_addr =
			    vb2_dma_contig_plane_dma_addr(vb, 0);
			if (ctx->pic_info.plane_num > 1)
				buf_spec_l1->c_dma_addr =
				    vb2_dma_contig_plane_dma_addr(vb, 1);
			else
				buf_spec_l1->c_dma_addr =
				    buf_spec_l1->y_dma_addr +
				    ctx->pic_info.fb_size[0];
			dev_dbg(&ctx->dev->plat_dev->dev,
				"config canvas for poc %d canvas %d y_dma_addr 0x%llx c_dma_addr 0x%llx\n",
				buf_spec_l1->dpb->top_field_order_cnt,
				buf_spec_l1->canvas_pos,
				buf_spec_l1->y_dma_addr,
				buf_spec_l1->c_dma_addr);
		} else if (buf_spec_l0 && buf_spec_l1) {
			memcpy(buf_spec_l1, buf_spec_l0,
			       sizeof(struct h264_decode_buf_spec));
			dev_dbg(&ctx->dev->plat_dev->dev,
				"config canvas for poc %d canvas %d y_dma_addr 0x%llx c_dma_addr 0x%llx\n",
				buf_spec_l1->dpb->top_field_order_cnt,
				buf_spec_l1->canvas_pos,
				buf_spec_l1->y_dma_addr,
				buf_spec_l1->c_dma_addr);
		}
	}
}

static int get_poc_by_canvas_pos(struct aml_h264_ctx *h264_ctx, int canvas_pos)
{
	int i;

	for (i = 0; i < (V4L2_H264_NUM_DPB_ENTRIES + 1); i++) {
		if (h264_ctx->ref_canvas[i].canvas_pos == canvas_pos)
			return h264_ctx->ref_canvas[i].poc;
	}
	return -1;
}

static struct v4l2_h264_dpb_entry *get_dpb_by_poc(struct v4l2_ctrl_h264_decode_params *decode,
						  int poc)
{
	int i;

	for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
		if (decode->dpb[i].top_field_order_cnt == poc)
			return &decode->dpb[i];
	}
	return NULL;
}

static int h264_config_decode_buf(struct aml_vdec_hw *hw,
				  struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)hw->curr_ctx;
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;
	struct v4l2_ctrl_h264_decode_params *decode =
	    (struct v4l2_ctrl_h264_decode_params *)ctrls->decode;
	unsigned int canvas_adr;
	unsigned int ref_cfg;
	unsigned int ref_cfg_once = 0;
	struct slice *curr_slice = &h264_ctx->mslice;
	unsigned int type_cfg = 0x3;	/* 0x3: frame type */
	unsigned int colocate_adr_offset = 0;
	unsigned int colocate_wr_adr;
	unsigned int info0;
	unsigned int info1;
	unsigned int info2;
	int i, j;
	int h264_buffer_info_data_write_count;
	u8 canvas_pos;
	u8 use_mode_8x8_flag;
	u32 reg_val;

	regmap_write(hw->map[DOS_BUS], H264_CURRENT_POC_IDX_RESET, 0);
	regmap_write(hw->map[DOS_BUS], H264_CURRENT_POC, decode->top_field_order_cnt);
	regmap_write(hw->map[DOS_BUS], H264_CURRENT_POC, decode->top_field_order_cnt);
	regmap_write(hw->map[DOS_BUS], H264_CURRENT_POC, decode->bottom_field_order_cnt);
	regmap_write(hw->map[DOS_BUS], CURR_CANVAS_CTRL, h264_ctx->curr_spec.canvas_pos << 24);
	regmap_read(hw->map[DOS_BUS], CURR_CANVAS_CTRL, &canvas_adr);
	canvas_adr &= 0xffffff;
	dev_dbg(hw->dev, "canvas_pos = %d canvas_adr 0x%x\n",
		h264_ctx->curr_spec.canvas_pos, canvas_adr);

	regmap_write(hw->map[DOS_BUS], REC_CANVAS_ADDR, canvas_adr);
	regmap_write(hw->map[DOS_BUS], DBKR_CANVAS_ADDR, canvas_adr);
	regmap_write(hw->map[DOS_BUS], DBKW_CANVAS_ADDR, canvas_adr);

	regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_INDEX, 16);

	for (j = 0; j < (V4L2_H264_NUM_DPB_ENTRIES + 1); j++) {
		int poc;
		struct v4l2_h264_dpb_entry *dpb = NULL;

		info0 = 0;
		info1 = 0;
		info2 = 0;

		poc = get_poc_by_canvas_pos(h264_ctx, j);
		if (poc == decode->top_field_order_cnt) {
			info0 = 0xf480 | 0xf;
			info1 = decode->top_field_order_cnt;
			info2 = decode->bottom_field_order_cnt;
			if (decode->bottom_field_order_cnt <
			    decode->top_field_order_cnt)
				info0 |= 0x100;
		} else {
			dpb = get_dpb_by_poc(decode, poc);
			if (dpb && (dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)) {
				info0 = 0xf480;
				if (dpb->bottom_field_order_cnt <
				    dpb->top_field_order_cnt)
					info0 |= 0x100;
				info1 = dpb->top_field_order_cnt;
				info2 = dpb->bottom_field_order_cnt;
				if (dpb->flags &
				    V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM)
					info0 |= ((1 << 5) | (1 << 4));
			}
		}

		regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, info0);
		regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, info1);
		regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, info2);
	}

	regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_INDEX, 0);
	/* when frame width <= 256, Disable DDR_BYTE64_CACHE */
	if (ctx->pic_info.coded_width <= 256) {
		regmap_update_bits(hw->map[DOS_BUS], IQIDCT_CONTROL, (1 << 16), (1 << 16));
		regmap_write(hw->map[DOS_BUS], DCAC_DDR_BYTE64_CTL,
			     (read_dos_reg(hw, DCAC_DDR_BYTE64_CTL) & (~0xf)) | 0xa);
	} else {
		regmap_update_bits(hw->map[DOS_BUS], IQIDCT_CONTROL, (1 << 16), 0);
		regmap_write(hw->map[DOS_BUS], DCAC_DDR_BYTE64_CTL,
			     (read_dos_reg(hw, DCAC_DDR_BYTE64_CTL) & (~0xf)));
	}

	ref_cfg = 0;
	j = 0;

	for (i = 0; i < h264_ctx->list_size[0]; i++) {
		canvas_pos = h264_ctx->ref_list0[i].canvas_pos;
		/* bit 0:3 canvas_pos bit 5:6 frame struct cfg */
		ref_cfg_once = (canvas_pos & 0x1f) | (type_cfg << 5);
		ref_cfg <<= 8;
		ref_cfg |= ref_cfg_once;
		j++;

		if (j == 4) {
			regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA,
				     ref_cfg);
			dev_dbg(hw->dev, "H264_BUFFER_INFO_DATA: %x\n",
				ref_cfg);
			h264_buffer_info_data_write_count++;
			j = 0;
		}
	}

	if (j != 0) {
		while (j != 4) {
			ref_cfg <<= 8;
			ref_cfg |= ref_cfg_once;
			j++;
		}
		regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, ref_cfg);
		dev_dbg(hw->dev, "H264_BUFFER_INFO_DATA: %x\n", ref_cfg);
		h264_buffer_info_data_write_count++;
	}
	ref_cfg = (ref_cfg_once << 24) | (ref_cfg_once << 16) |
	    (ref_cfg_once << 8) | ref_cfg_once;
	for (j = h264_buffer_info_data_write_count; j < 8; j++) {
		dev_dbg(hw->dev, "H264_BUFFER_INFO_DATA: %x\n", ref_cfg);
		regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, ref_cfg);
	}

	regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_INDEX, 8);
	j = 0;
	ref_cfg = 0;

	for (i = 0; i < h264_ctx->list_size[1]; i++) {
		canvas_pos = h264_ctx->ref_list1[i].canvas_pos;
		ref_cfg_once = (canvas_pos & 0x1f) | (type_cfg << 5);
		ref_cfg <<= 8;
		ref_cfg |= ref_cfg_once;
		j++;

		if (j == 4) {
			regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, ref_cfg);
			dev_dbg(hw->dev, "H264_BUFFER_INFO_DATA: %x\n", ref_cfg);
			j = 0;
		}
	}

	if (j != 0) {
		while (j != 4) {
			ref_cfg <<= 8;
			ref_cfg |= ref_cfg_once;
			j++;
		}
		dev_dbg(hw->dev, "H264_BUFFER_INFO_DATA: %x\n", ref_cfg);
		regmap_write(hw->map[DOS_BUS], H264_BUFFER_INFO_DATA, ref_cfg);
	}

	if (get_flag(ctrls->sps->flags, V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) &&
	    get_flag(ctrls->sps->flags, V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE))
		use_mode_8x8_flag = 1;
	else
		use_mode_8x8_flag = 0;

	read_poll_timeout(read_dos_reg, reg_val,
			  !(reg_val & 0x800),
			  10, 0, true, hw, H264_CO_MB_RW_CTL);

	/* col buf for curr frame */
	colocate_adr_offset = COL_SIZE_FOR_ONE_MB;
	if (use_mode_8x8_flag)
		colocate_adr_offset >>= 2;
	colocate_adr_offset *= curr_slice->first_mb_in_slice;

	if (h264_ctx->curr_spec.col_buf_index >= 0 &&
	    h264_ctx->curr_spec.col_buf_index < h264_ctx->colocated_buf_num) {
		colocate_wr_adr = h264_ctx->collated_cma_addr +
		    ((h264_ctx->one_col_buf_size *
		      h264_ctx->curr_spec.col_buf_index) >> (use_mode_8x8_flag ? 2 : 0));
		if (colocate_adr_offset > h264_ctx->one_col_buf_size ||
		    colocate_wr_adr + h264_ctx->one_col_buf_size >
		    h264_ctx->collated_cma_addr_end) {
			dev_err(hw->dev,
				"Error, colocate buf is not enough, index is %d\n",
				h264_ctx->curr_spec.col_buf_index);
			return -1;
		}
		regmap_write(hw->map[DOS_BUS], H264_CO_MB_WR_ADDR,
			     (colocate_wr_adr + colocate_adr_offset));
		dev_dbg(hw->dev, "col buffer addr = 0x%x col_buf_index %d\n",
			(colocate_wr_adr + colocate_adr_offset),
			h264_ctx->curr_spec.col_buf_index);
	} else {
		regmap_write(hw->map[DOS_BUS], H264_CO_MB_WR_ADDR, 0xffffffff);
		dev_dbg(hw->dev, "col buffer addr = 0xffffffff\n");
	}

	if (h264_ctx->list_size[1] > 0) {
		struct h264_decode_buf_spec *colocate_pic =
		    &h264_ctx->ref_list1[0];
		struct h264_decode_buf_spec *curr_pic = &h264_ctx->curr_spec;
		int l10_structure = 2;	/* for pic struct == FRAME, default to 2 */
		int cur_colocate_ref_type;
		unsigned int colocate_rd_adr;
		unsigned int colocate_rd_adr_offset = 0;
		unsigned int val;

		cur_colocate_ref_type =
		    (abs(curr_pic->poc - colocate_pic->dpb->top_field_order_cnt) <
		     abs(curr_pic->poc - colocate_pic->dpb->bottom_field_order_cnt)) ? 0 : 1;
		colocate_rd_adr_offset = COL_SIZE_FOR_ONE_MB;
		if (use_mode_8x8_flag)
			colocate_rd_adr_offset >>= 2;

		colocate_rd_adr_offset *= curr_slice->first_mb_in_slice;
		if (colocate_pic->col_buf_index >= 0 &&
		    colocate_pic->col_buf_index < h264_ctx->colocated_buf_num) {
			colocate_rd_adr = h264_ctx->collated_cma_addr +
			    ((h264_ctx->one_col_buf_size *
			      colocate_pic->col_buf_index) >> (use_mode_8x8_flag
							       ? 2 : 0));
			if (colocate_rd_adr + h264_ctx->one_col_buf_size >
			    h264_ctx->collated_cma_addr_end) {
				dev_err(hw->dev,
					"Error, colocate rd buf is not enough, index is %d\n",
					colocate_pic->col_buf_index);
				return -1;
			}
			val  = ((colocate_rd_adr_offset + colocate_rd_adr) >> 3) |
				(cur_colocate_ref_type << 29) |
				(l10_structure << 30);
			regmap_write(hw->map[DOS_BUS], H264_CO_MB_RD_ADDR, val);
		} else {
			dev_err
			    (hw->dev,
			     "Error, reference pic has no colocated buf poc %d\n",
			     curr_pic->poc);
			return -1;
		}
	}

	return 0;
}

static void get_canvas_index(struct aml_vdec_hw *hw, struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)hw->curr_ctx;
	int i;
	struct h264_decode_buf_spec *buf;

	config_decode_canvas(hw, &h264_ctx->curr_spec,
			     h264_ctx->mb_width, h264_ctx->mb_height);
	if (h264_ctx->list_size[0] > 0) {
		for (i = 0; i < h264_ctx->list_size[0]; i++) {
			buf = &h264_ctx->ref_list0[i];
			config_decode_canvas(hw, buf, h264_ctx->mb_width,
					     h264_ctx->mb_height);
		}
	}

	if (h264_ctx->list_size[1] > 0) {
		for (i = 0; i < h264_ctx->list_size[1]; i++) {
			buf = &h264_ctx->ref_list1[i];
			config_decode_canvas(hw, buf, h264_ctx->mb_width,
					     h264_ctx->mb_height);
		}
	}
}

static void release_canvas_index(struct aml_vdec_hw *hw,
				 struct h264_decode_buf_spec *buf)
{
	if (buf->y_canvas_index >= 0) {
		dev_dbg(hw->dev, "free y_canvas %d\n", buf->y_canvas_index);
		meson_canvas_free(hw->canvas, buf->y_canvas_index);
		buf->y_canvas_index = -1;
	}

	if (buf->u_canvas_index >= 0) {
		dev_dbg(hw->dev, "free uv_canvas_index %d\n",
			buf->u_canvas_index);
		meson_canvas_free(hw->canvas, buf->u_canvas_index);
		buf->u_canvas_index = -1;
		buf->v_canvas_index = -1;
	}
}

static void h264_release_decode_spec(struct aml_vdec_hw *hw, struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)hw->curr_ctx;
	int i;
	struct h264_decode_buf_spec *buf;

	release_canvas_index(hw, &h264_ctx->curr_spec);

	if (h264_ctx->list_size[0] > 0) {
		for (i = 0; i < h264_ctx->list_size[0]; i++) {
			buf = &h264_ctx->ref_list0[i];
			if (buf->used) {
				buf->dpb = NULL;
				release_canvas_index(hw, buf);
				buf->used = 0;
			}
		}
		h264_ctx->list_size[0] = 0;
	}

	if (h264_ctx->list_size[1] > 0) {
		for (i = 0; i < h264_ctx->list_size[1]; i++) {
			buf = &h264_ctx->ref_list1[i];
			if (buf->used) {
				buf->dpb = NULL;
				release_canvas_index(hw, buf);
				buf->used = 0;
			}
		}
		h264_ctx->list_size[1] = 0;
	}
}

static void save_reg_status(struct aml_h264_ctx *h264_ctx)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);

	regmap_read(hw->map[DOS_BUS], IQIDCT_CONTROL, &h264_ctx->reg_iqidct_control);
	h264_ctx->reg_iqidct_control_init_flag = 1;
	regmap_read(hw->map[DOS_BUS], VCOP_CTRL_REG, &h264_ctx->reg_vcop_ctrl_reg);
	regmap_read(hw->map[DOS_BUS], RV_AI_MB_COUNT, &h264_ctx->reg_rv_ai_mb_count);
	regmap_read(hw->map[DOS_BUS], VLD_DECODE_CONTROL, &h264_ctx->vld_dec_control);
}

static void h264_get_slice_params(struct aml_h264_ctx *h264_ctx)
{
	struct slice *curr_slice = &h264_ctx->mslice;

	memset(curr_slice, 0, sizeof(struct slice));
	/* parsed by ucode */
	switch (h264_ctx->dpb_param.l.data[SLICE_TYPE]) {
	case I_Slice:
		curr_slice->slice_type = I_SLICE;
		break;
	case P_Slice:
		curr_slice->slice_type = P_SLICE;
		break;
	case B_Slice:
		curr_slice->slice_type = B_SLICE;
		break;
	default:
		curr_slice->slice_type = MAX_SLICE_TYPES;
		break;
	}

	curr_slice->first_mb_in_slice =
	    h264_ctx->dpb_param.l.data[FIRST_MB_IN_SLICE];
	curr_slice->num_ref_idx_l0 =
	    h264_ctx->dpb_param.dpb.num_ref_idx_l0_active_minus1 + 1;
	curr_slice->num_ref_idx_l1 =
	    h264_ctx->dpb_param.dpb.num_ref_idx_l1_active_minus1 + 1;
	curr_slice->frame_num = h264_ctx->ctrl_ref.decode->frame_num;
}

static irqreturn_t h264_isr(int irq, void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;

	regmap_write(dev->dec_hw->map[DOS_BUS], VDEC_ASSIST_MBOX1_CLR_REG, 1);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t h264_threaded_isr_func(int irq, void *priv)
{
	u32 dec_status;
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)dev->dec_hw->curr_ctx;
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)dev->dec_ctx;
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);
	unsigned short *p = (unsigned short *)h264_ctx->lmem_addr;
	int i, ii;

	regmap_read(hw->map[DOS_BUS], DPB_STATUS_REG, &dec_status);
	h264_ctx->dec_status = dec_status;
	dev_dbg
	    (&dev->plat_dev->dev,
	     "%s, dec_status 0x%x VIFF_BIT_CNT 0x%x MBY_MBX 0x%x VLD_SHIFT_STATUS 0x%x\n",
	     __func__, dec_status, read_dos_reg(hw, VIFF_BIT_CNT),
	     read_dos_reg(hw, MBY_MBX), read_dos_reg(hw, VLD_SHIFT_STATUS));

	regmap_read(hw->map[DOS_BUS], AV_SCRATCH_F, &h264_ctx->save_avscratch_f);

	switch (dec_status) {
	case H264_SLICE_HEADER_DONE:
		for (i = 0; i < 0x400; i += 4)
			for (ii = 0; ii < 4; ii++)
				h264_ctx->dpb_param.l.data[i + ii] = p[i + 3 - ii];
		save_reg_status(h264_ctx);
		h264_get_slice_params(h264_ctx);
		if (h264_ctx->mslice.first_mb_in_slice != 0)
			h264_release_decode_spec(hw, ctx);

		h264_config_decode_spec(hw, ctx);
		h264_reorder_reflists(h264_ctx);
		get_canvas_index(hw, ctx);

		if (h264_config_decode_buf(hw, ctx) < 0) {
			h264_release_decode_spec(hw, ctx);
			ctx->int_cond = 1;
			wake_up_interruptible(&ctx->queue);
			goto irq_handled;
		}
		if (h264_ctx->new_pic_flag == 1) {
			regmap_write(hw->map[DOS_BUS], DPB_STATUS_REG, H264_ACTION_DECODE_NEWPIC);
			dev_dbg(&dev->plat_dev->dev, "action decode new pic\n");
			h264_ctx->new_pic_flag = 0;
		} else {
			regmap_write(hw->map[DOS_BUS], DPB_STATUS_REG, H264_ACTION_DECODE_SLICE);
			dev_dbg(&dev->plat_dev->dev, "action decode new slice\n");
		}
		break;
	case H264_SLICE_DATA_DONE:
		h264_release_decode_spec(hw, ctx);
		h264_ctx->decode_pic_count++;
		ctx->int_cond = 1;
		v4l2_m2m_buf_done_and_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx,
						 VB2_BUF_STATE_DONE);
		wake_up_interruptible(&ctx->queue);
		break;
	default:
		h264_release_decode_spec(hw, ctx);
		ctx->int_cond = 1;
		v4l2_m2m_buf_done_and_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx,
						 VB2_BUF_STATE_ERROR);
		wake_up_interruptible(&ctx->queue);
		break;
	}
irq_handled:
	return IRQ_HANDLED;
}

static int h264_restore_hw_ctx(struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)ctx->codec_priv;
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);

	regmap_write(hw->map[DOS_BUS], POWER_CTL_VLD,
		     (read_dos_reg(hw, POWER_CTL_VLD) | (0 << 10) | (1 << 9) | (1 << 6)));

	regmap_write(hw->map[DOS_BUS], PSCALE_CTRL, 0);

	/* clear mailbox interrupt */
	regmap_write(hw->map[DOS_BUS], VDEC_ASSIST_MBOX1_CLR_REG, 1);

	/* enable mailbox interrupt */
	regmap_write(hw->map[DOS_BUS], VDEC_ASSIST_MBOX1_MASK, 1);

	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL, (1 << 17), (1 << 17));
	if (ctx->dec_fmt[AML_FMT_DST].fourcc == V4L2_PIX_FMT_NV21 ||
	    ctx->dec_fmt[AML_FMT_DST].fourcc == V4L2_PIX_FMT_NV21M)
		regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL,
				   (1 << 16), (1 << 16));
	else
		regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL, (1 << 16), 0);

	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL,
			   (0xbf << 24), (0xbf << 24));
	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL, (0xbf << 24), 0);
	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL, (1 << 31), 0);

	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_MUX_CTRL, (1 << 31), 0);
	regmap_write(hw->map[DOS_BUS], MDEC_EXTIF_CFG1, 0);
	regmap_write(hw->map[DOS_BUS], MDEC_PIC_DC_THRESH, 0x404038aa);

	regmap_write(hw->map[DOS_BUS], DPB_STATUS_REG, 0);

	regmap_write(hw->map[DOS_BUS], LMEM_DUMP_ADR, h264_ctx->lmem_phy_addr);
	regmap_write(hw->map[DOS_BUS], FRAME_COUNTER_REG, h264_ctx->decode_pic_count);
	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_8, h264_ctx->workspace_offset);
	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_G, h264_ctx->mc_cpu_paddr);

	regmap_write(hw->map[DOS_BUS], AV_SCRATCH_F,
		     ((h264_ctx->save_avscratch_f & 0xffffffc3) | (1 << 4)));
	regmap_update_bits(hw->map[DOS_BUS], AV_SCRATCH_F, (1 << 6), 0);

	regmap_write(hw->map[DOS_BUS], MDEC_PIC_DC_THRESH, 0x404038aa);

	if (h264_ctx->reg_iqidct_control_init_flag == 0)
		regmap_write(hw->map[DOS_BUS], IQIDCT_CONTROL, 0x200);

	if (h264_ctx->reg_iqidct_control)
		regmap_write(hw->map[DOS_BUS], IQIDCT_CONTROL, h264_ctx->reg_iqidct_control);

	if (h264_ctx->reg_vcop_ctrl_reg)
		regmap_write(hw->map[DOS_BUS], VCOP_CTRL_REG, h264_ctx->reg_vcop_ctrl_reg);

	if (h264_ctx->vld_dec_control)
		regmap_write(hw->map[DOS_BUS], VLD_DECODE_CONTROL, h264_ctx->vld_dec_control);

	dev_dbg
	    (hw->dev,
	     "IQIDCT_CONTROL = 0x%x, VCOP_CTRL_REG 0x%x VLD_DECODE_CONTROL 0x%x\n",
	     read_dos_reg(hw, IQIDCT_CONTROL), read_dos_reg(hw, VCOP_CTRL_REG),
	     read_dos_reg(hw, VLD_DECODE_CONTROL));

	return 0;
}

static void *aml_h264_get_ctrl(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_find(hdl, id);
	return ctrl ? ctrl->p_cur.p : NULL;
}

static int aml_h264_get_stateless_ctrl_ref(struct aml_h264_ctx *h264_ctx)
{
	struct aml_vdec_ctx *ctx = h264_ctx->v4l2_ctx;
	struct vdec_h264_stateless_ctrl_ref *ctrls = &h264_ctx->ctrl_ref;

	ctrls->sps =
		(struct v4l2_ctrl_h264_sps *)aml_h264_get_ctrl(&ctx->ctrl_handler,
			V4L2_CID_STATELESS_H264_SPS);
	if (WARN_ON(!ctrls->sps))
		return -EINVAL;

	ctrls->pps =
		(struct v4l2_ctrl_h264_pps *)aml_h264_get_ctrl(&ctx->ctrl_handler,
			V4L2_CID_STATELESS_H264_PPS);
	if (WARN_ON(!ctrls->pps))
		return -EINVAL;

	ctrls->decode =
		(struct v4l2_ctrl_h264_decode_params *)aml_h264_get_ctrl(&ctx->ctrl_handler,
			V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	if (WARN_ON(!ctrls->decode))
		return -EINVAL;

	ctrls->scaling =
		(struct v4l2_ctrl_h264_scaling_matrix *)aml_h264_get_ctrl(&ctx->ctrl_handler,
			V4L2_CID_STATELESS_H264_SCALING_MATRIX);
	if (WARN_ON(!ctrls->scaling))
		return -EINVAL;

	return 0;
}

static void copy_mc_cpu_fw(void *mc_cpu_addr, const u8 *data)
{
	/*header */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_HEADER,
	       data + 0x4000, MC_SWAP_SIZE);
	/*data */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_DATA,
	       data + 0x2000, MC_SWAP_SIZE);
	/*mmco */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_MMCO,
	       data + 0x6000, MC_SWAP_SIZE);
	/*list */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_LIST,
	       data + 0x3000, MC_SWAP_SIZE);
	/*slice */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_SLICE,
	       data + 0x5000, MC_SWAP_SIZE);
	/*main */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_MAIN, data, 0x2000);
	/*data */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_MAIN + 0x2000,
	       data + 0x2000, 0x1000);
	/*slice */
	memcpy((u8 *)mc_cpu_addr + MC_OFFSET_MAIN + 0x3000,
	       data + 0x5000, 0x1000);
}

static int aml_h264_load_fw_ext(void *priv, const u8 *data, u32 len)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)priv;
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)h264_ctx->v4l2_ctx;
	struct aml_vdec_hw *dec_hw;

	if (h264_ctx->mc_cpu_loaded)
		return 0;

	dec_hw = vdec_get_hw(ctx->dev);
	if (!dec_hw)
		return -1;

	if (len > MC_TOTAL_SIZE) {
		dev_info(dec_hw->dev, "size of mc_cpu_fw id invalid\n");
		return -1;
	}

	h264_ctx->mc_cpu_vaddr = dma_alloc_coherent(dec_hw->dev, MC_TOTAL_SIZE,
						    &h264_ctx->mc_cpu_paddr,
						    GFP_KERNEL);
	if (!h264_ctx->mc_cpu_vaddr)
		return -ENOMEM;

	copy_mc_cpu_fw(h264_ctx->mc_cpu_vaddr, data);

	h264_ctx->mc_cpu_loaded = true;

	dev_dbg(dec_hw->dev, "h264 mccpu fw loaded\n");

	return 0;
}

int aml_h264_init(void *priv)
{
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)priv;
	struct aml_vdec_hw *dec_hw;
	struct aml_h264_ctx *h264_ctx;
	int ret = 0;

	h264_ctx = kzalloc(sizeof(*h264_ctx), GFP_KERNEL);
	if (!h264_ctx)
		return -ENOMEM;

	h264_ctx->v4l2_ctx = ctx;
	dec_hw = vdec_get_hw(ctx->dev);
	if (!dec_hw)
		return -1;

	h264_ctx->mc_cpu_loaded = false;
	dec_hw->hw_ops.irq_handler = h264_isr;
	dec_hw->hw_ops.irq_threaded_func = h264_threaded_isr_func;
	dec_hw->hw_ops.load_firmware_ex = aml_h264_load_fw_ext;

	h264_ctx->lmem_addr = dma_alloc_coherent(dec_hw->dev, LMEM_DUMP_SIZE,
						 &h264_ctx->lmem_phy_addr,
						 GFP_KERNEL);
	if (!h264_ctx->lmem_addr) {
		ret = -ENOMEM;
		goto err_alloc_lmem;
	}

	h264_ctx->cma_alloc_vaddr =
	    dma_alloc_coherent(dec_hw->dev, V_BUF_ADDR_OFFSET,
			       &h264_ctx->cma_alloc_addr, GFP_KERNEL);
	if (!h264_ctx->cma_alloc_vaddr) {
		ret = -ENOMEM;
		goto err_alloc_workspace;
	}

	h264_ctx->workspace_offset = h264_ctx->cma_alloc_addr + DCAC_READ_MARGIN;
	h264_ctx->workspace_vaddr = h264_ctx->cma_alloc_vaddr + DCAC_READ_MARGIN;

	ctx->codec_priv = h264_ctx;
	dec_hw->curr_ctx = h264_ctx;
	h264_ctx->col_buf_alloc_size = 0;
	h264_ctx->init_flag = 0;
	h264_ctx->new_pic_flag = 0;
	h264_ctx->param_set = 0;
	h264_ctx->reg_iqidct_control_init_flag = 0;
	h264_ctx->decode_pic_count = 0;

	return 0;

err_alloc_workspace:
	dma_free_coherent(dec_hw->dev, LMEM_DUMP_SIZE,
			  h264_ctx->lmem_addr, h264_ctx->lmem_phy_addr);
err_alloc_lmem:
	kfree(h264_ctx);

	return ret;
}

void aml_h264_exit(void *priv)
{
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)priv;
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)ctx->codec_priv;
	struct aml_vdec_hw *dec_hw;

	if (!h264_ctx) {
		dev_info(&ctx->dev->plat_dev->dev,
			 "h264 decoder is already destroyed or not created!\n");
		return;
	}
	dec_hw = vdec_get_hw(ctx->dev);
	h264_ctx->param_set = 0;

	if (ctx->dos_clk_en)
		aml_stop_vdec_hw(dec_hw);

	if (h264_ctx->collated_cma_vaddr) {
		dma_free_coherent(dec_hw->dev, h264_ctx->col_buf_alloc_size,
				  h264_ctx->collated_cma_vaddr,
				  h264_ctx->collated_cma_addr);
		h264_ctx->col_buf_alloc_size = 0;
	}

	if (h264_ctx->mc_cpu_vaddr) {
		dma_free_coherent(dec_hw->dev, MC_TOTAL_SIZE,
				  h264_ctx->mc_cpu_vaddr,
				  h264_ctx->mc_cpu_paddr);
		h264_ctx->mc_cpu_loaded = false;
	}

	if (h264_ctx->lmem_addr)
		dma_free_coherent(dec_hw->dev, LMEM_DUMP_SIZE,
				  h264_ctx->lmem_addr, h264_ctx->lmem_phy_addr);

	if (h264_ctx->cma_alloc_vaddr)
		dma_free_coherent(dec_hw->dev, V_BUF_ADDR_OFFSET,
				  h264_ctx->cma_alloc_vaddr,
				  h264_ctx->cma_alloc_addr);

	kfree(ctx->codec_priv);
	dec_hw->curr_ctx = NULL;
	ctx->codec_priv = NULL;
}

static void config_decode_mode(struct aml_vdec_ctx *ctx)
{
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)ctx->codec_priv;
	struct aml_vdec_hw *hw = vdec_get_hw(ctx->dev);

	regmap_write(hw->map[DOS_BUS], H264_DECODE_MODE, 0x1);	/*decode mode framebase */
	regmap_write(hw->map[DOS_BUS], HEAD_PADDING_REG, 0);
	regmap_write(hw->map[DOS_BUS], H264_DECODE_SEQINFO, h264_ctx->seq_info);
	regmap_write(hw->map[DOS_BUS], INIT_FLAG_REG, 1);
}

int aml_h264_dec_run(void *priv)
{
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)priv;
	struct aml_h264_ctx *h264_ctx = (struct aml_h264_ctx *)ctx->codec_priv;
	struct aml_vdec_hw *dec_hw = vdec_get_hw(ctx->dev);
	int ret = -1;
	int i;

	ret = aml_h264_get_stateless_ctrl_ref(h264_ctx);
	if (ret < 0) {
		dev_err(&ctx->dev->plat_dev->dev, "not ctrl ref for h264 decoder\n");
		return ret;
	}

	h264_ctx->new_pic_flag = 1;
	h264_config_params(ctx);

	if (h264_prepare_input(ctx) < 0)
		return ret;

	if (alloc_colocate_cma(h264_ctx, ctx) < 0)
		return ret;

	h264_restore_hw_ctx(ctx);

	config_decode_mode(ctx);
	/* enable stream input hardware */
	regmap_update_bits(dec_hw->map[DOS_BUS], VLD_MEM_VIFIFO_CONTROL, 0x6, 0x6);
	/* enable hardware timer */
	regmap_write(dec_hw->map[DOS_BUS], NAL_SEARCH_CTL,
		     read_dos_reg(dec_hw, NAL_SEARCH_CTL) | (1 << 16));
	regmap_write(dec_hw->map[DOS_BUS], MDEC_EXTIF_CFG2,
		     read_dos_reg(dec_hw, MDEC_EXTIF_CFG2) | 0x20);
	regmap_write(dec_hw->map[DOS_BUS], NAL_SEARCH_CTL,
		     read_dos_reg(dec_hw, NAL_SEARCH_CTL) & (~0x2));
	regmap_update_bits(dec_hw->map[DOS_BUS], VDEC_ASSIST_MMC_CTRL1,
			   (1 << 3), 0);

	aml_start_vdec_hw(dec_hw);
	h264_ctx->init_flag = 1;

	regmap_write(dec_hw->map[DOS_BUS], DPB_STATUS_REG, H264_ACTION_SEARCH_HEAD);

	ret = wait_event_interruptible_timeout(ctx->queue, ctx->int_cond,
					       msecs_to_jiffies(DECODER_TIMEOUT_MS));
	ctx->int_cond = 0;
	if (!ret) {
		ret = -1;
		dev_err(&ctx->dev->plat_dev->dev, "dec timeout=%u\n", DECODER_TIMEOUT_MS);
		for (i = 0; i < 16; i++) {	/* 16 : show ucode PC 16 times when timeout */
			dev_info(&ctx->dev->plat_dev->dev, "decoder timeout, pc 0x%x\n",
				 read_dos_reg(dec_hw, MPC_E));
			usleep_range(10, 20);
		}
		h264_release_decode_spec(dec_hw, ctx);
	} else if (-ERESTARTSYS == ret) {
		ret = -1;
		h264_release_decode_spec(dec_hw, ctx);
		dev_err(&ctx->dev->plat_dev->dev, "dec inter fail\n");
	}

	aml_stop_vdec_hw(dec_hw);
	h264_ctx->init_flag = 0;

	return ret;
}
