/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Common definitions for MaxLinear MxL603/MxL608 tuner drivers
 *
 * Copyright (C) 2014 Sasa Savic <sasa.savic.sr@gmail.com>
 * Copyright (C) 2018 Igor Mokrushin <mcmcc@mail.ru>
 */

#ifndef __MXL60X_COMMON_H__
#define __MXL60X_COMMON_H__

#include <media/dvb_frontend.h>

enum mxl60x_chip {
	MXL60X_603,
	MXL60X_608,
};

enum mxl60x_if_freq {
	MXL60X_IF_3_65MHz,
	MXL60X_IF_4MHz,
	MXL60X_IF_4_1MHz,
	MXL60X_IF_4_15MHz,
	MXL60X_IF_4_5MHz,
	MXL60X_IF_4_57MHz,
	MXL60X_IF_5MHz,
	MXL60X_IF_5_38MHz,
	MXL60X_IF_6MHz,
	MXL60X_IF_6_28MHz,
	MXL60X_IF_7_2MHz,
	MXL60X_IF_8_25MHz,
	MXL60X_IF_35_25MHz,
	MXL60X_IF_36MHz,
	MXL60X_IF_36_15MHz,
	MXL60X_IF_36_65MHz,
	MXL60X_IF_44MHz,
};

enum mxl60x_xtal_freq {
	MXL60X_XTAL_16MHz,
	MXL60X_XTAL_24MHz,
};

enum mxl60x_agc {
	MXL60X_AGC_SELF,
	MXL60X_AGC_EXTERNAL,
};

struct mxl60x_config {
	enum mxl60x_xtal_freq xtal_freq_hz;
	enum mxl60x_if_freq if_freq_hz;
	enum mxl60x_agc agc_type;

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

struct dvb_frontend *mxl60x_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c, u8 addr,
				   struct mxl60x_config *cfg,
				   enum mxl60x_chip chip);

#endif /* __MXL60X_COMMON_H__ */
