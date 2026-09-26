// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * MPEG-2 request decoding with multi-instance firmware.
 */

#include <linux/delay.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_mpeg12.h"
#include "dos_regs.h"
#include "amvdec_helpers.h"

#define SIZE_WORKSPACE		SZ_128K

/* map firmware registers to known MPEG1/2 functions */
#define MREG_SEQ_INFO		AV_SCRATCH_4
#define MREG_PIC_INFO		AV_SCRATCH_5
#define MREG_PIC_WIDTH		AV_SCRATCH_6
#define MREG_PIC_HEIGHT		AV_SCRATCH_7
#define MREG_BUFFEROUT		AV_SCRATCH_9
#define MREG_CMD		AV_SCRATCH_A
#define MREG_CO_MV_START	AV_SCRATCH_B
#define MREG_ERROR_COUNT	AV_SCRATCH_C
#define MREG_WAIT_BUFFER	AV_SCRATCH_E
#define MREG_FATAL_ERROR	AV_SCRATCH_F

static irqreturn_t codec_mpeg12_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

/*
 * MPEG-2 multi-instance firmware takes destination and reference buffers
 * from each request. The host prepends sequence and picture headers.
 */
#define MPEG12_SL_WORKSPACE_SIZE	(4 * SZ_64K)
#define MPEG12_SL_CCBUF_SIZE		SZ_64K
#define MPEG12_SL_PADDING		SZ_1K
#define MPEG12_SL_FIFO_ALIGN		8

#define MREG_CC_ADDR		AV_SCRATCH_0
#define MREG_REF0		AV_SCRATCH_2
#define MREG_REF1		AV_SCRATCH_3
#define MREG_INPUT		AV_SCRATCH_8

#define MPEG12_PIC_DONE		1
#define MPEG12_DATA_REQUEST	4
#define PICINFO_PAIR_ERRORS	0xc0000000

struct codec_mpeg12_sl {
	void	  *workspace_vaddr;
	dma_addr_t workspace_paddr;
	void	  *ccbuf_vaddr;
	dma_addr_t ccbuf_paddr;
	u32 canvas_spec[VB2_MAX_FRAME];

	/*
	 * A field picture is half a firmware picture.  After the first field
	 * the firmware parks asking for more data, and the second field's
	 * request - same CAPTURE buffer, opposite parity - resumes it.
	 */
	bool cur_first_field;
	u32 cur_structure;
	u16 cur_temporal_ref;
	bool pair_pending;
	u32 pair_dst;
	u32 pair_structure;
	u16 pair_temporal_ref;
};

static int codec_mpeg12_sl_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_mpeg12_sl *m;

	m = kzalloc_obj(*m);
	if (!m)
		return -ENOMEM;
	m->workspace_vaddr = dma_alloc_coherent(core->dev, MPEG12_SL_WORKSPACE_SIZE,
						&m->workspace_paddr, GFP_KERNEL);
	m->ccbuf_vaddr = dma_alloc_coherent(core->dev, MPEG12_SL_CCBUF_SIZE,
					    &m->ccbuf_paddr, GFP_KERNEL);
	if (!m->workspace_vaddr || !m->ccbuf_vaddr) {
		if (m->workspace_vaddr)
			dma_free_coherent(core->dev, MPEG12_SL_WORKSPACE_SIZE,
					  m->workspace_vaddr, m->workspace_paddr);
		if (m->ccbuf_vaddr)
			dma_free_coherent(core->dev, MPEG12_SL_CCBUF_SIZE,
					  m->ccbuf_vaddr, m->ccbuf_paddr);
		kfree(m);
		return -ENOMEM;
	}
	memset(m->workspace_vaddr, 0, MPEG12_SL_WORKSPACE_SIZE);
	memset(m->ccbuf_vaddr, 0, MPEG12_SL_CCBUF_SIZE);

	sess->priv = m;
	return 0;
}

static int codec_mpeg12_sl_stop(struct amvdec_session *sess)
{
	struct codec_mpeg12_sl *m = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!m)
		return 0;
	dma_free_coherent(core->dev, MPEG12_SL_WORKSPACE_SIZE,
			  m->workspace_vaddr, m->workspace_paddr);
	dma_free_coherent(core->dev, MPEG12_SL_CCBUF_SIZE,
			  m->ccbuf_vaddr, m->ccbuf_paddr);
	return 0;
}

static int codec_mpeg12_sl_canvas(struct amvdec_session *sess,
				  struct vb2_buffer *vb, u32 *spec)
{
	struct codec_mpeg12_sl *m = sess->priv;
	int ret;

	if (vb->index >= VB2_MAX_FRAME)
		return -EINVAL;
	ret = meson_amvdec_canvas_nv12m_spec(sess, vb, ALIGN(sess->width, 64),
				       ALIGN(sess->height, 32),
				       &m->canvas_spec[vb->index]);
	if (!ret)
		*spec = m->canvas_spec[vb->index];
	return ret;
}

/*
 * Match the second field to the open pair and reuse its temporal reference
 * without inserting a sequence header.
 */
bool meson_amvdec_codec_mpeg12_sl_second_field(struct amvdec_session *sess,
				  const struct v4l2_ctrl_mpeg2_picture *pic,
				  u16 *temporal_ref)
{
	struct codec_mpeg12_sl *m = sess->priv;

	if (!m || !m->pair_pending || !sess->request_job.dst ||
	    pic->picture_structure == V4L2_MPEG2_PIC_FRAME ||
	    pic->picture_structure == m->pair_structure ||
	    sess->request_job.dst->vb2_buf.index != m->pair_dst)
		return false;
	*temporal_ref = m->pair_temporal_ref;
	return true;
}

static void codec_mpeg12_sl_input(struct amvdec_core *core, dma_addr_t src,
				  u32 fifo_size, u32 payload)
{
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_START_PTR, src);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_END_PTR,
			 src + fifo_size - MPEG12_SL_FIFO_ALIGN);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CURR_PTR, src);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 1);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 2);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_RP, src);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_WP,
			 round_down(src + payload + MPEG12_SL_PADDING,
				    MPEG12_SL_FIFO_ALIGN));
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 3);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 2);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL,
			 (0x11 << 16) | BIT(10) | (7 << 3));
	meson_amvdec_write_dos(core, VIFF_BIT_CNT, payload * 8);
}

/*
 * Decode one picture.  @vb already starts with the synthesized headers.  The
 * destination and the resolved references are those of the request job.
 */
int meson_amvdec_codec_mpeg12_sl_feed(struct amvdec_session *sess, struct vb2_buffer *vb,
			 const struct v4l2_ctrl_mpeg2_picture *pic,
			 u16 temporal_ref)
{
	struct codec_mpeg12_sl *m = sess->priv;
	struct amvdec_core *core = sess->core;
	dma_addr_t src = vb2_dma_contig_plane_dma_addr(vb, 0);
	u32 payload = vb2_get_plane_payload(vb, 0);
	u32 fifo_size = round_down(vb2_plane_size(vb, 0), MPEG12_SL_FIFO_ALIGN);
	u8 *data = vb2_plane_vaddr(vb, 0);
	struct vb2_buffer *dst, *fwd, *bwd;
	bool field = pic->picture_structure != V4L2_MPEG2_PIC_FRAME;
	u32 rec, ref0, ref1;
	bool resume;
	u16 tref;
	int i, ret;

	if (!m || !data || !sess->request_job.dst)
		return -EINVAL;
	if (!payload || fifo_size <= MPEG12_SL_PADDING ||
	    payload >= fifo_size - MPEG12_SL_PADDING ||
	    !IS_ALIGNED(src, MPEG12_SL_FIFO_ALIGN))
		return -EINVAL;

	dst = &sess->request_job.dst->vb2_buf;
	resume = meson_amvdec_codec_mpeg12_sl_second_field(sess, pic, &tref) &&
		 meson_amvdec_read_dos(core, MREG_BUFFEROUT) == MPEG12_DATA_REQUEST;
	m->pair_pending = false;
	m->cur_first_field = field && !resume;
	m->cur_structure = pic->picture_structure;
	m->cur_temporal_ref = temporal_ref;
	if (resume) {
		/*
		 * The firmware is parked mid-pair with the references of the
		 * first field.  Hand it the rest of the picture and let go.
		 */
		memset(data + payload, 0, MPEG12_SL_PADDING);
		data[payload + 2] = 1;
		dma_sync_single_for_device(core->dev, src,
					   payload + MPEG12_SL_PADDING,
					   DMA_TO_DEVICE);
		meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
		codec_mpeg12_sl_input(core, src, fifo_size, payload);
		meson_amvdec_write_dos(core, AV_SCRATCH_L, 0);
		meson_amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 0x6);
		dev_dbg(core->dev, "mpeg2 stateless: resume second field payload=%u dst=%u\n",
			payload, dst->index);
		dma_wmb();
		meson_amvdec_write_dos(core, MREG_BUFFEROUT, 0);
		return 0;
	}

	fwd = sess->request_job.refs[0];
	bwd = sess->request_job.refs[1];
	ret = codec_mpeg12_sl_canvas(sess, dst, &rec);
	if (ret)
		return ret;
	switch (pic->picture_coding_type) {
	case V4L2_MPEG2_PIC_CODING_TYPE_I:
		/* Unused roles still get a valid, owned surface. */
		ref0 = rec;
		ref1 = rec;
		break;
	case V4L2_MPEG2_PIC_CODING_TYPE_P:
		/*
		 * The P field that completes an I field predicts from its
		 * mate alone and may have no other picture to name.
		 */
		if (!fwd && field) {
			ref0 = rec;
			ref1 = rec;
			break;
		}
		if (!fwd || fwd == dst)
			return -ENOENT;
		ret = codec_mpeg12_sl_canvas(sess, fwd, &ref1);
		if (ret)
			return ret;
		ref0 = ref1;
		break;
	case V4L2_MPEG2_PIC_CODING_TYPE_B:
		if (!fwd || !bwd || fwd == dst || bwd == dst)
			return -ENOENT;
		ret = codec_mpeg12_sl_canvas(sess, fwd, &ref0);
		if (!ret)
			ret = codec_mpeg12_sl_canvas(sess, bwd, &ref1);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	/* The engine reads ahead of the picture: a start code, then zeros. */
	memset(data + payload, 0, MPEG12_SL_PADDING);
	data[payload + 2] = 1;
	dma_sync_single_for_device(core->dev, src, payload + MPEG12_SL_PADDING,
				   DMA_TO_DEVICE);

	/* Halt the processors before touching the VLD; drain their DMA. */
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

	/* Input: the VLD FIFO is the OUTPUT buffer itself. */
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(5) | BIT(4) | BIT(3));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));

	codec_mpeg12_sl_input(core, src, fifo_size, payload);

	/* Fresh context: every header and matrix is in this access unit. */
	meson_amvdec_write_dos(core, MREG_CO_MV_START, m->workspace_paddr);
	meson_amvdec_write_dos(core, MREG_CC_ADDR, m->ccbuf_paddr);
	meson_amvdec_write_dos(core, MREG_REF0, ref0);
	meson_amvdec_write_dos(core, MREG_REF1, ref1);
	meson_amvdec_write_dos(core, REC_CANVAS_ADDR, rec);
	meson_amvdec_write_dos(core, ANC2_CANVAS_ADDR, rec);

	meson_amvdec_write_dos(core, MPEG1_2_REG, 0);
	meson_amvdec_write_dos(core, PSCALE_CTRL, 0);
	meson_amvdec_write_dos(core, PIC_HEAD_INFO, 0x380);
	meson_amvdec_write_dos(core, M4_CONTROL_REG, 0);
	meson_amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	meson_amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	meson_amvdec_write_dos(core, AV_SCRATCH_G, 0);
	meson_amvdec_write_dos(core, ASSIST_MBOX1_MASK, 1);
	meson_amvdec_write_dos(core, MREG_CMD, (sess->width << 16) | sess->height);
	meson_amvdec_write_dos(core, MREG_PIC_WIDTH, 0);
	meson_amvdec_write_dos(core, MREG_PIC_HEIGHT, 0);
	meson_amvdec_write_dos(core, MREG_SEQ_INFO, 0);
	meson_amvdec_write_dos(core, F_CODE_REG, 0);
	meson_amvdec_write_dos(core, SLICE_VER_POS_PIC_TYPE, 0);
	meson_amvdec_write_dos(core, MB_INFO, 0);
	meson_amvdec_write_dos(core, VCOP_CTRL_REG, 0);
	meson_amvdec_write_dos(core, AV_SCRATCH_H, 0);
	meson_amvdec_write_dos(core, MREG_ERROR_COUNT, 0);
	/* bit 0: the CC buffer is the one named above */
	meson_amvdec_write_dos(core, MREG_FATAL_ERROR, 1);
	meson_amvdec_write_dos(core, MREG_WAIT_BUFFER, 0);
	meson_amvdec_write_dos(core, AV_SCRATCH_J, 0);
	meson_amvdec_write_dos(core, AV_SCRATCH_L, 0);
	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M)
		meson_amvdec_write_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));
	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(16));
	/* frame-based input, no leading bytes to skip, no context to restore */
	meson_amvdec_write_dos(core, MREG_INPUT, BIT(7));

	meson_amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 0x6);

	dev_dbg(core->dev,
		"mpeg2 stateless: feed type=%u payload=%u rec=%06x ref0=%06x ref1=%06x dst=%u\n",
		pic->picture_coding_type, payload, rec, ref0, ref1, dst->index);

	/* Order every store above before the processors are released. */
	dma_wmb();
	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);
	meson_amvdec_write_dos(core, MPSR, 1);
	return 0;
}

static irqreturn_t codec_mpeg12_sl_threaded_isr(struct amvdec_session *sess)
{
	struct codec_mpeg12_sl *m = sess->priv;
	struct amvdec_core *core = sess->core;
	u32 reg, info;

	meson_amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);

	/* Sequence-information and user-data notifications only want an ack. */
	if (meson_amvdec_read_dos(core, AV_SCRATCH_G)) {
		meson_amvdec_write_dos(core, AV_SCRATCH_G, 0);
		return IRQ_HANDLED;
	}
	if (meson_amvdec_read_dos(core, AV_SCRATCH_J) & 0x8000)
		meson_amvdec_write_dos(core, AV_SCRATCH_J, 0);

	reg = meson_amvdec_read_dos(core, MREG_BUFFEROUT);
	if (!reg)
		return IRQ_HANDLED;
	info = meson_amvdec_read_dos(core, MREG_PIC_INFO);
	dev_dbg(core->dev, "mpeg2 stateless: irq bufferout=%x pic_info=%08x err=%u fatal=%u\n",
		reg, info, meson_amvdec_read_dos(core, MREG_ERROR_COUNT),
		meson_amvdec_read_dos(core, MREG_FATAL_ERROR));
	if (!sess->request_job.dst)
		return IRQ_HANDLED;
	/*
	 * First field in, the firmware wants the second.  Leave it parked on
	 * that request: clearing BUFFEROUT is what resumes it.
	 */
	if (reg == MPEG12_DATA_REQUEST && m->cur_first_field) {
		m->cur_first_field = false;
		m->pair_pending = true;
		m->pair_dst = sess->request_job.dst->vb2_buf.index;
		m->pair_structure = m->cur_structure;
		m->pair_temporal_ref = m->cur_temporal_ref;
		meson_amvdec_request_signal(sess, sess->request_job.dst);
		return IRQ_HANDLED;
	}
	/* Anything but picture-done is a failed picture, never a clean one. */
	if (reg != MPEG12_PIC_DONE || (info & PICINFO_PAIR_ERRORS)) {
		dev_err_ratelimited(core->dev,
				    "MPEG-2 picture failed: status %u pic_info %08x\n",
				    reg, info);
		sess->request_job.error = true;
	}
	meson_amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	meson_amvdec_request_signal(sess, sess->request_job.dst);
	return IRQ_HANDLED;
}

struct amvdec_codec_ops meson_amvdec_codec_mpeg12_sl_ops = {
	.start = codec_mpeg12_sl_start,
	.stop = codec_mpeg12_sl_stop,
	.isr = codec_mpeg12_isr,
	.threaded_isr = codec_mpeg12_sl_threaded_isr,
};
