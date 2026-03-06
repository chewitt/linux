/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip Image Enhancement Processor v2 (IEP2) driver
 *
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 *
 */

#ifndef __IEP2_H__
#define __IEP2_H__

#include "iep-common.h"

#define IEP2_NAME "rockchip-iep2"

struct rockchip_iep2 {
	struct rk_iep_dev base;

	struct clk *aclk;
	struct clk *hclk;
	struct clk *sclk;

	/* DMA working buffers */
	dma_addr_t mv_dma;
	void *mv_buf;
	dma_addr_t md_dma;
	void *md_buf;
};

static inline void iep2_write(struct rockchip_iep2 *iep2, u32 reg, u32 value)
{
	writel(value, iep2->base.regs + reg);
}

static inline u32 iep2_read(struct rockchip_iep2 *iep2, u32 reg)
{
	return readl(iep2->base.regs + reg);
}

#endif
