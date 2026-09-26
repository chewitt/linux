// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * V4L2 request decoder core.
 */

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-dma-contig.h>

#include "amvdec.h"
#include "hevc_regs.h"
#include "codec_hevc_common.h"
#include "codec_hevc.h"
#include "codec_hevc_synth.h"
#include "codec_h264_synth.h"
#include "codec_mpeg12_synth.h"
#include "esparser.h"
#include "amvdec_helpers.h"

/* Stuck-request report: see vdec_job_watch() */
static unsigned int job_watch_ms = 1000;
module_param(job_watch_ms, uint, 0644);
MODULE_PARM_DESC(job_watch_ms,
		 "Report a request that has not completed after this long (0 = off)");

struct dummy_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

/* 16 MiB for parsed bitstream swap exchange */
#define SIZE_VIFIFO SZ_16M

/*
 * Canvas strides are 64-byte aligned for VDEC writes and VD1 scanout.
 * Plane heights are 32-row aligned for macroblock rows and field pairs.
 */
static u32 get_output_size(u32 width, u32 height)
{
	return ALIGN(ALIGN(width, 64) * ALIGN(height, 32), SZ_64K);
}

u32 meson_amvdec_get_output_size(struct amvdec_session *sess)
{
	return get_output_size(sess->width, sess->height);
}

static bool vdec_needs_parser_vififo(struct amvdec_session *sess)
{
	return !sess->fmt_out->codec_ops->direct_input;
}

static int vdec_poweron(struct amvdec_session *sess)
{
	int ret;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;

	ret = clk_prepare_enable(sess->core->dos_parser_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(sess->core->dos_clk);
	if (ret)
		goto disable_dos_parser;

	ret = vdec_ops->start(sess);
	if (ret)
		goto disable_dos;

	if (vdec_needs_parser_vififo(sess))
		meson_amvdec_esparser_power_up(sess);

	return 0;

disable_dos:
	clk_disable_unprepare(sess->core->dos_clk);
disable_dos_parser:
	clk_disable_unprepare(sess->core->dos_parser_clk);

	return ret;
}

static void vdec_wait_inactive(struct amvdec_session *sess)
{
	/* Treat 50 ms without an IRQ as inactive. */
	while (time_is_after_jiffies64(sess->last_irq_jiffies +
				       msecs_to_jiffies(50)))
		msleep(25);
}

static void vdec_cancel_job_watch(struct amvdec_session *sess)
{
	WRITE_ONCE(sess->watch_on, false);
	cancel_delayed_work_sync(&sess->job_watch);
}

static int vdec_poweroff(struct amvdec_session *sess)
{
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;
	int ret;

	vdec_cancel_job_watch(sess);

	sess->should_stop = 1;
	vdec_wait_inactive(sess);

	meson_amvdec_esparser_quiesce(sess->core);

	/*
	 * Detach the session and synchronize its IRQ before freeing codec memory.
	 * Hold claim_lock through power-down so another session cannot start the core.
	 * No codec lock is held while waiting for the IRQ handler.
	 */
	mutex_lock(&sess->core->claim_lock);
	sess->core->cur_sess = NULL;
	synchronize_irq(sess->core->irq);

	ret = vdec_ops->stop(sess);
	if (!ret) {
		clk_disable_unprepare(sess->core->dos_clk);
		clk_disable_unprepare(sess->core->dos_parser_clk);
	}
	mutex_unlock(&sess->core->claim_lock);

	return ret;
}

/* HEVC-core formats: where the firmware and the stream fetcher stand. */
static void vdec_job_watch_stream(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	if (sess->fmt_out->pixfmt != V4L2_PIX_FMT_HEVC_SLICE &&
	    sess->fmt_out->pixfmt != V4L2_PIX_FMT_VP9_FRAME)
		return;
	/* Registers only while this session owns the powered core */
	if (READ_ONCE(core->cur_sess) != sess)
		return;

	dev_warn(core->dev,
		 "stuck request: status=%x endpoint=%x shift=%x lcu=%x stream wp=%x rp=%x level=%x\n",
		 meson_amvdec_read_dos(core, HEVC_ASSIST_SCRATCH_0),
		 meson_amvdec_read_dos(core, HEVC_ASSIST_SCRATCH_N),
		 meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
		 meson_amvdec_read_dos(core, HEVC_PARSER_LCU_START),
		 meson_amvdec_read_dos(core, HEVC_STREAM_WR_PTR),
		 meson_amvdec_read_dos(core, HEVC_STREAM_RD_PTR),
		 meson_amvdec_read_dos(core, HEVC_STREAM_LEVEL));
}

/*
 * Report stalled jobs once per job_watch_ms interval, including queue state
 * and the request trace. A running job or queued input requires progress.
 */
static void vdec_job_watch(struct work_struct *work)
{
	struct amvdec_session *sess = container_of(to_delayed_work(work),
						   struct amvdec_session,
						   job_watch);
	u32 seq = READ_ONCE(sess->run_seq);
	u64 age = ktime_get_ns() - READ_ONCE(sess->run_ns);
	bool running = v4l2_m2m_get_curr_priv(sess->m2m_dev) == sess;
	unsigned int src = v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx);
	unsigned int dst = v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
	unsigned int ms = READ_ONCE(job_watch_ms);

	if (!READ_ONCE(sess->watch_on) || !ms)
		return;
	/* A stopped or failed queue cannot make progress on another job. */
	if (READ_ONCE(sess->should_stop) ||
	    sess->m2m_ctx->out_q_ctx.q.error ||
	    sess->m2m_ctx->cap_q_ctx.q.error) {
		WRITE_ONCE(sess->watch_on, false);
		return;
	}

	if (age > (u64)ms * NSEC_PER_MSEC && (running || src) &&
	    sess->watch_dumped_seq != seq) {
		sess->watch_dumped_seq = seq;
		dev_warn(sess->core->dev,
			 "no new job for %llu ms: job %u running=%d src_ready=%u dst_ready=%u parser_queued=%d request_state=%d run_armed=%d seq=%u/%u\n",
			 age / NSEC_PER_MSEC, seq, running, src, dst,
			 atomic_read(&sess->esparser_queued_bufs),
			 atomic_read(&sess->request_job.state),
			 READ_ONCE(sess->m2m_run_armed),
			 READ_ONCE(sess->sequence_out),
			 READ_ONCE(sess->sequence_cap));
		vdec_job_watch_stream(sess);
		meson_amvdec_trace_dump(sess, "job watch");
	}

	if (READ_ONCE(sess->watch_on))
		mod_delayed_work(system_dfl_wq, &sess->job_watch,
				 msecs_to_jiffies(ms / 4 + 1));
}

static void vdec_m2m_device_run(void *priv)
{
	struct amvdec_session *sess = priv;

	meson_amvdec_trace(sess, AMVDEC_TR_RUN, 0, 0, 0);
	WRITE_ONCE(sess->run_ns, ktime_get_ns());
	WRITE_ONCE(sess->run_seq, sess->run_seq + 1);
	if (READ_ONCE(job_watch_ms) && !READ_ONCE(sess->should_stop)) {
		WRITE_ONCE(sess->watch_on, true);
		queue_delayed_work(system_dfl_wq, &sess->job_watch,
				   msecs_to_jiffies(job_watch_ms / 4 + 1));
	}
	WRITE_ONCE(sess->m2m_run_armed, true);
	schedule_work(&sess->esparser_queue_work);
}

static void vdec_m2m_job_abort(void *priv)
{
	struct amvdec_session *sess = priv;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	vdec_cancel_job_watch(sess);
	if (codec_ops->job_abort)
		codec_ops->job_abort(sess);
	v4l2_m2m_job_finish(sess->m2m_dev, sess->m2m_ctx);
}

static const struct v4l2_m2m_ops vdec_m2m_ops = {
	.device_run = vdec_m2m_device_run,
	.job_abort = vdec_m2m_job_abort,
};

static void process_num_buffers(struct vb2_queue *q,
				struct amvdec_session *sess,
				unsigned int *num_buffers,
				bool is_reqbufs)
{
	const struct amvdec_format *fmt_out = sess->fmt_out;
	unsigned int q_num_bufs = vb2_get_num_buffers(q);
	unsigned int buffers_total = q_num_bufs + *num_buffers;

	if (!buffers_total)
		*num_buffers = 1;
	if (is_reqbufs && buffers_total < fmt_out->min_buffers)
		*num_buffers = fmt_out->min_buffers - q_num_bufs;
	if (buffers_total > fmt_out->max_buffers)
		*num_buffers = fmt_out->max_buffers - q_num_bufs;

	q->min_queued_buffers = 0;
}

/*
 * GXBB's ANC2AXI reference table keeps only address bits 31:16 (the slot
 * number is packed into bits 15:8), so every HEVC reference plane must be
 * 64 KiB aligned.  CMA aligns an allocation to its size rounded up to a
 * power of two, so sizing each NV12M plane as a 64 KiB multiple is enough.
 */
static bool vdec_nv12m_needs_64k(struct amvdec_session *sess)
{
	u32 pixfmt = sess->fmt_out->pixfmt;

	return sess->core->platform->revision == AMVDEC_REVISION_GXBB &&
	       pixfmt == V4L2_PIX_FMT_HEVC_SLICE;
}

static u32 vdec_nv12m_plane_size(struct amvdec_session *sess, u32 size)
{
	return vdec_nv12m_needs_64k(sess) ? ALIGN(size, SZ_64K) : size;
}

static int vdec_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			    unsigned int *num_planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct amvdec_session *sess = vb2_get_drv_priv(q);
	u32 output_size = meson_amvdec_get_output_size(sess);

	if (*num_planes) {
		switch (q->type) {
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			if (*num_planes != 1 ||
			    sizes[0] < sess->src_buffer_size)
				return -EINVAL;
			break;
		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
			switch (sess->pixfmt_cap) {
			case V4L2_PIX_FMT_NV12M:
				if (*num_planes != 2 ||
				    sizes[0] < vdec_nv12m_plane_size(sess, output_size) ||
				    sizes[1] < vdec_nv12m_plane_size(sess, output_size / 2))
					return -EINVAL;
				break;
			case V4L2_PIX_FMT_YUV420M:
				if (*num_planes != 3 ||
				    sizes[0] < output_size ||
				    sizes[1] < output_size / 4 ||
				    sizes[2] < output_size / 4)
					return -EINVAL;
				break;
			case V4L2_PIX_FMT_AM21C:
				if (*num_planes != 1 ||
				    sizes[0] < AM21C_HEADER_SIZE)
					return -EINVAL;
				break;
			default:
				return -EINVAL;
			}

			process_num_buffers(q, sess, num_buffers, false);
			break;
		}

		return 0;
	}

	switch (q->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		sizes[0] = sess->src_buffer_size;
		*num_planes = 1;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		switch (sess->pixfmt_cap) {
		case V4L2_PIX_FMT_NV12M:
			sizes[0] = vdec_nv12m_plane_size(sess, output_size);
			sizes[1] = vdec_nv12m_plane_size(sess, output_size / 2);
			*num_planes = 2;
			break;
		case V4L2_PIX_FMT_YUV420M:
			sizes[0] = output_size;
			sizes[1] = output_size / 4;
			sizes[2] = output_size / 4;
			*num_planes = 3;
			break;
		case V4L2_PIX_FMT_AM21C:
			sizes[0] = AM21C_HEADER_SIZE;
			*num_planes = 1;
			break;
		default:
			return -EINVAL;
		}

		process_num_buffers(q, sess, num_buffers, true);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Software-only snapshots: safe even when the decoder is powered down. */
static void vdec_debug_state(struct amvdec_session *sess, const char *event,
			     u32 type, u32 value)
{
	dev_dbg(sess->core->dev,
		"%s sess=%p type=%u value=%#x status=%u stream=%u/%u stop=%u src_ready=%u dst_ready=%u seq=%u/%u credit=%d irq_jiffies=%llu\n",
		event, sess, type, value, READ_ONCE(sess->status),
		READ_ONCE(sess->streamon_out), READ_ONCE(sess->streamon_cap),
		READ_ONCE(sess->should_stop),
		v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx),
		v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx),
		READ_ONCE(sess->sequence_out), READ_ONCE(sess->sequence_cap),
		atomic_read(&sess->esparser_queued_bufs),
		READ_ONCE(sess->last_irq_jiffies));
}

static void vdec_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct amvdec_session *sess = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(sess->m2m_ctx, vbuf);
	vdec_debug_state(sess, "qbuf", vb->type, vb->index);
}

static int vdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct amvdec_session *sess = vb2_get_drv_priv(q);
	struct amvdec_core *core = sess->core;
	struct vb2_v4l2_buffer *buf;
	int ret;

	vdec_debug_state(sess, "streamon-enter", q->type, count);

	/* Claim the shared decoder atomically across independently locked sessions. */
	mutex_lock(&core->claim_lock);
	if (core->cur_sess && core->cur_sess != sess) {
		mutex_unlock(&core->claim_lock);
		ret = -EBUSY;
		goto bufs_done;
	}
	core->cur_sess = sess;
	mutex_unlock(&core->claim_lock);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		sess->streamon_out = 1;
	else
		sess->streamon_cap = 1;

	if (!sess->streamon_out || !sess->streamon_cap)
		return 0;

	if (sess->status == STATUS_RUNNING ||
	    sess->status == STATUS_INIT)
		return 0;

	sess->vififo_size = 0;
	sess->vififo_vaddr = NULL;
	sess->vififo_paddr = 0;
	if (vdec_needs_parser_vififo(sess)) {
		sess->vififo_size = SIZE_VIFIFO;
		sess->vififo_vaddr =
			dma_alloc_coherent(sess->core->dev, sess->vififo_size,
					   &sess->vififo_paddr, GFP_KERNEL);
		if (!sess->vififo_vaddr) {
			dev_err(sess->core->dev, "Failed to request VIFIFO buffer\n");
			ret = -ENOMEM;
			goto bufs_done;
		}
	}

	sess->should_stop = 0;
	sess->last_offset = 0;
	sess->wrap_count = 0;
	sess->bitdepth = 8;
	atomic_set(&sess->esparser_queued_bufs, 0);

	ret = vdec_poweron(sess);
	if (ret)
		goto vififo_free;

	sess->sequence_cap = 0;
	sess->sequence_out = 0;

	sess->status = STATUS_INIT;
	schedule_work(&sess->esparser_queue_work);
	return 0;

vififo_free:
	if (sess->vififo_vaddr)
		dma_free_coherent(sess->core->dev, sess->vififo_size,
				  sess->vififo_vaddr, sess->vififo_paddr);
bufs_done:
	mutex_lock(&core->claim_lock);
	if (core->cur_sess == sess)
		core->cur_sess = NULL;
	mutex_unlock(&core->claim_lock);

	while ((buf = v4l2_m2m_src_buf_remove(sess->m2m_ctx)))
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
	while ((buf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx)))
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		sess->streamon_out = 0;
	else
		sess->streamon_cap = 0;

	return ret;
}

static void vdec_free_canvas(struct amvdec_session *sess)
{
	int i;

	for (i = 0; i < sess->canvas_num; ++i)
		meson_canvas_free(sess->core->canvas, sess->canvas_alloc[i]);

	sess->canvas_num = 0;

	/* The firmware buffer mappings name the canvases just freed. */
	sess->num_fw_bufs = 0;
	for (i = 0; i < ARRAY_SIZE(sess->fw_idx_to_vb2_idx); ++i)
		sess->fw_idx_to_vb2_idx[i] = VB2_IDX_UNMAPPED;
	for (i = 0; i < ARRAY_SIZE(sess->vb2_idx_to_fw_idx); ++i)
		sess->vb2_idx_to_fw_idx[i] = VB2_IDX_UNMAPPED;
}

static void vdec_reset_timestamps(struct amvdec_session *sess)
{
	struct amvdec_timestamp *tmp, *n;

	list_for_each_entry_safe(tmp, n, &sess->timestamps, list) {
		list_del(&tmp->list);
		kfree(tmp);
	}
}

static void vdec_stop_streaming(struct vb2_queue *q)
{
	struct amvdec_session *sess = vb2_get_drv_priv(q);
	struct amvdec_core *core = sess->core;
	struct vb2_v4l2_buffer *buf;
	int stop_ret = 0;

	vdec_cancel_job_watch(sess);
	vdec_debug_state(sess, "streamoff-enter", q->type, 0);

	if (sess->status == STATUS_RUNNING ||
	    sess->status == STATUS_INIT) {
		stop_ret = vdec_poweroff(sess);
		if (stop_ret) {
			dev_err(core->dev,
				"decoder stop failed (%d); retaining private DMA allocations\n",
				stop_ret);
			vb2_queue_error(&sess->m2m_ctx->out_q_ctx.q);
			vb2_queue_error(&sess->m2m_ctx->cap_q_ctx.q);
			sess->status = STATUS_STOPPED;
		} else {
			if (meson_amvdec_request_jobs(sess))
				meson_amvdec_request_retire(sess, true);
			if (sess->request_job.input_vaddr) {
				dma_free_coherent(core->dev, sess->request_job.input_size,
						  sess->request_job.input_vaddr,
						  sess->request_job.input_paddr);
				sess->request_job.input_vaddr = NULL;
				sess->request_job.input_size = 0;
			}
			vdec_free_canvas(sess);
			if (sess->vififo_vaddr)
				dma_free_coherent(sess->core->dev,
						  sess->vififo_size,
						  sess->vififo_vaddr,
						  sess->vififo_paddr);
			sess->hevc_headers_valid = false;
			vdec_reset_timestamps(sess);
			kfree(sess->priv);
			sess->priv = NULL;
			sess->status = STATUS_STOPPED;
		}
	}

	/* Return both buffers of the active request before tearing down either queue. */
	if (meson_amvdec_request_jobs(sess))
		meson_amvdec_request_retire(sess, true);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		while ((buf = v4l2_m2m_src_buf_remove(sess->m2m_ctx))) {
			v4l2_ctrl_request_complete(buf->vb2_buf.req_obj.req,
						   &sess->ctrl_handler);
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
		}

		sess->streamon_out = 0;
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);

		sess->streamon_cap = 0;
	}

	/* Release an early claim once this session is fully off */
	mutex_lock(&core->claim_lock);
	if (!sess->streamon_out && !sess->streamon_cap &&
	    core->cur_sess == sess)
		core->cur_sess = NULL;
	mutex_unlock(&core->claim_lock);
	vdec_debug_state(sess, "streamoff-exit", q->type, 0);
}

static int vdec_vb2_buf_prepare(struct vb2_buffer *vb)
{
	to_vb2_v4l2_buffer(vb)->field = V4L2_FIELD_NONE;
	return 0;
}

static int vdec_buf_out_validate(struct vb2_buffer *vb)
{
	to_vb2_v4l2_buffer(vb)->field = V4L2_FIELD_NONE;
	return 0;
}

static void vdec_buf_request_complete(struct vb2_buffer *vb)
{
	struct amvdec_session *sess = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &sess->ctrl_handler);
}

static const struct vb2_ops vdec_vb2_ops = {
	.queue_setup = vdec_queue_setup,
	.start_streaming = vdec_start_streaming,
	.stop_streaming = vdec_stop_streaming,
	.buf_queue = vdec_vb2_buf_queue,
	.buf_prepare = vdec_vb2_buf_prepare,
	.buf_out_validate = vdec_buf_out_validate,
	.buf_request_complete = vdec_buf_request_complete,
};

static int
vdec_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "meson-amvdec", sizeof(cap->driver));
	strscpy(cap->card, "Amlogic Video Decoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:meson-amvdec", sizeof(cap->bus_info));

	return 0;
}

static const struct amvdec_format *
find_format(const struct amvdec_format *fmts, u32 size, u32 pixfmt)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		if (fmts[i].pixfmt == pixfmt)
			return &fmts[i];
	}

	return NULL;
}

static unsigned int
vdec_supports_pixfmt_cap(const struct amvdec_format *fmt_out, u32 pixfmt_cap)
{
	int i;

	for (i = 0; fmt_out->pixfmts_cap[i]; i++)
		if (fmt_out->pixfmts_cap[i] == pixfmt_cap)
			return 1;

	return 0;
}

static const struct amvdec_format *
vdec_try_fmt_common(struct amvdec_session *sess, u32 size,
		    struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *pfmt = pixmp->plane_fmt;
	const struct amvdec_format *fmts = sess->core->platform->formats;
	const struct amvdec_format *fmt_out = NULL;
	u32 output_size = 0;

	memset(pfmt[0].reserved, 0, sizeof(pfmt[0].reserved));
	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		fmt_out = find_format(fmts, size, pixmp->pixelformat);
		if (!fmt_out) {
			pixmp->pixelformat = fmts[0].pixfmt;
			fmt_out = &fmts[0];
		}
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		fmt_out = sess->fmt_out;
		break;
	default:
		return NULL;
	}

	pixmp->width = clamp(pixmp->width,
			     amvdec_min_coded_width(fmt_out->pixfmt),
			     fmt_out->max_width);
	pixmp->height = clamp(pixmp->height,
			      amvdec_min_coded_height(fmt_out->pixfmt),
			      fmt_out->max_height);
	output_size = get_output_size(pixmp->width, pixmp->height);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (!pfmt[0].sizeimage)
			pfmt[0].sizeimage = sess->src_buffer_size;
		pfmt[0].bytesperline = 0;
		pixmp->num_planes = 1;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt_out = sess->fmt_out;
		if (!vdec_supports_pixfmt_cap(fmt_out, pixmp->pixelformat))
			pixmp->pixelformat = fmt_out->pixfmts_cap[0];

		memset(pfmt[1].reserved, 0, sizeof(pfmt[1].reserved));
		if (pixmp->pixelformat == V4L2_PIX_FMT_NV12M) {
			/* HEVC and VP9 linear output uses a 32-byte row pitch. */
			u32 align = (fmt_out->pixfmt == V4L2_PIX_FMT_HEVC_SLICE ||
				     fmt_out->pixfmt == V4L2_PIX_FMT_VP9_FRAME) ? 32 : 64;

			pfmt[0].sizeimage = vdec_nv12m_plane_size(sess, output_size);
			pfmt[0].bytesperline = ALIGN(pixmp->width, align);

			pfmt[1].sizeimage = vdec_nv12m_plane_size(sess, output_size / 2);
			pfmt[1].bytesperline = ALIGN(pixmp->width, align);
			pixmp->num_planes = 2;
		} else if (pixmp->pixelformat == V4L2_PIX_FMT_YUV420M) {
			pfmt[0].sizeimage = output_size;
			pfmt[0].bytesperline = ALIGN(pixmp->width, 64);

			pfmt[1].sizeimage = output_size / 4;
			pfmt[1].bytesperline = ALIGN(pixmp->width, 64) / 2;

			pfmt[2].sizeimage = output_size / 2;
			pfmt[2].bytesperline = ALIGN(pixmp->width, 64) / 2;
			pixmp->num_planes = 3;
		} else if (pixmp->pixelformat == V4L2_PIX_FMT_AM21C) {
			/* one plane: the FBC/MMU compression header */
			pfmt[0].sizeimage = AM21C_HEADER_SIZE;
			pfmt[0].bytesperline = 0;
			pixmp->num_planes = 1;
		}
	}

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;

	return fmt_out;
}

static int vdec_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct amvdec_session *sess = file_to_amvdec_session(file);

	vdec_try_fmt_common(sess, sess->core->platform->num_formats, f);

	return 0;
}

static int vdec_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct amvdec_session *sess = file_to_amvdec_session(file);
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		pixmp->pixelformat = sess->pixfmt_cap;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		pixmp->pixelformat = sess->fmt_out->pixfmt;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pixmp->width = sess->width;
		pixmp->height = sess->height;
		pixmp->colorspace = sess->colorspace;
		pixmp->ycbcr_enc = sess->ycbcr_enc;
		pixmp->quantization = sess->quantization;
		pixmp->xfer_func = sess->xfer_func;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pixmp->width = sess->width;
		pixmp->height = sess->height;
	}

	vdec_try_fmt_common(sess, sess->core->platform->num_formats, f);

	return 0;
}

static int vdec_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct amvdec_session *sess = file_to_amvdec_session(file);
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	u32 num_formats = sess->core->platform->num_formats;
	const struct amvdec_format *fmt_out;
	struct v4l2_pix_format_mplane orig_pixmp;
	struct v4l2_format format;
	u32 pixfmt_out = 0, pixfmt_cap = 0;

	if (vb2_is_busy(v4l2_m2m_get_src_vq(sess->m2m_ctx)) ||
	    vb2_is_busy(v4l2_m2m_get_dst_vq(sess->m2m_ctx)))
		return -EBUSY;

	orig_pixmp = *pixmp;

	fmt_out = vdec_try_fmt_common(sess, num_formats, f);
	if (!fmt_out)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pixfmt_out = pixmp->pixelformat;
		pixfmt_cap = sess->pixfmt_cap;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pixfmt_cap = pixmp->pixelformat;
		pixfmt_out = sess->fmt_out->pixfmt;
	}

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	format.fmt.pix_mp.pixelformat = pixfmt_out;
	format.fmt.pix_mp.width = orig_pixmp.width;
	format.fmt.pix_mp.height = orig_pixmp.height;
	vdec_try_fmt_common(sess, num_formats, &format);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		sess->width = format.fmt.pix_mp.width;
		sess->height = format.fmt.pix_mp.height;
		sess->colorspace = pixmp->colorspace;
		sess->ycbcr_enc = pixmp->ycbcr_enc;
		sess->quantization = pixmp->quantization;
		sess->xfer_func = pixmp->xfer_func;
		sess->src_buffer_size = pixmp->plane_fmt[0].sizeimage;
	}

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = pixfmt_cap;
	format.fmt.pix_mp.width = orig_pixmp.width;
	format.fmt.pix_mp.height = orig_pixmp.height;
	vdec_try_fmt_common(sess, num_formats, &format);

	sess->width = format.fmt.pix_mp.width;
	sess->height = format.fmt.pix_mp.height;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		sess->fmt_out = fmt_out;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		sess->pixfmt_cap = format.fmt.pix_mp.pixelformat;

	return 0;
}

static int vdec_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct amvdec_session *sess = file_to_amvdec_session(file);
	const struct amvdec_platform *platform = sess->core->platform;
	const struct amvdec_format *fmt_out;

	memset(f->reserved, 0, sizeof(f->reserved));

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (f->index >= platform->num_formats)
			return -EINVAL;

		fmt_out = &platform->formats[f->index];
		f->pixelformat = fmt_out->pixfmt;
		f->flags = fmt_out->flags;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt_out = sess->fmt_out;
		if (f->index >= 4 || !fmt_out->pixfmts_cap[f->index])
			return -EINVAL;

		f->pixelformat = fmt_out->pixfmts_cap[f->index];
	} else {
		return -EINVAL;
	}

	return 0;
}

static int vdec_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct amvdec_session *sess = file_to_amvdec_session(file);
	const struct amvdec_format *formats = sess->core->platform->formats;
	const struct amvdec_format *fmt;
	u32 num_formats = sess->core->platform->num_formats;

	fmt = find_format(formats, num_formats, fsize->pixel_format);
	if (!fmt || fsize->index)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;

	/* Advertise the coded-size floor enforced by each codec. */
	fsize->stepwise.min_width = amvdec_min_coded_width(fsize->pixel_format);
	fsize->stepwise.min_height = amvdec_min_coded_height(fsize->pixel_format);
	fsize->stepwise.max_width = fmt->max_width;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.max_height = fmt->max_height;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int vdec_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops vdec_ioctl_ops = {
	.vidioc_querycap = vdec_querycap,
	.vidioc_enum_fmt_vid_cap = vdec_enum_fmt,
	.vidioc_enum_fmt_vid_out = vdec_enum_fmt,
	.vidioc_s_fmt_vid_cap_mplane = vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = vdec_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane = vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = vdec_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = vdec_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = vdec_try_fmt,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_enum_framesizes = vdec_enum_framesizes,
	.vidioc_subscribe_event = vdec_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int m2m_queue_init(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq)
{
	struct amvdec_session *sess = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = &vdec_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = sess;
	src_vq->buf_struct_size = sizeof(struct dummy_buf);
	src_vq->supports_requests = true;
	src_vq->requires_requests = true;
	src_vq->min_queued_buffers = 0;
	src_vq->dev = sess->core->dev;
	src_vq->lock = &sess->lock;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	/*
	 * Let vb2 report CMA allocation failures through REQBUFS without an allocator
	 * warning or free-map dump.
	 */
	dst_vq->gfp_flags = __GFP_NOWARN;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &vdec_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->drv_priv = sess;
	dst_vq->buf_struct_size = sizeof(struct dummy_buf);
	dst_vq->min_queued_buffers = 0;
	dst_vq->dev = sess->core->dev;
	dst_vq->lock = &sess->lock;
	return vb2_queue_init(dst_vq);
}

static const struct v4l2_ctrl_h264_scaling_matrix vdec_h264_scaling_default = {
	.scaling_list_4x4 = { [0 ... 5] = { [0 ... 15] = 16 } },
	.scaling_list_8x8 = { [0 ... 1] = { [0 ... 63] = 16 } },
};

/* Find the coded format in the selected compatible's capability table. */
static const struct amvdec_format *
vdec_format_by_pixfmt(struct amvdec_core *core, u32 pixfmt)
{
	const struct amvdec_platform *platform = core->platform;
	u32 i;

	for (i = 0; i < platform->num_formats; i++)
		if (platform->formats[i].pixfmt == pixfmt)
			return &platform->formats[i];
	return NULL;
}

/* Reject unsupported sequence syntax before a client commits to hardware. */
static struct amvdec_session *vdec_ctrl_session(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct amvdec_session, ctrl_handler);
}

static int vdec_h264_try_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_STATELESS_H264_SPS: {
		const struct v4l2_ctrl_h264_sps *sps = ctrl->p_new.p_h264_sps;
		const struct amvdec_format *fmt =
			vdec_format_by_pixfmt(vdec_ctrl_session(ctrl)->core,
					      V4L2_PIX_FMT_H264_SLICE);
		u32 w = (sps->pic_width_in_mbs_minus1 + 1) * 16;
		u32 h = (sps->pic_height_in_map_units_minus1 + 1) * 16 *
			((sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) ? 1 : 2);

		/* Enforce the coded-size limit from platform data. */
		if (fmt && (w > fmt->max_width || h > fmt->max_height))
			return -EINVAL;
		return h264_sps_validate(sps) ? -EINVAL : 0;
	}
	case V4L2_CID_STATELESS_H264_PPS:
		return ctrl->p_new.p_h264_pps->num_slice_groups_minus1 ?
		       -EINVAL : 0;
	default:
		return 0;
	}
}

static const struct v4l2_ctrl_ops vdec_h264_ctrl_ops = {
	.try_ctrl = vdec_h264_try_ctrl,
};

static int amvdec_hevc_try_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_STATELESS_HEVC_SPS: {
		const struct v4l2_ctrl_hevc_sps *sps = ctrl->p_new.p_hevc_sps;
		const struct amvdec_format *fmt =
			vdec_format_by_pixfmt(vdec_ctrl_session(ctrl)->core,
					      V4L2_PIX_FMT_HEVC_SLICE);

		if (fmt && (sps->pic_width_in_luma_samples > fmt->max_width ||
			    sps->pic_height_in_luma_samples > fmt->max_height ||
			    (fmt->max_bit_depth &&
			     sps->bit_depth_luma_minus8 + 8 > fmt->max_bit_depth)))
			return -EINVAL;
		if (hevc_sps_bad_geometry(sps)) {
			dev_dbg(vdec_ctrl_session(ctrl)->core->dev,
				"HEVC pictures with one CTB column and multiple rows are unsupported\n");
			return -EINVAL;
		}
		return hevc_sps_validate(sps) ? -EINVAL : 0;
	}
	case V4L2_CID_STATELESS_HEVC_PPS:
		return hevc_pps_validate(ctrl->p_new.p_hevc_pps) ? -EINVAL : 0;
	case V4L2_CID_STATELESS_HEVC_EXT_SPS_ST_RPS: {
		struct hevc_rps *resolved;
		int ret;

		if (ctrl->new_elems > HEVC_RPS_MAX_SETS)
			return -EINVAL;
		resolved = kcalloc(ctrl->new_elems, sizeof(*resolved), GFP_KERNEL);
		if (!resolved)
			return -ENOMEM;
		ret = hevc_rps_resolve(ctrl->p_new.p, ctrl->new_elems, resolved);
		kfree(resolved);
		return ret;
	}
	case V4L2_CID_STATELESS_HEVC_EXT_SPS_LT_RPS:
		/* The primary image does not implement the SPS LT table ABI. */
		return ctrl->new_elems ? -EINVAL : 0;
	default:
		return 0;
	}
}

static const struct v4l2_ctrl_ops amvdec_hevc_ctrl_ops = {
	.try_ctrl = amvdec_hevc_try_ctrl,
};

static int vdec_mpeg2_try_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_STATELESS_MPEG2_SEQUENCE:
		return mpeg12_sequence_validate(ctrl->p_new.p_mpeg2_sequence) ?
		       -EINVAL : 0;
	default:
		return 0;
	}
}

static const struct v4l2_ctrl_ops vdec_mpeg2_ctrl_ops = {
	.try_ctrl = vdec_mpeg2_try_ctrl,
};

/* Compound controls must have defaults accepted by their try_ctrl callback. */
static const struct v4l2_ctrl_h264_sps vdec_h264_sps_default = {
	.profile_idc = 77,
	.level_idc = 41,
	.chroma_format_idc = 1,
	.flags = V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY,
};

static const struct v4l2_ctrl_hevc_sps amvdec_hevc_sps_default = {
	.chroma_format_idc = 1,
};

static const struct v4l2_ctrl_mpeg2_sequence vdec_mpeg2_sequence_default = {
	.horizontal_size = 16,
	.vertical_size = 16,
	.chroma_format = 1,
};

static int vdec_init_ctrls(struct amvdec_session *sess)
{
	struct v4l2_ctrl_handler *ctrl_handler = &sess->ctrl_handler;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_handler, 20);
	if (ret)
		return ret;

	/* Create controls for the formats in the selected capability table. */
	if (vdec_format_by_pixfmt(sess->core, V4L2_PIX_FMT_H264_SLICE)) {
		static const struct v4l2_ctrl_config h264_ctrls[] = {
			{
				.id = V4L2_CID_STATELESS_H264_SPS,
				.ops = &vdec_h264_ctrl_ops,
				.p_def.p_const = &vdec_h264_sps_default,
			}, {
				.id = V4L2_CID_STATELESS_H264_PPS,
				.ops = &vdec_h264_ctrl_ops,
			}, {
				.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
				.ops = &vdec_h264_ctrl_ops,
				.p_def.p_const = &vdec_h264_scaling_default,
			}, {
				.id = V4L2_CID_STATELESS_H264_PRED_WEIGHTS,
			}, {
				.id = V4L2_CID_STATELESS_H264_SLICE_PARAMS,
			}, {
				.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
			}, {
				.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
				.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
				.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
				.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			}, {
				.id = V4L2_CID_STATELESS_H264_START_CODE,
				.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
				.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
				.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			},
		};
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(h264_ctrls); i++)
			v4l2_ctrl_new_custom(ctrl_handler, &h264_ctrls[i], NULL);
	}

	if (vdec_format_by_pixfmt(sess->core, V4L2_PIX_FMT_MPEG2_SLICE)) {
		static const struct v4l2_ctrl_config mpeg2_ctrls[] = {
			{
				.id = V4L2_CID_STATELESS_MPEG2_SEQUENCE,
				.ops = &vdec_mpeg2_ctrl_ops,
				.p_def.p_const = &vdec_mpeg2_sequence_default,
			}, {
				.id = V4L2_CID_STATELESS_MPEG2_PICTURE,
			}, {
				.id = V4L2_CID_STATELESS_MPEG2_QUANTISATION,
			},
		};
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(mpeg2_ctrls); i++)
			v4l2_ctrl_new_custom(ctrl_handler,
					     &mpeg2_ctrls[i], NULL);
	}

	if (vdec_format_by_pixfmt(sess->core, V4L2_PIX_FMT_HEVC_SLICE)) {
		static const struct v4l2_ctrl_config hevc_ctrls[] = {
			{
				.id = V4L2_CID_STATELESS_HEVC_SPS,
				.ops = &amvdec_hevc_ctrl_ops,
				.p_def.p_const = &amvdec_hevc_sps_default,
			}, {
				.id = V4L2_CID_STATELESS_HEVC_PPS,
				.ops = &amvdec_hevc_ctrl_ops,
			}, {
				.id = V4L2_CID_STATELESS_HEVC_SLICE_PARAMS,
				.type = V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS,
				.flags = V4L2_CTRL_FLAG_DYNAMIC_ARRAY,
				.dims = { AMVDEC_HEVC_MAX_SLICES },
			}, {
				.id = V4L2_CID_STATELESS_HEVC_DECODE_PARAMS,
			}, {
				.id = V4L2_CID_STATELESS_HEVC_EXT_SPS_ST_RPS,
				.ops = &amvdec_hevc_ctrl_ops,
				.flags = V4L2_CTRL_FLAG_DYNAMIC_ARRAY,
				.dims = { HEVC_RPS_MAX_SETS },
			}, {
				.id = V4L2_CID_STATELESS_HEVC_EXT_SPS_LT_RPS,
				.ops = &amvdec_hevc_ctrl_ops,
				.flags = V4L2_CTRL_FLAG_DYNAMIC_ARRAY,
				.dims = { 32 },
			}, {
				.id = V4L2_CID_STATELESS_HEVC_SCALING_MATRIX,
			}, {
				.id = V4L2_CID_STATELESS_HEVC_DECODE_MODE,
				.min = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
				.max = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
				.def = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
			}, {
				.id = V4L2_CID_STATELESS_HEVC_START_CODE,
				.min = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
				.max = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
				.def = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
			},
		};
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(hevc_ctrls); i++)
			v4l2_ctrl_new_custom(ctrl_handler, &hevc_ctrls[i], NULL);
	}

	/* Menus are derived from this compatible's codec descriptors. */
	for (unsigned int i = 0; i < sess->core->platform->num_formats; i++) {
		const struct amvdec_format *fmt = &sess->core->platform->formats[i];

		if (fmt->profile_ctrl)
			v4l2_ctrl_new_std_menu(ctrl_handler, NULL, fmt->profile_ctrl,
					       fls(fmt->profiles) - 1, ~fmt->profiles,
					       fmt->default_profile);
		if (fmt->level_ctrl)
			v4l2_ctrl_new_std_menu(ctrl_handler, NULL, fmt->level_ctrl,
					       fmt->max_level, 0, fmt->default_level);
	}

	ret = ctrl_handler->error;
	if (ret) {
		v4l2_ctrl_handler_free(ctrl_handler);
		return ret;
	}

	return 0;
}

static int vdec_open(struct file *file)
{
	struct amvdec_core *core = video_drvdata(file);
	struct device *dev = core->dev;
	const struct amvdec_format *formats = core->platform->formats;
	struct amvdec_session *sess;
	int ret;

	sess = kzalloc_obj(*sess);
	if (!sess)
		return -ENOMEM;

	sess->core = core;

	sess->m2m_dev = core->m2m_dev;

	sess->m2m_ctx = v4l2_m2m_ctx_init(sess->m2m_dev, sess, m2m_queue_init);
	if (IS_ERR(sess->m2m_ctx)) {
		dev_err(dev, "Fail to v4l2_m2m_ctx_init\n");
		ret = PTR_ERR(sess->m2m_ctx);
		goto err_free_sess;
	}

	ret = vdec_init_ctrls(sess);
	if (ret)
		goto err_m2m_ctx_release;

	sess->pixfmt_cap = formats[0].pixfmts_cap[0];
	sess->fmt_out = &formats[0];
	sess->width = 1280;
	sess->height = 720;
	sess->src_buffer_size = SZ_1M;

	INIT_LIST_HEAD(&sess->timestamps);
	INIT_WORK(&sess->esparser_queue_work, meson_amvdec_esparser_queue_all_src);
	INIT_DELAYED_WORK(&sess->job_watch, vdec_job_watch);
	mutex_init(&sess->lock);
	spin_lock_init(&sess->ts_spinlock);

	v4l2_fh_init(&sess->fh, core->vdev_dec);
	sess->fh.ctrl_handler = &sess->ctrl_handler;
	v4l2_fh_add(&sess->fh, file);
	sess->fh.m2m_ctx = sess->m2m_ctx;

	return 0;

err_m2m_ctx_release:
	v4l2_m2m_ctx_release(sess->m2m_ctx);
err_free_sess:
	kfree(sess);
	return ret;
}

static int vdec_close(struct file *file)
{
	struct amvdec_session *sess = file_to_amvdec_session(file);

	WRITE_ONCE(sess->watch_on, false);
	disable_delayed_work_sync(&sess->job_watch);
	disable_work_sync(&sess->esparser_queue_work);
	v4l2_m2m_ctx_release(sess->m2m_ctx);
	v4l2_fh_del(&sess->fh, file);
	v4l2_fh_exit(&sess->fh);
	v4l2_ctrl_handler_free(&sess->ctrl_handler);

	mutex_destroy(&sess->lock);

	kfree(sess);

	return 0;
}

static const struct v4l2_file_operations vdec_fops = {
	.owner = THIS_MODULE,
	.open = vdec_open,
	.release = vdec_close,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

static irqreturn_t vdec_isr(int irq, void *data)
{
	struct amvdec_core *core = data;
	struct amvdec_session *sess = core->cur_sess;

	/* An interrupt can arrive after STREAMOFF detaches the session. */
	if (!sess)
		return IRQ_NONE;

	sess->last_irq_jiffies = get_jiffies_64();

	return sess->fmt_out->codec_ops->isr(sess);
}

static irqreturn_t vdec_threaded_isr(int irq, void *data)
{
	struct amvdec_core *core = data;
	struct amvdec_session *sess = core->cur_sess;

	if (!sess)
		return IRQ_NONE;

	return sess->fmt_out->codec_ops->threaded_isr(sess);
}

static const struct of_device_id vdec_dt_match[] = {
	{ .compatible = "amlogic,gxbb-vdec",
	  .data = &meson_amvdec_platform_gxbb },
	{ .compatible = "amlogic,gxm-vdec",
	  .data = &meson_amvdec_platform_gxm },
	{ .compatible = "amlogic,gxl-vdec",
	  .data = &meson_amvdec_platform_gxl },
	{ .compatible = "amlogic,gxlx-vdec",
	  .data = &meson_amvdec_platform_gxlx },
	{ .compatible = "amlogic,g12a-vdec",
	  .data = &meson_amvdec_platform_g12a },
	{ .compatible = "amlogic,sm1-vdec",
	  .data = &meson_amvdec_platform_sm1 },
	{}
};
MODULE_DEVICE_TABLE(of, vdec_dt_match);

static int vdec_request_validate(struct media_request *req)
{
	unsigned int count = vb2_request_buffer_cnt(req);

	if (!count)
		return -ENOENT;
	if (count > 1)
		return -EINVAL;

	return vb2_request_validate(req);
}

static const struct media_device_ops vdec_media_ops = {
	.req_validate = vdec_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static int vdec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct video_device *vdev;
	struct amvdec_core *core;
	const struct of_device_id *of_id;
	int irq;
	int ret;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = dev;
	platform_set_drvdata(pdev, core);

	core->fw_vaddr = dmam_alloc_coherent(dev, AMVDEC_FW_SIZE,
					     &core->fw_paddr, GFP_KERNEL);
	if (!core->fw_vaddr)
		return -ENOMEM;

	core->dos_base = devm_platform_ioremap_resource_byname(pdev, "dos");
	if (IS_ERR(core->dos_base))
		return PTR_ERR(core->dos_base);

	core->esparser_base = devm_platform_ioremap_resource_byname(pdev, "esparser");
	if (IS_ERR(core->esparser_base))
		return PTR_ERR(core->esparser_base);

	core->regmap_ao =
		syscon_regmap_lookup_by_phandle(dev->of_node,
						"amlogic,ao-sysctrl");
	if (IS_ERR(core->regmap_ao)) {
		dev_err(dev, "Couldn't regmap AO sysctrl\n");
		return PTR_ERR(core->regmap_ao);
	}

	core->canvas = meson_canvas_get(dev);
	if (IS_ERR(core->canvas))
		return PTR_ERR(core->canvas);

	of_id = of_match_node(vdec_dt_match, dev->of_node);
	core->platform = of_id->data;

	if (core->platform->revision == AMVDEC_REVISION_G12A ||
	    core->platform->revision == AMVDEC_REVISION_SM1) {
		core->amvdec_hevcf_clk = devm_clk_get(dev, "vdec_hevcf");
		if (IS_ERR(core->amvdec_hevcf_clk))
			return -EPROBE_DEFER;
	}

	core->dos_parser_clk = devm_clk_get(dev, "dos_parser");
	if (IS_ERR(core->dos_parser_clk))
		return -EPROBE_DEFER;

	core->dos_clk = devm_clk_get(dev, "dos");
	if (IS_ERR(core->dos_clk))
		return -EPROBE_DEFER;

	core->amvdec_1_clk = devm_clk_get(dev, "vdec_1");
	if (IS_ERR(core->amvdec_1_clk))
		return -EPROBE_DEFER;

	core->amvdec_hevc_clk = devm_clk_get(dev, "vdec_hevc");
	if (IS_ERR(core->amvdec_hevc_clk))
		return -EPROBE_DEFER;

	irq = platform_get_irq_byname(pdev, "vdec");
	core->irq = irq;
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(core->dev, irq, vdec_isr,
					vdec_threaded_isr, IRQF_ONESHOT,
					"vdec", core);
	if (ret)
		return ret;

	ret = meson_amvdec_esparser_init(pdev, core);
	if (ret)
		return ret;

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret) {
		dev_err(dev, "Couldn't register v4l2 device\n");
		return -ENOMEM;
	}

	core->m2m_dev = v4l2_m2m_init(&vdec_m2m_ops);
	if (IS_ERR(core->m2m_dev)) {
		ret = PTR_ERR(core->m2m_dev);
		goto err_v4l2_unregister;
	}

	core->mdev.dev = dev;
	strscpy(core->mdev.model, "meson-amvdec", sizeof(core->mdev.model));
	strscpy(core->mdev.bus_info, "platform:meson-amvdec",
		sizeof(core->mdev.bus_info));
	media_device_init(&core->mdev);
	core->mdev.ops = &vdec_media_ops;
	core->v4l2_dev.mdev = &core->mdev;

	vdev = video_device_alloc();
	if (!vdev) {
		ret = -ENOMEM;
		goto err_media_cleanup;
	}

	core->vdev_dec = vdev;
	core->dev_dec = dev;
	mutex_init(&core->lock);
	mutex_init(&core->claim_lock);

	strscpy(vdev->name, "meson-video-decoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = &vdec_fops;
	vdev->ioctl_ops = &vdec_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->lock = &core->lock;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	video_set_drvdata(vdev, core);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "Failed registering video device\n");
		goto err_vdev_release;
	}

	ret = v4l2_m2m_register_media_controller(core->m2m_dev, vdev,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret)
		goto err_video_unregister;

	ret = media_device_register(&core->mdev);
	if (ret)
		goto err_mc_unregister;

	return 0;

err_mc_unregister:
	v4l2_m2m_unregister_media_controller(core->m2m_dev);
err_video_unregister:
	video_unregister_device(vdev);
	goto err_media_cleanup;
err_vdev_release:
	video_device_release(vdev);
err_media_cleanup:
	media_device_cleanup(&core->mdev);
	v4l2_m2m_release(core->m2m_dev);
err_v4l2_unregister:
	v4l2_device_unregister(&core->v4l2_dev);
	return ret;
}

static void vdec_remove(struct platform_device *pdev)
{
	struct amvdec_core *core = platform_get_drvdata(pdev);

	meson_amvdec_codec_hevc_fbc_pool_drain();
	meson_amvdec_codec_hevc_workspace_release();
	media_device_unregister(&core->mdev);
	v4l2_m2m_unregister_media_controller(core->m2m_dev);
	video_unregister_device(core->vdev_dec);
	media_device_cleanup(&core->mdev);
	v4l2_m2m_release(core->m2m_dev);
	v4l2_device_unregister(&core->v4l2_dev);
}

static struct platform_driver meson_amvdec_driver = {
	.probe = vdec_probe,
	.remove = vdec_remove,
	.driver = {
		.name = "meson-amvdec",
		.of_match_table = vdec_dt_match,
	},
};
module_platform_driver(meson_amvdec_driver);

MODULE_DESCRIPTION("Amlogic stateless video decoder driver");
MODULE_AUTHOR("Maxime Jourdan <mjourdan@baylibre.com>");
MODULE_LICENSE("GPL");
