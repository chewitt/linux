// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Image Enhancement Processor (IEP) common V4L2 M2M framework
 *
 * Copyright (C) 2020 Alex Bee <knaerzche@gmail.com>
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 *
 * Common code shared between IEP and IEP2 deinterlace drivers.
 */

#include <linux/module.h>
#include <linux/videodev2.h>

#include "iep-common.h"

/* Format helpers */

struct rk_iep_fmt *rk_iep_fmt_find(struct rk_iep_fmt *fmts, unsigned int n,
				    u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (fmts[i].fourcc == pixelformat)
			return &fmts[i];
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(rk_iep_fmt_find);

bool rk_iep_check_pix_format(struct rk_iep_fmt *fmts, unsigned int n,
			     u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		if (fmts[i].fourcc == pixelformat)
			return true;

	return false;
}
EXPORT_SYMBOL_GPL(rk_iep_check_pix_format);

void rk_iep_prepare_format(struct v4l2_pix_format *pix_fmt,
			   struct rk_iep_fmt *fmts, unsigned int nfmts)
{
	unsigned int width, height;
	struct rk_iep_fmt *hw_fmt;

	hw_fmt = rk_iep_fmt_find(fmts, nfmts, pix_fmt->pixelformat);
	if (!hw_fmt) {
		hw_fmt = &fmts[0];
		pix_fmt->pixelformat = hw_fmt->fourcc;
	}

	width = ALIGN(clamp(pix_fmt->width, RK_IEP_MIN_WIDTH,
			    RK_IEP_MAX_WIDTH), 16);
	height = ALIGN(clamp(pix_fmt->height, RK_IEP_MIN_HEIGHT,
			     RK_IEP_MAX_HEIGHT), 16);

	pix_fmt->width = width;
	pix_fmt->height = height;
	pix_fmt->bytesperline = width;
	pix_fmt->sizeimage = height * (width * hw_fmt->depth) >> 3;
}
EXPORT_SYMBOL_GPL(rk_iep_prepare_format);

/* Buffer helpers */

struct vb2_v4l2_buffer *rk_iep_m2m_next_dst_buf(struct rk_iep_ctx *ctx)
{
	struct vb2_v4l2_buffer *dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* application has set a dst sequence: take it as start point */
	if (ctx->dst_sequence == 0 && dst_buf->sequence > 0)
		ctx->dst_sequence = dst_buf->sequence;

	dst_buf->sequence = ctx->dst_sequence++;

	return dst_buf;
}
EXPORT_SYMBOL_GPL(rk_iep_m2m_next_dst_buf);

void rk_iep_m2m_dst_bufs_done(struct rk_iep_ctx *ctx,
			      enum vb2_buffer_state state)
{
	if (ctx->dst0_buf) {
		v4l2_m2m_buf_done(ctx->dst0_buf, state);
		ctx->dst_buffs_done++;
		ctx->dst0_buf = NULL;
	}

	if (ctx->dst1_buf) {
		v4l2_m2m_buf_done(ctx->dst1_buf, state);
		ctx->dst_buffs_done++;
		ctx->dst1_buf = NULL;
	}
}
EXPORT_SYMBOL_GPL(rk_iep_m2m_dst_bufs_done);

/* VB2 queue ops */

int rk_iep_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
		       unsigned int *nplanes, unsigned int sizes[],
		       struct device *alloc_devs[])
{
	struct rk_iep_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt.pix;
	else
		pix_fmt = &ctx->dst_fmt.pix;

	if (*nplanes) {
		if (sizes[0] < pix_fmt->sizeimage)
			return -EINVAL;
	} else {
		sizes[0] = pix_fmt->sizeimage;
		*nplanes = 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_queue_setup);

int rk_iep_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rk_iep_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt.pix;
	else
		pix_fmt = &ctx->dst_fmt.pix;

	if (vb2_plane_size(vb, 0) < pix_fmt->sizeimage)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_buf_prepare);

void rk_iep_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rk_iep_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}
EXPORT_SYMBOL_GPL(rk_iep_buf_queue);

void rk_iep_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct rk_iep_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	do {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (vbuf)
			v4l2_m2m_buf_done(vbuf, state);
	} while (vbuf);

	if (V4L2_TYPE_IS_OUTPUT(vq->type) && ctx->prev_src_buf)
		v4l2_m2m_buf_done(ctx->prev_src_buf, state);
	else
		rk_iep_m2m_dst_bufs_done(ctx, state);
}
EXPORT_SYMBOL_GPL(rk_iep_queue_cleanup);

int rk_iep_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct rk_iep_ctx *ctx = vb2_get_drv_priv(vq);
	struct rk_iep_dev *iep_dev = ctx->iep_dev;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		ret = pm_runtime_get_sync(iep_dev->dev);
		if (ret < 0) {
			dev_err(iep_dev->dev, "Failed to enable module\n");
			goto err_runtime_get;
		}

		ctx->field_order_bff =
			ctx->src_fmt.pix.field == V4L2_FIELD_INTERLACED_BT;
		ctx->field_bff = ctx->field_order_bff;

		ctx->src_sequence = 0;
		ctx->dst_sequence = 0;

		ctx->prev_src_buf = NULL;

		ctx->dst0_buf = NULL;
		ctx->dst1_buf = NULL;
		ctx->dst_buffs_done = 0;

		ctx->job_abort = false;

		if (iep_dev->hw_init)
			iep_dev->hw_init(iep_dev);
	}

	return 0;

err_runtime_get:
	rk_iep_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);

	return ret;
}
EXPORT_SYMBOL_GPL(rk_iep_start_streaming);

void rk_iep_stop_streaming(struct vb2_queue *vq)
{
	struct rk_iep_ctx *ctx = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		pm_runtime_mark_last_busy(ctx->iep_dev->dev);
		pm_runtime_put_autosuspend(ctx->iep_dev->dev);
	}

	rk_iep_queue_cleanup(vq, VB2_BUF_STATE_ERROR);
}
EXPORT_SYMBOL_GPL(rk_iep_stop_streaming);

/* V4L2 IOCTL ops */

int rk_iep_querycap(struct file *file, void *priv,
		    struct v4l2_capability *cap)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);

	strscpy(cap->driver, iep_dev->name, sizeof(cap->driver));
	strscpy(cap->card, iep_dev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", iep_dev->name);

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_querycap);

int rk_iep_enum_fmt(struct file *file, void *priv,
		    struct v4l2_fmtdesc *f)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);

	if (f->index < iep_dev->num_formats) {
		f->pixelformat = iep_dev->formats[f->index].fourcc;
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(rk_iep_enum_fmt);

int rk_iep_enum_framesizes(struct file *file, void *priv,
			   struct v4l2_frmsizeenum *fsize)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);

	if (fsize->index != 0)
		return -EINVAL;

	if (!rk_iep_check_pix_format(iep_dev->formats, iep_dev->num_formats,
				     fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = RK_IEP_MIN_WIDTH;
	fsize->stepwise.max_width = RK_IEP_MAX_WIDTH;
	fsize->stepwise.step_width = 16;

	fsize->stepwise.min_height = RK_IEP_MIN_HEIGHT;
	fsize->stepwise.max_height = RK_IEP_MAX_HEIGHT;
	fsize->stepwise.step_height = 16;

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_enum_framesizes);

int rk_iep_g_fmt_vid_cap(struct file *file, void *priv,
			 struct v4l2_format *f)
{
	struct rk_iep_ctx *ctx = rk_iep_file2ctx(file);

	f->fmt.pix = ctx->dst_fmt.pix;

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_g_fmt_vid_cap);

int rk_iep_g_fmt_vid_out(struct file *file, void *priv,
			 struct v4l2_format *f)
{
	struct rk_iep_ctx *ctx = rk_iep_file2ctx(file);

	f->fmt.pix = ctx->src_fmt.pix;

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_g_fmt_vid_out);

int rk_iep_try_fmt_vid_cap(struct file *file, void *priv,
			   struct v4l2_format *f)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);

	f->fmt.pix.field = V4L2_FIELD_NONE;
	rk_iep_prepare_format(&f->fmt.pix, iep_dev->formats,
			      iep_dev->num_formats);

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_try_fmt_vid_cap);

int rk_iep_try_fmt_vid_out(struct file *file, void *priv,
			   struct v4l2_format *f)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);

	if (f->fmt.pix.field != V4L2_FIELD_INTERLACED_TB &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED_BT &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED)
		f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	rk_iep_prepare_format(&f->fmt.pix, iep_dev->formats,
			      iep_dev->num_formats);

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_try_fmt_vid_out);

int rk_iep_s_fmt_vid_out(struct file *file, void *priv,
			 struct v4l2_format *f)
{
	struct rk_iep_ctx *ctx = rk_iep_file2ctx(file);
	struct rk_iep_dev *iep_dev = ctx->iep_dev;
	struct vb2_queue *vq;
	int ret;

	ret = rk_iep_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ctx->src_fmt.pix = f->fmt.pix;
	ctx->src_fmt.hw_fmt = rk_iep_fmt_find(iep_dev->formats,
					       iep_dev->num_formats,
					       f->fmt.pix.pixelformat);
	ctx->src_fmt.y_stride = RK_IEP_Y_STRIDE(f->fmt.pix.width,
						  f->fmt.pix.height);
	ctx->src_fmt.uv_stride = RK_IEP_UV_STRIDE(f->fmt.pix.width,
						    f->fmt.pix.height,
						    ctx->src_fmt.hw_fmt->uv_factor);

	/* Propagate colorspace information to capture */
	ctx->dst_fmt.pix.colorspace = f->fmt.pix.colorspace;
	ctx->dst_fmt.pix.xfer_func = f->fmt.pix.xfer_func;
	ctx->dst_fmt.pix.ycbcr_enc = f->fmt.pix.ycbcr_enc;
	ctx->dst_fmt.pix.quantization = f->fmt.pix.quantization;

	/* scaling is not supported */
	ctx->dst_fmt.pix.width = f->fmt.pix.width;
	ctx->dst_fmt.pix.height = f->fmt.pix.height;
	ctx->dst_fmt.y_stride = RK_IEP_Y_STRIDE(f->fmt.pix.width,
						  f->fmt.pix.height);
	ctx->dst_fmt.uv_stride = RK_IEP_UV_STRIDE(f->fmt.pix.width,
						    f->fmt.pix.height,
						    ctx->dst_fmt.hw_fmt->uv_factor);

	if (iep_dev->post_set_fmt)
		iep_dev->post_set_fmt(ctx, true);

	ctx->fmt_changed = true;

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_s_fmt_vid_out);

int rk_iep_s_fmt_vid_cap(struct file *file, void *priv,
			 struct v4l2_format *f)
{
	struct rk_iep_ctx *ctx = rk_iep_file2ctx(file);
	struct rk_iep_dev *iep_dev = ctx->iep_dev;
	struct vb2_queue *vq;
	int ret;

	ret = rk_iep_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	/* scaling is not supported */
	f->fmt.pix.width = ctx->src_fmt.pix.width;
	f->fmt.pix.height = ctx->src_fmt.pix.height;

	ctx->dst_fmt.pix = f->fmt.pix;
	ctx->dst_fmt.hw_fmt = rk_iep_fmt_find(iep_dev->formats,
					       iep_dev->num_formats,
					       f->fmt.pix.pixelformat);

	ctx->dst_fmt.y_stride = RK_IEP_Y_STRIDE(f->fmt.pix.width,
						  f->fmt.pix.height);
	ctx->dst_fmt.uv_stride = RK_IEP_UV_STRIDE(f->fmt.pix.width,
						    f->fmt.pix.height,
						    ctx->dst_fmt.hw_fmt->uv_factor);

	if (iep_dev->post_set_fmt)
		iep_dev->post_set_fmt(ctx, false);

	ctx->fmt_changed = true;

	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_s_fmt_vid_cap);

/* V4L2 file ops */

static int rk_iep_queue_init(void *priv, struct vb2_queue *src_vq,
			     struct vb2_queue *dst_vq)
{
	struct rk_iep_ctx *ctx = priv;
	struct rk_iep_dev *iep_dev = ctx->iep_dev;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->min_queued_buffers = 1;
	src_vq->ops = iep_dev->qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &iep_dev->mutex;
	src_vq->dev = iep_dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->min_queued_buffers = 2;
	dst_vq->ops = iep_dev->qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &iep_dev->mutex;
	dst_vq->dev = iep_dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

int rk_iep_open(struct file *file)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);
	struct rk_iep_ctx *ctx;
	int ret;

	if (mutex_lock_interruptible(&iep_dev->mutex))
		return -ERESTARTSYS;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mutex_unlock(&iep_dev->mutex);
		return -ENOMEM;
	}

	ctx->iep_dev = iep_dev;

	/* default output format */
	ctx->src_fmt.pix.pixelformat = iep_dev->formats[0].fourcc;
	ctx->src_fmt.pix.field = V4L2_FIELD_INTERLACED;
	ctx->src_fmt.pix.width = RK_IEP_DEFAULT_WIDTH;
	ctx->src_fmt.pix.height = RK_IEP_DEFAULT_HEIGHT;
	rk_iep_prepare_format(&ctx->src_fmt.pix, iep_dev->formats,
			      iep_dev->num_formats);
	ctx->src_fmt.hw_fmt = &iep_dev->formats[0];
	ctx->src_fmt.y_stride = RK_IEP_Y_STRIDE(ctx->src_fmt.pix.width,
						  ctx->src_fmt.pix.height);
	ctx->src_fmt.uv_stride = RK_IEP_UV_STRIDE(ctx->src_fmt.pix.width,
						    ctx->src_fmt.pix.height,
						    ctx->src_fmt.hw_fmt->uv_factor);

	/* default capture format */
	ctx->dst_fmt.pix.pixelformat = iep_dev->formats[0].fourcc;
	ctx->dst_fmt.pix.field = V4L2_FIELD_NONE;
	ctx->dst_fmt.pix.width = RK_IEP_DEFAULT_WIDTH;
	ctx->dst_fmt.pix.height = RK_IEP_DEFAULT_HEIGHT;
	rk_iep_prepare_format(&ctx->dst_fmt.pix, iep_dev->formats,
			      iep_dev->num_formats);
	ctx->dst_fmt.hw_fmt = &iep_dev->formats[0];
	ctx->dst_fmt.y_stride = RK_IEP_Y_STRIDE(ctx->dst_fmt.pix.width,
						  ctx->dst_fmt.pix.height);
	ctx->dst_fmt.uv_stride = RK_IEP_UV_STRIDE(ctx->dst_fmt.pix.width,
						    ctx->dst_fmt.pix.height,
						    ctx->dst_fmt.hw_fmt->uv_factor);

	/* hw-specific post-format setup (e.g. IEP2 uv_hw_stride) */
	if (iep_dev->post_set_fmt) {
		iep_dev->post_set_fmt(ctx, true);
		iep_dev->post_set_fmt(ctx, false);
	}

	/* ensure formats are written to HW */
	ctx->fmt_changed = true;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(iep_dev->m2m_dev, ctx,
					     &rk_iep_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_free;
	}

	v4l2_fh_add(&ctx->fh, file);

	mutex_unlock(&iep_dev->mutex);

	return 0;

err_free:
	kfree(ctx);
	mutex_unlock(&iep_dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(rk_iep_open);

int rk_iep_release(struct file *file)
{
	struct rk_iep_dev *iep_dev = video_drvdata(file);
	struct rk_iep_ctx *ctx = rk_iep_file2ctx(file);

	mutex_lock(&iep_dev->mutex);

	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	kfree(ctx);

	mutex_unlock(&iep_dev->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(rk_iep_release);

/* M2M job ops */

int rk_iep_job_ready(void *priv)
{
	struct rk_iep_ctx *ctx = priv;

	return v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) >= 2 &&
	       v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) >= 1;
}
EXPORT_SYMBOL_GPL(rk_iep_job_ready);

void rk_iep_job_abort(void *priv)
{
	struct rk_iep_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	ctx->job_abort = true;
}
EXPORT_SYMBOL_GPL(rk_iep_job_abort);

/* Registration helpers */

int rk_iep_register(struct rk_iep_dev *iep_dev,
		    const struct v4l2_m2m_ops *m2m_ops,
		    const struct v4l2_file_operations *fops,
		    const struct v4l2_ioctl_ops *ioctl_ops)
{
	struct video_device *vfd;
	int ret;

	mutex_init(&iep_dev->mutex);

	ret = v4l2_device_register(iep_dev->dev, &iep_dev->v4l2_dev);
	if (ret) {
		dev_err(iep_dev->dev, "Failed to register V4L2 device\n");
		return ret;
	}

	vfd = &iep_dev->vfd;
	vfd->name[0] = '\0';
	snprintf(vfd->name, sizeof(vfd->name), "%s", iep_dev->name);
	vfd->fops = fops;
	vfd->ioctl_ops = ioctl_ops;
	vfd->vfl_dir = VFL_DIR_M2M;
	vfd->minor = -1;
	vfd->release = video_device_release_empty;
	vfd->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	vfd->lock = &iep_dev->mutex;
	vfd->v4l2_dev = &iep_dev->v4l2_dev;

	video_set_drvdata(vfd, iep_dev);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		v4l2_err(&iep_dev->v4l2_dev,
			 "Failed to register video device\n");
		goto err_v4l2;
	}

	v4l2_info(&iep_dev->v4l2_dev,
		  "Device %s registered as /dev/video%d\n",
		  vfd->name, vfd->num);

	iep_dev->m2m_dev = v4l2_m2m_init(m2m_ops);
	if (IS_ERR(iep_dev->m2m_dev)) {
		v4l2_err(&iep_dev->v4l2_dev,
			 "Failed to initialize V4L2 M2M device\n");
		ret = PTR_ERR(iep_dev->m2m_dev);
		goto err_video;
	}

	pm_runtime_set_autosuspend_delay(iep_dev->dev, 100);
	pm_runtime_use_autosuspend(iep_dev->dev);
	pm_runtime_enable(iep_dev->dev);

	return 0;

err_video:
	video_unregister_device(&iep_dev->vfd);
err_v4l2:
	v4l2_device_unregister(&iep_dev->v4l2_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(rk_iep_register);

void rk_iep_unregister(struct rk_iep_dev *iep_dev)
{
	pm_runtime_dont_use_autosuspend(iep_dev->dev);
	pm_runtime_disable(iep_dev->dev);

	v4l2_m2m_release(iep_dev->m2m_dev);
	video_unregister_device(&iep_dev->vfd);
	v4l2_device_unregister(&iep_dev->v4l2_dev);
}
EXPORT_SYMBOL_GPL(rk_iep_unregister);

MODULE_DESCRIPTION("Rockchip IEP common framework");
MODULE_LICENSE("GPL v2");
