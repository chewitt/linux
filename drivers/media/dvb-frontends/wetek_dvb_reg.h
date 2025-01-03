// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
*/

#ifndef _DVB_REG_H_
#define _DVB_REG_H_

#include <linux/amlogic/iomap.h>

#include "wetek_stb_define.h"
#include "wetek_stb_regs_define.h"

#define WRITE_MPEG_REG(_r, _v)   aml_write_cbus(_r, _v)
#define READ_MPEG_REG(_r)        aml_read_cbus(_r)

#define WRITE_CBUS_REG(_r, _v)   aml_write_cbus(_r, _v)
#define READ_CBUS_REG(_r)        aml_read_cbus(_r)

#define WRITE_VCBUS_REG(_r, _v)  aml_write_vcbus(_r, _v)
#define READ_VCBUS_REG(_r)       aml_read_vcbus(_r)

#define BASE_IRQ 32
#define AM_IRQ(reg)             (reg + BASE_IRQ)
#define INT_DEMUX               AM_IRQ(23)
#define INT_DEMUX_1             AM_IRQ(5)
#define INT_DEMUX_2             AM_IRQ(53)
#define INT_ASYNC_FIFO_FILL     AM_IRQ(18)
#define INT_ASYNC_FIFO_FLUSH    AM_IRQ(19)
#define INT_ASYNC_FIFO2_FILL    AM_IRQ(24)
#define INT_ASYNC_FIFO2_FLUSH   AM_IRQ(25)

#endif
