/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#ifndef _AML_VDEC_DRV_H_
#define _AML_VDEC_DRV_H_

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/clk.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#define AML_VCODEC_MAX_PLANES 3
#define AML_VDEC_MIN_W    64U
#define AML_VDEC_MIN_H    64U
#define AML_VDEC_1080P_MAX_H  1088U
#define AML_VDEC_1080P_MAX_W  1920U

struct aml_vdec_ctx;
/**
 * enum aml_fmt_type - Type of format type
 */
enum aml_fmt_type {
	AML_FMT_DEC = 0,
	AML_FMT_FRAME = 1,
};

/**
 * enum aml_codec_type - Type of codec format
 */
enum aml_codec_type {
	CODEC_TYPE_H264 = 0,
	CODEC_TYPE_FRAME,
};

/**
 * enum aml_queue_type - Type of queue : cap or output
 */
enum aml_queue_type {
	AML_FMT_SRC = 0,
	AML_FMT_DST = 1,
};

/**
 * struct aml_video_fmt - aml video decoder fmt information
 * @fourcc: FourCC code of the format. See V4L2_PIX_FMT_*.
 * @align: Describe the align width/height required by hardware.
 * @is_10_bit_support: If the curr platform support p010 output.
 * @type: Curr queue type: capture or output.
 * @codec_type: Codec mode related. See aml_codec_type.
 * @num_planes: Num planes of the format.
 * @name: Name of the format.
 * @stepwise: Supported range of frame sizes (only for bitstream formats).
 */
struct aml_video_fmt {
	u32 fourcc;
	int align;
	int is_10_bit_support;
	enum aml_fmt_type type;
	enum aml_codec_type codec_type;
	u32 num_planes;
	const u8 *name;
	struct v4l2_frmsize_stepwise stepwise;
};

/**
 * struct aml_vdec_dev - driver data
 * @plat_dev: Platform device for the current driver.
 * @v4l2_dev: V4L2 device to register video devices for.
 * @m2m_dev_dec: Mem2mem device associated to this device.
 * @vfd: Video_device associated to this device.
 * @mdev: Media_device associated to this device.
 * @dec_ctx: Decoder context. See struct aml_vdec_ctx.
 * @dec_hw: Decoder hardware resources. See struct aml_vdec_hw.
 * @pvdec_data: Decoder platform data. See struct aml_dev_platform_data.
 * @dev_mutex: video_device lock.
 * @filp: v4l2 file handle pointer.
 */
struct aml_vdec_dev {
	struct platform_device *plat_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev_dec;
	struct video_device *vfd;
	struct media_device mdev;

	struct aml_vdec_ctx *dec_ctx;
	struct aml_vdec_hw *dec_hw;
	const struct aml_dev_platform_data *pvdec_data;

	struct mutex dev_mutex;
	struct file *filp;
};

/**
 * struct dec_pic_info - pic information description
 * @cap_pix_fmt: Pixel format for capture queue.
 * @output_pix_fmt: Pixel format for output queue.
 * @coded_width: Width for decode.
 * @coded_height: Height for decode.
 * @fb_size: Frame buffer size for Y or UV.
 * @plane_num: Num for planes for curr format.
 */
struct dec_pic_info {
	u32 cap_pix_fmt;
	u32 output_pix_fmt;
	u32 coded_width;
	u32 coded_height;
	u32 fb_size[2];
	u32 plane_num;
};

/**
 * struct aml_vdec_ctx - driver instance context
 * @dev: pointer to the aml_vdec_dev of the device.
 * @fh: struct v4l2 fh.
 * @m2m_ctx: pointer to v4l2_m2m_ctx context.
 * @ctrl_handler: V4L2 ctrl handler.
 * @v4l2_intf_lock: Mutex lock for v4l2 interface.
 * @codec_ops: Codec operation functions. See struct aml_codec_ops.
 * @int_cond: Variable used by the waitqueue.
 * @queue: Waitqueue to wait for the current decode context finish.
 * @pix_fmt: To store the V4L2 pixel format.
 * @dec_fmt: To describe the decoding format supported by hardware platform.
 * @is_cap_streamon: indicates if the current capture stream is on.
 * @is_output_streamon: indicates if the current output stream is on.
 * @dos_clk_en: indicates if dos clk is enabled.
 * @pic_info: Pic information for curr decoder context. See struct dec_pic_info.
 * @curr_dec_type: Current decoder type. (CODEC_TYPE_H264, etc.)
 * @codec_priv: Pointer to current decoder instance.
 */
struct aml_vdec_ctx {
	struct aml_vdec_dev *dev;
	struct v4l2_fh fh;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_ctrl_handler ctrl_handler;
	struct mutex v4l2_intf_lock;

	const struct aml_codec_ops *codec_ops;
	int int_cond;
	wait_queue_head_t queue;
	struct v4l2_pix_format_mplane pix_fmt[2];
	struct aml_video_fmt dec_fmt[2];

	bool is_cap_streamon;
	bool is_output_streamon;
	bool dos_clk_en;

	struct dec_pic_info pic_info;
	u32 curr_dec_type;
	void *codec_priv;
};

static inline struct aml_vdec_ctx *fh_to_dec_ctx(struct file *file)
{
	struct v4l2_fh *file_fh = file_to_v4l2_fh(file);

	return container_of(file_fh, struct aml_vdec_ctx, fh);
}

static inline struct aml_vdec_ctx *ctrl_to_dec_ctx(struct v4l2_ctrl_handler *ctrl)
{
	return container_of(ctrl, struct aml_vdec_ctx, ctrl_handler);
}

#endif
