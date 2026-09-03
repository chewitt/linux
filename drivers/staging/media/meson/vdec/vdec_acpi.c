// SPDX-License-Identifier: GPL-2.0+
/*
 * ACPI (PRP0001) support for the Amlogic G12 video decoder: a local
 * clock island over the HHI window.
 *
 * Under ACPI there is no clock provider - the main G12 clock
 * controller is never ported; firmware pre-configures fixed state and
 * each driver models exactly the clocks it owns.  The decoder needs
 * five: the dos / dos_parser bus gates (HHI_GCLK_MPEG0/1) and the
 * vdec_1 / vdec_hevc / vdec_hevcf composite clocks (mux + divider +
 * gate in HHI_VDEC_CLK_CNTL / HHI_VDEC2_CLK_CNTL).
 *
 * Only the always-running fixed fclk_div fixed_pll children are
 * modelled as mux parents.  In DT the same mux carries
 * CLK_SET_RATE_PARENT down to the HIFI/GP0 PLL DCOs and the CCF has
 * been observed parking vdec_1 on HIFI_PLL; here a rate request can
 * pick only a fixed parent (666666666 lands exactly on fclk_div3),
 * so PLL escape is impossible by construction.
 *
 * Register ownership: this island owns HHI 0x1e0/0x1e4 and RMWs one
 * bit each in GCLK_MPEG0/1 under its own spinlock - see the firmware
 * repo's HHI register ownership table before adding any overlap.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>

#include "vdec.h"

#define HHI_GCLK_MPEG0		0x140
#define HHI_GCLK_MPEG1		0x144
#define HHI_VDEC_CLK_CNTL	0x1e0
#define HHI_VDEC2_CLK_CNTL	0x1e4

static DEFINE_SPINLOCK(vdec_acpi_hhi_lock);

struct vdec_acpi_fixed {
	const char *name;
	unsigned long rate;
};

/* fixed_pll (2 GHz) post-dividers, mux order per g12a_vdec_parents */
static const struct vdec_acpi_fixed vdec_acpi_parents[] = {
	{ "vdec_acpi_fdiv2p5", 800000000 },
	{ "vdec_acpi_fdiv3",   666666666 },
	{ "vdec_acpi_fdiv4",   500000000 },
	{ "vdec_acpi_fdiv5",   400000000 },
	{ "vdec_acpi_fdiv7",   285714285 },
};

static struct clk *vdec_acpi_composite(struct device *dev,
				       void __iomem *reg,
				       const char *stem,
				       const char * const *parent_names,
				       u8 mux_shift, u8 div_shift,
				       u8 gate_bit)
{
	struct clk_hw *mux, *div, *gate;
	char name[32];

	snprintf(name, sizeof(name), "%s_sel", stem);
	mux = devm_clk_hw_register_mux(dev, name, parent_names,
				       ARRAY_SIZE(vdec_acpi_parents),
				       CLK_MUX_ROUND_CLOSEST,
				       reg, mux_shift, 0x7, 0,
				       &vdec_acpi_hhi_lock);
	if (IS_ERR(mux))
		return ERR_CAST(mux);

	snprintf(name, sizeof(name), "%s_div", stem);
	div = devm_clk_hw_register_divider(dev, name,
					   clk_hw_get_name(mux),
					   CLK_SET_RATE_PARENT,
					   reg, div_shift, 7,
					   CLK_DIVIDER_ROUND_CLOSEST,
					   &vdec_acpi_hhi_lock);
	if (IS_ERR(div))
		return ERR_CAST(div);

	gate = devm_clk_hw_register_gate(dev, stem, clk_hw_get_name(div),
					 CLK_SET_RATE_PARENT,
					 reg, gate_bit, 0,
					 &vdec_acpi_hhi_lock);
	if (IS_ERR(gate))
		return ERR_CAST(gate);

	return gate->clk;
}

int vdec_acpi_init_clks(struct amvdec_core *core, void __iomem *hhi)
{
	struct device *dev = core->dev;
	const char *parent_names[ARRAY_SIZE(vdec_acpi_parents)];
	struct clk_hw *hw;
	int i;

	for (i = 0; i < ARRAY_SIZE(vdec_acpi_parents); i++) {
		hw = devm_clk_hw_register_fixed_rate(dev,
					vdec_acpi_parents[i].name, NULL, 0,
					vdec_acpi_parents[i].rate);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
		parent_names[i] = vdec_acpi_parents[i].name;
	}

	hw = devm_clk_hw_register_gate(dev, "vdec_acpi_dos", NULL, 0,
				       hhi + HHI_GCLK_MPEG0, 1, 0,
				       &vdec_acpi_hhi_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	core->dos_clk = hw->clk;

	hw = devm_clk_hw_register_gate(dev, "vdec_acpi_dos_parser", NULL, 0,
				       hhi + HHI_GCLK_MPEG1, 25, 0,
				       &vdec_acpi_hhi_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	core->dos_parser_clk = hw->clk;

	core->vdec_1_clk = vdec_acpi_composite(dev, hhi + HHI_VDEC_CLK_CNTL,
					       "vdec_acpi_vdec_1",
					       parent_names, 9, 0, 8);
	if (IS_ERR(core->vdec_1_clk))
		return PTR_ERR(core->vdec_1_clk);

	core->vdec_hevcf_clk = vdec_acpi_composite(dev,
						   hhi + HHI_VDEC2_CLK_CNTL,
						   "vdec_acpi_vdec_hevcf",
						   parent_names, 9, 0, 8);
	if (IS_ERR(core->vdec_hevcf_clk))
		return PTR_ERR(core->vdec_hevcf_clk);

	core->vdec_hevc_clk = vdec_acpi_composite(dev,
						  hhi + HHI_VDEC2_CLK_CNTL,
						  "vdec_acpi_vdec_hevc",
						  parent_names, 25, 16, 24);
	if (IS_ERR(core->vdec_hevc_clk))
		return PTR_ERR(core->vdec_hevc_clk);

	return 0;
}