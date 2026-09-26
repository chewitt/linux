// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 *
 * HEVC and VP9 capture backing and framebuffer compression.
 */

#include <linux/dma-mapping.h>
#include <linux/mm.h>

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc_common.h"
#include "amvdec_helpers.h"
#include "hevc_regs.h"

#define MMU_COMPRESS_HEADER_SIZE 0x48000
#define MMU_MAP_SIZE 0x4800

/*
 * Pool noncontiguous MMU FBC body pages in fixed-size chunks. Allocate with
 * dma_alloc_pages() to respect the device DMA mask without requiring CMA.
 * Only the decoder and display access body contents; invalidate CPU cache
 * lines after allocation and before freeing pages.
 * Released AM21C bodies remain parked for display scanout. park[0] holds
 * the newest stopped session and park[1] holds the older generation.
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
	/* Protect pool lists and generation ownership. */
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
			/* Invalidate speculative CPU cache lines before returning the page. */
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
 * Release free chunks and the older parked generation. Keep park[0] for
 * possible display scanout.
 */
void meson_amvdec_codec_hevc_fbc_pool_reclaim(void)
{
	mutex_lock(&fbc_pool.lock);
	if (fbc_pool.dev) {
		fbc_chunks_release_locked(&fbc_pool.free);
		fbc_chunks_release_locked(&fbc_pool.park[1]);
	}
	mutex_unlock(&fbc_pool.lock);
}

void meson_amvdec_codec_hevc_fbc_pool_drain(void)
{
	mutex_lock(&fbc_pool.lock);
	if (fbc_pool.dev) {
		fbc_chunks_release_locked(&fbc_pool.free);
		fbc_chunks_release_locked(&fbc_pool.park[0]);
		fbc_chunks_release_locked(&fbc_pool.park[1]);
	}
	mutex_unlock(&fbc_pool.lock);
}

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
		 * Use __GFP_NORETRY and __GFP_NOWARN so allocation failure returns ENOMEM
		 * without invoking the OOM killer or issuing allocator warnings.
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

dma_addr_t meson_amvdec_codec_hevc_fbc_body_addr(struct codec_hevc_common *comm, u32 idx)
{
	/* Record the first body page as the nonzero backing address in MMU mode. */
	if (comm->fbc_chunks[idx] && comm->fbc_chunks[idx][0])
		return comm->fbc_chunks[idx][0]->dma[0];

	return comm->fbc_buffer_paddr[idx];
}

const u16 meson_amvdec_hevc_parser_cmd[] = {
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
void meson_amvdec_codec_hevc_setup_decode_head(struct amvdec_session *sess, int is_10bit)
{
	struct amvdec_core *core = sess->core;
	u32 use_mmu = codec_hevc_use_mmu(core->platform->revision,
					 sess->pixfmt_cap, is_10bit);
	u32 body_size = meson_amvdec_amfbc_body_size(sess->width, sess->height,
					       is_10bit, use_mmu);
	u32 head_size = meson_amvdec_amfbc_head_size(sess->width, sess->height);

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit)) {
		/* Enable 2-plane reference read mode */
		meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(31));
		return;
	}

	/* enable mem saving mode for 8-bit */
	if (!is_10bit)
		meson_amvdec_write_dos_bits(core, HEVC_SAO_CTRL5, BIT(9));
	else
		meson_amvdec_clear_dos_bits(core, HEVC_SAO_CTRL5, BIT(9));

	if (codec_hevc_use_mmu(core->platform->revision,
			       sess->pixfmt_cap, is_10bit))
		meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(4));
	else if (!is_10bit)
		meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(3));
	else
		meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, 0);

	/* MMU mode uses the frame page map and requires a zero body stride. */
	if (use_mmu)
		meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL2, 0);
	else if (core->platform->revision < AMVDEC_REVISION_SM1)
		meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL2, body_size / 32);
	meson_amvdec_write_dos(core, HEVC_CM_BODY_LENGTH, body_size);
	meson_amvdec_write_dos(core, HEVC_CM_HEADER_OFFSET, body_size);
	meson_amvdec_write_dos(core, HEVC_CM_HEADER_LENGTH, head_size);
}

static int codec_hevc_setup_buffers_gxbb(struct amvdec_session *sess,
					 struct codec_hevc_common *comm,
					 int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct vb2_buffer *bufs[MAX_REF_PIC_NUM];
	const bool fbc = codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit);
	dma_addr_t fill_y = 0, fill_uv = 0;
	unsigned int idx, n;
	u32 val;
	int i;

	/*
	 * GXBB commands encode the slot number. Program every index, using a valid
	 * surface for unused slots.
	 */
	n = codec_hevc_capture_buffers(sess, bufs);
	if (!n)
		return 0;
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
		return 0;

	meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0);
	memset(comm->anc_y_paddr, 0, sizeof(comm->anc_y_paddr));

	for (idx = 0; idx < MAX_REF_PIC_NUM; idx++) {
		struct vb2_buffer *vb = idx < n ? bufs[idx] : NULL;
		dma_addr_t buf_y_paddr = 0, buf_uv_paddr = 0;

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
		/* Only address bits 31:16 survive the command encoding. */
		if ((buf_y_paddr | buf_uv_paddr) & (SZ_64K - 1)) {
			dev_err_ratelimited(core->dev,
					    "CAPTURE buffer %u planes %pad/%pad not 64 KiB aligned\n",
				idx, &buf_y_paddr, &buf_uv_paddr);
			memset(comm->anc_y_paddr, 0, sizeof(comm->anc_y_paddr));
			return -EINVAL;
		}
		comm->anc_y_paddr[idx] = buf_y_paddr;

		if (fbc) {
			val = buf_y_paddr | (idx << 8) | 1;
			meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 val);
		} else {
			val = buf_y_paddr | ((idx * 2) << 8) | 1;
			meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 val);
			val = buf_uv_paddr | ((idx * 2 + 1) << 8) | 1;
			meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
					 val);
		}
	}

	meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; ++i)
		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
	return 0;
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
	 * Populate ANC2AXI densely by capture index, with two slots per NV12M
	 * buffer. Use a valid surface for unused entries.
	 */
	n = codec_hevc_capture_buffers(sess, bufs);
	if (!n)
		return;
	dev_dbg(core->dev, "setup %ux%u nbuf=%u 10bit=%d fbc=%d\n",
		sess->width, sess->height, n, is_10bit, fbc);

	meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR,
			 BIT(2) | BIT(1));
	memset(comm->anc_y_paddr, 0, sizeof(comm->anc_y_paddr));

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

			comm->anc_y_paddr[idx] = buf_y_paddr;
			if (!fill_y && buf_y_paddr) {
				fill_y = buf_y_paddr;
				fill_uv = buf_uv_paddr;
			}
		}

		if (!vb || !buf_y_paddr) {
			buf_y_paddr = fill_y;
			buf_uv_paddr = fill_uv;
		}

		meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
				 buf_y_paddr >> 5);
		if (!fbc)
			meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
					 buf_uv_paddr >> 5);
	}

	meson_amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; ++i)
		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
}

void meson_amvdec_codec_hevc_free_mmu_headers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm)
{
	struct device *dev = sess->core->dev;
	int i;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (comm->mmu_header_vaddr[i]) {
			meson_amvdec_dma_free_guarded(dev, comm->mmu_header_paddr[i],
						&comm->mmu_header_guard[i]);
			comm->mmu_header_vaddr[i] = NULL;
			comm->mmu_header_paddr[i] = 0;
		}
	}
}

int meson_amvdec_codec_hevc_verify_fbc_guards(struct amvdec_session *sess,
				 struct codec_hevc_common *comm,
				 const char *prefix)
{
	struct device *dev = sess->core->dev;
	char name[96];
	int ret = 0;
	int i;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (comm->fbc_buffer_vaddr[i]) {
			scnprintf(name, sizeof(name), "%s.fbc_body[%d]", prefix, i);
			ret |= meson_amvdec_dma_verify_guard(dev, name,
							 &comm->fbc_buffer_guard[i]);
		}
		if (comm->mmu_header_vaddr[i]) {
			scnprintf(name, sizeof(name), "%s.fbc_header[%d]", prefix, i);
			ret |= meson_amvdec_dma_verify_guard(dev, name,
							 &comm->mmu_header_guard[i]);
		}
	}
	if (comm->mmu_map_vaddr) {
		scnprintf(name, sizeof(name), "%s.mmu_map", prefix);
		ret |= meson_amvdec_dma_verify_guard(dev, name, &comm->mmu_map_guard);
	}

	return ret ? -EIO : 0;
}

/*
 * Collect capture buffers by index and return one past the highest index.
 * MMAP buffers have DMA backing after allocation; imported DMABUFs must be
 * queued and mapped. Leave absent indices NULL.
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
			unsigned int p;

			if (!vb)
				continue;
			/*
			 * CREATE_BUFS can publish a buffer before its planes exist; skip
			 * buffers
			 * without DMA backing.
			 */
			for (p = 0; p < vb->num_planes; p++)
				if (!READ_ONCE(vb->planes[p].mem_priv))
					break;
			if (p < vb->num_planes)
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
	if (meson_amvdec_request_jobs(sess)) {
		/* The job and its references are no longer on the ready queue. */
		for (i = 0; i <= ARRAY_SIZE(sess->request_job.refs); i++) {
			struct vb2_buffer *vb = i == ARRAY_SIZE(sess->request_job.refs) ?
				(sess->request_job.dst ? &sess->request_job.dst->vb2_buf : NULL) :
				sess->request_job.refs[i];
			if (!vb || vb->index >= MAX_REF_PIC_NUM)
				continue;
			bufs[vb->index] = vb;
			n = max(n, vb->index + 1);
		}
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

		/* Allocate each capture index only once. */
		if (!bufs[idx] || comm->mmu_header_vaddr[idx])
			continue;

		/* Free partial allocations and return -ENOMEM on failure. */
		vaddr = meson_amvdec_dma_alloc_guarded(dev, MMU_COMPRESS_HEADER_SIZE,
						 &paddr, GFP_KERNEL | __GFP_NOWARN,
							&comm->mmu_header_guard[idx]);
		if (!vaddr) {
			meson_amvdec_codec_hevc_free_mmu_headers(sess, comm);
			return -ENOMEM;
		}

		comm->mmu_header_vaddr[idx] = vaddr;
		comm->mmu_header_paddr[idx] = paddr;
	}

	return 0;
}

void meson_amvdec_codec_hevc_free_fbc_buffers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm)
{
	struct device *dev = sess->core->dev;
	int i;

	mutex_lock(&fbc_pool.lock);
	fbc_pool.dev = dev;

	if (sess->sequence_cap == 0 ||
	    codec_hevc_use_downsample(sess->pixfmt_cap, sess->bitdepth == 10)) {
		/*
		 * Reuse private downsampling bodies and bodies from sessions that produce
		 * no frame. Only AM21C bodies can remain on the display.
		 */
		fbc_chunks_park_locked(comm, &fbc_pool.free);
	} else {
		/*
		 * Age the parked generation and retain this session's AM21C bodies for
		 * scanout.
		 */
		list_splice_tail_init(&fbc_pool.park[1], &fbc_pool.free);
		list_splice_tail_init(&fbc_pool.park[0], &fbc_pool.park[1]);
		fbc_chunks_park_locked(comm, &fbc_pool.park[0]);
	}

	/* Return unused chunks to the page allocator. */
	fbc_chunks_release_locked(&fbc_pool.free);
	mutex_unlock(&fbc_pool.lock);

	/* Non-MMU FBC bodies are contiguous and privately owned */
	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (!comm->fbc_buffer_vaddr[i])
			continue;
		meson_amvdec_dma_free_guarded(dev, comm->fbc_buffer_paddr[i],
					&comm->fbc_buffer_guard[i]);
		comm->fbc_buffer_vaddr[i] = NULL;
		comm->fbc_buffer_paddr[i] = 0;
	}

	if (comm->mmu_map_vaddr) {
		meson_amvdec_dma_free_guarded(dev, comm->mmu_map_paddr,
					&comm->mmu_map_guard);
		comm->mmu_map_vaddr = NULL;
		comm->mmu_map_paddr = 0;
	}

	meson_amvdec_codec_hevc_free_mmu_headers(sess, comm);
}

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

	am21_size = meson_amvdec_amfbc_size(sess->width, sess->height, is_10bit,
				      use_mmu);
	comm->fbc_buffer_size = am21_size;

	/* Recycle the older parked generation at session setup. */
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

			vaddr = meson_amvdec_dma_alloc_guarded(dev, am21_size, &paddr,
							 GFP_KERNEL | __GFP_NOWARN,
							&comm->fbc_buffer_guard[idx]);
			if (!vaddr) {
				dev_err_ratelimited(dev,
					"Failed to allocate %u-byte FBC reference %u of %u\n",
					am21_size, idx, n);
				meson_amvdec_codec_hevc_free_fbc_buffers(sess, comm);
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
	nr_chunks = DIV_ROUND_UP(meson_amvdec_amfbc_body_size(sess->width,
							sess->height,
							is_10bit, use_mmu),
				 FBC_CHUNK_SIZE);
	if (comm->fbc_nr_chunks && comm->fbc_nr_chunks != nr_chunks) {
		/* Reallocate bodies when the required geometry changes. */
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
		 * Allocate backing for each new capture index without replacing existing
		 * bodies.
		 */
		if (comm->fbc_chunks[idx])
			continue;

		comm->fbc_chunks[idx] = kcalloc(nr_chunks,
						sizeof(*comm->fbc_chunks[idx]),
						GFP_KERNEL);
		if (!comm->fbc_chunks[idx]) {
			meson_amvdec_codec_hevc_free_fbc_buffers(sess, comm);
			return -ENOMEM;
		}

		/*
		 * Fail allocation rather than reuse park[0], whose bodies may still be
		 * scanned out.
		 */
		mutex_lock(&fbc_pool.lock);
		for (i = 0; i < nr_chunks; ++i) {
			comm->fbc_chunks[idx][i] = fbc_chunk_get_locked(dev);
			if (!comm->fbc_chunks[idx][i])
				break;
		}
		mutex_unlock(&fbc_pool.lock);

		if (i < nr_chunks) {
			dev_err(dev, "Failed to assemble FBC body %u (%u/%u chunks)\n",
				idx, i, nr_chunks);
			meson_amvdec_codec_hevc_free_fbc_buffers(sess, comm);
			return -ENOMEM;
		}
	}

	if (codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit)) {
		ret = codec_hevc_alloc_mmu_headers(sess, comm);
		if (ret) {
			meson_amvdec_codec_hevc_free_fbc_buffers(sess, comm);
			return ret;
		}
	}

	return 0;
}

int meson_amvdec_codec_hevc_setup_buffers(struct amvdec_session *sess,
			     struct codec_hevc_common *comm,
			     int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev;
	int ret;

	/* Allocate the frame MMU map once per session. */
	if (codec_hevc_use_mmu(core->platform->revision,
			       sess->pixfmt_cap, is_10bit) &&
	    !comm->mmu_map_vaddr) {
		comm->mmu_map_vaddr = meson_amvdec_dma_alloc_guarded(dev, MMU_MAP_SIZE,
							       &comm->mmu_map_paddr,
									GFP_KERNEL,
									&comm->mmu_map_guard);
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

	if (core->platform->revision == AMVDEC_REVISION_GXBB)
		return codec_hevc_setup_buffers_gxbb(sess, comm, is_10bit);

	codec_hevc_setup_buffers_gxl(sess, comm, is_10bit);
	return 0;
}

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

	return meson_amvdec_codec_hevc_fbc_body_addr(comm, idx) != 0;
}

/* The luma address the ANC2AXI slot of @vb must hold for this session. */
static dma_addr_t codec_hevc_anc_y_addr(struct amvdec_session *sess,
					struct codec_hevc_common *comm,
					struct vb2_buffer *vb, int is_10bit)
{
	const u32 revision = sess->core->platform->revision;
	u32 idx = vb->index;

	if (revision == AMVDEC_REVISION_GXBB)
		return codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit) ?
			comm->fbc_buffer_paddr[idx] :
			vb2_dma_contig_plane_dma_addr(vb, 0);

	if (!codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit))
		return vb2_dma_contig_plane_dma_addr(vb, 0);

	return codec_hevc_use_mmu(revision, sess->pixfmt_cap, is_10bit) ?
		comm->mmu_header_paddr[idx] : comm->fbc_buffer_paddr[idx];
}

int meson_amvdec_codec_hevc_ensure_frame_buffer(struct amvdec_session *sess,
				   struct codec_hevc_common *comm,
				   struct vb2_buffer *vb, int is_10bit)
{
	u32 idx = vb->index;

	if (idx >= MAX_REF_PIC_NUM)
		return -EINVAL;

	if (codec_hevc_frame_buffer_backed(sess, comm, idx, is_10bit) &&
	    comm->anc_y_paddr[idx] &&
	    comm->anc_y_paddr[idx] == codec_hevc_anc_y_addr(sess, comm, vb, is_10bit))
		return 0;

	/* Allocate missing backing for capture buffers added after session setup. */
	if (meson_amvdec_codec_hevc_setup_buffers(sess, comm, is_10bit) ||
	    !codec_hevc_frame_buffer_backed(sess, comm, idx, is_10bit) ||
	    !comm->anc_y_paddr[idx] ||
	    comm->anc_y_paddr[idx] != codec_hevc_anc_y_addr(sess, comm, vb, is_10bit)) {
		dev_err_ratelimited(sess->core->dev,
				    "CAPTURE buffer %u has no decoder mapping - dropping frame\n",
				    idx);
		return -ENOMEM;
	}

	return 0;
}

int meson_amvdec_codec_hevc_fill_mmu_map(struct amvdec_session *sess,
			    struct codec_hevc_common *comm,
			    struct vb2_buffer *vb,
			    u32 is_10bit)
{
	u32 use_mmu;
	u32 body_size;
	u32 nb_pages;
	u32 *mmu_map = comm->mmu_map_vaddr;
	u32 i;

	if (vb->index >= MAX_REF_PIC_NUM || !mmu_map)
		return -EINVAL;

	use_mmu = codec_hevc_use_mmu(sess->core->platform->revision,
				     sess->pixfmt_cap, is_10bit);
	if (!use_mmu)
		return 0;

	/*
	 * Only the compressed body is mapped; the header lives in its own
	 * buffer (the vb2 plane in AM21C mode, a private allocation when
	 * downsampling).
	 */
	body_size = meson_amvdec_amfbc_body_size(sess->width, sess->height,
					   is_10bit, use_mmu);
	nb_pages = DIV_ROUND_UP(body_size, PAGE_SIZE);

	if (nb_pages > MMU_MAP_SIZE / sizeof(u32)) {
		/* Refuse bodies exceeding the map capacity of 4608 4 KiB pages. */
		dev_err_ratelimited(sess->core->dev,
				    "FBC buffer (%u pages) exceeds MMU map capacity\n",
				    nb_pages);
		return -EINVAL;
	}

	if (comm->fbc_chunks[vb->index]) {
		for (i = 0; i < nb_pages; ++i) {
			u32 ci = i / FBC_CHUNK_PAGES;
			struct fbc_chunk *c;

			/*
			 * Check the chunk count before writing page addresses into the MMU
			 * map.
			 */
			if (ci >= comm->fbc_nr_chunks) {
				dev_err_ratelimited(sess->core->dev,
						    "FBC chunk %u >= %u (nb_pages %u, buf %u) - refusing to map\n",
					ci, comm->fbc_nr_chunks, nb_pages,
					vb->index);
				return -EINVAL;
			}

			c = comm->fbc_chunks[vb->index][ci];
			if (!c) {
				dev_err_ratelimited(sess->core->dev,
						    "FBC chunk %u of buffer %u is NULL - refusing to map\n",
					ci, vb->index);
				return -EINVAL;
			}

			mmu_map[i] = c->dma[i % FBC_CHUNK_PAGES] >> PAGE_SHIFT;
		}
	} else {
		u32 first_page =
			comm->fbc_buffer_paddr[vb->index] >> PAGE_SHIFT;

		if (!first_page)
			return -EINVAL;

		for (i = 0; i < nb_pages; ++i)
			mmu_map[i] = first_page + i;
	}

	return 0;
}
