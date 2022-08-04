// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phytium DWMAC platform glue driver
 *
 * Copyright (C) 2022 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/acpi.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "stmmac.h"
#include "stmmac_platform.h"

/**
 * Acquire Phytium DWMAC resources from ACPI
 */
static int dwmac_phytium_get_resources(struct platform_device *pdev,
				struct stmmac_resources *stmmac_res)
{
	memset(stmmac_res, 0, sizeof(*stmmac_res));

	stmmac_res->irq = platform_get_irq(pdev, 0);
	if (stmmac_res->irq < 0)
		return stmmac_res->irq;

	stmmac_res->addr = devm_platform_ioremap_resource(pdev, 0);
	stmmac_res->wol_irq = stmmac_res->irq;

	return PTR_ERR_OR_ZERO(stmmac_res->addr);
}

/**
 * Parse Phytium ACPI properties
 */
static struct plat_stmmacenet_data *
dwmac_phytium_parse_config_acpi(struct platform_device *pdev, const char *mac)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *np;
	struct plat_stmmacenet_data *plat;
	struct stmmac_dma_cfg *dma_cfg;
	struct stmmac_axi *axi;
	struct clk_hw *clk_hw;
	u64 clk_freq;
	int ret;

	plat = devm_kzalloc(dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return ERR_PTR(-ENOMEM);

	np = dev_fwnode(dev);

	plat->phy_interface = fwnode_get_phy_mode(np);
	

	/* Get max speed of operation from properties */
	if (fwnode_property_read_u32(np, "max-speed", &plat->max_speed))
		plat->max_speed = 1000;

	if (fwnode_property_read_u32(np, "bus_id", &plat->bus_id))
		plat->bus_id = 2;

	/* Default to PHY auto-detection */
	plat->phy_addr = -1;

	plat->mdio_bus_data = devm_kzalloc(dev,
					   sizeof(struct stmmac_mdio_bus_data),
					   GFP_KERNEL);

	fwnode_property_read_u32(np, "tx-fifo-depth", &plat->tx_fifo_size);
	fwnode_property_read_u32(np, "rx-fifo-depth", &plat->rx_fifo_size);
	if (plat->tx_fifo_size == 0)
		plat->tx_fifo_size = 0x10000;
	if (plat->rx_fifo_size == 0)
		plat->rx_fifo_size = 0x10000;

	plat->force_sf_dma_mode =
		fwnode_property_read_bool(np, "snps,force_sf_dma_mode");

	if (fwnode_property_read_bool(np, "snps,en-tx-lpi-clockgating"))
		plat->flags |= STMMAC_FLAG_EN_TX_LPI_CLOCKGATING;

	/* Set the maxmtu to a default of JUMBO_LEN in case the
	 * parameter is not present.
	 */
	plat->maxmtu = JUMBO_LEN;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	fwnode_property_read_u32(np, "max-frame-size", &plat->maxmtu);
	plat->core_type = DWMAC_CORE_GMAC;
	plat->pmt = 1;

	dma_cfg = devm_kzalloc(dev, sizeof(*dma_cfg), GFP_KERNEL);
	if (!dma_cfg)
		return ERR_PTR(-ENOMEM);
	plat->dma_cfg = dma_cfg;

	fwnode_property_read_u32(np, "snps,pbl", &dma_cfg->pbl);
	if (!dma_cfg->pbl)
		dma_cfg->pbl = DEFAULT_DMA_PBL;

	fwnode_property_read_u32(np, "snps,txpbl", &dma_cfg->txpbl);
	fwnode_property_read_u32(np, "snps,rxpbl", &dma_cfg->rxpbl);
	dma_cfg->pblx8 = !fwnode_property_read_bool(np, "snps,no-pbl-x8");

	dma_cfg->aal = fwnode_property_read_bool(np, "snps,aal");
	dma_cfg->fixed_burst = fwnode_property_read_bool(np, "snps,fixed-burst");
	dma_cfg->mixed_burst = fwnode_property_read_bool(np, "snps,mixed-burst");

	plat->force_thresh_dma_mode = fwnode_property_read_bool(np, "snps,force_thresh_dma_mode");
	if (plat->force_thresh_dma_mode)
		plat->force_sf_dma_mode = 0;

	fwnode_property_read_u32(np, "snps,ps-speed", &plat->mac_port_sel_speed);

	axi = devm_kzalloc(&pdev->dev, sizeof(*axi), GFP_KERNEL);
	if (!axi)
		return ERR_PTR(-ENOMEM);
	plat->axi = axi;

	axi->axi_wr_osr_lmt = 1;
	axi->axi_rd_osr_lmt = 1;

	plat->rx_queues_to_use = 1;
	plat->tx_queues_to_use = 1;

	/**
	 * First Queue must always be in DCB mode. As MTL_QUEUE_DCB=1 we need
	 * to always set this, otherwise Queue will be classified as AVB
	 * (because MTL_QUEUE_AVB = 0).
	 */
	plat->rx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;

	plat->rx_queues_cfg[0].use_prio = true;

	plat->rx_queues_cfg[0].pkt_route = 0x0;

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_SP;

	ret = fwnode_property_read_u64(np, "clock-frequency", &clk_freq);
	if (ret < 0)
		clk_freq = 125000000; /* default to 125MHz */

	clk_hw = clk_hw_register_fixed_rate(dev, dev_name(dev), NULL,
					    0, clk_freq);
	if (IS_ERR(clk_hw))
		return ERR_PTR(PTR_ERR(clk_hw));
	ret = devm_clk_hw_register_clkdev(dev, clk_hw, dev_name(dev),
					  dev_name(dev));
	if (ret)
		return ERR_PTR(ret);
	plat->stmmac_clk = clk_hw->clk;
	clk_prepare_enable(plat->stmmac_clk);

	return plat;
}

static int dwmac_phytium_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	int ret;

	ret = dwmac_phytium_get_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	if (has_acpi_companion(&pdev->dev)) {
		plat_dat = dwmac_phytium_parse_config_acpi(pdev, stmmac_res.mac);
		if (IS_ERR(plat_dat)) {
			dev_err(&pdev->dev, "ACPI configuration failed\n");
			return PTR_ERR(plat_dat);
		}
	} else {
		dev_err(&pdev->dev, "no ACPI properties\n");
		return -EINVAL;
	}

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_exit;

	return 0;

err_exit:
	if (plat_dat->exit)
		plat_dat->exit(&pdev->dev, plat_dat->bsp_priv);

	return ret;
}

static const struct acpi_device_id dwmac_phytium_acpi_match[] = {
	{
		.id = "PHYT0004",
	},
	{}
};
MODULE_DEVICE_TABLE(acpi, dwmac_phytium_acpi_match);

static struct platform_driver dwmac_phytium_driver = {
	.probe  = dwmac_phytium_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "dwmac-phytium",
		.pm		= &stmmac_pltfr_pm_ops,
		.acpi_match_table = ACPI_PTR(dwmac_phytium_acpi_match),
	},
};
module_platform_driver(dwmac_phytium_driver);

MODULE_DESCRIPTION("Glue driver for Phytium DWMAC");
MODULE_LICENSE("GPL v2");
