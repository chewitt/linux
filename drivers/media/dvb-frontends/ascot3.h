// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sony Ascot3 DVB-T/T2/C Tuner Driver
 * Based on ascot2e driver
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 * Copyright (C) 2015 Sasa Savic <sasa.savic.sr@gmail.com>
 *
 */

#ifndef __DVB_ASCOT3_H__
#define __DVB_ASCOT3_H__

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

/**
 * struct ascot3_config - the configuration of Ascot2E tuner driver
 * @i2c_address:	I2C address of the tuner
 * @xtal_freq_mhz:	Oscillator frequency, MHz
 */
struct ascot3_config {
	u8	i2c_address;
	u8	xtal_freq_mhz;
};

#if IS_REACHABLE(CONFIG_DVB_ASCOT3)
/**
 * ascot3_attach - Attach an ascot3 tuner
 *
 * @fe: frontend to be attached
 * @config: pointer to &struct ascot3_config with tuner configuration.
 * @i2c: i2c adapter to use.
 *
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *ascot3_attach(struct dvb_frontend *fe,
					const struct ascot3_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *ascot3_attach(struct dvb_frontend *fe,
					const struct ascot3_config *config,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
