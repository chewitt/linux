// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include "aml_vdec_canvas_utils.h"

#define CANVAS_REG_OFFSET   (0x12 << 2)

/*
 * DMC_CAV_LUT_DATAL/DMC_CAV_LUT_DATAH
 * high 32bits of canvas data which need to be configured
 * to canvas memory.
 * 64bits CANVAS look up table
 * bit 61 : 58   Endian control.
 * bit 61 :  1 : switch 2 64bits data inside 128bits boundary.
 * 0 : no change.
 * bit 60 :  1 : switch 2 32bits data inside 64bits data boundary.
 * 0 : no change.
 * bit 59 :  1 : switch 2 16bits data inside 32bits data boundary.
 * 0 : no change.
 * bit 58 :  1 : switch 2 8bits data  inside 16bits data boundary.
 * 0 : no change.
 * bit 57 : 56.   Canvas block mode.  2 : 64x32, 1: 32x32;
 *  0 : linear mode.
 * bit 55 :      canvas Y direction wrap control.
 * 1: wrap back in y.  0: not wrap back.
 * bit 54 :      canvas X direction wrap control.
 * 1: wrap back in X.  0: not wrap back.
 * bit 53 : 41.   canvas Hight.
 * bit 40 : 29.   canvas Width, unit: 8bytes. must in 32bytes boundary.
 * that means last 2 bits must be 0.
 * bit 28 : 0.    canvas start address.   unit. 8 bytes. must be in
 * 32bytes boundary. that means last 2bits must be 0.
 */

#define CANVAS_WADDR_LBIT       0
#define CANVAS_WIDTH_LBIT       29
#define CANVAS_HEIGHT_HBIT      (41 - 32)
#define CANVAS_WRAPX_HBIT       (54 - 32)
#define CANVAS_WRAPY_HBIT       (55 - 32)
#define CANVAS_BLKMODE_HBIT     (56 - 32)
#define CANVAS_ENDIAN_HBIT      (58 - 32)

/* canvas regs */
#define DC_CAV_LUT_DATAL        0x12
#define DC_CAV_LUT_DATAH        0x13
#define DC_CAV_LUT_ADDR         0x14
#define DC_CAV_LUT_RDATAL       0x15
#define DC_CAV_LUT_RDATAH       0x16

#define CANVAS_ADDR_LMASK       0x1fffffff
#define CANVAS_WIDTH_LMASK      0x7
#define CANVAS_WIDTH_LWID       3
#define CANVAS_WIDTH_HMASK      0x1ff
#define CANVAS_WIDTH_HBIT       0
#define CANVAS_HEIGHT_MASK      0x1fff
#define CANVAS_HEIGHT_BIT       9
#define CANVAS_ADDR_NOWRAP      0x00
#define CANVAS_ADDR_WRAPX       0x01
#define CANVAS_ADDR_WRAPY       0x02
#define CANVAS_BLKMODE_MASK     0x03

#define CANVAS_BLKMODE_LINEAR   0x00
#define CANVAS_BLKMODE_32X32    0x01
#define CANVAS_BLKMODE_64X32    0x02

#define CANVAS_LUT_WR_EN   (0x2 << 8)
#define CANVAS_LUT_RD_EN   (0x1 << 8)

#define CANVAS_ADDR_BITS_MASK		GENMASK(28, 0)
#define CANVAS_WIDTH_L_MASK		GENMASK(31, 29)
#define CANVAS_WIDTH_H_MASK		GENMASK(8, 0)
#define CANVAS_HEIGHT_H_MASK		GENMASK(21, 9)
#define CANVAS_WRAP_H_MASK		GENMASK(23, 23)
#define CANVAS_BLOCK_H_MASK		GENMASK(25, 24)
#define CANVAS_BITS_CTRL_MASK		GENMASK(29, 26)

static struct vdec_canvas_s canvas;

static void write_canvas_reg(u32 addr, u32 val)
{
	int offset = -DC_CAV_LUT_DATAL;

	return writel(val, canvas.regs_base + ((addr + offset) << 2));
}

static int aml_vdec_canvas_alloc(u8 *canvas_index)
{
	int i;
	unsigned long flags;

	*canvas_index = -1;
	spin_lock_irqsave(&canvas.canvas_lock, flags);
	for (i = 0; i < CANVAS_MAX_SIZE; i++) {
		if (i >= 0x10 && i <= 0x15)	/* 0x10~0x15 is used by RDMA */
			continue;
		if (canvas.canvas_used[i] == 0) {
			*canvas_index = i;
			canvas.canvas_used[i] = 1;
			spin_unlock_irqrestore(&canvas.canvas_lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&canvas.canvas_lock, flags);

	return -1;
}

static void aml_vdec_canvas_free(u8 canvas_index)
{
	unsigned long flags;

	if (canvas_index < 0 || canvas_index > CANVAS_MAX_SIZE)
		return;

	spin_lock_irqsave(&canvas.canvas_lock, flags);
	if (canvas.canvas_used[canvas_index])
		canvas.canvas_used[canvas_index] = 0;

	spin_unlock_irqrestore(&canvas.canvas_lock, flags);
}

static void aml_vdec_config_canvas(u8 canvas_index,
				   ulong addr, u32 width, u32 height,
				   u32 wrap, u32 blkmode, u32 endian)
{
	u32 data_h, data_l;

	data_h = FIELD_PREP(CANVAS_WIDTH_H_MASK,
			    (((width + 7) >> 3) >> CANVAS_WIDTH_LWID)) |
		FIELD_PREP(CANVAS_HEIGHT_H_MASK, height) |
		FIELD_PREP(CANVAS_WRAP_H_MASK, wrap) |
		FIELD_PREP(CANVAS_BLOCK_H_MASK, blkmode) |
		FIELD_PREP(CANVAS_BITS_CTRL_MASK, endian);

	data_l = FIELD_PREP(CANVAS_ADDR_BITS_MASK, ((addr + 7) >> 3)) |
		FIELD_PREP(CANVAS_WIDTH_L_MASK, ((width + 7) >> 3));

	write_canvas_reg(DC_CAV_LUT_DATAL, data_l);
	write_canvas_reg(DC_CAV_LUT_DATAH, data_h);
	write_canvas_reg(DC_CAV_LUT_ADDR, CANVAS_LUT_WR_EN | canvas_index);
}

void aml_vdec_canvas_register(struct aml_vdec_hw *hw)
{
	canvas.regs_base = hw->regs[DMC_BUS] + CANVAS_REG_OFFSET;
	memset(canvas.canvas_used, 0, CANVAS_MAX_SIZE);
	spin_lock_init(&canvas.canvas_lock);

	hw->hw_ops.canvas_alloc = aml_vdec_canvas_alloc;
	hw->hw_ops.canvas_free = aml_vdec_canvas_free;
	hw->hw_ops.config_canvas = aml_vdec_config_canvas;
}
