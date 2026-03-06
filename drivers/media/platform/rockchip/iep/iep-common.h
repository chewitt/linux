/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip Image Enhancement Processor (IEP) common definitions
 *
 * Copyright (C) 2020 Alex Bee <knaerzche@gmail.com>
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 */

#ifndef __RK_IEP_COMMON_H__
#define __RK_IEP_COMMON_H__

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

/* Hardware limits (shared by IEP and IEP2) */
#define RK_IEP_MIN_WIDTH	320U
#define RK_IEP_MAX_WIDTH	1920U
#define RK_IEP_MIN_HEIGHT	240U
#define RK_IEP_MAX_HEIGHT	1088U

#define RK_IEP_DEFAULT_WIDTH	320U
#define RK_IEP_DEFAULT_HEIGHT	240U

/* Stride calculation macros */
#define RK_IEP_Y_STRIDE(w, h)		((w) * (h))
#define RK_IEP_UV_STRIDE(w, h, fac)	((w) * (h) + (w) * (h) / (fac))

/* Common format descriptor */
struct rk_iep_fmt {
	u32 fourcc;
	u8 depth;
	u8 uv_factor;
	u8 color_swap;
	u8 hw_format;
};

/* Common frame format */
struct rk_iep_frm_fmt {
	struct rk_iep_fmt *hw_fmt;
	struct v4l2_pix_format pix;
	unsigned int y_stride;
	unsigned int uv_stride;
	unsigned int uv_hw_stride;	/* used by IEP2 only */
};

struct rk_iep_ctx;

/*
 * Common device structure - embedded as first member in driver-specific
 * device structs. Use container_of() to access the driver-specific struct.
 */
struct rk_iep_dev {
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct video_device vfd;
	struct device *dev;
	void __iomem *regs;
	/* vfd lock */
	struct mutex mutex;

	/* Driver identity */
	const char *name;

	/* Per-driver format table */
	struct rk_iep_fmt *formats;
	unsigned int num_formats;

	/* Per-driver VB2 queue ops */
	const struct vb2_ops *qops;

	/* Optional: called during start_streaming to initialize HW */
	void (*hw_init)(struct rk_iep_dev *iep_dev);
	/* Optional: called after format is set for hw-specific stride setup */
	void (*post_set_fmt)(struct rk_iep_ctx *ctx, bool is_output);
};

/* Common per-context structure */
struct rk_iep_ctx {
	struct v4l2_fh fh;
	struct rk_iep_dev *iep_dev;

	struct rk_iep_frm_fmt src_fmt;
	struct rk_iep_frm_fmt dst_fmt;

	struct vb2_v4l2_buffer *prev_src_buf;
	struct vb2_v4l2_buffer *dst0_buf;
	struct vb2_v4l2_buffer *dst1_buf;

	u32 dst_sequence;
	u32 src_sequence;

	/* bff = bottom field first */
	bool field_order_bff;
	bool field_bff;

	unsigned int dst_buffs_done;

	bool fmt_changed;
	bool job_abort;
};

static inline struct rk_iep_ctx *rk_iep_file2ctx(struct file *file)
{
	return container_of(file->private_data, struct rk_iep_ctx, fh);
}

/* Format helpers */
struct rk_iep_fmt *rk_iep_fmt_find(struct rk_iep_fmt *fmts, unsigned int n,
				    u32 pixelformat);
bool rk_iep_check_pix_format(struct rk_iep_fmt *fmts, unsigned int n,
			     u32 pixelformat);
void rk_iep_prepare_format(struct v4l2_pix_format *pix_fmt,
			   struct rk_iep_fmt *fmts, unsigned int nfmts);

/* Buffer helpers */
struct vb2_v4l2_buffer *rk_iep_m2m_next_dst_buf(struct rk_iep_ctx *ctx);
void rk_iep_m2m_dst_bufs_done(struct rk_iep_ctx *ctx,
			      enum vb2_buffer_state state);

/* VB2 queue ops */
int rk_iep_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
		       unsigned int *nplanes, unsigned int sizes[],
		       struct device *alloc_devs[]);
int rk_iep_buf_prepare(struct vb2_buffer *vb);
void rk_iep_buf_queue(struct vb2_buffer *vb);
void rk_iep_queue_cleanup(struct vb2_queue *vq, u32 state);
int rk_iep_start_streaming(struct vb2_queue *vq, unsigned int count);
void rk_iep_stop_streaming(struct vb2_queue *vq);

/* V4L2 IOCTL ops */
int rk_iep_querycap(struct file *file, void *priv,
		    struct v4l2_capability *cap);
int rk_iep_enum_fmt(struct file *file, void *priv,
		    struct v4l2_fmtdesc *f);
int rk_iep_enum_framesizes(struct file *file, void *priv,
			   struct v4l2_frmsizeenum *fsize);
int rk_iep_g_fmt_vid_cap(struct file *file, void *priv,
			 struct v4l2_format *f);
int rk_iep_g_fmt_vid_out(struct file *file, void *priv,
			 struct v4l2_format *f);
int rk_iep_try_fmt_vid_cap(struct file *file, void *priv,
			   struct v4l2_format *f);
int rk_iep_try_fmt_vid_out(struct file *file, void *priv,
			   struct v4l2_format *f);
int rk_iep_s_fmt_vid_out(struct file *file, void *priv,
			 struct v4l2_format *f);
int rk_iep_s_fmt_vid_cap(struct file *file, void *priv,
			 struct v4l2_format *f);

/* V4L2 file ops */
int rk_iep_open(struct file *file);
int rk_iep_release(struct file *file);

/* M2M job ops */
int rk_iep_job_ready(void *priv);
void rk_iep_job_abort(void *priv);

/* Registration helpers */
int rk_iep_register(struct rk_iep_dev *iep_dev,
		    const struct v4l2_m2m_ops *m2m_ops,
		    const struct v4l2_file_operations *fops,
		    const struct v4l2_ioctl_ops *ioctl_ops);
void rk_iep_unregister(struct rk_iep_dev *iep_dev);

#endif
