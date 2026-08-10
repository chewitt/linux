/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip video decoder Rows and Cols Buffers manager
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include <linux/types.h>

struct rkvdec_dev;
struct rkvdec_ctx;
struct rkvdec_core;

enum rcb_axis {
	PIC_WIDTH = 0,
	PIC_HEIGHT = 1
};

struct rcb_size_info {
	u8 multiplier;
	enum rcb_axis axis;
};

int rkvdec_allocate_rcb(struct rkvdec_dev *rkvdec, struct rkvdec_core *core,
			u32 width, u32 height,
			const struct rcb_size_info *size_info,
			size_t rcb_count);
dma_addr_t rkvdec_rcb_buf_dma_addr(struct rkvdec_ctx *ctx, int id);
size_t rkvdec_rcb_buf_size(struct rkvdec_ctx *ctx, int id);
int rkvdec_rcb_buf_count(struct rkvdec_ctx *ctx);
bool rkvdec_rcb_buf_validate_size(struct rkvdec_ctx *ctx);
void rkvdec_free_rcb(struct rkvdec_dev *rkvdec, struct rkvdec_core *core);
