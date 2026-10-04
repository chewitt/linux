// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Amlogic S4 PLL Clock Controller Driver
 *
 * Copyright (c) 2022-2023 Amlogic, inc. All rights reserved
 * Author: Yu Tu <yu.tu@amlogic.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/firmware/meson/meson_sm.h>
#include <linux/iopoll.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mpll.h"
#include "clk-pll.h"
#include "clk-regmap.h"
#include "meson-clkc-utils.h"
#include <dt-bindings/clock/amlogic,s4-pll-clkc.h>

#define ANACTRL_SYSPLL_CTRL0                       0x000
#define ANACTRL_FIXPLL_CTRL0                       0x040
#define ANACTRL_FIXPLL_CTRL1                       0x044
#define ANACTRL_FIXPLL_CTRL3                       0x04c
#define ANACTRL_GP0PLL_CTRL0                       0x080
#define ANACTRL_GP0PLL_CTRL1                       0x084
#define ANACTRL_GP0PLL_CTRL2                       0x088
#define ANACTRL_GP0PLL_CTRL3                       0x08c
#define ANACTRL_GP0PLL_CTRL4                       0x090
#define ANACTRL_GP0PLL_CTRL5                       0x094
#define ANACTRL_GP0PLL_CTRL6                       0x098
#define ANACTRL_HIFIPLL_CTRL0                      0x100
#define ANACTRL_HIFIPLL_CTRL1                      0x104
#define ANACTRL_HIFIPLL_CTRL2                      0x108
#define ANACTRL_HIFIPLL_CTRL3                      0x10c
#define ANACTRL_HIFIPLL_CTRL4                      0x110
#define ANACTRL_HIFIPLL_CTRL5                      0x114
#define ANACTRL_HIFIPLL_CTRL6                      0x118
#define ANACTRL_MPLL_CTRL0                         0x180
#define ANACTRL_MPLL_CTRL1                         0x184
#define ANACTRL_MPLL_CTRL2                         0x188
#define ANACTRL_MPLL_CTRL3                         0x18c
#define ANACTRL_MPLL_CTRL4                         0x190
#define ANACTRL_MPLL_CTRL5                         0x194
#define ANACTRL_MPLL_CTRL6                         0x198
#define ANACTRL_MPLL_CTRL7                         0x19c
#define ANACTRL_MPLL_CTRL8                         0x1a0
#define ANACTRL_HDMIPLL_CTRL0                      0x1c0

/*
 * These clock are a fixed value (fixed_pll is 2GHz) that is initialized by ROMcode.
 * The chip was changed fixed pll for security reasons. Fixed PLL registers are not writable
 * in the kernel phase. Write of fixed PLL-related register will cause the system to crash.
 * Meanwhile, these clock won't ever change at runtime.
 * For the above reasons, we can only use ro_ops for fixed PLL related clocks.
 */
static struct clk_regmap s4_fixed_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.frac = {
			.reg_off = ANACTRL_FIXPLL_CTRL1,
			.shift   = 0,
			.width   = 17,
		},
		.n = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.l = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fixed_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll_dco.hw
		},
		.num_parents = 1,
		/*
		 * This clock won't ever change at runtime so
		 * CLK_SET_RATE_PARENT is not required
		 */
	},
};

static struct clk_fixed_factor s4_fclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div2_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div3_div = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 20,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div3_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 21,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div4_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div5_div = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 22,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div5_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div7_div = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div7 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 23,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div7_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div2p5_div = {
	.mult = 2,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2p5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div2p5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2p5",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div2p5_div.hw
		},
		.num_parents = 1,
	},
};

static const struct pll_mult_range s4_gp0_pll_mult_range = {
	.min = 125,
	.max = 250,
};

/*
 * Internal gp0 pll emulation configuration parameters
 */
static const struct reg_sequence s4_gp0_pll_init_regs[] = {
	{ .reg = ANACTRL_GP0PLL_CTRL1,	.def = 0x00000000 },
	{ .reg = ANACTRL_GP0PLL_CTRL2,	.def = 0x00000000 },
	{ .reg = ANACTRL_GP0PLL_CTRL3,	.def = 0x48681c00 },
	{ .reg = ANACTRL_GP0PLL_CTRL4,	.def = 0x88770290 },
	{ .reg = ANACTRL_GP0PLL_CTRL5,	.def = 0x39272000 },
	{ .reg = ANACTRL_GP0PLL_CTRL6,	.def = 0x56540000 }
};

static struct clk_regmap s4_gp0_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.l = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.range = &s4_gp0_pll_mult_range,
		.init_regs = s4_gp0_pll_init_regs,
		.init_count = ARRAY_SIZE(s4_gp0_pll_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_gp0_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_GP0PLL_CTRL0,
		.shift = 16,
		.width = 3,
		.flags = (CLK_DIVIDER_POWER_OF_TWO |
			  CLK_DIVIDER_ROUND_CLOSEST),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_gp0_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * Internal hifi pll emulation configuration parameters
 */
static const struct reg_sequence s4_hifi_pll_init_regs[] = {
	{ .reg = ANACTRL_HIFIPLL_CTRL2,	.def = 0x00000000 },
	{ .reg = ANACTRL_HIFIPLL_CTRL3,	.def = 0x6a285c00 },
	{ .reg = ANACTRL_HIFIPLL_CTRL4,	.def = 0x65771290 },
	{ .reg = ANACTRL_HIFIPLL_CTRL5,	.def = 0x39272000 },
	{ .reg = ANACTRL_HIFIPLL_CTRL6,	.def = 0x56540000 }
};

static struct clk_regmap s4_hifi_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.frac = {
			.reg_off = ANACTRL_HIFIPLL_CTRL1,
			.shift   = 0,
			.width   = 17,
		},
		.l = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.range = &s4_gp0_pll_mult_range,
		.init_regs = s4_hifi_pll_init_regs,
		.init_count = ARRAY_SIZE(s4_hifi_pll_init_regs),
		.frac_max = 100000,
		.flags = CLK_MESON_PLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hifi_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_hifi_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_HIFIPLL_CTRL0,
		.shift = 16,
		.width = 2,
		.flags = (CLK_DIVIDER_POWER_OF_TWO |
			  CLK_DIVIDER_ROUND_CLOSEST),
	},
	.hw.init = &(struct clk_init_data){
		.name = "hifi_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_hifi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap s4_hdmi_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.l = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.range = &s4_gp0_pll_mult_range,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_hdmi_pll_od = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_HDMIPLL_CTRL0,
		.shift = 16,
		.width = 4,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_od",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_hdmi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap s4_hdmi_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_HDMIPLL_CTRL0,
		.shift = 20,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_hdmi_pll_od.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor s4_mpll_50m_div = {
	.mult = 1,
	.div = 80,
	.hw.init = &(struct clk_init_data){
		.name = "mpll_50m_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll_50m = {
	.data = &(struct clk_regmap_mux_data){
		.offset = ANACTRL_FIXPLL_CTRL3,
		.mask = 0x1,
		.shift = 5,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll_50m",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &s4_mpll_50m_div.hw },
		},
		.num_parents = 2,
	},
};

static struct clk_fixed_factor s4_mpll_prediv = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "mpll_prediv",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static const struct reg_sequence s4_mpll0_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL2, .def = 0x40000033 }
};

static struct clk_regmap s4_mpll0_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 29,
			.width	 = 1,
		},
		.init_regs = s4_mpll0_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll0_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL1,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll0_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence s4_mpll1_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL4,	.def = 0x40000033 }
};

static struct clk_regmap s4_mpll1_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 29,
			.width	 = 1,
		},
		.init_regs = s4_mpll1_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll1_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL3,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll1_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence s4_mpll2_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL6, .def = 0x40000033 }
};

static struct clk_regmap s4_mpll2_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 29,
			.width	 = 1,
		},
		.init_regs = s4_mpll2_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll2_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL5,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll2_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence s4_mpll3_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL8, .def = 0x40000033 }
};

static struct clk_regmap s4_mpll3_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 29,
			.width	 = 1,
		},
		.init_regs = s4_mpll3_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll3_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll3_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL7,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll3",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll3_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * The sys_pll and CPU clock registers can only be written by the secure
 * firmware. BL31 exposes them through the SM_PLL_CLK and SM_CPU_CLK
 * services, which take one of these sub-functions as first argument.
 */
#define S4_SECID_SYS_DCO_PLL		0
#define S4_SECID_SYS_DCO_PLL_DIS	1
#define S4_SECID_SYS_PLL_OD		2
#define S4_SECID_CPU_CLK_SEL		6
#define S4_SECID_CPU_CLK_RD		7
#define S4_SECID_CPU_CLK_DYN		8

/* Layout of the CPU clock control register returned by S4_SECID_CPU_CLK_RD */
#define S4_CPU_CLK_FINAL_SEL		BIT(11)
#define S4_CPU_CLK_DYN_SEL		BIT(10)
#define S4_CPU_CLK_DYN1_SHIFT		16
#define S4_CPU_CLK_DYN_PREMUX		GENMASK(1, 0)
#define S4_CPU_CLK_DYN_POSTMUX		BIT(2)
#define S4_CPU_CLK_DYN_DIV		GENMASK(9, 4)

static struct meson_sm_firmware *s4_sm_fw;

static int s4_sm_call(unsigned int cmd, u32 *val, u32 secid,
		      u32 arg0, u32 arg1, u32 arg2)
{
	s32 ret;
	int err;

	err = meson_sm_call(s4_sm_fw, cmd, &ret, secid, arg0, arg1, arg2, 0);
	if (err)
		return err;

	if (val)
		*val = ret;

	return 0;
}

static int s4_sys_pll_dco_wait_lock(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = clk->data;
	unsigned int locked;

	return read_poll_timeout_atomic(meson_parm_read, locked, locked, 20,
					100000, false, clk->map, &pll->l);
}

static int s4_sys_pll_dco_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = clk->data;
	int ret;

	if (meson_clk_pll_ro_ops.is_enabled(hw))
		return 0;

	ret = s4_sm_call(SM_PLL_CLK, NULL, S4_SECID_SYS_DCO_PLL,
			 meson_parm_read(clk->map, &pll->m),
			 meson_parm_read(clk->map, &pll->n), 0);
	if (ret)
		return ret;

	return s4_sys_pll_dco_wait_lock(hw);
}

static void s4_sys_pll_dco_disable(struct clk_hw *hw)
{
	s4_sm_call(SM_PLL_CLK, NULL, S4_SECID_SYS_DCO_PLL_DIS, 0, 0, 0);
}

static int s4_sys_pll_dco_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = clk->data;
	const struct pll_params_table *p;
	int ret;

	for (p = pll->table; p->n; p++)
		if (DIV_ROUND_UP_ULL((u64)parent_rate * p->m, p->n) == rate)
			break;

	if (!p->n)
		return -EINVAL;

	if (meson_clk_pll_ro_ops.is_enabled(hw))
		s4_sys_pll_dco_disable(hw);

	ret = s4_sm_call(SM_PLL_CLK, NULL, S4_SECID_SYS_DCO_PLL,
			 p->m, p->n, 0);
	if (ret)
		return ret;

	return s4_sys_pll_dco_wait_lock(hw);
}

static unsigned long s4_sys_pll_dco_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	return meson_clk_pll_ro_ops.recalc_rate(hw, parent_rate);
}

static int s4_sys_pll_dco_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	return meson_clk_pll_ops.determine_rate(hw, req);
}

static int s4_sys_pll_dco_is_enabled(struct clk_hw *hw)
{
	return meson_clk_pll_ro_ops.is_enabled(hw);
}

static const struct clk_ops s4_sys_pll_dco_ops = {
	.init		= clk_regmap_init,
	.recalc_rate	= s4_sys_pll_dco_recalc_rate,
	.determine_rate	= s4_sys_pll_dco_determine_rate,
	.set_rate	= s4_sys_pll_dco_set_rate,
	.is_enabled	= s4_sys_pll_dco_is_enabled,
	.enable		= s4_sys_pll_dco_enable,
	.disable	= s4_sys_pll_dco_disable,
};

static int s4_sys_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_div_data *div = clk_get_regmap_div_data(clk);
	int val;

	val = divider_get_val(rate, parent_rate, div->table, div->width,
			      div->flags);
	if (val < 0)
		return val;

	return s4_sm_call(SM_PLL_CLK, NULL, S4_SECID_SYS_PLL_OD,
			  clk_div_mask(div->width) << div->shift,
			  val << div->shift, 0);
}

static unsigned long s4_sys_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	return clk_regmap_divider_ro_ops.recalc_rate(hw, parent_rate);
}

static int s4_sys_pll_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_div_data *div = clk_get_regmap_div_data(clk);

	return divider_determine_rate(hw, req, div->table, div->width,
				      div->flags);
}

static const struct clk_ops s4_sys_pll_ops = {
	.init		= clk_regmap_init,
	.recalc_rate	= s4_sys_pll_recalc_rate,
	.determine_rate	= s4_sys_pll_determine_rate,
	.set_rate	= s4_sys_pll_set_rate,
};

static const struct pll_params_table s4_sys_pll_params_table[] = {
	PLL_PARAMS(168, 1), /* DCO = 4032M */
	PLL_PARAMS(184, 1), /* DCO = 4416M */
	PLL_PARAMS(200, 1), /* DCO = 4800M */
	PLL_PARAMS(216, 1), /* DCO = 5184M */
	PLL_PARAMS(233, 1), /* DCO = 5592M */
	PLL_PARAMS(234, 1), /* DCO = 5616M */
	PLL_PARAMS(249, 1), /* DCO = 5976M */
	PLL_PARAMS(125, 1), /* DCO = 3000M */
	PLL_PARAMS(126, 1), /* DCO = 3024M */
	PLL_PARAMS(134, 1), /* DCO = 3216M */
	PLL_PARAMS(142, 1), /* DCO = 3408M */
	PLL_PARAMS(150, 1), /* DCO = 3600M */
	PLL_PARAMS(158, 1), /* DCO = 3792M */
	PLL_PARAMS(159, 1), /* DCO = 3816M */
	PLL_PARAMS(160, 1), /* DCO = 3840M */
	PLL_PARAMS(167, 1), /* DCO = 4008M */
	{ /* sentinel */ }
};

static struct clk_regmap s4_sys_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_SYSPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_SYSPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_SYSPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.l = {
			.reg_off = ANACTRL_SYSPLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_SYSPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.table = s4_sys_pll_params_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll_dco",
		.ops = &s4_sys_pll_dco_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
		/* This clock feeds the CPU, avoid disabling it */
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_regmap s4_sys_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_SYSPLL_CTRL0,
		.shift = 16,
		.width = 3,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll",
		.ops = &s4_sys_pll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_sys_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

struct s4_cpu_dyn_param {
	u8 premux;
	u8 postmux;
	u8 div;
};

/* Settings used by the vendor firmware for the low CPU frequencies */
static const struct s4_cpu_dyn_param s4_cpu_dyn_params[] = {
	{ .premux = 0, .postmux = 0, .div = 0 },	/* 24M */
	{ .premux = 1, .postmux = 1, .div = 9 },	/* 100M */
	{ .premux = 1, .postmux = 1, .div = 3 },	/* 250M */
	{ .premux = 2, .postmux = 1, .div = 1 },	/* 333M */
	{ .premux = 1, .postmux = 1, .div = 1 },	/* 500M */
	{ .premux = 2, .postmux = 0, .div = 0 },	/* 666M */
	{ .premux = 1, .postmux = 0, .div = 0 },	/* 1G */
};

static u32 s4_cpu_clk_read(void)
{
	u32 val = 0;

	s4_sm_call(SM_CPU_CLK, &val, S4_SECID_CPU_CLK_RD, 0, 0, 0);

	return val;
}

/* Settings of the dynamic clock path currently feeding the CPU */
static u32 s4_cpu_dyn_active(void)
{
	u32 val = s4_cpu_clk_read();

	if (val & S4_CPU_CLK_DYN_SEL)
		val >>= S4_CPU_CLK_DYN1_SHIFT;

	return val;
}

static unsigned long s4_cpu_dyn_param_rate(const struct s4_cpu_dyn_param *p,
					   unsigned long parent_rate)
{
	if (!p->postmux)
		return parent_rate;

	return DIV_ROUND_UP_ULL((u64)parent_rate, p->div + 1);
}

static const struct s4_cpu_dyn_param *
s4_cpu_dyn_find(struct clk_hw *hw, unsigned long rate, u8 index)
{
	struct clk_hw *parent = clk_hw_get_parent_by_index(hw, index);
	unsigned long parent_rate = clk_hw_get_rate(parent);
	int i;

	for (i = 0; i < ARRAY_SIZE(s4_cpu_dyn_params); i++) {
		const struct s4_cpu_dyn_param *p = &s4_cpu_dyn_params[i];

		if (p->premux == index &&
		    s4_cpu_dyn_param_rate(p, parent_rate) == rate)
			return p;
	}

	return NULL;
}

static unsigned long s4_cpu_dyn_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	u32 val = s4_cpu_dyn_active();
	struct s4_cpu_dyn_param p = {
		.postmux = FIELD_GET(S4_CPU_CLK_DYN_POSTMUX, val),
		.div = FIELD_GET(S4_CPU_CLK_DYN_DIV, val),
	};

	return s4_cpu_dyn_param_rate(&p, parent_rate);
}

static u8 s4_cpu_dyn_get_parent(struct clk_hw *hw)
{
	return FIELD_GET(S4_CPU_CLK_DYN_PREMUX, s4_cpu_dyn_active());
}

static int s4_cpu_dyn_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	const struct s4_cpu_dyn_param *best = NULL;
	unsigned long best_rate = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(s4_cpu_dyn_params); i++) {
		const struct s4_cpu_dyn_param *p = &s4_cpu_dyn_params[i];
		struct clk_hw *parent = clk_hw_get_parent_by_index(hw, p->premux);
		unsigned long rate;

		if (!parent)
			continue;

		rate = s4_cpu_dyn_param_rate(p, clk_hw_get_rate(parent));
		if (!best ||
		    abs_diff(rate, req->rate) < abs_diff(best_rate, req->rate)) {
			best = p;
			best_rate = rate;
		}
	}

	if (!best)
		return -EINVAL;

	req->best_parent_hw = clk_hw_get_parent_by_index(hw, best->premux);
	req->best_parent_rate = clk_hw_get_rate(req->best_parent_hw);
	req->rate = best_rate;

	return 0;
}

static int s4_cpu_dyn_set_rate_and_parent(struct clk_hw *hw,
					  unsigned long rate,
					  unsigned long parent_rate, u8 index)
{
	const struct s4_cpu_dyn_param *p = s4_cpu_dyn_find(hw, rate, index);

	if (!p)
		return -EINVAL;

	return s4_sm_call(SM_CPU_CLK, NULL, S4_SECID_CPU_CLK_DYN,
			  p->premux, p->postmux, p->div);
}

static int s4_cpu_dyn_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	return s4_cpu_dyn_set_rate_and_parent(hw, rate, parent_rate,
					      s4_cpu_dyn_get_parent(hw));
}

static int s4_cpu_dyn_set_parent(struct clk_hw *hw, u8 index)
{
	u32 val = s4_cpu_dyn_active();

	return s4_sm_call(SM_CPU_CLK, NULL, S4_SECID_CPU_CLK_DYN, index,
			  FIELD_GET(S4_CPU_CLK_DYN_POSTMUX, val),
			  FIELD_GET(S4_CPU_CLK_DYN_DIV, val));
}

static const struct clk_ops s4_cpu_dyn_ops = {
	.recalc_rate		= s4_cpu_dyn_recalc_rate,
	.determine_rate		= s4_cpu_dyn_determine_rate,
	.set_rate		= s4_cpu_dyn_set_rate,
	.get_parent		= s4_cpu_dyn_get_parent,
	.set_parent		= s4_cpu_dyn_set_parent,
	.set_rate_and_parent	= s4_cpu_dyn_set_rate_and_parent,
};

static struct clk_hw s4_cpu_dyn_clk = {
	.init = &(struct clk_init_data){
		.name = "cpu_dyn_clk",
		.ops = &s4_cpu_dyn_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &s4_fclk_div2.hw },
			{ .hw = &s4_fclk_div3.hw },
		},
		.num_parents = 3,
	},
};

static u8 s4_cpu_clk_get_parent(struct clk_hw *hw)
{
	return !!(s4_cpu_clk_read() & S4_CPU_CLK_FINAL_SEL);
}

static int s4_cpu_clk_set_parent(struct clk_hw *hw, u8 index)
{
	return s4_sm_call(SM_CPU_CLK, NULL, S4_SECID_CPU_CLK_SEL,
			  S4_CPU_CLK_FINAL_SEL,
			  index ? S4_CPU_CLK_FINAL_SEL : 0, 0);
}

static int s4_cpu_clk_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, CLK_MUX_ROUND_CLOSEST);
}

static const struct clk_ops s4_cpu_clk_ops = {
	.get_parent	= s4_cpu_clk_get_parent,
	.set_parent	= s4_cpu_clk_set_parent,
	.determine_rate	= s4_cpu_clk_determine_rate,
};

static struct clk_hw s4_cpu_clk = {
	.init = &(struct clk_init_data){
		.name = "cpu_clk",
		.ops = &s4_cpu_clk_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_cpu_dyn_clk,
			&s4_sys_pll.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static int s4_sys_pll_notifier_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	switch (event) {
	case PRE_RATE_CHANGE:
		/* Run the CPU from the dynamic clock while sys_pll relocks */
		clk_hw_set_parent(&s4_cpu_clk, &s4_cpu_dyn_clk);
		udelay(100);
		return NOTIFY_OK;

	case POST_RATE_CHANGE:
		clk_hw_set_parent(&s4_cpu_clk, &s4_sys_pll.hw);
		udelay(100);
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block s4_sys_pll_nb = {
	.notifier_call = s4_sys_pll_notifier_cb,
};

/* Array of all clocks provided by this provider */
static struct clk_hw *s4_pll_hw_clks[] = {
	[CLKID_FIXED_PLL_DCO]		= &s4_fixed_pll_dco.hw,
	[CLKID_FIXED_PLL]		= &s4_fixed_pll.hw,
	[CLKID_FCLK_DIV2_DIV]		= &s4_fclk_div2_div.hw,
	[CLKID_FCLK_DIV2]		= &s4_fclk_div2.hw,
	[CLKID_FCLK_DIV3_DIV]		= &s4_fclk_div3_div.hw,
	[CLKID_FCLK_DIV3]		= &s4_fclk_div3.hw,
	[CLKID_FCLK_DIV4_DIV]		= &s4_fclk_div4_div.hw,
	[CLKID_FCLK_DIV4]		= &s4_fclk_div4.hw,
	[CLKID_FCLK_DIV5_DIV]		= &s4_fclk_div5_div.hw,
	[CLKID_FCLK_DIV5]		= &s4_fclk_div5.hw,
	[CLKID_FCLK_DIV7_DIV]		= &s4_fclk_div7_div.hw,
	[CLKID_FCLK_DIV7]		= &s4_fclk_div7.hw,
	[CLKID_FCLK_DIV2P5_DIV]		= &s4_fclk_div2p5_div.hw,
	[CLKID_FCLK_DIV2P5]		= &s4_fclk_div2p5.hw,
	[CLKID_GP0_PLL_DCO]		= &s4_gp0_pll_dco.hw,
	[CLKID_GP0_PLL]			= &s4_gp0_pll.hw,
	[CLKID_HIFI_PLL_DCO]		= &s4_hifi_pll_dco.hw,
	[CLKID_HIFI_PLL]		= &s4_hifi_pll.hw,
	[CLKID_HDMI_PLL_DCO]		= &s4_hdmi_pll_dco.hw,
	[CLKID_HDMI_PLL_OD]		= &s4_hdmi_pll_od.hw,
	[CLKID_HDMI_PLL]		= &s4_hdmi_pll.hw,
	[CLKID_MPLL_50M_DIV]		= &s4_mpll_50m_div.hw,
	[CLKID_MPLL_50M]		= &s4_mpll_50m.hw,
	[CLKID_MPLL_PREDIV]		= &s4_mpll_prediv.hw,
	[CLKID_MPLL0_DIV]		= &s4_mpll0_div.hw,
	[CLKID_MPLL0]			= &s4_mpll0.hw,
	[CLKID_MPLL1_DIV]		= &s4_mpll1_div.hw,
	[CLKID_MPLL1]			= &s4_mpll1.hw,
	[CLKID_MPLL2_DIV]		= &s4_mpll2_div.hw,
	[CLKID_MPLL2]			= &s4_mpll2.hw,
	[CLKID_MPLL3_DIV]		= &s4_mpll3_div.hw,
	[CLKID_MPLL3]			= &s4_mpll3.hw,
	[CLKID_SYS_PLL_DCO]		= &s4_sys_pll_dco.hw,
	[CLKID_SYS_PLL]			= &s4_sys_pll.hw,
	[CLKID_CPU_DYN_CLK]		= &s4_cpu_dyn_clk,
	[CLKID_CPU_CLK]			= &s4_cpu_clk,
};

static const struct reg_sequence s4_pll_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL0,	.def = 0x00000543 },
};

static const struct meson_clkc_data s4_pll_clkc_data = {
	.hw_clks = {
		.hws = s4_pll_hw_clks,
		.num = ARRAY_SIZE(s4_pll_hw_clks),
	},
	.init_regs = s4_pll_init_regs,
	.init_count = ARRAY_SIZE(s4_pll_init_regs),
};

static const struct of_device_id s4_pll_clkc_match_table[] = {
	{
		.compatible = "amlogic,s4-pll-clkc",
		.data = &s4_pll_clkc_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, s4_pll_clkc_match_table);

static int s4_pll_clkc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *sm_np;
	struct clk *clk;
	int ret;

	sm_np = of_parse_phandle(dev->of_node, "amlogic,secure-monitor", 0);
	if (sm_np) {
		s4_sm_fw = meson_sm_get(sm_np);
		of_node_put(sm_np);
		if (!s4_sm_fw)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "secure monitor not ready\n");
	} else {
		s4_pll_hw_clks[CLKID_SYS_PLL_DCO] = NULL;
		s4_pll_hw_clks[CLKID_SYS_PLL] = NULL;
		s4_pll_hw_clks[CLKID_CPU_DYN_CLK] = NULL;
		s4_pll_hw_clks[CLKID_CPU_CLK] = NULL;
	}

	ret = meson_clkc_mmio_probe(pdev);
	if (ret || !s4_sm_fw)
		return ret;

	clk = devm_clk_hw_get_clk(dev, &s4_sys_pll.hw, "dvfs");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = devm_clk_notifier_register(dev, clk, &s4_sys_pll_nb);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register the sys_pll notifier\n");

	clk = devm_clk_hw_get_clk(dev, &s4_cpu_dyn_clk, "dvfs");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_set_rate(clk, 1000000000);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to set the cpu_dyn_clk rate\n");

	return 0;
}

static struct platform_driver s4_pll_clkc_driver = {
	.probe		= s4_pll_clkc_probe,
	.driver		= {
		.name	= "s4-pll-clkc",
		.of_match_table = s4_pll_clkc_match_table,
	},
};
module_platform_driver(s4_pll_clkc_driver);

MODULE_DESCRIPTION("Amlogic S4 PLL Clock Controller driver");
MODULE_AUTHOR("Yu Tu <yu.tu@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
