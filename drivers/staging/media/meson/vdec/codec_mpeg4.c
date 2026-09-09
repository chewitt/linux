// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * MPEG-4 Part 2 (DivX/Xvid) and H.263 support for the Amlogic VDEC_1 engine,
 * driving the vendor's "legacy" free-running microcode.
 *
 * Two properties of this microcode differ from MPEG1/2 and must be honoured
 * exactly, because getting either wrong is silent and catastrophic rather
 * than merely wrong:
 *
 *  - It addresses its DC/AC prediction, MV and CBP scratch buffers at the
 *    FIXED physical base DCAC_BUFF_START_IP plus the offset handed to it in
 *    AV_SCRATCH_F.  Leaving AV_SCRATCH_F at zero does not disable the
 *    feature: the microcode then writes to physical 0x02b00000, which on
 *    this SoC is ordinary kernel memory.  That is a bus-master write into
 *    the running kernel, and it happens at codec start before a single byte
 *    of bitstream is parsed.
 *
 *  - Its eight decode buffers are described by canvas index triplets in
 *    AV_SCRATCH_0..3 and AV_SCRATCH_G..J, which are NOT contiguous.  The
 *    four registers in between are AV_SCRATCH_4..7, and AV_SCRATCH_7 is
 *    MP4_PIC_WH, the picture geometry.  Writing eight consecutive canvas
 *    entries from AV_SCRATCH_0 - the layout MPEG1/2 uses - therefore both
 *    overwrites the geometry and leaves buffers 4..7 pointing at canvas 0.
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_mpeg4.h"
#include "dos_regs.h"
#include "vdec_helpers.h"

/*
 * Scratch region for the microcode's own state.  The vendor driver allocates
 * exactly this much as the last of its buffers and derives AV_SCRATCH_F from
 * where it landed.
 */
#define SIZE_WORKSPACE		SZ_1M

/*
 * Physical base the microcode adds AV_SCRATCH_F to when addressing that
 * region.  Not a hint and not a default - the value is baked into the blob.
 */
#define DCAC_BUFF_START_IP	0x02b00000

/* map firmware registers to known MPEG-4 functions */
#define MP4_RATE		AV_SCRATCH_3
#define MP4_PIC_RATIO		AV_SCRATCH_5
#define MP4_ERR_COUNT		AV_SCRATCH_6
#define MP4_PIC_WH		AV_SCRATCH_7
#define MREG_BUFFERIN		AV_SCRATCH_8
#define MREG_BUFFEROUT		AV_SCRATCH_9
#define MP4_NOT_CODED_CNT	AV_SCRATCH_A
#define MP4_VOP_TIME_INC	AV_SCRATCH_B
#define MP4_OFFSET_REG		AV_SCRATCH_C
#define MP4_SYS_RATE		AV_SCRATCH_E
#define MREG_BUF_OFFSET		AV_SCRATCH_F
#define MREG_FATAL_ERROR	AV_SCRATCH_L

#define INTERLACE_FLAG		0x80

/* Widest picture this microcode is built for */
#define MP4_MAX_WIDTH		1920
#define MP4_MAX_HEIGHT		1088

struct codec_mpeg4 {
	void	  *workspace_vaddr;
	dma_addr_t workspace_paddr;
};

static const u8 eos_sequence[SZ_1K] = { 0x00, 0x00, 0x01, 0xB1 };

static const u8 *codec_mpeg4_eos_sequence(u32 *len)
{
	*len = ARRAY_SIZE(eos_sequence);
	return eos_sequence;
}

static int codec_mpeg4_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, MREG_BUFFERIN);
}

static void codec_mpeg4_recycle(struct amvdec_core *core, u32 buf_idx)
{
	/*
	 * Unlike MPEG1/2, which takes a 1-based index, this microcode takes
	 * the complement of a one-hot mask.
	 */
	amvdec_write_dos(core, MREG_BUFFERIN, ~(1 << buf_idx));
}

static int codec_mpeg4_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_mpeg4 *mpeg4;
	u32 buf_offset;
	int ret;

	/*
	 * The geometry goes straight into the canvas setup and into
	 * MP4_PIC_WH.  A bogus value here means the microcode decodes a
	 * full-size picture into undersized canvases, which is a wild write.
	 * There is no in-band source for it on this codec - it comes from the
	 * client's S_FMT - so validate it rather than trust it.
	 */
	if (sess->width < 16 || sess->height < 16 ||
	    sess->width > MP4_MAX_WIDTH || sess->height > MP4_MAX_HEIGHT) {
		dev_err(core->dev, "MPEG-4: refusing implausible geometry %ux%u\n",
			sess->width, sess->height);
		return -EINVAL;
	}

	mpeg4 = kzalloc_obj(*mpeg4);
	if (!mpeg4)
		return -ENOMEM;

	mpeg4->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						    &mpeg4->workspace_paddr,
						    GFP_KERNEL);
	if (!mpeg4->workspace_vaddr) {
		dev_err(core->dev, "Failed to request MPEG-4 workspace\n");
		ret = -ENOMEM;
		goto free_mpeg4;
	}

	/*
	 * AV_SCRATCH_F is an unsigned offset added to a fixed base, so a
	 * workspace below that base would wrap and send the microcode
	 * somewhere arbitrary.  Refuse instead.
	 */
	if (mpeg4->workspace_paddr < DCAC_BUFF_START_IP) {
		dev_err(core->dev,
			"MPEG-4: workspace at %pad is below the microcode's base %#x\n",
			&mpeg4->workspace_paddr, DCAC_BUFF_START_IP);
		ret = -ERANGE;
		goto free_workspace;
	}
	buf_offset = mpeg4->workspace_paddr - DCAC_BUFF_START_IP;

	/* Buffers 0..3 in AV_SCRATCH_0..3, buffers 4..7 in AV_SCRATCH_G..J */
	ret = amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_0, AV_SCRATCH_G, 0 },
					(u32[]){ 4, 4, 0 });
	if (ret)
		goto free_workspace;

	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));

	/* Tell the microcode where its scratch region actually is */
	amvdec_write_dos(core, MREG_BUF_OFFSET, buf_offset);

	/* disable PSCALE for hardware sharing */
	amvdec_write_dos(core, PSCALE_CTRL, 0);

	amvdec_write_dos(core, MP4_NOT_CODED_CNT, 0);
	amvdec_write_dos(core, MREG_BUFFERIN, 0);
	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	amvdec_write_dos(core, MREG_FATAL_ERROR, 0);

	amvdec_write_dos(core, MDEC_PIC_DC_THRESH, 0x404038aa);
	amvdec_write_dos(core, MP4_PIC_WH,
			 (sess->width << 16) | sess->height);
	amvdec_write_dos(core, MP4_SYS_RATE, 0);

	sess->keyframe_found = 1;
	sess->priv = mpeg4;

	return 0;

free_workspace:
	dma_free_coherent(core->dev, SIZE_WORKSPACE, mpeg4->workspace_vaddr,
			  mpeg4->workspace_paddr);
free_mpeg4:
	kfree(mpeg4);

	return ret;
}

static int codec_mpeg4_stop(struct amvdec_session *sess)
{
	struct codec_mpeg4 *mpeg4 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (mpeg4 && mpeg4->workspace_vaddr) {
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  mpeg4->workspace_vaddr,
				  mpeg4->workspace_paddr);
		mpeg4->workspace_vaddr = NULL;
	}

	return 0;
}

static irqreturn_t codec_mpeg4_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;
	u32 offset;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);

	if (amvdec_read_dos(core, MREG_FATAL_ERROR) == 1) {
		dev_err(core->dev, "MPEG-4 fatal error\n");
		amvdec_abort(sess);
		return IRQ_HANDLED;
	}

	reg = amvdec_read_dos(core, MREG_BUFFEROUT);
	if (!reg)
		return IRQ_HANDLED;

	buffer_index = reg & 0x7;
	if (reg & INTERLACE_FLAG)
		field = V4L2_FIELD_INTERLACED_TB;

	offset = amvdec_read_dos(core, MP4_OFFSET_REG);
	amvdec_dst_buf_done_idx(sess, buffer_index, offset, field);

	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	return IRQ_HANDLED;
}

static irqreturn_t codec_mpeg4_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_mpeg4_ops = {
	.start = codec_mpeg4_start,
	.stop = codec_mpeg4_stop,
	.isr = codec_mpeg4_isr,
	.threaded_isr = codec_mpeg4_threaded_isr,
	.can_recycle = codec_mpeg4_can_recycle,
	.recycle = codec_mpeg4_recycle,
	.eos_sequence = codec_mpeg4_eos_sequence,
};
