// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * VC-1 / WMV9 support for the Amlogic VDEC_1 engine.
 *
 * Structurally this is the MPEG-4 codec's twin, and it shares that codec's
 * one genuinely dangerous property: the microcode addresses its DC/AC
 * prediction and MV scratch buffers at a FIXED physical base plus the offset
 * it is handed in AV_SCRATCH_F.  The base differs (0x02e00000 here against
 * MPEG-4's 0x02b00000) and the region is twice the size, but the failure mode
 * for leaving AV_SCRATCH_F unwritten is identical: the microcode writes into
 * whatever happens to live at that physical address, which on this SoC is
 * ordinary kernel memory, and it does so at codec start before any bitstream
 * is parsed.
 *
 * The g12a blob uses four canvases in AV_SCRATCH_0..3 and reports buffer
 * numbers 1..4. Newer vendor source describes an incompatible eight-buffer
 * protocol; do not use its zero-based BUFFEROUT interpretation here.
 * AV_SCRATCH_J/K report the decoded width and height.
 *
 * Deliberately absent: the vendor driver opens its prot_init with a
 * DOS_SW_RESET0 sequence.  That is not repeated here.  vdec_1 owns the reset
 * and power-transition discipline on this driver, and issuing a DOS reset
 * from codec start - after __vdec_1_start() has already reconnected the
 * VDEC_1 DMC port - is itself a recorded way to hang this SoC.
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_vc1.h"
#include "codec_vc1_parser.h"
#include "dos_regs.h"
#include "esparser.h"
#include "vdec_helpers.h"

/* Scratch region for the microcode's own state */
#define SIZE_WORKSPACE		(2 * SZ_1M)

/* Physical base the microcode adds AV_SCRATCH_F to. Baked into the blob. */
#define DCAC_BUFF_START_ADDR	0x02e00000

/* map firmware registers to known VC-1 functions */
/*
 * Selects which VC-1 flavour the microcode expects: 0 for WMV3 (Simple and
 * Main profile, size described by the container) and 1 for WVC1 (Advanced
 * profile, in-band sequence header).  It is zero out of reset, so leaving it
 * alone silently puts the microcode in WMV3 mode; fed Advanced Profile BDUs
 * it then never recognises a picture, never raises an interrupt, and the
 * session simply produces nothing.
 */
#define VC1_PROFILE_SEL		AV_SCRATCH_4
#define VC1_ERROR_COUNT		AV_SCRATCH_6
#define VC1_SOS_COUNT		AV_SCRATCH_7
#define VC1_BUFFERIN		AV_SCRATCH_8
#define VC1_BUFFEROUT		AV_SCRATCH_9
#define VC1_OFFSET_REG		AV_SCRATCH_C
#define MREG_BUF_OFFSET		AV_SCRATCH_F
/* Geometry reported by the microcode */
#define VC1_PIC_WIDTH		AV_SCRATCH_J
#define VC1_PIC_HEIGHT		AV_SCRATCH_K

#define INTERLACE_FLAG		0x80
#define BOTTOM_FIELD_FIRST_FLAG	0x40

#define VC1_MAX_WIDTH		1920
#define VC1_MAX_HEIGHT		1088

struct codec_vc1 {
	struct vc1_stream stream;
	bool draining;
	u32 eos_offset;
	void	  *workspace_vaddr;
	dma_addr_t workspace_paddr;
};

u32 codec_vc1_prepare_input(struct amvdec_session *sess, struct vb2_buffer *vb)
{
	struct codec_vc1 *vc1 = sess->priv;
	u32 len = vb2_get_plane_payload(vb, 0);
	u32 capacity = vb2_plane_size(vb, 0);
	int ret;

	if (capacity < SZ_4K + SZ_512 || !vb2_plane_vaddr(vb, 0))
		return 0;
	ret = vc1_prepare_stream(&vc1->stream, vb2_plane_vaddr(vb, 0),
				 &len, capacity - SZ_512, sess->width, sess->height);
	if (ret) {
		dev_err(sess->core->dev, "VC-1 input preparation failed: %d\n", ret);
		return 0;
	}
	return len;
}

int codec_vc1_queue_eos(struct amvdec_session *sess, u32 offset)
{
	struct codec_vc1 *vc1 = sess->priv;
	u8 *data;
	int size, ret;

	if (vc1->draining)
		return 0;
	data = kzalloc(SZ_16K, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* The legacy firmware holds two reference pictures at sequence end.
	 * Two neutral P pictures release them. Discard any synthetic output
	 * by its stream offset rather than returning it as a real frame.
	 */
	size = vc1_flush_picture(&vc1->stream, data + 4, sess->height);
	if (size < 0) {
		kfree(data);
		return size;
	}
	data[2] = 1;
	data[3] = 0x0d;
	memcpy(data + SZ_4K, data, size + 4);
	data[SZ_8K + 2] = 1;
	data[SZ_8K + 3] = 0x0a;
	vc1->eos_offset = offset;
	vc1->draining = true;
	ret = esparser_queue_eos(sess->core, data, SZ_16K);
	kfree(data);
	return ret;
}

static int codec_vc1_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, VC1_BUFFERIN);
}

static void codec_vc1_recycle(struct amvdec_core *core, u32 buf_idx)
{
	/* Complement of a one-hot mask, as MPEG-4 - not a 1-based index */
	amvdec_write_dos(core, VC1_BUFFERIN, ~(1 << buf_idx));
}

static int codec_vc1_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vc1 *vc1;
	u32 buf_offset;
	int ret;

	if (sess->width < 16 || sess->height < 16 ||
	    sess->width > VC1_MAX_WIDTH || sess->height > VC1_MAX_HEIGHT) {
		dev_err(core->dev, "VC-1: refusing implausible geometry %ux%u\n",
			sess->width, sess->height);
		return -EINVAL;
	}

	vc1 = kzalloc_obj(*vc1);
	if (!vc1)
		return -ENOMEM;

	vc1->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						  &vc1->workspace_paddr,
						  GFP_KERNEL);
	if (!vc1->workspace_vaddr) {
		dev_err(core->dev, "Failed to request VC-1 workspace\n");
		ret = -ENOMEM;
		goto free_vc1;
	}

	if (vc1->workspace_paddr < DCAC_BUFF_START_ADDR) {
		dev_err(core->dev,
			"VC-1: workspace at %pad is below the microcode's base %#x\n",
			&vc1->workspace_paddr, DCAC_BUFF_START_ADDR);
		ret = -ERANGE;
		goto free_workspace;
	}
	buf_offset = vc1->workspace_paddr - DCAC_BUFF_START_ADDR;

	/* Four canvases, matching the g12a firmware protocol. */
	ret = amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_0, 0 },
				  (u32[]){ 4, 0 });
	if (ret)
		goto free_workspace;

	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));

	/*
	 * This microcode wants a deeper input FIFO than vdec_1's generic
	 * setup leaves behind (which is FIFO_CNT 1 / level 4).
	 */
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
			      (0x3 << MEM_FIFO_CNT_BIT) |
			      (0x3f << MEM_LEVEL_CNT_BIT));
	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
			      (2 << MEM_FIFO_CNT_BIT) |
			      (8 << MEM_LEVEL_CNT_BIT));

	/* Tell the microcode where its scratch region actually is */
	amvdec_write_dos(core, MREG_BUF_OFFSET, buf_offset);

	/* disable PSCALE for hardware sharing */
	amvdec_write_dos(core, PSCALE_CTRL, 0);

	/* We only offer Annex G, which is Advanced Profile */
	amvdec_write_dos(core, VC1_PROFILE_SEL, 1);

	amvdec_write_dos(core, VC1_SOS_COUNT, 0);
	amvdec_write_dos(core, VC1_BUFFERIN, 0);
	amvdec_write_dos(core, VC1_BUFFEROUT, 0);

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	amvdec_write_dos(core, ASSIST_MBOX1_MASK, 1);

	sess->keyframe_found = 1;
	sess->priv = vc1;

	return 0;

free_workspace:
	dma_free_coherent(core->dev, SIZE_WORKSPACE, vc1->workspace_vaddr,
			  vc1->workspace_paddr);
free_vc1:
	kfree(vc1);

	return ret;
}

static int codec_vc1_stop(struct amvdec_session *sess)
{
	struct codec_vc1 *vc1 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (vc1 && vc1->workspace_vaddr) {
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  vc1->workspace_vaddr, vc1->workspace_paddr);
		vc1->workspace_vaddr = NULL;
	}

	return 0;
}

static irqreturn_t codec_vc1_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;
	u32 offset;
	u32 w, h;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);

	reg = amvdec_read_dos(core, VC1_BUFFEROUT);
	if (!reg)
		return IRQ_HANDLED;

	/*
	 * The microcode reports the coded size it actually decoded.  Do not
	 * silently follow it: the canvases were already built from the size
	 * announced at source-change time, so a disagreement means the
	 * microcode is writing to a geometry we did not size for.
	 */
	w = amvdec_read_dos(core, VC1_PIC_WIDTH);
	h = amvdec_read_dos(core, VC1_PIC_HEIGHT);
	if (w && h && w <= VC1_MAX_WIDTH && h <= VC1_MAX_HEIGHT &&
	    (w != sess->width || h != sess->height)) {
		dev_err_once(core->dev,
			     "VC-1: microcode decoded %ux%u but canvases are %ux%u; aborting\n",
			     w, h, sess->width, sess->height);
		amvdec_write_dos(core, VC1_BUFFEROUT, 0);
		amvdec_abort(sess);
		return IRQ_HANDLED;
	}

	if (!(reg & 0x7) || (reg & 0x7) > 4) {
		dev_err(core->dev, "VC-1: invalid firmware buffer %u\n", reg & 0x7);
		amvdec_write_dos(core, VC1_BUFFEROUT, 0);
		amvdec_abort(sess);
		return IRQ_HANDLED;
	}
	buffer_index = (reg & 0x7) - 1;

	if (reg & INTERLACE_FLAG)
		field = (reg & BOTTOM_FIELD_FIRST_FLAG) ?
			V4L2_FIELD_INTERLACED_BT :
			V4L2_FIELD_INTERLACED_TB;

	/* The reported offset can precede the picture BDU by one 256-byte
	 * VLD fetch. ESPARSER pads each packet to at least 4 KiB, so this
	 * allowance cannot cross into the following packet's timestamp.
	 */
	offset = amvdec_read_dos(core, VC1_OFFSET_REG) + SZ_256;
	if (((struct codec_vc1 *)sess->priv)->draining &&
	    (s32)(offset - ((struct codec_vc1 *)sess->priv)->eos_offset) >= 0) {
		amvdec_write_dos(core, VC1_BUFFEROUT, 0);
		return IRQ_HANDLED;
	}
	amvdec_dst_buf_done_idx(sess, buffer_index, offset, field);

	amvdec_write_dos(core, VC1_BUFFEROUT, 0);
	return IRQ_HANDLED;
}

static irqreturn_t codec_vc1_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_vc1_ops = {
	.start = codec_vc1_start,
	.stop = codec_vc1_stop,
	.isr = codec_vc1_isr,
	.threaded_isr = codec_vc1_threaded_isr,
	.can_recycle = codec_vc1_can_recycle,
	.recycle = codec_vc1_recycle,
};
