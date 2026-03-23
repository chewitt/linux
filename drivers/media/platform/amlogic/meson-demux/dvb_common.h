// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 */

#ifndef __DVB_COMMON_H__
#define __DVB_COMMON_H__

//#include <linux/amlogic/aml_dvb_extern.h>

#include "media/dvb_frontend.h"

typedef enum dmx_source {
	DMX_SOURCE_FRONT0 = 0,
	DMX_SOURCE_FRONT1,
	DMX_SOURCE_FRONT2,
	DMX_SOURCE_FRONT3,
	DMX_SOURCE_DVR0 = 16,
	DMX_SOURCE_DVR1,
	DMX_SOURCE_DVR2,
	DMX_SOURCE_DVR3,
	DMX_SOURCE_FRONT0_OFFSET = 100,
	DMX_SOURCE_FRONT1_OFFSET,
	DMX_SOURCE_FRONT2_OFFSET
} dmx_source_t;

enum aml_dmx_id_t {
	AM_DMX_0 = 0,
	AM_DMX_1,
	AM_DMX_2,
	AM_DMX_MAX,
};

enum aml_ts_source_t {
	AM_TS_SRC_TS0,
	AM_TS_SRC_TS1,
	AM_TS_SRC_TS2,
	AM_TS_SRC_TS3,
	AM_TS_SRC_S_TS0,
	AM_TS_SRC_S_TS1,
	AM_TS_SRC_S_TS2,
	AM_TS_SRC_S_TS3,
	AM_TS_SRC_HIU,
	AM_TS_SRC_HIU1,
	AM_TS_SRC_DMX0,
	AM_TS_SRC_DMX1,
	AM_TS_SRC_DMX2
};

#endif /* __DVB_COMMON_H__ */
