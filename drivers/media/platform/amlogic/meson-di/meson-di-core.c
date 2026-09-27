// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/amlogic/meson-canvas.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include "meson-di.h"

#define MESON_DI_NAME		"meson-di"
#define MESON_DI_CLKB_RATE	666666666
#define MESON_DI_TIMEOUT_MS	100

static const struct meson_di_fmt meson_di_formats[] = {
	{ .fourcc = V4L2_PIX_FMT_NV12, .num_planes = 1 },
	{ .fourcc = V4L2_PIX_FMT_NV12M, .num_planes = 2 },
};

static const struct meson_di_fmt *meson_di_find_fmt(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(meson_di_formats); i++)
		if (meson_di_formats[i].fourcc == fourcc)
			return &meson_di_formats[i];

	return NULL;
}

static inline struct meson_di_ctx *file_to_ctx(struct file *file)
{
	return container_of(file_to_v4l2_fh(file), struct meson_di_ctx, fh);
}

static bool meson_di_field_valid(enum v4l2_field field)
{
	return field == V4L2_FIELD_INTERLACED ||
	       field == V4L2_FIELD_INTERLACED_TB ||
	       field == V4L2_FIELD_INTERLACED_BT;
}

static bool meson_di_bottom_first(enum v4l2_field field, unsigned int height)
{
	if (field == V4L2_FIELD_INTERLACED_BT)
		return true;
	if (field == V4L2_FIELD_INTERLACED)
		return height == 480;
	return false;
}

static void meson_di_fill_pix(struct v4l2_pix_format_mplane *pix,
			      const struct meson_di_fmt *fmt, bool capture)
{
	unsigned int bpl;

	pix->width = clamp(ALIGN(pix->width, 2), MESON_DI_MIN_WIDTH,
			   MESON_DI_MAX_WIDTH);
	pix->height = clamp(ALIGN(pix->height, 4), MESON_DI_MIN_HEIGHT,
			    MESON_DI_MAX_HEIGHT);
	pix->pixelformat = fmt->fourcc;
	pix->num_planes = fmt->num_planes;

	bpl = pix->plane_fmt[0].bytesperline;
	if (capture || bpl < pix->width || bpl > MESON_DI_MAX_STRIDE)
		bpl = ALIGN(pix->width, 64);
	else
		bpl = ALIGN(bpl, 32);

	memset(pix->plane_fmt, 0, sizeof(pix->plane_fmt));
	pix->plane_fmt[0].bytesperline = bpl;
	if (fmt->num_planes == 1) {
		pix->plane_fmt[0].sizeimage = bpl * pix->height * 3 / 2;
	} else {
		pix->plane_fmt[0].sizeimage = bpl * pix->height;
		pix->plane_fmt[1].bytesperline = bpl;
		pix->plane_fmt[1].sizeimage = bpl * pix->height / 2;
	}
}

static int meson_di_local_alloc(struct meson_di_ctx *ctx)
{
	struct meson_di *di = ctx->di;
	unsigned int w = ctx->out.pix.width, fh = ctx->out.pix.height / 2;
	unsigned int nr_stride = ALIGN(w * 2, 64);
	unsigned int mtn_stride = ALIGN(w / 2, 64);
	size_t nr = PAGE_ALIGN(nr_stride * fh);
	size_t mtn = PAGE_ALIGN(mtn_stride * fh);
	size_t slot = nr + 2 * mtn;
	size_t size = MESON_DI_LOCAL_BUFS * slot;
	unsigned int i;

	if (!ctx->loc_virt || ctx->loc_size < size) {
		if (ctx->loc_virt)
			dma_free_coherent(di->dev, ctx->loc_size,
					  ctx->loc_virt, ctx->loc_dma);
		ctx->loc_virt = dma_alloc_coherent(di->dev, size,
						   &ctx->loc_dma, GFP_KERNEL);
		if (!ctx->loc_virt) {
			ctx->loc_size = 0;
			return -ENOMEM;
		}
		ctx->loc_size = size;
	}

	ctx->nr_stride = nr_stride;
	ctx->mtn_stride = mtn_stride;
	for (i = 0; i < MESON_DI_LOCAL_BUFS; i++) {
		ctx->loc[i].nr = ctx->loc_dma + i * slot;
		ctx->loc[i].mtn = ctx->loc[i].nr + nr;
		ctx->loc[i].cnt = ctx->loc[i].mtn + mtn;
	}

	return 0;
}

static void meson_di_local_free(struct meson_di_ctx *ctx)
{
	if (!ctx->loc_virt)
		return;

	dma_free_coherent(ctx->di->dev, ctx->loc_size, ctx->loc_virt,
			  ctx->loc_dma);
	ctx->loc_virt = NULL;
	ctx->loc_size = 0;
}

static void meson_di_plane_addrs(struct vb2_buffer *vb,
				 const struct v4l2_pix_format_mplane *pix,
				 dma_addr_t *y, dma_addr_t *uv)
{
	*y = vb2_dma_contig_plane_dma_addr(vb, 0);
	if (pix->num_planes == 2)
		*uv = vb2_dma_contig_plane_dma_addr(vb, 1);
	else
		*uv = *y + pix->plane_fmt[0].bytesperline * pix->height;
}

static int meson_di_wait(struct meson_di *di, struct completion *done,
			 const char *stage)
{
	if (wait_for_completion_timeout(done,
					msecs_to_jiffies(MESON_DI_TIMEOUT_MS)))
		return 0;

	dev_err(di->dev, "%s timeout: intr %08x pre %08x post %08x arb %08x\n",
		stage, di_read(di, DI_INTR_CTRL), di_read(di, DI_PRE_CTRL),
		di_read(di, DI_POST_CTRL), di_read(di, DI_ARB_DBG_STAT_L1C1));
	meson_di_hw_recover(di, done == &di->pre_done);

	return -ETIMEDOUT;
}

static int meson_di_run(struct meson_di *di, struct meson_di_ctx *ctx,
			const struct meson_di_pre *pre,
			const struct meson_di_post *post)
{
	int ret, err;

	ret = meson_di_hw_pre(di, ctx, &pre[0]);
	if (!ret)
		ret = meson_di_wait(di, &di->pre_done, "pre");
	if (ret)
		return ret;

	ret = meson_di_hw_post(di, ctx, &post[0]);
	if (ret)
		return ret;

	ret = meson_di_hw_pre(di, ctx, &pre[1]);
	if (!ret)
		ret = meson_di_wait(di, &di->pre_done, "pre");
	err = meson_di_wait(di, &di->post_done, "post");
	if (ret || err)
		return ret ? ret : err;

	ret = meson_di_hw_post(di, ctx, &post[1]);
	if (!ret)
		ret = meson_di_wait(di, &di->post_done, "post");

	return ret;
}

static struct meson_di_local *meson_di_loc(struct meson_di_ctx *ctx,
					   unsigned int seq)
{
	return &ctx->loc[seq % MESON_DI_LOCAL_BUFS];
}

static void meson_di_setup_post(struct meson_di_ctx *ctx,
				struct meson_di_post *p, unsigned int seq)
{
	p->cur = meson_di_loc(ctx, seq);
	if (seq >= 2) {
		p->prev = meson_di_loc(ctx, seq - 1);
		p->next = meson_di_loc(ctx, seq + 1);
	}
}

static void meson_di_work(struct work_struct *work)
{
	struct meson_di *di = container_of(work, struct meson_di, work);
	struct meson_di_ctx *ctx = di->cur_ctx;
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct vb2_v4l2_buffer *src, *dst[2];
	struct meson_di_pre pre[2] = {};
	struct meson_di_post post[2] = {};
	enum vb2_buffer_state state;
	unsigned int seq = ctx->seq;
	enum v4l2_field field;
	bool bottom_first;
	unsigned int i;
	int ret = -ECANCELED;

	src = v4l2_m2m_src_buf_remove(m2m_ctx);
	dst[0] = v4l2_m2m_dst_buf_remove(m2m_ctx);
	dst[1] = v4l2_m2m_dst_buf_remove(m2m_ctx);

	field = src->field;
	if (!meson_di_field_valid(field))
		field = ctx->field;
	bottom_first = meson_di_bottom_first(field, ctx->out.pix.height);

	for (i = 0; i < 2; i++) {
		unsigned int t = seq + i;

		pre[i].in_stride = ctx->out.pix.plane_fmt[0].bytesperline;
		meson_di_plane_addrs(&src->vb2_buf, &ctx->out.pix,
				     &pre[i].in_y, &pre[i].in_uv);
		pre[i].wr = meson_di_loc(ctx, t);
		pre[i].chan2 = t >= 1 ? meson_di_loc(ctx, t - 1) : NULL;
		pre[i].mem = t >= 2 ? meson_di_loc(ctx, t - 2) : NULL;
		pre[i].seq = t;
		meson_di_loc(ctx, t)->bottom = bottom_first ^ i;

		post[i].out_stride = ctx->cap.pix.plane_fmt[0].bytesperline;
		meson_di_plane_addrs(&dst[i]->vb2_buf, &ctx->cap.pix,
				     &post[i].out_y, &post[i].out_uv);
	}

	meson_di_setup_post(ctx, &post[0], seq ? seq - 1 : 0);
	meson_di_setup_post(ctx, &post[1], seq);

	if (!ctx->aborting) {
		meson_di_hw_size(di, ctx->out.pix.width, ctx->out.pix.height);
		ret = meson_di_run(di, ctx, pre, post);
	}

	ctx->seq = ret ? 0 : seq + 2;

	state = ret ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE;
	for (i = 0; i < 2; i++) {
		v4l2_m2m_buf_copy_metadata(src, dst[i]);
		dst[i]->field = V4L2_FIELD_NONE;
		v4l2_m2m_buf_done(dst[i], state);
	}
	v4l2_m2m_buf_done(src, state);
	v4l2_m2m_job_finish(di->m2m_dev, m2m_ctx);
}

static void meson_di_device_run(void *priv)
{
	struct meson_di_ctx *ctx = priv;
	struct meson_di *di = ctx->di;

	di->cur_ctx = ctx;
	schedule_work(&di->work);
}

static int meson_di_job_ready(void *priv)
{
	struct meson_di_ctx *ctx = priv;

	return v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) >= 1 &&
	       v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) >= 2;
}

static void meson_di_job_abort(void *priv)
{
	struct meson_di_ctx *ctx = priv;

	ctx->aborting = true;
}

static const struct v4l2_m2m_ops meson_di_m2m_ops = {
	.device_run = meson_di_device_run,
	.job_ready = meson_di_job_ready,
	.job_abort = meson_di_job_abort,
};

static int meson_di_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *pix = V4L2_TYPE_IS_OUTPUT(vq->type) ?
					     &ctx->out.pix : &ctx->cap.pix;
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
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane *pix;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		pix = &ctx->out.pix;
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = ctx->field;
		if (!meson_di_field_valid(vbuf->field))
			return -EINVAL;
	} else {
		pix = &ctx->cap.pix;
		vbuf->field = V4L2_FIELD_NONE;
	}

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

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static int meson_di_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vq);
	struct meson_di *di = ctx->di;
	struct vb2_v4l2_buffer *vbuf;
	int ret;

	ret = pm_runtime_resume_and_get(di->dev);
	if (ret)
		goto err;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		ret = meson_di_local_alloc(ctx);
		if (ret) {
			pm_runtime_put(di->dev);
			goto err;
		}
	}

	ctx->aborting = false;
	ctx->seq = 0;

	return 0;

err:
	while ((vbuf = V4L2_TYPE_IS_OUTPUT(vq->type) ?
			v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx) :
			v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void meson_di_stop_streaming(struct vb2_queue *vq)
{
	struct meson_di_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	while ((vbuf = V4L2_TYPE_IS_OUTPUT(vq->type) ?
			v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx) :
			v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);

	pm_runtime_put(ctx->di->dev);
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
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &meson_di_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->di->mutex;
	src_vq->dev = ctx->di->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &meson_di_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->di->mutex;
	dst_vq->dev = ctx->di->dev;

	return vb2_queue_init(dst_vq);
}

static int meson_di_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	strscpy(cap->driver, MESON_DI_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Amlogic Video Deinterlacer", sizeof(cap->card));

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

static int meson_di_enum_framesizes(struct file *file, void *priv,
				    struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index || !meson_di_find_fmt(fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = MESON_DI_MIN_WIDTH;
	fsize->stepwise.max_width = MESON_DI_MAX_WIDTH;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.min_height = MESON_DI_MIN_HEIGHT;
	fsize->stepwise.max_height = MESON_DI_MAX_HEIGHT;
	fsize->stepwise.step_height = 4;

	return 0;
}

static int meson_di_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	bool out = V4L2_TYPE_IS_OUTPUT(f->type);

	f->fmt.pix_mp = out ? ctx->out.pix : ctx->cap.pix;
	f->fmt.pix_mp.field = out ? ctx->field : V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = ctx->colorspace;
	f->fmt.pix_mp.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix_mp.quantization = ctx->quantization;
	f->fmt.pix_mp.xfer_func = ctx->xfer_func;

	return 0;
}

static int meson_di_try_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	const struct meson_di_fmt *fmt = meson_di_find_fmt(pix->pixelformat);
	bool out = V4L2_TYPE_IS_OUTPUT(f->type);

	if (!fmt)
		fmt = &meson_di_formats[0];

	if (!out) {
		pix->width = ctx->out.pix.width;
		pix->height = ctx->out.pix.height;
		pix->field = V4L2_FIELD_NONE;
		pix->colorspace = ctx->colorspace;
		pix->ycbcr_enc = ctx->ycbcr_enc;
		pix->quantization = ctx->quantization;
		pix->xfer_func = ctx->xfer_func;
	} else if (!meson_di_field_valid(pix->field)) {
		pix->field = V4L2_FIELD_INTERLACED_TB;
	}

	meson_di_fill_pix(pix, fmt, !out);

	return 0;
}

static int meson_di_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct meson_di_ctx *ctx = file_to_ctx(file);
	struct vb2_queue *vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	int ret;

	if (vb2_is_busy(vq))
		return -EBUSY;

	ret = meson_di_try_fmt(file, priv, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		ctx->out.pix = *pix;
		ctx->out.fmt = meson_di_find_fmt(pix->pixelformat);
		ctx->field = pix->field;
		ctx->colorspace = pix->colorspace;
		ctx->ycbcr_enc = pix->ycbcr_enc;
		ctx->quantization = pix->quantization;
		ctx->xfer_func = pix->xfer_func;

		ctx->cap.pix.width = pix->width;
		ctx->cap.pix.height = pix->height;
		meson_di_fill_pix(&ctx->cap.pix, ctx->cap.fmt, true);
	} else {
		ctx->cap.pix = *pix;
		ctx->cap.fmt = meson_di_find_fmt(pix->pixelformat);
	}

	return 0;
}

static const struct v4l2_ioctl_ops meson_di_ioctl_ops = {
	.vidioc_querycap = meson_di_querycap,
	.vidioc_enum_framesizes = meson_di_enum_framesizes,

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

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static void meson_di_default_fmt(struct meson_di_ctx *ctx)
{
	ctx->out.fmt = &meson_di_formats[0];
	ctx->cap.fmt = &meson_di_formats[0];
	ctx->out.pix.width = 1920;
	ctx->out.pix.height = 1080;
	ctx->cap.pix.width = 1920;
	ctx->cap.pix.height = 1080;
	ctx->field = V4L2_FIELD_INTERLACED_TB;
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	meson_di_fill_pix(&ctx->out.pix, ctx->out.fmt, false);
	meson_di_fill_pix(&ctx->cap.pix, ctx->cap.fmt, true);
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
	meson_di_default_fmt(ctx);

	if (mutex_lock_interruptible(&di->mutex)) {
		kfree(ctx);
		return -ERESTARTSYS;
	}

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(di->m2m_dev, ctx,
					    meson_di_queue_init);
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
	meson_di_local_free(ctx);
	mutex_unlock(&di->mutex);
	kfree(ctx);

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

static irqreturn_t meson_di_pre_isr(int irq, void *priv)
{
	return meson_di_hw_pre_irq(priv);
}

static irqreturn_t meson_di_post_isr(int irq, void *priv)
{
	return meson_di_hw_post_irq(priv);
}

static int meson_di_runtime_resume(struct device *dev)
{
	struct meson_di *di = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(di->intr);
	if (ret)
		return ret;

	ret = clk_prepare_enable(di->clkb);
	if (ret) {
		clk_disable_unprepare(di->intr);
		return ret;
	}

	meson_di_hw_init(di);
	enable_irq(di->irq_pre);
	enable_irq(di->irq_post);

	return 0;
}

static int meson_di_runtime_suspend(struct device *dev)
{
	struct meson_di *di = dev_get_drvdata(dev);

	disable_irq(di->irq_pre);
	disable_irq(di->irq_post);
	meson_di_hw_stop(di);
	clk_disable_unprepare(di->clkb);
	clk_disable_unprepare(di->intr);

	return 0;
}

static const struct dev_pm_ops meson_di_pm_ops = {
	RUNTIME_PM_OPS(meson_di_runtime_suspend, meson_di_runtime_resume,
		       NULL)
};

static int meson_di_canvases(struct meson_di *di)
{
	unsigned int i;
	int ret;

	di->canvas = meson_canvas_get(di->dev);
	if (IS_ERR(di->canvas))
		return PTR_ERR(di->canvas);

	for (i = 0; i < MESON_DI_CVS_NUM; i++) {
		ret = meson_canvas_alloc(di->canvas, &di->cvs[i]);
		if (ret) {
			while (i--)
				meson_canvas_free(di->canvas, di->cvs[i]);
			return ret;
		}
	}

	return 0;
}

static void meson_di_canvases_free(struct meson_di *di)
{
	unsigned int i;

	for (i = 0; i < MESON_DI_CVS_NUM; i++)
		meson_canvas_free(di->canvas, di->cvs[i]);
}

static int meson_di_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct video_device *vfd;
	struct meson_di *di;
	struct resource *res;
	int ret;

	di = devm_kzalloc(dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = dev;
	di->data = of_device_get_match_data(dev);
	mutex_init(&di->mutex);
	spin_lock_init(&di->intr_lock);
	init_completion(&di->pre_done);
	init_completion(&di->post_done);
	INIT_WORK(&di->work, meson_di_work);
	platform_set_drvdata(pdev, di);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	di->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!di->base)
		return -ENOMEM;

	di->hhi = syscon_regmap_lookup_by_phandle(dev->of_node,
						  "amlogic,hhi-sysctrl");
	if (IS_ERR(di->hhi))
		return dev_err_probe(dev, PTR_ERR(di->hhi),
				     "failed to get HHI regmap\n");

	di->clkb = devm_clk_get(dev, "vpu_clkb");
	if (IS_ERR(di->clkb))
		return dev_err_probe(dev, PTR_ERR(di->clkb),
				     "failed to get vpu_clkb\n");

	di->intr = devm_clk_get(dev, "vpu_intr");
	if (IS_ERR(di->intr))
		return dev_err_probe(dev, PTR_ERR(di->intr),
				     "failed to get vpu_intr\n");

	ret = clk_set_rate(di->clkb, MESON_DI_CLKB_RATE);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set vpu_clkb rate\n");

	di->irq_pre = platform_get_irq_byname(pdev, "pre");
	if (di->irq_pre < 0)
		return di->irq_pre;
	ret = devm_request_irq(dev, di->irq_pre, meson_di_pre_isr,
			       IRQF_NO_AUTOEN, "meson-di-pre", di);
	if (ret)
		return ret;

	di->irq_post = platform_get_irq_byname(pdev, "post");
	if (di->irq_post < 0)
		return di->irq_post;
	ret = devm_request_irq(dev, di->irq_post, meson_di_post_isr,
			       IRQF_NO_AUTOEN, "meson-di-post", di);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = meson_di_canvases(di);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get canvases\n");

	ret = v4l2_device_register(dev, &di->v4l2_dev);
	if (ret)
		goto err_canvas;

	di->m2m_dev = v4l2_m2m_init(&meson_di_m2m_ops);
	if (IS_ERR(di->m2m_dev)) {
		ret = PTR_ERR(di->m2m_dev);
		goto err_v4l2;
	}

	pm_runtime_enable(dev);

	vfd = &di->vfd;
	strscpy(vfd->name, MESON_DI_NAME, sizeof(vfd->name));
	vfd->fops = &meson_di_fops;
	vfd->ioctl_ops = &meson_di_ioctl_ops;
	vfd->release = video_device_release_empty;
	vfd->lock = &di->mutex;
	vfd->v4l2_dev = &di->v4l2_dev;
	vfd->vfl_dir = VFL_DIR_M2M;
	vfd->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	video_set_drvdata(vfd, di);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_m2m;

	return 0;

err_m2m:
	pm_runtime_disable(dev);
	v4l2_m2m_release(di->m2m_dev);
err_v4l2:
	v4l2_device_unregister(&di->v4l2_dev);
err_canvas:
	meson_di_canvases_free(di);
	return ret;
}

static void meson_di_remove(struct platform_device *pdev)
{
	struct meson_di *di = platform_get_drvdata(pdev);

	video_unregister_device(&di->vfd);
	cancel_work_sync(&di->work);
	pm_runtime_disable(di->dev);
	v4l2_m2m_release(di->m2m_dev);
	v4l2_device_unregister(&di->v4l2_dev);
	meson_di_canvases_free(di);
}

static const struct meson_di_data meson_di_g12a_data = {
	.soc = MESON_DI_G12A,
};

static const struct meson_di_data meson_di_g12b_data = {
	.soc = MESON_DI_G12B,
};

static const struct meson_di_data meson_di_sm1_data = {
	.soc = MESON_DI_SM1,
};

static const struct of_device_id meson_di_match[] = {
	{ .compatible = "amlogic,g12a-di", .data = &meson_di_g12a_data },
	{ .compatible = "amlogic,g12b-di", .data = &meson_di_g12b_data },
	{ .compatible = "amlogic,sm1-di", .data = &meson_di_sm1_data },
	{ }
};
MODULE_DEVICE_TABLE(of, meson_di_match);

static struct platform_driver meson_di_driver = {
	.probe = meson_di_probe,
	.remove = meson_di_remove,
	.driver = {
		.name = MESON_DI_NAME,
		.of_match_table = meson_di_match,
		.pm = pm_ptr(&meson_di_pm_ops),
	},
};
module_platform_driver(meson_di_driver);

MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
MODULE_DESCRIPTION("Amlogic G12A/G12B/SM1 video deinterlacer");
MODULE_LICENSE("GPL");
