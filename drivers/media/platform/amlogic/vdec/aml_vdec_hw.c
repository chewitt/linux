// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/firmware.h>

#include "aml_vdec_drv.h"
#include "aml_vdec_hw.h"
#include "aml_vdec_platform.h"
#include "aml_vdec_canvas_utils.h"

#define MHz     (1000000)
#define MC_SIZE (4096 * 16)

static void __iomem *reg_base[MAX_REG_BUS];

static struct pm_pd_s vdec_domain_data[] = {
	[VDEC_1] = {.name = "pwrc-vdec", },
	[VDEC_HEVC] = {.name = "pwrc-hevc", },
};

u32 read_dos_reg(u32 addr)
{
	return readl(reg_base[DOS_BUS] + (addr << 2));
}

void write_dos_reg(u32 addr, int val)
{
	writel(val, reg_base[DOS_BUS] + (addr << 2));
}

u32 read_dmc_reg(u32 addr)
{
	return readl(reg_base[DMC_BUS] + (addr << 2));
}

void write_dmc_reg(u32 addr, int val)
{
	writel(val, reg_base[DMC_BUS] + (addr << 2));
}

void dos_reg_set_mask(u32 addr, u32 mask)
{
	u32 r;

	r = read_dos_reg(addr);
	write_dos_reg(addr, (r | mask));
}

void dos_reg_clear_mask(u32 addr, u32 mask)
{
	u32 r;

	r = read_dos_reg(addr);
	write_dos_reg(addr, (r & (~mask)));
}

void dos_reg_write_bits(u32 reg, u32 val, int start, int len)
{
	u32 to_val = read_dos_reg(reg);
	u32 mask = (((1L << (len)) - 1) << (start));

	to_val &= ~mask;
	to_val |= (val << start) & mask;
	write_dos_reg(reg, to_val);
}

void dos_enable(void)
{
	WRITE_VREG_BITS(DOS_GCLK_EN0, 0x3ff, 0, 10);

	WRITE_VREG(GCLK_EN, 0x3ff);

	CLEAR_VREG_MASK(MDEC_PIC_DC_CTRL, (1 << 31));
}

void aml_vdec_reset_core(void)
{
	unsigned int mask = 0;

	mask = (1 << 21);

	write_dmc_reg(0x0, (read_dmc_reg(0x0) & ~mask));
	usleep_range(60, 70);
	WRITE_VREG(DOS_SW_RESET0, (1 << 3) | (1 << 4) |
		   (1 << 5) | (1 << 7) | (1 << 8) | (1 << 9));
	WRITE_VREG(DOS_SW_RESET0, 0);
	CLEAR_VREG_MASK(VDEC_ASSIST_MMC_CTRL1, 1 << 3);
	CLEAR_VREG_MASK(MDEC_PIC_DC_MUX_CTRL, 1 << 31);
	WRITE_VREG(MDEC_EXTIF_CFG1, 0);

	write_dmc_reg(0x0, (read_dmc_reg(0x0) | mask));
}

void aml_start_vdec_hw(void)
{
	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);

	WRITE_VREG(DOS_SW_RESET0, (1 << 12) | (1 << 11));
	WRITE_VREG(DOS_SW_RESET0, 0);

	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);

	WRITE_VREG(MPSR, 0x0001);
}

void aml_stop_vdec_hw(void)
{
	ulong timeout = jiffies + HZ / 10;

	WRITE_VREG(MPSR, 0);
	WRITE_VREG(CPSR, 0);

	while (RD_VREG(IMEM_DMA_CTRL) & 0x8000) {
		if (time_after(jiffies, timeout))
			break;
	}

	timeout = jiffies + HZ / 10;
	while (RD_VREG(LMEM_DMA_CTRL) & 0x8000) {
		if (time_after(jiffies, timeout))
			break;
	}

	timeout = jiffies + HZ / 80;
	while (RD_VREG(WRRSP_LMEM) & 0xfff) {
		if (time_after(jiffies, timeout)) {
			pr_err("%s, ctrl %x, rsp %x, pc %x status %x,%x\n",
			       __func__, RD_VREG(LMEM_DMA_CTRL),
			       RD_VREG(WRRSP_LMEM), RD_VREG(MPC_E),
			       RD_VREG(AV_SCRATCH_J), RD_VREG(AV_SCRATCH_9));
			break;
		}
	}

	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);

	WRITE_VREG(DOS_SW_RESET0, (1 << 12) | (1 << 11));
	WRITE_VREG(DOS_SW_RESET0, 0);

	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);
	RD_VREG(DOS_SW_RESET0);
}

int load_firmware(struct aml_vdec_hw *hw, const char *path)
{
	const struct firmware *fw;
	struct device *dev = hw->dev;
	static u8 *mc_addr;
	static dma_addr_t mc_addr_map;
	int fw_head_len;
	ulong timeout;
	int ret;

	ret = request_firmware(&fw, path, dev);
	if (ret < 0) {
		pr_info("request_firmware %s failed ret %d\n", path, ret);
		return -EINVAL;
	}

	if (fw->size > MC_SIZE) {
		pr_info("fw %s oversize\n", path);
		ret = -EINVAL;
		goto release_firmware;
	}

	fw_head_len = 512;
	mc_addr = dma_alloc_coherent(dev, MC_SIZE, &mc_addr_map, GFP_KERNEL);
	if (!mc_addr) {
		pr_info("no mem for fw %s\n", path);
		ret = -ENOMEM;
		goto release_firmware;
	}
	memset(mc_addr, 0, MC_SIZE);
	memcpy(mc_addr, ((u8 *)fw->data + fw_head_len),
	       (fw->size - fw_head_len));

	WRITE_VREG(MPSR, 0);
	WRITE_VREG(CPSR, 0);

	timeout = RD_VREG(MPSR);
	timeout = RD_VREG(MPSR);
	timeout = jiffies + HZ;

	WRITE_VREG(IMEM_DMA_ADR, mc_addr_map);
	WRITE_VREG(IMEM_DMA_COUNT, 0x1000);
	WRITE_VREG(IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	while (RD_VREG(IMEM_DMA_CTRL) & 0x8000) {
		if (time_before(jiffies, timeout)) {
			schedule();
		} else {
			pr_info("vdec load mc error\n");
			ret = -EBUSY;
			break;
		}
	}

	/* Only h264 needs this step */
	if (hw->hw_ops.load_firmware_ex) {
		ret = hw->hw_ops.load_firmware_ex(hw->curr_ctx,
						  mc_addr,
						  (fw->size - fw_head_len));
		if (ret < 0) {
			ret = -EINVAL;
			goto free_dma_mem;
		}
	}

free_dma_mem:
	dma_free_coherent(dev, MC_SIZE, mc_addr, mc_addr_map);
release_firmware:
	release_firmware(fw);
	return ret;
}

static int vdec_clock_gate_init(struct aml_vdec_hw *hw)
{
	int i;

	hw->gates[VDEC].name = "vdec";
	hw->gates[VDEC_MUX].name = "clk_vdec_mux";
	hw->gates[HEVCF_MUX].name = "clk_hevcf_mux";

	for (i = 0; i < DOS_CLK_MAX; i++) {
		hw->gates[i].clk = devm_clk_get(hw->dev, hw->gates[i].name);
		hw->gates[i].ref_count = 0;
		mutex_init(&hw->gates[i].mutex);
	}

	return 0;
}

static int vdec_gate_clk(struct gate_switch_node *gate_node, bool enable)
{
	int ret = 0;

	mutex_lock(&gate_node->mutex);
	if (enable) {
		gate_node->ref_count++;
		if (gate_node->ref_count == 1) {
			ret = clk_prepare_enable(gate_node->clk);
			pr_debug("the %-15s clock on, ref cnt: %d ret = %d\n",
				 gate_node->name, gate_node->ref_count, ret);
		}
	} else {
		gate_node->ref_count--;
		if (!gate_node->ref_count) {
			clk_disable_unprepare(gate_node->clk);
			pr_debug("the %-15s clock off, ref cnt: %d ret = %d\n",
				 gate_node->name, gate_node->ref_count, ret);
		}
	}
	mutex_unlock(&gate_node->mutex);

	return 0;
}

static int vdec_switch_gate(struct aml_vdec_hw *hw,
			    const char *name, bool enable)
{
	int i;

	for (i = 0; i < DOS_CLK_MAX; i++) {
		if (!strcmp(name, hw->gates[i].name)) {
			if (hw->gates[i].clk)
				vdec_gate_clk(&hw->gates[i], enable);
			else
				return -ENODEV;
		}
	}
	return 0;
}

static int vdec_set_rate(struct aml_vdec_hw *hw,
			 const char *name, unsigned long hz)
{
	int i;

	for (i = 0; i < DOS_CLK_MAX; i++) {
		if (!strcmp(name, hw->gates[i].name)) {
			if (hw->gates[i].clk && hw->gates[i].ref_count) {
				clk_set_rate(hw->gates[i].clk, hz);
				pr_debug("after set, vdec mux clock is %lu Hz\n",
					 clk_get_rate(hw->gates[i].clk));
			} else {
				return -ENODEV;
			}
		}
	}
	return 0;
}

static void pm_vdec_clock_switch(struct aml_vdec_hw *hw, int id, bool on)
{
	int ret = 0;

	if (id == VDEC_1) {
		ret = vdec_switch_gate(hw, "clk_vdec_mux", on);
		vdec_set_rate(hw, "clk_vdec_mux", 499999992);
	} else if (id == VDEC_HEVC) {
		/* enable hevc clock */
		ret = vdec_switch_gate(hw, "clk_hevcf_mux", on);
		if (ret == -ENODEV)
			ret = vdec_switch_gate(hw, "clk_hevc_mux", on);
	}

	if (ret < 0)
		pr_debug("clk %d, unreachable ret %d\n", id, ret);
}

static void pm_vdec_power_switch(struct pm_pd_s *pd, int id, bool on)
{
	struct device *dev = pd[id].dev;
	int ret;

	if (!dev) {
		pr_debug("no dev %d, maybe always on\n", id);
		return;
	}

	if (on)
		ret = pm_runtime_get_sync(dev);
	else
		ret = pm_runtime_put_sync(dev);

	pr_debug("dev: %p link %p the %-15s power %s ret %d\n",
		 dev, pd[id].link, pd[id].name, on ? "on" : "off", ret);
}

static int pm_vdec_power_domain_init(struct aml_vdec_hw *hw)
{
	int i, err;
	const struct power_manager_s *pm = hw->pm;
	struct pm_pd_s *pd = pm->pd_data;

	mutex_init(&hw->pm_mutex);

	for (i = 0; i < VDEC_MAX; i++) {
		pd[i].dev = dev_pm_domain_attach_by_name(hw->dev, pd[i].name);
		if (IS_ERR_OR_NULL(pd[i].dev)) {
			err = PTR_ERR(pd[i].dev);
			pr_debug("Get %s failed, pm-domain: %d\n",
				 pd[i].name, err);
			continue;
		}

		pd[i].link = device_link_add(hw->dev, pd[i].dev,
					     DL_FLAG_PM_RUNTIME |
					     DL_FLAG_STATELESS);
		if (IS_ERR_OR_NULL(pd[i].link)) {
			pr_err("Adding %s device link failed!\n", pd[i].name);
			return -ENODEV;
		}

		pr_debug("power domain: name: %s, dev: %p, link: %p\n",
			 pd[i].name, pd[i].dev, pd[i].link);
	}

	return 0;
}

static void pm_vdec_power_domain_release(struct aml_vdec_hw *hw)
{
	int i;
	const struct power_manager_s *pm = hw->pm;
	struct pm_pd_s *pd = pm->pd_data;

	for (i = 0; i < VDEC_MAX; i++) {
		if (!IS_ERR_OR_NULL(pd[i].link))
			device_link_del(pd[i].link);

		if (!IS_ERR_OR_NULL(pd[i].dev))
			dev_pm_domain_detach(pd[i].dev, true);
	}
}

static void dos_local_config(bool is_on, int id)
{
	if (is_on) {
		usleep_range(20, 100);

		switch (id) {
		case VDEC_1:
			WRITE_VREG(DOS_MEM_PD_VDEC, 0);
			WRITE_VREG(DOS_SW_RESET0, 0xfffffffc);
			usleep_range(20, 100);
			WRITE_VREG(DOS_SW_RESET0, 0);
			usleep_range(20, 100);
			WRITE_VREG(DOS_MEM_PD_VDEC, 0);
			break;
		case VDEC_HEVC:
			WRITE_VREG(DOS_MEM_PD_HEVC, 0);
			WRITE_VREG(DOS_SW_RESET3, 0xffffffff);
			usleep_range(20, 100);
			WRITE_VREG(DOS_SW_RESET3, 0);
			usleep_range(20, 100);
			WRITE_VREG(DOS_MEM_PD_HEVC, 0);
			break;
		default:
			pr_debug("%s on, not found id %d\n", __func__, id);
			break;
		}
	} else {
		switch (id) {
		case VDEC_1:
			WRITE_VREG(DOS_SW_RESET0, 0xfffffffc);
			usleep_range(20, 100);
			WRITE_VREG(DOS_SW_RESET0, 0);
			usleep_range(20, 100);
			WRITE_VREG(DOS_MEM_PD_VDEC, 0xffffffffUL);
			break;
		case VDEC_HEVC:
			WRITE_VREG(DOS_SW_RESET3, 0xffffffff);
			usleep_range(20, 100);
			WRITE_VREG(DOS_SW_RESET3, 0);
			usleep_range(20, 100);
			WRITE_VREG(DOS_MEM_PD_HEVC, 0xffffffffUL);
			break;
		default:
			pr_debug("%s off, not found id %d\n", __func__, id);
			break;
		}
	}

	pr_debug("%s end, id %d, is_on %d\n", __func__, id, is_on);
}

static void pm_vdec_power_domain_power_on(struct aml_vdec_hw *hw, int id)
{
	const struct power_manager_s *pm = hw->pm;

	pm_vdec_clock_switch(hw, id, true);
	pm_vdec_power_switch(pm->pd_data, id, true);

	dos_local_config(1, id);
}

static void pm_vdec_power_domain_power_off(struct aml_vdec_hw *hw, int id)
{
	const struct power_manager_s *pm = hw->pm;

	dos_local_config(0, id);

	pm_vdec_clock_switch(hw, id, false);
	pm_vdec_power_switch(pm->pd_data, id, false);
}

static bool pm_vdec_power_domain_power_state(struct aml_vdec_hw *hw, int id)
{
	if (hw->pm->pd_data[id].dev)
		return pm_runtime_active(hw->pm->pd_data[id].dev);
	else
		return false;
}

static void vdec_poweron(struct aml_vdec_hw *hw, enum vdec_type_e core)
{
	if (core >= VDEC_MAX)
		return;

	mutex_lock(&hw->pm_mutex);
	if (!hw->pm->pd_data[core].dev)
		goto out;

	hw->pm->pd_data[core].ref_count++;
	if (hw->pm->pd_data[core].ref_count > 1)
		goto out;

	if (hw->pm->power_state(hw, core))
		goto out;

	hw->pm->power_on(hw, core);

out:
	mutex_unlock(&hw->pm_mutex);
}

static void vdec_poweroff(struct aml_vdec_hw *hw, enum vdec_type_e core)
{
	if (core >= VDEC_MAX)
		return;

	mutex_lock(&hw->pm_mutex);
	if (hw->pm->pd_data[core].ref_count == 0)
		goto out;

	hw->pm->pd_data[core].ref_count--;
	if (hw->pm->pd_data[core].ref_count > 0)
		goto out;

	hw->pm->power_off(hw, core);

out:
	mutex_unlock(&hw->pm_mutex);
}

int vdec_enable(struct aml_vdec_hw *hw)
{
	vdec_switch_gate(hw, "vdec", 1);
	vdec_poweron(hw, VDEC_1);

	return 0;
}

int vdec_disable(struct aml_vdec_hw *hw)
{
	vdec_poweroff(hw, VDEC_1);
	vdec_switch_gate(hw, "vdec", 0);

	return 0;
}

static const struct power_manager_s pm[] = {
	[AML_PM_PD] = {
		.pd_data    = vdec_domain_data,
		.init        = pm_vdec_power_domain_init,
		.release    = pm_vdec_power_domain_release,
		.power_on    = pm_vdec_power_domain_power_on,
		.power_off    = pm_vdec_power_domain_power_off,
		.power_state = pm_vdec_power_domain_power_state,
	},
};

static irqreturn_t vdec_irq_handler(int irq, void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_vdec_hw *hw = dev->dec_hw;
	irqreturn_t ret;

	if (hw->hw_ops.irq_handler)
		ret = hw->hw_ops.irq_handler(irq, priv);

	return ret;
}

static irqreturn_t vdec_threaded_isr_handler(int irq, void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_vdec_hw *hw = dev->dec_hw;
	irqreturn_t ret = IRQ_HANDLED;

	if (hw->hw_ops.irq_threaded_func)
		ret = hw->hw_ops.irq_threaded_func(irq, priv);

	return ret;
}

struct aml_vdec_hw *vdec_get_hw(void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;

	return dev->dec_hw;
}

int dev_request_hw_resources(void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_vdec_hw *hw;
	struct platform_device *pdev;
	struct resource res;
	u32 res_size;
	int i;
	int ret = -1;

	if (!dev || !dev->dec_hw) {
		pr_err("%s param invalid\n", __func__);
		return -1;
	}
	pdev = dev->plat_dev;
	hw = dev->dec_hw;
	hw->dev = &pdev->dev;

	hw->dec_irq = platform_get_irq(pdev, VDEC_IRQ_1);
	if (hw->dec_irq < 0) {
		dev_err(&pdev->dev, "get irq failed\n");
		return hw->dec_irq;
	}
	ret = devm_request_threaded_irq(&pdev->dev, hw->dec_irq, vdec_irq_handler,
					vdec_threaded_isr_handler, IRQF_ONESHOT,
					"vdec-1", dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to install irq %d (%d)",
			hw->dec_irq, ret);
		return -1;
	}

	for (i = 0; i < MAX_REG_BUS; i++) {
		if (of_address_to_resource(pdev->dev.of_node, i, &res)) {
			pr_err("of_address_to_resource %d failed\n", i);
			return -EINVAL;
		}

		res_size = resource_size(&res);
		hw->regs[i] = ioremap(res.start, res_size);
		reg_base[i] = hw->regs[i];

		pr_debug("%s, res start %llx, end %llx, iomap: %p\n",
			 __func__, (unsigned long long)res.start,
			 (unsigned long long)res.end, reg_base[i]);
	}

	hw->pm = &pm[dev->pvdec_data->power_type];
	if (hw->pm->init) {
		ret = hw->pm->init(hw);
		if (ret < 0) {
			pr_err("power mgr init failed!\n");
			return ret;
		}
	}
	vdec_clock_gate_init(hw);

	aml_vdec_canvas_register(hw);

	pr_debug("##Amlogic hw resource init OK##\n");

	return 0;
}

void dev_destroy_hw_resources(void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_vdec_hw *hw;

	if (!dev || !dev->dec_hw) {
		pr_err("%s param invalid\n", __func__);
		return;
	}
	hw = dev->dec_hw;

	if (hw->pm->release)
		hw->pm->release(hw);

	kfree(hw);
	dev->dec_hw = NULL;
	pr_debug("##Amlogic hw resource release OK##\n");
}
