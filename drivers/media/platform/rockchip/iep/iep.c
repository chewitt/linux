// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Image Enhancement Processor (IEP) driver
 *
 * Copyright (C) 2020 Alex Bee <knaerzche@gmail.com>
 *
 * Based on the Allwinner sun8i deinterlacer with scaler driver
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/videobuf2-dma-contig.h>

#include "iep-regs.h"
#include "iep.h"

static struct rk_iep_fmt iep_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.color_swap = IEP_YUV_SWP_SP_UV,
		.hw_format = IEP_COLOR_FMT_YUV420,
		.depth = 12,
		.uv_factor = 4,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.color_swap = IEP_YUV_SWP_SP_VU,
		.hw_format = IEP_COLOR_FMT_YUV420,
		.depth = 12,
		.uv_factor = 4,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.color_swap = IEP_YUV_SWP_SP_UV,
		.hw_format = IEP_COLOR_FMT_YUV422,
		.depth = 16,
		.uv_factor = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.color_swap = IEP_YUV_SWP_SP_VU,
		.hw_format = IEP_COLOR_FMT_YUV422,
		.depth = 16,
		.uv_factor = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.color_swap = IEP_YUV_SWP_P,
		.hw_format = IEP_COLOR_FMT_YUV420,
		.depth = 12,
		.uv_factor = 4,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.color_swap = IEP_YUV_SWP_P,
		.hw_format = IEP_COLOR_FMT_YUV422,
		.depth = 16,
		.uv_factor = 2,
	},
};

static void iep_setup_formats(struct rk_iep_ctx *ctx)
{
	struct rockchip_iep *iep = container_of(ctx->iep_dev,
						struct rockchip_iep, base);

	/* setup src dimensions */
	iep_write(iep, IEP_SRC_IMG_SIZE,
		  IEP_IMG_SIZE(ctx->src_fmt.pix.width, ctx->src_fmt.pix.height));

	/* setup dst dimensions */
	iep_write(iep, IEP_DST_IMG_SIZE,
		  IEP_IMG_SIZE(ctx->dst_fmt.pix.width, ctx->dst_fmt.pix.height));

	/* setup virtual width */
	iep_write(iep, IEP_VIR_IMG_WIDTH,
		  IEP_VIR_WIDTH(ctx->src_fmt.pix.width, ctx->dst_fmt.pix.width));

	/* setup src format */
	iep_shadow_mod(iep, IEP_CONFIG1, IEP_RAW_CONFIG1,
		       IEP_SRC_FMT_MASK | IEP_SRC_FMT_SWP_MASK(ctx->src_fmt.hw_fmt->hw_format),
		       IEP_SRC_FMT(ctx->src_fmt.hw_fmt->hw_format,
				   ctx->src_fmt.hw_fmt->color_swap));

	/* setup dst format */
	iep_shadow_mod(iep, IEP_CONFIG1, IEP_RAW_CONFIG1,
		       IEP_DST_FMT_MASK | IEP_DST_FMT_SWP_MASK(ctx->dst_fmt.hw_fmt->hw_format),
		       IEP_DST_FMT(ctx->dst_fmt.hw_fmt->hw_format,
				   ctx->dst_fmt.hw_fmt->color_swap));

	ctx->fmt_changed = false;
}

static void iep_dein_init(struct rockchip_iep *iep)
{
	unsigned int i;

	/* values taken from BSP driver */
	iep_shadow_mod(iep, IEP_CONFIG0, IEP_RAW_CONFIG0,
		       (IEP_DEIN_EDGE_INTPOL_SMTH_EN |
		       IEP_DEIN_EDGE_INTPOL_RADIUS_MASK |
		       IEP_DEIN_HIGH_FREQ_EN |
		       IEP_DEIN_HIGH_FREQ_MASK),
		       (IEP_DEIN_EDGE_INTPOL_SMTH_EN |
		       IEP_DEIN_EDGE_INTPOL_RADIUS(3) |
		       IEP_DEIN_HIGH_FREQ_EN |
		       IEP_DEIN_HIGH_FREQ(64)));

	for (i = 0; i < ARRAY_SIZE(default_dein_motion_tbl); i++)
		iep_write(iep, default_dein_motion_tbl[i][0],
			  default_dein_motion_tbl[i][1]);
}

static void iep_init(struct rockchip_iep *iep)
{
	iep_write(iep, IEP_CONFIG0, IEP_DEIN_MODE(IEP_DEIN_MODE_BYPASS));

	/* reset frame counter */
	iep_write(iep, IEP_FRM_CNT, 0);
}

static void iep_hw_init(struct rk_iep_dev *iep_dev)
{
	struct rockchip_iep *iep = container_of(iep_dev,
						struct rockchip_iep, base);

	iep_init(iep);
	iep_dein_init(iep);
}

static void iep_device_run(void *priv)
{
	struct rk_iep_ctx *ctx = priv;
	struct rockchip_iep *iep = container_of(ctx->iep_dev,
						struct rockchip_iep, base);
	struct vb2_v4l2_buffer *src, *dst;
	unsigned int dein_mode;
	dma_addr_t addr;

	if (ctx->fmt_changed)
		iep_setup_formats(ctx);

	if (ctx->prev_src_buf)
		dein_mode = IEP_DEIN_MODE_I4O2;
	else
		dein_mode = ctx->field_bff ? IEP_DEIN_MODE_I2O1B : IEP_DEIN_MODE_I2O1T;

	iep_shadow_mod(iep, IEP_CONFIG0, IEP_RAW_CONFIG0,
		       IEP_DEIN_MODE_MASK, IEP_DEIN_MODE(dein_mode));

	/* sync RAW_xxx registers with actual used */
	iep_write(iep, IEP_CONFIG_DONE, 1);

	/* setup src buff(s)/addresses */
	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	addr = vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 0);

	iep_write(iep, IEP_DEIN_IN_IMG0_Y(ctx->field_bff), addr);

	iep_write(iep, IEP_DEIN_IN_IMG0_CBCR(ctx->field_bff),
		  addr + ctx->src_fmt.y_stride);

	iep_write(iep, IEP_DEIN_IN_IMG0_CR(ctx->field_bff),
		  addr + ctx->src_fmt.uv_stride);

	if (IEP_DEIN_IN_MODE_FIELDS(dein_mode) == IEP_DEIN_IN_FIELDS_4)
		addr = vb2_dma_contig_plane_dma_addr(&ctx->prev_src_buf->vb2_buf, 0);

	iep_write(iep, IEP_DEIN_IN_IMG1_Y(ctx->field_bff), addr);

	iep_write(iep, IEP_DEIN_IN_IMG1_CBCR(ctx->field_bff),
		  addr + ctx->src_fmt.y_stride);

	iep_write(iep, IEP_DEIN_IN_IMG1_CR(ctx->field_bff),
		  addr + ctx->src_fmt.uv_stride);

	/* setup dst buff(s)/addresses */
	dst = rk_iep_m2m_next_dst_buf(ctx);
	addr = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);

	if (IEP_DEIN_OUT_MODE_FRAMES(dein_mode) == IEP_DEIN_OUT_FRAMES_2) {
		v4l2_m2m_buf_copy_metadata(ctx->prev_src_buf, dst);

		iep_write(iep, IEP_DEIN_OUT_IMG0_Y(ctx->field_bff), addr);

		iep_write(iep, IEP_DEIN_OUT_IMG0_CBCR(ctx->field_bff),
			  addr + ctx->dst_fmt.y_stride);

		iep_write(iep, IEP_DEIN_OUT_IMG0_CR(ctx->field_bff),
			  addr + ctx->dst_fmt.uv_stride);

		ctx->dst0_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		dst = rk_iep_m2m_next_dst_buf(ctx);
		addr = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);
	}

	v4l2_m2m_buf_copy_metadata(src, dst);

	iep_write(iep, IEP_DEIN_OUT_IMG1_Y(ctx->field_bff), addr);

	iep_write(iep, IEP_DEIN_OUT_IMG1_CBCR(ctx->field_bff),
		  addr + ctx->dst_fmt.y_stride);

	iep_write(iep, IEP_DEIN_OUT_IMG1_CR(ctx->field_bff),
		  addr + ctx->dst_fmt.uv_stride);

	ctx->dst1_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	iep_mod(iep, IEP_INT, IEP_INT_FRAME_DONE_EN,
		IEP_INT_FRAME_DONE_EN);

	/* start HW */
	iep_write(iep, IEP_FRM_START, 1);
}

static const struct v4l2_m2m_ops iep_m2m_ops = {
	.device_run	= iep_device_run,
	.job_ready	= rk_iep_job_ready,
	.job_abort	= rk_iep_job_abort,
};

static const struct vb2_ops iep_qops = {
	.queue_setup		= rk_iep_queue_setup,
	.buf_prepare		= rk_iep_buf_prepare,
	.buf_queue		= rk_iep_buf_queue,
	.start_streaming	= rk_iep_start_streaming,
	.stop_streaming		= rk_iep_stop_streaming,
};

static const struct v4l2_file_operations iep_fops = {
	.owner		= THIS_MODULE,
	.open		= rk_iep_open,
	.release	= rk_iep_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct v4l2_ioctl_ops iep_ioctl_ops = {
	.vidioc_querycap		= rk_iep_querycap,

	.vidioc_enum_framesizes		= rk_iep_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= rk_iep_enum_fmt,
	.vidioc_g_fmt_vid_cap		= rk_iep_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= rk_iep_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= rk_iep_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= rk_iep_enum_fmt,
	.vidioc_g_fmt_vid_out		= rk_iep_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= rk_iep_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= rk_iep_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
};

static irqreturn_t iep_isr(int irq, void *prv)
{
	struct rockchip_iep *iep = prv;
	struct rk_iep_ctx *ctx;
	u32 val;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;

	ctx = v4l2_m2m_get_curr_priv(iep->base.m2m_dev);
	if (!ctx) {
		v4l2_err(&iep->base.v4l2_dev,
			 "Instance released before the end of transaction\n");
		return IRQ_NONE;
	}

	/*
	 * The irq is shared with the iommu. If the runtime-pm state of the
	 * iep-device is disabled or the interrupt status doesn't match the
	 * expected mask the irq has been targeted to the iommu.
	 */

	if (!pm_runtime_active(iep->base.dev) ||
	    !(iep_read(iep, IEP_INT) & IEP_INT_MASK))
		return IRQ_NONE;

	/* disable interrupt - will be re-enabled at next iep_device_run */
	iep_mod(iep, IEP_INT,
		IEP_INT_FRAME_DONE_EN, 0);

	iep_mod(iep, IEP_INT, IEP_INT_FRAME_DONE_CLR,
		IEP_INT_FRAME_DONE_CLR);

	/* wait for all status regs to show "idle" */
	val = readl_poll_timeout(iep->base.regs + IEP_STATUS, val,
				  (val == 0), 100, IEP_TIMEOUT);

	if (val) {
		dev_err(iep->base.dev,
			"Failed to wait for job to finish: status: %u\n", val);
		state = VB2_BUF_STATE_ERROR;
		ctx->job_abort = true;
	}

	rk_iep_m2m_dst_bufs_done(ctx, state);

	ctx->field_bff = (ctx->dst_buffs_done % 2 == 0)
		     ? ctx->field_order_bff : !ctx->field_order_bff;

	if (ctx->dst_buffs_done == 2 || ctx->job_abort) {
		if (ctx->prev_src_buf)
			v4l2_m2m_buf_done(ctx->prev_src_buf, state);

		/* current src buff will be next prev */
		ctx->prev_src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);

		v4l2_m2m_job_finish(iep->base.m2m_dev, ctx->fh.m2m_ctx);
		ctx->dst_buffs_done = 0;

	} else {
		iep_device_run(ctx);
	}

	return IRQ_HANDLED;
}

static int iep_probe(struct platform_device *pdev)
{
	struct rockchip_iep *iep;
	int ret, irq;

	if (!pdev->dev.of_node)
		return -ENODEV;

	iep = devm_kzalloc(&pdev->dev, sizeof(*iep), GFP_KERNEL);
	if (!iep)
		return -ENOMEM;

	platform_set_drvdata(pdev, iep);
	iep->base.dev = &pdev->dev;
	iep->base.name = IEP_NAME;
	iep->base.formats = iep_formats;
	iep->base.num_formats = ARRAY_SIZE(iep_formats);
	iep->base.qops = &iep_qops;
	iep->base.hw_init = iep_hw_init;

	iep->axi_clk = devm_clk_get(iep->base.dev, "axi");
	if (IS_ERR(iep->axi_clk)) {
		dev_err(iep->base.dev, "failed to get aclk clock\n");
		return PTR_ERR(iep->axi_clk);
	}

	iep->ahb_clk = devm_clk_get(iep->base.dev, "ahb");
	if (IS_ERR(iep->ahb_clk)) {
		dev_err(iep->base.dev, "failed to get hclk clock\n");
		return PTR_ERR(iep->ahb_clk);
	}

	ret = clk_set_rate(iep->axi_clk, 300000000);
	if (ret) {
		dev_err(iep->base.dev, "failed to set axi clock rate to 300 MHz\n");
		return ret;
	}

	iep->base.regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(iep->base.regs))
		return PTR_ERR(iep->base.regs);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Could not set DMA coherent mask.\n");
		return ret;
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* IRQ is shared with IOMMU */
	ret = devm_request_irq(iep->base.dev, irq, iep_isr, IRQF_SHARED,
			       dev_name(iep->base.dev), iep);
	if (ret < 0) {
		dev_err(iep->base.dev, "failed to request irq\n");
		return ret;
	}

	return rk_iep_register(&iep->base, &iep_m2m_ops,
			       &iep_fops, &iep_ioctl_ops);
}

static void iep_remove(struct platform_device *pdev)
{
	struct rockchip_iep *iep = platform_get_drvdata(pdev);

	rk_iep_unregister(&iep->base);
}

static int __maybe_unused iep_runtime_suspend(struct device *dev)
{
	struct rockchip_iep *iep = dev_get_drvdata(dev);

	clk_disable_unprepare(iep->ahb_clk);
	clk_disable_unprepare(iep->axi_clk);

	return 0;
}

static int __maybe_unused iep_runtime_resume(struct device *dev)
{
	struct rockchip_iep *iep;
	int ret = 0;

	iep = dev_get_drvdata(dev);

	ret = clk_prepare_enable(iep->axi_clk);
	if (ret) {
		dev_err(iep->base.dev, "Cannot enable axi clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(iep->ahb_clk);
	if (ret) {
		dev_err(iep->base.dev, "Cannot enable ahb clock: %d\n", ret);
		goto err_disable_axi_clk;
	}

	return ret;

err_disable_axi_clk:
	clk_disable_unprepare(iep->axi_clk);
	return ret;
}

static const struct dev_pm_ops iep_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(iep_runtime_suspend,
			   iep_runtime_resume, NULL)
};

static const struct of_device_id rockchip_iep_match[] = {
	{
		.compatible = "rockchip,rk3228-iep",
	},
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_iep_match);

static struct platform_driver iep_pdrv = {
	.probe = iep_probe,
	.remove = iep_remove,
	.driver = {
		.name = IEP_NAME,
		.pm = &iep_pm_ops,
		.of_match_table = rockchip_iep_match,
	},
};

module_platform_driver(iep_pdrv);

MODULE_AUTHOR("Alex Bee <knaerzche@gmail.com>");
MODULE_DESCRIPTION("Rockchip Image Enhancement Processor");
MODULE_LICENSE("GPL v2");
