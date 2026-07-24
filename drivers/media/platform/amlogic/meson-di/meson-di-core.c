// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Amlogic GX SoC Video Deinterlacer (DI) driver
 *
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 *
 * A V4L2 memory-to-memory driver for the motion-adaptive hardware
 * deinterlacer found on the Amlogic GXBB, GXL and GXM SoCs. It converts
 * interlaced NV12 frames into progressive frames, emitting two progressive
 * frames for each interlaced input frame.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <linux/soc/amlogic/meson-canvas.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "meson-di.h"

#define MESON_DI_MIN_WIDTH	64
#define MESON_DI_MIN_HEIGHT	64
#define MESON_DI_MAX_WIDTH	1920
#define MESON_DI_MAX_HEIGHT	1088

#define MESON_DI_DEF_WIDTH	720
#define MESON_DI_DEF_HEIGHT	576

/* Noise-reduction working buffer: one full-size NV12 frame. */
#define MESON_DI_NR_SIZE	(MESON_DI_MAX_WIDTH * MESON_DI_MAX_HEIGHT * 3 / 2)
/* Motion buffer: one byte per luma sample is plenty for the motion map. */
#define MESON_DI_MTN_SIZE	(MESON_DI_MAX_WIDTH * MESON_DI_MAX_HEIGHT)
/* Contour buffer: same 4bpp motion-map geometry as the motion buffer. */
#define MESON_DI_CONT_SIZE	(MESON_DI_MAX_WIDTH * MESON_DI_MAX_HEIGHT)

static const struct meson_di_fmt meson_di_formats[] = {
	{ .fourcc = V4L2_PIX_FMT_NV12, .depth = 12 },
};

/* Debug: log format negotiation (G_FMT/TRY_FMT) to diagnose filter probing. */
static bool fmt_trace = true;
module_param(fmt_trace, bool, 0644);
MODULE_PARM_DESC(fmt_trace, "debug: log format negotiation");

static inline struct meson_di_ctx *file_to_ctx(struct file *file)
{
	return container_of(file_to_v4l2_fh(file), struct meson_di_ctx, fh);
}

static const struct meson_di_fmt *meson_di_find_fmt(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(meson_di_formats); i++)
		if (meson_di_formats[i].fourcc == fourcc)
			return &meson_di_formats[i];

	/*
	 * TRY_FMT/S_FMT must always return a valid format, so fall back to the
	 * first (and most widely supported) entry.
	 */
	return &meson_di_formats[0];
}

static struct v4l2_pix_format_mplane *
meson_di_get_pix(struct meson_di_ctx *ctx, enum v4l2_buf_type type)
{
	return V4L2_TYPE_IS_OUTPUT(type) ? &ctx->in : &ctx->out;
}

/*
 * The OUTPUT queue carries interlaced source frames and must preserve the
 * field order signalled by the client (top- or bottom-field-first). The
 * CAPTURE queue always carries progressive frames.
 */
static enum v4l2_field meson_di_out_field(enum v4l2_field field)
{
	if (field == V4L2_FIELD_INTERLACED_BT)
		return V4L2_FIELD_INTERLACED_BT;

	return V4L2_FIELD_INTERLACED_TB;
}

static void meson_di_fill_pix(struct v4l2_pix_format_mplane *pix,
			      const struct meson_di_fmt *fmt)
{
	u32 stride = ALIGN(pix->width, 32);

	pix->pixelformat = fmt->fourcc;

	/*
	 * NV12 in two planes: a full-size luma plane and a half-height
	 * interleaved chroma plane. The decoder hands each plane out as a
	 * separate DRM object, which is why the multi-planar API is used.
	 */
	pix->num_planes = 2;

	pix->plane_fmt[0].bytesperline = stride;
	pix->plane_fmt[0].sizeimage = stride * pix->height;
	pix->plane_fmt[1].bytesperline = stride;
	pix->plane_fmt[1].sizeimage = stride * pix->height / 2;
}

/*
 * mem2mem operations
 */

/* Debug: abort a job whose completion interrupt never arrives. */
#define MESON_DI_JOB_TIMEOUT_MS		200

static void meson_di_job_timeout(struct timer_list *t)
{
	struct meson_di *di = timer_container_of(di, t, job_timer);
	struct meson_di_ctx *ctx = di->curr;
	struct vb2_v4l2_buffer *src;

	if (!ctx)
		return;

	dev_warn(di->dev, "di: job timeout in phase %d\n", di->phase);
	meson_di_hw_dump(di);
	meson_di_hw_stop(di);

	if (di->cur_dst)
		v4l2_m2m_buf_done(di->cur_dst, VB2_BUF_STATE_ERROR);
	src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	if (src)
		v4l2_m2m_buf_done(src, VB2_BUF_STATE_ERROR);

	di->cur_dst = NULL;
	di->cur_src = NULL;
	di->phase = MESON_DI_PHASE_IDLE;
	di->curr = NULL;
	v4l2_m2m_job_finish(di->m2m_dev, ctx->fh.m2m_ctx);
}

static void meson_di_device_run(void *priv)
{
	struct meson_di_ctx *ctx = priv;
	struct meson_di *di = ctx->di;
	struct vb2_v4l2_buffer *src;

	/*
	 * The mem2mem framework serializes jobs, so only one device_run runs
	 * at a time and it does not overlap the interrupt handlers of the
	 * previous job (which end it via v4l2_m2m_job_finish).
	 */
	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!src) {
		v4l2_m2m_job_finish(di->m2m_dev, ctx->fh.m2m_ctx);
		return;
	}

	if (!ctx->streaming) {
		meson_di_hw_setup(ctx);
		ctx->streaming = true;
	}

	di->curr = ctx;
	di->cur_src = src;
	di->cur_dst = NULL;
	di->phase = MESON_DI_PHASE_PRE;

	/*
	 * Kick the pre stage (motion detection). Completion arrives on the
	 * "de" interrupt, which then triggers the post (blend) stage.
	 */
	meson_di_hw_pre(ctx, src);
	mod_timer(&di->job_timer,
		  jiffies + msecs_to_jiffies(MESON_DI_JOB_TIMEOUT_MS));
}

static int meson_di_job_ready(void *priv)
{
	struct meson_di_ctx *ctx = priv;

	/*
	 * Producing two CAPTURE frames per OUTPUT frame requires one source
	 * and at least two destination buffers to be queued.
	 */
	if (v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) < 1 ||
	    v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) < 2)
		return 0;

	return 1;
}

static const struct v4l2_m2m_ops meson_di_m2m_ops = {
	.device_run = meson_di_device_run,
	.job_ready = meson_di_job_ready,
};

/* "de" interrupt: pre stage complete, start the first output field. */
static irqreturn_t meson_di_de_isr(int irq, void *priv)
{
	struct meson_di *di = priv;
	struct meson_di_ctx *ctx = di->curr;
	struct vb2_v4l2_buffer *dst;

	if (!meson_di_hw_pre_done(di))
		return IRQ_NONE;

	if (!ctx || di->phase != MESON_DI_PHASE_PRE)
		return IRQ_HANDLED;

	timer_delete(&di->job_timer);

	dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	if (!dst) {
		struct vb2_v4l2_buffer *src;

		/* No capture buffer available: abort this frame. */
		meson_di_hw_stop(di);
		src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		if (src)
			v4l2_m2m_buf_done(src, VB2_BUF_STATE_ERROR);
		di->cur_src = NULL;
		di->phase = MESON_DI_PHASE_IDLE;
		di->curr = NULL;
		v4l2_m2m_job_finish(di->m2m_dev, ctx->fh.m2m_ctx);
		return IRQ_HANDLED;
	}

	di->cur_dst = dst;
	di->phase = MESON_DI_PHASE_POST0;
	meson_di_hw_post(ctx, dst, ctx->field == V4L2_FIELD_INTERLACED_BT);
	mod_timer(&di->job_timer,
		  jiffies + msecs_to_jiffies(MESON_DI_JOB_TIMEOUT_MS));

	return IRQ_HANDLED;
}

static void meson_di_finish_dst(struct meson_di *di)
{
	struct meson_di_ctx *ctx = di->curr;

	if (!di->cur_dst)
		return;

	di->cur_dst->sequence = ctx->sequence_cap++;
	if (di->cur_src) {
		di->cur_dst->vb2_buf.timestamp = di->cur_src->vb2_buf.timestamp;
		di->cur_dst->timecode = di->cur_src->timecode;
	}
	di->cur_dst->field = V4L2_FIELD_NONE;
	v4l2_m2m_buf_done(di->cur_dst, VB2_BUF_STATE_DONE);
	di->cur_dst = NULL;
}

/* "timerc" interrupt: post stage complete, deliver a progressive field. */
static irqreturn_t meson_di_post_isr(int irq, void *priv)
{
	struct meson_di *di = priv;
	struct meson_di_ctx *ctx = di->curr;
	struct vb2_v4l2_buffer *src, *dst;

	if (!meson_di_hw_post_done(di))
		return IRQ_NONE;

	if (!ctx)
		return IRQ_HANDLED;

	timer_delete(&di->job_timer);

	meson_di_finish_dst(di);

	if (di->phase == MESON_DI_PHASE_POST0) {
		dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (dst) {
			di->cur_dst = dst;
			di->phase = MESON_DI_PHASE_POST1;
			meson_di_hw_post(ctx, dst,
					 ctx->field != V4L2_FIELD_INTERLACED_BT);
			mod_timer(&di->job_timer,
				  jiffies + msecs_to_jiffies(MESON_DI_JOB_TIMEOUT_MS));
			return IRQ_HANDLED;
		}
	}

	/* Both output fields done; retire the source and end the job. */
	src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	if (src) {
		src->sequence = ctx->sequence_out++;
		v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
	}

	di->cur_src = NULL;
	di->phase = MESON_DI_PHASE_IDLE;
	di->curr = NULL;
	v4l2_m2m_job_finish(di->m2m_dev, ctx->fh.m2m_ctx);

	return IRQ_HANDLED;
}

/*
 * videobuf2 operations
 */

static int meson_di_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *pix = meson_di_get_pix(ctx, vq->type);
	unsigned int i;

	if (*nplanes) {
		if (*nplanes != pix->num_planes)
			return -EINVAL;
		for (i = 0; i < pix->num_planes; i++)
			if (sizes[i] < pix->plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}

	*nplanes = pix->num_planes;
	for (i = 0; i < pix->num_planes; i++)
		sizes[i] = pix->plane_fmt[i].sizeimage;

	return 0;
}

static int meson_di_buf_prepare(struct vb2_buffer *vb)
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct v4l2_pix_format_mplane *pix = meson_di_get_pix(ctx, vb->vb2_queue->type);
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type))
		vbuf->field = ctx->field;
	else
		vbuf->field = V4L2_FIELD_NONE;

	for (i = 0; i < pix->num_planes; i++) {
		if (vb2_plane_size(vb, i) < pix->plane_fmt[i].sizeimage)
			return -EINVAL;
		vb2_set_plane_payload(vb, i, pix->plane_fmt[i].sizeimage);
	}

	return 0;
}

static void meson_di_buf_queue(struct vb2_buffer *vb)
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int meson_di_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vq);
	struct meson_di *di = ctx->di;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		ctx->sequence_out = 0;
		ctx->streaming = false;
	} else {
		ctx->sequence_cap = 0;
	}

	ret = pm_runtime_resume_and_get(di->dev);
	if (ret < 0)
		return ret;

	return 0;
}

static void meson_di_stop_streaming(struct vb2_queue *vq)
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vq);
	struct meson_di *di = ctx->di;
	struct vb2_v4l2_buffer *vbuf;

	/*
	 * If a job for this context is running, fence the hardware and wait
	 * for any in-flight interrupt handler to finish before reclaiming the
	 * buffer it removed from the queue, so the handler cannot touch freed
	 * state after this returns.
	 */
	if (di->curr == ctx) {
		timer_delete_sync(&di->job_timer);
		meson_di_hw_stop(di);
		synchronize_irq(di->irq_de);
		synchronize_irq(di->irq_post);

		if (di->cur_dst)
			v4l2_m2m_buf_done(di->cur_dst, VB2_BUF_STATE_ERROR);

		di->cur_dst = NULL;
		di->cur_src = NULL;
		di->phase = MESON_DI_PHASE_IDLE;
		di->curr = NULL;
	}

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		ctx->streaming = false;

	pm_runtime_put(di->dev);
}

static const struct vb2_ops meson_di_qops = {
	.queue_setup = meson_di_queue_setup,
	.buf_prepare = meson_di_buf_prepare,
	.buf_queue = meson_di_buf_queue,
	.start_streaming = meson_di_start_streaming,
	.stop_streaming = meson_di_stop_streaming,
};

static int meson_di_queue_init(void *priv, struct vb2_queue *src_vq,
			       struct vb2_queue *dst_vq)
{
	struct meson_di_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &meson_di_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->di->mutex;
	src_vq->dev = ctx->di->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &meson_di_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->di->mutex;
	dst_vq->dev = ctx->di->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

/*
 * V4L2 ioctl operations
 */

static int meson_di_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	strscpy(cap->driver, MESON_DI_NAME, sizeof(cap->driver));
	strscpy(cap->card, MESON_DI_NAME, sizeof(cap->card));
	strscpy(cap->bus_info, "platform:" MESON_DI_NAME, sizeof(cap->bus_info));

	return 0;
}

static int meson_di_enum_fmt(struct file *file, void *priv,
			     struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(meson_di_formats))
		return -EINVAL;

	f->pixelformat = meson_di_formats[f->index].fourcc;

	return 0;
}

static int meson_di_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);

	f->fmt.pix_mp = *meson_di_get_pix(ctx, f->type);

	if (fmt_trace)
		dev_info(ctx->di->dev,
			 "di: g_fmt type=%u ret=%p4cc %ux%u field=%u np=%u bpl=%u\n",
			 f->type, &f->fmt.pix_mp.pixelformat,
			 f->fmt.pix_mp.width, f->fmt.pix_mp.height,
			 f->fmt.pix_mp.field, f->fmt.pix_mp.num_planes,
			 f->fmt.pix_mp.plane_fmt[0].bytesperline);

	return 0;
}

static int meson_di_try_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	u32 req = pix->pixelformat;
	const struct meson_di_fmt *fmt = meson_di_find_fmt(pix->pixelformat);

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		pix->width = clamp_t(u32, pix->width,
				     MESON_DI_MIN_WIDTH, MESON_DI_MAX_WIDTH);
		pix->height = clamp_t(u32, pix->height,
				      MESON_DI_MIN_HEIGHT, MESON_DI_MAX_HEIGHT);
		pix->field = meson_di_out_field(pix->field);
	} else {
		/* The progressive output geometry follows the source. */
		pix->width = ctx->in.width;
		pix->height = ctx->in.height;
		pix->field = V4L2_FIELD_NONE;
	}

	meson_di_fill_pix(pix, fmt);

	if (fmt_trace)
		dev_info(ctx->di->dev,
			 "di: try_fmt type=%u req=%p4cc ret=%p4cc %ux%u field=%u np=%u bpl=%u\n",
			 f->type, &req, &pix->pixelformat, pix->width,
			 pix->height, pix->field, pix->num_planes,
			 pix->plane_fmt[0].bytesperline);

	return 0;
}

static int meson_di_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct vb2_queue *vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	int ret;

	if (vb2_is_busy(vq))
		return -EBUSY;

	ret = meson_di_try_fmt(file, priv, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		ctx->in = f->fmt.pix_mp;
		ctx->in_fmt = meson_di_find_fmt(f->fmt.pix_mp.pixelformat);
		ctx->field = f->fmt.pix_mp.field;

		/* Progressive output inherits the source geometry. */
		ctx->out = f->fmt.pix_mp;
		ctx->out.field = V4L2_FIELD_NONE;
		ctx->out_fmt = ctx->in_fmt;
		meson_di_fill_pix(&ctx->out, ctx->out_fmt);
	} else {
		ctx->out = f->fmt.pix_mp;
		ctx->out_fmt = meson_di_find_fmt(f->fmt.pix_mp.pixelformat);
	}

	return 0;
}

/*
 * The deinterlacer neither crops nor scales, so selection always covers the
 * whole frame. The handlers still need to exist: the ffmpeg filter issues
 * VIDIOC_S_SELECTION during setup and treats a missing ioctl as an error.
 */
static int meson_di_g_selection(struct file *file, void *priv,
				struct v4l2_selection *s)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct v4l2_pix_format_mplane *pix;

	if (V4L2_TYPE_IS_OUTPUT(s->type)) {
		switch (s->target) {
		case V4L2_SEL_TGT_CROP:
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP_BOUNDS:
			break;
		default:
			return -EINVAL;
		}
		pix = &ctx->in;
	} else {
		switch (s->target) {
		case V4L2_SEL_TGT_COMPOSE:
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
			break;
		default:
			return -EINVAL;
		}
		pix = &ctx->out;
	}

	s->r.left = 0;
	s->r.top = 0;
	s->r.width = pix->width;
	s->r.height = pix->height;

	return 0;
}

static int meson_di_s_selection(struct file *file, void *priv,
				struct v4l2_selection *s)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct v4l2_pix_format_mplane *pix;

	if (V4L2_TYPE_IS_OUTPUT(s->type)) {
		if (s->target != V4L2_SEL_TGT_CROP)
			return -EINVAL;
		pix = &ctx->in;
	} else {
		if (s->target != V4L2_SEL_TGT_COMPOSE)
			return -EINVAL;
		pix = &ctx->out;
	}

	/* Cropping and scaling are not supported: report the full frame. */
	s->r.left = 0;
	s->r.top = 0;
	s->r.width = pix->width;
	s->r.height = pix->height;

	return 0;
}

static const struct v4l2_ioctl_ops meson_di_ioctl_ops = {
	.vidioc_querycap = meson_di_querycap,

	.vidioc_enum_fmt_vid_cap = meson_di_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = meson_di_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = meson_di_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = meson_di_s_fmt,

	.vidioc_enum_fmt_vid_out = meson_di_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane = meson_di_g_fmt,
	.vidioc_try_fmt_vid_out_mplane = meson_di_try_fmt,
	.vidioc_s_fmt_vid_out_mplane = meson_di_s_fmt,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection = meson_di_g_selection,
	.vidioc_s_selection = meson_di_s_selection,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/*
 * V4L2 file operations
 */

static void meson_di_set_default_fmt(struct meson_di_ctx *ctx)
{
	struct v4l2_pix_format_mplane pix = {
		.width = MESON_DI_DEF_WIDTH,
		.height = MESON_DI_DEF_HEIGHT,
	};

	ctx->in_fmt = &meson_di_formats[0];
	ctx->out_fmt = &meson_di_formats[0];
	ctx->field = V4L2_FIELD_INTERLACED_TB;

	pix.field = V4L2_FIELD_INTERLACED_TB;
	meson_di_fill_pix(&pix, ctx->in_fmt);
	ctx->in = pix;

	pix.field = V4L2_FIELD_NONE;
	ctx->out = pix;
}

static int meson_di_open(struct file *file)
{
	struct meson_di *di = video_drvdata(file);
	struct meson_di_ctx *ctx;
	int ret;

	ctx = kzalloc_obj(*ctx);
	if (!ctx)
		return -ENOMEM;

	ctx->di = di;
	meson_di_set_default_fmt(ctx);

	if (mutex_lock_interruptible(&di->mutex)) {
		kfree(ctx);
		return -ERESTARTSYS;
	}

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(di->m2m_dev, ctx,
					    &meson_di_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		mutex_unlock(&di->mutex);
		kfree(ctx);
		return ret;
	}

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	v4l2_fh_add(&ctx->fh, file);

	mutex_unlock(&di->mutex);

	return 0;
}

static int meson_di_release(struct file *file)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct meson_di *di = ctx->di;

	mutex_lock(&di->mutex);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	mutex_unlock(&di->mutex);

	return 0;
}

static const struct v4l2_file_operations meson_di_fops = {
	.owner = THIS_MODULE,
	.open = meson_di_open,
	.release = meson_di_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static const struct video_device meson_di_videodev = {
	.name = MESON_DI_NAME,
	.fops = &meson_di_fops,
	.ioctl_ops = &meson_di_ioctl_ops,
	.minor = -1,
	.release = video_device_release,
	.vfl_dir = VFL_DIR_M2M,
	.device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING,
};

/*
 * Platform driver
 */

static int meson_di_probe(struct platform_device *pdev)
{
	struct meson_di *di;
	struct video_device *vfd;
	struct resource *res;
	unsigned int i;
	int ret;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &pdev->dev;
	di->match_data = of_device_get_match_data(&pdev->dev);
	mutex_init(&di->mutex);
	timer_setup(&di->job_timer, meson_di_job_timeout, 0);
	platform_set_drvdata(pdev, di);

	/*
	 * The DI registers are a sub-region of the shared VPU register space
	 * (VCBUS), which the display driver already claims, so the region
	 * cannot be requested exclusively - map it without reserving it.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	di->regs = devm_ioremap(di->dev, res->start, resource_size(res));
	if (!di->regs)
		return -ENOMEM;

	di->vpu_clkb = devm_clk_get(di->dev, "vpu_clkb");
	if (IS_ERR(di->vpu_clkb))
		return dev_err_probe(di->dev, PTR_ERR(di->vpu_clkb),
				     "failed to get vpu_clkb\n");
	clk_set_rate(di->vpu_clkb, 250000000);

	di->irq_de = platform_get_irq_byname(pdev, "de");
	if (di->irq_de < 0)
		return di->irq_de;

	ret = devm_request_irq(di->dev, di->irq_de, meson_di_de_isr, 0,
			       dev_name(di->dev), di);
	if (ret)
		return dev_err_probe(di->dev, ret, "failed to request de irq\n");

	di->irq_post = platform_get_irq_byname(pdev, "timerc");
	if (di->irq_post < 0)
		return di->irq_post;

	ret = devm_request_irq(di->dev, di->irq_post, meson_di_post_isr, 0,
			       dev_name(di->dev), di);
	if (ret)
		return dev_err_probe(di->dev, ret,
				     "failed to request timerc irq\n");

	di->canvas = meson_canvas_get(di->dev);
	if (IS_ERR(di->canvas))
		return dev_err_probe(di->dev, PTR_ERR(di->canvas),
				     "failed to get canvas provider\n");

	for (i = 0; i < MESON_DI_CANVAS_NUM; i++) {
		ret = meson_canvas_alloc(di->canvas, &di->canvas_idx[i]);
		if (ret)
			goto err_canvas;
	}

	di->nr_size = MESON_DI_NR_SIZE;
	di->nr_buf = dma_alloc_coherent(di->dev, di->nr_size, &di->nr_dma,
					GFP_KERNEL);
	if (!di->nr_buf) {
		ret = -ENOMEM;
		goto err_canvas;
	}

	di->mtn_size = MESON_DI_MTN_SIZE;
	di->mtn_buf = dma_alloc_coherent(di->dev, di->mtn_size, &di->mtn_dma,
					 GFP_KERNEL);
	if (!di->mtn_buf) {
		ret = -ENOMEM;
		goto err_nr;
	}

	di->cont_size = MESON_DI_CONT_SIZE;
	di->cont_buf = dma_alloc_coherent(di->dev, di->cont_size, &di->cont_dma,
					  GFP_KERNEL);
	if (!di->cont_buf) {
		ret = -ENOMEM;
		goto err_mtn;
	}

	ret = v4l2_device_register(di->dev, &di->v4l2_dev);
	if (ret)
		goto err_dma;

	vfd = video_device_alloc();
	if (!vfd) {
		ret = -ENOMEM;
		goto err_v4l2;
	}

	*vfd = meson_di_videodev;
	vfd->lock = &di->mutex;
	vfd->v4l2_dev = &di->v4l2_dev;
	video_set_drvdata(vfd, di);
	di->vfd = vfd;

	di->m2m_dev = v4l2_m2m_init(&meson_di_m2m_ops);
	if (IS_ERR(di->m2m_dev)) {
		ret = PTR_ERR(di->m2m_dev);
		goto err_vfd;
	}

	pm_runtime_enable(di->dev);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_m2m;

	v4l2_info(&di->v4l2_dev, "registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));

	return 0;

err_m2m:
	pm_runtime_disable(di->dev);
	v4l2_m2m_release(di->m2m_dev);
err_vfd:
	video_device_release(vfd);
err_v4l2:
	v4l2_device_unregister(&di->v4l2_dev);
err_dma:
	dma_free_coherent(di->dev, di->cont_size, di->cont_buf, di->cont_dma);
err_mtn:
	dma_free_coherent(di->dev, di->mtn_size, di->mtn_buf, di->mtn_dma);
err_nr:
	dma_free_coherent(di->dev, di->nr_size, di->nr_buf, di->nr_dma);
err_canvas:
	while (i--)
		meson_canvas_free(di->canvas, di->canvas_idx[i]);

	return ret;
}

static void meson_di_remove(struct platform_device *pdev)
{
	struct meson_di *di = platform_get_drvdata(pdev);
	unsigned int i;

	video_unregister_device(di->vfd);
	timer_delete_sync(&di->job_timer);
	pm_runtime_disable(di->dev);
	v4l2_m2m_release(di->m2m_dev);
	v4l2_device_unregister(&di->v4l2_dev);
	dma_free_coherent(di->dev, di->cont_size, di->cont_buf, di->cont_dma);
	dma_free_coherent(di->dev, di->mtn_size, di->mtn_buf, di->mtn_dma);
	dma_free_coherent(di->dev, di->nr_size, di->nr_buf, di->nr_dma);
	for (i = 0; i < MESON_DI_CANVAS_NUM; i++)
		meson_canvas_free(di->canvas, di->canvas_idx[i]);
}

static int meson_di_runtime_resume(struct device *dev)
{
	struct meson_di *di = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(di->vpu_clkb);
	if (ret)
		return ret;

	meson_di_hw_init(di);

	return 0;
}

static int meson_di_runtime_suspend(struct device *dev)
{
	struct meson_di *di = dev_get_drvdata(dev);

	meson_di_hw_disable(di);
	clk_disable_unprepare(di->vpu_clkb);

	return 0;
}

static const struct dev_pm_ops meson_di_pm_ops = {
	RUNTIME_PM_OPS(meson_di_runtime_suspend, meson_di_runtime_resume, NULL)
};

static const struct meson_di_match_data meson_di_gxbb_data = {
	.hw_version = 2,
};

static const struct meson_di_match_data meson_di_gxl_data = {
	.hw_version = 3,
};

static const struct of_device_id meson_di_of_match[] = {
	{ .compatible = "amlogic,gxbb-di", .data = &meson_di_gxbb_data },
	{ .compatible = "amlogic,gxl-di", .data = &meson_di_gxl_data },
	{ .compatible = "amlogic,gxm-di", .data = &meson_di_gxl_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_di_of_match);

static struct platform_driver meson_di_driver = {
	.probe = meson_di_probe,
	.remove = meson_di_remove,
	.driver = {
		.name = MESON_DI_NAME,
		.of_match_table = meson_di_of_match,
		.pm = pm_ptr(&meson_di_pm_ops),
	},
};
module_platform_driver(meson_di_driver);

MODULE_DESCRIPTION("Amlogic GX SoC Video Deinterlacer driver");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
MODULE_LICENSE("GPL");
