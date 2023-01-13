// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MaxLinear MxL603 Tuner Driver
 *
 * Copyright (C) 2014 Sasa Savic <sasa.savic.sr@gmail.com>
 */

#include <linux/module.h>
#include "mxl603.h"
#include "mxl60x_common.h"

struct dvb_frontend *mxl603_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c, u8 addr,
				   struct mxl603_config *cfg)
{
	struct mxl60x_config common = {
		.xtal_freq_hz      = (enum mxl60x_xtal_freq)cfg->xtal_freq_hz,
		.if_freq_hz        = (enum mxl60x_if_freq)cfg->if_freq_hz,
		.agc_type          = (enum mxl60x_agc)cfg->agc_type,
		.xtal_cap          = cfg->xtal_cap,
		.gain_level        = cfg->gain_level,
		.if_out_gain_level = cfg->if_out_gain_level,
		.agc_set_point     = cfg->agc_set_point,
		.agc_invert_pol    = cfg->agc_invert_pol,
		.invert_if         = cfg->invert_if,
		.loop_thru_enable  = cfg->loop_thru_enable,
		.clk_out_enable    = cfg->clk_out_enable,
		.clk_out_div       = cfg->clk_out_div,
		.clk_out_ext       = cfg->clk_out_ext,
		.xtal_sharing_mode = cfg->xtal_sharing_mode,
		.single_supply_3_3V = cfg->single_supply_3_3V,
	};

	return mxl60x_attach(fe, i2c, addr, &common, MXL60X_603);
}
EXPORT_SYMBOL_GPL(mxl603_attach);

MODULE_DESCRIPTION("MaxLinear MxL603 Tuner Driver");
MODULE_AUTHOR("Sasa Savic <sasa.savic.sr@gmail.com>");
MODULE_LICENSE("GPL");
