// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include "aml_vdec.h"
#include "aml_vdec_hw.h"
#include "aml_vdec_platform.h"

#define AML_VDEC_DRV_NAME "aml-vdec-drv"

static int fops_vcodec_open(struct file *file)
{
	struct aml_vdec_dev *dec_dev = video_drvdata(file);
	struct aml_vdec_ctx *ctx = NULL;
	int ret = 0;

	ctx = kzalloc_obj(*ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&dec_dev->dev_mutex);
	dec_dev->dec_ctx = ctx;
	ctx->dev = dec_dev;
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh, file);
	dec_dev->filp = file;
	mutex_init(&ctx->v4l2_intf_lock);
	init_waitqueue_head(&ctx->queue);
	ctx->int_cond = 0;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(dec_dev->m2m_dev_dec, ctx,
					 &aml_vdec_queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR((__force void *)ctx->m2m_ctx);
		v4l2_err(&dec_dev->v4l2_dev, "Failed to v4l2_m2m_ctx_init() (%d)", ret);
		goto err_m2m_ctx_init;
	}

	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ret = aml_vdec_ctrls_setup(ctx);
	if (ret) {
		v4l2_err(&dec_dev->v4l2_dev, "Failed to init all ctrls (%d)", ret);
		goto err_ctrls_setup;
	}

	aml_vdec_reset_fmts(ctx);
	mutex_unlock(&dec_dev->dev_mutex);

	return ret;

err_ctrls_setup:
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
err_m2m_ctx_init:
	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&dec_dev->dev_mutex);

	return ret;
}

static int fops_vcodec_release(struct file *file)
{
	struct aml_vdec_dev *dec_dev = video_drvdata(file);
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);

	mutex_lock(&dec_dev->dev_mutex);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&dec_dev->dev_mutex);

	return 0;
}

static const struct v4l2_file_operations aml_vdec_fops = {
	.owner        = THIS_MODULE,
	.open        = fops_vcodec_open,
	.release    = fops_vcodec_release,
	.poll        = v4l2_m2m_fop_poll,
	.unlocked_ioctl    = video_ioctl2,
	.mmap        = v4l2_m2m_fop_mmap,
};

static const struct video_device dec_dev = {
	.name = "aml_dev_drv",
	.fops = &aml_vdec_fops,
	.ioctl_ops = &aml_vdec_ioctl_ops,
	.release = video_device_release,
	.vfl_dir = VFL_DIR_M2M,
	.device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING,
};

static const struct media_device_ops aml_m2m_media_ops = {
	.req_validate = vb2_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static int aml_vdec_drv_probe(struct platform_device *pdev)
{
	struct aml_vdec_dev *dev;
	struct video_device *vfd_dec;
	struct aml_vdec_hw *hw;
	int ret = 0;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->plat_dev = pdev;
	mutex_init(&dev->dev_mutex);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "v4l2_device_register err\n");

	vfd_dec = video_device_alloc();
	if (!vfd_dec) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_device_alloc;
	}
	*vfd_dec = dec_dev;
	vfd_dec->v4l2_dev = &dev->v4l2_dev;
	vfd_dec->lock = &dev->dev_mutex;
	video_set_drvdata(vfd_dec, dev);
	dev->vfd = vfd_dec;
	platform_set_drvdata(pdev, dev);

	hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw) {
		ret = -ENOMEM;
		goto err_dec_mem_init;
	}
	dev->dec_hw = hw;

	dev->pvdec_data = of_device_get_match_data(&pdev->dev);
	ret = dev->pvdec_data->req_hw_resource(dev);
	if (ret < 0)
		goto err_hw_init;

	dev->m2m_dev_dec = v4l2_m2m_init(&aml_vdec_m2m_ops);
	if (IS_ERR(dev->m2m_dev_dec)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem dec device\n");
		ret = PTR_ERR((__force void *)dev->m2m_dev_dec);
		goto err_hw_init;
	}

	ret = video_register_device(vfd_dec, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device");
		goto err_vid_dev_register;
	}

	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, AML_VDEC_DRV_NAME, sizeof(dev->mdev.model));
	media_device_init(&dev->mdev);
	dev->mdev.ops = &aml_m2m_media_ops;
	dev->v4l2_dev.mdev = &dev->mdev;

	ret = v4l2_m2m_register_media_controller(dev->m2m_dev_dec, vfd_dec,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem media controller\n");
		goto error_m2m_mc_register;
	}

	ret = media_device_register(&dev->mdev);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register media device");
		goto err_media_dev_register;
	}
	vdec_enable(dev->dec_hw);
	return 0;

err_media_dev_register:
	v4l2_m2m_unregister_media_controller(dev->m2m_dev_dec);
error_m2m_mc_register:
	media_device_cleanup(&dev->mdev);
err_vid_dev_register:
	v4l2_m2m_release(dev->m2m_dev_dec);
err_hw_init:
	dev->dec_hw = NULL;
err_dec_mem_init:
	video_device_release(vfd_dec);
err_device_alloc:
	v4l2_device_unregister(&dev->v4l2_dev);
	return ret;
}

static void aml_vdec_drv_remove(struct platform_device *pdev)
{
	struct aml_vdec_dev *dev = platform_get_drvdata(pdev);

	vdec_disable(dev->dec_hw);

	if (media_devnode_is_registered(dev->mdev.devnode)) {
		media_device_unregister(&dev->mdev);
		media_device_cleanup(&dev->mdev);
	}

	if (dev->m2m_dev_dec)
		v4l2_m2m_release(dev->m2m_dev_dec);
	if (dev->vfd)
		video_unregister_device(dev->vfd);
	if (dev->dec_hw) {
		dev->pvdec_data->destroy_hw_resource(dev);
		dev->dec_hw = NULL;
	}
	v4l2_device_unregister(&dev->v4l2_dev);
}

static const struct of_device_id aml_vdec_match[] = {
	{.compatible = "amlogic,s4-vcodec-dec", .data = &aml_vdec_s4_pdata},
	{},
};

static struct platform_driver aml_vcodec_dec_driver = {
	.probe    = aml_vdec_drv_probe,
	.remove    = aml_vdec_drv_remove,
	.driver    = {
		.name    = AML_VDEC_DRV_NAME,
		.of_match_table = aml_vdec_match,
	},
};

module_platform_driver(aml_vcodec_dec_driver);

MODULE_DESCRIPTION("Amlogic V4L2 decoder driver");
MODULE_LICENSE("GPL");
