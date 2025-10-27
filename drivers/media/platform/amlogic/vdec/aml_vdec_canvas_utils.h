/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */
#include "reg_defines.h"
#include "aml_vdec_hw.h"

#define CANVAS_MAX_SIZE 63

/**
 * struct vdec_canvas_s - helper to decoder canvas, which config the w/h, buffer addr of frames
 * @regs_base: base addr of canvas regs.
 * @canvas_used: used flag.
 * @canvas_lock: spinlock for canvas.
 */
struct vdec_canvas_s {
	void __iomem *regs_base;
	u8 canvas_used[CANVAS_MAX_SIZE];
	spinlock_t canvas_lock;
};

void aml_vdec_canvas_register(struct aml_vdec_hw *hw);
