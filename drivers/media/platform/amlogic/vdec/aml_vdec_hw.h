/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#ifndef _AML_VDEC_HW_H_
#define _AML_VDEC_HW_H_

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include "reg_defines.h"

#define VDEC_FIFO_ALIGN     8
#define VLD_PADDING_SIZE    1024

#define RD_VREG(addr) read_dos_reg(addr)
#define WRITE_VREG(addr, val) write_dos_reg(addr, val)
#define WRITE_VREG_BITS(r, val, start, len) dos_reg_write_bits(r, val, start, len)
#define CLEAR_VREG_MASK(r, mask) dos_reg_clear_mask(r, mask)
#define SET_VREG_MASK(r, mask) dos_reg_set_mask(r, mask)

/**
 * struct aml_vdec_hw_ops - codec mode specific operations for hw
 * @load_firmware_ex: Load firmware for current dec specific.
 * @irq_handler: mandatory call when the ISR triggers
 * @irq_threaded_func: mandatory call for the threaded ISR
 * @canvas_alloc: Alloc canvas for curr frame
 * @canvas_free: Release canvas.
 * @config_canvas: Config for curr frame, such as w/h, Y/UV start addr etc.
 */
struct aml_vdec_hw_ops {
	int (*load_firmware_ex)(void *priv, const u8 *data, u32 len);
	irqreturn_t (*irq_handler)(int irq, void *priv);
	irqreturn_t (*irq_threaded_func)(int irq, void *priv);
	int (*canvas_alloc)(u8 *canvas_index);
	void (*canvas_free)(u8 canvas_index);
	void (*config_canvas)(u8 canvas_index,
			      ulong addr, u32 width, u32 height,
			      u32 wrap, u32 blkmode, u32 endian);
};

/**
 * enum vdec_type_e - Type of decoder hardware.
 */
enum vdec_type_e {
	VDEC_1 = 0,
	VDEC_HEVC,
	VDEC_MAX,
};

/**
 * enum vdec_irq_num - Definition of the irq.
 */
enum vdec_irq_num {
	VDEC_IRQ_0 = 0,
	VDEC_IRQ_1,
	VDEC_IRQ_2,
	VDEC_IRQ_MAX,
};

/**
 * enum vdec_type_e - Type of decoder clock.
 */
enum clk_type_e {
	VDEC = 0,
	VDEC_MUX,
	HEVCF_MUX,
	DOS_CLK_MAX,
};

/**
 * enum aml_power_type_e - Type of decoder power.
 */
enum aml_power_type_e {
	AML_PM_PD = 0,
};

/**
 * enum mm_bus_e - Type of decoder hardware bus.
 */
enum mm_bus_e {
	DOS_BUS = 0,
	DMC_BUS,
	MAX_REG_BUS
};

/**
 * struct gate_switch_node - clock node definition
 * @clk: Pointer to clk instance.
 * @name: Clock name used.
 * @mutex: Mutex lock for multi decoder instance.
 * @ref_count: Curr clk instance ref count.
 */
struct gate_switch_node {
	struct clk *clk;
	const char *name;
	struct mutex mutex;
	int ref_count;
};

/**
 * struct pm_pd_s - power domain definition
 * @name: Power domain name.
 * @dev: Pointer to device structure.
 * @mutex: Pointer to device_link structure.
 * @ref_count: Curr power domain instance ref count.
 */
struct pm_pd_s {
	u8 *name;
	struct device *dev;
	struct device_link *link;
	int ref_count;
};

/**
 * struct aml_vdec_hw - decoder hardware resources definition
 * @pdev: Pointer to struct platform_device.
 * @dev: Pointer to struct device.
 * @regs: Reg base for dos/dmc hardware.
 * @pm_mutex: Mutex for pm->pd_data.
 * @pm: Pointer to struct power_manager_s.
 * @hw_ops: Hardware resource operation functions. See struct aml_vdec_hw_ops.
 * @gates: Clk instance used by curr decoder context.
 * @dec_irq: Irq registered.
 * @curr_ctx: Pointer to curr decoder context.
 */
struct aml_vdec_hw {
	struct platform_device *pdev;
	struct device *dev;
	void __iomem *regs[MAX_REG_BUS];
	struct mutex pm_mutex;
	const struct power_manager_s *pm;
	struct aml_vdec_hw_ops hw_ops;
	struct gate_switch_node gates[DOS_CLK_MAX];
	int dec_irq;
	void *curr_ctx;
};

/**
 * struct power_manager_s - Power manager & opertion function
 * @pd_data: Pointer to struct pm_pd_s
 * @init: Power manager init.
 * @release: Power manager release.
 * @power_on: Power on for decoder hw.
 * @power_off: Power off for decoder hw.
 * @power_state: Query the power state.
 */
struct power_manager_s {
	struct pm_pd_s *pd_data;
	int (*init)(struct aml_vdec_hw *hw);
	void (*release)(struct aml_vdec_hw *hw);
	void (*power_on)(struct aml_vdec_hw *hw, int id);
	void (*power_off)(struct aml_vdec_hw *hw, int id);
	bool (*power_state)(struct aml_vdec_hw *hw, int id);
};

int dev_request_hw_resources(void *priv);
void dev_destroy_hw_resources(void *priv);
struct aml_vdec_hw *vdec_get_hw(void *priv);
u32 read_dos_reg(u32 reg_addr);
void write_dos_reg(u32 addr, int val);
void dos_reg_write_bits(u32 reg, u32 val, int start, int len);
void dos_reg_clear_mask(u32 addr, u32 mask);
void dos_reg_set_mask(u32 addr, u32 mask);
u32 read_dmc_reg(u32 addr);
void write_dmc_reg(u32 addr, int val);
int vdec_enable(struct aml_vdec_hw *hw);
int vdec_disable(struct aml_vdec_hw *hw);
void dos_enable(void);
void aml_start_vdec_hw(void);
void aml_stop_vdec_hw(void);
int load_firmware(struct aml_vdec_hw *hw, const char *path);
void aml_vdec_reset_core(void);

#endif
