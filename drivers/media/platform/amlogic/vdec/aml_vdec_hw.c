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
#include <linux/iopoll.h>

#include "aml_vdec_drv.h"
#include "aml_vdec_hw.h"
#include "aml_vdec_platform.h"

#define MHz     (1000000)
#define MC_SIZE (4096 * 16)

static struct pm_pd_s vdec_domain_data[] = {
	[VDEC_1] = {.name = "vdec", },
	[VDEC_HEVC] = {.name = "hevc", },
};

u32 read_dos_reg(struct aml_vdec_hw *hw, u32 addr)
{
	u32 ret_val;

	regmap_read(hw->map[DOS_BUS], addr, &ret_val);

	return ret_val;
}

static void dos_reg_write_bits(struct aml_vdec_hw *hw, u32 reg, u32 val, int start, int len)
{
	u32 mask = (((1L << (len)) - 1) << (start));

	regmap_update_bits(hw->map[DOS_BUS], reg, mask, val);
}

void dos_enable(struct aml_vdec_hw *hw)
{
	dos_reg_write_bits(hw, DOS_GCLK_EN0, 0x3ff, 0, 10);

	regmap_write(hw->map[DOS_BUS], GCLK_EN, 0x3ff);

	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_CTRL, (1 << 31), 0);
}

void aml_vdec_reset_core(struct aml_vdec_hw *hw)
{
	unsigned int mask = 0;

	mask = (1 << 21);

	regmap_update_bits(hw->map[DMC_BUS], 0x0, mask, 0);
	usleep_range(60, 70);
	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0,
		     (1 << 3) | (1 << 4) | (1 << 5) | (1 << 7) |
		     (1 << 8) | (1 << 9));
	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0);
	regmap_update_bits(hw->map[DOS_BUS], VDEC_ASSIST_MMC_CTRL1, 1 << 3, 0);
	regmap_update_bits(hw->map[DOS_BUS], MDEC_PIC_DC_MUX_CTRL, 1 << 31, 0);
	regmap_write(hw->map[DOS_BUS], MDEC_EXTIF_CFG1, 0);

	regmap_update_bits(hw->map[DMC_BUS], 0x0, mask, mask);
}

void aml_start_vdec_hw(struct aml_vdec_hw *hw)
{
	u32 reg_read_val;

	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_read_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_read_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_read_val);

	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, (1 << 12) | (1 << 11));
	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0);

	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_read_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_read_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_read_val);

	regmap_write(hw->map[DOS_BUS], MPSR, 0x0001);
}

void aml_stop_vdec_hw(struct aml_vdec_hw *hw)
{
	u32 reg_val = 0;
	int ret;

	regmap_write(hw->map[DOS_BUS], MPSR, 0);
	regmap_write(hw->map[DOS_BUS], CPSR, 0);

	ret = read_poll_timeout_atomic(read_dos_reg, reg_val,
				       !(reg_val & 0x8000),
				       10, 100000, true,
				       hw, IMEM_DMA_CTRL);

	ret = read_poll_timeout_atomic(read_dos_reg, reg_val,
				       !(reg_val & 0x8000),
				       10, 100000, true,
				       hw, LMEM_DMA_CTRL);

	ret = read_poll_timeout_atomic(read_dos_reg, reg_val,
				       !(reg_val & 0xfff),
				       10, 800000, true,
				       hw, WRRSP_LMEM);
	if (ret)
		dev_err(hw->dev, "%s, ctrl %x, rsp %x, pc %x status %x,%x\n",
			__func__, read_dos_reg(hw, LMEM_DMA_CTRL),
			read_dos_reg(hw, WRRSP_LMEM), read_dos_reg(hw, MPC_E),
			read_dos_reg(hw, AV_SCRATCH_J), read_dos_reg(hw, AV_SCRATCH_9));

	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_val);

	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, (1 << 12) | (1 << 11));
	regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0);

	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_val);
	regmap_read(hw->map[DOS_BUS], DOS_SW_RESET0, &reg_val);
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
		dev_info(dev, "request_firmware %s failed ret %d\n", path, ret);
		return -EINVAL;
	}

	if (fw->size > MC_SIZE) {
		dev_info(dev, "fw %s oversize\n", path);
		ret = -EINVAL;
		goto release_firmware;
	}

	fw_head_len = 512;
	mc_addr = dma_alloc_coherent(dev, MC_SIZE, &mc_addr_map, GFP_KERNEL);
	if (!mc_addr) {
		dev_info(dev, "no mem for fw %s\n", path);
		ret = -ENOMEM;
		goto release_firmware;
	}
	memset(mc_addr, 0, MC_SIZE);
	memcpy(mc_addr, ((u8 *)fw->data + fw_head_len),
	       (fw->size - fw_head_len));

	regmap_write(hw->map[DOS_BUS], MPSR, 0);
	regmap_write(hw->map[DOS_BUS], CPSR, 0);

	timeout = read_dos_reg(hw, MPSR);
	timeout = read_dos_reg(hw, MPSR);

	timeout = jiffies + HZ;

	regmap_write(hw->map[DOS_BUS], IMEM_DMA_ADR, mc_addr_map);
	regmap_write(hw->map[DOS_BUS], IMEM_DMA_COUNT, 0x1000);
	regmap_write(hw->map[DOS_BUS], IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	while (read_dos_reg(hw, IMEM_DMA_CTRL) & 0x8000) {
		if (time_before(jiffies, timeout)) {
			schedule();
		} else {
			dev_info(dev, "vdec load mc error\n");
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
	hw->gates[VDEC].id = "vdec";
	hw->gates[VDEC_MUX].id = "clk_vdec_mux";
	hw->gates[HEVCF_MUX].id = "clk_hevcf_mux";

	return devm_clk_bulk_get(hw->dev, DOS_CLK_MAX, hw->gates);
}

static struct clk_bulk_data *vdec_get_clk_by_name(struct aml_vdec_hw *hw,
						  const char *name)
{
	int i;

	for (i = 0; i < DOS_CLK_MAX; i++) {
		if (!strcmp(name, hw->gates[i].id)) {
			if (hw->gates[i].clk)
				return &hw->gates[i];
		}
	}
	return NULL;
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
			dev_dbg(hw->dev, "Get %s failed, pm-domain: %d\n",
				pd[i].name, err);
			continue;
		}

		pd[i].link = device_link_add(hw->dev, pd[i].dev,
					     DL_FLAG_PM_RUNTIME |
					     DL_FLAG_STATELESS);
		if (IS_ERR_OR_NULL(pd[i].link)) {
			dev_err(hw->dev, "Adding %s device link failed!\n", pd[i].name);
			return -ENODEV;
		}

		dev_dbg(hw->dev, "power domain: name: %s, dev: %p, link: %p\n",
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

static void dos_local_config(struct aml_vdec_hw *hw, bool is_on, int id)
{
	if (is_on) {
		usleep_range(20, 100);

		switch (id) {
		case VDEC_1:
			regmap_write(hw->map[DOS_BUS], DOS_MEM_PD_VDEC, 0);
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0xfffffffc);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_MEM_PD_VDEC, 0);
			break;
		case VDEC_HEVC:
			regmap_write(hw->map[DOS_BUS], DOS_MEM_PD_HEVC, 0);
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET3, 0xffffffff);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET3, 0);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_MEM_PD_HEVC, 0);
			break;
		default:
			dev_info(hw->dev, "%s on, not found id %d\n", __func__, id);
			break;
		}
	} else {
		switch (id) {
		case VDEC_1:
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0xfffffffc);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET0, 0);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_MEM_PD_VDEC, 0xffffffffUL);
			break;
		case VDEC_HEVC:
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET3, 0xffffffff);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_SW_RESET3, 0);
			usleep_range(20, 100);
			regmap_write(hw->map[DOS_BUS], DOS_MEM_PD_HEVC, 0xffffffffUL);
			break;
		default:
			dev_info(hw->dev, "%s off, not found id %d\n", __func__, id);
			break;
		}
	}

	dev_dbg(hw->dev, "%s end, id %d, is_on %d\n", __func__, id, is_on);
}

static void pm_vdec_power_domain_power_on(struct aml_vdec_hw *hw, int id)
{
	const struct power_manager_s *pm = hw->pm;
	struct device *dev = pm->pd_data[id].dev;
	struct clk_bulk_data *gate_node;

	if (id == VDEC_1)
		gate_node = vdec_get_clk_by_name(hw, "clk_vdec_mux");
	else if (id == VDEC_HEVC)
		gate_node = vdec_get_clk_by_name(hw, "clk_hevcf_mux");

	if (gate_node) {
		clk_prepare_enable(gate_node->clk);
		if (id == VDEC_1) {
			clk_set_rate(gate_node->clk, 499999992);
			dev_dbg(hw->dev, "after set, vdec mux clock is %lu Hz\n",
				clk_get_rate(gate_node->clk));
		}
		dev_dbg(hw->dev, "the %-15s clock on\n", gate_node->id);
	} else {
		dev_info(hw->dev, "clk %d, unreachable\n", id);
	}

	if (dev) {
		pm_runtime_get_sync(dev);
		dev_dbg(dev, "dev: %p link %p the %-15s power on\n",
			dev, pm->pd_data[id].link, pm->pd_data[id].name);
	}

	dos_local_config(hw, 1, id);
}

static void pm_vdec_power_domain_power_off(struct aml_vdec_hw *hw, int id)
{
	const struct power_manager_s *pm = hw->pm;
	struct device *dev = pm->pd_data[id].dev;
	struct clk_bulk_data *gate_node;

	dos_local_config(hw, 0, id);

	if (dev) {
		pm_runtime_put_sync(dev);
		dev_dbg(dev, "dev: %p link %p the %-15s power off\n",
			dev, pm->pd_data[id].link, pm->pd_data[id].name);
	}

	if (id == VDEC_1)
		gate_node = vdec_get_clk_by_name(hw, "clk_vdec_mux");
	else if (id == VDEC_HEVC)
		gate_node = vdec_get_clk_by_name(hw, "clk_hevcf_mux");

	if (gate_node) {
		clk_disable_unprepare(gate_node->clk);
		dev_dbg(hw->dev, "the %-15s clock off\n", gate_node->id);
	} else {
		dev_info(hw->dev, "clk %d, unreachable\n", id);
	}
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
	vdec_poweron(hw, VDEC_1);

	return 0;
}

int vdec_disable(struct aml_vdec_hw *hw)
{
	vdec_poweroff(hw, VDEC_1);

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

static const struct regmap_config dos_regmap_conf = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x10000,
};

static const struct regmap_config dmc_regmap_conf = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x20,
};

int dev_request_hw_resources(void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_vdec_hw *hw;
	struct platform_device *pdev;
	void __iomem *reg_base[MAX_REG_BUS];
	struct resource res;
	int i;
	int ret = -1;

	if (!dev || !dev->dec_hw)
		return -1;

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
			dev_err(&pdev->dev, "of_address_to_resource %d failed\n", i);
			return -EINVAL;
		}
		reg_base[i] = devm_ioremap_resource(&pdev->dev, &res);

		if (IS_ERR(reg_base[i]))
			return PTR_ERR(reg_base[i]);

		if (i == DOS_BUS) {
			hw->map[i] = devm_regmap_init_mmio(&pdev->dev, reg_base[i],
							   &dos_regmap_conf);
		} else if (i == DMC_BUS) {
			hw->map[i] = devm_regmap_init_mmio(&pdev->dev, reg_base[i],
							   &dmc_regmap_conf);
		}

		if (IS_ERR(hw->map[i]))
			return PTR_ERR(hw->map[i]);

		dev_dbg(&pdev->dev, "%s, res start %llx, end %llx, iomap: %p\n",
			__func__, (unsigned long long)res.start,
			(unsigned long long)res.end, reg_base[i]);
	}
	hw->canvas = meson_canvas_get(&pdev->dev);
	if (IS_ERR(&pdev->dev))
		return PTR_ERR(&pdev->dev);

	hw->pm = &pm[dev->pvdec_data->power_type];
	if (hw->pm->init) {
		ret = hw->pm->init(hw);
		if (ret < 0) {
			dev_err(&pdev->dev, "power mgr init failed!\n");
			return ret;
		}
	}

	ret = vdec_clock_gate_init(hw);
	if (ret) {
		dev_err(&pdev->dev, "clk bulk init failed!\n");
		return ret;
	}

	dev_dbg(&pdev->dev, "##Amlogic hw resource init OK##\n");

	return 0;
}

void dev_destroy_hw_resources(void *priv)
{
	struct aml_vdec_dev *dev = (struct aml_vdec_dev *)priv;
	struct aml_vdec_hw *hw;

	if (!dev || !dev->dec_hw)
		return;

	hw = dev->dec_hw;

	if (hw->pm->release)
		hw->pm->release(hw);

	dev_dbg(hw->dev, "##Amlogic hw resource release OK##\n");
}
