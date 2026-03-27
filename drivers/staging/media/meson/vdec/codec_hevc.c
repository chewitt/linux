// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc.h"
#include "dos_regs.h"
#include "hevc_regs.h"
#include "vdec_helpers.h"
#include "codec_hevc_common.h"

/* HEVC reg mapping */
#define HEVC_DEC_STATUS_REG	HEVC_ASSIST_SCRATCH_0
	#define HEVC_ACTION_DONE	0xff
#define HEVC_RPM_BUFFER		HEVC_ASSIST_SCRATCH_1
#define HEVC_SHORT_TERM_RPS	HEVC_ASSIST_SCRATCH_2
#define HEVC_VPS_BUFFER		HEVC_ASSIST_SCRATCH_3
#define HEVC_SPS_BUFFER		HEVC_ASSIST_SCRATCH_4
#define HEVC_PPS_BUFFER		HEVC_ASSIST_SCRATCH_5
#define HEVC_SAO_UP		HEVC_ASSIST_SCRATCH_6
#define HEVC_STREAM_SWAP_BUFFER HEVC_ASSIST_SCRATCH_7
#define H265_MMU_MAP_BUFFER	HEVC_ASSIST_SCRATCH_7
#define HEVC_STREAM_SWAP_BUFFER2 HEVC_ASSIST_SCRATCH_8
#define HEVC_sao_mem_unit	HEVC_ASSIST_SCRATCH_9
#define HEVC_SAO_ABV		HEVC_ASSIST_SCRATCH_A
#define HEVC_sao_vb_size	HEVC_ASSIST_SCRATCH_B
#define HEVC_SAO_VB		HEVC_ASSIST_SCRATCH_C
#define HEVC_SCALELUT		HEVC_ASSIST_SCRATCH_D
#define HEVC_WAIT_FLAG		HEVC_ASSIST_SCRATCH_E
#define RPM_CMD_REG		HEVC_ASSIST_SCRATCH_F
#define LMEM_DUMP_ADR		HEVC_ASSIST_SCRATCH_F
#define DEBUG_REG1		HEVC_ASSIST_SCRATCH_G
#define HEVC_DECODE_MODE2	HEVC_ASSIST_SCRATCH_H
#define NAL_SEARCH_CTL		HEVC_ASSIST_SCRATCH_I
#define HEVC_DECODE_MODE	HEVC_ASSIST_SCRATCH_J
	#define DECODE_MODE_SINGLE 0
#define DECODE_STOP_POS		HEVC_ASSIST_SCRATCH_K
#define HEVC_AUX_ADR		HEVC_ASSIST_SCRATCH_L
#define HEVC_AUX_DATA_SIZE	HEVC_ASSIST_SCRATCH_M
#define HEVC_DECODE_SIZE	HEVC_ASSIST_SCRATCH_N

#define AMRISC_MAIN_REQ		 0x04

/* HEVC Constants */
#define MAX_REF_PIC_NUM		24
#define MAX_REF_ACTIVE		16
#define MAX_TILE_COL_NUM	10
#define MAX_TILE_ROW_NUM	20
#define MAX_SLICE_NUM		800
/*
 * INVALID_POC is used as a sentinel for "no valid POC yet".
 * 0x80000000 == INT_MIN as s32, which can never be a real H.265 POC value
 * (the spec limits POC to a range well within signed 32-bit).
 */
#define INVALID_POC		((s32)0x80000000)

/* HEVC Workspace layout */
#define MPRED_MV_BUF_SIZE 0x120000

#define IPP_SIZE	0x4000
#define SAO_ABV_SIZE	0x30000
#define SAO_VB_SIZE	0x30000
#define SH_TM_RPS_SIZE	0x800
#define VPS_SIZE	0x800
#define SPS_SIZE	0x800
#define PPS_SIZE	0x2000
#define SAO_UP_SIZE	0x2800
#define SWAP_BUF_SIZE	0x800
#define SWAP_BUF2_SIZE	0x800
#define SCALELUT_SIZE	0x8000
#define DBLK_PARA_SIZE	0x20000
#define DBLK_DATA_SIZE	0x80000
#define DBLK_DATA2_SIZE	0x80000
#define MMU_VBH_SIZE	0x5000
#define MPRED_ABV_SIZE	0x8000
#define MPRED_MV_SIZE	(MPRED_MV_BUF_SIZE * MAX_REF_PIC_NUM)
#define RPM_BUF_SIZE	0x100
#define LMEM_SIZE	0xA00

#define IPP_OFFSET       0x00
#define SAO_ABV_OFFSET   (IPP_OFFSET + IPP_SIZE)
#define SAO_VB_OFFSET    (SAO_ABV_OFFSET + SAO_ABV_SIZE)
#define SH_TM_RPS_OFFSET (SAO_VB_OFFSET + SAO_VB_SIZE)
#define VPS_OFFSET       (SH_TM_RPS_OFFSET + SH_TM_RPS_SIZE)
#define SPS_OFFSET       (VPS_OFFSET + VPS_SIZE)
#define PPS_OFFSET       (SPS_OFFSET + SPS_SIZE)
#define SAO_UP_OFFSET    (PPS_OFFSET + PPS_SIZE)
#define SWAP_BUF_OFFSET  (SAO_UP_OFFSET + SAO_UP_SIZE)
#define SWAP_BUF2_OFFSET (SWAP_BUF_OFFSET + SWAP_BUF_SIZE)
#define SCALELUT_OFFSET  (SWAP_BUF2_OFFSET + SWAP_BUF2_SIZE)
#define DBLK_PARA_OFFSET (SCALELUT_OFFSET + SCALELUT_SIZE)
#define DBLK_DATA_OFFSET (DBLK_PARA_OFFSET + DBLK_PARA_SIZE)
#define DBLK_DATA2_OFFSET (DBLK_DATA_OFFSET + DBLK_DATA_SIZE)
#define MMU_VBH_OFFSET   (DBLK_DATA2_OFFSET + DBLK_DATA2_SIZE)
#define MPRED_ABV_OFFSET (MMU_VBH_OFFSET + MMU_VBH_SIZE)
#define MPRED_MV_OFFSET  (MPRED_ABV_OFFSET + MPRED_ABV_SIZE)
#define RPM_OFFSET       (MPRED_MV_OFFSET + MPRED_MV_SIZE)
#define LMEM_OFFSET      (RPM_OFFSET + RPM_BUF_SIZE)

/* ISR decode status */
#define HEVC_DEC_IDLE                        0x0
#define HEVC_NAL_UNIT_VPS                    0x1
#define HEVC_NAL_UNIT_SPS                    0x2
#define HEVC_NAL_UNIT_PPS                    0x3
#define HEVC_NAL_UNIT_CODED_SLICE_SEGMENT    0x4
#define HEVC_CODED_SLICE_SEGMENT_DAT         0x5
#define HEVC_SLICE_DECODING                  0x6
#define HEVC_NAL_UNIT_SEI                    0x7
#define HEVC_SLICE_SEGMENT_DONE              0x8
#define HEVC_NAL_SEARCH_DONE                 0x9
#define HEVC_DECPIC_DATA_DONE                0xa
#define HEVC_DECPIC_DATA_ERROR               0xb
#define HEVC_SEI_DAT                         0xc
#define HEVC_SEI_DAT_DONE                    0xd

/* RPM misc_flag0 */
#define PCM_LOOP_FILTER_DISABLED_FLAG_BIT		0
#define PCM_ENABLE_FLAG_BIT				1
#define LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT	2
#define PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT	3
#define DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_BIT	4
#define PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT		5
#define DEBLOCKING_FILTER_OVERRIDE_FLAG_BIT		6
#define SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT	7
#define SLICE_SAO_LUMA_FLAG_BIT				8
#define SLICE_SAO_CHROMA_FLAG_BIT			9
#define SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT 10

/* Constants for HEVC_MPRED_CTRL1 */
#define AMVP_MAX_NUM_CANDS_MEM	3
#define AMVP_MAX_NUM_CANDS	2
#define NUM_CHROMA_MODE		5
#define DM_CHROMA_IDX		36

/* Buffer sizes */
#define SIZE_WORKSPACE ALIGN(LMEM_OFFSET + LMEM_SIZE, 64 * SZ_1K)
#define SIZE_AUX (SZ_1K * 16)
#define SIZE_FRAME_MMU (0x1200 * 4)
#define RPM_SIZE 0x80
#define RPS_USED_BIT 14

/*
 * DVB streams are live and may be joined mid-broadcast. We must tolerate
 * missing reference frames until a clean random-access point is found.
 * After this many consecutive reference-lookup failures we force a resync
 * so a temporary glitch cannot lock us into a permanently broken state.
 */
#define HEVC_MAX_CONSECUTIVE_REF_ERRORS  8

/* Data received from the HW in this form, do not rearrange */
union rpm_param {
	struct {
		u16 data[RPM_SIZE];
	} l;
	struct {
		u16 CUR_RPS[MAX_REF_ACTIVE];
		u16 num_ref_idx_l0_active;
		u16 num_ref_idx_l1_active;
		u16 slice_type;
		u16 slice_temporal_mvp_enable_flag;
		u16 dependent_slice_segment_flag;
		u16 slice_segment_address;
		u16 num_title_rows_minus1;
		u16 pic_width_in_luma_samples;
		u16 pic_height_in_luma_samples;
		u16 log2_min_coding_block_size_minus3;
		u16 log2_diff_max_min_coding_block_size;
		u16 log2_max_pic_order_cnt_lsb_minus4;
		u16 POClsb;
		u16 collocated_from_l0_flag;
		u16 collocated_ref_idx;
		u16 log2_parallel_merge_level;
		u16 five_minus_max_num_merge_cand;
		u16 sps_num_reorder_pics_0;
		u16 modification_flag;
		u16 tiles_flags;
		u16 num_tile_columns_minus1;
		u16 num_tile_rows_minus1;
		u16 tile_width[8];
		u16 tile_height[8];
		u16 misc_flag0;
		u16 pps_beta_offset_div2;
		u16 pps_tc_offset_div2;
		u16 slice_beta_offset_div2;
		u16 slice_tc_offset_div2;
		u16 pps_cb_qp_offset;
		u16 pps_cr_qp_offset;
		u16 first_slice_segment_in_pic_flag;
		u16 m_temporalId;
		u16 m_nalUnitType;
		u16 vui_num_units_in_tick_hi;
		u16 vui_num_units_in_tick_lo;
		u16 vui_time_scale_hi;
		u16 vui_time_scale_lo;
		u16 bit_depth;
		u16 profile_etc;
		u16 sei_frame_field_info;
		u16 video_signal_type;
		u16 modification_list[0x20];
		u16 conformance_window_flag;
		u16 conf_win_left_offset;
		u16 conf_win_right_offset;
		u16 conf_win_top_offset;
		u16 conf_win_bottom_offset;
		u16 chroma_format_idc;
		u16 color_description;
		u16 aspect_ratio_idc;
		u16 sar_width;
		u16 sar_height;
	} p;
};

enum nal_unit_type {
	NAL_UNIT_CODED_SLICE_BLA	= 16,
	NAL_UNIT_CODED_SLICE_BLANT	= 17,
	NAL_UNIT_CODED_SLICE_BLA_N_LP	= 18,
	NAL_UNIT_CODED_SLICE_IDR	= 19,
	NAL_UNIT_CODED_SLICE_IDR_N_LP	= 20,
	/*
	 * CRA (Clean Random Access) — H.265 NAL type 21.
	 * Broadcast/DVB encoders frequently insert CRA frames as entry
	 * points instead of full IDRs to save bandwidth.  The decoder
	 * may produce one "broken link" picture immediately after the CRA
	 * while it rebuilds its DPB, but subsequent frames are clean.
	 * Treat it as a sync point so live DVB viewers get a picture
	 * quickly rather than waiting for the next IDR.
	 */
	NAL_UNIT_CODED_SLICE_CRA	= 21,
};

enum slice_type {
	B_SLICE = 0,
	P_SLICE = 1,
	I_SLICE = 2,
};

/* A frame being decoded */
struct hevc_frame {
	struct list_head list;
	struct vb2_v4l2_buffer *vbuf;
	u32 offset;
	/*
	 * POC (Picture Order Count) is a signed quantity in the H.265 spec.
	 * After a CRA the first B-frame GOP contains frames with negative POCs
	 * (e.g. -10, -14).  Using u32 here caused those to wrap to ~4 billion,
	 * making the display-order sort and the reorder-buffer guard in
	 * codec_hevc_show_frames() completely wrong and preventing any frame
	 * from ever being output.  All POC storage and arithmetic must be s32.
	 */
	s32 poc;

	int referenced;
	int show;
	u32 num_reorder_pic;

	u32 cur_slice_idx;
	u32 cur_slice_type;

	/* 2 lists (L0/L1) ; 800 slices ; 16 refs */
	s32 ref_poc_list[2][MAX_SLICE_NUM][MAX_REF_ACTIVE];
	u32 ref_num[2];
};

struct codec_hevc {
	/* Protect the data structure */
	struct mutex lock;

	/* Common part of the HEVC decoder */
	struct codec_hevc_common common;

	/* Buffer for the HEVC Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;

	/* AUX buffer */
	void      *aux_vaddr;
	dma_addr_t aux_paddr;

	/* Contains many information parsed from the bitstream */
	union rpm_param rpm_param;

	/* Information computed from the RPM */
	u32 lcu_size; // Largest Coding Unit
	u32 lcu_x_num;
	u32 lcu_y_num;
	u32 lcu_total;

	/* Current Frame being handled */
	struct hevc_frame *cur_frame;
	s32 curr_poc;
	/* Collocated Reference Picture */
	struct hevc_frame *col_frame;
	s32 col_poc;

	/* All ref frames used by the HW at a given time */
	struct list_head ref_frames_list;
	u32 frames_num;

	/* Coded resolution reported by the hardware */
	u32 width, height;
	/* Resolution minus the conformance window offsets */
	u32 dst_width, dst_height;

	s32 prev_tid0_poc;
	u32 slice_segment_addr;
	u32 slice_addr;
	u32 ldc_flag;

	/* Whether we detected the bitstream as 10-bit */
	int is_10bit;

	/*
	 * DVB / live-stream robustness state
	 *
	 * stream_synced:
	 *   Set to true once we have successfully decoded an IDR, BLA or CRA
	 *   frame.  While false, incoming slices are discarded so we never
	 *   attempt to decode a P/B frame whose reference pictures do not
	 *   exist in the DPB.
	 *
	 * consecutive_ref_errors:
	 *   Incremented every time codec_hevc_set_ref_list() cannot find a
	 *   required reference frame in ref_frames_list.  Reset to zero on
	 *   every successful slice decode.  When it reaches
	 *   HEVC_MAX_CONSECUTIVE_REF_ERRORS we assume the stream has had a
	 *   discontinuity and force a resync so we do not keep accumulating
	 *   garbage DPB entries forever.
	 *
	 * needs_resync:
	 *   Latching flag set by codec_hevc_set_ref_list() when the error
	 *   threshold is exceeded.  Checked at the top of the ISR so that
	 *   the full reset path (which must run with hevc->lock held) is
	 *   taken exactly once per discontinuity event.
	 */
	bool stream_synced;
	u32  consecutive_ref_errors;
	bool needs_resync;
};

static u32 codec_hevc_num_pending_bufs(struct amvdec_session *sess)
{
	struct codec_hevc *hevc;
	u32 ret;

	hevc = sess->priv;
	if (!hevc)
		return 0;

	mutex_lock(&hevc->lock);
	ret = hevc->frames_num;
	mutex_unlock(&hevc->lock);

	return ret;
}

/* Update the L0 and L1 reference lists for a given frame */
static void codec_hevc_update_frame_refs(struct amvdec_session *sess,
					 struct hevc_frame *frame)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *params = &hevc->rpm_param;
	int num_ref_idx_l0_active =
		(params->p.num_ref_idx_l0_active > MAX_REF_ACTIVE) ?
		MAX_REF_ACTIVE : params->p.num_ref_idx_l0_active;
	int num_ref_idx_l1_active =
		(params->p.num_ref_idx_l1_active > MAX_REF_ACTIVE) ?
		MAX_REF_ACTIVE : params->p.num_ref_idx_l1_active;
	int ref_picset0[MAX_REF_ACTIVE] = { 0 };
	int ref_picset1[MAX_REF_ACTIVE] = { 0 };
	u16 *mod_list = params->p.modification_list;
	int num_neg = 0;
	int num_pos = 0;
	int total_num;
	int i;

	for (i = 0; i < MAX_REF_ACTIVE; i++) {
		frame->ref_poc_list[0][frame->cur_slice_idx][i] = 0;
		frame->ref_poc_list[1][frame->cur_slice_idx][i] = 0;
	}

	for (i = 0; i < MAX_REF_ACTIVE; i++) {
		u16 cur_rps = params->p.CUR_RPS[i];
		int delt = cur_rps & ((1 << (RPS_USED_BIT - 1)) - 1);

		if (cur_rps & 0x8000)
			break;

		if (!((cur_rps >> RPS_USED_BIT) & 1))
			continue;

		if ((cur_rps >> (RPS_USED_BIT - 1)) & 1) {
			ref_picset0[num_neg] =
			       frame->poc - ((1 << (RPS_USED_BIT - 1)) - delt);
			num_neg++;
		} else {
			ref_picset1[num_pos] = frame->poc + delt;
			num_pos++;
		}
	}

	total_num = num_neg + num_pos;

	if (total_num <= 0)
		goto end;

	for (i = 0; i < num_ref_idx_l0_active; i++) {
		int cidx;

		if (params->p.modification_flag & 0x1)
			cidx = mod_list[i];
		else
			cidx = i % total_num;

		frame->ref_poc_list[0][frame->cur_slice_idx][i] =
			cidx >= num_neg ? ref_picset1[cidx - num_neg] :
			ref_picset0[cidx];
	}

	if (params->p.slice_type != B_SLICE)
		goto end;

	if (params->p.modification_flag & 0x2) {
		for (i = 0; i < num_ref_idx_l1_active; i++) {
			int cidx;

			if (params->p.modification_flag & 0x1)
				cidx = mod_list[num_ref_idx_l0_active + i];
			else
				cidx = mod_list[i];

			frame->ref_poc_list[1][frame->cur_slice_idx][i] =
				(cidx >= num_pos) ? ref_picset0[cidx - num_pos]
				: ref_picset1[cidx];
		}
	} else {
		for (i = 0; i < num_ref_idx_l1_active; i++) {
			int cidx = i % total_num;

			frame->ref_poc_list[1][frame->cur_slice_idx][i] =
				cidx >= num_pos ? ref_picset0[cidx - num_pos] :
				ref_picset1[cidx];
		}
	}

end:
	frame->ref_num[0] = num_ref_idx_l0_active;
	frame->ref_num[1] = num_ref_idx_l1_active;

	dev_dbg(sess->core->dev,
		"Frame %d; slice %u; slice_type %u; num_l0 %u; num_l1 %u\n",
		frame->poc, frame->cur_slice_idx, params->p.slice_type,
		frame->ref_num[0], frame->ref_num[1]);
}

static void codec_hevc_update_ldc_flag(struct codec_hevc *hevc)
{
	struct hevc_frame *frame = hevc->cur_frame;
	u32 slice_type = frame->cur_slice_type;
	u32 slice_idx = frame->cur_slice_idx;
	int i;

	hevc->ldc_flag = 0;

	if (slice_type == I_SLICE)
		return;

	hevc->ldc_flag = 1;
	for (i = 0; (i < frame->ref_num[0]) && hevc->ldc_flag; i++) {
		if (frame->ref_poc_list[0][slice_idx][i] > frame->poc) {
			hevc->ldc_flag = 0;
			break;
		}
	}

	if (slice_type == P_SLICE)
		return;

	for (i = 0; (i < frame->ref_num[1]) && hevc->ldc_flag; i++) {
		if (frame->ref_poc_list[1][slice_idx][i] > frame->poc) {
			hevc->ldc_flag = 0;
			break;
		}
	}
}

/* Tag "old" frames that are no longer referenced */
static void codec_hevc_update_referenced(struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	struct hevc_frame *frame;
	int i;
	s32 curr_poc = hevc->curr_poc;

	list_for_each_entry(frame, &hevc->ref_frames_list, list) {
		int is_referenced = 0;
		s32 poc_tmp;

		if (!frame->referenced)
			continue;

		for (i = 0; i < MAX_REF_ACTIVE; i++) {
			int delt;

			if (param->p.CUR_RPS[i] & 0x8000)
				break;

			delt = param->p.CUR_RPS[i] &
			       ((1 << (RPS_USED_BIT - 1)) - 1);
			if (param->p.CUR_RPS[i] & (1 << (RPS_USED_BIT - 1))) {
				poc_tmp = curr_poc -
				      ((1 << (RPS_USED_BIT - 1)) - delt);
			} else {
				poc_tmp = curr_poc + delt;
			}

			if (poc_tmp == frame->poc) {
				is_referenced = 1;
				break;
			}
		}

		frame->referenced = is_referenced;
	}
}

static struct hevc_frame *
codec_hevc_get_next_ready_frame(struct codec_hevc *hevc)
{
	struct hevc_frame *tmp, *ret = NULL;
	s32 poc = INT_MAX;

	list_for_each_entry(tmp, &hevc->ref_frames_list, list) {
		if ((tmp->poc < poc) && tmp->show) {
			ret = tmp;
			poc = tmp->poc;
		}
	}

	return ret;
}

/* Try to output as many frames as possible */
static void codec_hevc_show_frames(struct amvdec_session *sess)
{
	struct hevc_frame *tmp, *n;
	struct codec_hevc *hevc = sess->priv;

	while ((tmp = codec_hevc_get_next_ready_frame(hevc))) {
		/*
		 * Reorder buffer guard: hold back frames until enough have
		 * accumulated to satisfy the display-reorder window, OR until
		 * curr_poc is valid (non-zero after the first IDR).
		 *
		 * curr_poc is s32; comparing it against 0 with != keeps the
		 * original intent (skip the guard at stream start when poc=0)
		 * while working correctly for negative POC values that arise
		 * in B-frame GOPs after a CRA.
		 */
		if (hevc->curr_poc != 0 &&
		    (hevc->frames_num <= tmp->num_reorder_pic))
			break;

		dev_dbg(sess->core->dev, "DONE frame poc %d; vbuf %u\n",
			tmp->poc, tmp->vbuf->vb2_buf.index);
		amvdec_dst_buf_done_offset(sess, tmp->vbuf, tmp->offset,
					   V4L2_FIELD_NONE, 0, false);

		tmp->show = 0;
		hevc->frames_num--;
	}

	/* clean output frame buffer */
	list_for_each_entry_safe(tmp, n, &hevc->ref_frames_list, list) {
		if (tmp->referenced || tmp->show)
			continue;

		list_del(&tmp->list);
		kfree(tmp);
	}
}

static int
codec_hevc_setup_workspace(struct amvdec_session *sess,
			   struct codec_hevc *hevc)
{
	struct amvdec_core *core = sess->core;
	u32 revision = core->platform->revision;
	dma_addr_t wkaddr;

	/* Allocate some memory for the HEVC decoder's state */
	hevc->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						   &wkaddr, GFP_KERNEL);
	if (!hevc->workspace_vaddr)
		return -ENOMEM;

	hevc->workspace_paddr = wkaddr;

	amvdec_write_dos(core, HEVCD_IPP_LINEBUFF_BASE, wkaddr + IPP_OFFSET);
	amvdec_write_dos(core, HEVC_RPM_BUFFER, wkaddr + RPM_OFFSET);
	amvdec_write_dos(core, HEVC_SHORT_TERM_RPS, wkaddr + SH_TM_RPS_OFFSET);
	amvdec_write_dos(core, HEVC_VPS_BUFFER, wkaddr + VPS_OFFSET);
	amvdec_write_dos(core, HEVC_SPS_BUFFER, wkaddr + SPS_OFFSET);
	amvdec_write_dos(core, HEVC_PPS_BUFFER, wkaddr + PPS_OFFSET);
	amvdec_write_dos(core, HEVC_SAO_UP, wkaddr + SAO_UP_OFFSET);

	if (codec_hevc_use_mmu(revision, sess->pixfmt_cap, hevc->is_10bit)) {
		amvdec_write_dos(core, HEVC_SAO_MMU_VH0_ADDR,
				 wkaddr + MMU_VBH_OFFSET);
		amvdec_write_dos(core, HEVC_SAO_MMU_VH1_ADDR,
				 wkaddr + MMU_VBH_OFFSET + (MMU_VBH_SIZE / 2));

		if (revision >= VDEC_REVISION_G12A)
			amvdec_write_dos(core, HEVC_ASSIST_MMU_MAP_ADDR,
					 hevc->common.mmu_map_paddr);
		else
			amvdec_write_dos(core, H265_MMU_MAP_BUFFER,
					 hevc->common.mmu_map_paddr);
	} else if (revision < VDEC_REVISION_G12A) {
		amvdec_write_dos(core, HEVC_STREAM_SWAP_BUFFER,
				 wkaddr + SWAP_BUF_OFFSET);
		amvdec_write_dos(core, HEVC_STREAM_SWAP_BUFFER2,
				 wkaddr + SWAP_BUF2_OFFSET);
	}

	amvdec_write_dos(core, HEVC_SCALELUT, wkaddr + SCALELUT_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG4, wkaddr + DBLK_PARA_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG5, wkaddr + DBLK_DATA_OFFSET);
	if (revision >= VDEC_REVISION_G12A)
		amvdec_write_dos(core, HEVC_DBLK_CFGE,
				 wkaddr + DBLK_DATA2_OFFSET);

	amvdec_write_dos(core, LMEM_DUMP_ADR, wkaddr + LMEM_OFFSET);

	return 0;
}

static int codec_hevc_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc;
	u32 val;
	int i;
	int ret;

	hevc = kzalloc(sizeof(*hevc), GFP_KERNEL);
	if (!hevc)
		return -ENOMEM;

	INIT_LIST_HEAD(&hevc->ref_frames_list);
	hevc->curr_poc = INVALID_POC;

	/*
	 * DVB: do not attempt to decode until we lock onto an IDR/BLA/CRA.
	 * This prevents spurious "Couldn't find ref. frame" warnings and
	 * corrupted output when the decoder is started mid-broadcast.
	 */
	hevc->stream_synced        = false;
	hevc->consecutive_ref_errors = 0;
	hevc->needs_resync         = false;

	ret = codec_hevc_setup_workspace(sess, hevc);
	if (ret)
		goto free_hevc;

	val = BIT(0); /* stream_fetch_enable */
	if (core->platform->revision >= VDEC_REVISION_G12A)
		val |= (0xf << 25); /* arwlen_axi_max */
	amvdec_write_dos_bits(core, HEVC_STREAM_CONTROL, val);

	val = amvdec_read_dos(core, HEVC_PARSER_INT_CONTROL) & 0x03ffffff;
	val |= (3 << 29) | BIT(27) | BIT(24) | BIT(22) | BIT(7) | BIT(4) |
	       BIT(0);
	amvdec_write_dos(core, HEVC_PARSER_INT_CONTROL, val);
	amvdec_write_dos_bits(core, HEVC_SHIFT_STATUS, BIT(1) | BIT(0));
	amvdec_write_dos(core, HEVC_SHIFT_CONTROL,
			 (3 << 6) | BIT(5) | BIT(2) | BIT(0));
	amvdec_write_dos(core, HEVC_CABAC_CONTROL, 1);
	amvdec_write_dos(core, HEVC_PARSER_CORE_CONTROL, 1);
	amvdec_write_dos(core, HEVC_DEC_STATUS_REG, 0);

	amvdec_write_dos(core, HEVC_IQIT_SCALELUT_WR_ADDR, 0);
	for (i = 0; i < 1024; ++i)
		amvdec_write_dos(core, HEVC_IQIT_SCALELUT_DATA, 0);

	amvdec_write_dos(core, HEVC_DECODE_SIZE, 0);

	amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE, BIT(16));
	for (i = 0; i < ARRAY_SIZE(vdec_hevc_parser_cmd); ++i)
		amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE,
				 vdec_hevc_parser_cmd[i]);

	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);
	amvdec_write_dos(core, HEVC_PARSER_IF_CONTROL,
			 BIT(5) | BIT(2) | BIT(0));

	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(0));
	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(1));

	amvdec_write_dos(core, HEVC_WAIT_FLAG, 1);

	/* clear mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_CLR_REG, 1);
	/* enable mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 1);
	/* disable PSCALE for hardware sharing */
	amvdec_write_dos(core, HEVC_PSCALE_CTRL, 0);
	/* Let the uCode do all the parsing */
	amvdec_write_dos(core, NAL_SEARCH_CTL, 0xc);

	amvdec_write_dos(core, DECODE_STOP_POS, 0);
	amvdec_write_dos(core, HEVC_DECODE_MODE, DECODE_MODE_SINGLE);
	amvdec_write_dos(core, HEVC_DECODE_MODE2, 0);

	/* AUX buffers */
	hevc->aux_vaddr = dma_alloc_coherent(core->dev, SIZE_AUX,
					     &hevc->aux_paddr, GFP_KERNEL);
	if (!hevc->aux_vaddr) {
		ret = -ENOMEM;
		goto free_hevc;
	}

	amvdec_write_dos(core, HEVC_AUX_ADR, hevc->aux_paddr);
	amvdec_write_dos(core, HEVC_AUX_DATA_SIZE,
			 (((SIZE_AUX) >> 4) << 16) | 0);
	mutex_init(&hevc->lock);
	sess->priv = hevc;

	return 0;

free_hevc:
	kfree(hevc);
	return ret;
}

static void codec_hevc_flush_output(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *tmp, *n;

	while ((tmp = codec_hevc_get_next_ready_frame(hevc))) {
		amvdec_dst_buf_done(sess, tmp->vbuf, V4L2_FIELD_NONE, 0);
		tmp->show = 0;
		hevc->frames_num--;
	}

	list_for_each_entry_safe(tmp, n, &hevc->ref_frames_list, list) {
		list_del(&tmp->list);
		kfree(tmp);
	}
}

static int codec_hevc_stop(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	mutex_lock(&hevc->lock);
	codec_hevc_flush_output(sess);

	if (hevc->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  hevc->workspace_vaddr,
				  hevc->workspace_paddr);

	if (hevc->aux_vaddr)
		dma_free_coherent(core->dev, SIZE_AUX,
				  hevc->aux_vaddr, hevc->aux_paddr);

	codec_hevc_free_fbc_buffers(sess, &hevc->common);
	mutex_unlock(&hevc->lock);
	mutex_destroy(&hevc->lock);

	return 0;
}

static struct hevc_frame *
codec_hevc_get_frame_by_poc(struct codec_hevc *hevc, s32 poc)
{
	struct hevc_frame *tmp;

	list_for_each_entry(tmp, &hevc->ref_frames_list, list) {
		if (tmp->poc == poc)
			return tmp;
	}

	return NULL;
}

static struct hevc_frame *
codec_hevc_prepare_new_frame(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct hevc_frame *new_frame = NULL;
	struct codec_hevc *hevc = sess->priv;
	struct vb2_v4l2_buffer *vbuf;
	union rpm_param *params = &hevc->rpm_param;

	new_frame = kzalloc(sizeof(*new_frame), GFP_KERNEL);
	if (!new_frame)
		return NULL;

	vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
	if (!vbuf) {
		dev_err(sess->core->dev, "No dst buffer available\n");
		kfree(new_frame);
		return NULL;
	}

	new_frame->vbuf = vbuf;
	new_frame->referenced = 1;
	new_frame->show = 1;
	new_frame->poc = hevc->curr_poc;
	new_frame->cur_slice_type = params->p.slice_type;
	new_frame->num_reorder_pic = params->p.sps_num_reorder_pics_0;
	new_frame->offset = amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT);

	list_add_tail(&new_frame->list, &hevc->ref_frames_list);
	hevc->frames_num++;

	return new_frame;
}

/*
 * codec_hevc_is_random_access_point - test whether the current NAL unit is a
 * valid starting point for a new decode sequence.
 *
 * IDR and BLA variants unconditionally clear the DPB so they are always safe.
 * CRA (Clean Random Access, type 21) is also a valid entry point: broadcast
 * encoders insert them frequently and they carry a complete intra-coded
 * picture.  The first inter frame after a CRA may reference pictures that
 * preceded the CRA in display order (so-called "RASL" frames); those will
 * produce one round of missing-reference warnings, but subsequent frames
 * decode cleanly.  Accepting CRA here is therefore the right trade-off for
 * live DVB: viewers get a picture within a few hundred milliseconds rather
 * than waiting for the encoder's next full IDR.
 */
static bool codec_hevc_is_random_access_point(struct codec_hevc *hevc)
{
	u32 nal = hevc->rpm_param.p.m_nalUnitType;

	return nal == NAL_UNIT_CODED_SLICE_IDR     ||
	       nal == NAL_UNIT_CODED_SLICE_IDR_N_LP ||
	       nal == NAL_UNIT_CODED_SLICE_BLA      ||
	       nal == NAL_UNIT_CODED_SLICE_BLANT    ||
	       nal == NAL_UNIT_CODED_SLICE_BLA_N_LP ||
	       nal == NAL_UNIT_CODED_SLICE_CRA;
}

/*
 * codec_hevc_reset_stream_state - flush the DPB and reset all POC tracking.
 *
 * Called when a stream discontinuity is detected (e.g. after a DVB channel
 * change, a transport-stream splice, or too many consecutive missing-reference
 * errors).  After this call the decoder is in the same state as after
 * codec_hevc_start(), waiting for the next random-access point before it will
 * accept any inter frames.
 *
 * Must be called with hevc->lock held.
 */
static void codec_hevc_reset_stream_state(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;

	dev_dbg(sess->core->dev,
		"HEVC: stream discontinuity detected — resetting DPB and POC state\n");

	/*
	 * Flush all frames that are ready to be displayed so the V4L2
	 * framework gets its buffers back.  Frames that are referenced but
	 * not yet shown (display-reorder buffer) are freed below; the core
	 * recycles their vbufs when the session restarts or they fall out of
	 * the M2M queue naturally.
	 */
	codec_hevc_flush_output(sess);

	/* Reset all POC tracking — stale values cause wrong ref-list deltas */
	hevc->curr_poc      = INVALID_POC;
	hevc->prev_tid0_poc = 0;
	hevc->col_poc       = INVALID_POC;
	hevc->cur_frame     = NULL;
	hevc->col_frame     = NULL;

	/* Reset error counters and re-arm sync detection */
	hevc->consecutive_ref_errors = 0;
	hevc->needs_resync            = false;
	hevc->stream_synced           = false;
}

static void
codec_hevc_set_sao(struct amvdec_session *sess, struct hevc_frame *frame)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	struct vb2_buffer *vb = &frame->vbuf->vb2_buf;
	union rpm_param *param = &hevc->rpm_param;
	u32 pic_height_cu =
		(hevc->height + hevc->lcu_size - 1) / hevc->lcu_size;
	u32 sao_mem_unit = (hevc->lcu_size == 16 ? 9 :
			   hevc->lcu_size == 32 ? 14 : 24) << 4;
	u32 sao_vb_size = (sao_mem_unit + (2 << 4)) * pic_height_cu;
	u32 misc_flag0 = param->p.misc_flag0;
	dma_addr_t buf_y_paddr;
	dma_addr_t buf_u_v_paddr;
	u32 slice_deblocking_filter_disabled_flag;
	u32 val, val_2;

	val = (amvdec_read_dos(core, HEVC_SAO_CTRL0) & ~0xf) |
	      ilog2(hevc->lcu_size);
	amvdec_write_dos(core, HEVC_SAO_CTRL0, val);

	amvdec_write_dos(core, HEVC_SAO_PIC_SIZE,
			 hevc->width | (hevc->height << 16));
	amvdec_write_dos(core, HEVC_SAO_PIC_SIZE_LCU,
			 (hevc->lcu_x_num - 1) | (hevc->lcu_y_num - 1) << 16);

	if (codec_hevc_use_downsample(sess->pixfmt_cap, hevc->is_10bit) ||
	    codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       hevc->is_10bit))
		buf_y_paddr =
		     hevc->common.fbc_buffer_paddr[vb->index];
	else
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);

	if (codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
		val = amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0000;
		amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
		amvdec_write_dos(core, HEVC_CM_BODY_START_ADDR, buf_y_paddr);
	}

	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M) {
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);
		buf_u_v_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 1);
		amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR, buf_y_paddr);
		amvdec_write_dos(core, HEVC_SAO_C_START_ADDR, buf_u_v_paddr);
		amvdec_write_dos(core, HEVC_SAO_Y_WPTR, buf_y_paddr);
		amvdec_write_dos(core, HEVC_SAO_C_WPTR, buf_u_v_paddr);
	}

	if (codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       hevc->is_10bit)) {
		dma_addr_t header_adr = vb2_dma_contig_plane_dma_addr(vb, 0);

		if (codec_hevc_use_downsample(sess->pixfmt_cap, hevc->is_10bit))
			header_adr = hevc->common.mmu_header_paddr[vb->index];
		amvdec_write_dos(core, HEVC_CM_HEADER_START_ADDR, header_adr);
		/* use HEVC_CM_HEADER_START_ADDR */
		amvdec_write_dos_bits(core, HEVC_SAO_CTRL5, BIT(10));
		amvdec_write_dos_bits(core, HEVC_SAO_CTRL9, BIT(0));
	}

	amvdec_write_dos(core, HEVC_SAO_Y_LENGTH,
			 amvdec_get_output_size(sess));
	amvdec_write_dos(core, HEVC_SAO_C_LENGTH,
			 (amvdec_get_output_size(sess) / 2));

	if (frame->cur_slice_idx == 0) {
		if (core->platform->revision >= VDEC_REVISION_G12A) {
			if (core->platform->revision >= VDEC_REVISION_SM1)
				val = 0xfc << 8;
			else
				val = 0x54 << 8;

			/* enable first, compressed write */
			if (codec_hevc_use_fbc(sess->pixfmt_cap,
					       hevc->is_10bit))
				val |= BIT(8);

			/* enable second, uncompressed write */
			if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M)
				val |= BIT(9);

			/* dblk pipeline mode=1 for performance */
			if (hevc->width >= 1280)
				val |= BIT(4);

			amvdec_write_dos(core, HEVC_DBLK_CFGB, val);
			amvdec_write_dos(core, HEVC_DBLK_STS1 + 16, BIT(28));
		}

		amvdec_write_dos(core, HEVC_DBLK_CFG2,
				 hevc->width | (hevc->height << 16));

		val = 0;
		if ((misc_flag0 >> PCM_ENABLE_FLAG_BIT) & 0x1)
			val |= ((misc_flag0 >>
				 PCM_LOOP_FILTER_DISABLED_FLAG_BIT) & 0x1) << 3;

		val |= (param->p.pps_cb_qp_offset & 0x1f) << 4;
		val |= (param->p.pps_cr_qp_offset & 0x1f) << 9;
		val |= (hevc->lcu_size == 64) ? 0 :
		       ((hevc->lcu_size == 32) ? 1 : 2);
		amvdec_write_dos(core, HEVC_DBLK_CFG1, val);
	}

	val = amvdec_read_dos(core, HEVC_SAO_CTRL1) & ~0x3ff3;
	val |= 0xff0; /* Set endianness for 2-bytes swaps (nv12) */
	if (core->platform->revision < VDEC_REVISION_G12A) {
		if (!codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit))
			val |= BIT(0); /* disable cm compression */
		/* TOFIX: Handle Amlogic Framebuffer compression */
	}

	amvdec_write_dos(core, HEVC_SAO_CTRL1, val);

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
		/* no downscale for NV12 */
		val = amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0000;
		amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
	}

	val = amvdec_read_dos(core, HEVCD_IPP_AXIIF_CONFIG) & ~0x30;
	val |= 0xf;
	amvdec_write_dos(core, HEVCD_IPP_AXIIF_CONFIG, val);

	val = 0;
	val_2 = amvdec_read_dos(core, HEVC_SAO_CTRL0);
	val_2 &= (~0x300);

	slice_deblocking_filter_disabled_flag = (misc_flag0 >>
			SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT) & 0x1;
	if ((misc_flag0 & (1 << DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_BIT)) &&
	    (misc_flag0 & (1 << DEBLOCKING_FILTER_OVERRIDE_FLAG_BIT))) {
		val |= slice_deblocking_filter_disabled_flag << 2;

		if (!slice_deblocking_filter_disabled_flag) {
			val |= (param->p.slice_beta_offset_div2 & 0xf) << 3;
			val |= (param->p.slice_tc_offset_div2 & 0xf) << 7;
		}
	} else {
		val |=
			((misc_flag0 >>
			  PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT) & 0x1) << 2;

		if (((misc_flag0 >> PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT) &
			0x1) == 0) {
			val |= (param->p.pps_beta_offset_div2 & 0xf) << 3;
			val |= (param->p.pps_tc_offset_div2 & 0xf) << 7;
		}
	}
	if ((misc_flag0 & (1 << PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)) &&
	    ((misc_flag0 & (1 << SLICE_SAO_LUMA_FLAG_BIT)) ||
	   (misc_flag0 & (1 << SLICE_SAO_CHROMA_FLAG_BIT)) ||
	   (!slice_deblocking_filter_disabled_flag))) {
		val |=
			((misc_flag0 >>
			  SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			 & 0x1)	<< 1;
		val_2 |=
			((misc_flag0 >>
			  SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			& 0x1) << 9;
	} else {
		val |=
			((misc_flag0 >>
			  PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			 & 0x1) << 1;
		val_2 |=
			((misc_flag0 >>
			  PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			 & 0x1) << 9;
	}

	amvdec_write_dos(core, HEVC_DBLK_CFG9, val);
	amvdec_write_dos(core, HEVC_SAO_CTRL0, val_2);

	amvdec_write_dos(core, HEVC_sao_mem_unit, sao_mem_unit);
	amvdec_write_dos(core, HEVC_SAO_ABV,
			 hevc->workspace_paddr + SAO_ABV_OFFSET);
	amvdec_write_dos(core, HEVC_sao_vb_size, sao_vb_size);
	amvdec_write_dos(core, HEVC_SAO_VB,
			 hevc->workspace_paddr + SAO_VB_OFFSET);
}

static dma_addr_t codec_hevc_get_frame_mv_paddr(struct codec_hevc *hevc,
						struct hevc_frame *frame)
{
	return hevc->workspace_paddr + MPRED_MV_OFFSET +
		(frame->vbuf->vb2_buf.index * MPRED_MV_BUF_SIZE);
}

static void
codec_hevc_set_mpred_ctrl(struct amvdec_core *core, struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	u32 slice_type = param->p.slice_type;
	u32 lcu_size_log2 = ilog2(hevc->lcu_size);
	u32 val;

	val = slice_type |
	      MPRED_CTRL0_ABOVE_EN |
	      MPRED_CTRL0_MV_WR_EN |
	      MPRED_CTRL0_BUF_LINEAR |
	      (lcu_size_log2 << 16) |
	      (3 << 20) | /* cu_size_log2 */
	      (param->p.log2_parallel_merge_level << 24);

	if (slice_type != I_SLICE)
		val |= MPRED_CTRL0_MV_RD_EN;

	if (param->p.collocated_from_l0_flag)
		val |= MPRED_CTRL0_COL_FROM_L0;

	if (param->p.slice_temporal_mvp_enable_flag)
		val |= MPRED_CTRL0_TMVP;

	if (hevc->ldc_flag)
		val |= MPRED_CTRL0_LDC;

	if (param->p.dependent_slice_segment_flag)
		val |= MPRED_CTRL0_NEW_SLI_SEG;

	if (param->p.slice_segment_address == 0)
		val |= MPRED_CTRL0_NEW_PIC |
		       MPRED_CTRL0_NEW_TILE;

	amvdec_write_dos(core, HEVC_MPRED_CTRL0, val);

	val = (5 - param->p.five_minus_max_num_merge_cand) |
	      (AMVP_MAX_NUM_CANDS << 4) |
	      (AMVP_MAX_NUM_CANDS_MEM << 8) |
	      (NUM_CHROMA_MODE << 12) |
	      (DM_CHROMA_IDX << 16);
	amvdec_write_dos(core, HEVC_MPRED_CTRL1, val);
}

static void codec_hevc_set_mpred_mv(struct amvdec_core *core,
				    struct codec_hevc *hevc,
				    struct hevc_frame *frame,
				    struct hevc_frame *col_frame)
{
	union rpm_param *param = &hevc->rpm_param;
	u32 lcu_size_log2 = ilog2(hevc->lcu_size);
	u32 mv_mem_unit = lcu_size_log2 == 6 ? 0x200 :
			  lcu_size_log2 == 5 ? 0x80 : 0x20;
	dma_addr_t col_mv_rd_start_addr, col_mv_rd_ptr, col_mv_rd_end_addr;
	dma_addr_t mpred_mv_wr_ptr;

	amvdec_read_dos(core, HEVC_MPRED_CURR_LCU);

	col_mv_rd_start_addr = codec_hevc_get_frame_mv_paddr(hevc, col_frame);
	mpred_mv_wr_ptr = codec_hevc_get_frame_mv_paddr(hevc, frame) +
			  (hevc->slice_addr * mv_mem_unit);
	col_mv_rd_ptr = col_mv_rd_start_addr +
			(hevc->slice_addr * mv_mem_unit);
	col_mv_rd_end_addr = col_mv_rd_start_addr +
			     (hevc->lcu_total * mv_mem_unit);

	amvdec_write_dos(core, HEVC_MPRED_MV_WR_START_ADDR,
			 codec_hevc_get_frame_mv_paddr(hevc, frame));
	amvdec_write_dos(core, HEVC_MPRED_MV_RD_START_ADDR,
			 col_mv_rd_start_addr);

	if (param->p.slice_segment_address == 0) {
		amvdec_write_dos(core, HEVC_MPRED_ABV_START_ADDR,
				 hevc->workspace_paddr + MPRED_ABV_OFFSET);
		amvdec_write_dos(core, HEVC_MPRED_MV_WPTR, mpred_mv_wr_ptr);
		amvdec_write_dos(core, HEVC_MPRED_MV_RPTR,
				 col_mv_rd_start_addr);
	} else {
		amvdec_write_dos(core, HEVC_MPRED_MV_RPTR, col_mv_rd_ptr);
	}

	amvdec_write_dos(core, HEVC_MPRED_MV_RD_END_ADDR, col_mv_rd_end_addr);
}

/* Update motion prediction with the current slice */
static void codec_hevc_set_mpred(struct amvdec_session *sess,
				 struct hevc_frame *frame,
				 struct hevc_frame *col_frame)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 *ref_num = frame->ref_num;
	u32 *ref_poc_l0 = frame->ref_poc_list[0][frame->cur_slice_idx];
	u32 *ref_poc_l1 = frame->ref_poc_list[1][frame->cur_slice_idx];
	u32 val;
	int i;

	codec_hevc_set_mpred_ctrl(core, hevc);
	codec_hevc_set_mpred_mv(core, hevc, frame, col_frame);

	amvdec_write_dos(core, HEVC_MPRED_PIC_SIZE,
			 hevc->width | (hevc->height << 16));

	val = ((hevc->lcu_x_num - 1) | (hevc->lcu_y_num - 1) << 16);
	amvdec_write_dos(core, HEVC_MPRED_PIC_SIZE_LCU, val);

	amvdec_write_dos(core, HEVC_MPRED_REF_NUM,
			 (ref_num[1] << 8) | ref_num[0]);
	amvdec_write_dos(core, HEVC_MPRED_REF_EN_L0, (1 << ref_num[0]) - 1);
	amvdec_write_dos(core, HEVC_MPRED_REF_EN_L1, (1 << ref_num[1]) - 1);

	amvdec_write_dos(core, HEVC_MPRED_CUR_POC, hevc->curr_poc);
	amvdec_write_dos(core, HEVC_MPRED_COL_POC, hevc->col_poc);

	for (i = 0; i < MAX_REF_ACTIVE; ++i) {
		amvdec_write_dos(core, HEVC_MPRED_L0_REF00_POC + i * 4,
				 ref_poc_l0[i]);
		amvdec_write_dos(core, HEVC_MPRED_L1_REF00_POC + i * 4,
				 ref_poc_l1[i]);
	}
}

/*  motion compensation reference cache controller */
static void codec_hevc_set_mcrcc(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 val, val_2;
	int l0_cnt = 0;
	int l1_cnt = 0x7fff;

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
		l0_cnt = hevc->cur_frame->ref_num[0];
		l1_cnt = hevc->cur_frame->ref_num[1];
	}

	if (hevc->cur_frame->cur_slice_type == I_SLICE) {
		amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0);
		return;
	}

	if (hevc->cur_frame->cur_slice_type == P_SLICE) {
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				 BIT(1));
		val = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val &= 0xffff;
		val |= (val << 16);
		amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);

		if (l0_cnt == 1) {
			amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
		} else {
			val = amvdec_read_dos(core,
					      HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
			val &= 0xffff;
			val |= (val << 16);
			amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
		}
	} else { /* B_SLICE */
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 0);
		val = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val &= 0xffff;
		val |= (val << 16);
		amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);

		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				 BIT(12) | BIT(1));
		val_2 = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val_2 &= 0xffff;
		val_2 |= (val_2 << 16);
		if (val == val_2 && l1_cnt > 1) {
			val_2 = amvdec_read_dos(core,
						HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
			val_2 &= 0xffff;
			val_2 |= (val_2 << 16);
		}
		amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
	}

	/* enable mcrcc progressive-mode */
	amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0xff0);
}

/*
 * codec_hevc_set_ref_list - program the hardware canvas reference list.
 *
 * DVB robustness changes vs. the original:
 *
 * 1. When a reference frame is not found in ref_frames_list we no longer
 *    silently skip it.  Instead we increment consecutive_ref_errors and,
 *    once the threshold is reached, set needs_resync so the ISR can
 *    perform a clean DPB flush before the next sync point.
 *
 * 2. On a successful lookup we reset consecutive_ref_errors so that a
 *    single bad frame (e.g. one RASL after a CRA) does not accumulate
 *    towards the threshold.
 */
static void codec_hevc_set_ref_list(struct amvdec_session *sess,
				    u32 ref_num, u32 *ref_poc_list)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *ref_frame;
	struct amvdec_core *core = sess->core;
	int i;
	u32 buf_id_y;
	u32 buf_id_uv;

	for (i = 0; i < ref_num; i++) {
		ref_frame = codec_hevc_get_frame_by_poc(hevc, ref_poc_list[i]);

		if (!ref_frame) {
			/*
			 * Missing reference — common at stream start and after
			 * DVB discontinuities.  Count the error; if we exceed
			 * the threshold mark the stream as needing resync.
			 * The actual flush is deferred to the ISR so we do not
			 * drop the lock here.
			 */
			hevc->consecutive_ref_errors++;
			dev_dbg(core->dev,
				"HEVC: missing ref frame POC %d (error %u/%u)\n",
				ref_poc_list[i],
				hevc->consecutive_ref_errors,
				HEVC_MAX_CONSECUTIVE_REF_ERRORS);

			if (hevc->consecutive_ref_errors >=
			    HEVC_MAX_CONSECUTIVE_REF_ERRORS) {
				dev_warn(core->dev,
					 "HEVC: too many missing refs — forcing stream resync\n");
				hevc->needs_resync = true;
			}
			continue;
		}

		/* Successful lookup — reset the error streak */
		hevc->consecutive_ref_errors = 0;

		if (codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
			buf_id_y = ref_frame->vbuf->vb2_buf.index;
			buf_id_uv = buf_id_y;
		} else {
			buf_id_y = ref_frame->vbuf->vb2_buf.index * 2;
			buf_id_uv = buf_id_y + 1;
		}

		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
				 (buf_id_uv << 16) |
				 (buf_id_uv << 8) |
				 buf_id_y);
	}
}

static void codec_hevc_set_mc(struct amvdec_session *sess,
			      struct hevc_frame *frame)
{
	struct amvdec_core *core = sess->core;

	if (frame->cur_slice_type == I_SLICE)
		return;

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	codec_hevc_set_ref_list(sess, frame->ref_num[0],
				frame->ref_poc_list[0][frame->cur_slice_idx]);

	if (frame->cur_slice_type == P_SLICE)
		return;

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
			 BIT(12) | BIT(0));
	codec_hevc_set_ref_list(sess, frame->ref_num[1],
				frame->ref_poc_list[1][frame->cur_slice_idx]);
}

static void codec_hevc_update_col_frame(struct codec_hevc *hevc)
{
	struct hevc_frame *cur_frame = hevc->cur_frame;
	union rpm_param *param = &hevc->rpm_param;
	u32 list_no = 0;
	u32 col_ref = param->p.collocated_ref_idx;
	u32 col_from_l0 = param->p.collocated_from_l0_flag;
	u32 cur_slice_idx = cur_frame->cur_slice_idx;

	if (cur_frame->cur_slice_type == B_SLICE)
		list_no = 1 - col_from_l0;

	if (col_ref >= cur_frame->ref_num[list_no])
		hevc->col_poc = INVALID_POC;
	else
		hevc->col_poc = cur_frame->ref_poc_list[list_no]
						       [cur_slice_idx]
						       [col_ref];

	if (cur_frame->cur_slice_type == I_SLICE)
		goto end;

	if (hevc->col_poc != INVALID_POC)
		hevc->col_frame = codec_hevc_get_frame_by_poc(hevc,
							      hevc->col_poc);
	else
		hevc->col_frame = hevc->cur_frame;

end:
	if (!hevc->col_frame)
		hevc->col_frame = hevc->cur_frame;
}

static void codec_hevc_update_pocs(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *param = &hevc->rpm_param;
	u32 nal_unit_type = param->p.m_nalUnitType;
	u32 temporal_id = param->p.m_temporalId & 0x7;
	int max_poc_lsb =
		1 << (param->p.log2_max_pic_order_cnt_lsb_minus4 + 4);
	int prev_poc_lsb;
	int prev_poc_msb;
	int poc_msb;
	int poc_lsb = param->p.POClsb;

	if (nal_unit_type == NAL_UNIT_CODED_SLICE_IDR ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_IDR_N_LP) {
		hevc->curr_poc = 0;
		if ((temporal_id - 1) == 0)
			hevc->prev_tid0_poc = hevc->curr_poc;

		return;
	}

	prev_poc_lsb = hevc->prev_tid0_poc % max_poc_lsb;
	prev_poc_msb = hevc->prev_tid0_poc - prev_poc_lsb;

	if ((poc_lsb < prev_poc_lsb) &&
	    ((prev_poc_lsb - poc_lsb) >= (max_poc_lsb / 2)))
		poc_msb = prev_poc_msb + max_poc_lsb;
	else if ((poc_lsb > prev_poc_lsb) &&
		 ((poc_lsb - prev_poc_lsb) > (max_poc_lsb / 2)))
		poc_msb = prev_poc_msb - max_poc_lsb;
	else
		poc_msb = prev_poc_msb;

	if (nal_unit_type == NAL_UNIT_CODED_SLICE_BLA   ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_BLANT ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_BLA_N_LP)
		poc_msb = 0;

	hevc->curr_poc = (poc_msb + poc_lsb);
	if ((temporal_id - 1) == 0)
		hevc->prev_tid0_poc = hevc->curr_poc;
}

static void codec_hevc_process_segment_header(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *param = &hevc->rpm_param;

	if (param->p.first_slice_segment_in_pic_flag == 0) {
		hevc->slice_segment_addr = param->p.slice_segment_address;
		if (!param->p.dependent_slice_segment_flag)
			hevc->slice_addr = hevc->slice_segment_addr;
	} else {
		hevc->slice_segment_addr = 0;
		hevc->slice_addr = 0;
	}

	codec_hevc_update_pocs(sess);
}

/*
 * codec_hevc_process_segment - set up hardware for one slice segment.
 *
 * DVB robustness changes vs. the original:
 *
 * 1. If we have not yet found a random-access point (stream_synced == false),
 *    discard the segment entirely.  This prevents the decoder from attempting
 *    to reconstruct P/B frames whose reference pictures were never received
 *    (i.e. everything that arrived before the first IDR/BLA/CRA).
 *
 * 2. Guard against a NULL cur_frame when slice_segment_address != 0.  This
 *    can happen right after codec_hevc_reset_stream_state() clears cur_frame
 *    and the very next interrupt carries a non-first slice of a picture whose
 *    first slice was never decoded.  In that case we treat the whole picture
 *    as lost and wait for the next first-slice boundary.
 */
static int codec_hevc_process_segment(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;
	union rpm_param *param = &hevc->rpm_param;
	u32 slice_segment_address = param->p.slice_segment_address;

	/*
	 * Gate on sync.  Until we see an IDR/BLA/CRA we have no reference
	 * frames at all, so decoding any inter slice would be pointless and
	 * would flood the log with "Couldn't find ref. frame" messages.
	 */
	if (!hevc->stream_synced) {
		dev_dbg(core->dev,
			"HEVC: waiting for sync point, discarding slice (seg_addr=%u)\n",
			slice_segment_address);
		return 0;
	}

	/* First slice of a new picture */
	if (slice_segment_address == 0) {
		codec_hevc_update_referenced(hevc);
		codec_hevc_show_frames(sess);

		hevc->cur_frame = codec_hevc_prepare_new_frame(sess);
		if (!hevc->cur_frame)
			return -1;
	} else {
		/*
		 * Subsequent slice of the current picture.  cur_frame can be
		 * NULL here if:
		 *   (a) we just reset state and the stream dropped us into
		 *       the middle of a picture, or
		 *   (b) codec_hevc_prepare_new_frame() failed for the first
		 *       slice.
		 * In either case we cannot decode this slice; skip it and
		 * wait for the next picture boundary.
		 */
		if (!hevc->cur_frame) {
			dev_dbg(core->dev,
				"HEVC: no current frame for slice seg_addr=%u, skipping\n",
				slice_segment_address);
			return 0;
		}
		hevc->cur_frame->cur_slice_idx++;
	}

	codec_hevc_update_frame_refs(sess, hevc->cur_frame);
	codec_hevc_update_col_frame(hevc);
	codec_hevc_update_ldc_flag(hevc);
	if (codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       hevc->is_10bit))
		codec_hevc_fill_mmu_map(sess, &hevc->common,
					&hevc->cur_frame->vbuf->vb2_buf,
					hevc->is_10bit);
	codec_hevc_set_mc(sess, hevc->cur_frame);
	codec_hevc_set_mcrcc(sess);
	codec_hevc_set_mpred(sess, hevc->cur_frame, hevc->col_frame);
	codec_hevc_set_sao(sess, hevc->cur_frame);

	amvdec_write_dos_bits(core, HEVC_WAIT_FLAG, BIT(1));
	amvdec_write_dos(core, HEVC_DEC_STATUS_REG,
			 HEVC_CODED_SLICE_SEGMENT_DAT);

	/* Interrupt the firmware's processor */
	amvdec_write_dos(core, HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);

	return 0;
}

static int codec_hevc_process_rpm(struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	int src_changed = 0;
	u32 dst_width, dst_height;
	u32 lcu_size;
	u32 is_10bit = 0;

	if (param->p.slice_segment_address	||
	    !param->p.pic_width_in_luma_samples	||
	    !param->p.pic_height_in_luma_samples)
		return 0;

	if (param->p.bit_depth)
		is_10bit = 1;

	hevc->width = param->p.pic_width_in_luma_samples;
	hevc->height = param->p.pic_height_in_luma_samples;
	dst_width = hevc->width;
	dst_height = hevc->height;

	lcu_size = 1 << (param->p.log2_min_coding_block_size_minus3 +
		   3 + param->p.log2_diff_max_min_coding_block_size);

	hevc->lcu_x_num = (hevc->width + lcu_size - 1) / lcu_size;
	hevc->lcu_y_num = (hevc->height + lcu_size - 1) / lcu_size;
	hevc->lcu_total = hevc->lcu_x_num * hevc->lcu_y_num;

	if (param->p.conformance_window_flag) {
		u32 sub_width = 1, sub_height = 1;

		switch (param->p.chroma_format_idc) {
		case 1:
			sub_height = 2;
			fallthrough;
		case 2:
			sub_width = 2;
			break;
		}

		dst_width -= sub_width *
			     (param->p.conf_win_left_offset +
			      param->p.conf_win_right_offset);
		dst_height -= sub_height *
			      (param->p.conf_win_top_offset +
			       param->p.conf_win_bottom_offset);
	}

	if (dst_width != hevc->dst_width ||
	    dst_height != hevc->dst_height ||
	    lcu_size != hevc->lcu_size ||
	    is_10bit != hevc->is_10bit)
		src_changed = 1;

	hevc->dst_width = dst_width;
	hevc->dst_height = dst_height;
	hevc->lcu_size = lcu_size;
	hevc->is_10bit = is_10bit;

	return src_changed;
}

/*
 * The RPM section within the workspace contains
 * many information regarding the parsed bitstream
 */
static void codec_hevc_fetch_rpm(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	u16 *rpm_vaddr = hevc->workspace_vaddr + RPM_OFFSET;
	int i, j;

	for (i = 0; i < RPM_SIZE; i += 4) {
		for (j = 0; j < 4; j++)
			hevc->rpm_param.l.data[i + j] =
				rpm_vaddr[i + 3 - j];
	}
}

static void codec_hevc_resume(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;

	if (codec_hevc_setup_buffers(sess, &hevc->common, hevc->is_10bit)) {
		amvdec_abort(sess);
		return;
	}

	codec_hevc_setup_decode_head(sess, hevc->is_10bit);
	codec_hevc_process_segment_header(sess);
	if (codec_hevc_process_segment(sess))
		amvdec_abort(sess);
}

/*
 * codec_hevc_threaded_isr - threaded interrupt service routine.
 *
 * DVB robustness changes vs. the original:
 *
 * 1. Handle HEVC_DECPIC_DATA_ERROR explicitly.  The hardware sets this status
 *    when it cannot reconstruct a picture (e.g. corrupted transport packets).
 *    Previously this fell through to the "Unrecognized dec_status" error path
 *    which called amvdec_abort() and killed the session.  For a live DVB
 *    stream we instead treat it as a transient error: increment the ref-error
 *    counter (so that persistent corruption still triggers a resync) and
 *    continue.  This keeps the decoder alive through brief signal dropouts.
 *
 * 2. Process needs_resync before attempting any new segment.  If
 *    codec_hevc_set_ref_list() raised the flag we flush the DPB and reset
 *    POC state here, before doing anything else with the new RPM data.
 *
 * 3. Update stream_synced when a random-access point is detected.  Once set,
 *    the flag persists until the next resync so normal inter-frames decode
 *    without any overhead.
 */
static irqreturn_t codec_hevc_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 dec_status = amvdec_read_dos(core, HEVC_DEC_STATUS_REG);

	if (!hevc)
		return IRQ_HANDLED;

	mutex_lock(&hevc->lock);

	/*
	 * HEVC_DECPIC_DATA_ERROR: the hardware could not reconstruct the
	 * picture (e.g. corrupted TS packets).  Rather than aborting the
	 * entire session (which would stop playback) we treat this as a
	 * recoverable per-frame error.  Increment the consecutive-error
	 * counter; if it reaches the threshold needs_resync will be set
	 * below and we will flush the DPB on the next interrupt.
	 */
	if (dec_status == HEVC_DECPIC_DATA_ERROR) {
		hevc->consecutive_ref_errors++;
		dev_dbg(core->dev,
			"HEVC: hardware decode error (status 0xb), error count %u\n",
			hevc->consecutive_ref_errors);

		if (hevc->consecutive_ref_errors >=
		    HEVC_MAX_CONSECUTIVE_REF_ERRORS) {
			dev_warn(core->dev,
				 "HEVC: persistent decode errors — forcing stream resync\n");
			hevc->needs_resync = true;
		}
		goto unlock;
	}

	if (dec_status != HEVC_SLICE_SEGMENT_DONE) {
		dev_err(core->dev_dec, "Unrecognized dec_status: %08X\n",
			dec_status);
		amvdec_abort(sess);
		goto unlock;
	}

	sess->keyframe_found = 1;
	codec_hevc_fetch_rpm(sess);

	/*
	 * Deferred resync: codec_hevc_set_ref_list() set needs_resync after
	 * too many consecutive missing-reference errors.  Do the flush now,
	 * before processing the new RPM data, so that stale POC values and
	 * DPB entries cannot corrupt the fresh decode state.
	 */
	if (hevc->needs_resync)
		codec_hevc_reset_stream_state(sess);

	/*
	 * Sync-point detection MUST happen before codec_hevc_process_rpm()
	 * because process_rpm() returns non-zero (src_changed) on the very
	 * first IDR — the initial dst_width/dst_height/lcu_size are all zero
	 * so every first-frame RPM looks like a resolution change.  The
	 * src_change path does "goto unlock", which previously bypassed this
	 * block entirely, leaving stream_synced = false forever and causing
	 * every subsequent slice to be discarded with "waiting for sync point".
	 *
	 * By checking the NAL type here, before process_rpm(), we set
	 * stream_synced = true on the IDR interrupt itself.  The src_change
	 * goto then fires as normal (triggering buffer re-allocation), and
	 * when the session resumes via codec_hevc_resume() the stream is
	 * already marked as synced so decoding proceeds immediately.
	 *
	 * IDR/BLA: unconditionally flush any stale DPB entries from the
	 * previous GOP.  CRA: only flush when not yet synced (stream start
	 * or post-discontinuity) — mid-stream CRAs do not mandate a DPB
	 * clear and flushing them would drop frames needlessly.
	 */
	if (codec_hevc_is_random_access_point(hevc)) {
		u32 nal = hevc->rpm_param.p.m_nalUnitType;
		bool is_idr_bla = (nal == NAL_UNIT_CODED_SLICE_IDR     ||
				   nal == NAL_UNIT_CODED_SLICE_IDR_N_LP ||
				   nal == NAL_UNIT_CODED_SLICE_BLA      ||
				   nal == NAL_UNIT_CODED_SLICE_BLANT    ||
				   nal == NAL_UNIT_CODED_SLICE_BLA_N_LP);

		if (is_idr_bla || !hevc->stream_synced) {
			dev_dbg(core->dev,
				"HEVC: sync point NAL type %u — resetting DPB\n",
				nal);
			codec_hevc_reset_stream_state(sess);
		}

		hevc->stream_synced = true;
		dev_dbg(core->dev,
			"HEVC: stream synced on NAL type %u\n", nal);
	}

	/*
	 * Process the RPM after sync detection.  If the resolution or bit
	 * depth changed the session needs new output buffers; amvdec_src_change
	 * triggers re-allocation and codec_hevc_resume() will be called once
	 * the buffers are ready.  At that point stream_synced is already true
	 * so the resume path decodes normally.
	 */
	if (codec_hevc_process_rpm(hevc)) {
		amvdec_src_change(sess, hevc->dst_width, hevc->dst_height, 16,
				  hevc->is_10bit ? 10 : 8);
		goto unlock;
	}

	codec_hevc_process_segment_header(sess);
	if (codec_hevc_process_segment(sess))
		amvdec_abort(sess);

unlock:
	mutex_unlock(&hevc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t codec_hevc_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_hevc_ops = {
	.start = codec_hevc_start,
	.stop = codec_hevc_stop,
	.isr = codec_hevc_isr,
	.threaded_isr = codec_hevc_threaded_isr,
	.num_pending_bufs = codec_hevc_num_pending_bufs,
	.drain = codec_hevc_flush_output,
	.resume = codec_hevc_resume,
};
