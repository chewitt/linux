// SPDX-License-Identifier: GPL-2.0+
/*
 * AV1 request decoding on the S4 HEVC core.
 *
 * The firmware parses the bitstream, so each request is fed as a sequence
 * header and a frame OBU rebuilt from the controls. At the frame header the
 * host programs the reference, motion field, loop filter and buffer state
 * that the vendor reference decoder derives from its own history; here it
 * comes from the controls and from what is kept for each CAPTURE buffer.
 */

#include <linux/slab.h>
#include <linux/unaligned.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "dos_regs.h"
#include "hevc_regs.h"
#include "codec_av1.h"
#include "codec_av1_synth.h"
#include "amvdec_helpers.h"
#include "amvdec_hevc.h"
#include "codec_hevc_common.h"

/* HEVC core registers used by AV1 */
#define HEVC_ASSIST_PIC_SIZE_FB_READ	(0x300d << 2)
#define HEVC_ASSIST_MBOX0_CLR_REG	(0x3071 << 2)
#define HEVC_ASSIST_MBOX0_MASK		(0x3072 << 2)
#define HEVC_PARSER_MEM_WR_ADDR		(0x312d << 2)
#define HEVC_PARSER_MEM_RW_DATA		(0x312e << 2)
#define AV1_SEG_W_ADDR			(0x3165 << 2)
#define AV1_SEG_R_ADDR			(0x3166 << 2)
#define PARSER_REF_SCALE_ENBL		(0x316b << 2)
#define AV1_SKIP_MODE_INFO		(0x316c << 2)
#define AV1_GMC_PARAM_BUFF_ADDR		(0x316d << 2)
#define AV1_UPSCALE_X0_QN		(0x316e << 2)
#define AV1_UPSCALE_STEP_QN		(0x316f << 2)
#define AV1_REF_SEG_INFO		(0x3171 << 2)
#define HEVC_MPRED_L0_REF01_POC		(0x3221 << 2)
#define HEVC_MPRED_L0_REF02_POC		(0x3222 << 2)
#define HEVC_MPRED_L0_REF03_POC		(0x3223 << 2)
#define HEVC_MPRED_L0_REF06_POC		(0x3226 << 2)
#define HEVC_MPRED_MV_RPTR_1		(0x3263 << 2)
#define HEVC_MPRED_MV_RPTR_2		(0x3264 << 2)
#define AV1D_MPP_REF_SCALE_ENBL		(0x3441 << 2)
#define HEVCD_MPP_DECOMP_AXIURG_CTL	(0x34c7 << 2)
#define HEVC_DBLK_CFG3			(0x3503 << 2)
#define HEVC_DBLK_CDEF0			(0x3515 << 2)
#define HEVC_DBLK_CDEF1			(0x3516 << 2)
#define HEVC_DBLK_UPS1			(0x351c << 2)
#define HEVC_DBLK_UPS2			(0x351d << 2)
#define HEVC_DBLK_UPS3			(0x351e << 2)
#define HEVC_DBLK_UPS4			(0x351f << 2)
#define HEVC_DBLK_UPS5			(0x3520 << 2)
#define HEVC_DBLK_DBLK0			(0x3523 << 2)
#define HEVC_DBLK_DBLK1			(0x3524 << 2)
#define HEVC_DBLK_DBLK2			(0x3525 << 2)
#define HEVC_SAO_MMU_DMA_CTRL		(0x363e << 2)
#define HEVC_CM_HEADER_START_ADDR2	(0x364a << 2)
#define HEVC_SAO_MMU_DMA_CTRL2		(0x364c << 2)
#define HEVC_SAO_MMU_VH0_ADDR2		(0x364d << 2)
#define HEVC_SAO_MMU_VH1_ADDR2		(0x364e << 2)
#define HEVC_DW_VH0_ADDDR		(0x365e << 2)
#define HEVC_DW_VH1_ADDDR		(0x365f << 2)
#define HEVC_SAO_Y_START_ADDR3		(0x3698 << 2)
#define HEVC_SAO_C_START_ADDR3		(0x369a << 2)
#define HEVC_FGS_IDX			(0x3660 << 2)
#define HEVC_FGS_DATA			(0x3661 << 2)
#define HEVC_FGS_CTRL			(0x3662 << 2)
#define HEVC_FGS_TABLE_START		(0x3666 << 2)
#define HEVC_FGS_TABLE_LENGTH		(0x3667 << 2)

/* Firmware protocol registers */
#define AV1_DEC_STATUS			HEVC_ASSIST_SCRATCH_0
#define AV1_RPM_BUFFER			HEVC_ASSIST_SCRATCH_1
#define AV1_PIC_END_LCU_COUNT		HEVC_ASSIST_SCRATCH_2
/* the swap firmware moves it to SCRATCH_3 and swaps through SCRATCH_8 */
#define AV1_CDF_BUFFER_W		HEVC_ASSIST_SCRATCH_8
#define AV1_DEBUG_DUMP_ADR		HEVC_ASSIST_SCRATCH_4
#define AV1_DAALA_TOP_BUFFER		HEVC_ASSIST_SCRATCH_5
#define AV1_AUX_DATA_SIZE		HEVC_ASSIST_SCRATCH_7
#define AV1_CDF_BUFFER_R		HEVC_ASSIST_SCRATCH_9
#define AV1_FG_STATUS			HEVC_ASSIST_SCRATCH_B
#define AV1_DEBUG_LOG_ADR		HEVC_ASSIST_SCRATCH_C
#define AV1_WAIT_FLAG			HEVC_ASSIST_SCRATCH_E
#define AV1_COMPATIBILITY		HEVC_ASSIST_SCRATCH_G
#define AV1_LMEM_DUMP_ADR		HEVC_ASSIST_SCRATCH_I
#define AV1_NAL_SEARCH_CTL		HEVC_ASSIST_SCRATCH_I
#define AV1_DECODE_MODE			HEVC_ASSIST_SCRATCH_J
#define AV1_DECODE_STOP_POS		HEVC_ASSIST_SCRATCH_K
#define AV1_AUX_ADR			HEVC_ASSIST_SCRATCH_L
#define AV1_DECODE_COUNT		HEVC_ASSIST_SCRATCH_M
#define AV1_DECODE_SIZE			HEVC_ASSIST_SCRATCH_N

/* Host to firmware */
#define AV1_DECODE_SLICE		5
#define AV1_SEARCH_HEAD			6
#define AV1_FGS_PARAM_CONT		8
#define AV1_ACTION_DONE			0xff
/* Firmware to host */
#define AV1_DEC_PIC_END			0xe0
#define AV1_FGS_PARAM			0xe1
#define AV1_SEQ_HEAD_DONE		0xf2
#define AV1_FRAME_HEAD_DONE		0xf3
#define AV1_FRAME_DONE			0xf4
#define AV1_DECODE_BUFEMPTY		0x20
#define AV1_SEARCH_BUFEMPTY		0x22

/* Single-instance stream mode; the firmware signals on mailbox 0 */
#define AV1_DECODE_MODE_SINGLE		0x80000000

/* Parser memory: next reference slot table, and segment info per buffer */
#define AV1_PARSER_REF_INFO		0x11a0
#define AV1_PARSER_SEG_INFO		0x11b0

/* RPM parameter block written by the firmware at each header */
#define AV1_RPM_WORDS			0x65
#define RPM_FRAME_TYPE			14
#define RPM_ORDER_HINT			21
#define RPM_DEC_WIDTH			25
#define RPM_WIDTH_SCALED		26
#define RPM_HEIGHT			27
#define RPM_BIT_DEPTH			56
#define RPM_SEQ_FLAGS			57
#define RPM_SEG_ENABLED			76

#define AV1_MAX_BUFS			16
#define AV1_FGS_REGS			39
#define AV1_FGS_TABLE_SIZE		(512 * 128 / 8)
#define AV1_REF_NO_SCALE		BIT(14)

/* Workspace, sized for 4096x2304 */
#define AV1_IPP_SIZE		0x4000
#define AV1_SEG_MAP_SIZE	0x36000		/* per buffer */
#define AV1_DAALA_TOP_SIZE	0x2000
#define AV1_CDF_SIZE		0x8000		/* per buffer */
#define AV1_GMC_SIZE		0x800
#define AV1_DBLK_PARA_SIZE	0x1a00
#define AV1_DBLK_DATA_SIZE	0x60800
#define AV1_CDEF_SIZE		0x28200
#define AV1_UPS_SIZE		0xc3000
#define AV1_MMU_VBH_SIZE	0x5000
#define AV1_MPRED_ABV_SIZE	0x5400
#define AV1_RPM_SIZE		0x100
#define AV1_LMEM_SIZE		0x800
#define AV1_AUX_SIZE		SZ_16K
/* target of the second write paths, which stay disabled */
#define AV1_SINK_SIZE		SZ_64K

/* enum constants, so that each offset expands only once */
enum {
	AV1_IPP_OFF = 0,
	AV1_SEG_MAP_OFF = ALIGN(AV1_IPP_OFF + AV1_IPP_SIZE, SZ_4K),
	AV1_DAALA_TOP_OFF = ALIGN(AV1_SEG_MAP_OFF + AV1_SEG_MAP_SIZE * AV1_MAX_BUFS, SZ_4K),
	AV1_CDF_OFF = ALIGN(AV1_DAALA_TOP_OFF + AV1_DAALA_TOP_SIZE, SZ_4K),
	AV1_GMC_OFF = ALIGN(AV1_CDF_OFF + AV1_CDF_SIZE * AV1_MAX_BUFS, SZ_4K),
	AV1_DBLK_PARA_OFF = ALIGN(AV1_GMC_OFF + AV1_GMC_SIZE, SZ_4K),
	AV1_DBLK_DATA_OFF = ALIGN(AV1_DBLK_PARA_OFF + AV1_DBLK_PARA_SIZE, SZ_4K),
	AV1_CDEF_OFF = ALIGN(AV1_DBLK_DATA_OFF + AV1_DBLK_DATA_SIZE, SZ_4K),
	AV1_UPS_OFF = ALIGN(AV1_CDEF_OFF + AV1_CDEF_SIZE, SZ_4K),
	AV1_FGS_OFF = ALIGN(AV1_UPS_OFF + AV1_UPS_SIZE, SZ_4K),
	AV1_MMU_VBH_OFF = ALIGN(AV1_FGS_OFF + AV1_FGS_TABLE_SIZE * AV1_MAX_BUFS, SZ_4K),
	AV1_MPRED_ABV_OFF = ALIGN(AV1_MMU_VBH_OFF + AV1_MMU_VBH_SIZE, SZ_4K),
	AV1_RPM_OFF = ALIGN(AV1_MPRED_ABV_OFF + AV1_MPRED_ABV_SIZE, SZ_4K),
	AV1_LMEM_OFF = ALIGN(AV1_RPM_OFF + AV1_RPM_SIZE, SZ_4K),
	AV1_AUX_OFF = ALIGN(AV1_LMEM_OFF + AV1_LMEM_SIZE, SZ_4K),
	AV1_SINK_OFF = ALIGN(AV1_AUX_OFF + AV1_AUX_SIZE, SZ_4K),
	AV1_WORKSPACE_SIZE = ALIGN(AV1_SINK_OFF + AV1_SINK_SIZE, SZ_64K),
};

#define AV1_MAX_LOOP_FILTER	63

/* State kept with each CAPTURE buffer for the pictures that refer to it */
struct av1_buf {
	bool valid;
	u8 frame_type;
	bool intra_only;
	u32 order_hint;
	u32 ref_order_hints[V4L2_AV1_REFS_PER_FRAME];
	u32 width;		/* upscaled */
	u32 height;
	u32 mi_cols;
	u32 mi_rows;
	bool seg_enabled;
	bool seg_update_map;
	bool prev_seg_enabled;
	s32 gm_params[V4L2_AV1_TOTAL_REFS_PER_FRAME][6];
	u32 fgs[AV1_FGS_REGS];
	bool fgs_valid;
};

/* The request being decoded */
struct av1_req {
	struct v4l2_ctrl_av1_sequence seq;
	struct v4l2_ctrl_av1_frame frame;
	/* CAPTURE buffer in each reference slot, NULL if not found */
	struct vb2_buffer *slot[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	u32 cur;
	u32 input_end;
};

struct codec_av1 {
	struct mutex lock;
	struct codec_hevc_common common;

	void *workspace_vaddr;
	dma_addr_t workspace_paddr;
	struct amvdec_dma_guard workspace_guard;

	void *mv_vaddr[AV1_MAX_BUFS];
	dma_addr_t mv_paddr[AV1_MAX_BUFS];
	u32 mv_size;

	struct av1_buf buf[AV1_MAX_BUFS];
	struct av1_req req;
	bool have_req;
	bool configured;
	int is_10bit;

	bool decoding;
	bool parked;
	bool failed;
	bool restream_pending;

	u16 rpm[AV1_RPM_WORDS];

	/* loop filter thresholds and levels */
	u8 lf_lim[AV1_MAX_LOOP_FILTER + 1];
	u8 lf_mblim[AV1_MAX_LOOP_FILTER + 1];
	u8 lvl[3][8][2][V4L2_AV1_TOTAL_REFS_PER_FRAME][2];
};

static inline bool av1_inter(const struct v4l2_ctrl_av1_frame *f)
{
	return !av1_frame_is_intra(f);
}

static struct vb2_buffer *av1_ref_vb(struct codec_av1 *av1, u32 ref)
{
	s8 idx = av1->req.frame.ref_frame_idx[ref - V4L2_AV1_REF_LAST_FRAME];

	if (idx < 0 || idx >= V4L2_AV1_TOTAL_REFS_PER_FRAME)
		return NULL;
	return av1->req.slot[idx];
}

static struct av1_buf *av1_ref_buf(struct codec_av1 *av1, u32 ref)
{
	struct vb2_buffer *vb = av1_ref_vb(av1, ref);

	if (!vb || vb->index >= AV1_MAX_BUFS || !av1->buf[vb->index].valid)
		return NULL;
	return &av1->buf[vb->index];
}

static struct av1_buf *av1_primary_buf(struct codec_av1 *av1)
{
	u32 primary = av1->req.frame.primary_ref_frame;

	if (primary >= V4L2_AV1_REFS_PER_FRAME || !av1_inter(&av1->req.frame))
		return NULL;
	return av1_ref_buf(av1, V4L2_AV1_REF_LAST_FRAME + primary);
}

static int av1_primary_index(struct codec_av1 *av1)
{
	u32 primary = av1->req.frame.primary_ref_frame;
	struct vb2_buffer *vb;

	if (!av1_primary_buf(av1))
		return -1;
	vb = av1_ref_vb(av1, V4L2_AV1_REF_LAST_FRAME + primary);
	return vb->index;
}

static void av1_free_mv(struct amvdec_core *core, struct codec_av1 *av1)
{
	int i;

	for (i = 0; i < AV1_MAX_BUFS; i++) {
		if (!av1->mv_vaddr[i])
			continue;
		dma_free_coherent(core->dev, av1->mv_size, av1->mv_vaddr[i],
				  av1->mv_paddr[i]);
		av1->mv_vaddr[i] = NULL;
	}
}

/* Motion vectors of one picture, for the largest superblock size */
static u32 av1_mv_size(u32 width, u32 height)
{
	u32 lcu_x = DIV_ROUND_UP(width, 128), lcu_y = DIV_ROUND_UP(height, 128);
	u32 a = lcu_x * lcu_y * 16 * 64;
	u32 b = lcu_x * ((lcu_y >> 3) + (lcu_y & 7)) * 16;
	u32 lcu_x64 = DIV_ROUND_UP(width, 64), lcu_y64 = DIV_ROUND_UP(height, 64);
	u32 a64 = lcu_x64 * lcu_y64 * 16 * 16;
	u32 b64 = lcu_x64 * ((lcu_y64 >> 3) + (lcu_y64 & 7)) * 16;

	return ALIGN(max(a + b, a64 + b64), SZ_64K);
}

static int av1_alloc(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	int i;

	av1->workspace_vaddr = meson_amvdec_dma_alloc_guarded(core->dev,
							      AV1_WORKSPACE_SIZE,
							      &av1->workspace_paddr,
							      GFP_KERNEL,
							      &av1->workspace_guard);
	if (!av1->workspace_vaddr)
		return -ENOMEM;
	memset(av1->workspace_vaddr, 0, AV1_WORKSPACE_SIZE);

	av1->mv_size = av1_mv_size(sess->width, sess->height);
	for (i = 0; i < AV1_MAX_BUFS; i++) {
		av1->mv_vaddr[i] = dma_alloc_coherent(core->dev, av1->mv_size,
						      &av1->mv_paddr[i],
						      GFP_KERNEL);
		if (!av1->mv_vaddr[i]) {
			av1_free_mv(core, av1);
			meson_amvdec_dma_free_guarded(core->dev,
						      av1->workspace_paddr,
						      &av1->workspace_guard);
			av1->workspace_vaddr = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

static void av1_update_sharpness(struct codec_av1 *av1, u32 sharpness)
{
	int lvl, lim;

	for (lvl = 0; lvl <= AV1_MAX_LOOP_FILTER; lvl++) {
		lim = lvl >> ((sharpness > 0) + (sharpness > 4));
		if (sharpness > 0 && lim > 9 - (int)sharpness)
			lim = 9 - sharpness;
		if (lim < 1)
			lim = 1;
		av1->lf_lim[lvl] = lim;
		av1->lf_mblim[lvl] = 2 * (lvl + 2) + lim;
	}
}

static void av1_write_lf_thresholds(struct amvdec_core *core,
				    struct codec_av1 *av1)
{
	int i;

	for (i = 0; i < 32; i++) {
		u32 thr = ((av1->lf_lim[i * 2 + 1] & 0x3f) << 8) |
			  av1->lf_mblim[i * 2 + 1];

		thr = (thr << 16) | ((av1->lf_lim[i * 2] & 0x3f) << 8) |
		      av1->lf_mblim[i * 2];
		meson_amvdec_write_dos(core, HEVC_DBLK_CFG9, thr);
	}
}

static void av1_hw_init(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	dma_addr_t ws = av1->workspace_paddr;
	int i;

	meson_amvdec_write_dos(core, AV1_RPM_BUFFER, ws + AV1_RPM_OFF);
	meson_amvdec_write_dos(core, AV1_DAALA_TOP_BUFFER, ws + AV1_DAALA_TOP_OFF);
	meson_amvdec_write_dos(core, AV1_GMC_PARAM_BUFF_ADDR, ws + AV1_GMC_OFF);
	meson_amvdec_write_dos(core, HEVCD_IPP_LINEBUFF_BASE, ws + AV1_IPP_OFF);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFGE, ws + AV1_DBLK_PARA_OFF);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFG4, ws + AV1_DBLK_PARA_OFF);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFG5, ws + AV1_DBLK_DATA_OFF);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFG3, 0x404010);

	meson_amvdec_write_dos(core, HEVC_SAO_MMU_VH0_ADDR, ws + AV1_MMU_VBH_OFF);
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_VH1_ADDR,
			       ws + AV1_MMU_VBH_OFF + AV1_MMU_VBH_SIZE / 2);
	meson_amvdec_write_dos_bits(core, HEVC_SAO_CTRL5, BIT(10));
	meson_amvdec_clear_dos_bits(core, HEVC_SAO_CTRL5, BIT(15));
	/* No MMU double write or triple write: disable both, and aim them away */
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_DMA_CTRL2, 0xffffffff);
	meson_amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR3, 0xffffffff);
	meson_amvdec_write_dos(core, HEVC_SAO_C_START_ADDR3, 0xffffffff);
	meson_amvdec_write_dos(core, HEVC_CM_HEADER_START_ADDR2, ws + AV1_SINK_OFF);
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_VH0_ADDR2, ws + AV1_SINK_OFF);
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_VH1_ADDR2, ws + AV1_SINK_OFF);
	meson_amvdec_write_dos(core, HEVC_DW_VH0_ADDDR, ws + AV1_SINK_OFF);
	meson_amvdec_write_dos(core, HEVC_DW_VH1_ADDDR, ws + AV1_SINK_OFF);
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_DMA_CTRL, av1->common.mmu_map_paddr);
	meson_amvdec_write_dos(core, HEVC_ASSIST_MMU_MAP_ADDR, av1->common.mmu_map_paddr);
	/* compressed co-located motion vectors */
	meson_amvdec_write_dos_bits(core, HEVC_MPRED_CTRL4, BIT(1));

	meson_amvdec_write_dos(core, HEVC_SHIFT_CONTROL,
			       BIT(10) | BIT(9) | (3 << 6) | (2 << 4) |
			       (3 << 1) | BIT(0));
	meson_amvdec_write_dos(core, AV1_DEC_STATUS, 0);
	/* efficiency mode: the firmware programs the parser itself; no RDMA */
	meson_amvdec_write_dos(core, AV1_COMPATIBILITY, BIT(1) | BIT(0));
	/* the firmware writes metadata here; no debug log or dumps */
	meson_amvdec_write_dos(core, AV1_AUX_ADR, ws + AV1_AUX_OFF);
	meson_amvdec_write_dos(core, AV1_AUX_DATA_SIZE, (AV1_AUX_SIZE >> 4) << 16);
	meson_amvdec_write_dos(core, AV1_DEBUG_LOG_ADR, 0);
	meson_amvdec_write_dos(core, AV1_DEBUG_DUMP_ADR, 0);

	meson_amvdec_write_dos(core, AV1_DECODE_MODE, AV1_DECODE_MODE_SINGLE);
	/* Current microcode treats a zero size as a limit, not as unlimited. */
	meson_amvdec_write_dos(core, AV1_DECODE_SIZE, 0x7fffffff);
	meson_amvdec_write_dos(core, AV1_DECODE_COUNT, 0);

	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE, BIT(16));
	for (i = 0; i < ARRAY_SIZE(meson_amvdec_hevc_parser_cmd); i++)
		meson_amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE,
				       meson_amvdec_hevc_parser_cmd[i]);
	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
	meson_amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);

	meson_amvdec_write_dos(core, HEVCD_MPP_DECOMP_AXIURG_CTL,
			       0x1 | BIT(2) | BIT(3) | (24 << 4) | (32 << 11) |
			       (24 << 18) | (32 << 25));

	av1_update_sharpness(av1, 0);
	av1_write_lf_thresholds(core, av1);
	/* first and second write, AV1 format */
	meson_amvdec_write_dos(core, HEVC_DBLK_CFGB, (0x57 << 8) | 0x4);

	meson_amvdec_write_dos_bits(core, HEVC_STREAM_CONTROL, BIT(0));
	meson_amvdec_write_dos_bits(core, HEVC_STREAM_FIFO_CTL, BIT(29));
	meson_amvdec_write_dos(core, AV1_WAIT_FLAG, 1);
	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX0_CLR_REG, 1);
	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX0_MASK, 1);
	meson_amvdec_write_dos(core, HEVC_PSCALE_CTRL, 0);
	meson_amvdec_write_dos(core, AV1_NAL_SEARCH_CTL, 0x8);
	meson_amvdec_write_dos(core, AV1_DECODE_STOP_POS, 0);
	/* shares its register with NAL_SEARCH_CTL; the vendor writes it last */
	meson_amvdec_write_dos(core, AV1_LMEM_DUMP_ADR, ws + AV1_LMEM_OFF);
}

static int codec_av1_start(struct amvdec_session *sess)
{
	struct codec_av1 *av1;
	int ret;

	av1 = kzalloc_obj(*av1);
	if (!av1)
		return -ENOMEM;
	if (sess->core->irq_mbox0 <= 0) {
		kfree(av1);
		return -ENODEV;
	}
	mutex_init(&av1->lock);

	meson_amvdec_codec_hevc_fbc_pool_reclaim();
	ret = av1_alloc(sess, av1);
	if (ret) {
		kfree(av1);
		return ret;
	}
	sess->priv = av1;
	av1_hw_init(sess, av1);
	return 0;
}

static int codec_av1_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_av1 *av1 = sess->priv;

	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX0_MASK, 0);
	mutex_lock(&av1->lock);
	if (av1->workspace_vaddr) {
		meson_amvdec_dma_verify_guard(core->dev, "av1.workspace",
					      &av1->workspace_guard);
		meson_amvdec_dma_free_guarded(core->dev, av1->workspace_paddr,
					      &av1->workspace_guard);
		av1->workspace_vaddr = NULL;
		av1_free_mv(core, av1);
	}
	meson_amvdec_codec_hevc_free_fbc_buffers(sess, &av1->common);
	mutex_unlock(&av1->lock);
	meson_amvdec_codec_hevc_fbc_pool_reclaim();
	return 0;
}

/* Write the reference slots the next frame header is parsed against. */
static void av1_write_ref_info(struct amvdec_core *core, struct codec_av1 *av1)
{
	u32 hints[2] = {}, sizes[8] = {}, idx[2] = {};
	int i;

	for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++) {
		struct vb2_buffer *vb = av1->req.slot[i];
		struct av1_buf *b;

		if (!vb || vb->index >= AV1_MAX_BUFS || !av1->buf[vb->index].valid)
			continue;
		b = &av1->buf[vb->index];
		hints[i / 4] |= (b->order_hint & 0xff) << ((i % 4) * 8);
		sizes[i] = (b->width << 16) | (b->height & 0xffff);
		idx[i / 4] |= (vb->index & 0xff) << ((i % 4) * 8);
	}
	meson_amvdec_write_dos(core, HEVC_PARSER_MEM_WR_ADDR, AV1_PARSER_REF_INFO);
	for (i = 0; i < 2; i++)
		meson_amvdec_write_dos(core, HEVC_PARSER_MEM_RW_DATA, hints[i]);
	for (i = 0; i < 8; i++)
		meson_amvdec_write_dos(core, HEVC_PARSER_MEM_RW_DATA, sizes[i]);
	for (i = 0; i < 2; i++)
		meson_amvdec_write_dos(core, HEVC_PARSER_MEM_RW_DATA, idx[i]);
}

static int av1_build_tile_group(const struct v4l2_ctrl_av1_frame *f,
				const struct v4l2_ctrl_av1_tile_group_entry *tg,
				u32 num_tiles, const u8 *data, u32 size,
				u8 *out, u32 out_size)
{
	u32 tiles = f->tile_info.tile_cols * f->tile_info.tile_rows;
	u32 n = 0, i, j;

	if (num_tiles != tiles)
		return -EINVAL;
	/* tile_start_and_end_present_flag = 0, then byte alignment */
	if (tiles > 1)
		out[n++] = 0;
	for (i = 0; i < tiles; i++) {
		const struct v4l2_ctrl_av1_tile_group_entry *e = NULL;
		u32 row = i / f->tile_info.tile_cols;
		u32 col = i % f->tile_info.tile_cols;

		for (j = 0; j < num_tiles; j++)
			if (tg[j].tile_row == row && tg[j].tile_col == col)
				e = &tg[j];
		if (!e || !e->tile_size || e->tile_offset > size ||
		    e->tile_size > size - e->tile_offset ||
		    n + AV1_SYNTH_TILE_SIZE_BYTES + e->tile_size > out_size)
			return -EINVAL;
		if (i + 1 < tiles) {
			put_unaligned_le32(e->tile_size - 1, out + n);
			n += AV1_SYNTH_TILE_SIZE_BYTES;
		}
		memcpy(out + n, data + e->tile_offset, e->tile_size);
		n += e->tile_size;
	}
	return n;
}

static u32 av1_put_prefixed_obu(u8 *out, u32 type, const u8 *payload, u32 len)
{
	u32 hdr = 1 + av1_leb128_size(len);

	av1_amlv_prefix(out, hdr + len);
	av1_obu_header(out + AV1_AMLV_PREFIX_SIZE, type, len);
	memmove(out + AV1_AMLV_PREFIX_SIZE + hdr, payload, len);
	return AV1_AMLV_PREFIX_SIZE + hdr + len;
}

int meson_amvdec_codec_av1_prepare(struct amvdec_session *sess,
				   const struct v4l2_ctrl_av1_sequence *seq,
				   const struct v4l2_ctrl_av1_frame *f,
				   const struct v4l2_ctrl_av1_tile_group_entry *tg,
				   u32 num_tiles, u8 *data, u32 *payload,
				   u32 plane_size)
{
	struct codec_av1 *av1 = sess->priv;
	struct av1_synth_tiles tiles;
	const s32 (*prev_gm)[6] = NULL;
	u32 slot_hints[V4L2_AV1_TOTAL_REFS_PER_FRAME] = {};
	struct av1_bw bw;
	struct av1_buf *pb;
	u32 cap, n, hdr_len, seq_len;
	u8 *buf, *hdr;
	int ret, i;

	if (!av1)
		return -EINVAL;
	if (f->upscaled_width != sess->width ||
	    f->frame_height_minus_1 + 1 != sess->height ||
	    (av1->configured && seq->bit_depth != (av1->is_10bit ? 10 : 8)))
		return -EOPNOTSUPP;

	mutex_lock(&av1->lock);
	av1->req.seq = *seq;
	av1->req.frame = *f;
	for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++) {
		struct vb2_buffer *vb = sess->request_job.refs[i];

		av1->req.slot[i] = vb;
		if (vb && vb->index < AV1_MAX_BUFS && av1->buf[vb->index].valid)
			slot_hints[i] = av1->buf[vb->index].order_hint;
	}
	av1->req.cur = sess->request_job.dst->vb2_buf.index;
	pb = av1_primary_buf(av1);
	if (pb)
		prev_gm = (const s32 (*)[6])pb->gm_params;
	if (av1->req.cur >= AV1_MAX_BUFS) {
		mutex_unlock(&av1->lock);
		return -EINVAL;
	}

	cap = *payload + 4 * num_tiles + 3 * AV1_AMLV_PREFIX_SIZE +
	      AV1_SYNTH_MAX_HEADERS + 64;
	buf = kvmalloc(cap, GFP_KERNEL);
	hdr = kmalloc(AV1_SYNTH_MAX_HEADERS, GFP_KERNEL);
	if (!buf || !hdr) {
		ret = -ENOMEM;
		goto out;
	}

	/* sequence header */
	bw = (struct av1_bw){ .buf = hdr, .size = AV1_SYNTH_MAX_HEADERS };
	av1_synth_seq(&bw, seq);
	seq_len = bw.pos / 8;
	n = av1_put_prefixed_obu(buf, AV1_SYNTH_OBU_SEQUENCE_HEADER, hdr, seq_len);

	/* frame: header, byte alignment, tile group */
	bw = (struct av1_bw){ .buf = hdr, .size = AV1_SYNTH_MAX_HEADERS };
	ret = av1_synth_frame_header(&bw, seq, f, prev_gm, slot_hints, &tiles);
	if (ret)
		goto out;
	av1_byte_align(&bw);
	hdr_len = bw.pos / 8;
	/* frame OBU payload at its final place: prefix + 1 + leb128(5) max */
	{
		u32 base = n + AV1_AMLV_PREFIX_SIZE + 1 + 5;
		int tg_len;

		memcpy(buf + base, hdr, hdr_len);
		tg_len = av1_build_tile_group(f, tg, num_tiles, data, *payload,
					      buf + base + hdr_len,
					      cap - base - hdr_len);
		if (tg_len < 0) {
			ret = tg_len;
			goto out;
		}
		n += av1_put_prefixed_obu(buf + n, AV1_SYNTH_OBU_FRAME,
					  buf + base, hdr_len + tg_len);
	}
	if (n > plane_size) {
		ret = -ENOSPC;
		goto out;
	}
	memcpy(data, buf, n);
	*payload = n;
	av1->req.input_end = 0;
	av1->have_req = true;
	/* The firmware is idle: not started yet, or parked at a picture end. */
	av1_write_ref_info(sess->core, av1);
	ret = 0;
out:
	mutex_unlock(&av1->lock);
	kfree(hdr);
	kvfree(buf);
	return ret;
}

void meson_amvdec_codec_av1_request_input_ready(struct amvdec_session *sess,
						u32 end)
{
	struct codec_av1 *av1 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!av1)
		return;
	mutex_lock(&av1->lock);
	av1->req.input_end = end;
	if (av1->failed) {
		mutex_unlock(&av1->lock);
		return;
	}
	meson_amvdec_write_dos(core, AV1_DECODE_SIZE, end);
	if (av1->parked) {
		av1->parked = false;
		meson_amvdec_write_dos_action(core, AV1_DEC_STATUS, AV1_SEARCH_HEAD);
	}
	dev_dbg(core->dev, "av1: input ready end=%u\n", end);
	mutex_unlock(&av1->lock);
}

static void av1_set_mc(struct amvdec_session *sess, struct codec_av1 *av1,
		       bool inter)
{
	struct amvdec_core *core = sess->core;
	struct av1_buf *cur = &av1->buf[av1->req.cur];
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	u32 dec_w = f->frame_width_minus_1 + 1, dec_h = f->frame_height_minus_1 + 1;
	u32 scale = 0, pass, i;

	for (pass = 0; pass < 2; pass++) {
		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				       ((pass ? 16 : 0) << 8) | 1);
		meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
				       (cur->order_hint << 24) |
				       (av1->req.cur << 16) | (av1->req.cur << 8) |
				       av1->req.cur);
		for (i = V4L2_AV1_REF_LAST_FRAME; i <= V4L2_AV1_REF_ALTREF_FRAME; i++) {
			struct vb2_buffer *vb = inter ? av1_ref_vb(av1, i) : NULL;
			struct av1_buf *b = inter ? av1_ref_buf(av1, i) : cur;
			u32 id = vb ? vb->index : av1->req.cur;

			if (!b) {
				meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
				continue;
			}
			meson_amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
					       (b->order_hint << 24) | (id << 16) |
					       (id << 8) | id);
		}
	}

	meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_TBL_ACCCONFIG, BIT(2));
	for (i = 0; i <= V4L2_AV1_REF_ALTREF_FRAME; i++) {
		struct av1_buf *b = (inter && i) ? av1_ref_buf(av1, i) : cur;
		u32 xs = AV1_REF_NO_SCALE, ys = AV1_REF_NO_SCALE;

		if (!b) {
			meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, 0);
			meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, 0);
		} else {
			meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, b->width);
			meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, b->height);
		}
		if (inter && i && b) {
			xs = ((b->width << 14) + dec_w / 2) / dec_w;
			ys = ((b->height << 14) + dec_h / 2) / dec_h;
			if (xs != AV1_REF_NO_SCALE || ys != AV1_REF_NO_SCALE)
				scale |= BIT(i);
		}
		meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, xs);
		meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, ys);
		/* body size is not used with the frame MMU */
		meson_amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, 0);
	}
	meson_amvdec_write_dos(core, AV1D_MPP_REF_SCALE_ENBL, scale);
	meson_amvdec_write_dos(core, PARSER_REF_SCALE_ENBL, scale);
}

/* Section 7.9 as the vendor reference decoder splits it with the hardware */
struct av1_mfmv {
	u32 count;		/* entries in id/offset/use */
	u32 id[3 + 4];
	s8 offset[3 + 4][V4L2_AV1_TOTAL_REFS_PER_FRAME];
	bool use[3 + 4];
};

static int av1_mf_project(struct codec_av1 *av1, struct av1_mfmv *mf, u32 ref)
{
	const struct v4l2_ctrl_av1_sequence *seq = &av1->req.seq;
	struct av1_buf *cur = &av1->buf[av1->req.cur];
	struct av1_buf *b = av1_ref_buf(av1, ref);
	u32 i = mf->count++, rf;

	mf->id[i] = ref;
	memset(mf->offset[i], 0, sizeof(mf->offset[i]));
	mf->use[i] = false;
	if (!b || b->frame_type == V4L2_AV1_KEY_FRAME || b->intra_only ||
	    b->mi_rows != cur->mi_rows || b->mi_cols != cur->mi_cols)
		return 0;
	for (rf = V4L2_AV1_REF_LAST_FRAME; rf <= V4L2_AV1_REF_ALTREF_FRAME; rf++)
		mf->offset[i][rf] = av1_relative_dist(seq, b->order_hint,
						      b->ref_order_hints[rf - 1]);
	mf->use[i] = true;
	return 1;
}

static void av1_setup_motion_field(struct codec_av1 *av1, struct av1_mfmv *mf)
{
	const struct v4l2_ctrl_av1_sequence *seq = &av1->req.seq;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	u32 hint[V4L2_AV1_REFS_PER_FRAME];
	int stamp = 2, i;
	struct av1_buf *last;

	mf->count = 0;
	if (!av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT))
		return;
	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
		struct av1_buf *b = av1_ref_buf(av1, V4L2_AV1_REF_LAST_FRAME + i);

		hint[i] = b ? b->order_hint : 0;
	}
	last = av1_ref_buf(av1, V4L2_AV1_REF_LAST_FRAME);
	if (last) {
		u32 alt_of_last = last->ref_order_hints[V4L2_AV1_REF_ALTREF_FRAME - 1];

		if (alt_of_last != hint[V4L2_AV1_REF_GOLDEN_FRAME - 1])
			av1_mf_project(av1, mf, V4L2_AV1_REF_LAST_FRAME);
		stamp--;
	}
	if (av1_relative_dist(seq, hint[V4L2_AV1_REF_BWDREF_FRAME - 1], f->order_hint) > 0 &&
	    av1_mf_project(av1, mf, V4L2_AV1_REF_BWDREF_FRAME))
		stamp--;
	if (av1_relative_dist(seq, hint[V4L2_AV1_REF_ALTREF2_FRAME - 1], f->order_hint) > 0 &&
	    av1_mf_project(av1, mf, V4L2_AV1_REF_ALTREF2_FRAME))
		stamp--;
	if (av1_relative_dist(seq, hint[V4L2_AV1_REF_ALTREF_FRAME - 1], f->order_hint) > 0 &&
	    stamp >= 0 && av1_mf_project(av1, mf, V4L2_AV1_REF_ALTREF_FRAME))
		stamp--;
	if (stamp >= 0)
		av1_mf_project(av1, mf, V4L2_AV1_REF_LAST2_FRAME);
}

static void av1_set_mpred(struct amvdec_session *sess, struct codec_av1 *av1,
			  bool inter)
{
	static const u32 ref_buf_reg[3] = {
		HEVC_MPRED_L0_REF03_POC, HEVC_MPRED_L0_REF03_POC + 4,
		HEVC_MPRED_L0_REF03_POC + 8,
	};
	struct amvdec_core *core = sess->core;
	const struct v4l2_ctrl_av1_sequence *seq = &av1->req.seq;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	struct av1_buf *cur = &av1->buf[av1->req.cur];
	u32 ref_buf_val[3] = {}, ref_off_val[6] = {}, mv_ref_id[3] = {};
	u32 data, tpl = 0, reg_i, i, j;
	struct av1_mfmv mf = {};
	int pos;

	meson_amvdec_write_dos(core, HEVC_MPRED_CTRL3, 0x10151015);
	meson_amvdec_write_dos(core, HEVC_MPRED_ABV_START_ADDR,
			       av1->workspace_paddr + AV1_MPRED_ABV_OFF);

	if (inter) {
		data = meson_amvdec_read_dos(core, HEVC_MPRED_CTRL4) & ~(0xff << 12);
		for (i = V4L2_AV1_REF_LAST_FRAME; i <= V4L2_AV1_REF_ALTREF_FRAME; i++) {
			struct av1_buf *b = av1_ref_buf(av1, i);

			if (b && av1_relative_dist(seq, b->order_hint, f->order_hint) > 0)
				data |= BIT(12 + i);
		}
		meson_amvdec_write_dos(core, HEVC_MPRED_CTRL4, data);
	} else {
		meson_amvdec_clear_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));
	}

	data = (av1_seq_flag(seq, V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT) << 27) |
	       ((seq->order_hint_bits ? seq->order_hint_bits - 1 : 0) << 24) |
	       ((cur->order_hint & 0xff) << 16) | (0x10 << 8) | 0x10;
	meson_amvdec_write_dos(core, HEVC_MPRED_L0_REF00_POC, data);

	if (inter) {
		/* reference buffer indices, then their order hints */
		data = 0;
		pos = 25;
		reg_i = 0;
		for (i = V4L2_AV1_REF_ALTREF_FRAME; i >= V4L2_AV1_REF_LAST_FRAME; i--) {
			struct vb2_buffer *vb = av1_ref_vb(av1, i);

			if (vb)
				data |= vb->index << pos;
			if (pos == 0) {
				ref_buf_val[reg_i++] = data;
				data = 0;
				pos = 24;
			} else {
				pos -= pos == 24 ? 8 : 5;
			}
		}
		for (i = V4L2_AV1_REF_ALTREF_FRAME; i >= V4L2_AV1_REF_LAST_FRAME; i--) {
			struct av1_buf *b = av1_ref_buf(av1, i);

			if (b)
				data |= (b->order_hint & 0xff) << pos;
			if (pos == 0) {
				ref_buf_val[reg_i++] = data;
				data = 0;
				pos = 24;
			} else {
				pos -= 8;
			}
		}
		if (pos != 24 && reg_i < 3)
			ref_buf_val[reg_i] = data;

		/* motion field projection offsets */
		av1_setup_motion_field(av1, &mf);
		data = 0;
		pos = 24;
		reg_i = 0;
		for (i = 0; i < mf.count; i++) {
			if (!mf.use[i])
				continue;
			if (tpl < 3)
				mv_ref_id[tpl] = mf.id[i];
			tpl++;
			for (j = V4L2_AV1_REF_LAST_FRAME; j <= V4L2_AV1_REF_ALTREF_FRAME; j++) {
				data |= ((u8)mf.offset[i][j]) << pos;
				if (pos == 0) {
					if (reg_i < 6)
						ref_off_val[reg_i++] = data;
					data = 0;
					pos = 24;
				} else {
					pos -= 8;
				}
			}
		}
		if (pos != 24 && reg_i < 6)
			ref_off_val[reg_i] = data;
		ref_off_val[5] |= min(tpl, 3u) | (mv_ref_id[0] << 2) |
				  (mv_ref_id[1] << 5) | (mv_ref_id[2] << 8);
	}
	for (i = 0; i < 3; i++)
		meson_amvdec_write_dos(core, ref_buf_reg[i], ref_buf_val[i]);
	for (i = 0; i < 6; i++)
		meson_amvdec_write_dos(core, HEVC_MPRED_L0_REF06_POC + 4 * i,
				       ref_off_val[i]);

	meson_amvdec_write_dos(core, HEVC_MPRED_MV_WR_START_ADDR,
			       av1->mv_paddr[av1->req.cur]);
	meson_amvdec_write_dos(core, HEVC_MPRED_MV_WPTR,
			       av1->mv_paddr[av1->req.cur]);
	for (i = 0; inter && i < min(tpl, 3u); i++) {
		struct vb2_buffer *vb = av1_ref_vb(av1, mv_ref_id[i]);
		dma_addr_t mv;

		if (!vb || vb->index >= AV1_MAX_BUFS)
			continue;
		mv = av1->mv_paddr[vb->index];
		if (i == 0) {
			meson_amvdec_write_dos(core, HEVC_MPRED_MV_RD_START_ADDR, mv);
			meson_amvdec_write_dos(core, HEVC_MPRED_MV_RPTR, mv);
		} else if (i == 1) {
			meson_amvdec_write_dos(core, HEVC_MPRED_L0_REF01_POC, mv);
			meson_amvdec_write_dos(core, HEVC_MPRED_MV_RPTR_1, mv);
		} else {
			meson_amvdec_write_dos(core, HEVC_MPRED_L0_REF02_POC, mv);
			meson_amvdec_write_dos(core, HEVC_MPRED_MV_RPTR_2, mv);
		}
	}

	data = meson_amvdec_read_dos(core, HEVC_MPRED_CTRL0) & ~(BIT(10) | BIT(11));
	data |= BIT(10);
	if (inter && av1_frame_flag(f, V4L2_AV1_FRAME_FLAG_USE_REF_FRAME_MVS))
		data |= BIT(11);
	meson_amvdec_write_dos(core, HEVC_MPRED_CTRL0, data);
}

static int av1_set_sao(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	struct vb2_buffer *vb = &sess->request_job.dst->vb2_buf;
	u32 output_size = meson_amvdec_get_output_size(sess);
	bool sb128 = av1_seq_flag(&av1->req.seq,
				  V4L2_AV1_SEQUENCE_FLAG_USE_128X128_SUPERBLOCK);
	bool nv12 = sess->pixfmt_cap == V4L2_PIX_FMT_NV12M;
	dma_addr_t body, header, y = 0, uv = 0;
	u32 val;

	body = meson_amvdec_codec_hevc_fbc_body_addr(&av1->common, vb->index);
	if (nv12) {
		/* NV12 is a double write; the compressed frame is driver-owned */
		if (vb2_plane_size(vb, 0) < output_size ||
		    vb2_plane_size(vb, 1) < output_size / 2)
			return -EINVAL;
		header = av1->common.mmu_header_paddr[vb->index];
		y = vb2_dma_contig_plane_dma_addr(vb, 0);
		uv = vb2_dma_contig_plane_dma_addr(vb, 1);
	} else if (sess->pixfmt_cap == V4L2_PIX_FMT_AM21C) {
		/* AM21C: the CAPTURE plane is the compression header */
		if (vb2_plane_size(vb, 0) < AM21C_HEADER_SIZE)
			return -EINVAL;
		header = vb2_dma_contig_plane_dma_addr(vb, 0);
	} else {
		return -EINVAL;
	}
	if (!body || !header)
		return -EINVAL;

	meson_amvdec_write_dos_bits(core, HEVC_SAO_CTRL9, BIT(1));
	/* AV1 mode; no downscaling of the double write */
	val = meson_amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~(0xff << 16);
	meson_amvdec_write_dos(core, HEVC_SAO_CTRL5, val | BIT(14));
	meson_amvdec_write_dos(core, HEVC_SAO_CTRL0, sb128 ? 0x7 : 0x6);

	meson_amvdec_write_dos(core, HEVC_CM_BODY_START_ADDR, body);
	meson_amvdec_write_dos(core, HEVC_CM_HEADER_START_ADDR, header);
	meson_amvdec_write_dos(core, HEVC_FGS_TABLE_START,
			       av1->workspace_paddr + AV1_FGS_OFF +
			       AV1_FGS_TABLE_SIZE * vb->index);
	meson_amvdec_write_dos(core, HEVC_FGS_TABLE_LENGTH, AV1_FGS_TABLE_SIZE * 8);

	if (nv12) {
		meson_amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR, y);
		meson_amvdec_write_dos(core, HEVC_SAO_C_START_ADDR, uv);
		meson_amvdec_write_dos(core, HEVC_SAO_Y_WPTR, y);
		meson_amvdec_write_dos(core, HEVC_SAO_C_WPTR, uv);
		meson_amvdec_write_dos(core, HEVC_SAO_Y_LENGTH, output_size);
		meson_amvdec_write_dos(core, HEVC_SAO_C_LENGTH, output_size / 2);
	}

	/* compressed write, and a linear double write for NV12 */
	val = meson_amvdec_read_dos(core, HEVC_DBLK_CFGB) & ~(BIT(8) | BIT(9));
	val |= BIT(8);
	if (nv12)
		val |= BIT(9);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFGB, val);

	val = meson_amvdec_read_dos(core, HEVC_SAO_CTRL1) & ~0x3ff3;
	val |= 0xff0;	/* little endian, NV12 */
	if (!nv12)
		val |= BIT(1);	/* no double write */
	meson_amvdec_write_dos(core, HEVC_SAO_CTRL1, val);

	val = meson_amvdec_read_dos(core, HEVCD_IPP_AXIIF_CONFIG) & ~0x30;
	val |= 0xf;
	val &= ~BIT(12);
	meson_amvdec_write_dos(core, HEVCD_IPP_AXIIF_CONFIG, val);
	return 0;
}

static u32 av1_seg_lf_info(const struct v4l2_av1_segmentation *s, int seg,
			   int f0, int f1)
{
	u32 v = 0;

	if (s->feature_enabled[seg] & V4L2_AV1_SEGMENT_FEATURE_ENABLED(f0))
		v |= BIT(7) | (s->feature_data[seg][f0] & 0x7f);
	if (s->feature_enabled[seg] & V4L2_AV1_SEGMENT_FEATURE_ENABLED(f1))
		v |= BIT(15) | ((s->feature_data[seg][f1] & 0x7f) << 8);
	return v;
}

/* Levels by reference frame and mode, with the loop filter deltas if @lf */
static void av1_fill_lvl(u8 lvl_out[V4L2_AV1_TOTAL_REFS_PER_FRAME][2], int lvl,
			 const struct v4l2_av1_loop_filter *lf)
{
	int scale = 1 << (lvl >> 5), ref, mode;

	if (!lf) {
		memset(lvl_out, lvl, V4L2_AV1_TOTAL_REFS_PER_FRAME * 2);
		return;
	}
	lvl_out[0][0] = clamp(lvl + lf->ref_deltas[0] * scale, 0,
			      AV1_MAX_LOOP_FILTER);
	for (ref = 1; ref < V4L2_AV1_TOTAL_REFS_PER_FRAME; ref++)
		for (mode = 0; mode < 2; mode++)
			lvl_out[ref][mode] = clamp(lvl + (lf->ref_deltas[ref] +
							  lf->mode_deltas[mode]) * scale,
						   0, AV1_MAX_LOOP_FILTER);
}

static void av1_set_loop_filter(struct amvdec_session *sess,
				struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	const struct v4l2_av1_loop_filter *lf = &f->loop_filter;
	const struct v4l2_av1_segmentation *s = &f->segmentation;
	bool seg_on = s->flags & V4L2_AV1_SEGMENTATION_FLAG_ENABLED;
	bool delta_on = lf->flags & V4L2_AV1_LOOP_FILTER_FLAG_DELTA_ENABLED;
	u32 seg_y[8], seg_c[8], level, val;
	int filt[3], filt_r[3], plane, seg, dir, i;

	for (seg = 0; seg < 8; seg++) {
		seg_y[seg] = seg_on ? av1_seg_lf_info(s, seg, 1, 2) : 0;
		seg_c[seg] = seg_on ? av1_seg_lf_info(s, seg, 3, 4) : 0;
	}
	filt[0] = lf->level[0];
	filt_r[0] = lf->level[1];
	filt[1] = filt_r[1] = lf->level[2];
	filt[2] = filt_r[2] = lf->level[3];

	av1_update_sharpness(av1, lf->sharpness);
	av1_write_lf_thresholds(core, av1);

	memset(av1->lvl, 0, sizeof(av1->lvl));
	for (plane = 0; plane < 3; plane++) {
		if (plane == 0 && !filt[0] && !filt_r[0])
			break;
		if (plane && !filt[plane])
			continue;
		for (seg = 0; seg < 8; seg++) {
			for (dir = 0; dir < 2; dir++) {
				int lvl = dir ? filt_r[plane] : filt[plane];
				int feat = plane == 2 ? 4 : plane == 1 ? 3 : 1 + dir;

				if (seg_on && (s->feature_enabled[seg] &
					       V4L2_AV1_SEGMENT_FEATURE_ENABLED(feat)))
					lvl = clamp(lvl + s->feature_data[seg][feat],
						    0, AV1_MAX_LOOP_FILTER);
				av1_fill_lvl(av1->lvl[plane][seg][dir], lvl,
					     delta_on ? lf : NULL);
			}
		}
	}
	for (i = 0; i < 192; i++) {
		u32 p = (i >> 6) & 3, sg = (i >> 3) & 7, r = i & 7;

		level = ((av1->lvl[p][sg][1][r][1] & 0x3f) << 24) |
			((av1->lvl[p][sg][1][r][0] & 0x3f) << 16) |
			((av1->lvl[p][sg][0][r][1] & 0x3f) << 8) |
			(av1->lvl[p][sg][0][r][0] & 0x3f);
		if (!lf->level[0] && !lf->level[1])
			level = 0;
		meson_amvdec_write_dos(core, HEVC_DBLK_CFGA, level);
	}
	meson_amvdec_write_dos(core, HEVC_DBLK_DBLK0,
			       lf->level[0] | (lf->level[1] << 6) |
			       (lf->level[2] << 12) | (lf->level[3] << 18));
	for (i = 0; i < 10; i++)
		meson_amvdec_write_dos(core, HEVC_DBLK_DBLK1,
				       (u8)(i < 2 ? lf->mode_deltas[i] :
					    lf->ref_deltas[i - 2]));
	for (i = 0; i < 8; i++)
		meson_amvdec_write_dos(core, HEVC_DBLK_DBLK2,
				       seg_y[i] | (seg_c[i] << 16));

	val = meson_amvdec_read_dos(core, HEVC_DBLK_CFGB) & ~(BIT(28) | BIT(29) | (3 << 4));
	if (delta_on)
		val |= BIT(28);
	if (seg_on)
		val |= BIT(29);
	if (f->frame_width_minus_1 + 1 >= 1280)
		val |= BIT(4);
	meson_amvdec_write_dos(core, HEVC_DBLK_CFGB, val);

	meson_amvdec_write_dos(core, HEVC_DBLK_CDEF0,
			       av1->workspace_paddr + AV1_CDEF_OFF);
	val = (meson_amvdec_read_dos(core, HEVC_DBLK_CDEF1) & 0xffffff00) | 17;
	meson_amvdec_write_dos(core, HEVC_DBLK_CDEF1, val);
}

static s32 av1_upscale_step(s32 in, s32 out)
{
	return ((in << 14) + out / 2) / out;
}

static s32 av1_upscale_x0(s32 in, s32 out, s32 step)
{
	s32 err = out * step - (in << 14);
	s32 x0 = (-((out - in) << 13) + out / 2) / out + (1 << 7) - err / 2;

	return (u32)x0 & ((1 << 14) - 1);
}

static void av1_set_upscale(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	s32 w = f->frame_width_minus_1 + 1, uw = f->upscaled_width;
	s32 sy = av1_upscale_step(w, uw);
	s32 x0y = av1_upscale_x0(w, uw, sy);
	s32 sc = av1_upscale_step((w + 1) >> 1, (uw + 1) >> 1);
	s32 x0c = av1_upscale_x0((w + 1) >> 1, (uw + 1) >> 1, sc);

	meson_amvdec_write_dos(core, HEVC_DBLK_UPS1, av1->workspace_paddr + AV1_UPS_OFF);
	meson_amvdec_write_dos(core, HEVC_DBLK_UPS2, x0y);
	meson_amvdec_write_dos(core, HEVC_DBLK_UPS3, x0c);
	meson_amvdec_write_dos(core, HEVC_DBLK_UPS4, sy);
	meson_amvdec_write_dos(core, HEVC_DBLK_UPS5, sc);
	meson_amvdec_write_dos(core, AV1_UPSCALE_X0_QN, (x0c << 16) | x0y);
	meson_amvdec_write_dos(core, AV1_UPSCALE_STEP_QN, (sc << 16) | sy);
}

/* Section 7.20, with the reference indices the hardware wants */
static u32 av1_skip_mode_info(struct codec_av1 *av1)
{
	const struct v4l2_ctrl_av1_sequence *seq = &av1->req.seq;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	int idx[2] = { -1, -1 }, i;
	u32 hint[2] = {}, h;

	if (!av1_skip_mode_allowed(seq, f))
		return 0;
	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
		h = f->order_hints[V4L2_AV1_REF_LAST_FRAME + i];
		if (av1_relative_dist(seq, h, f->order_hint) < 0) {
			if (idx[0] < 0 || av1_relative_dist(seq, h, hint[0]) > 0) {
				idx[0] = i;
				hint[0] = h;
			}
		} else if (av1_relative_dist(seq, h, f->order_hint) > 0) {
			if (idx[1] < 0 || av1_relative_dist(seq, h, hint[1]) < 0) {
				idx[1] = i;
				hint[1] = h;
			}
		}
	}
	if (idx[1] < 0) {
		for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
			h = f->order_hints[V4L2_AV1_REF_LAST_FRAME + i];
			if (av1_relative_dist(seq, h, hint[0]) < 0 &&
			    (idx[1] < 0 || av1_relative_dist(seq, h, hint[1]) > 0)) {
				idx[1] = i;
				hint[1] = h;
			}
		}
	}
	return BIT(8) | (min(idx[0], idx[1]) & 0xf) |
	       ((max(idx[0], idx[1]) & 0xf) << 4);
}

static void av1_set_buffers(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	struct av1_buf *cur = &av1->buf[av1->req.cur];
	struct av1_buf *prev = av1_primary_buf(av1);
	int prev_idx = av1_primary_index(av1);
	dma_addr_t ws = av1->workspace_paddr;
	u32 val;

	meson_amvdec_write_dos(core, AV1_CDF_BUFFER_W,
			       ws + AV1_CDF_OFF + AV1_CDF_SIZE * av1->req.cur);
	meson_amvdec_write_dos(core, AV1_SEG_W_ADDR,
			       ws + AV1_SEG_MAP_OFF + AV1_SEG_MAP_SIZE * av1->req.cur);
	meson_amvdec_write_dos(core, AV1_CDF_BUFFER_R, ws + AV1_CDF_OFF +
			       (prev ? AV1_CDF_SIZE * prev_idx : 0));
	meson_amvdec_write_dos(core, AV1_SEG_R_ADDR, ws + AV1_SEG_MAP_OFF +
			       (prev ? AV1_SEG_MAP_SIZE * prev_idx : 0));

	cur->prev_seg_enabled = prev && prev->seg_enabled &&
				(prev->seg_update_map || prev->prev_seg_enabled) &&
				prev->mi_rows == cur->mi_rows &&
				prev->mi_cols == cur->mi_cols;
	val = (cur->prev_seg_enabled << 31) |
	      ((prev ? prev->intra_only : 0) << 30) |
	      ((prev ? prev_idx : 0x1f) << 24) | (av1->req.cur << 16) |
	      av1_skip_mode_info(av1);
	meson_amvdec_write_dos(core, AV1_SKIP_MODE_INFO, val);

	meson_amvdec_write_dos(core, HEVC_PARSER_MEM_WR_ADDR,
			       AV1_PARSER_SEG_INFO + av1->req.cur);
	meson_amvdec_write_dos(core, HEVC_PARSER_MEM_RW_DATA,
			       (f->segmentation.flags & V4L2_AV1_SEGMENTATION_FLAG_ENABLED) ?
			       meson_amvdec_read_dos(core, AV1_REF_SEG_INFO) : 0);
}

/* Record what later pictures need from the one being decoded. */
static void av1_save_state(struct codec_av1 *av1)
{
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	struct av1_buf *cur = &av1->buf[av1->req.cur];
	int i;

	cur->frame_type = f->frame_type;
	cur->intra_only = (av1->rpm[RPM_SEG_ENABLED] >> 2) & 1;
	cur->order_hint = f->order_hint;
	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++)
		cur->ref_order_hints[i] = f->order_hints[V4L2_AV1_REF_LAST_FRAME + i];
	cur->width = f->upscaled_width;
	cur->height = f->frame_height_minus_1 + 1;
	cur->mi_cols = 2 * ((f->frame_width_minus_1 + 1 + 7) >> 3);
	cur->mi_rows = 2 * ((f->frame_height_minus_1 + 1 + 7) >> 3);
	cur->seg_enabled = f->segmentation.flags & V4L2_AV1_SEGMENTATION_FLAG_ENABLED;
	cur->seg_update_map = f->segmentation.flags & V4L2_AV1_SEGMENTATION_FLAG_UPDATE_MAP;
	memcpy(cur->gm_params, f->global_motion.params, sizeof(cur->gm_params));
	cur->fgs_valid = false;
	cur->valid = true;
}

static void av1_check_rpm(struct amvdec_session *sess, struct codec_av1 *av1)
{
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	u16 *r = av1->rpm;

	if (r[RPM_FRAME_TYPE] != f->frame_type ||
	    r[RPM_ORDER_HINT] != (f->order_hint & 0xffff) ||
	    r[RPM_DEC_WIDTH] != f->frame_width_minus_1 + 1 ||
	    r[RPM_HEIGHT] != f->frame_height_minus_1 + 1) {
		dev_warn_ratelimited(sess->core->dev,
				     "av1: firmware parsed type %u hint %u size %ux%u, controls %u %u %ux%u\n",
				     r[RPM_FRAME_TYPE], r[RPM_ORDER_HINT],
				     r[RPM_DEC_WIDTH], r[RPM_HEIGHT],
				     f->frame_type, f->order_hint,
				     f->frame_width_minus_1 + 1,
				     f->frame_height_minus_1 + 1);
	}
}

static int av1_process_frame(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	struct vb2_buffer *vb = &sess->request_job.dst->vb2_buf;
	const struct v4l2_ctrl_av1_frame *f = &av1->req.frame;
	bool inter = av1_inter(f);
	int ret;

	memcpy(av1->rpm, av1->workspace_vaddr + AV1_RPM_OFF, sizeof(av1->rpm));
	av1_check_rpm(sess, av1);

	if (!av1->configured) {
		av1->is_10bit = sess->bitdepth == 10;
		ret = meson_amvdec_codec_hevc_setup_buffers(sess, &av1->common,
							    av1->is_10bit);
		if (ret)
			return ret;
		meson_amvdec_codec_hevc_setup_decode_head(sess, av1->is_10bit);
		meson_amvdec_write_dos(core, HEVC_SAO_MMU_DMA_CTRL,
				       av1->common.mmu_map_paddr);
		meson_amvdec_write_dos(core, HEVC_ASSIST_MMU_MAP_ADDR,
				       av1->common.mmu_map_paddr);
		av1->configured = true;
	}
	if (meson_amvdec_codec_hevc_ensure_frame_buffer(sess, &av1->common, vb,
							av1->is_10bit) ||
	    meson_amvdec_codec_hevc_fill_mmu_map(sess, &av1->common, vb,
						 av1->is_10bit))
		return -ENOMEM;

	/* The picture being decoded no longer holds what was in its buffer. */
	av1->buf[av1->req.cur].valid = false;
	av1_save_state(av1);

	meson_amvdec_write_dos(core, HEVC_PARSER_PICTURE_SIZE,
			       ((f->frame_height_minus_1 + 1) << 16) | f->upscaled_width);
	meson_amvdec_write_dos(core, HEVC_ASSIST_PIC_SIZE_FB_READ,
			       ((f->frame_height_minus_1 + 1) << 16) | f->upscaled_width);
	meson_amvdec_write_dos(core, AV1_PIC_END_LCU_COUNT, 0);

	av1_set_mc(sess, av1, inter);
	av1_set_mpred(sess, av1, inter);
	meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0x2);
	meson_amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0x0);
	ret = av1_set_sao(sess, av1);
	if (ret)
		return ret;
	av1_set_loop_filter(sess, av1);
	av1_set_upscale(sess, av1);
	av1_set_buffers(sess, av1);

	if (av1->req.input_end)
		meson_amvdec_write_dos(core, AV1_DECODE_SIZE, av1->req.input_end);
	dev_dbg(core->dev, "av1: decode idx=%u type=%u hint=%u inter=%u end=%u\n",
		av1->req.cur, f->frame_type, f->order_hint, inter,
		av1->req.input_end);
	av1->decoding = true;
	meson_amvdec_write_dos_action(core, AV1_DEC_STATUS, AV1_DECODE_SLICE);
	return 0;
}

static void av1_film_grain(struct amvdec_session *sess, struct codec_av1 *av1)
{
	struct amvdec_core *core = sess->core;
	u32 status = meson_amvdec_read_dos(core, AV1_FG_STATUS);
	struct av1_buf *cur = av1->have_req ? &av1->buf[av1->req.cur] : NULL;
	int i;

	meson_amvdec_write_dos(core, AV1_FG_STATUS, AV1_FGS_PARAM_CONT);
	meson_amvdec_write_dos(core, AV1_DEC_STATUS, AV1_FGS_PARAM_CONT);
	if (status & BIT(11)) {
		struct vb2_buffer *vb = av1->req.slot[(status >> 8) & 7];
		struct av1_buf *src = vb && vb->index < AV1_MAX_BUFS ?
				      &av1->buf[vb->index] : NULL;

		if (!src || !src->fgs_valid)
			return;
		meson_amvdec_write_dos(core, HEVC_FGS_IDX, 0);
		for (i = 0; i < AV1_FGS_REGS; i++) {
			meson_amvdec_write_dos(core, HEVC_FGS_DATA, src->fgs[i]);
			if (cur)
				cur->fgs[i] = src->fgs[i];
		}
		if (cur)
			cur->fgs_valid = true;
		meson_amvdec_write_dos_bits(core, HEVC_FGS_CTRL, BIT(0));
	} else if (cur) {
		meson_amvdec_write_dos(core, HEVC_FGS_IDX, 0);
		for (i = 0; i < AV1_FGS_REGS; i++)
			cur->fgs[i] = meson_amvdec_read_dos(core, HEVC_FGS_DATA);
		cur->fgs_valid = true;
	}
}

static irqreturn_t codec_av1_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_av1 *av1 = sess->priv;
	u32 status;

	if (!av1)
		return IRQ_HANDLED;
	mutex_lock(&av1->lock);
	if (!av1->workspace_vaddr || av1->failed)
		goto unlock;

	status = meson_amvdec_read_dos(core, AV1_DEC_STATUS) & 0xff;
	meson_amvdec_trace(sess, AMVDEC_TR_IRQ, status,
			   meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
			   meson_amvdec_read_dos(core, AV1_DECODE_SIZE));
	dev_dbg(core->dev, "av1: irq status=%x shift=%u size=%u\n", status,
		meson_amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT),
		meson_amvdec_read_dos(core, AV1_DECODE_SIZE));

	switch (status) {
	case AV1_FGS_PARAM:
		av1_film_grain(sess, av1);
		break;
	case AV1_SEQ_HEAD_DONE:
		meson_amvdec_write_dos_action(core, AV1_DEC_STATUS, AV1_SEARCH_HEAD);
		break;
	case AV1_FRAME_HEAD_DONE:
	case AV1_FRAME_DONE:
		if (!av1->have_req || av1->decoding || !sess->request_job.dst ||
		    av1_process_frame(sess, av1))
			goto fail;
		break;
	case AV1_DEC_PIC_END:
		if (!av1->decoding || !sess->request_job.dst)
			goto fail;
		av1->decoding = false;
		av1->have_req = false;
		av1->parked = true;
		meson_amvdec_codec_av1_arm_restream(sess);
		meson_amvdec_request_signal(sess, sess->request_job.dst);
		break;
	case AV1_SEARCH_BUFEMPTY:
	case AV1_DECODE_BUFEMPTY:
	case AV1_ACTION_DONE:
		break;
	default:
		goto fail;
	}
	goto unlock;

fail:
	dev_err(core->dev, "av1: request failed, status %x decoding %u\n",
		status, av1->decoding);
	av1->failed = true;
	meson_amvdec_abort(sess);
unlock:
	mutex_unlock(&av1->lock);
	return IRQ_HANDLED;
}

static irqreturn_t codec_av1_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX0_CLR_REG, 1);
	return IRQ_WAKE_THREAD;
}

/* See meson_amvdec_codec_hevc_arm_restream(): the same 2 GiB counter limit applies here. */
void meson_amvdec_codec_av1_arm_restream(struct amvdec_session *sess)
{
	struct codec_av1 *av1 = sess->priv;

	if (av1 && meson_amvdec_read_dos(sess->core, HEVC_SHIFT_BYTE_COUNT) >=
		   (HEVC_STREAM_RESTART_MIB << 20))
		av1->restream_pending = true;
}

bool meson_amvdec_codec_av1_restream_pending(struct amvdec_session *sess)
{
	struct codec_av1 *av1 = sess->priv;

	return av1 && av1->restream_pending;
}

int meson_amvdec_codec_av1_restream_reinit(struct amvdec_session *sess)
{
	struct codec_av1 *av1 = sess->priv;

	av1_hw_init(sess, av1);
	if (av1->configured) {
		if (meson_amvdec_codec_hevc_setup_buffers(sess, &av1->common,
							  av1->is_10bit))
			return -EIO;
		meson_amvdec_codec_hevc_setup_decode_head(sess, av1->is_10bit);
		meson_amvdec_write_dos(sess->core, HEVC_SAO_MMU_DMA_CTRL,
				       av1->common.mmu_map_paddr);
		meson_amvdec_write_dos(sess->core, HEVC_ASSIST_MMU_MAP_ADDR,
				       av1->common.mmu_map_paddr);
	}
	av1->parked = false;
	av1->restream_pending = false;
	return 0;
}

struct amvdec_codec_ops meson_amvdec_codec_av1_ops = {
	.start = codec_av1_start,
	.stop = codec_av1_stop,
	.isr = codec_av1_isr,
	.threaded_isr = codec_av1_threaded_isr,
};
