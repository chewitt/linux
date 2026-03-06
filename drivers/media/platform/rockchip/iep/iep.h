/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip Image Enhancement Processor (IEP) driver
 *
 * Copyright (C) 2020 Alex Bee <knaerzche@gmail.com>
 *
 */

#ifndef __IEP_H__
#define __IEP_H__

#include "iep-common.h"

#define IEP_NAME "rockchip-iep"

/* Timeout in ns */
#define IEP_TIMEOUT 250000

struct rockchip_iep {
	struct rk_iep_dev base;

	struct clk *axi_clk;
	struct clk *ahb_clk;
};

static inline void iep_write(struct rockchip_iep *iep, u32 reg, u32 value)
{
	writel(value, iep->base.regs + reg);
};

static inline u32 iep_read(struct rockchip_iep *iep, u32 reg)
{
	return readl(iep->base.regs + reg);
};

static inline void iep_shadow_mod(struct rockchip_iep *iep, u32 reg,
				  u32 shadow_reg, u32 mask, u32 val)
{
	u32 temp = iep_read(iep, shadow_reg) & ~(mask);

	temp |= val & mask;
	iep_write(iep, reg, temp);
};

static inline void iep_mod(struct rockchip_iep *iep, u32 reg, u32 mask, u32 val)
{
	iep_shadow_mod(iep, reg, reg, mask, val);
};

#endif
