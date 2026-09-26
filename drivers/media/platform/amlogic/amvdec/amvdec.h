/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * V4L2 request decoder core.
 */

#ifndef __MESON_AMVDEC_CORE_H_
#define __MESON_AMVDEC_CORE_H_

#include <linux/completion.h>
#include <linux/irqreturn.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/media-device.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include "amvdec_platform.h"
#include "codec_h264_scratch.h"

/* 32 buffers in 3-plane YUV420 */
#define MAX_CANVAS (32 * 3)
#define AMVDEC_HEVC_MAX_SLICES 800

struct amvdec_dma_guard {
	void *raw_vaddr;
	dma_addr_t raw_paddr;
	size_t raw_size;
	size_t size;
	size_t prefix_size;	/* guard bytes before the device-visible buffer */
};

/*
 * AM21C capture exposes one compression-header plane. Driver-owned body
 * pages are reached through its page table for VD1 scanout as
 * DRM_FORMAT_YUV420_10BIT with DRM_FORMAT_MOD_AMLOGIC_FBC(SCATTER).
 * The header size matches MMU_COMPRESS_HEADER_SIZE.
 */

#define AM21C_HEADER_SIZE 0x48000

/**
 * struct amvdec_timestamp - stores a src timestamp along with a VIFIFO offset
 *
 * @list: timestamp list node
 * @tc: timecode from the v4l2 buffer
 * @ts: timestamp from the VB2 buffer
 * @offset: offset in the VIFIFO of the associated input packet
 * @flags: flags from the v4l2 buffer
 * @vp9_frame: VP9_FRAME request control copied while the OUTPUT request is live
 * @has_vp9_frame: true when vp9_frame contains a valid request snapshot
 */
struct amvdec_timestamp {
	struct list_head list;
	struct v4l2_timecode tc;
	u64 ts;
	u32 offset;
	u32 flags;
	struct v4l2_ctrl_vp9_frame vp9_frame;
	bool has_vp9_frame;
	struct v4l2_ctrl_hevc_sps hevc_sps;
	struct v4l2_ctrl_hevc_pps hevc_pps;
	struct v4l2_ctrl_hevc_slice_params hevc_slice;
	struct v4l2_ctrl_hevc_decode_params hevc_decode;
	bool has_hevc_slice;
	u32 input_end; /* cumulative coded endpoint, excluding current tail padding */
};

struct amvdec_session;

/* Largest microcode image loaded into a core's IMEM (VDEC_1 and HEVC alike) */
#define AMVDEC_FW_SIZE		(4096 * 4)

/**
 * struct amvdec_core - device parameters, singleton
 *
 * @dos_base: DOS memory base address
 * @esparser_base: PARSER memory base address
 * @regmap_ao: regmap for the AO bus
 * @dev: core device
 * @dev_dec: decoder device
 * @platform: platform-specific data
 * @canvas: canvas provider reference
 * @dos_parser_clk: DOS_PARSER clock
 * @dos_clk: DOS clock
 * @amvdec_1_clk: VDEC_1 clock
 * @amvdec_hevc_clk: VDEC_HEVC clock
 * @amvdec_hevcf_clk: VDEC_HEVCF clock
 * @esparser_completion: completion of the current parser DMA fetch
 * @esparser_reset: RESET for the PARSER
 * @vdev_dec: video device for the decoder
 * @v4l2_dev: v4l2 device
 * @cur_sess: current decoding session
 * @lock: video device lock
 */
struct amvdec_core {
	void __iomem *dos_base;
	void __iomem *esparser_base;
	struct regmap *regmap_ao;

	struct device *dev;
	struct device *dev_dec;
	const struct amvdec_platform *platform;

	struct meson_canvas *canvas;

	/*
	 * Keep the IMEM DMA source for the device lifetime: the DMA engine can
	 * report idle before all reads complete.
	 */
	void *fw_vaddr;
	dma_addr_t fw_paddr;

	struct clk *dos_parser_clk;
	struct clk *dos_clk;
	struct clk *amvdec_1_clk;
	struct clk *amvdec_hevc_clk;
	struct clk *amvdec_hevcf_clk;

	struct completion esparser_completion;
	struct reset_control *esparser_reset;

	struct video_device *vdev_dec;
	struct v4l2_device v4l2_dev;
	struct media_device mdev;
	struct v4l2_m2m_dev *m2m_dev;

	struct amvdec_session *cur_sess;
	/* Serialize video-device ioctls. */
	struct mutex lock;
	/*
	 * Protect decoder ownership through power-down. Acquire claim_lock after
	 * the device or session lock; never wait for either while holding it.
	 */
	struct mutex claim_lock;

	/* kept so teardown can wait an in-flight handler out */
	int irq;
};

/**
 * struct amvdec_ops - vdec operations
 *
 * @start: mandatory call when the vdec needs to initialize
 * @stop: mandatory call when the vdec needs to stop
 * @conf_esparser: mandatory call to let the vdec configure the ESPARSER
 * @vififo_level: mandatory call to get the current amount of data
 *		  in the VIFIFO
 */
struct amvdec_ops {
	int (*start)(struct amvdec_session *sess);
	int (*stop)(struct amvdec_session *sess);
	void (*conf_esparser)(struct amvdec_session *sess);
	u32 (*vififo_level)(struct amvdec_session *sess);
};

/**
 * struct amvdec_codec_ops - codec operations
 *
 * @start: mandatory call when the codec needs to initialize
 * @stop: mandatory call when the codec needs to stop
 * @load_firmware: optional call to load/prepare the complete firmware image
 * @load_extended_firmware: optional call to load additional firmware bits
 * @post_start: optional call after the firmware processor is started
 * @pre_stop: optional live-domain call before vdec_ops->stop powers down
 * @job_abort: optional call when videobuf2 requests a job abort/timeout
 * @isr: mandatory call when the ISR triggers
 * @threaded_isr: mandatory call for the threaded ISR
 * @direct_input: the codec points the decoder's stream FIFO at the OUTPUT
 *	buffer itself: the core leaves the firmware processors halted at
 *	start and does not power up the parser VIFIFO
 */
struct amvdec_codec_ops {
	int (*start)(struct amvdec_session *sess);
	int (*stop)(struct amvdec_session *sess);
	void (*pre_stop)(struct amvdec_session *sess);
	void (*job_abort)(struct amvdec_session *sess);
	int (*load_firmware)(struct amvdec_session *sess,
			     const u8 *data, u32 len);
	int (*load_extended_firmware)(struct amvdec_session *sess,
				      const u8 *data, u32 len);
	void (*post_start)(struct amvdec_session *sess);
	irqreturn_t (*isr)(struct amvdec_session *sess);
	irqreturn_t (*threaded_isr)(struct amvdec_session *sess);
	bool direct_input;
};

/**
 * struct amvdec_format - describes one of the OUTPUT (src) format supported
 *
 * @profile_ctrl: profile menu control ID, zero if absent
 * @default_profile: default profile menu entry
 * @level_ctrl: level menu control ID, zero if absent
 * @default_level: default level menu entry
 * @profiles: supported profile menu entries as a bit mask
 * @max_level: highest level menu entry
 * @max_bit_depth: highest coded bit depth, zero for no format-specific limit
 * @pixfmt: V4L2 pixel format
 * @min_buffers: minimum amount of CAPTURE (dst) buffers
 * @max_buffers: maximum amount of CAPTURE (dst) buffers
 * @max_width: maximum picture width supported
 * @max_height: maximum picture height supported
 * @flags: enum flags associated with this pixfmt
 * @vdec_ops: the VDEC operations that support this format
 * @codec_ops: the codec operations that support this format
 * @firmware_path: Path to the firmware that supports this format
 * @pixfmts_cap: list of CAPTURE pixel formats available with pixfmt
 */
struct amvdec_format {
	u32 profile_ctrl;
	u32 default_profile;
	u32 level_ctrl;
	u32 default_level;
	u32 profiles;
	u32 max_level;
	u32 pixfmt;
	u32 min_buffers;
	u32 max_buffers;
	u32 max_width;
	u32 max_height;
	/* Maximum coded bit depth; zero means no format-specific limit. */
	u8 max_bit_depth;
	u32 flags;

	struct amvdec_ops *vdec_ops;
	struct amvdec_codec_ops *codec_ops;

	char *firmware_path;
	u32 pixfmts_cap[4];
};

enum amvdec_status {
	STATUS_STOPPED,
	STATUS_INIT,
	STATUS_RUNNING,
};

/* No firmware buffer index is mapped to this vb2 buffer */
#define VB2_IDX_UNMAPPED	U32_MAX

/* Record request events in memory and dump them when a job stalls. */
#define AMVDEC_TRACE_LEN	128

struct amvdec_trace_ev {
	u64 ns;
	u32 id, a, b, c;
};

enum amvdec_trace_id {
	AMVDEC_TR_FEED = 1,	/* a = ring offset, b = bytes incl. padding, c = timestamp/1000 */
	AMVDEC_TR_FED,		/* a = write result, b = parser WP */
	AMVDEC_TR_READY,	/* a = endpoint, b = parked, c = status */
	AMVDEC_TR_IRQ,		/* a = status, b = shift byte count, c = LCU */
	AMVDEC_TR_KICK,		/* a = decode size, b = slice index, c = slice address */
	AMVDEC_TR_DONE,		/* a = shift byte count, b = decode size, c = LCU */
	AMVDEC_TR_RUN,		/* m2m device_run: next job handed to the driver */
	AMVDEC_TR_WORK,		/* feed worker entered: a = run armed, b = src ready */
	AMVDEC_TR_SIGNAL,	/* picture handed back to userspace: a = vb2 index */
};

/*
 * Per-request control copies and synthesised headers for esparser_queue(),
 * kept in the session rather than on its stack.  Only the feed worker
 * touches them, under sess->lock.
 */
struct amvdec_feed_scratch {
	struct h264_parse_scratch h264_parse;
	struct v4l2_ctrl_vp9_frame vp9_frame;
	struct v4l2_ctrl_hevc_sps hevc_sps;
	struct v4l2_ctrl_hevc_pps hevc_pps;
	struct v4l2_ctrl_hevc_slice_params hevc_slice;
	struct v4l2_ctrl_hevc_decode_params hevc_decode;
	struct v4l2_ctrl_mpeg2_sequence mpeg2_seq;
	struct v4l2_ctrl_mpeg2_picture mpeg2_pic;
	struct v4l2_ctrl_mpeg2_picture mpeg2_job_pic;
	struct v4l2_ctrl_mpeg2_quantisation mpeg2_quant;
	u8 headers[512];
};

/**
 * struct amvdec_session - decoding session parameters
 *
 * @core: reference to the vdec core struct
 * @fh: v4l2 file handle
 * @m2m_dev: v4l2 m2m device
 * @m2m_ctx: v4l2 m2m context
 * @ctrl_handler: V4L2 control handler
 * @lock: cap & out queues lock
 * @fmt_out: vdec pixel format for the OUTPUT queue
 * @pixfmt_cap: V4L2 pixel format for the CAPTURE queue
 * @src_buffer_size: size in bytes of the OUTPUT buffers' only plane
 * @width: current picture width
 * @height: current picture height
 * @colorspace: current colorspace
 * @ycbcr_enc: current ycbcr_enc
 * @quantization: current quantization
 * @xfer_func: current transfer function
 * @esparser_queued_bufs: number of buffers currently queued into ESPARSER
 * @esparser_queue_work: work struct for the ESPARSER to process src buffers
 * @streamon_cap: stream on flag for capture queue
 * @streamon_out: stream on flag for output queue
 * @sequence_cap: capture sequence counter
 * @sequence_out: output sequence counter
 * @should_stop: stop feeding during teardown
 * @num_fw_bufs: number of destination buffers mapped to firmware canvases
 * @canvas_alloc: array of all the canvas IDs allocated
 * @canvas_num: number of canvas IDs allocated
 * @vififo_vaddr: virtual address for the VIFIFO
 * @vififo_paddr: physical address for the VIFIFO
 * @vififo_size: size of the VIFIFO dma alloc
 * @timestamps: chronological list of src timestamps
 * @ts_spinlock: spinlock for the timestamps list
 * @last_irq_jiffies: tracks last time the vdec triggered an IRQ
 * @last_offset: tracks last offset of vififo
 * @wrap_count: number of times the vififo wrapped around
 * @fw_idx_to_vb2_idx: firmware buffer index to vb2 buffer index
 * @vb2_idx_to_fw_idx: vb2 buffer index to firmware buffer index, the
 *	inverse of @fw_idx_to_vb2_idx.  VB2_IDX_UNMAPPED for a buffer the
 *	firmware has no canvas for.
 * @fw_buf_canvas: canvas specification programmed for each firmware index
 * @fw_buf_paddr: plane DMA addresses the canvases of each firmware index
 *	describe
 * @status: current decoding status
 * @priv: codec private data
 */
struct amvdec_session {
	struct amvdec_trace_ev trace[AMVDEC_TRACE_LEN];
	atomic_t trace_pos;
	/* Stuck-request watch: see vdec_job_watch() */
	struct delayed_work job_watch;
	u64 run_ns;
	u32 run_seq;
	u32 watch_dumped_seq;
	bool watch_on;
	struct amvdec_core *core;

	struct v4l2_fh fh;
	struct v4l2_m2m_dev *m2m_dev;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_ctrl_handler ctrl_handler;
	/* Serialize buffer queues, feed work and stream transitions. */
	struct mutex lock;

	const struct amvdec_format *fmt_out;
	u32 pixfmt_cap;
	u32 src_buffer_size;

	u32 width;
	u32 height;
	u32 colorspace;
	u32 bitdepth;
	u8 ycbcr_enc;
	u8 quantization;
	u8 xfer_func;

	atomic_t esparser_queued_bufs;
	struct work_struct esparser_queue_work;
	/*
	 * Only device_run() arms a feed. STREAMON work must not consume input
	 * before the request controls are applied.
	 */
	bool m2m_run_armed;
	/* HEVC/VP9: exactly one request owns the hardware until picture done.
	 * Input and reference identity must not follow mutable vb2 timestamps.
	 * state: 0 idle, 1 active, 2 completed (retired by the feed worker).
	 */
	struct {
		atomic_t state;
		struct vb2_v4l2_buffer *src, *dst;
		struct vb2_buffer *refs[16];
		u64 ref_ts[16];
		u64 timestamp;
		bool error, credit;
		struct v4l2_ctrl_hevc_slice_params *slices;
		unsigned int num_slices;
		void *input_vaddr;
		dma_addr_t input_paddr;
		size_t input_size;
	} request_job;
	struct amvdec_feed_scratch feed;
	/* Synthetic completion metadata, protected by ts_spinlock. */
	struct vb2_v4l2_buffer metadata_src;
	u8 hevc_cached_headers[8192];
	u32 hevc_cached_headers_len;
	bool hevc_headers_valid;

	unsigned int streamon_cap, streamon_out;
	unsigned int sequence_cap, sequence_out;

	unsigned int should_stop;

	unsigned int num_fw_bufs;

	u8 canvas_alloc[MAX_CANVAS];
	u32 canvas_num;

	void *vififo_vaddr;
	dma_addr_t vififo_paddr;
	u32 vififo_size;

	struct list_head timestamps;
	spinlock_t ts_spinlock; /* timestamp list lock */

	u64 last_irq_jiffies;
	u32 last_offset;
	u32 wrap_count;
	u32 fw_idx_to_vb2_idx[32];
	u32 vb2_idx_to_fw_idx[32];
	u32 fw_buf_canvas[32];
	dma_addr_t fw_buf_paddr[32][3];

	enum amvdec_status status;
	void *priv;
};

static inline struct amvdec_session *file_to_amvdec_session(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct amvdec_session, fh);
}

u32 meson_amvdec_get_output_size(struct amvdec_session *sess);

void meson_amvdec_trace(struct amvdec_session *sess, u32 id, u32 a, u32 b, u32 c);
void meson_amvdec_trace_dump(struct amvdec_session *sess, const char *why);

#endif
