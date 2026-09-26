/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * H.264 frame and field reference-list helpers.
 */

#ifndef __MESON_AMVDEC_CODEC_H264_REFS_H_
#define __MESON_AMVDEC_CODEC_H264_REFS_H_

#include "codec_h264_synth.h"

/*
 * Keep FrameNumWrap signed: short-term references above the current
 * frame_num wrap to negative PicNum values (H.264 8-27 and 8-28).
 */

struct h264_ref {
	int fw;
	s32 poc, top, bottom;
	s32 pic_num;
	bool lt, field;
	u8 view, dpb_idx, structure;
};

/* Field rows share one interleaved motion-vector slot. */
static inline u32 h264_colocate_read_offset(u32 first_mb, u32 mb_width,
					    u8 cur_structure, u8 ref_structure,
					 bool direct_8x8)
{
	u32 mby = first_mb / mb_width, mbx = first_mb % mb_width;
	u32 units;

	if (cur_structure < 2)
		units = ref_structure != 2 ? first_mb * 2 : mby * 2 * mb_width + mbx;
	else if (ref_structure < 2)
		units = ((cur_structure == 3 ? mby : mby / 2) * mb_width + mbx) * 2;
	else
		units = cur_structure == 2 ? first_mb : first_mb * 2;
	return (units * 96) >> (direct_8x8 ? 2 : 0);
}

/* Metadata describes bytes actually completed, independently of a DPB view. */
struct h264_field_surface {
	bool valid, bottom, reference;
	u8 decoded_fields, structure;
	s32 top_poc, bottom_poc;
	u32 vb2_idx, frame_num;
	unsigned long long timestamp;
	s32 poc;
	struct v4l2_ctrl_h264_sps sps;
};

/*
 * Pair opposite fields with the same frame_num, reference class, SPS and
 * capture generation. Each field request may supply a different PPS.
 */
static inline bool h264_field_is_mate(const struct h264_field_surface *surface,
				      const struct h264_slice_refs *picture,
				      unsigned long long timestamp,
		u32 vb2_idx, const struct v4l2_ctrl_h264_sps *sps,
		const struct v4l2_ctrl_h264_pps *pps)
{
	return surface->valid && surface->decoded_fields == (1U << surface->bottom) &&
		surface->timestamp == timestamp && surface->vb2_idx == vb2_idx &&
		picture->field_pic && !picture->idr &&
		surface->bottom != picture->bottom_field &&
		surface->frame_num == picture->frame_num &&
		surface->reference == !!picture->nal_ref_idc &&
		pps->seq_parameter_set_id == sps->seq_parameter_set_id &&
		!memcmp(&surface->sps, sps, sizeof(*sps));
}

/* A DPB view may expose only parity bytes actually completed in this slot. */
static inline bool h264_surface_view_valid(const struct h264_field_surface *surface,
					   const struct v4l2_h264_dpb_entry *entry, u8 view,
		const struct v4l2_ctrl_h264_sps *sps)
{
	if (!view || view > V4L2_H264_FRAME_REF ||
	    (entry->fields & view) != view ||
	    !(entry->flags & V4L2_H264_DPB_ENTRY_FLAG_VALID))
		return false;
	if (!surface->valid)
		return !(entry->flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD);
	return surface->timestamp == entry->reference_ts &&
		(surface->decoded_fields & view) == view &&
		!memcmp(&surface->sps, sps, sizeof(*sps)) &&
		(!(view & V4L2_H264_TOP_FIELD_REF) ||
		 surface->top_poc == entry->top_field_order_cnt) &&
		(!(view & V4L2_H264_BOTTOM_FIELD_REF) ||
		 surface->bottom_poc == entry->bottom_field_order_cnt);
}

static inline u32 h264_surface_info0(const struct h264_field_surface *surface)
{
	u32 info;

	switch (surface->structure) {
	case 0:
		info = 0xf400;
		break;
	case 1:
		info = 0xf440;
		break;
	case 3:
		info = 0xf4c0;
		break;
	case 2:
	default:
		info = 0xf480;
		break;
	}
	if (surface->decoded_fields == V4L2_H264_FRAME_REF &&
	    surface->bottom_poc < surface->top_poc)
		info |= 0x100;
	return info;
}

/*
 * Apply H.264 8.2.4.3 list modifications from an immutable inventory.
 * Remove duplicates only after the newly fixed prefix. The caller provides
 * 32 entries; errors leave the list and length unchanged.
 */
#ifdef __KERNEL__
static noinline
#else
static
#endif
int h264_refs_modify(struct h264_ref *list, u32 *len,
		     const struct h264_ref *inventory, u32 count,
				  const struct h264_slice_refs *sr, u32 lx,
				  u32 max_pic_num)
{
	struct h264_ref work[V4L2_H264_REF_LIST_LEN + 1];
	u32 n = *len, idx, k, dst, active;
	s32 curr = sr->field_pic ? 2 * sr->frame_num + 1 : sr->frame_num;
	s32 pred;

	if (lx > 1 || n > V4L2_H264_REF_LIST_LEN ||
	    count > (sr->field_pic ? V4L2_H264_REF_LIST_LEN :
		     V4L2_H264_NUM_DPB_ENTRIES) ||
	    !max_pic_num || max_pic_num > (sr->field_pic ? 131072 : 65536) ||
	    sr->frame_num >= (max_pic_num >> !!sr->field_pic) ||
	    sr->n_mod[lx] > H264_MAX_RPLM)
		return -EINVAL;
	if (!sr->n_mod[lx])
		return 0;
	active = sr->num_ref_idx_active[lx];
	if (!active || active > V4L2_H264_REF_LIST_LEN || sr->n_mod[lx] > active)
		return -EINVAL;

	memcpy(work, list, n * sizeof(*list));
	pred = curr;
	for (idx = 0; idx < sr->n_mod[lx]; idx++) {
		const struct h264_rplm_op *op = &sr->mod[lx][idx];
		const struct h264_ref *ref = NULL;
		s32 pic_num;

		if (op->idc < 2) {
			s32 diff;

			if (op->arg >= max_pic_num)
				return -EINVAL;
			diff = op->arg + 1;
			pred += op->idc ? diff : -diff;
			if (pred < 0)
				pred += max_pic_num;
			else if (pred >= (s32)max_pic_num)
				pred -= max_pic_num;
			pic_num = pred > curr ?
				  pred - (s32)max_pic_num : pred;
		} else if (op->idc == 2) {
			/* Inventory carries LongTermPicNum, including field parity. */
			if (op->arg >= max_pic_num)
				return -EINVAL;
			pic_num = op->arg;
		} else {
			return -EINVAL;
		}
		for (k = 0; k < count; k++)
			if (inventory[k].lt == (op->idc == 2) &&
			    inventory[k].pic_num == pic_num) {
				ref = &inventory[k];
				break;
			}
		if (!ref || idx > n)
			return -EINVAL;

		for (k = n; k > idx; k--)
			work[k] = work[k - 1];
		work[idx] = *ref;
		dst = idx + 1;
		for (k = idx + 1; k <= n; k++)
			if (work[k].lt != ref->lt || work[k].pic_num != ref->pic_num)
				work[dst++] = work[k];
		n = dst > V4L2_H264_REF_LIST_LEN ? V4L2_H264_REF_LIST_LEN : dst;
	}
	/*
	 * Limit the active count to initialized entries. Every explicit target
	 * must exist in the inventory; the register writer pads with the last entry.
	 */
	if (n > active)
		n = active;
	memcpy(list, work, n * sizeof(*list));
	*len = n;
	return 0;
}
#endif
