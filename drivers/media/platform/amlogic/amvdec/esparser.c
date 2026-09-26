// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Elementary-stream input and request scheduling.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cleanup.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-mem2mem.h>

#include "dos_regs.h"
#include "esparser.h"
#include "amvdec_hevc.h"
#include "amvdec_helpers.h"
#include "codec_mpeg12.h"
#include "codec_mpeg12_synth.h"

/* Zero lookahead appended after each HEVC/VP9 request payload; not part of
 * the coded endpoint.
 */
#define REQUEST_TAIL_SIZE	SZ_4K

/* PARSER REGS (CBUS) */
#define PARSER_CONTROL 0x00
	#define ES_PACK_SIZE_BIT	8
	#define ES_WRITE		BIT(5)
	#define ES_SEARCH		BIT(1)
	#define ES_PARSER_START		BIT(0)
#define PARSER_FETCH_ADDR	0x4
#define PARSER_FETCH_CMD	0x8
#define PARSER_CONFIG 0x14
	#define PS_CFG_MAX_FETCH_CYCLE_BIT	0
	#define PS_CFG_STARTCODE_WID_24_BIT	10
	#define PS_CFG_MAX_ES_WR_CYCLE_BIT	12
	#define PS_CFG_PFIFO_EMPTY_CNT_BIT	16
#define PFIFO_WR_PTR 0x18
#define PFIFO_RD_PTR 0x1c
#define PARSER_SEARCH_PATTERN 0x24
	#define ES_START_CODE_PATTERN 0x00000100
#define PARSER_SEARCH_MASK 0x28
	#define ES_START_CODE_MASK	0xffffff00
	#define FETCH_ENDIAN_BIT	27
#define PARSER_INT_ENABLE	0x2c
	#define PARSER_INT_HOST_EN_BIT	8
#define PARSER_INT_STATUS	0x30
	#define PARSER_INTSTAT_SC_FOUND	1
#define PARSER_ES_CONTROL	0x5c
#define PARSER_VIDEO_START_PTR	0x80
#define PARSER_VIDEO_END_PTR	0x84
#define PARSER_VIDEO_WP		0x88
#define PARSER_VIDEO_HOLE	0x90

#define VP9_HEADER_SIZE		16

static irqreturn_t esparser_isr(int irq, void *dev)
{
	int int_status;
	struct amvdec_core *core = dev;

	int_status = meson_amvdec_read_parser(core, PARSER_INT_STATUS);
	meson_amvdec_write_parser(core, PARSER_INT_STATUS, int_status);

	if (int_status & PARSER_INTSTAT_SC_FOUND) {
		meson_amvdec_write_parser(core, PFIFO_RD_PTR, 0);
		meson_amvdec_write_parser(core, PFIFO_WR_PTR, 0);
		complete(&core->esparser_completion);
	}

	return IRQ_HANDLED;
}

static int
esparser_write_data(struct amvdec_core *core, dma_addr_t addr, u32 size)
{
	reinit_completion(&core->esparser_completion);
	/* Order the reset before the relaxed MMIO starts the parser. */
	wmb();

	meson_amvdec_write_parser(core, PFIFO_RD_PTR, 0);
	meson_amvdec_write_parser(core, PFIFO_WR_PTR, 0);
	meson_amvdec_write_parser(core, PARSER_CONTROL,
			    ES_WRITE |
			    ES_PARSER_START |
			    ES_SEARCH |
			    (size << ES_PACK_SIZE_BIT));

	meson_amvdec_write_parser(core, PARSER_FETCH_ADDR, addr);
	meson_amvdec_write_parser(core, PARSER_FETCH_CMD,
			    (7 << FETCH_ENDIAN_BIT) |
			    (size + ESPARSER_SEARCH_PATTERN_SIZE));

	return wait_for_completion_timeout(&core->esparser_completion, HZ / 5);
}

static u32 esparser_vififo_get_free_space(struct amvdec_session *sess)
{
	u32 vififo_usage;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;
	struct amvdec_core *core = sess->core;

	vififo_usage  = vdec_ops->vififo_level(sess);
	vififo_usage += meson_amvdec_read_parser(core, PARSER_VIDEO_HOLE);
	vififo_usage += (6 * SZ_1K); // 6 KiB internal fifo

	dev_dbg(core->dev,
		"fifo sess=%p usage=%u size=%u offset=%u wraps=%u\n",
		sess, vififo_usage, sess->vififo_size,
		sess->last_offset, sess->wrap_count);

	if (vififo_usage > sess->vififo_size) {
		dev_warn_ratelimited(sess->core->dev,
				     "VIFIFO usage (%u) > VIFIFO size (%u)\n",
			 vififo_usage, sess->vififo_size);
		return 0;
	}

	return sess->vififo_size - vififo_usage;
}

static u32 esparser_get_offset(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 offset = meson_amvdec_read_parser(core, PARSER_VIDEO_WP) -
		     sess->vififo_paddr;

	if (offset < sess->last_offset)
		sess->wrap_count++;

	sess->last_offset = offset;
	offset += (sess->wrap_count * sess->vififo_size);

	return offset;
}

/*
 * A prefeed refusal completes this job without poisoning either vb2 queue.
 * The ready destination belongs to this scheduled job, unlike earlier DONE
 * buffers on vb2's completion list. No destination has been selected by HW.
 */
static void esparser_src_error(struct amvdec_session *sess,
			       struct vb2_v4l2_buffer *src)
{
	if (meson_amvdec_request_jobs(sess) && sess->request_job.src == src) {
		if (sess->request_job.credit) {
			/* Hardware may have consumed input: ownership ends at stop. */
			sess->request_job.error = true;
			meson_amvdec_abort(sess);
			return;
		}
		meson_amvdec_request_refuse(sess);
		return;
	}
	/* Also completes the OUTPUT's media-request control-handler object. */
	meson_amvdec_src_buf_done(sess, src, VB2_BUF_STATE_ERROR);

}

static int
esparser_queue(struct amvdec_session *sess, struct vb2_v4l2_buffer *vbuf)
{
	int ret;
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	struct amvdec_core *core = sess->core;
	u32 payload_size = vb2_get_plane_payload(vb, 0);
	u32 original_payload_size = payload_size;
	dma_addr_t phy = vb2_dma_contig_plane_dma_addr(vb, 0);
	struct amvdec_feed_scratch *f = &sess->feed;
	struct v4l2_ctrl_vp9_frame *vp9_frame_ptr = NULL;
	struct v4l2_ctrl_hevc_sps *hevc_sps_ptr = NULL;
	struct v4l2_ctrl_hevc_pps *hevc_pps_ptr = NULL;
	struct v4l2_ctrl_hevc_slice_params *hevc_slice_ptr = NULL;
	struct v4l2_ctrl_hevc_decode_params *hevc_decode_ptr = NULL;

	u8 *hevc_headers __free(kfree) = NULL;
	u32 hevc_headers_len = 0;
	u32 offset;
	u32 pad_size;

	u16 mpeg2_tref = 0;
	bool mpeg2_second = false;
	bool mpeg2_job = false;

	BUILD_BUG_ON(sizeof(f->headers) < MPEG12_SYNTH_MAX_HEADERS);

	dev_dbg(core->dev, "feed attempt sess=%p idx=%u bytes=%u\n",
		sess, vb->index, payload_size);

	/*
	 * Decide whether this buffer can be fed BEFORE reading its request's
	 * controls: applying a request stashes its per-picture parameters,
	 * so examining a buffer that is then deferred would overwrite the
	 * parameters of the picture currently being decoded.
	 */

	if (meson_amvdec_request_jobs(sess) &&
	    (!READ_ONCE(sess->m2m_run_armed) || atomic_read(&sess->request_job.state)))
		return -EAGAIN;

	if (esparser_vififo_get_free_space(sess) < payload_size)
		return -EAGAIN;

	if (meson_amvdec_request_jobs(sess) && meson_amvdec_request_begin(sess, vbuf))
		return -EAGAIN;
	v4l2_m2m_src_buf_remove_by_buf(sess->m2m_ctx, vbuf);

	offset = esparser_get_offset(sess);

	struct media_request *req = vb->req_obj.req;
	struct v4l2_ctrl *ctrl;

	if (!req) {
		dev_err_ratelimited(core->dev, "stateless buffer has no request\n");
		esparser_src_error(sess, vbuf);
		return -EINVAL;
	}

	ret = v4l2_ctrl_request_setup(req, &sess->ctrl_handler);
	if (ret) {
		dev_dbg(core->dev, "request setup failed: %d\n", ret);
		esparser_src_error(sess, vbuf);
		return ret;
	}

	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_MPEG2_SLICE) {
		u8 *vaddr = vb2_plane_vaddr(vb, 0);
		u32 plane_size = vb2_plane_size(vb, 0);
		bool have_quant = false;
		int hdrs_len;

		ctrl = v4l2_ctrl_find(&sess->ctrl_handler,
				      V4L2_CID_STATELESS_MPEG2_SEQUENCE);
		if (!ctrl || !ctrl->p_cur.p_mpeg2_sequence) {
			dev_err_ratelimited(core->dev, "MPEG2 SEQUENCE control missing\n");
			esparser_src_error(sess, vbuf);
			return -EINVAL;
		}
		f->mpeg2_seq = *ctrl->p_cur.p_mpeg2_sequence;
		/*
		 * The canvases and CAPTURE planes are sized from the
		 * negotiated format; a larger picture would be written
		 * past them.  Compare in macroblocks, as for H.264.
		 */
		if (DIV_ROUND_UP(f->mpeg2_seq.horizontal_size, 16) >
		    DIV_ROUND_UP(sess->width, 16) ||
		    DIV_ROUND_UP(f->mpeg2_seq.vertical_size, 16) >
		    DIV_ROUND_UP(sess->height, 16)) {
			dev_err_ratelimited(core->dev,
					    "MPEG2 sequence size %ux%u exceeds negotiated %ux%u\n",
				f->mpeg2_seq.horizontal_size,
				f->mpeg2_seq.vertical_size,
				sess->width, sess->height);
			esparser_src_error(sess, vbuf);
			return -EINVAL;
		}

		ctrl = v4l2_ctrl_find(&sess->ctrl_handler,
				      V4L2_CID_STATELESS_MPEG2_PICTURE);
		if (!ctrl || !ctrl->p_cur.p_mpeg2_picture) {
			dev_err_ratelimited(core->dev, "MPEG2 PICTURE control missing\n");
			esparser_src_error(sess, vbuf);
			return -EINVAL;
		}
		f->mpeg2_pic = *ctrl->p_cur.p_mpeg2_picture;
		f->mpeg2_job_pic = f->mpeg2_pic;
		mpeg2_job = true;

		ctrl = v4l2_ctrl_find(&sess->ctrl_handler,
				      V4L2_CID_STATELESS_MPEG2_QUANTISATION);
		if (ctrl && ctrl->p_cur.p_mpeg2_quantisation) {
			f->mpeg2_quant = *ctrl->p_cur.p_mpeg2_quantisation;
			have_quant = true;
		}

		/*
		 * Prepend a sequence header except between paired fields, which share
		 * the same temporal reference.
		 */
		mpeg2_tref = sess->sequence_out;
		mpeg2_second =
			meson_amvdec_codec_mpeg12_sl_second_field(sess, &f->mpeg2_pic,
								  &mpeg2_tref);
		hdrs_len = mpeg12_synth_headers(&f->mpeg2_seq, &f->mpeg2_pic,
						have_quant ? &f->mpeg2_quant : NULL,
						mpeg2_tref, !mpeg2_second,
						f->headers, sizeof(f->headers));
		if (hdrs_len < 0) {
			dev_err_ratelimited(core->dev,
					    "MPEG2 header synthesis rejected controls: %d\n",
				hdrs_len);
			esparser_src_error(sess, vbuf);
			return hdrs_len;
		}
		if (!vaddr || payload_size + hdrs_len > plane_size) {
			dev_err_ratelimited(core->dev,
					    "MPEG2 buffer too small for synthesized headers\n");
			esparser_src_error(sess, vbuf);
			return -ENOSPC;
		}
		memmove(vaddr + hdrs_len, vaddr, payload_size);
		memcpy(vaddr, f->headers, hdrs_len);
		payload_size += hdrs_len;
		vb2_set_plane_payload(vb, 0, payload_size);
		dev_dbg(core->dev,
			"MPEG2 synthesized %d header bytes, coding_type=%u\n",
			hdrs_len, f->mpeg2_pic.picture_coding_type);
	}

	ret = meson_amvdec_add_ts(sess, vb->timestamp, vbuf->timecode, offset,
			    vbuf->flags, vp9_frame_ptr, hevc_sps_ptr, hevc_pps_ptr,
			    hevc_slice_ptr, hevc_decode_ptr);
	if (ret) {
		esparser_src_error(sess, vbuf);
		return ret;
	}

	dev_dbg(core->dev, "esparser: ts = %llu pld_size = %u offset = %08X flags = %08X\n",
		vb->timestamp, payload_size, offset, vbuf->flags);

	vbuf->flags = 0;
	vbuf->field = V4L2_FIELD_NONE;
	vbuf->sequence = sess->sequence_out++;

	/*
	 * The multi blob takes the OUTPUT buffer as its stream FIFO directly:
	 * no ESPARSER copy, no start-code padding, and the buffer stays owned
	 * by the hardware until the picture completes.
	 */

	unsigned int i, n = 0;
	u64 refs[16];

	if (hevc_decode_ptr) {
		n = f->hevc_decode.num_active_dpb_entries;
		if (n > ARRAY_SIZE(refs)) {
			esparser_src_error(sess, vbuf);
			return -EINVAL;
		}
		for (i = 0; i < n; i++)
			refs[i] = f->hevc_decode.dpb[i].timestamp;
	} else if (mpeg2_job) {
		if (f->mpeg2_job_pic.picture_coding_type !=
		    V4L2_MPEG2_PIC_CODING_TYPE_I)
			refs[n++] = f->mpeg2_job_pic.forward_ref_ts;
		if (f->mpeg2_job_pic.picture_coding_type ==
		    V4L2_MPEG2_PIC_CODING_TYPE_B)
			refs[n++] = f->mpeg2_job_pic.backward_ref_ts;
	} else if (vp9_frame_ptr && !(f->vp9_frame.flags &
		   (V4L2_VP9_FRAME_FLAG_KEY_FRAME | V4L2_VP9_FRAME_FLAG_INTRA_ONLY))) {
		n = 3;
		refs[0] = f->vp9_frame.last_frame_ts;
		refs[1] = f->vp9_frame.golden_frame_ts;
		refs[2] = f->vp9_frame.alt_frame_ts;
	}
	for (i = 0; i < n; i++) {
		if (meson_amvdec_request_resolve_ref(sess, i, refs[i])) {
			/* The codec checks the entries the picture uses. */
			if (hevc_decode_ptr) {
				sess->request_job.refs[i] = NULL;
				sess->request_job.ref_ts[i] = refs[i];
				continue;
			}
			/* A P field may have only its mate to predict from. */
			if (mpeg2_second && f->mpeg2_job_pic.picture_coding_type ==
			    V4L2_MPEG2_PIC_CODING_TYPE_P)
				continue;
			dev_err(core->dev,
				"prefeed reference %u timestamp %llu missing/current\n",
				i, refs[i]);
			esparser_src_error(sess, vbuf);
			return -EINVAL;
		}
	}
	if (mpeg2_job) {
		/* The VLD reads this buffer directly; nothing goes
		 * through the parser FIFO.
		 */
		sess->request_job.credit = true;
		atomic_inc(&sess->esparser_queued_bufs);
		ret = meson_amvdec_codec_mpeg12_sl_feed(sess, vb, &f->mpeg2_job_pic,
							mpeg2_tref);
		if (ret) {
			sess->request_job.credit = false;
			atomic_dec(&sess->esparser_queued_bufs);
			esparser_src_error(sess, vbuf);
			return ret;
		}
		return 0;
	}
	/* Record the framed/synthesized coded endpoint before padding or DMA.
	 * Reading PARSER_VIDEO_WP for offset includes previous transport bytes.
	 */

	u32 tail = REQUEST_TAIL_SIZE;
	u32 plane_size = vb2_plane_size(vb, 0);
	u8 *data = vb2_plane_vaddr(vb, 0);

	size_t needed = (size_t)payload_size + tail + ESPARSER_SEARCH_PATTERN_SIZE;

	if (!data || payload_size > plane_size ||
	    esparser_vififo_get_free_space(sess) < payload_size + tail) {
		esparser_src_error(sess, vbuf);
		return -EINVAL;
	}
	/* The transport tail belongs to the driver, not the client's
	 * negotiated OUTPUT padding. Previous DMA has completed before
	 * another request can resize or reuse this storage.
	 */
	if (sess->request_job.input_size < needed) {
		void *new_data;
		dma_addr_t new_phy;

		new_data = dma_alloc_coherent(core->dev, needed, &new_phy,
					      GFP_KERNEL);
		if (!new_data) {
			esparser_src_error(sess, vbuf);
			return -ENOMEM;
		}
		if (sess->request_job.input_vaddr)
			dma_free_coherent(core->dev, sess->request_job.input_size,
					  sess->request_job.input_vaddr,
					sess->request_job.input_paddr);
		sess->request_job.input_vaddr = new_data;
		sess->request_job.input_paddr = new_phy;
		sess->request_job.input_size = needed;
	}
	memcpy(sess->request_job.input_vaddr, data, payload_size);
	data = sess->request_job.input_vaddr;
	phy = sess->request_job.input_paddr;
	memset(data + payload_size, 0, tail + ESPARSER_SEARCH_PATTERN_SIZE);
	data[payload_size + tail + 2] = 1;
	data[payload_size + tail + 3] = 0xff;
	pad_size = tail;
	dev_dbg(core->dev,
		"feed mode=%08x idx=%u ts=%llu original=%u headers=%u payload=%u pad=%u write=%u fetch=%u offset=%u free=%u queued=%d\n",
		 sess->fmt_out->pixfmt, vb->index, vb->timestamp,
		 original_payload_size, hevc_headers_len,
		 payload_size, pad_size,
		 payload_size + pad_size, payload_size + pad_size + ESPARSER_SEARCH_PATTERN_SIZE,
		 offset, esparser_vififo_get_free_space(sess),
		 atomic_read(&sess->esparser_queued_bufs));
	sess->request_job.credit = true;
	atomic_inc(&sess->esparser_queued_bufs);
	meson_amvdec_trace(sess, AMVDEC_TR_FEED, offset, payload_size + pad_size,
		     (u32)(vb->timestamp / 1000));
	ret = esparser_write_data(core, phy, payload_size + pad_size);
	meson_amvdec_trace(sess, AMVDEC_TR_FED, ret,
		     meson_amvdec_read_parser(core, PARSER_VIDEO_WP), 0);

	if (ret > 0) {
		/* Only a successful feed establishes the firmware's cached headers. */
		if (hevc_headers_len) {
			memcpy(sess->hevc_cached_headers, hevc_headers, hevc_headers_len);
			sess->hevc_cached_headers_len = hevc_headers_len;
			sess->hevc_headers_valid = true;
			dev_dbg(core->dev, "HEVC parameter sets sent ts=%llu bytes=%u\n",
				vb->timestamp, hevc_headers_len);
		}
	}

	if (ret <= 0) {
		dev_warn_ratelimited(core->dev, "esparser: input parsing error\n");
		meson_amvdec_remove_ts(sess, vb->timestamp);
		esparser_src_error(sess, vbuf);
		meson_amvdec_write_parser(core, PARSER_FETCH_CMD, 0);

		return 0;
	}

	dev_dbg(core->dev,
		"feed done mode=%08x idx=%u ts=%llu payload=%u parser_ret=%d queued_before_inc=%d\n",
		 sess->fmt_out->pixfmt, vb->index, vb->timestamp, payload_size, ret,
		 atomic_read(&sess->esparser_queued_bufs));

	return 0;
}

void meson_amvdec_esparser_queue_all_src(struct work_struct *work)
{
	struct v4l2_m2m_buffer *buf, *n;
	struct amvdec_session *sess =
		container_of(work, struct amvdec_session, esparser_queue_work);
	u32 fed = 0;
	int ret = 0;

	mutex_lock(&sess->lock);
	meson_amvdec_trace(sess, AMVDEC_TR_WORK, READ_ONCE(sess->m2m_run_armed),
		     v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx), 0);
	dev_dbg(sess->core->dev, "worker begin src=%u queued=%d\n",
		v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx),
		atomic_read(&sess->esparser_queued_bufs));
	if (meson_amvdec_request_jobs(sess))
		meson_amvdec_request_retire(sess, false);
	/* Retire a finished picture before feeding the next one. */

	/*
	 * The stream position counter must not reach 0x80000000. Restart the
	 * core here, before anything is fed: the firmware is parked after a
	 * completed picture and the VIFIFO is empty, so nothing is lost.
	 * VP9 restarts at a key frame instead: see esparser_queue().
	 */

	v4l2_m2m_for_each_src_buf_safe(sess->m2m_ctx, buf, n) {
		if (sess->should_stop)
			break;

		ret = esparser_queue(sess, &buf->vb);
		if (ret < 0)
			break;
		fed++;
		if (meson_amvdec_request_jobs(sess))
			break;
		/*
		 * One access unit per decode: the buffer is already off the
		 * ready queue (esparser_queue() dequeues what it feeds), and
		 * the VLD is pointed at it until the picture completes.
		 */

	}
	dev_dbg(sess->core->dev,
		"worker end fed=%u ret=%d src=%u queued=%d dst=%u\n",
		fed, ret, v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx),
		atomic_read(&sess->esparser_queued_bufs),
		v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx));
	mutex_unlock(&sess->lock);
}

int meson_amvdec_esparser_power_up(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;

	reset_control_reset(core->esparser_reset);
	meson_amvdec_write_parser(core, PARSER_CONFIG,
			    (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
			    (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
			    (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

	meson_amvdec_write_parser(core, PFIFO_RD_PTR, 0);
	meson_amvdec_write_parser(core, PFIFO_WR_PTR, 0);

	meson_amvdec_write_parser(core, PARSER_SEARCH_PATTERN,
			    ES_START_CODE_PATTERN);
	meson_amvdec_write_parser(core, PARSER_SEARCH_MASK, ES_START_CODE_MASK);

	meson_amvdec_write_parser(core, PARSER_CONFIG,
			    (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
			    (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
			    (16 << PS_CFG_MAX_FETCH_CYCLE_BIT) |
			    (2  << PS_CFG_STARTCODE_WID_24_BIT));

	meson_amvdec_write_parser(core, PARSER_CONTROL,
			    (ES_SEARCH | ES_PARSER_START));

	meson_amvdec_write_parser(core, PARSER_VIDEO_START_PTR, sess->vififo_paddr);
	meson_amvdec_write_parser(core, PARSER_VIDEO_END_PTR,
			    sess->vififo_paddr + sess->vififo_size - 8);
	meson_amvdec_write_parser(core, PARSER_ES_CONTROL,
			    meson_amvdec_read_parser(core, PARSER_ES_CONTROL) & ~1);

	if (vdec_ops->conf_esparser)
		vdec_ops->conf_esparser(sess);

	meson_amvdec_write_parser(core, PARSER_INT_STATUS, 0xffff);
	meson_amvdec_write_parser(core, PARSER_INT_ENABLE,
			    BIT(PARSER_INT_HOST_EN_BIT));

	return 0;
}

/*
 * Cancel parser DMA, clear stream control, wait for idle and reset the
 * parser before gating its clock. Outstanding transactions must not cross
 * a power transition.
 */
void meson_amvdec_esparser_quiesce(struct amvdec_core *core)
{
	int i;

	meson_amvdec_write_parser(core, PARSER_FETCH_CMD, 0);
	meson_amvdec_write_parser(core, PARSER_CONTROL, 0);

	for (i = 0; i < 100; i++) {
		if (!(meson_amvdec_read_parser(core, PARSER_ES_CONTROL) & BIT(19)))
			break;
		udelay(10);
	}
	if (i == 100)
		dev_warn_ratelimited(core->dev, "parser ES request still pending\n");

	meson_amvdec_write_parser(core, PARSER_INT_ENABLE, 0);
	meson_amvdec_write_parser(core, PARSER_INT_STATUS, 0xffff);
	meson_amvdec_write_parser(core, PARSER_VIDEO_HOLE, 0);

	reset_control_reset(core->esparser_reset);
}

int meson_amvdec_esparser_init(struct platform_device *pdev, struct amvdec_core *core)
{
	struct device *dev = &pdev->dev;
	int ret;
	int irq;

	irq = platform_get_irq_byname(pdev, "esparser");
	if (irq < 0)
		return irq;

	init_completion(&core->esparser_completion);

	ret = devm_request_irq(dev, irq, esparser_isr, IRQF_SHARED,
			       "esparserirq", core);
	if (ret) {
		dev_err(dev, "Failed requesting ESPARSER IRQ\n");
		return ret;
	}

	core->esparser_reset =
		devm_reset_control_get_exclusive(dev, "esparser");
	if (IS_ERR(core->esparser_reset)) {
		dev_err(dev, "Failed to get esparser_reset\n");
		return PTR_ERR(core->esparser_reset);
	}

	return 0;
}
