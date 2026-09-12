// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 */

#include <linux/dma-mapping.h>
#include <linux/mm.h>

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc_common.h"
#include "vdec_helpers.h"
#include "hevc_regs.h"

#define MMU_COMPRESS_HEADER_SIZE 0x48000
#define MMU_MAP_SIZE 0x4800

/*
 * Pooled chunk allocator for MMU-mode compressed bodies.
 *
 * In MMU mode the hardware reaches body pages only through the frame
 * page map, so the body needs no contiguity at all: it is assembled
 * from single pages, grouped into fixed-size chunks purely so the pool
 * has a manageable unit of bookkeeping.  That removes the 16 MiB
 * contiguous allocation a 4K 10-bit frame used to need, and makes
 * chunks fungible between sessions regardless of resolution or depth.
 *
 * The pages come from dma_alloc_pages() one at a time.  A PAGE_SIZE
 * request short-circuits dma_alloc_contiguous(), so it is served by the
 * page allocator and never touches CMA - which is what takes ~256 MiB
 * of a 4K 10-bit session out of the contended pool.  It is not
 * alloc_pages(): dma_alloc_pages() applies the device's DMA mask (this
 * one has the default 32-bit coherent mask) and retries from a lower
 * zone, a check raw page allocation would skip.
 *
 * The pages are CACHED, unlike the coherent range they replace, and
 * nothing in this driver ever reads or writes a body through the CPU -
 * only the decoder and the display do, at the point of coherency.  So
 * the cache is handled exactly twice: invalidate after allocation, to
 * drop the dirty lines dma_alloc_pages() leaves behind when it zeroes
 * the page (they would otherwise write back over decoded data), and
 * invalidate again before the page goes back to the allocator, so no
 * speculatively-loaded stale line outlives it.  Chunks moving through
 * the pool need no maintenance in between, because no CPU access
 * happens in between.
 *
 * Display safety is unchanged and still generational: a chunk released
 * by a session is NEVER handed straight back out, because the VD1
 * plane may still be scanning that frame (writing into it corrupts the
 * picture and can hang the display's FBC decompressor - a fatal DMC
 * wedge).  park[0] holds the most recently stopped session; it rotates
 * to park[1], and only park[1] returns to the free list, once a LATER
 * session has delivered a frame and thus proved the parked frames are
 * off-screen.
 */
#define FBC_CHUNK_PAGES	256u
#define FBC_CHUNK_SIZE	(FBC_CHUNK_PAGES << PAGE_SHIFT)	/* 1 MiB */

static unsigned int
codec_hevc_capture_buffers(struct amvdec_session *sess,
			   struct vb2_buffer *bufs[MAX_REF_PIC_NUM]);

struct fbc_chunk {
	struct list_head list;
	struct page **pages;	/* FBC_CHUNK_PAGES single pages */
	dma_addr_t  *dma;	/* their bus addresses, in the same order */
};

static struct {
	/* protects all fields; sessions are serialized but drain isn't */
	struct mutex lock;
	struct device *dev;
	struct list_head free;
	struct list_head park[2];
	u32 nr_total;
} fbc_pool = {
	.lock  = __MUTEX_INITIALIZER(fbc_pool.lock),
	.free  = LIST_HEAD_INIT(fbc_pool.free),
	.park  = { LIST_HEAD_INIT(fbc_pool.park[0]),
		   LIST_HEAD_INIT(fbc_pool.park[1]) },
};

static void fbc_chunk_free(struct device *dev, struct fbc_chunk *c)
{
	u32 i;

	if (c->pages) {
		for (i = 0; i < FBC_CHUNK_PAGES; ++i) {
			if (!c->pages[i])
				continue;
			/*
			 * The decoder wrote this page without going through
			 * the cache; drop anything the CPU may have pulled in
			 * speculatively before the page is reused.
			 */
			dma_sync_single_for_cpu(dev, c->dma[i], PAGE_SIZE,
						DMA_FROM_DEVICE);
			dma_free_pages(dev, PAGE_SIZE, c->pages[i], c->dma[i],
				       DMA_FROM_DEVICE);
		}
	}

	kfree(c->pages);
	kfree(c->dma);
	kfree(c);
}

static void fbc_chunks_release_locked(struct list_head *h)
{
	struct fbc_chunk *c, *n;

	list_for_each_entry_safe(c, n, h, list) {
		list_del(&c->list);
		fbc_chunk_free(fbc_pool.dev, c);
		fbc_pool.nr_total--;
	}
}

/*
 * Return everything the pool is not actually using to CMA.
 *
 * Holding chunks between sessions saves re-allocating them, but they are
 * 1 MiB pieces, and a session does not start with chunks: it starts with
 * a single contiguous ~8 MiB workspace.  A pool sitting on hundreds of
 * megabytes of 1 MiB holes leaves CMA with no run long enough for it, and
 * codec_hevc_start() fails with -ENOMEM.  Seeking is where this bites,
 * because a seek tears the session down and builds a new one while the
 * outgoing generation is still parked - the decoder simply never came
 * back, and the player kept its clock running over the last picture.
 *
 * Chunks are cheap to re-take from CMA and this only ever runs between
 * sessions, so give the memory back and let the next session shape it.
 * park[0] is left alone: those bodies may still be on a screen.
 */
void codec_hevc_fbc_pool_reclaim(void)
{
	mutex_lock(&fbc_pool.lock);
	if (fbc_pool.dev) {
		fbc_chunks_release_locked(&fbc_pool.free);
		fbc_chunks_release_locked(&fbc_pool.park[1]);
	}
	mutex_unlock(&fbc_pool.lock);
}
EXPORT_SYMBOL_GPL(codec_hevc_fbc_pool_reclaim);

void codec_hevc_fbc_pool_drain(void)
{
	mutex_lock(&fbc_pool.lock);
	if (fbc_pool.dev) {
		fbc_chunks_release_locked(&fbc_pool.free);
		fbc_chunks_release_locked(&fbc_pool.park[0]);
		fbc_chunks_release_locked(&fbc_pool.park[1]);
	}
	mutex_unlock(&fbc_pool.lock);
}
EXPORT_SYMBOL_GPL(codec_hevc_fbc_pool_drain);

/* Take a chunk from the free list, or make a new one.  Pool lock held. */
static struct fbc_chunk *fbc_chunk_get_locked(struct device *dev)
{
	struct fbc_chunk *c;
	u32 i;

	if (!list_empty(&fbc_pool.free)) {
		c = list_first_entry(&fbc_pool.free, struct fbc_chunk, list);
		list_del(&c->list);
		return c;
	}

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;

	c->pages = kcalloc(FBC_CHUNK_PAGES, sizeof(*c->pages), GFP_KERNEL);
	c->dma   = kcalloc(FBC_CHUNK_PAGES, sizeof(*c->dma), GFP_KERNEL);
	if (!c->pages || !c->dma)
		goto err;

	for (i = 0; i < FBC_CHUNK_PAGES; ++i) {
		/*
		 * __GFP_NORETRY, not bare GFP_KERNEL: an order-0 GFP_KERNEL
		 * allocation does not fail, it reclaims and then invokes the
		 * OOM killer.  A 4K session asks for ~65k of these, and a
		 * decode that cannot get memory must fail cleanly - the
		 * caller turns this into -ENOMEM - rather than kill the
		 * player to make room for itself.  __GFP_NOWARN keeps a
		 * handled failure off the console, as it did for the
		 * cma_alloc() path this replaces.
		 */
		c->pages[i] = dma_alloc_pages(dev, PAGE_SIZE, &c->dma[i],
					      DMA_FROM_DEVICE,
					      GFP_KERNEL | __GFP_NORETRY |
					      __GFP_NOWARN);
		if (!c->pages[i])
			goto err;

		/* Drop the dirty lines left by the zeroing above */
		dma_sync_single_for_device(dev, c->dma[i], PAGE_SIZE,
					   DMA_FROM_DEVICE);
	}

	fbc_pool.nr_total++;
	return c;

err:
	fbc_chunk_free(dev, c);
	return NULL;
}

/*
 * Same, but under memory pressure fall back on the parked generations.
 * Failing the session outright is worse than the residual risk of
 * reusing frames that are almost certainly off-screen by now - this is
 * the last resort before -ENOMEM.  Pool lock held.
 */
static struct fbc_chunk *fbc_chunk_get_pressure_locked(struct device *dev)
{
	struct fbc_chunk *c = fbc_chunk_get_locked(dev);

	if (c)
		return c;

	/*
	 * Only the aged-out generation may be recycled.
	 *
	 * park[0] is what the session that just stopped was displaying, and
	 * the CAPTURE buffers it backs can still be on screen - that is the
	 * entire reason it is parked instead of freed.  Handing those chunks
	 * to a new session lets its firmware DMA into memory the display is
	 * scanning out and, worse, leaves them on the pool's free list, where
	 * the next codec_hevc_free_fbc_buffers() dma_free_coherent()s them
	 * while they are still referenced.  Those pages go back to CMA live,
	 * and the damage surfaces later in whatever the allocator hands them
	 * to next - a BUG in set_buddy_order()/is_free_buddy_page() reached
	 * from an unrelated process's write path.
	 *
	 * park[1] has already been displaced by a newer generation, and is
	 * exactly what free_fbc_buffers() releases on the next session stop,
	 * so reusing it here matches that lifetime.
	 *
	 * If that is not enough, fail: a clean -ENOMEM is a far better
	 * outcome than corrupting memory this driver does not own.
	 */
	if (list_empty(&fbc_pool.park[1]))
		return NULL;

	list_splice_tail_init(&fbc_pool.park[1], &fbc_pool.free);
	dev_warn_once(dev,
		      "CMA pressure: recycled the aged-out FBC generation\n");

	return fbc_chunk_get_locked(dev);
}

/* Move every chunk this session holds onto @dst.  Pool lock held. */
static void fbc_chunks_park_locked(struct codec_hevc_common *comm,
				   struct list_head *dst)
{
	u32 i, j;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (!comm->fbc_chunks[i])
			continue;

		for (j = 0; j < comm->fbc_nr_chunks; ++j)
			if (comm->fbc_chunks[i][j])
				list_add_tail(&comm->fbc_chunks[i][j]->list,
					      dst);

		kfree(comm->fbc_chunks[i]);
		comm->fbc_chunks[i] = NULL;
	}

	comm->fbc_nr_chunks = 0;
}

dma_addr_t codec_hevc_fbc_body_addr(struct codec_hevc_common *comm, u32 idx)
{
	/*
	 * Vestigial in MMU mode - HEVCD_MPP_DECOMP_CTL2 is zeroed there and
	 * the hardware walks the page map instead - but still programmed,
	 * and codec_hevc_frame_buffer_backed() tests it for zero.  The
	 * body's first page is the faithful answer.
	 */
	if (comm->fbc_chunks[idx] && comm->fbc_chunks[idx][0])
		return comm->fbc_chunks[idx][0]->dma[0];

	return comm->fbc_buffer_paddr[idx];
}
EXPORT_SYMBOL_GPL(codec_hevc_fbc_body_addr);

const u16 vdec_hevc_parser_cmd[] = {
	0x0401,	0x8401,	0x0800,	0x0402,
	0x9002,	0x1423,	0x8CC3,	0x1423,
	0x8804,	0x9825,	0x0800,	0x04FE,
	0x8406,	0x8411,	0x1800,	0x8408,
	0x8409,	0x8C2A,	0x9C2B,	0x1C00,
	0x840F,	0x8407,	0x8000,	0x8408,
	0x2000,	0xA800,	0x8410,	0x04DE,
	0x840C,	0x840D,	0xAC00,	0xA000,
	0x08C0,	0x08E0,	0xA40E,	0xFC00,
	0x7C00
};

/* Configure decode head read mode */
void codec_hevc_setup_decode_head(struct amvdec_session *sess, int is_10bit)
{
	struct amvdec_core *core = sess->core;
	u32 use_mmu = codec_hevc_use_mmu(core->platform->revision,
					 sess->pixfmt_cap, is_10bit);
	u32 body_size = amvdec_amfbc_body_size(sess->width, sess->height,
					       is_10bit, use_mmu);
	u32 head_size = amvdec_amfbc_head_size(sess->width, sess->height);

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit)) {
		/* Enable 2-plane reference read mode */
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(31));
		return;
	}

	/* enable mem saving mode for 8-bit */
	if (!is_10bit)
		amvdec_write_dos_bits(core, HEVC_SAO_CTRL5, BIT(9));
	else
		amvdec_clear_dos_bits(core, HEVC_SAO_CTRL5, BIT(9));

	if (codec_hevc_use_mmu(core->platform->revision,
			       sess->pixfmt_cap, is_10bit))
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(4));
	else if (!is_10bit)
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(3));
	else
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, 0);

	/*
	 * In MMU mode the vendor driver programs DECOMP_CTL2 to zero;
	 * the body stride is irrelevant when the compressed body is
	 * reached through the frame MMU map.
	 */
	if (use_mmu)
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL2, 0);
	else if (core->platform->revision < VDEC_REVISION_SM1)
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL2, body_size / 32);
	amvdec_write_dos(core, HEVC_CM_BODY_LENGTH, body_size);
	amvdec_write_dos(core, HEVC_CM_HEADER_OFFSET, body_size);
	amvdec_write_dos(core, HEVC_CM_HEADER_LENGTH, head_size);
}
EXPORT_SYMBOL_GPL(codec_hevc_setup_decode_head);

static void codec_hevc_setup_buffers_gxbb(struct amvdec_session *sess,
					  struct codec_hevc_common *comm,
					  int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct vb2_buffer *bufs[MAX_REF_PIC_NUM];
	const bool fbc = codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit);
	dma_addr_t fill_y = 0, fill_uv = 0;
	unsigned int idx, n;
	int i;

	/*
	 * Unlike the auto-incrementing table on gxl and later, every entry
	 * here carries its own slot number, so the table can be - and must
	 * be - written for the whole index space: the reference lists
	 * address it by vb2_buf.index (index * 2 and index * 2 + 1 for the
	 * two-plane formats), whether or not that buffer happens to be
	 * queued right now.  A slot left unwritten reads as address zero
	 * and the MC engine predicts from physical page 0.
	 *
	 * A slot with no buffer behind it is never referenced by a correct
	 * decode; point it at a real buffer anyway so that a corrupt
	 * reference reads valid memory.
	 */
	n = codec_hevc_capture_buffers(sess, bufs);
	if (!n)
		return;

	for (idx = 0; idx < n; idx++) {
		if (!bufs[idx])
			continue;

		fill_y = fbc ? comm->fbc_buffer_paddr[idx] :
			       vb2_dma_contig_plane_dma_addr(bufs[idx], 0);
		if (!fill_y)
			continue;

		if (!fbc)
			fill_uv = vb2_dma_contig_plane_dma_addr(bufs[idx], 1);
		break;
	}

	if (!fill_y)
		return;

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0);

	for (idx = 0; idx < MAX_REF_PIC_NUM; idx++) {
		struct vb2_buffer *vb = idx < n ? bufs[idx] : NULL;
		dma_addr_t buf_y_paddr = 0;
		dma_addr_t buf_uv_paddr = 0;

		if (vb && fbc) {
			buf_y_paddr = comm->fbc_buffer_paddr[idx];
		} else if (vb) {
			buf_y_paddr = vb2_dma_contig_plane_dma_addr(vb, 0);
			buf_uv_paddr = vb2_dma_contig_plane_dma_addr(vb, 1);
		}

		if (!buf_y_paddr) {
			buf_y_paddr = fill_y;
			buf_uv_paddr = fill_uv;
		}

		if (fbc) {
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 buf_y_paddr | (idx << 8) | 1);
		} else {
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 buf_y_paddr | ((idx * 2) << 8) | 1);
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 buf_uv_paddr | ((idx * 2 + 1) << 8) |
					 1);
		}
	}

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; ++i)
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
}

static void codec_hevc_setup_buffers_gxl(struct amvdec_session *sess,
					 struct codec_hevc_common *comm,
					 int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct vb2_buffer *bufs[MAX_REF_PIC_NUM];
	u32 pixfmt_cap = sess->pixfmt_cap;
	const u32 revision = core->platform->revision;
	const bool fbc = codec_hevc_use_fbc(pixfmt_cap, is_10bit);
	dma_addr_t fill_y = 0, fill_uv = 0;
	unsigned int idx, n;
	int i;

	/*
	 * The ANC2AXI table auto-increments from slot 0, and the reference
	 * lists address it by vb2_buf.index (index*2 / index*2+1 for the
	 * two-plane formats).  So it must be written densely, one slot per
	 * index, exactly as the vendor driver does over MAX_REF_PIC_NUM -
	 * not one slot per queued buffer in queue order, which scrambles
	 * every slot after the first buffer that happens to be absent.
	 *
	 * A slot with no buffer behind it is never referenced by a correct
	 * decode; point it at a real buffer anyway so that a corrupt
	 * reference reads valid memory instead of address zero.
	 */
	n = codec_hevc_capture_buffers(sess, bufs);
	if (!n)
		return;

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR,
			 BIT(2) | BIT(1));

	for (idx = 0; idx < n; idx++) {
		struct vb2_buffer *vb = bufs[idx];
		dma_addr_t buf_y_paddr, buf_uv_paddr = 0;

		if (vb) {
			if (codec_hevc_use_downsample(pixfmt_cap, is_10bit)) {
				if (codec_hevc_use_mmu(revision, pixfmt_cap,
						       is_10bit))
					buf_y_paddr = comm->mmu_header_paddr[idx];
				else
					buf_y_paddr = comm->fbc_buffer_paddr[idx];
			} else {
				buf_y_paddr = vb2_dma_contig_plane_dma_addr(vb, 0);
			}
			if (!fbc)
				buf_uv_paddr =
					vb2_dma_contig_plane_dma_addr(vb, 1);

			if (!fill_y && buf_y_paddr) {
				fill_y = buf_y_paddr;
				fill_uv = buf_uv_paddr;
			}
		}

		if (!vb || !buf_y_paddr) {
			buf_y_paddr = fill_y;
			buf_uv_paddr = fill_uv;
		}

		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
				 buf_y_paddr >> 5);
		if (!fbc)
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
					 buf_uv_paddr >> 5);
	}

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; ++i)
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
}

void codec_hevc_free_mmu_headers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm)
{
	struct device *dev = sess->core->dev;
	int i;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (comm->mmu_header_vaddr[i]) {
			dma_free_coherent(dev, MMU_COMPRESS_HEADER_SIZE,
					  comm->mmu_header_vaddr[i],
					  comm->mmu_header_paddr[i]);
			comm->mmu_header_vaddr[i] = NULL;
		}
	}
}
EXPORT_SYMBOL_GPL(codec_hevc_free_mmu_headers);

/*
 * Collect the session's CAPTURE buffers, by index.
 *
 * Everything that programs a per-buffer resource - FBC bodies, MMU
 * headers, the ANC2AXI canvas table - used to walk only the buffers
 * QUEUED to the driver at that moment.  The canvas table is indexed by
 * vb2_buf.index (the HEVC and VP9 reference lists write
 * ref->vbuf->vb2_buf.index as the canvas id), so a walk over a queue
 * with a gap in it lands every later buffer in the wrong slot.  The
 * old rule that every buffer be queued before streaming hid that; a
 * zero-copy client that keeps frames on the display across a seek
 * breaks it, and then either deadlocks (if start is refused) or
 * decodes against scrambled references (if it is not).
 *
 * An MMAP buffer has a valid DMA address from allocation, queued or
 * not, so take every allocated buffer.  An imported DMABUF is mapped
 * only while queued, so for those keep the queued-only walk.
 *
 * Fills bufs[] by index (NULL for an index with no buffer) and returns
 * one past the highest index present, or 0 if there are none.
 */
static unsigned int
codec_hevc_capture_buffers(struct amvdec_session *sess,
			   struct vb2_buffer *bufs[MAX_REF_PIC_NUM])
{
	struct vb2_queue *q = v4l2_m2m_get_dst_vq(sess->m2m_ctx);
	struct v4l2_m2m_buffer *buf;
	unsigned int n = 0;
	unsigned int i;

	memset(bufs, 0, sizeof(*bufs) * MAX_REF_PIC_NUM);

	if (q->memory == VB2_MEMORY_MMAP) {
		for (i = 0; i < MAX_REF_PIC_NUM; i++) {
			struct vb2_buffer *vb = vb2_get_buffer(q, i);

			if (!vb)
				continue;
			bufs[i] = vb;
			n = i + 1;
		}
		return n;
	}

	v4l2_m2m_for_each_dst_buf(sess->m2m_ctx, buf) {
		struct vb2_buffer *vb = &buf->vb.vb2_buf;

		if (vb->index >= MAX_REF_PIC_NUM)
			continue;
		bufs[vb->index] = vb;
		if (vb->index + 1 > n)
			n = vb->index + 1;
	}
	return n;
}

static int codec_hevc_alloc_mmu_headers(struct amvdec_session *sess,
					struct codec_hevc_common *comm)
{
	struct device *dev = sess->core->dev;
	struct vb2_buffer *bufs[MAX_REF_PIC_NUM];
	unsigned int idx, n;

	n = codec_hevc_capture_buffers(sess, bufs);
	for (idx = 0; idx < n; idx++) {
		dma_addr_t paddr;
		void *vaddr;

		/* Per-index: resume() runs more than once per session */
		if (!bufs[idx] || comm->mmu_header_vaddr[idx])
			continue;

		/* Handled: failure frees what was taken and returns -ENOMEM. */
		vaddr = dma_alloc_coherent(dev, MMU_COMPRESS_HEADER_SIZE,
					   &paddr, GFP_KERNEL | __GFP_NOWARN);
		if (!vaddr) {
			codec_hevc_free_mmu_headers(sess, comm);
			return -ENOMEM;
		}

		comm->mmu_header_vaddr[idx] = vaddr;
		comm->mmu_header_paddr[idx] = paddr;
	}

	return 0;
}

void codec_hevc_free_fbc_buffers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm)
{
	struct device *dev = sess->core->dev;
	int i;

	mutex_lock(&fbc_pool.lock);
	fbc_pool.dev = dev;

	if (sess->sequence_cap == 0 ||
	    codec_hevc_use_downsample(sess->pixfmt_cap, sess->bitdepth == 10)) {
		/*
		 * Immediately reusable, for one of two reasons.
		 *
		 * Either this session never delivered a frame, so nothing of
		 * it can be on screen - or it is a downsampling session, and
		 * then its compressed bodies were never on screen either.
		 * Parking exists for AM21C, where the FBC body IS the surface
		 * the VD1 overlay scans out.  In downsample mode the decoder
		 * writes the body only for its own use and a second,
		 * uncompressed NV12 copy for everyone else (HEVC_DBLK_CFGB
		 * bit 8 vs bit 9), and it is the NV12 planes - ordinary vb2
		 * buffers, owned by userspace - that reach the display.  The
		 * body is private scratch and holding it back protects
		 * nothing.
		 *
		 * It is not cheap scratch, either: one 4K Main10 generation
		 * is ~358 MiB.  Measured on a VIM3 across a single session
		 * that had already finished, with no decoder process left
		 * alive, CmaFree 883 -> 525 MiB and the largest free run
		 * 220 -> 60 MiB.  The next 4K session then starts starved and
		 * fragmented: the same clip that scores 47.3 dB PSNR against
		 * a software decode on a clean boot scores 20.0 dB when it
		 * follows another 4K session.
		 *
		 * The still-displayed generations, if any, stay parked.
		 */
		fbc_chunks_park_locked(comm, &fbc_pool.free);
	} else {
		/*
		 * This session's frames took the screen, so whatever park[0]
		 * holds is off-plane now: age it out and park the current
		 * chunks as the new generation.
		 */
		list_splice_tail_init(&fbc_pool.park[1], &fbc_pool.free);
		list_splice_tail_init(&fbc_pool.park[0], &fbc_pool.park[1]);
		fbc_chunks_park_locked(comm, &fbc_pool.park[0]);
	}

	/*
	 * Anything now on the free list is neither in use nor parked for
	 * the display, so hand it back to the allocator rather than
	 * squatting on it: CMA is shared, and a pool that keeps its
	 * high-water mark starves every other consumer (a 4K H.264 session
	 * needs ~288 MB of its own and fails STREAMON without it).
	 */
	fbc_chunks_release_locked(&fbc_pool.free);
	mutex_unlock(&fbc_pool.lock);

	/* Non-MMU FBC bodies are contiguous and privately owned */
	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (!comm->fbc_buffer_vaddr[i])
			continue;
		dma_free_coherent(dev, comm->fbc_buffer_size,
				  comm->fbc_buffer_vaddr[i],
				  comm->fbc_buffer_paddr[i]);
		comm->fbc_buffer_vaddr[i] = NULL;
	}

	if (comm->mmu_map_vaddr) {
		dma_free_coherent(dev, MMU_MAP_SIZE,
				  comm->mmu_map_vaddr,
				  comm->mmu_map_paddr);
		comm->mmu_map_vaddr = NULL;
	}

	codec_hevc_free_mmu_headers(sess, comm);
}
EXPORT_SYMBOL_GPL(codec_hevc_free_fbc_buffers);

static int codec_hevc_alloc_fbc_buffers(struct amvdec_session *sess,
					struct codec_hevc_common *comm)
{
	struct device *dev = sess->core->dev;
	struct vb2_buffer *bufs[MAX_REF_PIC_NUM];
	unsigned int idx, n;
	u32 use_mmu;
	u32 am21_size;
	u32 nr_chunks;
	const u32 revision = sess->core->platform->revision;
	const u32 is_10bit = sess->bitdepth == 10 ? 1 : 0;
	int ret;

	use_mmu = codec_hevc_use_mmu(revision, sess->pixfmt_cap, is_10bit);

	am21_size = amvdec_amfbc_size(sess->width, sess->height, is_10bit,
				      use_mmu);
	comm->fbc_buffer_size = am21_size;

	/*
	 * A NEW session starting is proof that the older parked generation
	 * is off every screen - recycle it here so parked memory stays
	 * bounded to a single generation.
	 */
	mutex_lock(&fbc_pool.lock);
	fbc_pool.dev = dev;
	list_splice_tail_init(&fbc_pool.park[1], &fbc_pool.free);
	mutex_unlock(&fbc_pool.lock);

	n = codec_hevc_capture_buffers(sess, bufs);

	if (!use_mmu) {
		/* Contiguous body, allocated per index and privately owned */
		for (idx = 0; idx < n; idx++) {
			dma_addr_t paddr;
			void *vaddr;

			if (!bufs[idx] || comm->fbc_buffer_vaddr[idx])
				continue;

			vaddr = dma_alloc_coherent(dev, am21_size, &paddr,
						   GFP_KERNEL);
			if (!vaddr) {
				codec_hevc_free_fbc_buffers(sess, comm);
				return -ENOMEM;
			}

			comm->fbc_buffer_vaddr[idx] = vaddr;
			comm->fbc_buffer_paddr[idx] = paddr;
		}

		return 0;
	}

	/*
	 * MMU mode: the body is described page by page in the frame map, so
	 * assemble it from pool chunks - no large contiguous range needed.
	 */
	nr_chunks = DIV_ROUND_UP(amvdec_amfbc_body_size(sess->width,
							sess->height,
							is_10bit, use_mmu),
				 FBC_CHUNK_SIZE);
	if (comm->fbc_nr_chunks && comm->fbc_nr_chunks != nr_chunks) {
		/* Geometry changed under us - drop what we hold and re-take */
		mutex_lock(&fbc_pool.lock);
		fbc_chunks_park_locked(comm, &fbc_pool.free);
		mutex_unlock(&fbc_pool.lock);
	}
	comm->fbc_nr_chunks = nr_chunks;

	for (idx = 0; idx < n; idx++) {
		u32 i;

		if (!bufs[idx])
			continue;

		/*
		 * Per-index idempotency: recovery re-runs and staged buffer
		 * negotiation (players may grow the CAPTURE queue after the
		 * first resolution event) must fill in exactly the missing
		 * bodies - an all-or-nothing guard left grown buffers
		 * bodyless (map entries pointing nowhere = confetti frames).
		 */
		if (comm->fbc_chunks[idx])
			continue;

		comm->fbc_chunks[idx] = kcalloc(nr_chunks,
						sizeof(*comm->fbc_chunks[idx]),
						GFP_KERNEL);
		if (!comm->fbc_chunks[idx]) {
			codec_hevc_free_fbc_buffers(sess, comm);
			return -ENOMEM;
		}

		mutex_lock(&fbc_pool.lock);
		for (i = 0; i < nr_chunks; ++i) {
			comm->fbc_chunks[idx][i] =
				fbc_chunk_get_pressure_locked(dev);
			if (!comm->fbc_chunks[idx][i])
				break;
		}
		mutex_unlock(&fbc_pool.lock);

		if (i < nr_chunks) {
			dev_err(dev, "Failed to assemble FBC body %u (%u/%u chunks)\n",
				idx, i, nr_chunks);
			codec_hevc_free_fbc_buffers(sess, comm);
			return -ENOMEM;
		}
	}

	if (codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit)) {
		ret = codec_hevc_alloc_mmu_headers(sess, comm);
		if (ret) {
			codec_hevc_free_fbc_buffers(sess, comm);
			return ret;
		}
	}

	return 0;
}

int codec_hevc_setup_buffers(struct amvdec_session *sess,
			     struct codec_hevc_common *comm,
			     int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev;
	int ret;

	/* resume() may run more than once per session - allocate only once */
	if (codec_hevc_use_mmu(core->platform->revision,
			       sess->pixfmt_cap, is_10bit) &&
	    !comm->mmu_map_vaddr) {
		comm->mmu_map_vaddr = dma_alloc_coherent(dev, MMU_MAP_SIZE,
							 &comm->mmu_map_paddr,
							 GFP_KERNEL);
		if (!comm->mmu_map_vaddr)
			return -ENOMEM;
	}

	if (codec_hevc_use_mmu(core->platform->revision,
			       sess->pixfmt_cap, is_10bit) ||
	    codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit)) {
		ret = codec_hevc_alloc_fbc_buffers(sess, comm);
		if (ret)
			return ret;
	}

	if (core->platform->revision == VDEC_REVISION_GXBB)
		codec_hevc_setup_buffers_gxbb(sess, comm, is_10bit);
	else
		codec_hevc_setup_buffers_gxl(sess, comm, is_10bit);

	return 0;
}
EXPORT_SYMBOL_GPL(codec_hevc_setup_buffers);

static bool codec_hevc_frame_buffer_backed(struct amvdec_session *sess,
					   struct codec_hevc_common *comm,
					   u32 idx, int is_10bit)
{
	const u32 revision = sess->core->platform->revision;
	const u32 pixfmt = sess->pixfmt_cap;

	if (!codec_hevc_use_fbc(pixfmt, is_10bit))
		return true;	/* plain NV12: the vb2 planes are the frame */

	if (codec_hevc_use_downsample(pixfmt, is_10bit) &&
	    codec_hevc_use_mmu(revision, pixfmt, is_10bit) &&
	    !comm->mmu_header_paddr[idx])
		return false;

	return codec_hevc_fbc_body_addr(comm, idx) != 0;
}

int codec_hevc_ensure_frame_buffer(struct amvdec_session *sess,
				   struct codec_hevc_common *comm,
				   struct vb2_buffer *vb, int is_10bit)
{
	u32 idx = vb->index;

	if (idx >= MAX_REF_PIC_NUM)
		return -EINVAL;

	if (codec_hevc_frame_buffer_backed(sess, comm, idx, is_10bit))
		return 0;

	/*
	 * A buffer that was not allocated when the session was set up -
	 * queued late, or added with CREATE_BUFS.  Setup is per-index
	 * idempotent, so run it again for the ones that are missing.
	 */
	if (codec_hevc_setup_buffers(sess, comm, is_10bit) ||
	    !codec_hevc_frame_buffer_backed(sess, comm, idx, is_10bit)) {
		dev_err_ratelimited(sess->core->dev,
				    "CAPTURE buffer %u has no FBC backing - dropping frame\n",
				    idx);
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(codec_hevc_ensure_frame_buffer);

void codec_hevc_fill_mmu_map(struct amvdec_session *sess,
			     struct codec_hevc_common *comm,
			     struct vb2_buffer *vb,
			     u32 is_10bit)
{
	u32 use_mmu;
	u32 body_size;
	u32 nb_pages;
	u32 *mmu_map = comm->mmu_map_vaddr;
	u32 i;

	use_mmu = codec_hevc_use_mmu(sess->core->platform->revision,
				     sess->pixfmt_cap, is_10bit);

	/*
	 * Only the compressed body is mapped; the header lives in its own
	 * buffer (the vb2 plane in AM21C mode, a private allocation when
	 * downsampling).  This matches the vendor's page count.
	 */
	body_size = amvdec_amfbc_body_size(sess->width, sess->height,
					   is_10bit, use_mmu);
	nb_pages = DIV_ROUND_UP(body_size, PAGE_SIZE);

	if (nb_pages > MMU_MAP_SIZE / sizeof(u32)) {
		/*
		 * The map covers up to 4608 4K pages (18 MiB of FBC body);
		 * 4Kx2K 10-bit needs 4080.  Anything bigger cannot be
		 * described - clamp so the hardware at least stays inside
		 * the buffers we own.
		 */
		dev_err_once(sess->core->dev,
			     "FBC buffer (%u pages) exceeds MMU map capacity\n",
			     nb_pages);
		nb_pages = MMU_MAP_SIZE / sizeof(u32);
	}

	if (comm->fbc_chunks[vb->index]) {
		for (i = 0; i < nb_pages; ++i) {
			u32 ci = i / FBC_CHUNK_PAGES;
			struct fbc_chunk *c;

			/*
			 * nb_pages and fbc_nr_chunks are derived from the
			 * session geometry at two different times.  If they
			 * ever disagree, this walks off the chunk array and
			 * programs whatever it finds as a page frame number -
			 * and the decoder then DMAs into that physical
			 * address.  The damage lands in whatever owns that
			 * memory, arbitrarily far from this driver, so fail
			 * the frame instead.
			 */
			if (ci >= comm->fbc_nr_chunks) {
				dev_err_ratelimited(sess->core->dev,
					"FBC chunk %u >= %u (nb_pages %u, buf %u) - refusing to map\n",
					ci, comm->fbc_nr_chunks, nb_pages,
					vb->index);
				break;
			}

			c = comm->fbc_chunks[vb->index][ci];
			if (!c) {
				dev_err_ratelimited(sess->core->dev,
					"FBC chunk %u of buffer %u is NULL - refusing to map\n",
					ci, vb->index);
				break;
			}

			mmu_map[i] = c->dma[i % FBC_CHUNK_PAGES] >> PAGE_SHIFT;
		}
	} else {
		u32 first_page =
			comm->fbc_buffer_paddr[vb->index] >> PAGE_SHIFT;

		for (i = 0; i < nb_pages; ++i)
			mmu_map[i] = first_page + i;
	}
}
EXPORT_SYMBOL_GPL(codec_hevc_fill_mmu_map);
