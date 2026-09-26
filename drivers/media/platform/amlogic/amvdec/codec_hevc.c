// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * HEVC request decoding.
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc.h"
#include "dos_regs.h"
#include "hevc_regs.h"
#include "amvdec_helpers.h"
#include "codec_hevc_common.h"
#include "amvdec_hevc.h"
#include "esparser.h"

/* HEVC reg mapping */
#define HEVC_DEC_STATUS_REG	HEVC_ASSIST_SCRATCH_0
	#define HEVC_ACTION_DONE	0xff

/* Stall watchdog states and intervals. */
#define STALL_IDLE		0
#define STALL_ARMED		1
#define STALL_PROCESSING	2
#define STALL_DISABLED		3
#define STALL_TIMEOUT_MS	200
#define STALL_GRACE_MS		10
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
#define MAX_SLICE_NUM		AMVDEC_HEVC_MAX_SLICES
#define INVALID_POC		0x80000000

/* HEVC Workspace layout */
/*
 * The vendor 4K workspace is shared by GX and G12. At 3840x2160 the
 * largest MV extent is 60 * 34 CTBs * 512 bytes = 0xff000 per picture.
 */
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
/*
 * Allocate each capture buffer's motion-vector storage separately to reduce
 * contiguous allocation sizes. Hardware accepts an independent MV address
 * for each frame.
 */
#define RPM_OFFSET       (MPRED_ABV_OFFSET + MPRED_ABV_SIZE)
#define LMEM_OFFSET      (RPM_OFFSET + RPM_BUF_SIZE)

/* ISR decode status */
#define HEVC_CODED_SLICE_SEGMENT_DAT         0x5
#define HEVC_SLICE_SEGMENT_DONE              0x8
/*
 * The firmware has consumed everything the VIFIFO holds and is waiting for
 * more input.  Not an error: the feed worker is writing the next access unit
 * concurrently, and the firmware resumes on its own once the data lands.
 */
#define HEVC_DECODE_BUFEMPTY                 0x20
#define HEVC_SEARCH_BUFEMPTY                 0x22

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
#define RPM_SIZE 0x80

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
		u16 poc_lsb;
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
		u16 temporal_id;
		u16 nal_unit_type;
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
	u32 poc;
	u32 input_end;

	int referenced;
	int show;
	u32 num_reorder_pic;

	u32 cur_slice_idx;
	u32 cur_slice_type;

	/* 2 lists (L0/L1) ; 800 slices ; 16 refs */
	u32 ref_poc_list[2][MAX_SLICE_NUM][MAX_REF_ACTIVE];
	u32 ref_num[2];
	struct v4l2_ctrl_hevc_slice_params slice_ctrl;
	struct v4l2_ctrl_hevc_decode_params decode_ctrl;
	struct hevc_frame *ctrl_refs[2][MAX_REF_ACTIVE];
};

struct codec_hevc {
	/* Protect the data structure */
	struct mutex lock;

	/*
	 * Set once the first IRAP slice of the session has been seen;
	 * everything before it is skipped (a session fed from mid-GOP
	 * has no valid references at all).
	 */
	u32 seen_irap;

	/* Arm the watchdog when firmware owes an interrupt; disarm it at IRQ entry. */
	struct amvdec_session *sess;
	struct delayed_work stall_work;
	bool request_failed;
	atomic_t stall_state;
	u32 stall_lcu;
	u32 stall_status;
	u32 stall_shift;
	u32 stall_ticks;
	u32 stall_recoveries;

	/* Common part of the HEVC decoder */
	struct codec_hevc_common common;

	/* Buffer for the HEVC Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;
	struct amvdec_dma_guard workspace_guard;

	/* Per-capture motion-vector buffers share the workspace cache lifetime. */
	void      *mv_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t mv_paddr[MAX_REF_PIC_NUM];
	struct amvdec_dma_guard mv_guard[MAX_REF_PIC_NUM];

	/* AUX buffer */
	void      *aux_vaddr;
	dma_addr_t aux_paddr;

	/* Firmware-parsed bitstream parameters. */
	union rpm_param rpm_param;

	/* Information computed from the RPM */
	u32 lcu_size; // Largest Coding Unit
	u32 lcu_x_num;

	/*
	 * Tile grid of the current picture in LCUs, and the tile the current
	 * slice segment starts in.  One whole-picture tile when the PPS has
	 * none.
	 */
	u32 tile_cols, tile_rows;
	u16 tile_col_start[MAX_TILE_COL_NUM + 1];
	u16 tile_row_start[MAX_TILE_ROW_NUM + 1];
	u32 tile_x, tile_y;
	bool tile_enabled;
	u32 lcu_y_num;
	u32 lcu_total;

	/* Current Frame being handled */
	struct hevc_frame *cur_frame;
	u32 curr_poc;
	/* Collocated Reference Picture */
	struct hevc_frame *col_frame;
	u32 col_poc;

	/* All ref frames used by the HW at a given time */
	struct list_head ref_frames_list;
	u32 frames_num;

	/* Coded resolution reported by the hardware */
	u32 width, height;
	/* Resolution minus the conformance window offsets */
	u32 dst_width, dst_height;

	u32 prev_tid0_poc;
	u32 slice_segment_addr;
	u32 slice_addr;
	u32 ldc_flag;

	/* Coded bit depth is 10. */
	int is_10bit;

	/* The workspace is borrowed from the module cache, not owned */
	bool ws_cached;
	bool request_size, request_parked;
	u32 request_origin;
	/* Set when the shifter has grown enough that the next endpoint could
	 * cross 0x80000000; acted on between pictures, with nothing queued.
	 */
	bool restream_pending;
};

static void codec_hevc_stall_arm(struct codec_hevc *hevc);
static int codec_hevc_verify_or_fail(struct amvdec_session *sess,
				     const char *prefix);

/* Enable or disable skipping slices before the first IRAP. */
static void codec_hevc_set_resync(struct amvdec_session *sess, bool on)
{
	struct codec_hevc *hevc = sess->priv;

	hevc->seen_irap = !on;
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

	/* POCs are signed: RADL pictures of an IDR count down from zero */
	hevc->ldc_flag = 1;
	for (i = 0; (i < frame->ref_num[0]) && hevc->ldc_flag; i++) {
		if ((s32)frame->ref_poc_list[0][slice_idx][i] > (s32)frame->poc) {
			hevc->ldc_flag = 0;
			break;
		}
	}

	if (slice_type == P_SLICE)
		return;

	for (i = 0; (i < frame->ref_num[1]) && hevc->ldc_flag; i++) {
		if ((s32)frame->ref_poc_list[1][slice_idx][i] > (s32)frame->poc) {
			hevc->ldc_flag = 0;
			break;
		}
	}
}

/*
 * Cache the workspace across sessions; G12 also caches the full MV set.
 * GX allocates MV slots per capture buffer to leave room for contiguous
 * compressed references. Lend the cache to one session at a time.
 */
static struct {
	/* Protect cached workspace ownership and allocation. */
	struct mutex lock;
	struct device *dev;
	void *vaddr;
	dma_addr_t paddr;
	struct amvdec_dma_guard guard;
	void *mv_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t mv_paddr[MAX_REF_PIC_NUM];
	struct amvdec_dma_guard mv_guard[MAX_REF_PIC_NUM];
	bool busy;
} hevc_ws = {
	.lock = __MUTEX_INITIALIZER(hevc_ws.lock),
};

static void codec_hevc_free_mv_set(struct device *dev, void **vaddr,
				   dma_addr_t *paddr,
				   struct amvdec_dma_guard *guard)
{
	int i;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (!vaddr[i])
			continue;

		meson_amvdec_dma_free_guarded(dev, paddr[i], &guard[i]);
		vaddr[i] = NULL;
		paddr[i] = 0;
	}
}

/*
 * G12 reserves every MV slot at startup. GX reserves each slot before its
 * first picture instead, reducing the contiguous-memory footprint.
 */
static int codec_hevc_alloc_mv_set(struct device *dev, void **vaddr,
				   dma_addr_t *paddr,
				   struct amvdec_dma_guard *guard)
{
	int i;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		vaddr[i] = meson_amvdec_dma_alloc_guarded(dev, MPRED_MV_BUF_SIZE,
						    &paddr[i], GFP_KERNEL,
							&guard[i]);
		if (!vaddr[i]) {
			codec_hevc_free_mv_set(dev, vaddr, paddr, guard);
			return -ENOMEM;
		}
	}

	return 0;
}

static int codec_hevc_verify_workspace_guards(struct amvdec_core *core,
					      struct codec_hevc *hevc,
					      const char *prefix)
{
	char name[96];
	int ret = 0;
	int i;

	if (hevc->workspace_vaddr) {
		scnprintf(name, sizeof(name), "%s.workspace.%s", prefix,
			  hevc->ws_cached ? "cached" : "private");
		ret |= meson_amvdec_dma_verify_guard(core->dev, name,
						       &hevc->workspace_guard);
	}
	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (!hevc->mv_vaddr[i])
			continue;
		scnprintf(name, sizeof(name), "%s.mv[%d].%s", prefix, i,
			  hevc->ws_cached && amvdec_is_g12(core) ? "cached" : "private");
		ret |= meson_amvdec_dma_verify_guard(core->dev, name,
						       &hevc->mv_guard[i]);
	}
	ret |= meson_amvdec_codec_hevc_verify_fbc_guards(hevc->sess, &hevc->common, prefix);

	return ret ? -EIO : 0;
}

static void codec_hevc_guard_fail_session(struct amvdec_session *sess)
{
	dev_err(sess->core->dev, "HEVC-core DMA guard changed; failing session\n");
	vb2_queue_error(&sess->m2m_ctx->out_q_ctx.q);
	vb2_queue_error(&sess->m2m_ctx->cap_q_ctx.q);
}

static int codec_hevc_verify_or_fail(struct amvdec_session *sess,
				     const char *prefix)
{
	struct codec_hevc *hevc = sess->priv;

	if (!codec_hevc_verify_workspace_guards(sess->core, hevc, prefix))
		return 0;

	codec_hevc_guard_fail_session(sess);
	return -EIO;
}

static int codec_hevc_alloc_workspace(struct amvdec_core *core,
				      struct codec_hevc *hevc)
{
	bool cache_mv = amvdec_is_g12(core);

	mutex_lock(&hevc_ws.lock);

	if (!hevc_ws.vaddr) {
		hevc_ws.vaddr = meson_amvdec_dma_alloc_guarded(core->dev, SIZE_WORKSPACE,
							 &hevc_ws.paddr, GFP_KERNEL,
								 &hevc_ws.guard);
		if (hevc_ws.vaddr) {
			if (cache_mv &&
			    codec_hevc_alloc_mv_set(core->dev, hevc_ws.mv_vaddr,
						    hevc_ws.mv_paddr,
						    hevc_ws.mv_guard)) {
				meson_amvdec_dma_free_guarded(core->dev, hevc_ws.paddr,
							&hevc_ws.guard);
				hevc_ws.vaddr = NULL;
			} else {
				hevc_ws.dev = core->dev;
			}
		}
	}

	if (hevc_ws.vaddr && !hevc_ws.busy) {
		/* Hand it over as if freshly allocated */
		memset(hevc_ws.vaddr, 0, SIZE_WORKSPACE);
		hevc_ws.busy = true;
		hevc->workspace_vaddr = hevc_ws.vaddr;
		hevc->workspace_paddr = hevc_ws.paddr;
		hevc->workspace_guard = hevc_ws.guard;
		memcpy(hevc->mv_vaddr, hevc_ws.mv_vaddr, sizeof(hevc->mv_vaddr));
		memcpy(hevc->mv_paddr, hevc_ws.mv_paddr, sizeof(hevc->mv_paddr));
		memcpy(hevc->mv_guard, hevc_ws.mv_guard, sizeof(hevc->mv_guard));
		hevc->ws_cached = true;
		mutex_unlock(&hevc_ws.lock);
		return 0;
	}

	mutex_unlock(&hevc_ws.lock);

	/* Allocate privately when the cache is absent or busy. */
	hevc->workspace_vaddr = meson_amvdec_dma_alloc_guarded(core->dev, SIZE_WORKSPACE,
							 &hevc->workspace_paddr,
							      GFP_KERNEL,
							      &hevc->workspace_guard);
	if (!hevc->workspace_vaddr)
		return -ENOMEM;

	if (cache_mv && codec_hevc_alloc_mv_set(core->dev, hevc->mv_vaddr,
						hevc->mv_paddr,
						hevc->mv_guard)) {
		meson_amvdec_dma_free_guarded(core->dev, hevc->workspace_paddr,
					&hevc->workspace_guard);
		hevc->workspace_vaddr = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void codec_hevc_put_workspace(struct amvdec_core *core,
				     struct codec_hevc *hevc)
{
	if (!hevc->workspace_vaddr)
		return;

	if (hevc->ws_cached) {
		mutex_lock(&hevc_ws.lock);
		hevc_ws.busy = false;
		mutex_unlock(&hevc_ws.lock);
	} else {
		meson_amvdec_dma_free_guarded(core->dev, hevc->workspace_paddr,
					&hevc->workspace_guard);
	}
	if (!hevc->ws_cached || !amvdec_is_g12(core))
		codec_hevc_free_mv_set(core->dev, hevc->mv_vaddr,
				       hevc->mv_paddr, hevc->mv_guard);

	/* Cached or not, this session no longer refers to any of it. */
	memset(hevc->mv_vaddr, 0, sizeof(hevc->mv_vaddr));
	memset(hevc->mv_paddr, 0, sizeof(hevc->mv_paddr));
	memset(hevc->mv_guard, 0, sizeof(hevc->mv_guard));
	hevc->workspace_vaddr = NULL;
	hevc->workspace_paddr = 0;
	memset(&hevc->workspace_guard, 0, sizeof(hevc->workspace_guard));
	hevc->ws_cached = false;
}

/* Module teardown: give the lent buffer back to CMA */
void meson_amvdec_codec_hevc_workspace_release(void)
{
	mutex_lock(&hevc_ws.lock);
	if (hevc_ws.vaddr && !hevc_ws.busy) {
		meson_amvdec_dma_free_guarded(hevc_ws.dev, hevc_ws.paddr,
					&hevc_ws.guard);
		hevc_ws.vaddr = NULL;
		hevc_ws.paddr = 0;
		codec_hevc_free_mv_set(hevc_ws.dev, hevc_ws.mv_vaddr,
				       hevc_ws.mv_paddr, hevc_ws.mv_guard);
	}
	mutex_unlock(&hevc_ws.lock);
}

/*
 * Program workspace addresses at startup and after capture allocation.
 * MMU register values depend on bit depth and the allocated frame map.
 */
static void
codec_hevc_setup_workspace(struct amvdec_session *sess,
			   struct codec_hevc *hevc)
{
	struct amvdec_core *core = sess->core;
	u32 revision = core->platform->revision;
	dma_addr_t wkaddr = hevc->workspace_paddr;

	meson_amvdec_write_dos(core, HEVCD_IPP_LINEBUFF_BASE, wkaddr + IPP_OFFSET);
	meson_amvdec_write_dos(core, HEVC_RPM_BUFFER, wkaddr + RPM_OFFSET);
	meson_amvdec_write_dos(core, HEVC_SHORT_TERM_RPS, wkaddr + SH_TM_RPS_OFFSET);
	meson_amvdec_write_dos(core, HEVC_VPS_BUFFER, wkaddr + VPS_OFFSET);
	meson_amvdec_write_dos(core, HEVC_SPS_BUFFER, wkaddr + SPS_OFFSET);
	meson_amvdec_write_dos(core, HEVC_PPS_BUFFER, wkaddr + PPS_OFFSET);
	meson_amvdec_write_dos(core, HEVC_SAO_UP, wkaddr + SAO_UP_OFFSET);

	if (codec_hevc_use_mmu(revision, sess->pixfmt_cap, hevc->is_10bit)) {
		meson_amvdec_write_dos(core, HEVC_SAO_MMU_VH0_ADDR,
				 wkaddr + MMU_VBH_OFFSET);
		meson_amvdec_write_dos(core, HEVC_SAO_MMU_VH1_ADDR,
				 wkaddr + MMU_VBH_OFFSET + (MMU_VBH_SIZE / 2));

		/*
		 * Program MMU addresses only after capture allocation supplies a frame map.
		 * Header parsing does not need the map.
		 */
		if (hevc->common.mmu_map_paddr) {
			if (revision >= AMVDEC_REVISION_G12A)
				meson_amvdec_write_dos(core, HEVC_ASSIST_MMU_MAP_ADDR,
						 hevc->common.mmu_map_paddr);
			else
				meson_amvdec_write_dos(core, H265_MMU_MAP_BUFFER,
						 hevc->common.mmu_map_paddr);
		}
	} else if (revision < AMVDEC_REVISION_G12A) {
		meson_amvdec_write_dos(core, HEVC_STREAM_SWAP_BUFFER,
				 wkaddr + SWAP_BUF_OFFSET);
		meson_amvdec_write_dos(core, HEVC_STREAM_SWAP_BUFFER2,
				 wkaddr + SWAP_BUF2_OFFSET);
	}

	meson_amvdec_write_dos(core, HEVC_SCALELUT, wkaddr + SCALELUT_OFFSET);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFG4, wkaddr + DBLK_PARA_OFFSET);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFG5, wkaddr + DBLK_DATA_OFFSET);
	if (revision >= AMVDEC_REVISION_G12A)
		meson_amvdec_write_dos(core, HEVC_DBLK_CFGE,
				 wkaddr + DBLK_DATA2_OFFSET);

	meson_amvdec_write_dos(core, LMEM_DUMP_ADR, wkaddr + LMEM_OFFSET);
}

/*
 * Initialize hardware from the allocated workspace and AUX buffers, both
 * at session startup and after a stream-counter restart.
 */
static void codec_hevc_hw_init(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 val;
	int i;

	codec_hevc_setup_workspace(sess, hevc);

	val = BIT(0); /* stream_fetch_enable */
	if (core->platform->revision >= AMVDEC_REVISION_G12A)
		val |= (0xf << 25); /* arwlen_axi_max */
	meson_amvdec_write_dos_bits(core, HEVC_STREAM_CONTROL, val);

	val = meson_amvdec_read_dos(core, HEVC_PARSER_INT_CONTROL) & 0x03ffffff;
	val |= (3 << 29) | BIT(27) | BIT(24) | BIT(22) | BIT(7) | BIT(4) |
	       BIT(0);
	meson_amvdec_write_dos(core, HEVC_PARSER_INT_CONTROL, val);
	meson_amvdec_write_dos_bits(core, HEVC_SHIFT_STATUS, BIT(1) | BIT(0));
	meson_amvdec_write_dos(core, HEVC_SHIFT_CONTROL,
			 (3 << 6) | BIT(5) | BIT(2) | BIT(0));
	meson_amvdec_write_dos(core, HEVC_CABAC_CONTROL, 1);
	meson_amvdec_write_dos(core, HEVC_PARSER_CORE_CONTROL, 1);
	meson_amvdec_write_dos(core, HEVC_DEC_STATUS_REG, 0);

	meson_amvdec_write_dos(core, HEVC_IQIT_SCALELUT_WR_ADDR, 0);
	for (i = 0; i < 1024; ++i)
		meson_amvdec_write_dos(core, HEVC_IQIT_SCALELUT_DATA, 0);

	meson_amvdec_write_dos(core, HEVC_DECODE_SIZE, 0);

	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE, BIT(16));
	for (i = 0; i < ARRAY_SIZE(meson_amvdec_hevc_parser_cmd); ++i)
		meson_amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE,
				 meson_amvdec_hevc_parser_cmd[i]);

	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);
	meson_amvdec_write_dos(core, HEVC_PARSER_IF_CONTROL,
			 BIT(5) | BIT(2) | BIT(0));

	meson_amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(0));
	meson_amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(1));

	meson_amvdec_write_dos(core, HEVC_WAIT_FLAG, 1);

	/* clear mailbox interrupt */
	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX1_CLR_REG, 1);
	/* enable mailbox interrupt */
	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 1);
	/* disable PSCALE for hardware sharing */
	meson_amvdec_write_dos(core, HEVC_PSCALE_CTRL, 0);
	/* Let the uCode do all the parsing */
	meson_amvdec_write_dos(core, NAL_SEARCH_CTL, 0xc);

	meson_amvdec_write_dos(core, DECODE_STOP_POS, 0);
	meson_amvdec_write_dos(core, HEVC_DECODE_MODE, DECODE_MODE_SINGLE);
	meson_amvdec_write_dos(core, HEVC_DECODE_MODE2, 0);

	meson_amvdec_write_dos(core, HEVC_AUX_ADR, hevc->aux_paddr);
	meson_amvdec_write_dos(core, HEVC_AUX_DATA_SIZE,
			 (((SIZE_AUX) >> 4) << 16) | 0);
}

static void codec_hevc_stall_work(struct work_struct *work);

/*
 * Schedule a restart before cumulative input positions reach 0x80000000,
 * where firmware stops consuming. Reinitialize the stream engine and
 * firmware at an empty request boundary, then reset host offsets to match.
 */
void meson_amvdec_codec_hevc_arm_restream(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!hevc)
		return;
	if (meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT) <
	    (HEVC_STREAM_RESTART_MIB << 20))
		return;
	hevc->restream_pending = true;
}

bool meson_amvdec_codec_hevc_restream_pending(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;

	return hevc && hevc->restream_pending;
}

/* Re-arm the codec after the core has been reset and the firmware restarted. */
int meson_amvdec_codec_hevc_restream_reinit(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	codec_hevc_hw_init(sess);
	if (meson_amvdec_codec_hevc_setup_buffers(sess, &hevc->common, hevc->is_10bit))
		return -EIO;
	codec_hevc_setup_workspace(sess, hevc);
	meson_amvdec_codec_hevc_setup_decode_head(sess, hevc->is_10bit);
	meson_amvdec_write_dos(core, HEVC_DECODE_SIZE, 0);
	hevc->request_origin = 0;
	hevc->request_parked = false;
	hevc->restream_pending = false;
	return 0;
}

static int codec_hevc_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc;
	int ret;

	hevc = kzalloc_obj(*hevc);
	if (!hevc)
		return -ENOMEM;

	INIT_LIST_HEAD(&hevc->ref_frames_list);
	hevc->curr_poc = INVALID_POC;
	hevc->sess = sess;
	INIT_DELAYED_WORK(&hevc->stall_work, codec_hevc_stall_work);
	atomic_set(&hevc->stall_state, STALL_IDLE);

	/* Reclaim unused FBC pool pages before allocating the contiguous workspace. */
	meson_amvdec_codec_hevc_fbc_pool_reclaim();

	/*
	 * The parked generation is not reclaimed on failure: its bodies may
	 * still be scanned out, and freeing them hands memory the display is
	 * reading back to the allocator.  Fail the session instead.
	 */
	ret = codec_hevc_alloc_workspace(core, hevc);
	if (ret)
		goto free_hevc;

	/* AUX buffers */
	hevc->aux_vaddr = dma_alloc_coherent(core->dev, SIZE_AUX,
					     &hevc->aux_paddr, GFP_KERNEL);
	if (!hevc->aux_vaddr) {
		codec_hevc_put_workspace(core, hevc);
		ret = -ENOMEM;
		goto free_hevc;
	}

	mutex_init(&hevc->lock);
	sess->priv = hevc;

	codec_hevc_hw_init(sess);
	hevc->request_size = true;
	/* Firmware has not started and no input has been fed. */
	hevc->request_origin = meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT);
	dev_dbg(core->dev, "request start origin=%u size_enabled=%u\n",
		hevc->request_origin, hevc->request_size);

	return 0;

free_hevc:
	kfree(hevc);
	return ret;
}

static void codec_hevc_flush_output(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *tmp, *n;

	list_for_each_entry_safe(tmp, n, &hevc->ref_frames_list, list) {
		if (hevc->col_frame == tmp)
			hevc->col_frame = NULL;
		if (hevc->cur_frame == tmp)
			hevc->cur_frame = NULL;
		list_del(&tmp->list);
		kfree(tmp);
	}

	/* Require a new IRAP after discarding all frame metadata. */
	codec_hevc_set_resync(sess, true);
	hevc->curr_poc = INVALID_POC;
}

static int codec_hevc_stop(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	atomic_set(&hevc->stall_state, STALL_DISABLED);
	cancel_delayed_work_sync(&hevc->stall_work);
	mutex_lock(&hevc->lock);
	codec_hevc_flush_output(sess);

	codec_hevc_verify_or_fail(sess, "hevc.stop");

	codec_hevc_put_workspace(core, hevc);

	if (hevc->aux_vaddr)
		dma_free_coherent(core->dev, SIZE_AUX,
				  hevc->aux_vaddr, hevc->aux_paddr);

	meson_amvdec_codec_hevc_free_fbc_buffers(sess, &hevc->common);
	mutex_unlock(&hevc->lock);

	/* Release unused pool pages before userspace allocates the next capture set. */
	meson_amvdec_codec_hevc_fbc_pool_reclaim();
	mutex_destroy(&hevc->lock);

	return 0;
}

static struct hevc_frame *
codec_hevc_nearest_done_frame(struct codec_hevc *hevc,
			      struct hevc_frame *cur_frame, u32 poc)
{
	struct hevc_frame *tmp, *best = NULL;
	u32 best_dist = U32_MAX;

	list_for_each_entry(tmp, &hevc->ref_frames_list, list) {
		u32 dist;

		if (tmp == cur_frame)
			continue;

		dist = abs((s32)(tmp->poc - poc));
		if (dist < best_dist) {
			best = tmp;
			best_dist = dist;
		}
	}

	return best;
}

static int codec_hevc_ensure_mv_buffer(struct amvdec_session *sess, u32 idx)
{
	struct codec_hevc *hevc = sess->priv;

	if (idx >= MAX_REF_PIC_NUM)
		return -EINVAL;
	if (hevc->mv_vaddr[idx])
		return 0;

	hevc->mv_vaddr[idx] = meson_amvdec_dma_alloc_guarded(sess->core->dev,
							     MPRED_MV_BUF_SIZE,
							     &hevc->mv_paddr[idx],
							     GFP_KERNEL | __GFP_NOWARN,
							     &hevc->mv_guard[idx]);
	if (!hevc->mv_vaddr[idx]) {
		dev_err_ratelimited(sess->core->dev,
				    "Failed to allocate %u-byte MV buffer %u\n",
				    MPRED_MV_BUF_SIZE, idx);
		return -ENOMEM;
	}

	return 0;
}

static struct hevc_frame *
codec_hevc_prepare_new_frame(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct hevc_frame *new_frame = NULL;
	struct codec_hevc *hevc = sess->priv;
	struct vb2_v4l2_buffer *vbuf;
	union rpm_param *params = &hevc->rpm_param;
	u32 poc, lsb_mask;

	new_frame = kzalloc_obj(*new_frame);
	if (!new_frame)
		return NULL;

	vbuf = sess->request_job.dst;
	if (!vbuf) {
		dev_err_ratelimited(sess->core->dev, "No dst buffer available\n");
		kfree(new_frame);
		return NULL;
	}

	/* Require valid capture backing before programming decode addresses. */
	if (codec_hevc_ensure_mv_buffer(sess, vbuf->vb2_buf.index) ||
	    meson_amvdec_codec_hevc_ensure_frame_buffer(sess, &hevc->common,
							&vbuf->vb2_buf, hevc->is_10bit)) {
		kfree(new_frame);
		return NULL;
	}

	struct hevc_frame *old, *n;

	/* A QBUF transfers this buffer back to the decoder. Retire its
	 * old metadata before assigning a new timestamp and MV contents.
	 */
	list_for_each_entry_safe(old, n, &hevc->ref_frames_list, list) {
		if (old->vbuf != vbuf)
			continue;
		if (old->show) {
			kfree(new_frame);
			return NULL;
		}
		if (hevc->col_frame == old)
			hevc->col_frame = NULL;
		if (hevc->cur_frame == old)
			hevc->cur_frame = NULL;
		list_del(&old->list);
		kfree(old);
	}
	if (meson_amvdec_dst_buf_prepare_hevc(sess, vbuf,
					      meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
					      &new_frame->slice_ctrl, &new_frame->decode_ctrl,
					      &new_frame->input_end, &new_frame->offset)) {
		meson_amvdec_dst_buf_error_stateless(sess, vbuf);
		kfree(new_frame);
		return NULL;
	}
	/*
	 * Use the request POC when its LSB matches firmware. The client may omit
	 * RASL pictures or temporal sub-layers from the decoded sequence.
	 */
	poc = new_frame->decode_ctrl.pic_order_cnt_val;
	lsb_mask = (1U << (params->p.log2_max_pic_order_cnt_lsb_minus4 + 4)) - 1;
	if (poc != hevc->curr_poc && !((poc ^ hevc->curr_poc) & lsb_mask)) {
		if (hevc->prev_tid0_poc == hevc->curr_poc)
			hevc->prev_tid0_poc = poc;
		hevc->curr_poc = poc;
	}

	new_frame->vbuf = vbuf;
	new_frame->referenced = 1;
	new_frame->show = 1;
	new_frame->poc = hevc->curr_poc;
	new_frame->cur_slice_type = params->p.slice_type;
	new_frame->num_reorder_pic = params->p.sps_num_reorder_pics_0;

	list_add_tail(&new_frame->list, &hevc->ref_frames_list);
	hevc->frames_num++;

	return new_frame;
}

static int
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
	u32 output_size = meson_amvdec_get_output_size(sess);

	if (vb->index >= MAX_REF_PIC_NUM)
		return -EINVAL;

	val = (meson_amvdec_read_dos(core, HEVC_SAO_CTRL0) & ~0xf) |
	      ilog2(hevc->lcu_size);
	meson_amvdec_write_dos(core, HEVC_SAO_CTRL0, val);

	meson_amvdec_write_dos(core, HEVC_SAO_PIC_SIZE,
			 hevc->width | (hevc->height << 16));
	meson_amvdec_write_dos(core, HEVC_SAO_PIC_SIZE_LCU,
			 (hevc->lcu_x_num - 1) | (hevc->lcu_y_num - 1) << 16);

	if (codec_hevc_use_downsample(sess->pixfmt_cap, hevc->is_10bit) ||
	    codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       hevc->is_10bit))
		buf_y_paddr =
		     meson_amvdec_codec_hevc_fbc_body_addr(&hevc->common, vb->index);
	else
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);

	if (codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
		if (!buf_y_paddr)
			return -EINVAL;
		val = meson_amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0000;
		meson_amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
		meson_amvdec_write_dos(core, HEVC_CM_BODY_START_ADDR, buf_y_paddr);
	}

	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M) {
		if (vb2_plane_size(vb, 0) < output_size ||
		    vb2_plane_size(vb, 1) < output_size / 2)
			return -EINVAL;
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);
		buf_u_v_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 1);
		meson_amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR, buf_y_paddr);
		meson_amvdec_write_dos(core, HEVC_SAO_C_START_ADDR, buf_u_v_paddr);
		meson_amvdec_write_dos(core, HEVC_SAO_Y_WPTR, buf_y_paddr);
		meson_amvdec_write_dos(core, HEVC_SAO_C_WPTR, buf_u_v_paddr);
	}

	if (codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       hevc->is_10bit)) {
		dma_addr_t header_adr = vb2_dma_contig_plane_dma_addr(vb, 0);

		if (codec_hevc_use_downsample(sess->pixfmt_cap, hevc->is_10bit))
			header_adr = hevc->common.mmu_header_paddr[vb->index];
		if (!header_adr)
			return -EINVAL;
		meson_amvdec_write_dos(core, HEVC_CM_HEADER_START_ADDR, header_adr);
		/* use HEVC_CM_HEADER_START_ADDR */
		meson_amvdec_write_dos_bits(core, HEVC_SAO_CTRL5, BIT(10));
		meson_amvdec_write_dos_bits(core, HEVC_SAO_CTRL9, BIT(0));
	}

	meson_amvdec_write_dos(core, HEVC_SAO_Y_LENGTH,
			 output_size);
	meson_amvdec_write_dos(core, HEVC_SAO_C_LENGTH,
			 (output_size / 2));

	if (frame->cur_slice_idx == 0) {
		if (core->platform->revision >= AMVDEC_REVISION_G12A) {
			if (core->platform->revision >= AMVDEC_REVISION_SM1)
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

			meson_amvdec_write_dos(core, HEVC_DBLK_CFGB, val);
			meson_amvdec_write_dos(core, HEVC_G12A_DBLK_STS1, BIT(28));
		}

		meson_amvdec_write_dos(core, HEVC_DBLK_CFG2,
				 hevc->width | (hevc->height << 16));

		val = 0;
		if ((misc_flag0 >> PCM_ENABLE_FLAG_BIT) & 0x1)
			val |= ((misc_flag0 >>
				 PCM_LOOP_FILTER_DISABLED_FLAG_BIT) & 0x1) << 3;

		val |= (param->p.pps_cb_qp_offset & 0x1f) << 4;
		val |= (param->p.pps_cr_qp_offset & 0x1f) << 9;
		val |= (hevc->lcu_size == 64) ? 0 :
		       ((hevc->lcu_size == 32) ? 1 : 2);
		meson_amvdec_write_dos(core, HEVC_DBLK_CFG1, val);
	}

	val = meson_amvdec_read_dos(core, HEVC_SAO_CTRL1) & ~0x3ff3;
	val |= 0xff0; /* Set endianness for 2-bytes swaps (nv12) */
	if (core->platform->revision < AMVDEC_REVISION_G12A) {
		if (!codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit))
			val |= BIT(0); /* disable cm compression */

	}

	meson_amvdec_write_dos(core, HEVC_SAO_CTRL1, val);

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
		/* no downscale for NV12 */
		val = meson_amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0000;
		meson_amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
	}

	val = meson_amvdec_read_dos(core, HEVCD_IPP_AXIIF_CONFIG) & ~0x30;
	val |= 0xf;
	meson_amvdec_write_dos(core, HEVCD_IPP_AXIIF_CONFIG, val);

	val = 0;
	val_2 = meson_amvdec_read_dos(core, HEVC_SAO_CTRL0);
	val_2 &= (~0x300);

	if (param->p.tiles_flags & 1) {
		u32 across = (misc_flag0 >>
			      LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT) & 0x1;

		val |= across;
		val_2 |= across << 8;
	}

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
	   !slice_deblocking_filter_disabled_flag)) {
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

	meson_amvdec_write_dos(core, HEVC_DBLK_CFG9, val);
	meson_amvdec_write_dos(core, HEVC_SAO_CTRL0, val_2);

	meson_amvdec_write_dos(core, HEVC_sao_mem_unit, sao_mem_unit);
	meson_amvdec_write_dos(core, HEVC_SAO_ABV,
			 hevc->workspace_paddr + SAO_ABV_OFFSET);
	meson_amvdec_write_dos(core, HEVC_sao_vb_size, sao_vb_size);
	meson_amvdec_write_dos(core, HEVC_SAO_VB,
			 hevc->workspace_paddr + SAO_VB_OFFSET);
	return 0;
}

static dma_addr_t codec_hevc_get_frame_mv_paddr(struct codec_hevc *hevc,
						struct hevc_frame *frame)
{
	u32 idx = frame->vbuf->vb2_buf.index;

	if (WARN_ON_ONCE(idx >= MAX_REF_PIC_NUM))
		idx = 0;

	return hevc->mv_paddr[idx];
}

/*
 * Lay out the tile grid from what the firmware parsed out of the PPS.  The
 * RPM block has room for eight explicit sizes per axis, more than the
 * firmware's own parser takes (five).
 */
static int codec_hevc_update_tiles(struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	bool uniform = param->p.tiles_flags & 2;
	u32 cols = 1, rows = 1, i;

	hevc->tile_enabled = param->p.tiles_flags & 1;
	if (hevc->tile_enabled) {
		cols = param->p.num_tile_columns_minus1 + 1;
		rows = param->p.num_tile_rows_minus1 + 1;
	}
	if (cols > hevc->lcu_x_num || rows > hevc->lcu_y_num ||
	    cols > MAX_TILE_COL_NUM || rows > MAX_TILE_ROW_NUM ||
	    (!uniform && (cols > 9 || rows > 9)))
		return -EINVAL;

	hevc->tile_col_start[0] = 0;
	for (i = 1; i < cols; i++)
		hevc->tile_col_start[i] = uniform ?
			i * hevc->lcu_x_num / cols :
			hevc->tile_col_start[i - 1] + param->p.tile_width[i - 1];
	hevc->tile_row_start[0] = 0;
	for (i = 1; i < rows; i++)
		hevc->tile_row_start[i] = uniform ?
			i * hevc->lcu_y_num / rows :
			hevc->tile_row_start[i - 1] + param->p.tile_height[i - 1];
	hevc->tile_col_start[cols] = hevc->lcu_x_num;
	hevc->tile_row_start[rows] = hevc->lcu_y_num;
	for (i = 0; i < cols; i++)
		if (hevc->tile_col_start[i] >= hevc->tile_col_start[i + 1])
			return -EINVAL;
	for (i = 0; i < rows; i++)
		if (hevc->tile_row_start[i] >= hevc->tile_row_start[i + 1])
			return -EINVAL;

	hevc->tile_cols = cols;
	hevc->tile_rows = rows;
	return 0;
}

/* Returns true when the slice segment opens a tile other than the last one */
static bool codec_hevc_locate_tile(struct codec_hevc *hevc)
{
	u32 addr = hevc->rpm_param.p.slice_segment_address;
	u32 x = addr % hevc->lcu_x_num, y = addr / hevc->lcu_x_num;
	u32 tx = 0, ty = 0;

	if (!addr) {
		hevc->tile_x = 0;
		hevc->tile_y = 0;
		return true;
	}
	if (!hevc->tile_enabled)
		return false;
	while (tx + 1 < hevc->tile_cols && x >= hevc->tile_col_start[tx + 1])
		tx++;
	while (ty + 1 < hevc->tile_rows && y >= hevc->tile_row_start[ty + 1])
		ty++;
	if (tx == hevc->tile_x && ty == hevc->tile_y)
		return false;
	hevc->tile_x = tx;
	hevc->tile_y = ty;
	return true;
}

/* Raster LCU address to tile scan, the order slice segments arrive in */
static u32 codec_hevc_addr_ts(struct codec_hevc *hevc, u32 addr)
{
	u32 x, y, tx = 0, ty = 0, w, ts;

	if (!hevc->tile_enabled || !hevc->tile_cols || !hevc->lcu_x_num)
		return addr;
	x = addr % hevc->lcu_x_num;
	y = addr / hevc->lcu_x_num;
	while (tx + 1 < hevc->tile_cols && x >= hevc->tile_col_start[tx + 1])
		tx++;
	while (ty + 1 < hevc->tile_rows && y >= hevc->tile_row_start[ty + 1])
		ty++;
	w = hevc->tile_col_start[tx + 1] - hevc->tile_col_start[tx];
	/* whole tile rows above, then the tiles to the left in this row */
	ts = hevc->tile_row_start[ty] * hevc->lcu_x_num +
	     (hevc->tile_row_start[ty + 1] - hevc->tile_row_start[ty]) *
	     hevc->tile_col_start[tx];
	return ts + (y - hevc->tile_row_start[ty]) * w +
	       x - hevc->tile_col_start[tx];
}

static void
codec_hevc_set_mpred_ctrl(struct amvdec_core *core, struct codec_hevc *hevc,
			  bool new_tile)
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
		val |= MPRED_CTRL0_NEW_PIC;

	if (new_tile)
		val |= MPRED_CTRL0_NEW_TILE;

	meson_amvdec_write_dos(core, HEVC_MPRED_CTRL0, val);

	val = (5 - param->p.five_minus_max_num_merge_cand) |
	      (AMVP_MAX_NUM_CANDS << 4) |
	      (AMVP_MAX_NUM_CANDS_MEM << 8) |
	      (NUM_CHROMA_MODE << 12) |
	      (DM_CHROMA_IDX << 16);
	meson_amvdec_write_dos(core, HEVC_MPRED_CTRL1, val);
}

static int codec_hevc_set_mpred_mv(struct amvdec_core *core,
				   struct codec_hevc *hevc,
				   struct hevc_frame *frame,
				   struct hevc_frame *col_frame)
{
	union rpm_param *param = &hevc->rpm_param;
	u32 lcu_size_log2 = ilog2(hevc->lcu_size);
	u32 mv_mem_unit = lcu_size_log2 == 6 ? 0x200 :
			  lcu_size_log2 == 5 ? 0x80 : 0x20;
	u64 mv_extent = (u64)hevc->lcu_total * mv_mem_unit;
	u64 slice_offset = (u64)hevc->slice_addr * mv_mem_unit;
	dma_addr_t col_mv_rd_start_addr, col_mv_rd_ptr, col_mv_rd_end_addr;
	dma_addr_t mpred_mv_wr_ptr;

	if (!hevc->lcu_total || mv_extent > MPRED_MV_BUF_SIZE ||
	    slice_offset > MPRED_MV_BUF_SIZE) {
		dev_err_ratelimited(core->dev,
				    "HEVC MV extent %llu exceeds buffer %u (lcu_total %u slice %u unit %#x)\n",
				    mv_extent, MPRED_MV_BUF_SIZE,
				    hevc->lcu_total, hevc->slice_addr,
				    mv_mem_unit);
		return -EINVAL;
	}

	meson_amvdec_read_dos(core, HEVC_MPRED_CURR_LCU);

	col_mv_rd_start_addr = codec_hevc_get_frame_mv_paddr(hevc, col_frame);
	mpred_mv_wr_ptr = codec_hevc_get_frame_mv_paddr(hevc, frame) +
			  (dma_addr_t)slice_offset;
	col_mv_rd_ptr = col_mv_rd_start_addr +
			(dma_addr_t)slice_offset;
	col_mv_rd_end_addr = col_mv_rd_start_addr +
			     (dma_addr_t)mv_extent;

	meson_amvdec_write_dos(core, HEVC_MPRED_MV_WR_START_ADDR,
			 codec_hevc_get_frame_mv_paddr(hevc, frame));
	meson_amvdec_write_dos(core, HEVC_MPRED_MV_RD_START_ADDR,
			 col_mv_rd_start_addr);

	if (param->p.slice_segment_address == 0) {
		meson_amvdec_write_dos(core, HEVC_MPRED_ABV_START_ADDR,
				 hevc->workspace_paddr + MPRED_ABV_OFFSET);
		meson_amvdec_write_dos(core, HEVC_MPRED_MV_WPTR, mpred_mv_wr_ptr);
		meson_amvdec_write_dos(core, HEVC_MPRED_MV_RPTR,
				 col_mv_rd_start_addr);
	} else if (!param->p.dependent_slice_segment_flag) {
		meson_amvdec_write_dos(core, HEVC_MPRED_MV_RPTR, col_mv_rd_ptr);
	}

	meson_amvdec_write_dos(core, HEVC_MPRED_MV_RD_END_ADDR, col_mv_rd_end_addr);
	return 0;
}

/* Update motion prediction with the current slice */
static int codec_hevc_set_mpred(struct amvdec_session *sess,
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

	u32 tile_w, mv_mem_unit;

	if (!hevc->rpm_param.p.slice_segment_address &&
	    codec_hevc_update_tiles(hevc)) {
		dev_err_ratelimited(core->dev, "HEVC invalid tile grid\n");
		return -EINVAL;
	}
	if (!hevc->tile_cols)
		return -EINVAL;

	codec_hevc_set_mpred_ctrl(core, hevc, codec_hevc_locate_tile(hevc));
	if (codec_hevc_set_mpred_mv(core, hevc, frame, col_frame))
		return -EINVAL;

	/* The MV buffers are picture raster; skip the other tiles' columns */
	tile_w = hevc->tile_col_start[hevc->tile_x + 1] -
		 hevc->tile_col_start[hevc->tile_x];
	mv_mem_unit = hevc->lcu_size == 64 ? 0x200 :
		      hevc->lcu_size == 32 ? 0x80 : 0x20;
	val = (hevc->lcu_x_num - tile_w) * mv_mem_unit;
	meson_amvdec_write_dos(core, HEVC_MPRED_MV_WR_ROW_JUMP, val);
	meson_amvdec_write_dos(core, HEVC_MPRED_MV_RD_ROW_JUMP, val);
	meson_amvdec_write_dos(core, HEVC_MPRED_TILE_START,
			 hevc->tile_col_start[hevc->tile_x] |
			 hevc->tile_row_start[hevc->tile_y] << 16);
	meson_amvdec_write_dos(core, HEVC_MPRED_TILE_SIZE_LCU, tile_w |
			 (hevc->tile_row_start[hevc->tile_y + 1] -
			  hevc->tile_row_start[hevc->tile_y]) << 16);

	meson_amvdec_write_dos(core, HEVC_MPRED_PIC_SIZE,
			 hevc->width | (hevc->height << 16));

	val = ((hevc->lcu_x_num - 1) | (hevc->lcu_y_num - 1) << 16);
	meson_amvdec_write_dos(core, HEVC_MPRED_PIC_SIZE_LCU, val);

	meson_amvdec_write_dos(core, HEVC_MPRED_REF_NUM,
			 (ref_num[1] << 8) | ref_num[0]);
	meson_amvdec_write_dos(core, HEVC_MPRED_REF_EN_L0, (1 << ref_num[0]) - 1);
	meson_amvdec_write_dos(core, HEVC_MPRED_REF_EN_L1, (1 << ref_num[1]) - 1);

	meson_amvdec_write_dos(core, HEVC_MPRED_CUR_POC, hevc->curr_poc);
	meson_amvdec_write_dos(core, HEVC_MPRED_COL_POC, hevc->col_poc);

	for (i = 0; i < MAX_REF_ACTIVE; ++i) {
		meson_amvdec_write_dos(core, HEVC_MPRED_L0_REF00_POC + i * 4,
				 ref_poc_l0[i]);
		meson_amvdec_write_dos(core, HEVC_MPRED_L1_REF00_POC + i * 4,
				 ref_poc_l1[i]);
	}

	return 0;
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
		meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0);
		return;
	}

	if (hevc->cur_frame->cur_slice_type == P_SLICE) {
		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				 BIT(1));
		val = meson_amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val &= 0xffff;
		val |= (val << 16);
		meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);

		if (l0_cnt == 1) {
			meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
		} else {
			val = meson_amvdec_read_dos(core,
					      HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
			val &= 0xffff;
			val |= (val << 16);
			meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
		}
	} else { /* B_SLICE */
		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 0);
		val = meson_amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val &= 0xffff;
		val |= (val << 16);
		meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);

		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				 BIT(12) | BIT(1));
		val_2 = meson_amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val_2 &= 0xffff;
		val_2 |= (val_2 << 16);
		if (val == val_2 && l1_cnt > 1) {
			val_2 = meson_amvdec_read_dos(core,
						HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
			val_2 &= 0xffff;
			val_2 |= (val_2 << 16);
		}
		meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val_2);
	}

	/* enable mcrcc progressive-mode */
	meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0xff0);
}

static void codec_hevc_set_ref_list(struct amvdec_session *sess,
				    struct hevc_frame *cur_frame,
				    u32 ref_num, u32 *ref_poc_list, unsigned int list_no)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *ref_frame;
	struct amvdec_core *core = sess->core;
	int i;
	u32 buf_id_y;
	u32 buf_id_uv;

	for (i = 0; i < ref_num; i++) {
		ref_frame = cur_frame->ctrl_refs[list_no][i];

		if (!ref_frame)
			return;

		if (!ref_frame) {
			dev_warn_ratelimited(core->dev, "Couldn't find ref. frame %u\n",
					     ref_poc_list[i]);
			/*
			 * The canvas-data port auto-increments. Write one entry per resolved
			 * reference to preserve hardware list indices.
			 */
			ref_frame = codec_hevc_nearest_done_frame(hevc,
								  cur_frame,
							ref_poc_list[i]);
			if (!ref_frame)
				ref_frame = cur_frame;
		}

		if (codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit)) {
			buf_id_y = ref_frame->vbuf->vb2_buf.index;
			buf_id_uv = buf_id_y;
		} else {
			buf_id_y = ref_frame->vbuf->vb2_buf.index * 2;
			buf_id_uv = buf_id_y + 1;
		}

		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
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

	meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	codec_hevc_set_ref_list(sess, frame, frame->ref_num[0],
				frame->ref_poc_list[0][frame->cur_slice_idx], 0);

	if (frame->cur_slice_type == P_SLICE)
		return;

	meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
			 BIT(12) | BIT(0));
	codec_hevc_set_ref_list(sess, frame, frame->ref_num[1],
				frame->ref_poc_list[1][frame->cur_slice_idx], 1);
}

static void codec_hevc_update_pocs(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *param = &hevc->rpm_param;
	u32 nal_unit_type = param->p.nal_unit_type;
	u32 temporal_id = param->p.temporal_id & 0x7;
	int max_poc_lsb =
		1 << (param->p.log2_max_pic_order_cnt_lsb_minus4 + 4);
	int prev_poc_lsb;
	int prev_poc_msb;
	int poc_msb;
	int poc_lsb = param->p.poc_lsb;

	if (nal_unit_type == NAL_UNIT_CODED_SLICE_IDR ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_IDR_N_LP) {
		hevc->curr_poc = 0;
		if ((temporal_id - 1) == 0)
			hevc->prev_tid0_poc = hevc->curr_poc;

		return;
	}

	prev_poc_lsb = hevc->prev_tid0_poc % max_poc_lsb;
	prev_poc_msb = hevc->prev_tid0_poc - prev_poc_lsb;

	if (poc_lsb < prev_poc_lsb &&
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
	/* H.265 8.3.1: non-reference TRAIL/TSA/STSA and leading RADL/RASL
	 * pictures are not prevTid0Pic. RPM stores nuh_temporal_id_plus1.
	 */
	if (temporal_id == 1 && nal_unit_type != 0 &&
	    nal_unit_type != 2 && nal_unit_type != 4 &&
	    (nal_unit_type < 6 || nal_unit_type > 9))
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
 * Tell the firmware to skip the current slice and search for the next
 * start code without decoding: clear the decode wait flag, respond
 * ACTION_DONE and kick the MCPU.
 */
static int codec_hevc_skip_slice(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	meson_amvdec_clear_dos_bits(core, HEVC_WAIT_FLAG, BIT(1));
	meson_amvdec_write_dos_action(core, HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE);
	meson_amvdec_write_dos(core, HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
	codec_hevc_stall_arm(sess->priv);

	/* Schedule feed work after skipping a slice, which produces no capture completion. */
	schedule_work(&sess->esparser_queue_work);

	return 0;
}

/* (Re)arm the stall watchdog: the firmware now owes us an interrupt */
static void codec_hevc_stall_arm(struct codec_hevc *hevc)
{
	if (atomic_read(&hevc->stall_state) == STALL_DISABLED)
		return;
	hevc->stall_lcu = 0xffffffff;
	hevc->stall_status = 0xffffffff;
	hevc->stall_shift = 0xffffffff;
	hevc->stall_ticks = 0;
	atomic_set(&hevc->stall_state, STALL_ARMED);
	mod_delayed_work(system_dfl_wq, &hevc->stall_work,
			 msecs_to_jiffies(STALL_TIMEOUT_MS));
}

static void codec_hevc_stall_work(struct work_struct *work)
{
	struct codec_hevc *hevc = container_of(to_delayed_work(work),
					       struct codec_hevc,
					       stall_work);
	struct amvdec_session *sess = hevc->sess;
	struct amvdec_core *core = sess->core;
	u32 status, lcu, shift;

	if (atomic_read(&hevc->stall_state) != STALL_ARMED)
		return;

	status = meson_amvdec_read_dos(core, HEVC_DEC_STATUS_REG);

	/*
	 * Track stream consumption as well as LCU and status changes; firmware can
	 * make progress without reconstructing a picture.
	 */
	lcu = meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START) & 0xffffff;
	shift = meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT);
	if (lcu != hevc->stall_lcu || status != hevc->stall_status ||
	    shift != hevc->stall_shift) {
		dev_dbg(core->dev,
			"stall sample moving status=%08x lcu=%06x shift=%08x prev_status=%08x prev_lcu=%06x prev_shift=%08x queued=%d recoveries=%u\n",
			 status, lcu, shift, hevc->stall_status, hevc->stall_lcu,
			 hevc->stall_shift, atomic_read(&sess->esparser_queued_bufs),
			 hevc->stall_recoveries);
		hevc->stall_lcu = lcu;
		hevc->stall_status = status;
		hevc->stall_shift = shift;
		hevc->stall_ticks = 0;
		mod_delayed_work(system_dfl_wq, &hevc->stall_work,
				 msecs_to_jiffies(STALL_TIMEOUT_MS));
		return;
	}

	/* An empty input queue means starvation, not a decode stall. */
	if (!atomic_read(&sess->esparser_queued_bufs)) {
		mod_delayed_work(system_dfl_wq, &hevc->stall_work,
				 msecs_to_jiffies(STALL_TIMEOUT_MS));
		return;
	}

	/* Require three unchanged samples before claiming a stall. */
	if (++hevc->stall_ticks < 3) {
		dev_dbg(core->dev,
			"stall sample unchanged tick=%u status=%08x lcu=%06x shift=%08x queued=%d recoveries=%u\n",
			 hevc->stall_ticks, status, lcu, shift,
			 atomic_read(&sess->esparser_queued_bufs), hevc->stall_recoveries);
		mod_delayed_work(system_dfl_wq, &hevc->stall_work,
				 msecs_to_jiffies(STALL_GRACE_MS));
		return;
	}

	/* Claim the stall - a late IRQ from here on is discarded */
	if (atomic_cmpxchg(&hevc->stall_state, STALL_ARMED,
			   STALL_PROCESSING) != STALL_ARMED)
		return;

	mutex_lock(&hevc->lock);
	struct hevc_frame *frame;

	list_for_each_entry(frame, &hevc->ref_frames_list, list)
		dev_dbg(core->dev,
			"held poc=%u show=%u referenced=%u current=%u reorder=%u\n",
			 frame->poc, frame->show, frame->referenced,
			 frame == hevc->cur_frame, frame->num_reorder_pic);

	{
		u32 ready = v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
		u32 total = hevc->frames_num + ready;

		dev_dbg(core->dev,
			"credit frames_num=%u dst_bufs_ready=%u num_dst_bufs=%u esparser_queued_bufs=%d src_ready=%u\n",
			 hevc->frames_num, ready,
			 total,
			 atomic_read(&sess->esparser_queued_bufs),
			 v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx));
	}
	dev_warn_ratelimited(core->dev,
			     "decode stall status=%08x lcu=%06x shift=%08x ticks=%u queued=%d recoveries=%u wait=%08x rd=%08x wr=%08x - recovering in place\n",
		 status, meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START), shift,
		 hevc->stall_ticks, atomic_read(&sess->esparser_queued_bufs),
		 hevc->stall_recoveries, meson_amvdec_read_dos(core, HEVC_WAIT_FLAG),
		 meson_amvdec_read_dos(core, HEVC_STREAM_RD_PTR),
		 meson_amvdec_read_dos(core, HEVC_STREAM_WR_PTR));
	dev_err(core->dev,
		"request stall status=%x shift=%u size=%u lcu=%x total=%u; abort, no recovery completion\n",
		status, shift, meson_amvdec_read_dos(core, HEVC_DECODE_SIZE),
		meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START), hevc->lcu_total);
	hevc->request_failed = true;
	atomic_set(&hevc->stall_state, STALL_DISABLED);
	meson_amvdec_abort(sess);
	mutex_unlock(&hevc->lock);
}

/* Request indices are UAPI DPB indices, not indices into a private POC map.
 * Resolve every named entry before writing any MC or collocated-MV address.
 */
static int codec_hevc_refs_from_ctrl(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *frame = hevc->cur_frame;
	const struct v4l2_ctrl_hevc_decode_params *d = &frame->decode_ctrl;
	const struct v4l2_ctrl_hevc_slice_params *s = &frame->slice_ctrl;
	struct hevc_frame *dpb[MAX_REF_ACTIVE] = { 0 };
	u8 order[2][MAX_REF_ACTIVE];
	u32 used = 0;
	unsigned int i, j, list, total;

	/*
	 * Refuse prediction from long-term references: hardware output is inexact
	 * even with HEVC_MPRED_LT_REF set.
	 */
	if (d->num_poc_lt_curr) {
		dev_err_ratelimited(sess->core->dev, "HEVC prediction from long-term references unsupported\n");
		return -EOPNOTSUPP;
	}
	if (d->num_active_dpb_entries > MAX_REF_ACTIVE ||
	    d->num_poc_st_curr_before > MAX_REF_ACTIVE ||
	    d->num_poc_st_curr_after > MAX_REF_ACTIVE)
		return -EINVAL;
	total = d->num_poc_st_curr_before + d->num_poc_st_curr_after;
	if (total > d->num_active_dpb_entries || total > MAX_REF_ACTIVE)
		return -EINVAL;
	if ((u32)d->pic_order_cnt_val != hevc->curr_poc ||
	    s->slice_pic_order_cnt != d->pic_order_cnt_val ||
	    s->slice_type != hevc->rpm_param.p.slice_type ||
	    s->slice_segment_addr != hevc->rpm_param.p.slice_segment_address) {
		dev_err_ratelimited(sess->core->dev,
				    "HEVC control/RPM disagreement: poc=%d slice_poc=%d rpm_poc=%d\n",
			d->pic_order_cnt_val, s->slice_pic_order_cnt, (s32)hevc->curr_poc);
		return -EINVAL;
	}
	frame->poc = d->pic_order_cnt_val;
	for (i = 0; i < d->num_active_dpb_entries; i++) {
		const struct v4l2_hevc_dpb_entry *e = &d->dpb[i];
		struct vb2_buffer *vb;
		struct hevc_frame *tmp;

		if ((e->flags & ~V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE) ||
		    e->field_pic) {
			dev_err_ratelimited(sess->core->dev, "HEVC field DPB entry unsupported\n");
			return -EOPNOTSUPP;
		}
		for (j = 0; j < i; j++)
			if (d->dpb[j].timestamp == e->timestamp ||
			    d->dpb[j].pic_order_cnt_val == e->pic_order_cnt_val)
				return -EINVAL;
		vb = sess->request_job.refs[i];
		if (sess->request_job.ref_ts[i] != e->timestamp)
			return -EINVAL;
		/*
		 * Only references used by the current picture must resolve to decoded
		 * capture buffers. Unused DPB entries may describe missing pictures.
		 */
		if (!vb)
			continue;
		if (vb == &frame->vbuf->vb2_buf)
			return -EINVAL;
		list_for_each_entry(tmp, &hevc->ref_frames_list, list)
			if (&tmp->vbuf->vb2_buf == vb) {
				dpb[i] = tmp;
				break;
			}
		/* Allow unresolved entries outside the current reference sets. */
		if (!dpb[i] || (s32)dpb[i]->poc != e->pic_order_cnt_val) {
			dpb[i] = NULL;
			continue;
		}
		dev_dbg(sess->core->dev,
			"dpb current=%d slot=%u timestamp=%llu idx=%u copied=%u poc=%d pending_display=%d\n",
			 d->pic_order_cnt_val, i, e->timestamp, vb->index,
			 vb->copied_timestamp, e->pic_order_cnt_val, dpb[i]->show);
	}

	/* Validate the current POC sets and construct the default list order. */
	for (list = 0; list < 2; list++) {
		unsigned int n = list ? d->num_poc_st_curr_after : d->num_poc_st_curr_before;
		const u8 *set = list ? d->poc_st_curr_after : d->poc_st_curr_before;
		s32 previous = d->pic_order_cnt_val;

		for (i = 0; i < n; i++) {
			u8 idx = set[i];
			s32 poc;

			if (idx >= d->num_active_dpb_entries || (used & BIT(idx)))
				return -EINVAL;
			if (!dpb[idx]) {
				dev_err_ratelimited(sess->core->dev,
						    "HEVC DPB[%u] reference timestamp %llu not found\n",
					idx, d->dpb[idx].timestamp);
				return -ENOENT;
			}
			poc = d->dpb[idx].pic_order_cnt_val;
			if (list ? poc <= previous : poc >= previous)
				return -EINVAL;
			previous = poc;
			used |= BIT(idx);
			order[0][list ? d->num_poc_st_curr_before + i : i] = idx;
			order[1][list ? i : d->num_poc_st_curr_after + i] = idx;
		}
	}
	memset(frame->ctrl_refs, 0, sizeof(frame->ctrl_refs));
	frame->ref_num[0] = s->slice_type == I_SLICE ? 0 :
			   (unsigned int)s->num_ref_idx_l0_active_minus1 + 1;
	frame->ref_num[1] = s->slice_type == B_SLICE ?
			   (unsigned int)s->num_ref_idx_l1_active_minus1 + 1 : 0;
	for (list = 0; list < 2; list++) {
		const u8 *indices = list ? s->ref_idx_l1 : s->ref_idx_l0;
		u32 *pocs = frame->ref_poc_list[list][frame->cur_slice_idx];

		memset(pocs, 0, sizeof(frame->ref_poc_list[list][0]));
		if (frame->ref_num[list] > MAX_REF_ACTIVE ||
		    (frame->ref_num[list] && !total))
			return -EINVAL;
		for (i = 0; i < frame->ref_num[list]; i++) {
			u8 idx = indices[i];

			/*
			 * Validate final slice lists against the default order unless list
			 * modification is enabled; modified entries must belong to the
			 * current RPS.
			 */
			if (idx >= d->num_active_dpb_entries || !(used & BIT(idx)) ||
			    (!hevc->rpm_param.p.modification_flag &&
			     idx != order[list][i % total])) {
				dev_err_ratelimited(sess->core->dev,
						    "HEVC invalid reference list L%u[%u]\n",
						    list, i);
				return -EINVAL;
			}
			frame->ctrl_refs[list][i] = dpb[idx];
			pocs[i] = d->dpb[idx].pic_order_cnt_val;
		}
	}
	/* The MC list and its collocated MV source use the same resolved buffers. */
	hevc->col_frame = frame;
	hevc->col_poc = frame->poc;
	if (s->slice_type != I_SLICE) {
		bool from_l0 = s->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0;
		bool tmvp = s->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED;

		if (frame->ref_num[0] != hevc->rpm_param.p.num_ref_idx_l0_active ||
		    (s->slice_type == B_SLICE &&
		     frame->ref_num[1] != hevc->rpm_param.p.num_ref_idx_l1_active) ||
		    tmvp != !!hevc->rpm_param.p.slice_temporal_mvp_enable_flag ||
		    (tmvp && (from_l0 != !!hevc->rpm_param.p.collocated_from_l0_flag ||
		     s->collocated_ref_idx != hevc->rpm_param.p.collocated_ref_idx)))
			return -EINVAL;
		list = s->slice_type == B_SLICE ? !from_l0 : 0;
		if (s->collocated_ref_idx >= frame->ref_num[list])
			return -EINVAL;
		hevc->col_frame = frame->ctrl_refs[list][s->collocated_ref_idx];
		hevc->col_poc = hevc->col_frame->poc;
	}
	return 0;
}

static int codec_hevc_process_segment(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;
	union rpm_param *param = &hevc->rpm_param;
	u32 slice_segment_address = param->p.slice_segment_address;
	u32 nal_type = param->p.nal_unit_type;

	/* Skip slices until an IRAP establishes the session reference state. */
	if (!hevc->seen_irap) {
		if (nal_type >= NAL_UNIT_CODED_SLICE_BLA &&
		    nal_type <= NAL_UNIT_CODED_SLICE_CRA) {
			codec_hevc_set_resync(sess, false);
		} else {
			dev_dbg(core->dev,
				"skipping pre-IRAP slice (nal %u)\n",
				nal_type);
			return codec_hevc_skip_slice(sess);
		}
	}

	/* First slice: new frame */
	if (slice_segment_address == 0) {
		hevc->cur_frame = codec_hevc_prepare_new_frame(sess);
		if (!hevc->cur_frame)
			return -1;
	} else {
		/* Skip continuation slices without a current frame. */
		if (!hevc->cur_frame)
			return codec_hevc_skip_slice(sess);

		/* Bound the slice index before accessing per-slice reference arrays. */
		if (hevc->cur_frame->cur_slice_idx + 1 >= MAX_SLICE_NUM) {
			dev_warn_ratelimited(core->dev,
					     "%u slice segments without a frame start; dropping frame\n",
					     MAX_SLICE_NUM);
			return codec_hevc_skip_slice(sess);
		}

		hevc->cur_frame->cur_slice_idx++;
	}

	struct hevc_frame *f = hevc->cur_frame;
	unsigned int idx = f->cur_slice_idx;

	if (idx >= sess->request_job.num_slices || idx >= MAX_SLICE_NUM ||
	    sess->request_job.slices[idx].slice_segment_addr != slice_segment_address ||
	    (idx && codec_hevc_addr_ts(hevc, slice_segment_address) <=
	     codec_hevc_addr_ts(hevc,
				sess->request_job.slices[idx - 1].slice_segment_addr))) {
		dev_err(core->dev, "slice order/address disagreement idx=%u addr=%u count=%u\n",
			idx, slice_segment_address, sess->request_job.num_slices);
		return -EINVAL;
	}
	f->slice_ctrl = sess->request_job.slices[idx];
	dev_dbg(core->dev, "segment ts=%llu slice=%u/%u addr=%u lcu=%x\n",
		sess->request_job.timestamp, idx, sess->request_job.num_slices,
		slice_segment_address, meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START));
	if (codec_hevc_refs_from_ctrl(sess)) {
		struct hevc_frame *bad = hevc->cur_frame;

		dev_err_ratelimited(core->dev, "HEVC invalid request references; failing current picture\n");
		meson_amvdec_dst_buf_error_stateless(sess, bad->vbuf);
		meson_amvdec_remove_ts(sess, bad->vbuf->vb2_buf.timestamp);
		hevc->frames_num--;
		list_del(&bad->list);
		hevc->cur_frame = NULL;
		hevc->col_frame = NULL;
		kfree(bad);
		return -EINVAL;
	}
	codec_hevc_update_ldc_flag(hevc);
	if (codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       hevc->is_10bit) &&
	    meson_amvdec_codec_hevc_fill_mmu_map(sess, &hevc->common,
				    &hevc->cur_frame->vbuf->vb2_buf,
				    hevc->is_10bit))
		return -EINVAL;
	/* Reject an absent current frame before programming reconstruction. */
	if (WARN_ON_ONCE(!hevc->cur_frame))
		return -1;

	codec_hevc_set_mc(sess, hevc->cur_frame);
	codec_hevc_set_mcrcc(sess);
	if (codec_hevc_set_mpred(sess, hevc->cur_frame, hevc->col_frame))
		return -EINVAL;
	if (codec_hevc_set_sao(sess, hevc->cur_frame))
		return -EINVAL;

	dev_dbg(core->dev,
		"segment kick mode=%08x nal=%u slice_type=%u slice_addr=%u first=%u poc=%u curr_poc=%u frame_off=%u shift=%08x rd=%08x wr=%08x wait=%08x queued=%d frames=%u dst_idx=%u\n",
		 sess->fmt_out->pixfmt, nal_type, param->p.slice_type,
		 slice_segment_address, param->p.first_slice_segment_in_pic_flag,
		 param->p.poc_lsb, hevc->curr_poc, hevc->cur_frame->offset,
		 meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
		 meson_amvdec_read_dos(core, HEVC_STREAM_RD_PTR),
		 meson_amvdec_read_dos(core, HEVC_STREAM_WR_PTR),
		 meson_amvdec_read_dos(core, HEVC_WAIT_FLAG),
		 atomic_read(&sess->esparser_queued_bufs), hevc->frames_num,
		 hevc->cur_frame->vbuf->vb2_buf.index);

	f = hevc->cur_frame;
	bool final_segment = f->cur_slice_idx + 1 == sess->request_job.num_slices;
	u32 header_shift = meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT);
	u32 decode_size = 0;

	/*
	 * Check the cumulative request endpoint, allowing one 64-bit fetch of
	 * shifter read-ahead beyond it.
	 */
	if (hevc->request_origin || !f->input_end ||
	    f->input_end <= f->offset ||
	    header_shift > f->input_end + 8)
		return -EINVAL;
	if (final_segment)
		decode_size = f->input_end;
	meson_amvdec_write_dos(core, HEVC_DECODE_SIZE, decode_size);
	dev_dbg(core->dev, "kick ts=%llu idx=%u poc=%d slice=%u/%u start=%u endpoint=%u origin=%u header_shift=%u size=%u queued=%d\n",
		f->vbuf->vb2_buf.timestamp, f->vbuf->vb2_buf.index,
		 (s32)f->poc, f->cur_slice_idx, sess->request_job.num_slices,
		 f->offset, f->input_end, hevc->request_origin, header_shift,
		 meson_amvdec_read_dos(core, HEVC_DECODE_SIZE),
		 atomic_read(&sess->esparser_queued_bufs));
	meson_amvdec_write_dos_bits(core, HEVC_WAIT_FLAG, BIT(1));
	meson_amvdec_write_dos_action(core, HEVC_DEC_STATUS_REG,
				HEVC_CODED_SLICE_SEGMENT_DAT);

	/* Interrupt the firmware's processor */
	meson_amvdec_write_dos(core, HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
	meson_amvdec_trace(sess, AMVDEC_TR_KICK, meson_amvdec_read_dos(core, HEVC_DECODE_SIZE),
		     hevc->cur_frame ? hevc->cur_frame->cur_slice_idx : ~0U,
		     slice_segment_address);
	dev_dbg(core->dev,
		"segment kicked status=%08x wait=%08x shift=%08x rd=%08x wr=%08x queued=%d\n",
		 meson_amvdec_read_dos(core, HEVC_DEC_STATUS_REG),
		 meson_amvdec_read_dos(core, HEVC_WAIT_FLAG),
		 meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
		 meson_amvdec_read_dos(core, HEVC_STREAM_RD_PTR),
		 meson_amvdec_read_dos(core, HEVC_STREAM_WR_PTR),
		 atomic_read(&sess->esparser_queued_bufs));
	codec_hevc_stall_arm(hevc);

	return 0;
}

static int codec_hevc_process_rpm(struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	int src_changed = 0;
	u32 dst_width, dst_height;
	u32 lcu_size, lcu_size_log2;
	u32 is_10bit = 0;

	if (param->p.slice_segment_address	||
	    !param->p.pic_width_in_luma_samples	||
	    !param->p.pic_height_in_luma_samples)
		return 0;

	lcu_size_log2 = param->p.log2_min_coding_block_size_minus3 + 3 +
		       param->p.log2_diff_max_min_coding_block_size;
	if (lcu_size_log2 < 4 || lcu_size_log2 > 6)
		return -EINVAL;
	lcu_size = 1U << lcu_size_log2;
	if (param->p.pic_width_in_luma_samples <= lcu_size &&
	    param->p.pic_height_in_luma_samples > lcu_size) {
		dev_dbg(hevc->sess->core->dev,
			"HEVC pictures with one CTB column and multiple rows are unsupported\n");
		return -EOPNOTSUPP;
	}

	if (param->p.pic_width_in_luma_samples <
		amvdec_min_coded_width(hevc->sess->fmt_out->pixfmt) ||
	    param->p.pic_height_in_luma_samples <
		amvdec_min_coded_height(hevc->sess->fmt_out->pixfmt)) {
		dev_err_ratelimited(hevc->sess->core->dev,
				    "HEVC coded size %ux%u below hardware floor\n",
				    param->p.pic_width_in_luma_samples,
				    param->p.pic_height_in_luma_samples);
		return -EINVAL;
	}
	if (param->p.pic_width_in_luma_samples >
		hevc->sess->fmt_out->max_width ||
	    param->p.pic_height_in_luma_samples >
		hevc->sess->fmt_out->max_height) {
		dev_err_ratelimited(hevc->sess->core->dev,
				    "HEVC coded size %ux%u above format maximum\n",
				    param->p.pic_width_in_luma_samples,
				    param->p.pic_height_in_luma_samples);
		return -EINVAL;
	}
	if (param->p.pic_width_in_luma_samples != hevc->sess->width ||
	    param->p.pic_height_in_luma_samples != hevc->sess->height) {
		dev_err_ratelimited(hevc->sess->core->dev,
				    "HEVC coded size changed from negotiated %ux%u to %ux%u\n",
				    hevc->sess->width, hevc->sess->height,
				    param->p.pic_width_in_luma_samples,
				    param->p.pic_height_in_luma_samples);
		return -EINVAL;
	}

	if (param->p.bit_depth)
		is_10bit = 1;

	hevc->width = param->p.pic_width_in_luma_samples;
	hevc->height = param->p.pic_height_in_luma_samples;
	dst_width = hevc->width;
	dst_height = hevc->height;

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

/* Read firmware parameters from the workspace RPM block. */
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

static void codec_hevc_configure(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	dev_dbg(core->dev,
		"storage depth=%u rpm_depth=%u mmu=%u fbc=%u downsample=%u width=%u height=%u\n",
		 sess->bitdepth, hevc->rpm_param.p.bit_depth,
		 codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap, hevc->is_10bit),
		 codec_hevc_use_fbc(sess->pixfmt_cap, hevc->is_10bit),
		 codec_hevc_use_downsample(sess->pixfmt_cap, hevc->is_10bit),
		 sess->width, sess->height);

	if (meson_amvdec_codec_hevc_setup_buffers(sess, &hevc->common, hevc->is_10bit)) {
		meson_amvdec_abort(sess);
		return;
	}

	/*
	 * GXBB, GXL and GXM firmware moves RPM from scratch 1 to scratch 8 during
	 * boot and reuses scratch 1 as parser state. Only G12A, G12B and SM1 need
	 * the workspace map replay after MMU allocation.
	 */
	if (amvdec_is_g12(core))
		codec_hevc_setup_workspace(sess, hevc);
	meson_amvdec_codec_hevc_setup_decode_head(sess, hevc->is_10bit);
	codec_hevc_process_segment_header(sess);
	if (codec_hevc_process_segment(sess))
		meson_amvdec_abort(sess);
}

/* Called after input DMA and input-credit publication. The corresponding
 * request snapshot and CAPTURE backing exist before the feed.
 */
void meson_amvdec_codec_hevc_request_input_ready(struct amvdec_session *sess, u64 ts, u32 end)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!hevc)
		return;
	mutex_lock(&hevc->lock);
	meson_amvdec_trace(sess, AMVDEC_TR_READY, end, hevc->request_parked,
		     meson_amvdec_read_dos(core, HEVC_DEC_STATUS_REG));
	/*
	 * A small picture can be decoded, and the firmware parked at 0x0a,
	 * before the feed worker that submitted it gets here.  The input being
	 * reported is then the picture that has just completed, not the next
	 * one: there is nothing to release and nothing wrong.
	 */
	if (hevc->request_parked && hevc->cur_frame &&
	    end == hevc->cur_frame->input_end) {
		mutex_unlock(&hevc->lock);
		return;
	}
	if (hevc->request_parked && !hevc->request_failed) {
		if (!hevc->cur_frame || end <= hevc->cur_frame->input_end ||
		    meson_amvdec_read_dos(core, HEVC_DEC_STATUS_REG) != 0x0a) {
			hevc->request_failed = true;
			dev_err(core->dev, "invalid next request\n");
			meson_amvdec_abort(sess);
		} else {
			/*
			 * Firmware parked at 0x0a polls the status register; resume with
			 * 0xff
			 * without an MCPU interrupt. Clear the host engine-work bit only
			 * after
			 * this picture completes, because the park consumes its MPRED event.
			 */
			meson_amvdec_clear_dos_bits(core, HEVC_WAIT_FLAG, BIT(1));
			meson_amvdec_write_dos_action(core, HEVC_DEC_STATUS_REG, 0xff);
			/* Order the park acknowledgment before the next endpoint. */
			wmb();
			meson_amvdec_write_dos(core, HEVC_DECODE_SIZE, end);
			hevc->request_parked = false;
			dev_dbg(core->dev, "resume ts=%llu endpoint=%u queued=%d\n",
				ts, end, atomic_read(&sess->esparser_queued_bufs));
			codec_hevc_stall_arm(hevc);
		}
	}
	mutex_unlock(&hevc->lock);
}

static irqreturn_t codec_hevc_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 dec_status = meson_amvdec_read_dos(core, HEVC_DEC_STATUS_REG);
	int ret;

	if (!hevc)
		return IRQ_HANDLED;

	mutex_lock(&hevc->lock);
	if (hevc->request_failed)
		goto unlock;
	meson_amvdec_trace(sess, AMVDEC_TR_IRQ, dec_status,
		     meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
		     meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START));
	dev_dbg(core->dev, "irq ts=%llu status=%x slice=%d/%u lcu=%x\n",
		sess->request_job.timestamp, dec_status,
		hevc->cur_frame ? (int)hevc->cur_frame->cur_slice_idx : -1,
		sess->request_job.num_slices,
		meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START));
	dev_dbg(core->dev, "request irq status=%x shift=%u size=%u lcu=%x total=%u queued=%d\n",
		dec_status, meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
		meson_amvdec_read_dos(core, HEVC_DECODE_SIZE),
		meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START), hevc->lcu_total,
		atomic_read(&sess->esparser_queued_bufs));
	if (dec_status == 0x0a) {
		struct hevc_frame *f = hevc->cur_frame;
		u32 lcu = meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START) & 0xffffff;

		if (!hevc->request_size || !f || !f->show || hevc->request_parked ||
		    f->vbuf != sess->request_job.dst) {
			dev_err(core->dev, "unassociated/duplicate done\n");
			hevc->request_failed = true;
			meson_amvdec_abort(sess);
			goto unlock;
		}
		dev_dbg(core->dev, "end ts=%llu slice=%u/%u lcu=%x total=%u\n",
			sess->request_job.timestamp, f->cur_slice_idx,
			sess->request_job.num_slices, lcu, hevc->lcu_total);
		if (f->cur_slice_idx + 1 != sess->request_job.num_slices ||
		    !hevc->lcu_total || lcu != hevc->lcu_total - 1) {
			dev_err(core->dev,
				"early completion: slice=%u/%u lcu=%x total=%u\n",
				f->cur_slice_idx, sess->request_job.num_slices,
				lcu, hevc->lcu_total);
			hevc->request_failed = true;
			meson_amvdec_abort(sess);
			goto unlock;
		}
		meson_amvdec_trace(sess, AMVDEC_TR_DONE,
				   meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
				   meson_amvdec_read_dos(core, HEVC_DECODE_SIZE), lcu);
		meson_amvdec_write_dos(core, HEVC_DECODE_SIZE, 0);
		/* Publish the zero endpoint before completing this request. */
		wmb();
		hevc->request_parked = true;
		meson_amvdec_codec_hevc_arm_restream(sess);
		f->show = 0;
		hevc->frames_num--;
		dev_dbg(core->dev, "done ts=%llu idx=%u poc=%d; parked\n",
			f->vbuf->vb2_buf.timestamp, f->vbuf->vb2_buf.index, (s32)f->poc);
		meson_amvdec_request_signal(sess, f->vbuf);
		goto unlock;
	}
	/* Wait for input when the firmware reports an empty stream buffer. */
	if (dec_status == HEVC_SEARCH_BUFEMPTY ||
	    dec_status == HEVC_DECODE_BUFEMPTY) {
		dev_dbg(core->dev,
			"input starved status=%x queued=%d; waiting\n",
			dec_status,
			atomic_read(&sess->esparser_queued_bufs));
		goto unlock;
	}
	/*
	 * After handling picture completion and starvation, require a slice-header
	 * report. Other statuses cannot complete the active request.
	 */
	if (dec_status != HEVC_SLICE_SEGMENT_DONE) {
		dev_err(core->dev,
			"non-picture-done status=%x parked=%d queued=%d frames=%u cur=%d size=%u shift=%u origin=%u\n",
			dec_status, hevc->request_parked,
			atomic_read(&sess->esparser_queued_bufs),
			hevc->frames_num,
			hevc->cur_frame ? (int)hevc->cur_frame->cur_slice_idx : -1,
			meson_amvdec_read_dos(core, HEVC_DECODE_SIZE),
			meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
			hevc->request_origin);
		hevc->request_failed = true;
		meson_amvdec_abort(sess);
		goto unlock;
	}

	if (dec_status != HEVC_SLICE_SEGMENT_DONE) {
		dev_err_ratelimited(core->dev_dec, "Unrecognized dec_status: %08X\n",
				    dec_status);
		meson_amvdec_abort(sess);
		goto unlock;
	}

	codec_hevc_fetch_rpm(sess);
	ret = codec_hevc_process_rpm(hevc);
	if (ret < 0) {
		meson_amvdec_abort(sess);
		goto unlock;
	}
	if (ret) {
		codec_hevc_configure(sess);
		goto unlock;
	}

	codec_hevc_process_segment_header(sess);
	if (codec_hevc_process_segment(sess))
		meson_amvdec_abort(sess);

unlock:
	mutex_unlock(&hevc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t codec_hevc_isr(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;

	if (hevc) {
		/* Disarm the watchdog; discard late IRQs after a stall claims the decoder. */
		if (atomic_cmpxchg(&hevc->stall_state, STALL_ARMED,
				   STALL_IDLE) == STALL_PROCESSING)
			return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops meson_amvdec_codec_hevc_ops = {
	.start = codec_hevc_start,
	.stop = codec_hevc_stop,
	.isr = codec_hevc_isr,
	.threaded_isr = codec_hevc_threaded_isr,
};
