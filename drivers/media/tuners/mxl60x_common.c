// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common implementation for MaxLinear MxL603/MxL608 tuner drivers
 *
 * Copyright (C) 2014 Sasa Savic <sasa.savic.sr@gmail.com>
 * Copyright (C) 2018 Igor Mokrushin <mcmcc@mail.ru>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/types.h>
#include "mxl60x_common.h"

enum mxl60x_mode {
	MXL60X_MODE_CABLE,
	MXL60X_MODE_ISDBT_ATSC,
	MXL60X_MODE_DVBT,
};

enum mxl60x_bw_mhz {
	MXL60X_CABLE_BW_6MHZ = 0x00,
	MXL60X_CABLE_BW_7MHZ = 0x01,
	MXL60X_CABLE_BW_8MHZ = 0x02,
	MXL60X_TERR_BW_6MHZ  = 0x20,
	MXL60X_TERR_BW_7MHZ  = 0x21,
	MXL60X_TERR_BW_8MHZ  = 0x22,
};

struct reg_pair_t {
	u8 reg;
	u8 val;
};

struct freq_table {
	u32 center_freq;
	u8 reg1;
	u8 reg2;
};

struct mxl60x_state {
	struct mxl60x_config config;
	struct i2c_adapter *i2c;
	u8 addr;
	u32 frequency;
	u32 bandwidth;
	enum mxl60x_chip chip;
};

static struct freq_table mxl60x_cable_freq[] = {
	{ 1, 0x00, 0xD8 },
	{ 695000000, 0x20, 0xD7 },
	{ 0, 0, 0 },
};

static struct freq_table mxl60x_digital_freq[] = {
	{ 1, 0x00, 0xD8 },
	{ 0, 0, 0 },
};

/*
 * Register 0xD5 differs between MxL603 (0x05) and MxL608 (0x04).
 * The table uses the MxL608 value; MxL603 overrides after loading.
 */
static struct reg_pair_t mxl60x_digital_dvbc[] = {
	{ 0x0C, 0x00 }, { 0x13, 0x04 }, { 0x53, 0x7E }, { 0x57, 0x91 },
	{ 0x5C, 0xB1 }, { 0x62, 0xF2 }, { 0x6E, 0x03 }, { 0x6F, 0xD1 },
	{ 0x87, 0x77 }, { 0x88, 0x55 }, { 0x93, 0x33 }, { 0x97, 0x03 },
	{ 0xBA, 0x40 }, { 0x98, 0xAF }, { 0x9B, 0x20 }, { 0x9C, 0x1E },
	{ 0xA0, 0x18 }, { 0xA5, 0x09 }, { 0xC2, 0xA9 }, { 0xC5, 0x7C },
	{ 0xCD, 0x64 }, { 0xCE, 0x7C }, { 0xD5, 0x04 }, { 0xD9, 0x00 },
	{ 0xEA, 0x00 }, { 0xDC, 0x1C }, { 0, 0 }
};

static struct reg_pair_t mxl60x_digital_isdbt_atsc[] = {
	{ 0x0C, 0x00 }, { 0x13, 0x04 }, { 0x53, 0xFE }, { 0x57, 0x91 },
	{ 0x62, 0xC2 }, { 0x6E, 0x01 }, { 0x6F, 0x51 }, { 0x87, 0x77 },
	{ 0x88, 0x55 }, { 0x93, 0x22 }, { 0x97, 0x02 }, { 0xBA, 0x30 },
	{ 0x98, 0xAF }, { 0x9B, 0x20 }, { 0x9C, 0x1E }, { 0xA0, 0x18 },
	{ 0xA5, 0x09 }, { 0xC2, 0xA9 }, { 0xC5, 0x7C }, { 0xCD, 0xEB },
	{ 0xCE, 0x7F }, { 0xD5, 0x03 }, { 0xD9, 0x04 }, { 0, 0 }
};

static struct reg_pair_t mxl60x_digital_dvbt[] = {
	{ 0x0C, 0x00 }, { 0x13, 0x04 }, { 0x53, 0xFE }, { 0x57, 0x91 },
	{ 0x62, 0xC2 }, { 0x6E, 0x01 }, { 0x6F, 0x51 }, { 0x87, 0x77 },
	{ 0x88, 0x55 }, { 0x93, 0x22 }, { 0x97, 0x02 }, { 0xBA, 0x30 },
	{ 0x98, 0xAF }, { 0x9B, 0x20 }, { 0x9C, 0x1E }, { 0xA0, 0x18 },
	{ 0xA5, 0x09 }, { 0xC2, 0xA9 }, { 0xC5, 0x7C }, { 0xCD, 0x64 },
	{ 0xCE, 0x7C }, { 0xD5, 0x03 }, { 0xD9, 0x04 }, { 0, 0 }
};

static const char *mxl60x_chip_name(enum mxl60x_chip chip)
{
	return chip == MXL60X_603 ? "MxL603" : "MxL608";
}

static int mxl60x_write_reg(struct mxl60x_state *state, u8 reg, u8 val)
{
	u8 buf[] = { reg, val };
	struct i2c_msg msg = {
		.addr = state->addr, .flags = 0, .buf = buf, .len = 2
	};
	int ret;

	ret = i2c_transfer(state->i2c, &msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&state->i2c->dev,
			 "i2c write failed = %d, reg = %02x\n", ret, reg);
		ret = -EREMOTEIO;
	}
	return ret;
}

static int mxl60x_write_regs(struct mxl60x_state *state,
			      struct reg_pair_t *reg_pair)
{
	unsigned int i = 0;
	int ret = 0;

	while ((ret == 0) && (reg_pair[i].reg || reg_pair[i].val)) {
		ret = mxl60x_write_reg(state, reg_pair[i].reg, reg_pair[i].val);
		i++;
	}
	return ret;
}

static int mxl60x_read_reg(struct mxl60x_state *state, u8 reg, u8 *val)
{
	u8 buf[2] = { 0xfb, reg };
	struct i2c_msg msg[] = {
		{ .addr = state->addr, .flags = 0, .buf = buf, .len = 2 },
		{ .addr = state->addr, .flags = I2C_M_RD, .buf = val, .len = 1 },
	};
	int ret;

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret == 2) {
		ret = 0;
	} else {
		dev_warn(&state->i2c->dev,
			 "i2c read failed = %d, reg = %02x\n", ret, reg);
		ret = -EREMOTEIO;
	}
	return ret;
}

/*
 * Write the DVB-C register table, applying the MxL603-specific
 * override for register 0xD5 (0x05 vs 0x04 for MxL608).
 */
static int mxl60x_write_dvbc_regs(struct mxl60x_state *state)
{
	int ret;

	ret = mxl60x_write_regs(state, mxl60x_digital_dvbc);
	if (ret)
		return ret;

	if (state->chip == MXL60X_603)
		ret = mxl60x_write_reg(state, 0xD5, 0x05);

	return ret;
}

static int mxl60x_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl60x_state *state = fe->tuner_priv;

	*frequency = 0;

	switch (state->config.if_freq_hz) {
	case MXL60X_IF_3_65MHz:
		*frequency = 3650000;
		break;
	case MXL60X_IF_4MHz:
		*frequency = 4000000;
		break;
	case MXL60X_IF_4_1MHz:
		*frequency = 4100000;
		break;
	case MXL60X_IF_4_15MHz:
		*frequency = 4150000;
		break;
	case MXL60X_IF_4_5MHz:
		*frequency = 4500000;
		break;
	case MXL60X_IF_4_57MHz:
		*frequency = 4570000;
		break;
	case MXL60X_IF_5MHz:
		*frequency = 5000000;
		break;
	case MXL60X_IF_5_38MHz:
		*frequency = 5380000;
		break;
	case MXL60X_IF_6MHz:
		*frequency = 6000000;
		break;
	case MXL60X_IF_6_28MHz:
		*frequency = 6280000;
		break;
	case MXL60X_IF_7_2MHz:
		*frequency = 7200000;
		break;
	case MXL60X_IF_8_25MHz:
		*frequency = 8250000;
		break;
	case MXL60X_IF_35_25MHz:
		*frequency = 35250000;
		break;
	case MXL60X_IF_36MHz:
		*frequency = 36000000;
		break;
	case MXL60X_IF_36_15MHz:
		*frequency = 36150000;
		break;
	case MXL60X_IF_36_65MHz:
		*frequency = 36650000;
		break;
	case MXL60X_IF_44MHz:
		*frequency = 44000000;
		break;
	}
	return 0;
}

static int mxl60x_set_freq(struct mxl60x_state *state, int freq,
			    enum mxl60x_mode mode, enum mxl60x_bw_mhz bw,
			    struct freq_table *ftable)
{
	u8 d = 0, d1 = 0, d2 = 0, d3 = 0;
	u16 f;
	u32 tmp, div;
	int ret;
	int i;

	ret = mxl60x_write_reg(state, 0x12, 0x00);
	if (ret)
		goto err;

	if (freq < 700000000) {
		ret = mxl60x_write_reg(state, 0x7C, 0x1F);
		if (ret)
			goto err;

		if (mode == MXL60X_MODE_CABLE)
			d = 0xC1;
		else
			d = 0x81;
	} else {
		ret = mxl60x_write_reg(state, 0x7C, 0x9F);
		if (ret)
			goto err;

		if (mode == MXL60X_MODE_CABLE)
			d = 0xD1;
		else
			d = 0x91;
	}

	ret = mxl60x_write_reg(state, 0x00, 0x01);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x31, d);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x00);
	if (ret)
		goto err;

	for (i = 0; 0 != ftable->center_freq; i++, ftable++) {
		if (ftable->center_freq == 1) {
			d1 = ftable->reg1;
			d2 = ftable->reg2;
			break;
		}
	}

	for (i = 0; 0 != ftable->center_freq; i++, ftable++) {
		if ((ftable->center_freq - 500000) <= freq &&
		    (ftable->center_freq + 500000) >= freq) {
			d1 = ftable->reg1;
			d2 = ftable->reg2;
			break;
		}
	}

	ret = mxl60x_write_reg(state, 0xEA, d1);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0xEB, d2);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x0F, bw);
	if (ret)
		goto err;

	/* convert freq to 10.6 fixed point float [MHz] */
	f = freq / 1000000;
	tmp = freq % 1000000;
	div = 1000000;
	for (i = 0; i < 6; i++) {
		f <<= 1;
		div >>= 1;
		if (tmp > div) {
			tmp -= div;
			f |= 1;
		}
	}
	if (tmp > 7812)
		f++;

	d1 = f & 0xFF;
	d2 = f >> 8;

	ret = mxl60x_write_reg(state, 0x10, d1);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x11, d2);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x0B, 0x01);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x01);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0x96, &d);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x00);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0xB6, &d1);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x01);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0x60, &d2);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0x5F, &d3);
	if (ret)
		goto err;

	if ((d & 0x10) == 0x10) {
		d1 &= 0xBF;
		d1 |= 0x0E;

		d2 &= 0xC0;
		d2 |= 0x0E;

		d3 &= 0xC0;
		d3 |= 0x0E;
	} else {
		d1 |= 0x40;
		d1 &= 0xC0;

		d2 &= 0xC0;
		d2 |= 0x37;

		d3 &= 0xC0;
		d3 |= 0x37;
	}

	ret = mxl60x_write_reg(state, 0x60, d2);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x5F, d3);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x00);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0xB6, d);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x12, 0x01);
	if (ret)
		goto err;

	msleep(20);

	d |= 0x40;

	ret = mxl60x_write_reg(state, 0xB6, d);
	if (ret)
		goto err;

	msleep(20);

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_set_mode(struct dvb_frontend *fe, struct mxl60x_state *state,
			    enum mxl60x_mode mode)
{
	u8 cfg_0, cfg_1, pwr, dfe;
	int ret;
	u32 if_out_freq;
	struct reg_pair_t *reg_table;

	ret = mxl60x_get_if_frequency(fe, &if_out_freq);
	if (!if_out_freq)
		goto err;

	if_out_freq /= 1000;

	switch (mode) {
	case MXL60X_MODE_CABLE:
		reg_table = mxl60x_digital_dvbc;
		pwr = 0;
		dfe = 0xFF;

		if (if_out_freq < 35250) {
			cfg_0 = 0xFE;
			cfg_1 = 0x10;
		} else {
			cfg_0 = 0xD9;
			cfg_1 = 0x16;
		}
		break;
	case MXL60X_MODE_ISDBT_ATSC:
		reg_table = mxl60x_digital_isdbt_atsc;
		dfe = 0x1C;

		if (if_out_freq < 35250) {
			cfg_0 = 0xF9;
			cfg_1 = 0x18;
			pwr = 0xF1;
		} else {
			cfg_0 = 0xD9;
			cfg_1 = 0x16;
			pwr = 0xB1;
		}
		switch (state->config.if_out_gain_level) {
		case 0x09:
			dfe = 0x44;
			break;
		case 0x08:
			dfe = 0x43;
			break;
		case 0x07:
			dfe = 0x42;
			break;
		case 0x06:
			dfe = 0x41;
			break;
		case 0x05:
			dfe = 0x40;
			break;
		default:
			break;
		}
		break;
	case MXL60X_MODE_DVBT:
		reg_table = mxl60x_digital_dvbt;
		dfe = 0;
		if (if_out_freq < 35250) {
			cfg_0 = 0xFE;
			cfg_1 = 0x18;
			pwr = 0xF1;
		} else {
			cfg_0 = 0xD9;
			cfg_1 = 0x16;
			pwr = 0xB1;
		}
		switch (state->config.if_out_gain_level) {
		case 0x09:
			dfe = 0x44;
			break;
		case 0x08:
			dfe = 0x43;
			break;
		case 0x07:
			dfe = 0x42;
			break;
		case 0x06:
			dfe = 0x41;
			break;
		case 0x05:
			dfe = 0x40;
			break;
		default:
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	if (reg_table == mxl60x_digital_dvbc)
		ret = mxl60x_write_dvbc_regs(state);
	else
		ret = mxl60x_write_regs(state, reg_table);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x5A, cfg_0);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x5B, cfg_1);
	if (ret)
		goto err;

	if (pwr) {
		ret = mxl60x_write_reg(state, 0x5C, pwr);
		if (ret)
			goto err;
	}

	ret = mxl60x_write_reg(state, 0xEA,
			       state->config.xtal_freq_hz ? 0x0E : 0x0D);
	if (ret)
		goto err;

	if (dfe != 0xFF) {
		ret = mxl60x_write_reg(state, 0xDC, dfe);
		if (ret)
			goto err;
	}

	ret = mxl60x_write_reg(state, 0x03, 0x00);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x03, 0x01);
	if (ret)
		goto err;

	msleep(50);

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_set_agc(struct mxl60x_state *state)
{
	u8 d = 0;
	int ret;

	ret = mxl60x_read_reg(state, 0x08, &d);
	if (ret)
		goto err;

	d &= 0xF2;
	d = (u8)(d | (state->config.agc_type << 2) | 0x01);
	ret = mxl60x_write_reg(state, 0x08, d);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0x09, &d);
	if (ret)
		goto err;

	d &= 0x80;
	d |= (u8)(state->config.agc_set_point & 0xff);
	ret = mxl60x_write_reg(state, 0x09, d);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0x5E, &d);
	if (ret)
		goto err;

	d &= 0xEF;
	d |= (state->config.agc_invert_pol << 4);
	ret = mxl60x_write_reg(state, 0x5E, d);
	if (ret)
		goto err;

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_set_if_out(struct mxl60x_state *state)
{
	u8 d = 0;
	int ret;

	ret = mxl60x_read_reg(state, 0x04, &d);
	if (ret)
		goto err;

	d |= state->config.if_freq_hz;

	ret = mxl60x_write_reg(state, 0x04, d);
	if (ret)
		goto err;

	d = 0;
	if (state->config.invert_if)
		d = 0x3 << 6;

	d += (state->config.gain_level & 0x0F);
	d |= 0x20;
	ret = mxl60x_write_reg(state, 0x05, d);
	if (ret)
		goto err;

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_set_xtal(struct mxl60x_state *state)
{
	u8 d = 0;
	int ret;

	d = (u8)((state->config.xtal_freq_hz << 5) |
		 (state->config.xtal_cap & 0x1F));
	d |= (state->config.clk_out_enable << 7);

	ret = mxl60x_write_reg(state, 0x01, d);
	if (ret)
		goto err;

	d = (0x01 & (u8)state->config.clk_out_div);

	if (state->config.xtal_sharing_mode) {
		d |= 0x40;

		ret = mxl60x_write_reg(state, 0x02, d);
		if (ret)
			goto err;
		ret = mxl60x_write_reg(state, 0x6D, 0x80);
		if (ret)
			goto err;
	} else {
		d &= 0x01;
		ret = mxl60x_write_reg(state, 0x02, d);
		if (ret)
			goto err;
		ret = mxl60x_write_reg(state, 0x6D, 0x0A);
		if (ret)
			goto err;
	}

	if (state->config.single_supply_3_3V) {
		ret = mxl60x_write_reg(state, 0x0E, 0x14);
		if (ret)
			goto err;
	}

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_tuner_init_default(struct mxl60x_state *state)
{
	u8 d = 0;
	int ret;

	ret = mxl60x_write_reg(state, 0xFF, 0x00);
	if (ret)
		goto err;

	ret = mxl60x_write_dvbc_regs(state);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x01);
	if (ret)
		goto err;

	ret = mxl60x_read_reg(state, 0x31, &d);
	if (ret)
		goto err;

	d &= 0x2F;
	d |= 0xD0;

	ret = mxl60x_write_reg(state, 0x31, d);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x00);
	if (ret)
		goto err;

	if (state->config.single_supply_3_3V) {
		ret = mxl60x_write_reg(state, 0x0E, 0x04);
		if (ret)
			goto err;
	}

	mdelay(1);

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_synth_lock_status(struct mxl60x_state *state, int *rf_locked,
				     int *ref_locked)
{
	u8 d = 0;
	int ret;

	*rf_locked = 0;
	*ref_locked = 0;

	ret = mxl60x_read_reg(state, 0x2B, &d);
	if (ret)
		goto err;

	if ((d & 0x02) == 0x02)
		*rf_locked = 1;

	if ((d & 0x01) == 0x01)
		*ref_locked = 1;

	return 0;

err:
	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct mxl60x_state *state = fe->tuner_priv;
	int rf_locked, ref_locked, ret;

	*status = 0;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl60x_synth_lock_status(state, &rf_locked, &ref_locked);
	if (ret)
		goto err;

	dev_dbg(&state->i2c->dev, "%s%s", rf_locked ? "rf locked " : "",
		ref_locked ? "ref locked" : "");

	if ((rf_locked) || (ref_locked))
		*status |= TUNER_STATUS_LOCKED;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct mxl60x_state *state = fe->tuner_priv;
	struct freq_table *ftable;
	enum mxl60x_bw_mhz bw;
	enum mxl60x_mode mode;
	int ret;
	u32 freq = c->frequency;

	dev_dbg(&state->i2c->dev,
		"%s: delivery_system = %d frequency = %d bandwidth_hz = %d\n",
		__func__, c->delivery_system, c->frequency, c->bandwidth_hz);

	switch (c->delivery_system) {
	case SYS_ATSC:
		mode = MXL60X_MODE_ISDBT_ATSC;
		bw = MXL60X_TERR_BW_6MHZ;
		ftable = mxl60x_digital_freq;
		break;
	case SYS_DVBC_ANNEX_A:
		mode = MXL60X_MODE_CABLE;
		ftable = mxl60x_cable_freq;
		bw = MXL60X_CABLE_BW_8MHZ;
		break;
	case SYS_DVBT:
	case SYS_DVBT2:
		mode = MXL60X_MODE_DVBT;
		ftable = mxl60x_digital_freq;
		switch (c->bandwidth_hz) {
		case 6000000:
			bw = MXL60X_TERR_BW_6MHZ;
			break;
		case 7000000:
			bw = MXL60X_TERR_BW_7MHZ;
			break;
		case 8000000:
			bw = MXL60X_TERR_BW_8MHZ;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: err state = %d\n", __func__,
			fe->dtv_property_cache.delivery_system);
		return -EINVAL;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl60x_tuner_init_default(state);
	if (ret)
		goto err;

	ret = mxl60x_set_xtal(state);
	if (ret)
		goto err;

	ret = mxl60x_set_if_out(state);
	if (ret)
		goto err;

	ret = mxl60x_set_agc(state);
	if (ret)
		goto err;

	ret = mxl60x_set_mode(fe, state, mode);
	if (ret)
		goto err;

	ret = mxl60x_set_freq(state, freq, mode, bw, ftable);
	if (ret)
		goto err;

	state->frequency = freq;
	state->bandwidth = c->bandwidth_hz;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	msleep(15);

	return 0;

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl60x_state *state = fe->tuner_priv;

	*frequency = state->frequency;
	return 0;
}

static int mxl60x_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct mxl60x_state *state = fe->tuner_priv;

	*bandwidth = state->bandwidth;
	return 0;
}

static int mxl60x_init(struct dvb_frontend *fe)
{
	struct mxl60x_state *state = fe->tuner_priv;
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* wake from standby */
	ret = mxl60x_write_reg(state, 0x0B, 0x01);
	if (ret)
		goto err;
	ret = mxl60x_write_reg(state, 0x12, 0x01);
	if (ret)
		goto err;
	ret = mxl60x_write_reg(state, 0x00, 0x01);
	if (ret)
		goto err;

	if (state->config.loop_thru_enable)
		ret = mxl60x_write_reg(state, 0x60, 0x0E);
	else
		ret = mxl60x_write_reg(state, 0x60, 0x37);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x00, 0x00);
	if (ret)
		goto err;

	return 0;

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static int mxl60x_sleep(struct dvb_frontend *fe)
{
	struct mxl60x_state *state = fe->tuner_priv;
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* enter standby mode */
	ret = mxl60x_write_reg(state, 0x12, 0x00);
	if (ret)
		goto err;

	ret = mxl60x_write_reg(state, 0x0B, 0x00);
	if (ret)
		goto err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&state->i2c->dev, "%s: failed = %d\n", __func__, ret);
	return ret;
}

static void mxl60x_release(struct dvb_frontend *fe)
{
	struct mxl60x_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);
}

static const struct dvb_tuner_ops mxl60x_tuner_ops = {
	.info = {
		.name = "MxL60x Common",
		.frequency_min_hz  =    1 * MHz,
		.frequency_max_hz  = 1200 * MHz,
		.frequency_step_hz =   25 * kHz,
	},
	.init              = mxl60x_init,
	.sleep             = mxl60x_sleep,
	.set_params        = mxl60x_set_params,
	.get_status        = mxl60x_get_status,
	.get_frequency     = mxl60x_get_frequency,
	.get_bandwidth     = mxl60x_get_bandwidth,
	.release           = mxl60x_release,
	.get_if_frequency  = mxl60x_get_if_frequency,
};

static int mxl60x_get_chip_id(struct mxl60x_state *state)
{
	int ret;
	u8 id;

	ret = mxl60x_read_reg(state, 0x18, &id);
	if (ret)
		goto err;

	if (id != 0x02) {
		ret = -ENODEV;
		goto err;
	}

	dev_info(&state->i2c->dev, "%s detected id = 0x%02X\n",
		 mxl60x_chip_name(state->chip), id);

	return ret;

err:
	dev_warn(&state->i2c->dev, "%s unable to identify device (0x%02x)\n",
		 mxl60x_chip_name(state->chip), id);
	return ret;
}

struct dvb_frontend *mxl60x_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c, u8 addr,
				   struct mxl60x_config *cfg,
				   enum mxl60x_chip chip)
{
	struct mxl60x_state *state = NULL;
	int ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		dev_err(&i2c->dev, "kzalloc() failed\n");
		goto err;
	}

	state->config = *cfg;
	state->i2c = i2c;
	state->addr = addr;
	state->chip = chip;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl60x_get_chip_id(state);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret)
		goto err_free;

	dev_info(&i2c->dev, "Attaching %s\n", mxl60x_chip_name(chip));

	fe->tuner_priv = state;

	memcpy(&fe->ops.tuner_ops, &mxl60x_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	/* Set the correct chip name in tuner_ops.info */
	snprintf(fe->ops.tuner_ops.info.name,
		 sizeof(fe->ops.tuner_ops.info.name),
		 "MaxLinear %s", mxl60x_chip_name(chip));

	return fe;

err_free:
	kfree(state);
err:
	return NULL;
}
EXPORT_SYMBOL_GPL(mxl60x_attach);

MODULE_DESCRIPTION("MaxLinear MxL603/MxL608 Common Tuner Driver");
MODULE_AUTHOR("Sasa Savic <sasa.savic.sr@gmail.com>");
MODULE_AUTHOR("Igor Mokrushin <mcmcc@mail.ru>");
MODULE_LICENSE("GPL");
