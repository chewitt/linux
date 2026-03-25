// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MaxLinear MxL608 Tuner Driver
 *
 * Copyright (C) 2019 Igor Mokrushin <mcmcc@mail.ru>
 *
 */

#ifndef __MXL608_H__
#define __MXL608_H__

#include <linux/dvb/version.h>
#include <media/dvb_frontend.h>

enum mxl608_if_freq {
	MXL608_IF_3_65MHz,
	MXL608_IF_4MHz,
	MXL608_IF_4_1MHz,
	MXL608_IF_4_15MHz,
	MXL608_IF_4_5MHz,
	MXL608_IF_4_57MHz,
	MXL608_IF_5MHz,
	MXL608_IF_5_38MHz,
	MXL608_IF_6MHz,
	MXL608_IF_6_28MHz,
	MXL608_IF_7_2MHz,
	MXL608_IF_8_25MHz,
	MXL608_IF_35_25MHz,
	MXL608_IF_36MHz,
	MXL608_IF_36_15MHz,
	MXL608_IF_36_65MHz,
	MXL608_IF_44MHz,
};

enum mxl608_xtal_freq {
	MXL608_XTAL_16MHz,
	MXL608_XTAL_24MHz,
};

enum mxl608_agc {
	MXL608_AGC_SELF,
	MXL608_AGC_EXTERNAL,
};

struct mxl608_config {
	enum mxl608_xtal_freq xtal_freq_hz;
	enum mxl608_if_freq if_freq_hz;
	enum mxl608_agc agc_type;

	u8 i2c_address; /* i2c addr = 0x60 */
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
	u8 no_xtal_cfg;
};

extern struct dvb_frontend *mxl608_attach(struct dvb_frontend *fe,
					  struct mxl608_config *cfg,
					  struct i2c_adapter *i2c);

#endif /* __MXL608_H__ */
