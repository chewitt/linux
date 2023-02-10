// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Sony CXD2837ER DVB-T/T2/C demodulator.
 *
 * Copyright (C) 2014 Sasa Savic <sasa.savic.sr@gmail.com>
 *
 */

#ifndef _CXD2837_H_
#define _CXD2837_H_

#include <linux/types.h>
#include <linux/i2c.h>

enum EDemodState {
	Unknown,
	Shutdown,
	Sleep,
	Active
};

enum xtal_freq {
	XTAL_20500KHz,           /* 20.5 MHz */
	XTAL_41000KHz            /* 41 MHz */
};

enum ts_serial_clk {
	SERIAL_TS_CLK_HIGH_FULL,   /* High frequency, full rate */
	SERIAL_TS_CLK_MID_FULL,    /* Mid frequency, full rate */
	SERIAL_TS_CLK_LOW_FULL,    /* Low frequency, full rate */
	SERIAL_TS_CLK_HIGH_HALF,   /* High frequency, half rate */
	SERIAL_TS_CLK_MID_HALF,    /* Mid frequency, half rate */
	SERIAL_TS_CLK_LOW_HALF     /* Low frequency, half rate */
};

struct cxd2837_cfg {

	/* Demodulator I2C address.
	 * Default: none, must set
	 * Values: 0x6c, 0x6d
	 */
	u8 adr;

	/* IF AGC polarity.
	 * Default: 0
	 * Values: 0, 1
	 */
	bool if_agc_polarity;

	/* RFAIN monitoring.
	 * Default: 0
	 * Values: 0, 1
	 */
	bool rfain_monitoring;

	/* TS error polarity.
	 * Default: 0
	 * Values: 0 : low, 1 : high
	 */
	bool ts_error_polarity;

	/* Clock polarity.
	 * Default: 0
	 * Values:  0 : Falling edge, 1 : Rising edge
	 */
	bool clock_polarity;

	/* IFAGC ADC range/
	 * Default: 0
	 * Values:  0 : 1.4Vpp, 1 : 1.0Vpp, 2 : 0.7Vpp
	 */
	u8 ifagc_adc_range;

	/* Spectrum inversion.
	 * Default: 0
	 * Values: 0, 1
	 */
	bool spec_inv;

	/* Demodulator crystal frequency.
	*/
	enum xtal_freq xtal;

	/* TS serial clock frequency
	*/
	enum ts_serial_clk ts_clock;
};

extern struct dvb_frontend *cxd2837_attach(struct i2c_adapter *i2c,
					   struct cxd2837_cfg *cfg);

#endif
