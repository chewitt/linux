// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2026 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/soc.h>

#include "audin.h"
#include "gx-formatter.h"

static struct snd_soc_dapm_widget audin_dapm_widgets[] = {
	SND_SOC_DAPM_PGA_E("I2S Formatter", SND_SOC_NOPM, 0, 0, NULL, 0,
			   gx_formatter_event,
			   (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD)),
};

static const struct snd_soc_component_driver audin_component = {
	.dapm_widgets		= audin_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(audin_dapm_widgets),
};

static const struct regmap_config audin_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x304,
};

static int meson_gx_audin_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *mmio;
	struct regmap *regmap;
	struct clk *clk;
	int ret;

	ret = device_reset(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to reset device\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return dev_err_probe(dev, -EINVAL, "Failed to get memory resource\n");

	/*
	 * Do not use devm_platform_ioremap_resource() here: it would claim the
	 * whole AUDIN window exclusively and the FIFO children would then fail
	 * to request their own sub-ranges.
	 */
	mmio = devm_ioremap(dev, res->start, resource_size(res));
	if (!mmio)
		return dev_err_probe(dev, -ENOMEM, "Failed to remap memory\n");

	regmap = devm_regmap_init_mmio(dev, mmio, &audin_regmap_cfg);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to init regmap\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Failed to get clock\n");

	ret = gx_formatter_create(dev, &audin_dapm_widgets[0], &audin_formatter_i2s_drv, regmap);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to allocate formatter\n");

	ret = devm_snd_soc_register_component(dev, &audin_component, NULL, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register component\n");

	ret = devm_of_platform_populate(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to probe child nodes\n");

	return 0;
}

static void meson_gx_audin_remove(struct platform_device *pdev)
{
	gx_formatter_free(&audin_dapm_widgets[0]);
}

static const struct of_device_id meson_gx_audin_of_match[] = {
	{ .compatible = "amlogic,meson-gxbb-audin" },
	{ .compatible = "amlogic,meson-gxl-audin" },
	{}
};
MODULE_DEVICE_TABLE(of, meson_gx_audin_of_match);

static struct platform_driver meson_gx_audin_driver = {
	.driver = {
		.name = "meson-gx-audin",
		.of_match_table = meson_gx_audin_of_match,
	},
	.probe = meson_gx_audin_probe,
	.remove = meson_gx_audin_remove,
};
module_platform_driver(meson_gx_audin_driver);

MODULE_DESCRIPTION("Meson GX AUDIN driver");
MODULE_AUTHOR("Valerio Setti <vsetti@baylibre.com>");
MODULE_LICENSE("GPL");
