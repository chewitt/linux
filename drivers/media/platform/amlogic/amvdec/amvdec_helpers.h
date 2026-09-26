/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Shared decoder, canvas and DMA helpers.
 */

#ifndef __MESON_AMVDEC_HELPERS_H_
#define __MESON_AMVDEC_HELPERS_H_

#include "amvdec.h"

/*
 * G12A, G12B and SM1 route decoder DDR requests through the DMC and provide
 * RESET7 pipeline resets; GXBB/GXL/GXM do not.
 */
static inline bool amvdec_is_g12(struct amvdec_core *core)
{
	return core->platform->revision == AMVDEC_REVISION_G12A ||
	       core->platform->revision == AMVDEC_REVISION_SM1;
}

void *meson_amvdec_dma_alloc_guarded(struct device *dev, size_t size,
			       dma_addr_t *dma_handle, gfp_t gfp,
				       struct amvdec_dma_guard *guard);
void meson_amvdec_dma_free_guarded(struct device *dev, dma_addr_t visible_paddr,
			     struct amvdec_dma_guard *guard);
int meson_amvdec_dma_verify_guard(struct device *dev, const char *name,
			    const struct amvdec_dma_guard *guard);
bool meson_amvdec_dma_guards_enabled(void);

/*
 * Coded-size floors shared by format negotiation and codec validation: VP9
 * allows 8-pixel dimensions; other formats require at least 16 pixels.
 */
static inline u32 amvdec_min_coded_width(u32 pixfmt)
{
	if (pixfmt == V4L2_PIX_FMT_VP9_FRAME)
		return 8;

	return 16;
}

static inline u32 amvdec_min_coded_height(u32 pixfmt)
{
	if (pixfmt == V4L2_PIX_FMT_VP9_FRAME)
		return 8;

	return 16;
}

/* Map one prepared CAPTURE buffer and retain its canvas index for the session. */
int meson_amvdec_map_capture_buffer(struct amvdec_session *sess,
			      struct vb2_buffer *vb,
			      u32 reg_base[], u32 reg_num[]);

/* Complete the request control handler before returning the OUTPUT buffer. */
void meson_amvdec_src_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf,
			 enum vb2_buffer_state state);

/* Helpers to read/write to the various IPs (DOS, PARSER) */
u32 meson_amvdec_read_dos(struct amvdec_core *core, u32 reg);
void meson_amvdec_write_dos(struct amvdec_core *core, u32 reg, u32 val);
int meson_amvdec_canvas_nv12m_spec(struct amvdec_session *sess, struct vb2_buffer *vb,
			     u32 width, u32 height, u32 *spec);
bool meson_amvdec_request_jobs(struct amvdec_session *sess);
int meson_amvdec_request_begin(struct amvdec_session *sess, struct vb2_v4l2_buffer *src);
int meson_amvdec_request_resolve_ref(struct amvdec_session *sess, unsigned int index, u64 ts);
void meson_amvdec_request_signal(struct amvdec_session *sess, struct vb2_v4l2_buffer *dst);
void meson_amvdec_request_retire(struct amvdec_session *sess, bool cancel);
void meson_amvdec_request_refuse(struct amvdec_session *sess);

void meson_amvdec_write_dos_action(struct amvdec_core *core, u32 reg, u32 val);
void meson_amvdec_write_dos_bits(struct amvdec_core *core, u32 reg, u32 val);
void meson_amvdec_clear_dos_bits(struct amvdec_core *core, u32 reg, u32 val);
u32 meson_amvdec_read_parser(struct amvdec_core *core, u32 reg);
void meson_amvdec_write_parser(struct amvdec_core *core, u32 reg, u32 val);

/*
 * Park DMC request ports before decoder resets or power transitions. The
 * DMC registers are outside the decoder power domains; DOS registers require
 * powered memories and removed isolation.
 */
int meson_amvdec_dmc_park(struct amvdec_core *core, u32 mask);
void meson_amvdec_dmc_unpark(struct amvdec_core *core, u32 mask);
/* G12B latches DMC-side decode pipeline state; pulse it before unparking */
void meson_amvdec_dmc_pipeline_reset(struct amvdec_core *core);

/* Helpers for the Amlogic compressed framebuffer format */
u32 meson_amvdec_amfbc_body_size(u32 width, u32 height, u32 is_10bit, u32 use_mmu);
u32 meson_amvdec_amfbc_head_size(u32 width, u32 height);
u32 meson_amvdec_amfbc_size(u32 width, u32 height, u32 is_10bit, u32 use_mmu);

int meson_amvdec_dst_buf_prepare_hevc(struct amvdec_session *sess,
				struct vb2_v4l2_buffer *vbuf, u32 offset,
			      struct v4l2_ctrl_hevc_slice_params *slice,
			      struct v4l2_ctrl_hevc_decode_params *decode,
			      u32 *input_end, u32 *input_offset);
int meson_amvdec_dst_buf_prepare_stateless(struct amvdec_session *sess,
				     struct vb2_v4l2_buffer *vbuf,
				     struct v4l2_ctrl_vp9_frame *vp9_frame, u32 *input_end);
void meson_amvdec_dst_buf_done_stateless(struct amvdec_session *sess,
				   struct vb2_v4l2_buffer *vbuf, u32 field);
void meson_amvdec_dst_buf_error_stateless(struct amvdec_session *sess,
				    struct vb2_v4l2_buffer *vbuf);

/**
 * meson_amvdec_add_ts() - Add a timestamp to the list
 *
 * @sess: current session
 * @ts: timestamp to add
 * @tc: timecode to add
 * @offset: offset in the VIFIFO of the associated input packet
 * @flags: the vb2_v4l2_buffer flags
 * @vp9_frame: optional copied VP9_FRAME request control
 */
int meson_amvdec_add_ts(struct amvdec_session *sess, u64 ts,
		  struct v4l2_timecode tc, u32 offset, u32 flags,
		  const struct v4l2_ctrl_vp9_frame *vp9_frame,
		  const struct v4l2_ctrl_hevc_sps *hevc_sps,
		  const struct v4l2_ctrl_hevc_pps *hevc_pps,
		  const struct v4l2_ctrl_hevc_slice_params *hevc_slice,
		  const struct v4l2_ctrl_hevc_decode_params *hevc_decode);
int meson_amvdec_set_input_end(struct amvdec_session *sess, u64 ts, u32 end);
void meson_amvdec_remove_ts(struct amvdec_session *sess, u64 ts);

unsigned int meson_amvdec_flush_ts(struct amvdec_session *sess);

/**
 * meson_amvdec_abort() - Abort the current decoding session
 *
 * @sess: current session
 */
void meson_amvdec_abort(struct amvdec_session *sess);
#endif
