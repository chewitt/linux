// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * H.264 request decoding with multi-instance firmware.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-h264.h>

#include "amvdec_helpers.h"
#include "dos_regs.h"
#include <linux/spinlock.h>

#include "codec_h264.h"
#include "codec_h264_refs.h"
#define SIZE_EXT_FW_MULTI	((20 + 16) * SZ_1K)
#define SIZE_MULTI_FW_MIN	0x7000
#define SIZE_MULTI_AUX	SZ_16K
#define SIZE_WORKSPACE	0x200000
#define SIZE_WORKSPACE_MULTI	0x228080
#define SIZE_WORKSPACE_MULTI_RP	(SIZE_WORKSPACE_MULTI + 2 * SZ_4K)
#define SIZE_SEI	(8 * SZ_1K)
#define SIZE_LMEM	PAGE_SIZE

#define DCAC_READ_MARGIN	SZ_64K

#define MC_OFFSET_HEADER	0x0000
#define MC_OFFSET_DATA		0x1000
#define MC_OFFSET_MMCO		0x2000
#define MC_OFFSET_LIST		0x3000
#define MC_OFFSET_SLICE		0x4000
#define MC_OFFSET_MAIN		0x5000
#define MC_SWAP_SIZE		SZ_4K

#define LMEM_DUMP_ADR	AV_SCRATCH_L
#define H264_DECODE_MODE	AV_SCRATCH_4
#define H264_DECODE_SEQINFO	AV_SCRATCH_5
#define H264_DECODE_SIZE	AV_SCRATCH_E
#define H264_AUX_ADR		AV_SCRATCH_C
#define H264_AUX_DATA_SIZE	AV_SCRATCH_H
#define DPB_STATUS_REG		AV_SCRATCH_J
#define INIT_FLAG_REG		AV_SCRATCH_2
#define HEAD_PADING_REG		AV_SCRATCH_3
#define NAL_SEARCH_CTL		AV_SCRATCH_9

#define DECODE_MODE_MULTI_FRAMEBASE	0x1
#define H264_SLICE_HEAD_DONE		0x01
#define H264_PIC_DATA_DONE		0x02
#define H264_CONFIG_REQUEST		0x11
#define H264_ACTION_SEARCH_HEAD		0xf0
#define H264_ACTION_DECODE_SLICE	0xf1
#define H264_ACTION_CONFIG_DONE		0xf2
#define H264_ACTION_DECODE_NEWPIC	0xf3

/* Scale register word offsets for byte-addressed accessors. */
#define VDEC_ASSIST_MMC_CTRL1	0x0008	/* 0x0002 */
#define WRRSP_LMEM		0x0d4c	/* 0x0353 */
#define AV_SCRATCH_M		0x2758	/* 0x09d6 */
#define MDEC_PIC_DC_MUX_CTRL	0x2634	/* 0x098d */
#define MDEC_EXTIF_CFG1		0x2794	/* 0x09e5 */
#define MDEC_EXTIF_CFG2		0x2798	/* 0x09e6 */
#define IQIDCT_CONTROL		0x3838	/* 0x0e0e */
#define FRAME_COUNTER_REG	AV_SCRATCH_I
#define DEBUG_REG1		AV_SCRATCH_M

/* Per-picture decode registers, expressed as byte offsets. */
#define DBKR_CANVAS_ADDR		0x26c0
#define DBKW_CANVAS_ADDR		0x26c4
#define REC_CANVAS_ADDR			0x26c8
#define CURR_CANVAS_CTRL		0x26cc
#define H264_BUFFER_INFO_DATA		0x3088	/* PMV2_X  0x0c22 */
#define H264_BUFFER_INFO_INDEX		0x3090	/* PMV3_X  0x0c24 */
#define H264_CURRENT_POC_IDX_RESET	0x30c0	/* LAST_SLICE_MV_ADDR 0x0c30 */
#define H264_CURRENT_POC		0x30c8	/* LAST_MVY 0x0c32 */
#define H264_CO_MB_WR_ADDR		0x30e0	/* VLD_C38 0x0c38 */
#define H264_CO_MB_RD_ADDR		0x30e4	/* VLD_C39 0x0c39 */
#define H264_CO_MB_RW_CTL		0x30f4	/* VLD_C3D 0x0c3d */
#define H264_MBY_MBX			0x301c	/* MB_MOTION_MODE 0x0c07 */

/* RPM (LMEM) word offsets */
#define H264_RPM_LOG2_MAX_FRAME_NUM	0x83
#define H264_RPM_FRAME_MBS_ONLY_FLAG	0x84
#define H264_RPM_PIC_ORDER_CNT_TYPE	0x85
#define H264_RPM_LOG2_MAX_POC_LSB	0x86
#define H264_RPM_TOTAL_MB_HEIGHT	0x8f
#define H264_RPM_MB_X_NUM		0xb0
#define H264_RPM_MB_HEIGHT		0xb2
#define H264_RPM_PREV_MB_WIDTH		0xf1
#define H264_RPM_PREV_FRAME_SIZE_IN_MB	0xf2

/* Canvas-table slots reserved for CAPTURE buffers (ANC0_CANVAS_ADDR). */
#define H264_MULTI_MAX_FW_BUFS		24

#define H264_COLOCATE_BYTES_PER_MB	96

/*
 * G12B firmware reports SEI status 0x53; acknowledge with 0x54 to advance
 * to CONFIG_REQUEST (0x11).
 */
#define H264_SEI_DATA_READY		0x53
#define H264_SEI_DATA_DONE		0x54

struct h264_field_ref_scratch {
	struct v4l2_h264_reflist_builder builder;
	struct v4l2_h264_reference ordered[2][V4L2_H264_REF_LIST_LEN];
	struct v4l2_ctrl_h264_decode_params params;
};

struct codec_h264 {
	/* H.264 decoder requires an extended firmware */
	void      *ext_fw_vaddr;
	dma_addr_t ext_fw_paddr;
	u32        ext_fw_size;
	u32        multi_irq_count;
	bool       multi_powered;
	bool       multi_hw_started;

	/* Buffer for the H.264 Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;
	u32        workspace_size;

	/* Buffer for parsed SEI data */
	void      *sei_vaddr;
	dma_addr_t sei_paddr;

	/* Auxiliary firmware buffer. */
	void      *aux_vaddr;
	dma_addr_t aux_paddr;

	/* LMEM page the multi firmware publishes its parameter block through */
	void      *lmem_vaddr;
	dma_addr_t lmem_paddr;

	/* Colocated MV store for the multi blob, one slot per CAPTURE frame */
	void      *colocate_vaddr;
	dma_addr_t colocate_paddr;
	u32        colocate_size;
	u32        colocate_buf_size;

	/*
	 * Per-picture state for the multi blob, captured from the V4L2
	 * controls when the access unit is fed. The firmware decodes one
	 * picture at a time, so a single record suffices.
	 */
	bool       multi_pic_valid;
	int        multi_params_error;
	/* Last PPS spliced into the bitstream; the firmware parses only that. */
	u8 multi_last_headers[H264_SYNTH_MAX_HEADERS];
	u32 multi_last_sps_len, multi_last_headers_len;
	bool       multi_headers_valid;
	bool       multi_pic_started;
	bool       multi_done_pending;
	bool       multi_done_error;
	struct h264_slice_refs multi_slice_refs;
	struct h264_slice_refs multi_request_picture;
	bool multi_geometry_valid;
	u32 multi_geometry_width_mbs, multi_geometry_height_mbs;
	const u8 *multi_slice_data; /* cached copy of the AU; immutable until completion */
	u8        *multi_src_vaddr;  /* the OUTPUT buffer itself, which the VLD reads */
	u8        *scan_copy;        /* see meson_amvdec_codec_h264_scan_copy() */
	u32        scan_copy_size;
	u32 multi_slice_size, multi_slice_cursor, multi_slice_count;
	u32 multi_slice_type_mask, multi_slice_mv_offset;
	bool multi_slice_uniform;
	bool multi_field_picture;
	bool multi_field_mate;
	struct h264_field_surface multi_field_slots[H264_MULTI_MAX_FW_BUFS];
	u32 multi_decoded_fields;
	spinlock_t multi_field_lock; /* Terminal state and provenance publication. */
	u32 multi_field_phase; /* 0 decoding, 2 ready, 3 failed, 4 claimed */
	struct h264_slice_refs multi_fields[1];
	/*
	 * Working arrays kept off the stack.  write_ref_lists runs in the
	 * threaded ISR, field_preflight in the feed worker: each has its own.
	 */
	struct h264_ref wrl_refs[V4L2_H264_NUM_DPB_ENTRIES];
	struct h264_ref wrl_l0[V4L2_H264_REF_LIST_LEN];
	struct h264_ref wrl_l1[V4L2_H264_REF_LIST_LEN];
	struct h264_ref pre_refs[V4L2_H264_REF_LIST_LEN];
	struct h264_slice_refs pre_first;
	struct h264_field_ref_scratch pre_field_refs;
	struct h264_field_ref_scratch wrl_field_refs;
	/* HEADER parsing runs in the threaded ISR, apart from sess->feed. */
	struct h264_parse_scratch irq_parse;

	bool       multi_sps_mbaff;
	u32        multi_done_fw_idx;
	struct vb2_v4l2_buffer *multi_src_vbuf;
	u32        multi_pic_count;
	u64        multi_pic_ts;
	/* Sequence words, re-applied after each per-picture VLD reset. */
	u32        multi_seq_cfg;
	u32        multi_seq_cfg2;
	u32        multi_seq_cfgb;
	u32        multi_seq_scratch0;
	u32        multi_nal_search_or;
	u32        multi_pic_vb2_idx;
	u32        multi_pic_fw_idx;
	s32        multi_pic_top_poc;
	s32        multi_pic_bottom_poc;
	u32        multi_sps_max_refs;
	bool       multi_l1_valid;
	u32        multi_l1_fw_idx;
	u8         multi_l1_structure, multi_l1_view;
	s32        multi_l1_top;
	s32        multi_l1_bottom;
	struct v4l2_ctrl_h264_sps multi_sps;
	struct v4l2_ctrl_h264_pps multi_pps;
	struct v4l2_h264_dpb_entry multi_dpb[V4L2_H264_NUM_DPB_ENTRIES];
	u8         multi_nal_ref_idc;

	u32 mb_width;
	u32 mb_height;
};

static u16 codec_h264_multi_lmem_word(const struct codec_h264 *h264, u32 i)
{
	const u16 *lmem = h264->lmem_vaddr;

	return lmem[(i & ~3) + 3 - (i & 3)];
}

static u8 codec_h264_multi_current_structure(const struct codec_h264 *h264)
{
	if (h264->multi_field_picture)
		return h264->multi_slice_refs.bottom_field ? 1 : 0;
	if (h264->multi_slice_refs.valid)
		return h264->multi_slice_refs.mbaff ? 3 : 2;
	return h264->multi_sps_mbaff ? 3 : 2;
}

static dma_addr_t codec_h264_multi_workspace_base(const struct codec_h264 *h264)
{
	return h264->workspace_paddr + DCAC_READ_MARGIN;
}

/* Answer firmware configuration requests from the negotiated format and SPS. */
static void codec_h264_multi_write_dpb_config(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	u32 config = h264->multi_seq_scratch0;

	if (!config) {
		u32 max_refs = h264->multi_sps_max_refs ?
			       h264->multi_sps_max_refs : 1;

		config = ((max_refs + 1) << 24) |
			 (V4L2_H264_NUM_DPB_ENTRIES << 16) |
			 (V4L2_H264_NUM_DPB_ENTRIES << 8);
	}
	meson_amvdec_write_dos(core, AV_SCRATCH_0, config);
	dev_dbg(core->dev_dec, "h264 multi scratch0: %#x\n", config);
}

/* Resolve active DPB timestamps to CAPTURE buffers and their firmware canvas slots. */
static int codec_h264_multi_ref_fw_idx(struct amvdec_session *sess,
				       const struct v4l2_h264_dpb_entry *e)
{
	struct vb2_queue *q = v4l2_m2m_get_dst_vq(sess->m2m_ctx);
	struct vb2_buffer *vb = vb2_find_buffer(q, e->reference_ts);
	u32 idx;

	if (!vb)
		return -ENOENT;
	idx = vb->index;
	if (idx >= ARRAY_SIZE(sess->vb2_idx_to_fw_idx) ||
	    sess->vb2_idx_to_fw_idx[idx] >= sess->num_fw_bufs)
		return -ENOENT;
	return sess->vb2_idx_to_fw_idx[idx];
}

static const struct v4l2_h264_dpb_entry *
codec_h264_multi_ref_in_slot(struct amvdec_session *sess, u32 fw_slot)
{
	struct codec_h264 *h264 = sess->priv;
	int i;

	for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
		const struct v4l2_h264_dpb_entry *e = &h264->multi_dpb[i];

		if (!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;
		if (codec_h264_multi_ref_fw_idx(sess, e) == (int)fw_slot)
			return e;
	}
	return NULL;
}

/*
 * Build frame reference lists per H.264 8.2.4.2. P uses descending PicNum
 * then ascending long-term indices. B orders short-term references around
 * the current POC, then appends long-term references. Swap the first two
 * entries of an identical B list1 when it contains multiple entries.
 * Pack four canvas/structure entries per word and pad with the last entry.
 */
static void codec_h264_multi_write_list(struct amvdec_session *sess, u32 index,
					const struct h264_ref *refs, u32 n, u32 view)
{
	struct amvdec_core *core = sess->core;
	u32 i, words = 0, val = 0, last = 0;

	meson_amvdec_write_dos(core, H264_BUFFER_INFO_INDEX, index);
	for (i = 0; i < n; i++) {
		last = (refs[i].fw & 0x1f) | ((refs[i].view ? refs[i].view : view) << 5);
		val = (val << 8) | last;
		if ((i & 3) == 3) {
			meson_amvdec_write_dos(core, H264_BUFFER_INFO_DATA, val);
			words++;
			val = 0;
		}
	}
	if (n & 3) {
		for (i = n & 3; i < 4; i++)
			val = (val << 8) | last;
		meson_amvdec_write_dos(core, H264_BUFFER_INFO_DATA, val);
		words++;
	}
	val = (last << 24) | (last << 16) | (last << 8) | last;
	for (; words < 8; words++)
		meson_amvdec_write_dos(core, H264_BUFFER_INFO_DATA, val);
}

/* Read reference-list modifications from slice headers in frame-based mode. */
void meson_amvdec_codec_h264_multi_set_slice_refs(struct amvdec_session *sess,
				     const struct h264_slice_refs *sr)
{
	struct codec_h264 *h264 = sess->priv;

	if (!h264)
		return;
	if (sr)
		h264->multi_slice_refs = *sr;
	else
		memset(&h264->multi_slice_refs, 0,
		       sizeof(h264->multi_slice_refs));
}

/*
 * Compare the SPS and PPS syntax against the firmware header cache. Emit
 * changed headers before any picture, including changes under the same ID.
 */
int meson_amvdec_codec_h264_multi_headers(struct amvdec_session *sess,
			     const struct v4l2_ctrl_h264_sps *sps,
			    const struct v4l2_ctrl_h264_pps *pps,
			    const struct v4l2_ctrl_h264_scaling_matrix *sm,
			    bool force, u8 *dst, u32 cap, u32 *len)
{
	struct codec_h264 *h264 = sess->priv;
	u32 ns, np, total;
	bool same_sps, same_pps;
	int ret;

	*len = 0;
	if (!h264)
		return -EINVAL;
	ret = h264_synth_sps(sps, dst, cap, &ns);
	if (ret)
		goto invalidate;
	ret = h264_synth_pps(sps, pps, sm, dst + ns, cap - ns, &np);
	if (ret)
		goto invalidate;
	total = ns + np;
	if (total > sizeof(h264->multi_last_headers)) {
		ret = -ENOSPC;
		goto invalidate;
	}
	/* Compare the actual syntax consumed by firmware, including resolved
	 * scaling matrices. Control padding and unused lists are not syntax.
	 */
	same_sps = h264->multi_headers_valid &&
		   ns == h264->multi_last_sps_len &&
		   !memcmp(dst, h264->multi_last_headers, ns);
	same_pps = same_sps && total == h264->multi_last_headers_len &&
		   !memcmp(dst + ns, h264->multi_last_headers + ns, np);
	memcpy(h264->multi_last_headers, dst, total);
	h264->multi_last_sps_len = ns;
	h264->multi_last_headers_len = total;
	if (force || !same_sps) {
		*len = total;
	} else if (!same_pps) {
		memmove(dst, dst + ns, np);
		*len = np;
	}
	/* A queued header is not yet firmware state. HEADER_DONE commits it;
	 * a failed submission leaves the next request requiring both headers.
	 */
invalidate:
	h264->multi_headers_valid = false;
	return ret;
}

/* The upstream builder expands 16 stores into up to 32 ordered field views. */
static int codec_h264_multi_field_refs(struct amvdec_session *sess,
				       struct h264_ref *list, u32 *count, u32 lx, bool preflight)
{
	struct codec_h264 *h264 = sess->priv;
	const struct h264_slice_refs *sr = preflight ? &h264->multi_fields[0] :
						    &h264->multi_slice_refs;
	struct h264_field_ref_scratch *scratch = preflight ?
		&h264->pre_field_refs : &h264->wrl_field_refs;
	struct v4l2_h264_reflist_builder *builder = &scratch->builder;
	struct v4l2_h264_reference (*ordered)[V4L2_H264_REF_LIST_LEN] = scratch->ordered;
	struct v4l2_ctrl_h264_decode_params *params = &scratch->params;
	u32 type = sr->slice_type % 5;
	u32 i, active, n, max_pic_num;
	int ret;

	if (lx > 1 || type > 1 || (lx && type != 1))
		return -EOPNOTSUPP;
	active = sr->num_ref_idx_active[lx];
	if (!active || active > V4L2_H264_REF_LIST_LEN ||
	    sr->n_mod[lx] > H264_MAX_RPLM)
		return -EOPNOTSUPP;
	memset(params, 0, sizeof(*params));
	params->frame_num = sr->frame_num;
	params->top_field_order_cnt = h264->multi_pic_top_poc;
	params->bottom_field_order_cnt = h264->multi_pic_bottom_poc;
	params->flags = V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC;
	if (sr->bottom_field)
		params->flags |= V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD;
	v4l2_h264_init_reflist_builder(builder, params, &h264->multi_sps,
				       h264->multi_dpb);
	if (!builder->num_valid ||
	    (!sr->n_mod[lx] && builder->num_valid < active))
		return -EINVAL;
	if (type == 1)
		v4l2_h264_build_b_ref_lists(builder, ordered[0], ordered[1]);
	else
		v4l2_h264_build_p_ref_list(builder, ordered[0]);
	/* Modification can select beyond the initial active prefix. */
	n = sr->n_mod[lx] ? builder->num_valid : active;
	for (i = 0; i < n; i++) {
		const struct v4l2_h264_reference *r = &ordered[lx][i];
		const struct v4l2_h264_dpb_entry *e;

		if (r->index >= ARRAY_SIZE(h264->multi_dpb))
			return -EINVAL;
		e = &h264->multi_dpb[r->index];
		list[i] = (struct h264_ref) {
			.dpb_idx = r->index, .view = r->fields,
			.field = !!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD),
			.structure = r->fields == V4L2_H264_BOTTOM_FIELD_REF ? 1 : 0,
			.top = e->top_field_order_cnt, .bottom = e->bottom_field_order_cnt,
			.poc = r->fields == V4L2_H264_TOP_FIELD_REF ?
				e->top_field_order_cnt : e->bottom_field_order_cnt,
			.lt = !!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM),
			/*
			 * frame_num is FrameNumWrap for short-term entries and
			 * LongTermFrameIdx for long-term entries. The field formula
			 * therefore yields PicNum or LongTermPicNum respectively.
			 */
			.pic_num = 2 * builder->refs[r->index].frame_num +
				(r->fields == builder->cur_pic_fields),
		};
	}
	max_pic_num = 1U << (h264->multi_sps.log2_max_frame_num_minus4 + 5);
	if (sr->n_mod[lx]) {
		/* The shared helper snapshots its work before committing. */
		ret = h264_refs_modify(list, &n, list, n, sr, lx, max_pic_num);
		if (ret)
			return ret;
	}
	if (n < active)
		return -EINVAL;
	/* Validate only the selected views, as on the unmodified-list path. */
	for (i = 0; i < active; i++) {
		const struct v4l2_h264_dpb_entry *e = &h264->multi_dpb[list[i].dpb_idx];
		const struct h264_field_surface *surface;
		int fw = codec_h264_multi_ref_fw_idx(sess, e);

		if (fw < 0 || fw >= H264_MULTI_MAX_FW_BUFS)
			return -EINVAL;
		surface = &h264->multi_field_slots[fw];
		if ((surface->valid && surface->vb2_idx != sess->fw_idx_to_vb2_idx[fw]) ||
		    !h264_surface_view_valid(surface, e, list[i].view, &h264->multi_sps) ||
		    (fw == h264->multi_pic_fw_idx && !h264->multi_field_mate))
			return -EINVAL;
		list[i].fw = fw;
		if (!list[i].field)
			list[i].structure = surface->structure;
		if (!preflight)
			dev_dbg(sess->core->dev_dec,
				"h264 field ref: order=%u current=%u fw=%u view=%u ts=%llu poc=%d pic_num=%d list=%u\n",
			 i, h264->multi_pic_fw_idx, fw, list[i].view, e->reference_ts,
			 list[i].poc, list[i].pic_num, lx);
	}
	*count = active;
	return 0;
}

static int codec_h264_multi_write_ref_lists(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	struct h264_ref *refs = h264->wrl_refs;
	struct h264_ref *l0 = h264->wrl_l0;
	struct h264_ref *l1 = h264->wrl_l1;
	s32 cur_poc = min(h264->multi_pic_top_poc, h264->multi_pic_bottom_poc);
	u32 slice_type = h264->multi_slice_refs.slice_type % 5;
	bool is_b = slice_type == 1;
	u32 n = 0, n0 = 0, n1 = 0, i, j, l0_view = 3;

	if (slice_type == 2)
		goto write_lists;
	if (h264->multi_field_picture) {
		int ret = codec_h264_multi_field_refs(sess, l0, &n0, 0, false);

		if (!ret && is_b)
			ret = codec_h264_multi_field_refs(sess, l1, &n1, 1, false);
		if (ret)
			return ret;
		goto write_lists;
	}

	for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
		const struct v4l2_h264_dpb_entry *e = &h264->multi_dpb[i];
		int fw;

		if (!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;
		fw = codec_h264_multi_ref_fw_idx(sess, e);
		if (fw < 0) {
			dev_warn_ratelimited(core->dev_dec,
					     "h264 multi: pic vb2=%u poc=%d: ref ts=%llu frame_num=%u has no CAPTURE buffer\n",
				 h264->multi_pic_vb2_idx, h264->multi_pic_top_poc,
				 e->reference_ts, e->frame_num);
			continue;
		}
		if (fw >= H264_MULTI_MAX_FW_BUFS)
			return -EINVAL;
		if (!h264_surface_view_valid(&h264->multi_field_slots[fw], e,
					     V4L2_H264_FRAME_REF, &h264->multi_sps)) {
			dev_err_ratelimited(core->dev_dec,
					    "h264 frame ref: invalid view fw=%d fields=%u flags=%#x ts=%llu completed=%u surface_valid=%u surface_ts=%llu poc=%d/%d stored=%d/%d\n",
				fw, e->fields, e->flags, e->reference_ts,
				h264->multi_field_slots[fw].decoded_fields,
				h264->multi_field_slots[fw].valid,
				h264->multi_field_slots[fw].timestamp,
				e->top_field_order_cnt, e->bottom_field_order_cnt,
				h264->multi_field_slots[fw].top_poc,
				h264->multi_field_slots[fw].bottom_poc);
			return -EINVAL;
		}
		refs[n].fw = fw;
		refs[n].view = 3;
		refs[n].field = !!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD);
		refs[n].structure = h264->multi_field_slots[fw].structure;
		refs[n].lt = !!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM);
		/*
		 * Derive signed FrameNumWrap from frame_num and MaxFrameNum for short-term
		 * references, as specified by H.264 (8-27) and (8-28).
		 */
		if (refs[n].lt || !h264->multi_slice_refs.valid) {
			refs[n].pic_num = (s32)e->pic_num;
		} else {
			s32 max_fn = 1 << (h264->multi_sps.log2_max_frame_num_minus4 + 4);
			s32 curr = (s32)h264->multi_slice_refs.frame_num;
			s32 fn = (s32)e->frame_num;

			refs[n].pic_num = fn > curr ? fn - max_fn : fn;
		}
		refs[n].top = e->top_field_order_cnt;
		refs[n].bottom = e->bottom_field_order_cnt;
		refs[n].poc = min(e->top_field_order_cnt,
				  e->bottom_field_order_cnt);
		n++;
	}

	if (!is_b) {
		/* P: descending PicNum, long-term last (ascending). */
		for (i = 0; i < n; i++) {
			if (refs[i].lt)
				continue;
			l0[n0++] = refs[i];
		}
		for (i = 1; i < n0; i++)
			for (j = i; j > 0 && l0[j - 1].pic_num < l0[j].pic_num; j--)
				swap(l0[j - 1], l0[j]);
		for (i = 0; i < n; i++)
			if (refs[i].lt)
				l0[n0++] = refs[i];
	} else {
		/* B list0: POC below descending, then above ascending. */
		for (i = 0; i < n; i++)
			if (!refs[i].lt && refs[i].poc < cur_poc)
				l0[n0++] = refs[i];
		for (i = 1; i < n0; i++)
			for (j = i; j > 0 && l0[j - 1].poc < l0[j].poc; j--)
				swap(l0[j - 1], l0[j]);
		j = n0;
		for (i = 0; i < n; i++)
			if (!refs[i].lt && refs[i].poc > cur_poc)
				l0[n0++] = refs[i];
		for (i = j + 1; i < n0; i++) {
			u32 k;

			for (k = i; k > j && l0[k - 1].poc > l0[k].poc; k--)
				swap(l0[k - 1], l0[k]);
		}
		/* B list1: the mirror image. */
		for (i = 0; i < n; i++)
			if (!refs[i].lt && refs[i].poc > cur_poc)
				l1[n1++] = refs[i];
		for (i = 1; i < n1; i++)
			for (j = i; j > 0 && l1[j - 1].poc > l1[j].poc; j--)
				swap(l1[j - 1], l1[j]);
		j = n1;
		for (i = 0; i < n; i++)
			if (!refs[i].lt && refs[i].poc < cur_poc)
				l1[n1++] = refs[i];
		for (i = j + 1; i < n1; i++) {
			u32 k;

			for (k = i; k > j && l1[k - 1].poc < l1[k].poc; k--)
				swap(l1[k - 1], l1[k]);
		}
		for (i = 0; i < n; i++)
			if (refs[i].lt) {
				l0[n0++] = refs[i];
				l1[n1++] = refs[i];
			}
		if (n1 > 1 && n0 == n1) {
			bool same = true;

			for (i = 0; i < n1; i++)
				if (l0[i].fw != l1[i].fw)
					same = false;
			if (same)
				swap(l1[0], l1[1]);
		}
	}

	/* Apply slice-header modifications and limit each list to its active count. */
	if (h264->multi_slice_refs.valid) {
		const struct h264_slice_refs *sr = &h264->multi_slice_refs;
		u32 max_pic_num = 1u << (h264->multi_sps.log2_max_frame_num_minus4 + 4);

		int ret = 0;

		if (sr->n_mod[0])
			ret = h264_refs_modify(l0, &n0, refs, n, sr, 0,
					       max_pic_num);
		if (!ret && sr->n_mod[1])
			ret = h264_refs_modify(l1, &n1, refs, n, sr, 1,
					       max_pic_num);
		if (ret) {
			dev_err_ratelimited(core->dev_dec,
					    "h264 multi: invalid reference-list modification frame_num=%u active=%u/%u mods=%u/%u\n",
				sr->frame_num, sr->num_ref_idx_active[0],
				sr->num_ref_idx_active[1], sr->n_mod[0],
				sr->n_mod[1]);
			return ret;
		}
		if (sr->num_ref_idx_active[0] && n0 > sr->num_ref_idx_active[0])
			n0 = sr->num_ref_idx_active[0];
		if (is_b && sr->num_ref_idx_active[1] &&
		    n1 > sr->num_ref_idx_active[1])
			n1 = sr->num_ref_idx_active[1];
	}

write_lists:
	codec_h264_multi_write_list(sess, 0, l0, n0, l0_view);
	codec_h264_multi_write_list(sess, 8, l1, n1, 3);

	h264->multi_l1_valid = n1 > 0;
	h264->multi_l1_fw_idx = n1 ? l1[0].fw : 0;
	h264->multi_l1_top = n1 ? l1[0].top : 0;
	h264->multi_l1_bottom = n1 ? l1[0].bottom : 0;
	h264->multi_l1_view = n1 ? l1[0].view : 0;
	h264->multi_l1_structure = n1 ? l1[0].structure : 0;

	dev_dbg(core->dev_dec,
		"h264 multi refs: pic_ts=%llu poc=%d %s list0=%u list1=%u l1[0]_fw=%d rplm=%u/%u\n",
		 h264->multi_pic_ts, h264->multi_pic_top_poc,
		 is_b ? "B" : "P/I", n0, n1, n1 ? l1[0].fw : -1,
		 h264->multi_slice_refs.n_mod[0], h264->multi_slice_refs.n_mod[1]);
	return 0;
}

/* Select the ready CAPTURE buffer and retain its firmware canvas slot. */
static int codec_h264_multi_pick_capture(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	struct vb2_v4l2_buffer *dst;
	u32 vb2_idx, fw_idx;

	dst = v4l2_m2m_next_dst_buf(sess->m2m_ctx);
	if (!dst) {
		dev_err_ratelimited(core->dev_dec, "h264 multi: no CAPTURE buffer ready\n");
		return -ENOBUFS;
	}

	vb2_idx = dst->vb2_buf.index;
	if (vb2_idx >= ARRAY_SIZE(sess->vb2_idx_to_fw_idx)) {
		dev_err_ratelimited(core->dev_dec, "h264 multi: vb2_idx %u out of range\n",
				    vb2_idx);
		return -EINVAL;
	}
	/*
	 * Map this prepared buffer on first use; the client may grow the pool
	 * while decoding.
	 */
	if (meson_amvdec_map_capture_buffer(sess, &dst->vb2_buf,
				      (u32[]){ ANC0_CANVAS_ADDR, 0 },
				      (u32[]){ H264_MULTI_MAX_FW_BUFS, 0 })) {
		dev_err_ratelimited(core->dev_dec, "h264 multi: canvas map failed for vb2 %u\n",
				    vb2_idx);
		return -EINVAL;
	}
	fw_idx = sess->vb2_idx_to_fw_idx[vb2_idx];
	if (fw_idx >= sess->num_fw_bufs) {
		dev_err_ratelimited(core->dev_dec,
				    "h264 multi: vb2_idx %u has no firmware canvas\n",
			vb2_idx);
		return -EINVAL;
	}

	h264->multi_pic_vb2_idx = vb2_idx;
	h264->multi_pic_fw_idx = fw_idx;
	if (h264->multi_src_vbuf)
		h264->multi_pic_ts = h264->multi_src_vbuf->vb2_buf.timestamp;
	if (h264->multi_field_picture) {
		struct h264_field_surface *first = &h264->multi_field_slots[fw_idx];

		h264->multi_field_mate = h264_field_is_mate(first, &h264->multi_fields[0],
							    h264->multi_pic_ts, vb2_idx,
							    &h264->multi_sps, &h264->multi_pps);
		/* A capture timestamp is reusable after a pair or a new picture. */
		if (first->valid && first->decoded_fields != 3 &&
		    first->timestamp == h264->multi_pic_ts &&
		    first->frame_num == h264->multi_fields[0].frame_num &&
		    !h264->multi_fields[0].idr && !h264->multi_field_mate)
			return -EINVAL;
		if (h264->multi_field_mate) {
			if (first->bottom)
				h264->multi_pic_bottom_poc = first->poc;
			else
				h264->multi_pic_top_poc = first->poc;
		}
		/* Timestamp lookup must see the current owned capture before L0. */
		v4l2_m2m_buf_copy_metadata(h264->multi_src_vbuf, dst);
		dev_dbg(core->dev_dec,
			"h264 field pick: mate=%u fw=%u vb2=%u ts=%llu bottom=%u frame_num=%u pending=%u top=%d bottom_poc=%d\n",
			 h264->multi_field_mate, fw_idx, vb2_idx, h264->multi_pic_ts,
			 h264->multi_fields[0].bottom_field, h264->multi_fields[0].frame_num,
			 first->valid, h264->multi_pic_top_poc, h264->multi_pic_bottom_poc);
	}
	/* A proven mate retains completed first-field provenance until claim. */
	if (!h264->multi_field_mate)
		h264->multi_field_slots[fw_idx].valid = false;
	return 0;
}

/*
 * Only field terminal state/provenance use this lock; never hold it over
 * MMIO, queue completion or queue abort. Completion linearizes at claim.
 */
static bool codec_h264_multi_field_advance(struct codec_h264 *h264, u32 from, u32 to)
{
	unsigned long flags;
	bool advanced;

	spin_lock_irqsave(&h264->multi_field_lock, flags);
	advanced = h264->multi_field_phase == from;
	if (advanced) {
		h264->multi_decoded_fields = BIT(h264->multi_fields[0].bottom_field);
		h264->multi_field_phase = to;
	}
	spin_unlock_irqrestore(&h264->multi_field_lock, flags);
	return advanced;
}

static void codec_h264_multi_stop_processor(struct amvdec_session *sess,
					    const char *tag)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	u32 rp0, rp1;
	int i;

	if (!h264 || !h264->multi_powered || !h264->multi_hw_started)
		return;

	dev_dbg(core->dev_dec, "h264 stop: tag=%s begin\n", tag);
	meson_amvdec_write_dos(core, MPSR, 0);
	meson_amvdec_write_dos(core, CPSR, 0);

	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	if (i == 1000)
		dev_warn(core->dev_dec, "h264 stop: tag=%s IMEM DMA did not drain ctrl=%#x\n",
			 tag, meson_amvdec_read_dos(core, IMEM_DMA_CTRL));

	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, LMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	if (i == 1000)
		dev_warn(core->dev_dec, "h264 stop: tag=%s LMEM DMA did not drain ctrl=%#x\n",
			 tag, meson_amvdec_read_dos(core, LMEM_DMA_CTRL));

	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, WRRSP_LMEM) & 0xfff))
			break;
		udelay(10);
	}
	if (i == 1000)
		dev_warn(core->dev_dec, "h264 stop: tag=%s WRRSP_LMEM did not drain wrrsp=%#x\n",
			 tag, meson_amvdec_read_dos(core, WRRSP_LMEM));

	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);

	for (i = 0; i < 100; i++) {
		rp0 = meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_RP);
		udelay(30);
		rp1 = meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_RP);
		if (rp0 == rp1)
			break;
	}
	if (i == 100)
		dev_warn(core->dev_dec, "h264 stop: tag=%s VIFIFO_RP still moving rp0=%#x rp1=%#x\n",
			 tag, rp0, rp1);

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, BIT(15));
	h264->multi_hw_started = false;
	dev_dbg(core->dev_dec,
		"h264 stop: tag=%s done rp0=%#x rp1=%#x level=%#x bitcnt=%#x\n",
		 tag, rp0, rp1, meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_LEVEL),
		 meson_amvdec_read_dos(core, VIFF_BIT_CNT));
}

static void codec_h264_job_abort(struct amvdec_session *sess)
{
	codec_h264_multi_stop_processor(sess, "timeout");
}

static void codec_h264_pre_stop(struct amvdec_session *sess)
{
	codec_h264_multi_stop_processor(sess, "stop");
}

static void codec_h264_multi_abort(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	unsigned long flags;

	if (h264)
		codec_h264_multi_stop_processor(sess, "abort");

	if (h264->multi_field_picture) {
		spin_lock_irqsave(&h264->multi_field_lock, flags);
		/* A later abort cannot revoke an already claimed result. */
		if (h264->multi_field_phase != 4) {
			h264->multi_decoded_fields = 0;
			h264->multi_field_phase = 3;
			if (h264->multi_pic_fw_idx < ARRAY_SIZE(h264->multi_field_slots))
				h264->multi_field_slots[h264->multi_pic_fw_idx].valid = false;
		}
		spin_unlock_irqrestore(&h264->multi_field_lock, flags);
	}
	meson_amvdec_abort(sess);
}

static bool codec_h264_multi_field_claim(struct codec_h264 *h264, bool have_buffers)
{
	unsigned long flags;
	bool claimed;

	spin_lock_irqsave(&h264->multi_field_lock, flags);
	claimed = have_buffers && h264->multi_field_phase == 2 &&
		  h264->multi_decoded_fields == BIT(h264->multi_fields[0].bottom_field) &&
		  h264->multi_done_fw_idx == h264->multi_pic_fw_idx &&
		  h264->multi_done_fw_idx < ARRAY_SIZE(h264->multi_field_slots);
	if (claimed) {
		struct h264_field_surface *surface =
			&h264->multi_field_slots[h264->multi_done_fw_idx];

		h264->multi_field_phase = 4;
		memset(surface, 0, sizeof(*surface));
		surface->valid = true;
		surface->bottom = h264->multi_fields[0].bottom_field;
		surface->decoded_fields = h264->multi_field_mate ? 3 : h264->multi_decoded_fields;
		surface->structure = h264->multi_fields[0].bottom_field ? 1 : 0;
		surface->frame_num = h264->multi_fields[0].frame_num;
		surface->timestamp = h264->multi_pic_ts;
		surface->vb2_idx = h264->multi_pic_vb2_idx;
		surface->top_poc = h264->multi_pic_top_poc;
		surface->bottom_poc = h264->multi_pic_bottom_poc;
		surface->poc = h264->multi_fields[0].bottom_field ?
				h264->multi_pic_bottom_poc : h264->multi_pic_top_poc;
		surface->sps = h264->multi_sps;
		surface->reference = !!h264->multi_fields[0].nal_ref_idc;
	} else if (h264->multi_field_phase != 4) {
		h264->multi_decoded_fields = 0;
		h264->multi_field_phase = 3;
		if (h264->multi_pic_fw_idx < ARRAY_SIZE(h264->multi_field_slots))
			h264->multi_field_slots[h264->multi_pic_fw_idx].valid = false;
	}
	spin_unlock_irqrestore(&h264->multi_field_lock, flags);
	return claimed;
}

/* Match the frame HEADER inventory check before any FIFO/firmware write. */
static int codec_h264_multi_frame_preflight(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct h264_slice_refs *slice = &h264->pre_first;
	bool has_fields = false, checked = false;
	u32 cursor = 0, count = 0, i;
	int ret;

	ret = h264_frame_refs_preflight(h264->multi_slice_data,
					h264->multi_slice_size,
					&h264->multi_sps,
					&h264->multi_pps,
					&h264->multi_request_picture, &sess->feed.h264_parse);
	if (ret == -EBADMSG)
		dev_err_ratelimited(sess->core->dev_dec,
				    "H.264 initial CAVLC macroblocks overlap the next slice or exceed the picture\n");
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(h264->multi_dpb); i++) {
		const struct v4l2_h264_dpb_entry *e = &h264->multi_dpb[i];
		int fw;

		if (!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;
		fw = codec_h264_multi_ref_fw_idx(sess, e);
		if ((e->flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD) ||
		    (fw >= 0 && fw < H264_MULTI_MAX_FW_BUFS &&
		     h264->multi_field_slots[fw].valid))
			has_fields = true;
	}
	if (!has_fields)
		return 0;

	while (!(ret = h264_next_slice_refs(h264->multi_slice_data,
					    h264->multi_slice_size, &cursor, &h264->multi_sps,
		 &h264->multi_pps, slice, &sess->feed.h264_parse))) {
		if (!h264_same_frame_picture(slice, &h264->multi_request_picture))
			return -EINVAL;
		count++;
		/* Intra slices do not consume the active DPB inventory. */
		if (slice->slice_type % 5 == 2 || checked)
			continue;
		for (i = 0; i < ARRAY_SIZE(h264->multi_dpb); i++) {
			const struct v4l2_h264_dpb_entry *e = &h264->multi_dpb[i];
			int fw;

			if (!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
				continue;
			fw = codec_h264_multi_ref_fw_idx(sess, e);
			if (fw < 0 && !(e->flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD))
				continue;
			if (fw < 0 || fw >= H264_MULTI_MAX_FW_BUFS ||
			    !h264_surface_view_valid(&h264->multi_field_slots[fw], e,
					V4L2_H264_FRAME_REF, &h264->multi_sps)) {
				dev_dbg(sess->core->dev_dec,
					"h264 frame ref: prefeed refused fw=%d fields=%u flags=%#x ts=%llu poc=%d/%d\n",
					 fw, e->fields, e->flags, e->reference_ts,
					 e->top_field_order_cnt, e->bottom_field_order_cnt);
				return -EINVAL;
			}
		}
		checked = true;
	}
	return ret == -ENOENT && count ? 0 : (ret == -ENOENT ? -EINVAL : ret);
}

static int codec_h264_multi_field_preflight(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct h264_ref *refs = h264->pre_refs;
	u32 count, cursor = 0;
	int ret;

	h264->multi_field_picture = false;
	h264->multi_field_mate = false;
	h264->multi_field_phase = 0;
	h264->multi_decoded_fields = 0;
	if (!h264->multi_request_picture.field_pic)
		return 0;
	ret = h264_parse_single_field(h264->multi_slice_data, h264->multi_slice_size,
				      &h264->multi_sps, &h264->multi_pps,
				      &h264->multi_request_picture,
		&h264->multi_fields[0], &sess->feed.h264_parse);
	if (ret < 0) {
		dev_err_ratelimited(sess->core->dev_dec,
				    "h264 field preflight: rejected err=%d poc_type=%u sps_flags=%#x pps_flags=%#x weighted_bipred=%u type=%u ref_idc=%u\n",
			ret, h264->multi_sps.pic_order_cnt_type, h264->multi_sps.flags,
			h264->multi_pps.flags, h264->multi_pps.weighted_bipred_idc,
			h264->multi_request_picture.slice_type,
			h264->multi_request_picture.nal_ref_idc);
		return ret;
	}
	/* All slices belong to one field; coalescing is not supported. */
	if (ret < 1)
		return -EOPNOTSUPP;
	/* Only POC0 carries pic_order_cnt_lsb; POC1 uses the client POCs. */
	if (h264->multi_sps.pic_order_cnt_type == 0 &&
	    ((s64)(h264->multi_fields[0].bottom_field ?
	     h264->multi_pic_bottom_poc : h264->multi_pic_top_poc) -
	     h264->multi_fields[0].poc_lsb) %
	    (1U << (h264->multi_sps.log2_max_pic_order_cnt_lsb_minus4 + 4)))
		return -EINVAL;
	/* Resolve actual completed reference views before firmware owns this job.
	 * A refused mixed-picture request can leave later controls naming a
	 * missing surface; HEADER-time discovery would abort the whole session.
	 */
	h264->pre_first = h264->multi_fields[0];
	{
		struct vb2_v4l2_buffer *dst = v4l2_m2m_next_dst_buf(sess->m2m_ctx);
		u32 fw = H264_MULTI_MAX_FW_BUFS, idx;

		if (!dst || !h264->multi_src_vbuf)
			return -EINVAL;
		idx = dst->vb2_buf.index;
		/* QBUF may clear CAPTURE's timestamp; resolve a mate to its own
		 * completed first field before vb2_find_buffer() searches it.
		 * No canvas is allocated or programmed during this preflight.
		 */
		v4l2_m2m_buf_copy_metadata(h264->multi_src_vbuf, dst);
		if (idx < ARRAY_SIZE(sess->vb2_idx_to_fw_idx)) {
			u32 mapped = sess->vb2_idx_to_fw_idx[idx];

			if (mapped < sess->num_fw_bufs && mapped < H264_MULTI_MAX_FW_BUFS &&
			    sess->fw_idx_to_vb2_idx[mapped] == idx)
				fw = mapped;
		}
		h264->multi_pic_fw_idx = fw;
		h264->multi_field_mate = fw < H264_MULTI_MAX_FW_BUFS &&
			h264_field_is_mate(&h264->multi_field_slots[fw], &h264->multi_fields[0],
					   h264->multi_src_vbuf->vb2_buf.timestamp, idx,
				&h264->multi_sps, &h264->multi_pps);
		/* Every slice can carry independent active counts and B lists. */
		while (!(ret = h264_next_slice_refs_mode(h264->multi_slice_data,
							 h264->multi_slice_size, &cursor,
							 &h264->multi_sps,
				&h264->multi_pps, true, &h264->multi_fields[0],
				&sess->feed.h264_parse))) {
			u32 type = h264->multi_fields[0].slice_type % 5;

			if (type == 2)
				continue;
			ret = codec_h264_multi_field_refs(sess, refs, &count, 0, true);
			if (!ret && type == 1)
				ret = codec_h264_multi_field_refs(sess, refs, &count, 1, true);
			if (ret)
				break;
		}
		h264->multi_fields[0] = h264->pre_first;
		if (ret != -ENOENT)
			return ret;
	}
	h264->multi_field_picture = true;
	return 0;
}

static int codec_h264_multi_next_slice(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct h264_slice_refs sr;
	u32 bytes_per_unit = H264_COLOCATE_BYTES_PER_MB;
	u32 first_mb, type, type_bit;
	u64 units;
	int ret;

	if (!h264->multi_src_vbuf || !h264->multi_slice_data || !h264->lmem_vaddr)
		return -EINVAL;
	ret = h264_next_slice_refs_mode(h264->multi_slice_data, h264->multi_slice_size,
					&h264->multi_slice_cursor, &h264->multi_sps,
				   &h264->multi_pps, h264->multi_field_picture, &sr,
				   &h264->irq_parse);
	if (ret)
		return ret;
	first_mb = codec_h264_multi_lmem_word(h264, 0xf0);
	type = codec_h264_multi_lmem_word(h264, 0x82) % 5;
	if (sr.first_mb != first_mb || sr.slice_type % 5 != type ||
	    !h264_same_frame_picture(&sr, &h264->multi_request_picture))
		return -EINVAL;
	units = (u64)(h264->multi_sps.pic_width_in_mbs_minus1 + 1) *
		(h264->multi_sps.pic_height_in_map_units_minus1 + 1);
	if (!(h264->multi_sps.flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) &&
	    !sr.field_pic && !sr.mbaff)
		units *= 2;
	if (sr.first_mb >= units ||
	    (!h264->multi_slice_count && sr.first_mb) ||
	    (h264->multi_slice_count && sr.first_mb <= h264->multi_slice_refs.first_mb))
		return -EINVAL;
	if (sr.field_pic &&
	    (!h264->multi_field_picture || READ_ONCE(h264->multi_field_phase) ||
	     codec_h264_multi_lmem_word(h264, 0x80) != (sr.idr ? 5 : 1) ||
	     codec_h264_multi_lmem_word(h264, 0x81) != sr.nal_ref_idc ||
	     codec_h264_multi_lmem_word(h264, 0x7c) != (sr.bottom_field ? 2 : 1)))
		return -EINVAL;
	type_bit = BIT(type);
	if ((h264->multi_slice_uniform || sr.slice_type >= 5) &&
	    (h264->multi_slice_type_mask & ~type_bit))
		return -EINVAL;
	h264->multi_slice_uniform |= sr.slice_type >= 5;
	h264->multi_slice_type_mask |= type_bit;
	/* first_mb is not doubled; field/MBAFF MV units are 192 bytes. */
	if (sr.field_pic || sr.mbaff)
		bytes_per_unit *= 2;
	if ((codec_h264_multi_lmem_word(h264, 0x8c) & 6) == 6)
		bytes_per_unit >>= 2;
	if ((u64)(sr.first_mb + 1) * bytes_per_unit > h264->colocate_buf_size)
		return -EINVAL;
	h264->multi_slice_mv_offset = sr.first_mb * bytes_per_unit;
	h264->multi_slice_refs = sr;
	h264->multi_slice_count++;
	dev_dbg(sess->core->dev_dec, "h264 multi slice: n=%u first_mb=%u type=%u mv_offset=%u\n",
		h264->multi_slice_count, sr.first_mb, type, h264->multi_slice_mv_offset);
	return 0;
}

static int codec_h264_multi_config_decode_buf(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	u32 fw_idx = h264->multi_pic_fw_idx;
	u32 canvas, info0;
	s32 top_poc = h264->multi_pic_top_poc;
	s32 bottom_poc = h264->multi_pic_bottom_poc;
	u32 colocate_wr;
	u32 i;

	if (fw_idx >= sess->num_fw_bufs) {
		dev_err_ratelimited(core->dev_dec,
				    "h264 multi decbuf: invalid fw_idx=%u fw_bufs=%u\n",
			fw_idx, sess->num_fw_bufs);
		return -EINVAL;
	}

	/* Before the mate is decoded, only the first field's POC is valid. */
	if (h264->multi_field_picture &&
	    !READ_ONCE(h264->multi_field_phase) && !h264->multi_field_mate) {
		s32 first_poc = h264->multi_fields[0].bottom_field ? bottom_poc : top_poc;

		top_poc = first_poc;
		bottom_poc = first_poc;
	}
	/* Write frame POC as min(top, bottom), followed by top and bottom POC. */
	meson_amvdec_write_dos(core, H264_CURRENT_POC_IDX_RESET, 0);
	meson_amvdec_write_dos(core, H264_CURRENT_POC,
			 min(top_poc, bottom_poc));
	meson_amvdec_write_dos(core, H264_CURRENT_POC, top_poc);
	meson_amvdec_write_dos(core, H264_CURRENT_POC, bottom_poc);

	/* Reconstruction and deblock canvases for the chosen CAPTURE buffer. */
	meson_amvdec_write_dos(core, CURR_CANVAS_CTRL, fw_idx << 24);
	canvas = meson_amvdec_read_dos(core, CURR_CANVAS_CTRL) & 0xffffff;
	meson_amvdec_write_dos(core, REC_CANVAS_ADDR, canvas);
	meson_amvdec_write_dos(core, DBKR_CANVAS_ADDR, canvas);
	meson_amvdec_write_dos(core, DBKW_CANVAS_ADDR, canvas);

	/*
	 * Buffer-info indices 16 onward hold info0/top_poc/bottom_poc per canvas
	 * slot. Bit 8 marks bottom-first pairs; the low nibble marks the decode
	 * slot. Indices 0 and 8 hold the packed reference lists, including padding.
	 * Picture-structure tags: 0xf4c0 MBAFF, 0xf400 top field, 0xf440 bottom
	 * field and 0xf480 progressive frame.
	 */
	switch (codec_h264_multi_current_structure(h264)) {
	case 0:
		info0 = 0xf400;
		break;
	case 1:
		info0 = 0xf440;
		break;
	case 3:
		info0 = 0xf4c0;
		break;
	case 2:
	default:
		info0 = 0xf480;
		break;
	}
	if (bottom_poc < top_poc)
		info0 |= 0x100;

	/*
	 * Index by firmware canvas position, covering all 24 capture slots.
	 * Zero unused slots, including those without a mapping.
	 */
	meson_amvdec_write_dos(core, H264_BUFFER_INFO_INDEX, 16);
	for (i = 0; i < H264_MULTI_MAX_FW_BUFS; i++) {
		const struct v4l2_h264_dpb_entry *ref;
		u32 r_info0 = 0;
		s32 r_top = 0, r_bot = 0;

		ref = i == fw_idx ? NULL : codec_h264_multi_ref_in_slot(sess, i);
		if (i == fw_idx) {
			r_info0 = info0 | 0xf;
			r_top = top_poc;
			r_bot = bottom_poc;
		} else if (ref) {
			/*
			 * Retain each reference picture structure. MbaffFrameFlag is
			 * constant
			 * across frame pictures in a sequence.
			 */
			const struct h264_field_surface *surface = &h264->multi_field_slots[i];

			r_info0 = h264->multi_sps_mbaff ? 0xf4c0 : 0xf480;
			r_top = ref->top_field_order_cnt;
			r_bot = ref->bottom_field_order_cnt;
			if (surface->valid && surface->timestamp == ref->reference_ts &&
			    surface->vb2_idx == sess->fw_idx_to_vb2_idx[i]) {
				r_info0 = h264_surface_info0(surface);
				r_top = surface->top_poc;
				r_bot = surface->bottom_poc;
				if (surface->decoded_fields != V4L2_H264_FRAME_REF) {
					r_bot = surface->poc;
					r_top = r_bot;
				}
			} else if (r_bot < r_top) {
				r_info0 |= 0x100;
			}
			if (ref->flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM)
				r_info0 |= (ref->fields & V4L2_H264_TOP_FIELD_REF ? 0x10 : 0) |
					   (ref->fields & V4L2_H264_BOTTOM_FIELD_REF ? 0x20 : 0);
		}
		if (h264->multi_field_picture && r_info0)
			dev_dbg(core->dev_dec, "h264 field slot: fw=%u current=%u info0=%#x top=%d bottom=%d\n",
				i, i == fw_idx, r_info0, r_top, r_bot);
		meson_amvdec_write_dos(core, H264_BUFFER_INFO_DATA, r_info0);
		meson_amvdec_write_dos(core, H264_BUFFER_INFO_DATA, r_top);
		meson_amvdec_write_dos(core, H264_BUFFER_INFO_DATA, r_bot);
	}

	if (codec_h264_multi_write_ref_lists(sess))
		return -EINVAL;

	/*
	 * Use one colocated-MV slot per capture canvas. Each slice addresses its
	 * macroblock or MBAFF-pair offset within that slot.
	 */
	if (!h264->colocate_vaddr || !h264->colocate_buf_size) {
		dev_err_ratelimited(core->dev_dec, "h264 multi decbuf: no colocated MV store\n");
		return -EINVAL;
	}
	if (h264->multi_field_picture) {
		for (i = 0; i < 1000; i++) {
			if (!(meson_amvdec_read_dos(core, H264_CO_MB_RW_CTL) & BIT(11)))
				break;
			udelay(1);
		}
		if (i == 1000) {
			dev_err_ratelimited(core->dev_dec, "h264 field: CO_MB_RW_CTL busy phase=%u\n",
					    READ_ONCE(h264->multi_field_phase));
			return -ETIMEDOUT; /* HEADER caller aborts both queues. */
		}
	} else {
		while (meson_amvdec_read_dos(core, H264_CO_MB_RW_CTL) & BIT(11))
			cpu_relax();
	}
	colocate_wr = h264->colocate_paddr + h264->colocate_buf_size * fw_idx +
		      h264->multi_slice_mv_offset;
	meson_amvdec_write_dos(core, H264_CO_MB_WR_ADDR, colocate_wr);
	if (h264->multi_l1_valid) {
		/*
		 * Direct mode reads the colocated MVs of L1[0]. Bits 31:30
		 * carry that picture's coding structure (2 = frame) and bit
		 * 29 selects which field of it to use; for a frame picture
		 * select the field by POC distance.
		 */
		u32 rd = h264->colocate_paddr +
			 h264->colocate_buf_size * h264->multi_l1_fw_idx;
		s32 cur = min(h264->multi_pic_top_poc,
			      h264->multi_pic_bottom_poc);
		u32 ref_type = abs(cur - h264->multi_l1_top) <
			       abs(cur - h264->multi_l1_bottom) ? 0 : 1;

		u8 cur_structure = codec_h264_multi_current_structure(h264);
		bool direct_8x8 = (codec_h264_multi_lmem_word(h264, 0x8c) & 6) == 6;
		u32 offset, value;

		/* Field bit29 selects L1[0]'s parity, not the current field's. */
		if (h264->multi_field_picture)
			ref_type = h264->multi_l1_view == V4L2_H264_BOTTOM_FIELD_REF;
		offset = h264_colocate_read_offset(h264->multi_slice_refs.first_mb,
						   h264->multi_sps.pic_width_in_mbs_minus1 + 1,
						   cur_structure,
			h264->multi_l1_structure, direct_8x8);
		if (offset >= h264->colocate_buf_size)
			return -EINVAL;
		rd += offset;
		value = (rd >> 3) | ((u32)h264->multi_l1_structure << 30) |
			(ref_type << 29);
		meson_amvdec_write_dos(core, H264_CO_MB_RD_ADDR, value);
		if (h264->multi_field_picture || h264->multi_l1_structure < 2)
			dev_dbg(core->dev_dec,
				"h264 field mv: cur=%u l1_fw=%u structure=%u view=%u bit29=%u direct8=%u offset=%#x addr=%#x value=%#x\n",
				 cur_structure, h264->multi_l1_fw_idx, h264->multi_l1_structure,
				 h264->multi_l1_view, ref_type, direct_8x8, offset, rd, value);
	}

	dev_dbg(core->dev_dec,
		"h264 multi decbuf: fw_idx=%u vb2_idx=%u canvas=%#x poc=(%d,%d) info0=%#x colo_wr=%#x\n",
		 fw_idx, h264->multi_pic_vb2_idx, canvas,
		 top_poc, bottom_poc, info0, colocate_wr);

	return 0;
}

/* Inverse of codec_h264_multi_lmem_word()'s 4-word swizzle. */
static void codec_h264_multi_lmem_set(struct codec_h264 *h264, u32 i, u16 v)
{
	u16 *lmem = h264->lmem_vaddr;

	if (!lmem)
		return;
	lmem[(i & ~3) + 3 - (i & 3)] = v;
}

/*
 * Supply SPS-derived geometry in RPM after SLICE_HEAD_DONE and before
 * starting reconstruction; the firmware leaves these fields unset.
 */
static void codec_h264_multi_inject_sps_rpm(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	const struct v4l2_ctrl_h264_sps *sps = &h264->multi_sps;
	u32 mb_w = h264->mb_width, mb_h = h264->mb_height;

	if (!h264->lmem_vaddr || !h264->multi_pic_valid)
		return;

	codec_h264_multi_lmem_set(h264, H264_RPM_LOG2_MAX_FRAME_NUM,
				  sps->log2_max_frame_num_minus4 + 4);
	codec_h264_multi_lmem_set(h264, H264_RPM_FRAME_MBS_ONLY_FLAG,
				  !!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY));
	codec_h264_multi_lmem_set(h264, H264_RPM_PIC_ORDER_CNT_TYPE,
				  sps->pic_order_cnt_type);
	codec_h264_multi_lmem_set(h264, H264_RPM_LOG2_MAX_POC_LSB,
				  sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
	codec_h264_multi_lmem_set(h264, H264_RPM_TOTAL_MB_HEIGHT, mb_h);
	codec_h264_multi_lmem_set(h264, H264_RPM_MB_X_NUM, mb_w);
	codec_h264_multi_lmem_set(h264, H264_RPM_MB_HEIGHT, mb_h);
	codec_h264_multi_lmem_set(h264, H264_RPM_PREV_MB_WIDTH, mb_w);
	codec_h264_multi_lmem_set(h264, H264_RPM_PREV_FRAME_SIZE_IN_MB,
				  mb_w * mb_h);

	dev_dbg(sess->core->dev_dec,
		"h264 rpm inject: mb=%ux%u log2_frame_num=%u mbs_only=%u poc_type=%u\n",
		 mb_w, mb_h, sps->log2_max_frame_num_minus4 + 4,
		 !!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY),
		 sps->pic_order_cnt_type);
}

static u16 h264_flag(u32 flags, u32 bit)
{
	return !!(flags & bit);
}

static int codec_h264_multi_host_config(struct amvdec_session *sess);

/* Cache request controls for per-picture hardware programming. */
void meson_amvdec_codec_h264_multi_set_params(struct amvdec_session *sess,
				 const struct v4l2_ctrl_h264_sps *sps,
				 const struct v4l2_ctrl_h264_pps *pps,
				 const struct v4l2_ctrl_h264_decode_params *dec)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	u32 cfg, frame_size, max_refs, max_list, pic_h_mbs;
	u32 req_width_mbs, req_height_mbs, fmt_width_mbs, fmt_height_mbs;

	if (!h264 || !h264->workspace_vaddr)
		return;

	h264->multi_pic_top_poc = dec->top_field_order_cnt;
	h264->multi_pic_bottom_poc = dec->bottom_field_order_cnt;
	h264->multi_sps_max_refs = sps->max_num_ref_frames;
	h264->multi_sps = *sps;
	h264->multi_pps = *pps;
	memset(&h264->multi_request_picture, 0, sizeof(h264->multi_request_picture));
	h264->multi_request_picture.frame_num = dec->frame_num;
	h264->multi_request_picture.pps_id = pps->pic_parameter_set_id;
	h264->multi_request_picture.idr = !!(dec->flags & V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC);
	h264->multi_request_picture.idr_pic_id = dec->idr_pic_id;
	h264->multi_request_picture.nal_ref_idc = dec->nal_ref_idc;
	h264->multi_request_picture.field_pic =
		!!(dec->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC);
	h264->multi_request_picture.bottom_field =
		!!(dec->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD);
	if (sps->pic_order_cnt_type == 0) {
		h264->multi_request_picture.poc_lsb = dec->pic_order_cnt_lsb;
		if (!h264->multi_request_picture.field_pic &&
		    (pps->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT))
			h264->multi_request_picture.delta_poc_bottom =
				dec->delta_pic_order_cnt_bottom;
	} else if (sps->pic_order_cnt_type == 1 &&
		   !(sps->flags & V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO)) {
		h264->multi_request_picture.delta_poc[0] = dec->delta_pic_order_cnt0;
		if (!h264->multi_request_picture.field_pic &&
		    (pps->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT))
			h264->multi_request_picture.delta_poc[1] = dec->delta_pic_order_cnt1;
	}
	memcpy(h264->multi_dpb, dec->dpb, sizeof(h264->multi_dpb));
	h264->multi_nal_ref_idc = dec->nal_ref_idc;
	h264->multi_pic_valid = true;
	h264->multi_params_error = 0;

	/*
	 * With frame_mbs_only_flag = 0 the SPS counts map units, which are
	 * field height: FrameHeightInMbs is twice PicHeightInMapUnits (7-18).
	 * Using it directly would size an MBAFF frame at half its height.
	 */
	pic_h_mbs = sps->pic_height_in_map_units_minus1 + 1;
	if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY))
		pic_h_mbs *= 2;
	req_width_mbs = sps->pic_width_in_mbs_minus1 + 1;
	req_height_mbs = pic_h_mbs;
	fmt_width_mbs = DIV_ROUND_UP(sess->width, 16);
	fmt_height_mbs = DIV_ROUND_UP(sess->height, 16);
	if (!fmt_width_mbs || !fmt_height_mbs ||
	    req_width_mbs > fmt_width_mbs || req_height_mbs > fmt_height_mbs ||
	    (h264->multi_geometry_valid &&
	     (req_width_mbs != h264->multi_geometry_width_mbs ||
	      req_height_mbs != h264->multi_geometry_height_mbs))) {
		dev_warn_ratelimited(core->dev_dec,
				     "h264 multi: rejected SPS geometry %ux%u MBs for negotiated %ux%u MBs (active %ux%u MBs)\n",
			req_width_mbs, req_height_mbs,
			fmt_width_mbs, fmt_height_mbs,
			h264->multi_geometry_valid ? h264->multi_geometry_width_mbs : 0,
			h264->multi_geometry_valid ? h264->multi_geometry_height_mbs : 0);
		h264->multi_pic_valid = false;
		h264->multi_params_error = -EINVAL;
		return;
	}
	h264->multi_geometry_valid = true;
	h264->multi_geometry_width_mbs = req_width_mbs;
	h264->multi_geometry_height_mbs = req_height_mbs;
	frame_size = (sps->pic_width_in_mbs_minus1 + 1) * pic_h_mbs;
	cfg = (h264_flag(sps->flags, V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) << 31) |
	      (sps->max_num_ref_frames << 24) | (frame_size << 8) |
	      (sps->pic_width_in_mbs_minus1 + 1);
	/*
	 * Keep these: the per-picture VLD reset in the feed path runs after
	 * this point, so they are re-applied there rather than relied on here.
	 */
	h264->multi_seq_cfg = cfg;
	h264->multi_seq_cfg2 = (h264_flag(sps->flags,
				V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE) << 15) |
			       sps->chroma_format_idc;
	h264->multi_seq_cfgb = (sps->max_num_ref_frames << 8) | sps->level_idc;
	h264->multi_nal_search_or = ((sps->level_idc & 0xff) << 7) | BIT(2);

	h264->multi_sps_mbaff = !(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) &&
			    !!(sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD);
	/*
	 * The decoder applies frame implicit weights to MBAFF field pairs,
	 * without the per-field weights required by H.264 8.4.2.3.1.
	 */
	if (h264->multi_sps_mbaff && pps->weighted_bipred_idc == 2)
		dev_warn_once(core->dev_dec,
			      "H.264 MBAFF with implicit weighted bi-prediction is not bit-exact on this hardware\n");
	h264->mb_width = (sps->pic_width_in_mbs_minus1 + 4) & ~3u;
	h264->mb_height = (pic_h_mbs + 3) & ~3u;
	max_refs = clamp_t(u32, sps->max_num_ref_frames, 0,
			   V4L2_H264_NUM_DPB_ENTRIES);

	max_list = (pps->num_ref_idx_l1_default_active_minus1 + 1) +
		   (pps->num_ref_idx_l0_default_active_minus1 + 1);
	if (max_list > max_refs)
		max_refs = max_list;
	h264->multi_sps_max_refs = max_refs;

	h264->multi_seq_scratch0 = ((max_refs + 1) << 24) |
				   (V4L2_H264_NUM_DPB_ENTRIES << 16) |
				   (V4L2_H264_NUM_DPB_ENTRIES << 8);

	dev_dbg(core->dev_dec,
		"h264 multi params: poc=%d nrefs=%u | sps_id=%u pps_id=%u mb=%ux%u seq_cfg=%#x max_refs=%u\n",
		 dec->top_field_order_cnt,
		 ({
			unsigned int k, c = 0;

			for (k = 0; k < V4L2_H264_NUM_DPB_ENTRIES; k++)
				if (dec->dpb[k].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)
					c++;
			c;
		 }),
		 sps->seq_parameter_set_id, pps->pic_parameter_set_id,
		 h264->mb_width, h264->mb_height, cfg, max_refs);
}

static int codec_h264_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_h264 *h264 = sess->priv;
	int ret;

	BUILD_BUG_ON(DCAC_READ_MARGIN + 0x218000 + 0x10 > SIZE_WORKSPACE_MULTI);
	BUILD_BUG_ON(SIZE_WORKSPACE_MULTI > SIZE_WORKSPACE_MULTI_RP);

	h264->workspace_size = SIZE_WORKSPACE_MULTI_RP;

	/* Allocate some memory for the H.264 decoder's state */
	h264->workspace_vaddr =
		dma_alloc_coherent(core->dev, h264->workspace_size,
				   &h264->workspace_paddr, GFP_KERNEL);
	if (!h264->workspace_vaddr)
		return -ENOMEM;

	/* Allocate some memory for the H.264 SEI dump */
	h264->sei_vaddr = dma_alloc_coherent(core->dev, SIZE_SEI,
					     &h264->sei_paddr, GFP_KERNEL);
	if (!h264->sei_vaddr)
		return -ENOMEM;

	h264->aux_vaddr = dma_alloc_coherent(core->dev, SIZE_MULTI_AUX,
					     &h264->aux_paddr, GFP_KERNEL);
	if (!h264->aux_vaddr)
		return -ENOMEM;

	h264->lmem_vaddr = dma_alloc_coherent(core->dev, SIZE_LMEM,
					      &h264->lmem_paddr,
					      GFP_KERNEL);
	if (!h264->lmem_vaddr)
		return -ENOMEM;

	memset(h264->workspace_vaddr, 0, h264->workspace_size);

	/*
	 * Reset VLD_PART, IQIDCT, MC, DBLK and PIC_DC after clocks and memories
	 * are enabled, following the vendor driver initialization sequence.
	 */
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6) | BIT(4));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6) | BIT(4));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(9) | BIT(8));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);

	meson_amvdec_write_dos_bits(core, POWER_CTL_VLD, BIT(9) | BIT(6));

	h264->multi_powered = true;
	h264->multi_hw_started = false;
	meson_amvdec_write_dos(core, AV_SCRATCH_8,
			       codec_h264_multi_workspace_base(h264));
	meson_amvdec_write_dos(core, H264_AUX_ADR, h264->aux_paddr);
	meson_amvdec_write_dos(core, H264_AUX_DATA_SIZE,
			       ((SIZE_MULTI_AUX >> 4) << 16));
	meson_amvdec_write_dos(core, H264_DECODE_MODE,
			       DECODE_MODE_MULTI_FRAMEBASE);
	meson_amvdec_write_dos(core, H264_DECODE_SEQINFO, 0);
	meson_amvdec_write_dos(core, HEAD_PADING_REG, 0);
	meson_amvdec_write_dos(core, INIT_FLAG_REG, 0);
	meson_amvdec_write_dos(core, DPB_STATUS_REG, 0);
	dev_dbg(core->dev_dec,
		"h264 multi init: mode=framebase workspace=%pad aux=%pad ext=%pad\n",
		 &h264->workspace_paddr, &h264->aux_paddr,
		 &h264->ext_fw_paddr);

	ret = codec_h264_multi_host_config(sess);
	if (ret)
		return ret;
	meson_amvdec_write_dos(core, AV_SCRATCH_G, h264->ext_fw_paddr);
	if (h264->lmem_vaddr)
		meson_amvdec_write_dos(core, LMEM_DUMP_ADR, h264->lmem_paddr);

	/* Enable "error correction" */
	meson_amvdec_write_dos(core, AV_SCRATCH_F,
			 (meson_amvdec_read_dos(core, AV_SCRATCH_F) & 0xffffffc3) |
			 BIT(4) | BIT(7));

	meson_amvdec_write_dos(core, MDEC_PIC_DC_THRESH, 0x404038aa);

	/* Set NAL_SEARCH_CTL before starting the processor. */
	if (h264->lmem_vaddr)
		meson_amvdec_write_dos(core, NAL_SEARCH_CTL, 0);

	return 0;
}

static int codec_h264_stop(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;

	/* Return the active OUTPUT buffer owned by the codec before vb2 teardown. */
	if (h264->multi_src_vbuf) {
		h264->multi_slice_data = NULL;
		meson_amvdec_src_buf_done(sess, h264->multi_src_vbuf, VB2_BUF_STATE_ERROR);
		h264->multi_src_vbuf = NULL;
	}
	h264->multi_done_pending = false;
	h264->multi_pic_started = false;
	h264->multi_hw_started = false;
	h264->multi_powered = false;
	h264->multi_headers_valid = false;

	if (h264->ext_fw_vaddr)
		dma_free_coherent(core->dev, h264->ext_fw_size,
				  h264->ext_fw_vaddr, h264->ext_fw_paddr);

	if (h264->workspace_vaddr)
		dma_free_coherent(core->dev, h264->workspace_size,
				  h264->workspace_vaddr, h264->workspace_paddr);

	if (h264->sei_vaddr)
		dma_free_coherent(core->dev, SIZE_SEI,
				  h264->sei_vaddr, h264->sei_paddr);

	if (h264->aux_vaddr)
		dma_free_coherent(core->dev, SIZE_MULTI_AUX,
				  h264->aux_vaddr, h264->aux_paddr);

	if (h264->lmem_vaddr)
		dma_free_coherent(core->dev, SIZE_LMEM,
				  h264->lmem_vaddr, h264->lmem_paddr);

	if (h264->colocate_vaddr)
		dma_free_coherent(core->dev, h264->colocate_size,
				  h264->colocate_vaddr, h264->colocate_paddr);

	return 0;
}

static int codec_h264_load_firmware(struct amvdec_session *sess,
				    const u8 *data, u32 len)
{
	struct codec_h264 *h264;
	struct amvdec_core *core = sess->core;
	u32 ext_size = SIZE_EXT_FW_MULTI;

	if (len < SIZE_MULTI_FW_MIN)
		return -EINVAL;

	h264 = kzalloc_obj(*h264);
	if (!h264)
		return -ENOMEM;

	spin_lock_init(&h264->multi_field_lock);
	h264->ext_fw_size = ext_size;
	h264->ext_fw_vaddr = dma_alloc_coherent(core->dev, ext_size,
						&h264->ext_fw_paddr,
						GFP_KERNEL);
	if (!h264->ext_fw_vaddr) {
		kvfree(h264->scan_copy);
		kfree(h264);
		return -ENOMEM;
	}

	memcpy(h264->ext_fw_vaddr + MC_OFFSET_HEADER,
	       data + 0x4000, MC_SWAP_SIZE);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_DATA,
	       data + 0x2000, MC_SWAP_SIZE);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_MMCO,
	       data + 0x6000, MC_SWAP_SIZE);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_LIST,
	       data + 0x3000, MC_SWAP_SIZE);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_SLICE,
	       data + 0x5000, MC_SWAP_SIZE);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_MAIN,
	       data, 0x2000);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_MAIN + 0x2000,
	       data + 0x2000, 0x1000);
	memcpy(h264->ext_fw_vaddr + MC_OFFSET_MAIN + 0x3000,
	       data + 0x5000, 0x1000);
	dev_dbg(core->dev_dec,
		"h264 fw layout: multi size=%u ext_size=%u\n",
		 len, ext_size);

	sess->priv = h264;

	return 0;
}

/*
 * One colocated MV slot per CAPTURE frame, indexed by canvas position, so a B
 * picture can read the MVs of its L1[0] without the firmware owning the
 * mapping. Each slot holds 96 bytes per macroblock.
 */
static int codec_h264_multi_alloc_colocate(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	u32 mb_total;

	if (h264->colocate_vaddr)
		return 0;

	mb_total = h264->mb_width * h264->mb_height;
	if (!mb_total)
		return -EINVAL;

	/*
	 * Allocate MV storage for every firmware canvas slot so CAPTURE can grow
	 * without resizing the store during decoding.
	 * MBAFF stores motion vectors for both halves of each macroblock pair
	 * and requires twice the progressive-frame storage.
	 */
	h264->colocate_buf_size = mb_total * H264_COLOCATE_BYTES_PER_MB *
				  (h264->multi_sps_mbaff ? 2 : 1);
	h264->colocate_size = h264->colocate_buf_size * H264_MULTI_MAX_FW_BUFS;
	h264->colocate_vaddr = dma_alloc_coherent(core->dev,
						  h264->colocate_size,
						  &h264->colocate_paddr,
						  GFP_KERNEL);
	if (!h264->colocate_vaddr) {
		dev_err_ratelimited(core->dev_dec,
				    "h264 multi: colocated MV alloc failed (%u bytes)\n",
			h264->colocate_size);
		return -ENOMEM;
	}

	dev_dbg(core->dev_dec,
		"h264 multi: colocated MV store paddr=%pad per_buf=%#x total=%#x slots=%u\n",
		 &h264->colocate_paddr, h264->colocate_buf_size,
		 h264->colocate_size, (u32)H264_MULTI_MAX_FW_BUFS);
	return 0;
}

/* Configure capture canvases and colocated-MV storage before feeding pictures. */
static int codec_h264_multi_host_config(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;

	h264->mb_width = DIV_ROUND_UP(sess->width, 16);
	h264->mb_height = DIV_ROUND_UP(sess->height, 16);

	/* Apply sequence geometry from each request SPS. */
	dev_dbg(core->dev_dec,
		"h264 multi host config: %ux%u mb=%ux%u fw_bufs=%u\n",
		 sess->width, sess->height, h264->mb_width, h264->mb_height,
		 sess->num_fw_bufs);

	return codec_h264_multi_alloc_colocate(sess);
}

static irqreturn_t codec_h264_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_h264 *h264 = sess->priv;
	u32 status;

	if (h264) {
		u32 level = meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_LEVEL);
		u32 wp = meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_WP);
		u32 rp = meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_RP);
		u32 bit_count = meson_amvdec_read_dos(core, VIFF_BIT_CNT);

		status = meson_amvdec_read_dos(core, DPB_STATUS_REG);
		meson_amvdec_trace(sess, AMVDEC_TR_IRQ, status, level, bit_count);
		dev_dbg(core->dev,
			"H264 multi status: time_ns=%llu irq=%u status=%#04x level=%#x wp=%#x rp=%#x bits=%#x\n",
			 (unsigned long long)ktime_get_ns(),
			 ++h264->multi_irq_count, status, level, wp, rp,
			 bit_count);
		if (status == H264_SEI_DATA_READY) {
			meson_amvdec_write_dos_action(core, DPB_STATUS_REG,
						H264_SEI_DATA_DONE);
			dev_dbg(core->dev,
				"H264 multi reply: status=%#04x action=%#04x\n",
				 status, H264_SEI_DATA_DONE);
		} else if (status == H264_CONFIG_REQUEST) {
			u32 scratch1 = meson_amvdec_read_dos(core, AV_SCRATCH_1);
			u32 scratch2 = meson_amvdec_read_dos(core, AV_SCRATCH_2);
			u32 scratch6 = meson_amvdec_read_dos(core, AV_SCRATCH_6);
			u32 scratchb = meson_amvdec_read_dos(core, AV_SCRATCH_B);

			dev_dbg(core->dev,
				"H264 multi config: scratch1=%#x scratch2=%#x scratch6=%#x scratchb=%#x\n",
				 scratch1, scratch2, scratch6, scratchb);
			meson_amvdec_write_dos_action(core, DPB_STATUS_REG,
						H264_ACTION_CONFIG_DONE);
			dev_dbg(core->dev,
				"H264 multi reply: status=%#04x action=%#04x\n",
				 status, H264_ACTION_CONFIG_DONE);
			/*
			 * Answer the configuration request from the host SPS and capture
			 * format.
			 */
			codec_h264_multi_write_dpb_config(sess);
		} else if (status == H264_SLICE_HEAD_DONE) {
			u32 action;

			h264->multi_headers_valid = h264->multi_last_headers_len != 0;

			if (codec_h264_multi_next_slice(sess)) {
				dev_err_ratelimited(core->dev_dec, "h264 multi: slice header/picture mismatch\n");
				codec_h264_multi_abort(sess);
				return IRQ_HANDLED;
			}
			codec_h264_multi_inject_sps_rpm(sess);
			if (!h264->multi_pic_started) {
				if (codec_h264_multi_pick_capture(sess)) {
					if (h264->multi_field_picture)
						codec_h264_multi_abort(sess);
					return IRQ_HANDLED;
				}
				if (codec_h264_multi_config_decode_buf(sess)) {
					codec_h264_multi_abort(sess);
					return IRQ_HANDLED;
				}
				h264->multi_pic_started = true;
				action = H264_ACTION_DECODE_NEWPIC;
			} else {
				if (codec_h264_multi_config_decode_buf(sess)) {
					codec_h264_multi_abort(sess);
					return IRQ_HANDLED;
				}
				action = H264_ACTION_DECODE_SLICE;
			}
			meson_amvdec_trace(sess, AMVDEC_TR_KICK, action, status, 0);
			meson_amvdec_write_dos_action(core, DPB_STATUS_REG, action);
			dev_dbg(core->dev_dec,
				"H264 multi reply: status=%#04x action=%#04x\n",
				 status, action);
		} else if (status == H264_PIC_DATA_DONE) {
			u32 mby_mbx = meson_amvdec_read_dos(core, H264_MBY_MBX);

			meson_amvdec_trace(sess, AMVDEC_TR_DONE, status,
				     h264->multi_pic_vb2_idx, mby_mbx);
			u32 decoded_mbs = ((mby_mbx & 0xff) + 1) *
					  (((mby_mbx >> 8) & 0xff) + 1);
			u32 expected_mb_width = h264->multi_sps.pic_width_in_mbs_minus1 + 1;
			u32 expected_mb_height =
				(h264->multi_sps.pic_height_in_map_units_minus1 + 1) *
				((h264->multi_sps.flags &
				  V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) ? 1 : 2);
			u32 expected_mbs = expected_mb_width * expected_mb_height;

			dev_dbg(core->dev_dec,
				"h264 multi pic done: time_ns=%llu fw_idx=%u vb2_idx=%u mby_mbx=%#x decoded_mbs=%u mb_total=%u nal_search_ctl=%#x\n",
				 (unsigned long long)ktime_get_ns(),
				 h264->multi_pic_fw_idx, h264->multi_pic_vb2_idx,
				 mby_mbx, decoded_mbs,
				 h264->mb_width * h264->mb_height,
				 meson_amvdec_read_dos(core, NAL_SEARCH_CTL));
			if (!h264->multi_field_picture && h264->multi_slice_count == 1 &&
			    ((mby_mbx & 0xff) != expected_mb_height - 1 ||
			     ((mby_mbx >> 8) & 0xff) != expected_mb_width - 1)) {
				dev_warn_ratelimited(core->dev_dec,
						     "h264 multi: incomplete picture: decoded %u/%u macroblocks (mby_mbx=%#x)\n",
					decoded_mbs, expected_mbs, mby_mbx);
				h264->multi_done_error = true;
			}
			/*
			 * Record completion and wake the feed worker, which owns removal and
			 * retirement of m2m buffers.
			 */
			if (h264->multi_field_picture) {
				u32 w = h264->multi_sps.pic_width_in_mbs_minus1 + 1;
				u32 h = h264->multi_sps.pic_height_in_map_units_minus1 + 1;

				if (!h264->multi_src_vbuf || !h264->multi_pic_started ||
				    !h264->multi_slice_count ||
				    (mby_mbx & 255) != h - 1 || ((mby_mbx >> 8) & 255) != w - 1) {
					codec_h264_multi_abort(sess);
					return IRQ_HANDLED;
				}
				if (!codec_h264_multi_field_advance(h264, 0, 2)) {
					codec_h264_multi_abort(sess);
					return IRQ_HANDLED;
				}
				dev_dbg(core->dev_dec, "h264 field done: fw=%u bottom=%u count=%u ts=%llu\n",
					h264->multi_pic_fw_idx, h264->multi_fields[0].bottom_field,
					w * h, h264->multi_pic_ts);
			}
			if (h264->multi_slice_data) {
				const u8 *remaining = h264->multi_slice_data +
						      h264->multi_slice_cursor;
				u32 size = h264->multi_slice_size - h264->multi_slice_cursor;
				u32 unused_len;

				if (h264_find_first_slice(remaining, size, &unused_len)) {
					dev_err_ratelimited(core->dev_dec, "h264 multi: picture done with unconsumed slices\n");
					codec_h264_multi_abort(sess);
					return IRQ_HANDLED;
				}
			}
			if (h264->multi_pic_started) {
				h264->multi_pic_started = false;
				h264->multi_done_fw_idx = h264->multi_pic_fw_idx;
				h264->multi_done_pending = true;
				schedule_work(&sess->esparser_queue_work);
			}
		} else if (h264->multi_field_picture) {
			dev_err_ratelimited(core->dev_dec, "h264 field: unexpected status=%#x phase=%u\n",
					    status, READ_ONCE(h264->multi_field_phase));
			codec_h264_multi_abort(sess);
		}
		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

static irqreturn_t codec_h264_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	meson_amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);

	return IRQ_WAKE_THREAD;
}

static void codec_h264_post_start(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;

	if (!h264)
		return;

	/* amvdec_1_start() leaves direct-input firmware halted. The first feed
	 * publishes input and context before it releases the processors.
	 */
	meson_amvdec_write_dos(sess->core, MPSR, 0);
	meson_amvdec_write_dos(sess->core, CPSR, 0);
	dev_dbg(sess->core->dev_dec, "h264 multi: halted until first feed\n");
}

/*
 * Use OUTPUT as the VLD stream FIFO. Align the base to eight bytes and
 * provide a padding window for hardware read-ahead.
 */
#define VDEC_FIFO_ALIGN		8
#define VLD_PADDING_SIZE	1024

/* One access unit in flight: the VLD is pointed at that buffer. */
bool meson_amvdec_codec_h264_multi_busy(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;

	return h264 && h264->multi_src_vbuf;
}

/*
 * Retire OUTPUT and CAPTURE from the feed worker. Copy the OUTPUT
 * timestamp to CAPTURE so later requests can name it as a reference.
 */
void meson_amvdec_codec_h264_multi_finish_picture(struct amvdec_session *sess)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	struct vb2_v4l2_buffer *dst;
	bool abort_session;

	if (!h264 || !h264->multi_done_pending)
		return;
	h264->multi_done_pending = false;
	abort_session = h264->multi_done_error;
	if (h264->multi_done_error)
		dev_warn_ratelimited(core->dev_dec,
				     "h264 multi: completing incomplete picture as an error\n");

	dst = v4l2_m2m_dst_buf_remove_by_idx(sess->m2m_ctx,
					     sess->fw_idx_to_vb2_idx[h264->multi_done_fw_idx]);
	if (h264->multi_field_picture &&
	    !codec_h264_multi_field_claim(h264, dst && h264->multi_src_vbuf)) {
		dev_err_ratelimited(core->dev_dec, "h264 field: aborted or lost completion buffer\n");
		h264->multi_slice_data = NULL;
		codec_h264_multi_abort(sess);
		if (dst)
			v4l2_m2m_buf_done(dst, VB2_BUF_STATE_ERROR);
		if (h264->multi_src_vbuf) {
			h264->multi_slice_data = NULL;
			meson_amvdec_src_buf_done(sess, h264->multi_src_vbuf, VB2_BUF_STATE_ERROR);
			h264->multi_src_vbuf = NULL;
			atomic_dec(&sess->esparser_queued_bufs);
		}
		v4l2_m2m_job_finish(sess->m2m_dev, sess->m2m_ctx);
		return;
	}
	if (dst && h264->multi_src_vbuf && !h264->multi_done_error &&
	    !h264->multi_field_picture &&
	    h264->multi_done_fw_idx < ARRAY_SIZE(h264->multi_field_slots)) {
		struct h264_field_surface *surface =
			&h264->multi_field_slots[h264->multi_done_fw_idx];

		memset(surface, 0, sizeof(*surface));
		surface->valid = true;
		surface->decoded_fields = V4L2_H264_FRAME_REF;
		surface->structure = codec_h264_multi_current_structure(h264);
		surface->frame_num = h264->multi_slice_refs.frame_num;
		surface->timestamp = h264->multi_pic_ts;
		surface->vb2_idx = h264->multi_pic_vb2_idx;
		surface->top_poc = h264->multi_pic_top_poc;
		surface->bottom_poc = h264->multi_pic_bottom_poc;
		surface->poc = min(h264->multi_pic_top_poc, h264->multi_pic_bottom_poc);
		surface->sps = h264->multi_sps;
		surface->reference = !!h264->multi_nal_ref_idc;
	}

	if (dst && h264->multi_src_vbuf && h264->multi_done_error) {
		u32 plane;

		v4l2_m2m_buf_copy_metadata(h264->multi_src_vbuf, dst);
		for (plane = 0; plane < dst->vb2_buf.num_planes; plane++)
			vb2_set_plane_payload(&dst->vb2_buf, plane, 0);
		dst->sequence = sess->sequence_cap++;
		v4l2_m2m_buf_done(dst, VB2_BUF_STATE_ERROR);
	} else if (dst && h264->multi_src_vbuf) {
		u32 output_size = meson_amvdec_get_output_size(sess);

		switch (sess->pixfmt_cap) {
		case V4L2_PIX_FMT_NV12M:
			vb2_set_plane_payload(&dst->vb2_buf, 0, output_size);
			vb2_set_plane_payload(&dst->vb2_buf, 1, output_size / 2);
			break;
		case V4L2_PIX_FMT_YUV420M:
			vb2_set_plane_payload(&dst->vb2_buf, 0, output_size);
			vb2_set_plane_payload(&dst->vb2_buf, 1, output_size / 4);
			vb2_set_plane_payload(&dst->vb2_buf, 2, output_size / 4);
			break;
		}
		v4l2_m2m_buf_copy_metadata(h264->multi_src_vbuf, dst);
		dst->field = V4L2_FIELD_NONE;
		if (h264->multi_field_mate)
			dst->field = h264->multi_pic_top_poc < h264->multi_pic_bottom_poc ?
				     V4L2_FIELD_INTERLACED_TB : V4L2_FIELD_INTERLACED_BT;
		if (h264->multi_field_picture)
			dev_dbg(core->dev_dec,
				"h264 field complete: mate=%u fw=%u vb2=%u ts=%llu fields=%u field=%u top=%d bottom=%d\n",
				 h264->multi_field_mate, h264->multi_pic_fw_idx, dst->vb2_buf.index,
				 dst->vb2_buf.timestamp,
				 h264->multi_field_slots[h264->multi_pic_fw_idx].decoded_fields,
				 dst->field, h264->multi_pic_top_poc, h264->multi_pic_bottom_poc);
		dst->sequence = sess->sequence_cap++;
		v4l2_m2m_buf_done(dst, VB2_BUF_STATE_DONE);
		meson_amvdec_trace(sess, AMVDEC_TR_SIGNAL, dst->vb2_buf.index, 0, 0);
		dev_dbg(core->dev_dec, "h264 multi done: vb2=%u ts=%llu\n",
			dst->vb2_buf.index, dst->vb2_buf.timestamp);
	} else if (!dst) {
		dev_err_ratelimited(core->dev_dec,
				    "h264 multi: fw_idx %u has no CAPTURE buffer\n",
			h264->multi_done_fw_idx);
	} else {
		/*
		 * Return the owned destination as an error when no source supplies metadata.
		 */
		dev_err_ratelimited(core->dev_dec,
				    "h264 multi drop: vb2=%u removed with no OUTPUT buffer\n",
			dst->vb2_buf.index);
	}

	if (h264->multi_src_vbuf) {
		h264->multi_slice_data = NULL;
		meson_amvdec_src_buf_done(sess, h264->multi_src_vbuf,
				    h264->multi_done_error ? VB2_BUF_STATE_ERROR :
				    VB2_BUF_STATE_DONE);
		h264->multi_src_vbuf = NULL;
		atomic_dec(&sess->esparser_queued_bufs);
	}
	h264->multi_done_error = false;
	/*
	 * A short picture may already have been used as a reference by the
	 * firmware.  Continuing would either expose dependent pictures as valid
	 * or wait forever if the damaged stream leaves the firmware mid-slice.
	 * Fail both queues after returning the errored picture so userspace can
	 * tear down this session and start the next stream on a clean decoder.
	 */
	if (abort_session) {
		codec_h264_multi_stop_processor(sess, "done-error");
		codec_h264_multi_abort(sess);
	}

	v4l2_m2m_job_finish(sess->m2m_dev, sess->m2m_ctx);
}

/*
 * Called only before direct VLD feed, while the session worker owns the job.
 * The rejected destination is newly queued; already-DONE CAPTUREs are untouched.
 */
void meson_amvdec_codec_h264_multi_discard_capture(struct amvdec_session *sess, u32 vb2_idx)
{
	struct codec_h264 *h264 = sess->priv;
	u32 fw;

	if (!h264)
		return;
	/* Header synthesis updates this cache before firmware sees the bytes. */
	h264->multi_headers_valid = false;
	if (vb2_idx >= ARRAY_SIZE(sess->vb2_idx_to_fw_idx))
		return;
	fw = sess->vb2_idx_to_fw_idx[vb2_idx];
	if (fw >= H264_MULTI_MAX_FW_BUFS || fw >= sess->num_fw_bufs ||
	    sess->fw_idx_to_vb2_idx[fw] != vb2_idx)
		return;
	h264->multi_field_slots[fw].valid = false;
}

void meson_amvdec_codec_h264_multi_hold_src(struct amvdec_session *sess,
			       struct vb2_v4l2_buffer *vbuf)
{
	struct codec_h264 *h264 = sess->priv;

	if (h264) {
		h264->multi_src_vbuf = vbuf;
		if (!vbuf) {
			h264->multi_decoded_fields = 0;
			h264->multi_slice_data = NULL;
			h264->multi_slice_size = 0;
		}
	}
}

/*
 * Copy each access unit into cached memory for repeated byte-wise header
 * parsing. Hardware and synthesized-header writes still use the coherent
 * OUTPUT buffer. Fall back to parsing OUTPUT if allocation fails.
 */
const u8 *meson_amvdec_codec_h264_scan_copy(struct amvdec_session *sess, const u8 *src,
			       u32 len, u32 cap)
{
	struct codec_h264 *h264 = sess->priv;

	if (!h264 || !src || !len || len > cap)
		return src;
	if (cap > h264->scan_copy_size) {
		kvfree(h264->scan_copy);
		h264->scan_copy = kvmalloc(cap, GFP_KERNEL);
		h264->scan_copy_size = h264->scan_copy ? cap : 0;
		if (!h264->scan_copy)
			return src;
	}
	memcpy(h264->scan_copy, src, len);
	return h264->scan_copy;
}

int meson_amvdec_codec_h264_multi_feed_buffer(struct amvdec_session *sess,
				 struct vb2_buffer *vb)
{
	struct codec_h264 *h264 = sess->priv;
	struct amvdec_core *core = sess->core;
	dma_addr_t src_dma = vb2_dma_contig_plane_dma_addr(vb, 0);
	u32 payload = vb2_get_plane_payload(vb, 0);
	u32 fifo_size = round_down(vb2_plane_size(vb, 0), VDEC_FIFO_ALIGN);
	int i, ret;

	/* WP includes read-ahead, but must stay inside the allocated ring. */
	if (!payload || fifo_size <= VLD_PADDING_SIZE ||
	    payload >= fifo_size - VLD_PADDING_SIZE ||
	    !IS_ALIGNED(src_dma, VDEC_FIFO_ALIGN))
		return -EINVAL;
	/* Headers have been synthesized already; retain the actual DMA payload. */
	h264->multi_src_vaddr = vb2_plane_vaddr(vb, 0);
	h264->multi_slice_data = meson_amvdec_codec_h264_scan_copy(sess, h264->multi_src_vaddr,
						      payload, vb2_plane_size(vb, 0));
	h264->multi_slice_size = payload;
	h264->multi_slice_cursor = 0;
	h264->multi_slice_count = 0;
	h264->multi_slice_type_mask = 0;
	h264->multi_slice_uniform = false;
	h264->multi_slice_mv_offset = 0;
	h264->multi_done_error = false;
	if (!h264->multi_slice_data)
		return -EINVAL;
	if (!h264->multi_pic_valid)
		return h264->multi_params_error ? h264->multi_params_error : -EINVAL;
	ret = codec_h264_multi_field_preflight(sess);
	if (!ret && !h264->multi_request_picture.field_pic)
		ret = codec_h264_multi_frame_preflight(sess);
	if (ret) {
		dev_err_ratelimited(core->dev_dec, "h264 field: source rejected: %d\n", ret);
		/* No feed occurred; esparser retires only this request. */
		return ret;
	}

	/*
	 * Clear padding so VLD cannot read stale OUTPUT bytes beyond the payload.
	 */
	memset(h264->multi_src_vaddr + payload, 0, VLD_PADDING_SIZE);
	dma_sync_single_for_device(core->dev, src_dma + payload,
				   VLD_PADDING_SIZE, DMA_TO_DEVICE);

	dev_dbg(core->dev_dec, "h264 multi feed enter: pic=%u paddr=%pad payload=%u\n",
		h264->multi_pic_count, &src_dma, payload);

	/* Halt AMRISC before resetting the VLD to avoid outstanding bus transactions. */
	meson_amvdec_write_dos(core, MPSR, 0);
	meson_amvdec_write_dos(core, CPSR, 0);
	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, LMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}

	/*
	 * Restore input and per-picture context before resetting and restarting
	 * the firmware processors. Use OUTPUT as the VLD FIFO.
	 */
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(5) | BIT(4) | BIT(3));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_START_PTR, src_dma);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_END_PTR,
			 src_dma + fifo_size - VDEC_FIFO_ALIGN);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CURR_PTR,
			 round_down(src_dma, VDEC_FIFO_ALIGN));

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 1);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 2);

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_RP,
			 round_down(src_dma, VDEC_FIFO_ALIGN));
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_WP,
			 round_down(src_dma + payload + VLD_PADDING_SIZE,
				    VDEC_FIFO_ALIGN));

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 3);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 2);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL,
			 (0x11 << 16) | BIT(10) | (7 << 3));

	meson_amvdec_write_dos(core, AV_SCRATCH_1, 0);
	meson_amvdec_write_dos(core, M4_CONTROL_REG, BIT(13));
	meson_amvdec_write_dos(core, H264_DECODE_SIZE, payload);
	meson_amvdec_write_dos(core, VIFF_BIT_CNT, payload * 8);

	/* Restore the per-picture hardware context. */
	meson_amvdec_write_dos(core, POWER_CTL_VLD,
			 meson_amvdec_read_dos(core, POWER_CTL_VLD) |
			 BIT(9) | BIT(6));
	meson_amvdec_write_dos(core, PSCALE_CTRL, 0);
	meson_amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	meson_amvdec_write_dos(core, ASSIST_MBOX1_MASK, 1);

	meson_amvdec_write_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));
	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(16));
	meson_amvdec_write_dos_bits(core, MDEC_PIC_DC_CTRL, 0xbfu << 24);
	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, 0xbfu << 24);
	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));
	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_MUX_CTRL, BIT(31));
	meson_amvdec_write_dos(core, MDEC_EXTIF_CFG1, 0);
	meson_amvdec_write_dos(core, MDEC_PIC_DC_THRESH, 0x404038aa);

	meson_amvdec_write_dos(core, DPB_STATUS_REG, 0);
	if (h264->lmem_paddr)
		meson_amvdec_write_dos(core, LMEM_DUMP_ADR, h264->lmem_paddr);
	meson_amvdec_write_dos(core, FRAME_COUNTER_REG, h264->multi_pic_count);
	meson_amvdec_write_dos(core, AV_SCRATCH_8,
			 codec_h264_multi_workspace_base(h264));
	meson_amvdec_write_dos(core, AV_SCRATCH_F,
			 (meson_amvdec_read_dos(core, AV_SCRATCH_F) & 0xffffffc3) |
			 BIT(4));
	meson_amvdec_clear_dos_bits(core, AV_SCRATCH_F, BIT(6));
	/* The vendor driver sets this register only on GXL and newer, not on GXBB. */
	if (core->platform->revision >= AMVDEC_REVISION_GXL)
		meson_amvdec_write_dos(core, IQIDCT_CONTROL, 0x200);

	/* Enable frame-based decode and stream input. */
	meson_amvdec_write_dos(core, H264_DECODE_MODE, DECODE_MODE_MULTI_FRAMEBASE);
	meson_amvdec_write_dos(core, HEAD_PADING_REG, 0);
	meson_amvdec_write_dos(core, H264_DECODE_SEQINFO, h264->multi_seq_cfg);

	/*
	 * Restore sequence words after VLD reset. Leave AV_SCRATCH_1 zero so the
	 * firmware takes geometry from H264_DECODE_SEQINFO.
	 */
	meson_amvdec_write_dos(core, AV_SCRATCH_2, h264->multi_seq_cfg2);
	meson_amvdec_write_dos(core, AV_SCRATCH_B, h264->multi_seq_cfgb);
	/* INIT_FLAG_REG aliases AV_SCRATCH_2; write the initialization flag last. */
	meson_amvdec_write_dos(core, INIT_FLAG_REG, 1);
	meson_amvdec_write_dos(core, AV_SCRATCH_0, h264->multi_seq_scratch0);
	meson_amvdec_write_dos(core, NAL_SEARCH_CTL, h264->multi_nal_search_or);

	meson_amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 0x6);
	meson_amvdec_write_dos_bits(core, NAL_SEARCH_CTL, BIT(16));
	meson_amvdec_write_dos_bits(core, MDEC_EXTIF_CFG2, 0x20);
	meson_amvdec_clear_dos_bits(core, NAL_SEARCH_CTL, 0x2);
	meson_amvdec_clear_dos_bits(core, VDEC_ASSIST_MMC_CTRL1, BIT(3));

	/* Restart the processors and request the slice header. */
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_write_dos(core, MPSR, 1);

	h264->multi_pic_started = false;
	h264->multi_hw_started = true;
	meson_amvdec_write_dos_action(core, DPB_STATUS_REG,
				H264_ACTION_SEARCH_HEAD);

	dev_dbg(core->dev_dec,
		"h264 multi feed: pic=%u paddr=%pad payload=%u\n",
		 h264->multi_pic_count, &src_dma, payload);
	h264->multi_pic_count++;
	return 0;
}

/*
 * H.264 uses multi-instance firmware with direct OUTPUT input. Keep the
 * processors halted until the first request supplies input and context.
 */
struct amvdec_codec_ops meson_amvdec_codec_h264_multi_ops = {
	.start = codec_h264_start,
	.stop = codec_h264_stop,
	.pre_stop = codec_h264_pre_stop,
	.job_abort = codec_h264_job_abort,
	.load_firmware = codec_h264_load_firmware,
	.post_start = codec_h264_post_start,
	.isr = codec_h264_isr,
	.threaded_isr = codec_h264_threaded_isr,
	.direct_input = true,
};
