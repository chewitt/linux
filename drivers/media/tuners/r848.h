// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rafael R848 silicon tuner driver
 *
 * Copyright (C) 2015 Luis Alves <ljalvs@gmail.com>
 *
 */

#ifndef R848_H
#define R848_H

#include <linux/kconfig.h>
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

typedef struct _R848_Sys_Info_Type {
	u16 IF_KHz;
	u16 FILT_CAL_IF;
	u8 BW;
	u8 V17M;
	u8 HPF_COR;
	u8 FILT_EXT_ENA;
	u8 FILT_EXT_WIDEST;
	u8 FILT_EXT_POINT;
	//u8 AGC_CLK;
	u8 FILT_COMP;
	u8 FILT_CUR;
	u8 FILT_3DB;
	u8 SWBUF_CUR;
	u8 TF_CUR;
	u8 INDUC_BIAS;
	u8 SWCAP_CLK;
	u8 NA_PWR_DET;
} R848_Sys_Info_Type;

struct r848_config {
	/* tuner i2c address */
	u8 i2c_address;

	/* tuner */
	u8 R848_DetectTfType;
	unsigned char R848_pre_standard;
	u8 R848_Array[40];
	u8 R848_Xtal_Pwr;
	u8 R848_Xtal_Pwr_tmp;

	/* dvbc/t */
	u8 R848_SetTfType;
	R848_Sys_Info_Type Sys_Info1;
	/* DVBT */
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_R848)
extern struct dvb_frontend *r848_attach(struct dvb_frontend *fe,
					struct r848_config *cfg,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *r848_attach(struct dvb_frontend *fe,
					       struct r848_config *cfg,
					       struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* R848_H */
