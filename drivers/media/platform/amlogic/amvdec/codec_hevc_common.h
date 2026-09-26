/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * HEVC and VP9 capture backing and framebuffer compression.
 */

#ifndef __MESON_AMVDEC_HEVC_COMMON_H_
#define __MESON_AMVDEC_HEVC_COMMON_H_

#include "amvdec.h"

#define PARSER_CMD_SKIP_CFG_0 0x0000090b
#define PARSER_CMD_SKIP_CFG_1 0x1b14140f
#define PARSER_CMD_SKIP_CFG_2 0x001b1910

#define VDEC_HEVC_PARSER_CMD_LEN 37
extern const u16 meson_amvdec_hevc_parser_cmd[VDEC_HEVC_PARSER_CMD_LEN];

#define MAX_REF_PIC_NUM	24

/*
 * Restart the HEVC core once the firmware's stream position counter passes
 * this many MiB, before it can reach 0x80000000 where the firmware stops
 * consuming (see meson_amvdec_codec_hevc_arm_restream()).
 */
#define HEVC_STREAM_RESTART_MIB	1024

struct fbc_chunk;

struct codec_hevc_common {
	/*
	 * Non-MMU FBC (pre-G12A): the compressed body must be physically
	 * contiguous because the hardware strides through it directly.
	 */
	void      *fbc_buffer_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t fbc_buffer_paddr[MAX_REF_PIC_NUM];
	struct amvdec_dma_guard fbc_buffer_guard[MAX_REF_PIC_NUM];

	/*
	 * MMU FBC (G12A+): the body is reached only through the per-frame
	 * page map, so it needs no contiguity at all - it is assembled
	 * from fixed-size chunks handed out by a shared pool.
	 */
	struct fbc_chunk **fbc_chunks[MAX_REF_PIC_NUM];
	u32        fbc_nr_chunks;
	/* Allocation sizes for freeing buffers independently of current geometry. */
	u32        fbc_buffer_size;

	void      *mmu_header_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t mmu_header_paddr[MAX_REF_PIC_NUM];
	struct amvdec_dma_guard mmu_header_guard[MAX_REF_PIC_NUM];

	/*
	 * Luma address in each ANC2AXI slot; zero marks a filler slot. Refresh the
	 * table when capture buffers gain backing.
	 */
	dma_addr_t anc_y_paddr[MAX_REF_PIC_NUM];

	void      *mmu_map_vaddr;
	dma_addr_t mmu_map_paddr;
	struct amvdec_dma_guard mmu_map_guard;
};

/* Whether framebuffer compression is required. */
static inline int codec_hevc_use_fbc(u32 pixfmt, int is_10bit)
{
	return is_10bit || pixfmt == V4L2_PIX_FMT_AM21C;
}

/* Whether 10-bit decode produces 8-bit NV12 output. */
static inline int codec_hevc_use_downsample(u32 pixfmt, int is_10bit)
{
	return is_10bit && pixfmt != V4L2_PIX_FMT_AM21C;
}

/* Whether decoding uses the frame MMU. */
static inline int codec_hevc_use_mmu(u32 revision, u32 pixfmt, int is_10bit)
{
	return revision >= AMVDEC_REVISION_G12A &&
	       codec_hevc_use_fbc(pixfmt, is_10bit);
}

/* Configure decode head read mode */
void meson_amvdec_codec_hevc_setup_decode_head(struct amvdec_session *sess, int is_10bit);

void meson_amvdec_codec_hevc_fbc_pool_drain(void);
void meson_amvdec_codec_hevc_fbc_pool_reclaim(void);

/* Base address of a frame's compressed body, whichever backing is in use */
dma_addr_t meson_amvdec_codec_hevc_fbc_body_addr(struct codec_hevc_common *comm, u32 idx);
void meson_amvdec_codec_hevc_free_fbc_buffers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm);

void meson_amvdec_codec_hevc_free_mmu_headers(struct amvdec_session *sess,
				 struct codec_hevc_common *comm);

int meson_amvdec_codec_hevc_verify_fbc_guards(struct amvdec_session *sess,
				 struct codec_hevc_common *comm,
				 const char *prefix);

int meson_amvdec_codec_hevc_setup_buffers(struct amvdec_session *sess,
			     struct codec_hevc_common *comm,
			     int is_10bit);

/*
 * Ensure capture backing exists before programming the decode head;
 * return an error if allocation fails.
 */
int meson_amvdec_codec_hevc_ensure_frame_buffer(struct amvdec_session *sess,
				   struct codec_hevc_common *comm,
				   struct vb2_buffer *vb, int is_10bit);

int meson_amvdec_codec_hevc_fill_mmu_map(struct amvdec_session *sess,
			    struct codec_hevc_common *comm,
			    struct vb2_buffer *vb,
			    u32 is_10bit);

#endif
