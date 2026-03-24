//SPX-License-Identifier: GPL-2.0-or-later
/*
 * Availink AVL6862 Demod Driver
 *
 * Copyright (C) 2015 Luis Alves <ljalvs@gmail.com>
 *
 */

#ifndef AVL6862_H
#define AVL6862_H

#include <linux/dvb/frontend.h>
#include "media/dvb_frontend.h"

#define MAX_CHANNEL_INFO 256

struct avl6862_config {
	int i2c_id; // i2c adapter id
	void *i2c_adapter; // i2c adapter
	u8 demod_address; // demodulator i2c address
	u8 dual_tuner; // 0: single tuner, 1: dual tuner
	unsigned char eDiseqcStatus;
	int ts_serial;
	int gpio_lock_led;
};

struct avl6862_priv {
	struct i2c_adapter *i2c;
	struct avl6862_config *config;
	struct dvb_frontend frontend;
	enum fe_delivery_system delivery_system;

	/* DVB-Tx */
	u16 g_nChannel_ts_total;

	/* Copy of the config provided to the mxl603_attach */
	struct avl6862_config _cfg;
};

extern struct dvb_frontend *avl6862_attach(struct avl6862_config *config,
					   struct i2c_adapter *i2c);

#endif /* AVL6862_H */
