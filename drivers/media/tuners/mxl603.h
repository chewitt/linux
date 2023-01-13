// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the MaxLinear MxL603 tuner
 *
 * Copyright (C) 2014 Sasa Savic <sasa.savic.sr@gmail.com>
 *
 */

#ifndef __MXL603_H__
#define __MXL603_H__

#include <linux/dvb/version.h>
#include <media/dvb_frontend.h>

enum mxl603_if_freq {
	MXL603_IF_3_65MHz,
	MXL603_IF_4MHz,
	MXL603_IF_4_1MHz,
	MXL603_IF_4_15MHz,
	MXL603_IF_4_5MHz,
	MXL603_IF_4_57MHz,
	MXL603_IF_5MHz,
	MXL603_IF_5_38MHz,
	MXL603_IF_6MHz,
	MXL603_IF_6_28MHz,
	MXL603_IF_7_2MHz,
	MXL603_IF_8_25MHz,
	MXL603_IF_35_25MHz,
	MXL603_IF_36MHz,
	MXL603_IF_36_15MHz,
	MXL603_IF_36_65MHz,
	MXL603_IF_44MHz,
};

enum mxl603_xtal_freq {
	MXL603_XTAL_16MHz,
	MXL603_XTAL_24MHz,
};

enum mxl603_agc {
	MXL603_AGC_SELF,
	MXL603_AGC_EXTERNAL,
};

struct mxl603_config {
	enum mxl603_xtal_freq xtal_freq_hz;
	enum mxl603_if_freq if_freq_hz;
	enum mxl603_agc agc_type;

	u8 xtal_cap;
	u8 gain_level;
	u8 if_out_gain_level;
	u8 agc_set_point;
	u8 agc_invert_pol;
	u8 invert_if;
	u8 loop_thru_enable;
	u8 clk_out_enable;
	u8 clk_out_div;
	u8 clk_out_ext;
	u8 xtal_sharing_mode;
	u8 single_supply_3_3V;
};

extern struct dvb_frontend *mxl603_attach(struct dvb_frontend *fe,
					  struct i2c_adapter *i2c, u8 addr,
					  struct mxl603_config *cfg);

#endif /* __MXL603_H__ */
