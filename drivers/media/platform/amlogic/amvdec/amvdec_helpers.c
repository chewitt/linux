// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Shared decoder, canvas and DMA helpers.
 */

#include <linux/gcd.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>

#include "amvdec_helpers.h"

#define NUM_CANVAS_NV12 2
#define NUM_CANVAS_YUV420 3

#define DMA_GUARD_PATTERN 0x5a

/*
 * Optional DMA guards surround coherent HEVC-core allocations and are
 * checked after hardware access. The layout is fixed at module load.
 */
static bool dma_guards;
module_param(dma_guards, bool, 0444);
MODULE_PARM_DESC(dma_guards,
		 "Debug: add guard pages around HEVC-core coherent allocations and check them (default off)");

bool meson_amvdec_dma_guards_enabled(void)
{
	return READ_ONCE(dma_guards);
}

static void amvdec_dma_fill_guards(const struct amvdec_dma_guard *guard)
{
	if (!guard->raw_vaddr || !guard->raw_size)
		return;
	memset(guard->raw_vaddr, DMA_GUARD_PATTERN, guard->prefix_size);
	memset((u8 *)guard->raw_vaddr + guard->prefix_size + guard->size,
	       DMA_GUARD_PATTERN, PAGE_SIZE);
}

void *meson_amvdec_dma_alloc_guarded(struct device *dev, size_t size,
			       dma_addr_t *dma_handle, gfp_t gfp,
				       struct amvdec_dma_guard *guard)
{
	void *raw;
	dma_addr_t raw_dma, visible;
	size_t aligned = PAGE_ALIGN(size);
	size_t raw_size, alignment;

	memset(guard, 0, sizeof(*guard));
	if (!meson_amvdec_dma_guards_enabled()) {
		raw = dma_alloc_coherent(dev, size, dma_handle, gfp);
		if (raw) {
			guard->raw_vaddr = raw;
			guard->raw_paddr = *dma_handle;
			guard->raw_size = size;
			guard->size = size;
		}
		return raw;
	}

	/*
	 * Preserve 64 KiB alignment for large GXBB HEVC allocations. Include any
	 * alignment padding in the prefix guard; smaller buffers remain page-aligned.
	 */
	alignment = aligned >= SZ_64K ? SZ_64K : PAGE_SIZE;
	if (aligned > SIZE_MAX - alignment - PAGE_SIZE)
		return NULL;
	raw_size = alignment + aligned + PAGE_SIZE;
	raw = dma_alloc_coherent(dev, raw_size, &raw_dma, gfp);
	if (!raw)
		return NULL;

	visible = ALIGN(raw_dma + PAGE_SIZE, alignment);
	guard->raw_vaddr = raw;
	guard->raw_paddr = raw_dma;
	guard->raw_size = raw_size;
	guard->size = aligned;
	guard->prefix_size = visible - raw_dma;
	*dma_handle = visible;
	amvdec_dma_fill_guards(guard);

	return (u8 *)raw + guard->prefix_size;
}

void meson_amvdec_dma_free_guarded(struct device *dev, dma_addr_t visible_paddr,
			     struct amvdec_dma_guard *guard)
{
	if (!guard->raw_vaddr)
		return;

	dma_free_coherent(dev, guard->raw_size, guard->raw_vaddr,
			  guard->raw_paddr ? guard->raw_paddr : visible_paddr);
	memset(guard, 0, sizeof(*guard));
}

int meson_amvdec_dma_verify_guard(struct device *dev, const char *name,
			    const struct amvdec_dma_guard *guard)
{
	const u8 *before;
	const u8 *after;
	size_t off;
	int ret = 0;

	if (!meson_amvdec_dma_guards_enabled() || !guard->raw_vaddr ||
	    !guard->prefix_size ||
	    guard->raw_size < guard->prefix_size + guard->size + PAGE_SIZE)
		return 0;

	before = guard->raw_vaddr;
	after = (const u8 *)guard->raw_vaddr + guard->prefix_size + guard->size;

	/*
	 * The guard memory is uncached: scan it word-wise first and walk it a
	 * byte at a time only to report what changed.
	 */
	if (!memchr_inv(before, DMA_GUARD_PATTERN, guard->prefix_size) &&
	    !memchr_inv(after, DMA_GUARD_PATTERN, PAGE_SIZE))
		return 0;

	/* offsets are reported relative to the start of the prefix guard */
	for (off = 0; off < guard->prefix_size; ) {
		size_t start, end;

		while (off < guard->prefix_size && before[off] == DMA_GUARD_PATTERN)
			off++;
		if (off == guard->prefix_size)
			break;
		start = off;
		while (off < guard->prefix_size && before[off] != DMA_GUARD_PATTERN)
			off++;
		end = off;
		dev_err(dev, "DMA guard changed %s before offset=%zu length=%zu\n",
			name, start, end - start);
		ret = -EIO;
	}

	for (off = 0; off < PAGE_SIZE; ) {
		size_t start, end;

		while (off < PAGE_SIZE && after[off] == DMA_GUARD_PATTERN)
			off++;
		if (off == PAGE_SIZE)
			break;
		start = off;
		while (off < PAGE_SIZE && after[off] != DMA_GUARD_PATTERN)
			off++;
		end = off;
		dev_err(dev, "DMA guard changed %s after offset=%zu length=%zu\n",
			name, start, end - start);
		ret = -EIO;
	}

	return ret;
}

/*
 * G12 DMC offsets are word-addressed: DMC_REQ_CTRL is word 0 and
 * DMC_CHAN_STS is word 0x36 (byte 0xd8). Bits 15:0 select axibus channels
 * and bits 23:16 select ambus channels; a set status bit means idle.
 */
#define G12A_DMC_BASE		0xff638000
#define G12A_DMC_SIZE		0x100
	#define DMC_REQ_CTRL	0x00
	#define DMC_CHAN_STS	0xd8

/* RESET7 pulse register; bits 15:11 are the DMC decode pipelines */
#define G12A_RESET7_ADDR	0xffd01020
	#define RESET7_DMC_PIPEL	GENMASK(15, 11)

static void __iomem *amvdec_map(u64 addr, u32 size, void __iomem **cache)
{
	if (!*cache)
		*cache = ioremap(addr, size);
	return *cache;
}

int meson_amvdec_dmc_park(struct amvdec_core *core, u32 mask)
{
	static void __iomem *dmc_base;
	u32 val;
	int i;

	/* G12 physical addresses: not the reset/DMC blocks of older SoCs */
	if (!amvdec_is_g12(core))
		return -EOPNOTSUPP;

	if (!amvdec_map(G12A_DMC_BASE, G12A_DMC_SIZE, &dmc_base))
		return -ENOMEM;

	val = readl(dmc_base + DMC_REQ_CTRL);
	writel(val & ~mask, dmc_base + DMC_REQ_CTRL);

	for (i = 0; i < 100; i++) {
		if ((readl(dmc_base + DMC_CHAN_STS) & mask) == mask)
			return 0;
		udelay(10);
	}

	dev_warn(core->dev, "DMC ports %08x did not idle before reset\n", mask);
	return -ETIMEDOUT;
}

void meson_amvdec_dmc_unpark(struct amvdec_core *core, u32 mask)
{
	static void __iomem *dmc_base;
	u32 val;

	/* G12 physical addresses: not the reset/DMC blocks of older SoCs */
	if (!amvdec_is_g12(core))
		return;

	if (!amvdec_map(G12A_DMC_BASE, G12A_DMC_SIZE, &dmc_base))
		return;

	val = readl(dmc_base + DMC_REQ_CTRL);
	writel(val | mask, dmc_base + DMC_REQ_CTRL);
}

void meson_amvdec_dmc_pipeline_reset(struct amvdec_core *core)
{
	static void __iomem *reset7;

	/* G12 physical addresses: not the reset/DMC blocks of older SoCs */
	if (!amvdec_is_g12(core))
		return;

	if (!amvdec_map(G12A_RESET7_ADDR, 4, &reset7))
		return;

	writel(RESET7_DMC_PIPEL, reset7);
}

u32 meson_amvdec_read_dos(struct amvdec_core *core, u32 reg)
{
	return readl_relaxed(core->dos_base + reg);
}

void meson_amvdec_write_dos(struct amvdec_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->dos_base + reg);
}

void meson_amvdec_write_dos_action(struct amvdec_core *core, u32 reg, u32 val)
{
	/* Order register and DMA writes before the firmware action doorbell. */
	dma_wmb();
	meson_amvdec_write_dos(core, reg, val);
}

void meson_amvdec_write_dos_bits(struct amvdec_core *core, u32 reg, u32 val)
{
	meson_amvdec_write_dos(core, reg, meson_amvdec_read_dos(core, reg) | val);
}

void meson_amvdec_clear_dos_bits(struct amvdec_core *core, u32 reg, u32 val)
{
	meson_amvdec_write_dos(core, reg, meson_amvdec_read_dos(core, reg) & ~val);
}

u32 meson_amvdec_read_parser(struct amvdec_core *core, u32 reg)
{
	return readl_relaxed(core->esparser_base + reg);
}

void meson_amvdec_write_parser(struct amvdec_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->esparser_base + reg);
}

/* AMFBC body is made out of 64x32 blocks with varying block size */
u32 meson_amvdec_amfbc_body_size(u32 width, u32 height, u32 is_10bit, u32 use_mmu)
{
	u32 width_64 = ALIGN(width, 64) / 64;
	u32 height_32 = ALIGN(height, 32) / 32;
	u32 blk_size = 4096;

	if (!is_10bit) {
		if (use_mmu)
			blk_size = 3200;
		else
			blk_size = 3072;
	}

	return blk_size * width_64 * height_32;
}

/* 32 bytes per 128x64 block */
u32 meson_amvdec_amfbc_head_size(u32 width, u32 height)
{
	u32 width_128 = ALIGN(width, 128) / 128;
	u32 height_64 = ALIGN(height, 64) / 64;

	return 32 * width_128 * height_64;
}

u32 meson_amvdec_amfbc_size(u32 width, u32 height, u32 is_10bit, u32 use_mmu)
{
	return ALIGN(meson_amvdec_amfbc_body_size(width, height, is_10bit, use_mmu) +
		     meson_amvdec_amfbc_head_size(width, height), SZ_64K);
}

static int canvas_alloc(struct amvdec_session *sess, u8 *canvas_id)
{
	int ret;

	if (sess->canvas_num >= MAX_CANVAS) {
		dev_err(sess->core->dev, "Reached max number of canvas\n");
		return -ENOMEM;
	}

	ret = meson_canvas_alloc(sess->core->canvas, canvas_id);
	if (ret)
		return ret;

	sess->canvas_alloc[sess->canvas_num++] = *canvas_id;
	return 0;
}

/*
 * Program the canvases of one CAPTURE buffer and write their IDs to @reg.
 * A nonzero *@spec names canvases already allocated for this slot, which
 * are reprogrammed in place; otherwise new ones are allocated and *@spec
 * is set.
 */
static int set_canvas_yuv420m(struct amvdec_session *sess,
			      struct vb2_buffer *vb, u32 width,
			      u32 height, u32 reg, u32 *spec)
{
	struct amvdec_core *core = sess->core;
	u8 canvas_id[NUM_CANVAS_YUV420]; /* Y U V */
	dma_addr_t buf_paddr[NUM_CANVAS_YUV420]; /* Y U V */
	int ret, i;

	for (i = 0; i < NUM_CANVAS_YUV420; ++i) {
		if (*spec) {
			canvas_id[i] = (*spec >> (8 * i)) & 0xff;
		} else {
			ret = canvas_alloc(sess, &canvas_id[i]);
			if (ret)
				return ret;
		}

		buf_paddr[i] =
		    vb2_dma_contig_plane_dma_addr(vb, i);
	}

	/* Y plane */
	meson_canvas_config(core->canvas, canvas_id[0], buf_paddr[0],
			    width, height, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	/* U plane */
	meson_canvas_config(core->canvas, canvas_id[1], buf_paddr[1],
			    width / 2, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	/* V plane */
	meson_canvas_config(core->canvas, canvas_id[2], buf_paddr[2],
			    width / 2, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	*spec = (canvas_id[2] << 16) | (canvas_id[1] << 8) | canvas_id[0];
	if (reg)
		meson_amvdec_write_dos(core, reg, *spec);

	return 0;
}

static int set_canvas_nv12m(struct amvdec_session *sess,
			    struct vb2_buffer *vb, u32 width,
			    u32 height, u32 reg, u32 *spec)
{
	struct amvdec_core *core = sess->core;
	u8 canvas_id[NUM_CANVAS_NV12]; /* Y U/V */
	dma_addr_t buf_paddr[NUM_CANVAS_NV12]; /* Y U/V */
	int ret, i;

	for (i = 0; i < NUM_CANVAS_NV12; ++i) {
		if (*spec) {
			canvas_id[i] = (*spec >> (8 * i)) & 0xff;
		} else {
			ret = canvas_alloc(sess, &canvas_id[i]);
			if (ret)
				return ret;
		}

		buf_paddr[i] =
		    vb2_dma_contig_plane_dma_addr(vb, i);
	}

	/* Y plane */
	meson_canvas_config(core->canvas, canvas_id[0], buf_paddr[0],
			    width, height, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	/* U/V plane */
	meson_canvas_config(core->canvas, canvas_id[1], buf_paddr[1],
			    width, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	*spec = (canvas_id[1] << 16) | (canvas_id[1] << 8) | canvas_id[0];
	if (reg)
		meson_amvdec_write_dos(core, reg, *spec);

	return 0;
}

/*
 * Give one CAPTURE buffer a canvas pair of its own and return the canvas
 * specification (Y | UV << 8 | UV << 16) without writing it anywhere.  Used
 * by codecs whose host names the destination and references per picture.
 * Pass the previous spec to reconfigure the same canvases in place.
 */
int meson_amvdec_canvas_nv12m_spec(struct amvdec_session *sess, struct vb2_buffer *vb,
			     u32 width, u32 height, u32 *spec)
{
	struct amvdec_core *core = sess->core;
	u8 id[NUM_CANVAS_NV12];
	int ret, i;

	if (*spec) {
		id[0] = *spec & 0xff;
		id[1] = (*spec >> 8) & 0xff;
	} else {
		for (i = 0; i < NUM_CANVAS_NV12; i++) {
			ret = canvas_alloc(sess, &id[i]);
			if (ret)
				return ret;
		}
	}
	meson_canvas_config(core->canvas, id[0],
			    vb2_dma_contig_plane_dma_addr(vb, 0),
			    width, height, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);
	meson_canvas_config(core->canvas, id[1],
			    vb2_dma_contig_plane_dma_addr(vb, 1),
			    width, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);
	*spec = (id[1] << 16) | (id[1] << 8) | id[0];
	return 0;
}

/* Remember the plane addresses the canvases of @fw_idx now describe. */
static void amvdec_record_fw_buf_paddr(struct amvdec_session *sess,
				       u32 fw_idx, struct vb2_buffer *vb)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sess->fw_buf_paddr[fw_idx]); i++)
		sess->fw_buf_paddr[fw_idx][i] = i < vb->num_planes ?
			vb2_dma_contig_plane_dma_addr(vb, i) : 0;
}

static bool amvdec_fw_buf_paddr_match(struct amvdec_session *sess,
				      u32 fw_idx, struct vb2_buffer *vb)
{
	unsigned int i;

	for (i = 0; i < vb->num_planes; i++)
		if (sess->fw_buf_paddr[fw_idx][i] !=
		    vb2_dma_contig_plane_dma_addr(vb, i))
			return false;

	return true;
}

/*
 * Map the prepared CAPTURE buffer when the decoder selects it. Keep its
 * firmware index for the session so reference lists remain valid as the
 * client grows the buffer pool.
 */
int meson_amvdec_map_capture_buffer(struct amvdec_session *sess,
			      struct vb2_buffer *vb,
			      u32 reg_base[], u32 reg_num[])
{
	u32 pixfmt = sess->pixfmt_cap;
	u32 width = ALIGN(sess->width, 64);
	u32 height = ALIGN(sess->height, 32);
	u32 reg_cur, reg_num_cur, reg_base_cur;
	unsigned int i;
	int fw_idx, ret;

	if (!vb || vb->index >= ARRAY_SIZE(sess->vb2_idx_to_fw_idx))
		return -EINVAL;

	/* First buffer of the session: nothing is mapped yet. */
	if (!sess->num_fw_bufs) {
		for (i = 0; i < ARRAY_SIZE(sess->fw_idx_to_vb2_idx); ++i)
			sess->fw_idx_to_vb2_idx[i] = VB2_IDX_UNMAPPED;
		for (i = 0; i < ARRAY_SIZE(sess->vb2_idx_to_fw_idx); ++i)
			sess->vb2_idx_to_fw_idx[i] = VB2_IDX_UNMAPPED;
	}

	if (vb->num_planes > ARRAY_SIZE(sess->fw_buf_paddr[0]))
		return -EINVAL;
	for (i = 0; i < vb->num_planes; i++) {
		if (!vb->planes[i].mem_priv) {
			dev_err(sess->core->dev,
				"CAPTURE buffer %u plane %u has no memory\n",
				vb->index, i);
			return -EINVAL;
		}
	}

	fw_idx = sess->vb2_idx_to_fw_idx[vb->index];
	if (fw_idx != VB2_IDX_UNMAPPED) {
		/*
		 * Already mapped.  A DMABUF index can be requeued with other
		 * memory behind it: reprogram its canvases in place if so.
		 */
		if (amvdec_fw_buf_paddr_match(sess, fw_idx, vb))
			return 0;
	} else {
		fw_idx = sess->num_fw_bufs;
		if (fw_idx >= ARRAY_SIZE(sess->fw_idx_to_vb2_idx)) {
			dev_err(sess->core->dev,
				"Too many CAPTURE buffers to map (fw %d)\n", fw_idx);
			return -EINVAL;
		}
		sess->fw_buf_canvas[fw_idx] = 0;
	}

	/* Walk reg_base[]/reg_num[] to the register slot for this index. */
	reg_base_cur = 0;
	reg_num_cur = fw_idx;
	while (reg_base[reg_base_cur] && reg_num_cur >= reg_num[reg_base_cur]) {
		reg_num_cur -= reg_num[reg_base_cur];
		reg_base_cur++;
	}
	if (!reg_base[reg_base_cur])
		return -EINVAL;
	reg_cur = reg_base[reg_base_cur] + reg_num_cur * 4;

	switch (pixfmt) {
	case V4L2_PIX_FMT_NV12M:
		ret = set_canvas_nv12m(sess, vb, width, height, reg_cur,
				       &sess->fw_buf_canvas[fw_idx]);
		break;
	case V4L2_PIX_FMT_YUV420M:
		ret = set_canvas_yuv420m(sess, vb, width, height, reg_cur,
					 &sess->fw_buf_canvas[fw_idx]);
		break;
	default:
		dev_err(sess->core->dev, "Unsupported pixfmt %08X\n", pixfmt);
		return -EINVAL;
	}
	if (ret)
		return ret;

	amvdec_record_fw_buf_paddr(sess, fw_idx, vb);
	if (fw_idx == sess->num_fw_bufs) {
		sess->fw_idx_to_vb2_idx[fw_idx] = vb->index;
		sess->vb2_idx_to_fw_idx[vb->index] = fw_idx;
		sess->num_fw_bufs = fw_idx + 1;
	}
	return 0;
}

/*
 * Complete the request control handler before returning OUTPUT. Normal
 * vb2 buffer completion does not complete the control-handler object.
 */
void meson_amvdec_src_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf,
			 enum vb2_buffer_state state)
{
	if (vbuf->vb2_buf.req_obj.req)
		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &sess->ctrl_handler);
	v4l2_m2m_buf_done(vbuf, state);
}

int meson_amvdec_add_ts(struct amvdec_session *sess, u64 ts,
		  struct v4l2_timecode tc, u32 offset, u32 vbuf_flags,
		  const struct v4l2_ctrl_vp9_frame *vp9_frame,
		  const struct v4l2_ctrl_hevc_sps *hevc_sps,
		  const struct v4l2_ctrl_hevc_pps *hevc_pps,
		  const struct v4l2_ctrl_hevc_slice_params *hevc_slice,
		  const struct v4l2_ctrl_hevc_decode_params *hevc_decode)
{
	struct amvdec_timestamp *new_ts;
	unsigned long flags;

	new_ts = kzalloc_obj(*new_ts);
	if (!new_ts)
		return -ENOMEM;

	new_ts->ts = ts;
	new_ts->tc = tc;
	new_ts->offset = offset;
	new_ts->flags = vbuf_flags;
	if (vp9_frame) {
		new_ts->vp9_frame = *vp9_frame;
		new_ts->has_vp9_frame = true;
	}
	if (hevc_sps && hevc_pps && hevc_slice && hevc_decode) {
		new_ts->hevc_sps = *hevc_sps;
		new_ts->hevc_pps = *hevc_pps;
		new_ts->hevc_slice = *hevc_slice;
		new_ts->hevc_decode = *hevc_decode;
		new_ts->has_hevc_slice = true;
	}

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	list_add_tail(&new_ts->list, &sess->timestamps);
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
	return 0;
}

int meson_amvdec_set_input_end(struct amvdec_session *sess, u64 ts, u32 end)
{
	struct amvdec_timestamp *tmp;
	unsigned long flags;
	int ret = -ENOENT;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	list_for_each_entry(tmp, &sess->timestamps, list) {
		if (tmp->ts == ts) {
			tmp->input_end = end;
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
	return ret;
}

void meson_amvdec_remove_ts(struct amvdec_session *sess, u64 ts)
{
	struct amvdec_timestamp *tmp;
	unsigned long flags;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	list_for_each_entry(tmp, &sess->timestamps, list) {
		if (tmp->ts == ts) {
			list_del(&tmp->list);
			kfree(tmp);
			goto unlock;
		}
	}
	/* Cleanup is idempotent: preparation may already have consumed the record. */
	dev_dbg(sess->core->dev_dec, "Timestamp %llu already removed\n", ts);

unlock:
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
}

/*
 * Discard timestamp snapshots when the stream offset restarts at zero.
 * Call only with the firmware parked after a completed picture and no input
 * queued, so no active request loses its controls.
 */
unsigned int meson_amvdec_flush_ts(struct amvdec_session *sess)
{
	struct amvdec_timestamp *tmp, *n;
	unsigned int dropped = 0;
	unsigned long flags;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	list_for_each_entry_safe(tmp, n, &sess->timestamps, list) {
		list_del(&tmp->list);
		kfree(tmp);
		dropped++;
	}
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);

	return dropped;
}

static void dst_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf,
			 u32 field, u64 timestamp,
			 struct v4l2_timecode timecode, u32 flags)
{
	struct device *dev = sess->core->dev_dec;
	u32 output_size = meson_amvdec_get_output_size(sess);

	switch (sess->pixfmt_cap) {
	case V4L2_PIX_FMT_NV12M:
		vb2_set_plane_payload(&vbuf->vb2_buf, 0, output_size);
		vb2_set_plane_payload(&vbuf->vb2_buf, 1, output_size / 2);
		break;
	case V4L2_PIX_FMT_YUV420M:
		vb2_set_plane_payload(&vbuf->vb2_buf, 0, output_size);
		vb2_set_plane_payload(&vbuf->vb2_buf, 1, output_size / 4);
		vb2_set_plane_payload(&vbuf->vb2_buf, 2, output_size / 4);
		break;
	case V4L2_PIX_FMT_AM21C:
		vb2_set_plane_payload(&vbuf->vb2_buf, 0, AM21C_HEADER_SIZE);
		break;
	}

	vbuf->vb2_buf.timestamp = timestamp;
	vbuf->sequence = sess->sequence_cap++;
	vbuf->flags = flags;
	vbuf->timecode = timecode;

	dev_dbg(dev, "Buffer %u done, ts = %llu, flags = %08X\n",
		vbuf->vb2_buf.index, timestamp, flags);
	vbuf->field = field;
	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);

	/* Wake the feed worker after returning CAPTURE. */
	schedule_work(&sess->esparser_queue_work);
}

bool meson_amvdec_request_jobs(struct amvdec_session *sess)
{
	return sess->fmt_out->pixfmt == V4L2_PIX_FMT_HEVC_SLICE ||
	       sess->fmt_out->pixfmt == V4L2_PIX_FMT_VP9_FRAME ||
	       sess->fmt_out->pixfmt == V4L2_PIX_FMT_MPEG2_SLICE;
}

int meson_amvdec_request_begin(struct amvdec_session *sess, struct vb2_v4l2_buffer *src)
{
	if (WARN_ON(atomic_read(&sess->request_job.state)))
		return -EBUSY;
	sess->request_job.dst = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
	if (!sess->request_job.dst)
		return -ENOBUFS;
	sess->request_job.src = src;
	sess->request_job.timestamp = src->vb2_buf.timestamp;
	sess->request_job.error = false;
	sess->request_job.credit = false;
	memset(sess->request_job.refs, 0, sizeof(sess->request_job.refs));
	WRITE_ONCE(sess->m2m_run_armed, false);
	atomic_set(&sess->request_job.state, 1);
	return 0;
}

int meson_amvdec_request_resolve_ref(struct amvdec_session *sess, unsigned int index, u64 ts)
{
	struct vb2_buffer *vb;

	if (index >= ARRAY_SIZE(sess->request_job.refs))
		return -EINVAL;
	vb = vb2_find_buffer(v4l2_m2m_get_dst_vq(sess->m2m_ctx), ts);
	if (!vb || vb == &sess->request_job.dst->vb2_buf)
		return -ENOENT;
	sess->request_job.refs[index] = vb;
	sess->request_job.ref_ts[index] = ts;
	return 0;
}

void meson_amvdec_request_signal(struct amvdec_session *sess, struct vb2_v4l2_buffer *dst)
{
	if (WARN_ON(dst != sess->request_job.dst) ||
	    atomic_cmpxchg(&sess->request_job.state, 1, 2) != 1) {
		meson_amvdec_abort(sess);
		return;
	}
	/* The worker also owns feeding: retirement cannot race the DMA submit. */
	schedule_work(&sess->esparser_queue_work);
}

void meson_amvdec_request_retire(struct amvdec_session *sess, bool cancel)
{
	struct vb2_v4l2_buffer *src = sess->request_job.src;
	struct vb2_v4l2_buffer *dst = sess->request_job.dst;
	bool error = cancel || sess->request_job.error;
	unsigned int i;

	if (!src || (!cancel && atomic_read(&sess->request_job.state) != 2))
		return;
	if (error) {
		for (i = 0; i < dst->vb2_buf.num_planes; i++)
			vb2_set_plane_payload(&dst->vb2_buf, i, 0);
		v4l2_m2m_buf_done(dst, VB2_BUF_STATE_ERROR);
	} else {
		dst_buf_done(sess, dst, V4L2_FIELD_NONE,
			     sess->request_job.timestamp, src->timecode, src->flags);
	}
	meson_amvdec_remove_ts(sess, sess->request_job.timestamp);
	meson_amvdec_src_buf_done(sess, src, error ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	if (sess->request_job.credit)
		atomic_dec(&sess->esparser_queued_bufs);
	kfree(sess->request_job.slices);
	sess->request_job.slices = NULL;
	sess->request_job.num_slices = 0;
	sess->request_job.src = NULL;
	sess->request_job.dst = NULL;
	atomic_set(&sess->request_job.state, 0);
	v4l2_m2m_job_finish(sess->m2m_dev, sess->m2m_ctx);
}

void meson_amvdec_request_refuse(struct amvdec_session *sess)
{
	sess->request_job.error = true;
	atomic_set(&sess->request_job.state, 2);
	meson_amvdec_request_retire(sess, false);
}

/* Keep the offset record for reordered completion; copy controls at allocation,
 * before another picture can resolve this CAPTURE timestamp as a reference.
 */
int meson_amvdec_dst_buf_prepare_hevc(struct amvdec_session *sess,
				struct vb2_v4l2_buffer *vbuf, u32 offset,
			      struct v4l2_ctrl_hevc_slice_params *slice,
			      struct v4l2_ctrl_hevc_decode_params *decode, u32 *input_end,
			      u32 *input_offset)
{
	struct amvdec_timestamp *tmp, *match = NULL;
	struct vb2_v4l2_buffer *src = &sess->metadata_src;
	unsigned long flags;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	list_for_each_entry(tmp, &sess->timestamps, list) {
		if (tmp->offset > offset)
			break;
		match = tmp;
	}
	if (!match || !match->has_hevc_slice) {
		spin_unlock_irqrestore(&sess->ts_spinlock, flags);
		dev_err_ratelimited(sess->core->dev,
				    "HEVC no request snapshot at offset %u\n", offset);
		return -ENOENT;
	}
	*slice = match->hevc_slice;
	*decode = match->hevc_decode;
	*input_end = match->input_end;
	*input_offset = match->offset;
	src->vb2_buf.timestamp = match->ts;
	src->timecode = match->tc;
	src->flags = match->flags;
	src->field = V4L2_FIELD_NONE;
	v4l2_m2m_buf_copy_metadata(src, vbuf);
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
	dev_dbg(sess->core->dev,
		"prepare idx=%u timestamp=%llu copied=%u poc=%d offset=%u start=%u end=%u\n",
		 vbuf->vb2_buf.index, vbuf->vb2_buf.timestamp,
		 vbuf->vb2_buf.copied_timestamp, decode->pic_order_cnt_val,
		 offset, *input_offset, *input_end);
	return 0;
}

int meson_amvdec_dst_buf_prepare_stateless(struct amvdec_session *sess,
				     struct vb2_v4l2_buffer *vbuf,
				     struct v4l2_ctrl_vp9_frame *vp9_frame, u32 *input_end)
{
	struct amvdec_timestamp *tmp;
	struct list_head *timestamps = &sess->timestamps;
	struct vb2_v4l2_buffer *src_vbuf = &sess->metadata_src;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	if (list_empty(timestamps)) {
		dev_err_ratelimited(sess->core->dev_dec,
				    "Buffer %u prepared but timestamp list is empty\n",
			vbuf->vb2_buf.index);
		ret = -ENOENT;
		goto unlock;
	}

	tmp = list_first_entry(timestamps, struct amvdec_timestamp, list);
	if (!tmp->has_vp9_frame) {
		dev_err_ratelimited(sess->core->dev_dec,
				    "Buffer %u prepared without VP9_FRAME controls\n",
			vbuf->vb2_buf.index);
		ret = -EINVAL;
		goto unlock;
	}

	src_vbuf->vb2_buf.timestamp = tmp->ts;
	src_vbuf->timecode = tmp->tc;
	src_vbuf->flags = tmp->flags;
	src_vbuf->field = V4L2_FIELD_NONE;
	*vp9_frame = tmp->vp9_frame;
	*input_end = tmp->input_end;
	v4l2_m2m_buf_copy_metadata(src_vbuf, vbuf);

	list_del(&tmp->list);
	kfree(tmp);

unlock:
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
	if (ret)
		return ret;

	return 0;
}

void meson_amvdec_dst_buf_done_stateless(struct amvdec_session *sess,
				   struct vb2_v4l2_buffer *vbuf, u32 field)
{
	dst_buf_done(sess, vbuf, field, vbuf->vb2_buf.timestamp,
		     vbuf->timecode, vbuf->flags);
	atomic_dec(&sess->esparser_queued_bufs);
	v4l2_m2m_job_finish(sess->m2m_dev, sess->m2m_ctx);
}

void meson_amvdec_dst_buf_error_stateless(struct amvdec_session *sess,
				    struct vb2_v4l2_buffer *vbuf)
{
	if (meson_amvdec_request_jobs(sess)) {
		sess->request_job.error = true;
		meson_amvdec_abort(sess);
		return;
	}
	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	atomic_dec(&sess->esparser_queued_bufs);
	v4l2_m2m_job_finish(sess->m2m_dev, sess->m2m_ctx);
}

void meson_amvdec_abort(struct amvdec_session *sess)
{
	/* Identify the caller of a terminal decoding failure. */
	dev_warn_ratelimited(sess->core->dev, "decoding session aborted by %pS\n",
			     __builtin_return_address(0));
	vb2_queue_error(&sess->m2m_ctx->cap_q_ctx.q);
	vb2_queue_error(&sess->m2m_ctx->out_q_ctx.q);
}

void meson_amvdec_trace(struct amvdec_session *sess, u32 id, u32 a, u32 b, u32 c)
{
	u32 pos = (u32)atomic_inc_return(&sess->trace_pos) % AMVDEC_TRACE_LEN;
	struct amvdec_trace_ev *ev = &sess->trace[pos];

	ev->ns = ktime_get_ns();
	ev->id = id;
	ev->a = a;
	ev->b = b;
	ev->c = c;
}

void meson_amvdec_trace_dump(struct amvdec_session *sess, const char *why)
{
	static const char * const name[] = {
		"?", "feed", "fed", "ready", "irq", "kick", "done",
		"run", "work", "signal",
	};
	u32 end = (u32)atomic_read(&sess->trace_pos);
	u32 i;

	dev_dbg(sess->core->dev, "flight recorder (%s):\n", why);
	for (i = 1; i <= AMVDEC_TRACE_LEN; i++) {
		struct amvdec_trace_ev *ev = &sess->trace[(end + i) % AMVDEC_TRACE_LEN];

		if (!ev->id)
			continue;
		dev_dbg(sess->core->dev, "  %llu.%06llu %-5s %08x %08x %08x\n",
			ev->ns / NSEC_PER_SEC, (ev->ns % NSEC_PER_SEC) / 1000,
			ev->id < ARRAY_SIZE(name) ? name[ev->id] : "?",
			ev->a, ev->b, ev->c);
	}
}
