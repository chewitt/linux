// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 */

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
 * page map, so the body needs no contiguity: it is assembled from
 * fixed-size chunks.  That removes the 16 MiB-contiguous allocation a
 * 4K 10-bit frame used to need - the single biggest source of CMA
 * pressure - and makes chunks fungible between sessions regardless of
 * resolution or bit depth.
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
	void *vaddr;
	dma_addr_t paddr;
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

static void fbc_chunks_release_locked(struct list_head *h)
{
	struct fbc_chunk *c, *n;

	list_for_each_entry_safe(c, n, h, list) {
		list_del(&c->list);
		dma_free_coherent(fbc_pool.dev, FBC_CHUNK_SIZE,
				  c->vaddr, c->paddr);
		kfree(c);
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

	if (!list_empty(&fbc_pool.free)) {
		c = list_first_entry(&fbc_pool.free, struct fbc_chunk, list);
		list_del(&c->list);
		return c;
	}

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;

	c->vaddr = dma_alloc_coherent(dev, FBC_CHUNK_SIZE, &c->paddr,
				      GFP_KERNEL);
	if (!c->vaddr) {
		kfree(c);
		return NULL;
	}

	fbc_pool.nr_total++;
	return c;
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

	if (list_empty(&fbc_pool.park[0]) && list_empty(&fbc_pool.park[1]))
		return NULL;

	list_splice_tail_init(&fbc_pool.park[1], &fbc_pool.free);
	list_splice_tail_init(&fbc_pool.park[0], &fbc_pool.free);
	dev_warn_once(dev, "CMA pressure: recycled parked FBC chunks\n");

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
	if (comm->fbc_chunks[idx] && comm->fbc_chunks[idx][0])
		return comm->fbc_chunks[idx][0]->paddr;

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
	struct v4l2_m2m_buffer *buf;
	u32 buf_num = v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
	dma_addr_t buf_y_paddr = 0;
	dma_addr_t buf_uv_paddr = 0;
	u32 idx = 0;
	u32 val;
	int i;

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0);

	v4l2_m2m_for_each_dst_buf(sess->m2m_ctx, buf) {
		struct vb2_buffer *vb = &buf->vb.vb2_buf;

		idx = vb->index;

		if (codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit))
			buf_y_paddr = comm->fbc_buffer_paddr[idx];
		else
			buf_y_paddr = vb2_dma_contig_plane_dma_addr(vb, 0);

		if (codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit)) {
			val = buf_y_paddr | (idx << 8) | 1;
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 val);
		} else {
			buf_uv_paddr = vb2_dma_contig_plane_dma_addr(vb, 1);
			val = buf_y_paddr | ((idx * 2) << 8) | 1;
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 val);
			val = buf_uv_paddr | ((idx * 2 + 1) << 8) | 1;
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 val);
		}
	}

	if (codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit))
		val = buf_y_paddr | (idx << 8) | 1;
	else
		val = buf_y_paddr | ((idx * 2) << 8) | 1;

	/* Fill the remaining unused slots with the last buffer's Y addr */
	for (i = buf_num; i < MAX_REF_PIC_NUM; ++i)
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, val);

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

		vaddr = dma_alloc_coherent(dev, MMU_COMPRESS_HEADER_SIZE,
					   &paddr, GFP_KERNEL);
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

	if (sess->sequence_cap == 0) {
		/*
		 * This session never delivered a frame: nothing of it can be
		 * on screen, so its chunks are immediately reusable.  The
		 * still-displayed generations (if any) stay parked.
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

			mmu_map[i] = (c->paddr >> PAGE_SHIFT) +
				     (i % FBC_CHUNK_PAGES);
		}
	} else {
		u32 first_page =
			comm->fbc_buffer_paddr[vb->index] >> PAGE_SHIFT;

		for (i = 0; i < nb_pages; ++i)
			mmu_map[i] = first_page + i;
	}
}
EXPORT_SYMBOL_GPL(codec_hevc_fill_mmu_map);
