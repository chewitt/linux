/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_HEVC_COMMON_H_
#define __MESON_VDEC_HEVC_COMMON_H_

#include "vdec.h"

#define PARSER_CMD_SKIP_CFG_0 0x0000090b
#define PARSER_CMD_SKIP_CFG_1 0x1b14140f
#define PARSER_CMD_SKIP_CFG_2 0x001b1910

#define VDEC_HEVC_PARSER_CMD_LEN 37
extern const u16 vdec_hevc_parser_cmd[VDEC_HEVC_PARSER_CMD_LEN];

#define MAX_REF_PIC_NUM	24

struct fbc_chunk;

struct codec_hevc_common {
	/*
	 * Non-MMU FBC (pre-G12A): the compressed body must be physically
	 * contiguous because the hardware strides through it directly.
	 */
	void      *fbc_buffer_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t fbc_buffer_paddr[MAX_REF_PIC_NUM];

	/*
	 * MMU FBC (G12A+): the body is reached only through the per-frame
	 * page map, so it needs no contiguity at all - it is assembled
	 * from fixed-size chunks handed out by a shared pool.
	 */
	struct fbc_chunk **fbc_chunks[MAX_REF_PIC_NUM];
	u32        fbc_nr_chunks;
	/* size the fbc buffers were allocated with - used to free them
	 * correctly if the session dimensions changed in between
	 */
	u32        fbc_buffer_size;

	void      *mmu_header_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t mmu_header_paddr[MAX_REF_PIC_NUM];

	void      *mmu_map_vaddr;
	dma_addr_t mmu_map_paddr;
};

/* Returns 1 if we must use framebuffer compression */
static inline int codec_hevc_use_fbc(u32 pixfmt, int is_10bit)
{
	return is_10bit || pixfmt == V4L2_PIX_FMT_MESON_AM21C;
}

/* Returns 1 if we are decoding 10-bit but outputting 8-bit NV12 */
static inline int codec_hevc_use_downsample(u32 pixfmt, int is_10bit)
{
	return is_10bit && pixfmt != V4L2_PIX_FMT_MESON_AM21C;
}

/* Returns 1 if we are decoding using the IOMMU */
static inline int codec_hevc_use_mmu(u32 revision, u32 pixfmt, int is_10bit)
{
	return revision >= VDEC_REVISION_G12A &&
	       codec_hevc_use_fbc(pixfmt, is_10bit);
}

/* Configure decode head read mode */
void codec_hevc_setup_decode_head(struct amvdec_session *sess, int is_10bit);

void codec_hevc_fbc_pool_drain(void);

/* Base address of a frame's compressed body, whichever backing is in use */
dma_addr_t codec_hevc_fbc_body_addr(struct codec_hevc_common *comm, u32 idx);
void codec_hevc_free_fbc_buffers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm);

void codec_hevc_free_mmu_headers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm);

int codec_hevc_setup_buffers(struct amvdec_session *sess,
			     struct codec_hevc_common *comm,
			     int is_10bit);

void codec_hevc_fill_mmu_map(struct amvdec_session *sess,
			     struct codec_hevc_common *comm,
			     struct vb2_buffer *vb,
			     u32 is_10bit);

#endif
