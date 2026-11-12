// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include "hantro.h"

static const u8 zigzag[64] = {
	0,   1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

void hantro_mpeg2_dec_copy_qtable(u8 *qtable,
				  const struct v4l2_ctrl_mpeg2_quantisation *ctrl)
{
	int i, n;

	if (!qtable || !ctrl)
		return;

	for (i = 0; i < ARRAY_SIZE(zigzag); i++) {
		n = zigzag[i];
		qtable[n + 0] = ctrl->intra_quantiser_matrix[i];
		qtable[n + 64] = ctrl->non_intra_quantiser_matrix[i];
		qtable[n + 128] = ctrl->chroma_intra_quantiser_matrix[i];
		qtable[n + 192] = ctrl->chroma_non_intra_quantiser_matrix[i];
	}
}

/*
 * Tell whether a field picture is the second field of its frame, which
 * decides where its references are: the second field can predict from the
 * first one, already decoded into the same buffer. top_field_first can't
 * tell, it is always 0 in field pictures (ISO/IEC 13818-2, 6.3.10). The
 * second field is the field of opposite parity decoded right after the
 * first one, with the same OUTPUT timestamp.
 */
bool hantro_mpeg2_dec_second_field(struct hantro_ctx *ctx,
				   const struct v4l2_ctrl_mpeg2_picture *pic,
				   u64 timestamp)
{
	struct hantro_mpeg2_dec_hw_ctx *mpeg2 = &ctx->mpeg2_dec;
	bool second;

	if (pic->picture_structure == V4L2_MPEG2_PIC_FRAME) {
		mpeg2->field_structure = 0;
		return false;
	}

	second = mpeg2->field_structure &&
		 mpeg2->field_structure != pic->picture_structure &&
		 mpeg2->field_ts == timestamp;

	mpeg2->field_structure = second ? 0 : pic->picture_structure;
	mpeg2->field_ts = timestamp;

	return second;
}

int hantro_mpeg2_dec_init(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	ctx->mpeg2_dec.field_structure = 0;
	ctx->mpeg2_dec.qtable.size = ARRAY_SIZE(zigzag) * 4;
	ctx->mpeg2_dec.qtable.cpu =
		dma_alloc_coherent(vpu->dev,
				   ctx->mpeg2_dec.qtable.size,
				   &ctx->mpeg2_dec.qtable.dma,
				   GFP_KERNEL);
	if (!ctx->mpeg2_dec.qtable.cpu)
		return -ENOMEM;
	return 0;
}

void hantro_mpeg2_dec_exit(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	dma_free_coherent(vpu->dev,
			  ctx->mpeg2_dec.qtable.size,
			  ctx->mpeg2_dec.qtable.cpu,
			  ctx->mpeg2_dec.qtable.dma);
}
