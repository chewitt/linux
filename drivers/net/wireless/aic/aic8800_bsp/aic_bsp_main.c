// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bsp sdio main function
 *
 ******************************************************************************
 */

#include "aic_bsp_driver.h"
#include "aicwf_txq_prealloc.h"
#include <linux/errno.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <generated/utsrelease.h>

#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>

#define DRV_DESCRIPTION "AIC BSP"
#define DRV_COPYRIGHT   "Copyright(c) 2015-2020 AICSemi"
#define DRV_AUTHOR      "AICSemi"

int aicwf_dbg_level_bsp = LOGERROR; // | LOGINFO | LOGDEBUG | LOGTRACE;
module_param(aicwf_dbg_level_bsp, int, 0660);

static struct platform_device *aicbsp_pdev;

const struct aicbsp_firmware fw_8800d80_u02[AICBSP_CPMODE_MAX] = {
	[AICBSP_CPMODE_WORK] = {
		.desc = "normal work mode(8800d80 sdio u02)",
		.wl_fw = "fmacfw_8800d80_u02.bin",
	},
};

const struct aicbsp_firmware *aicbsp_firmware_list = fw_8800d80_u02;

struct aicbsp_info_t aicbsp_info = {
	.hwinfo_r = AICBSP_HWINFO_DEFAULT,
	.hwinfo = AICBSP_HWINFO_DEFAULT,
	.cpmode = AICBSP_CPMODE_DEFAULT,
	.fwlog_en = AICBSP_FWLOG_EN_DEFAULT,
	.adap_test = 0,
#ifdef CONFIG_IRQ_FALL
	.irqf = 1,
#else
	.irqf = 0,
#endif
};

/* mutex for aicbsp power */
struct mutex aicbsp_power_lock;

static struct platform_driver aicbsp_driver = {
	.driver = {
			.owner = THIS_MODULE,
			.name = "aic_bsp",
		},
};

static ssize_t hwinfo_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t count = 0;

	count += sprintf(&buf[count], "chip hw rev: ");
	if (aicbsp_info.hwinfo_r < 0)
		count += sprintf(&buf[count], "-1(not avalible)\n");
	else
		count += sprintf(&buf[count], "0x%02X\n", aicbsp_info.chip_rev);

	count += sprintf(&buf[count], "hwinfo read: ");
	if (aicbsp_info.hwinfo_r < 0)
		count +=
			sprintf(&buf[count], "%d(not avalible), ", aicbsp_info.hwinfo_r);
	else
		count += sprintf(&buf[count], "0x%02X, ", aicbsp_info.hwinfo_r);

	if (aicbsp_info.hwinfo < 0)
		count +=
			sprintf(&buf[count], "set: %d(not avalible)\n", aicbsp_info.hwinfo);
	else
		count += sprintf(&buf[count], "set: 0x%02X\n", aicbsp_info.hwinfo);

	return count;
}

static ssize_t hwinfo_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	long val;
	int err = kstrtol(buf, 0, &val);

	if (err) {
		pr_err("invalid input\n");
		return err;
	}

	if ((val == -1) || (val >= 0 && val <= 0xFF)) {
		aicbsp_info.hwinfo = val;
	} else {
		pr_err("invalid values\n");
		return -EINVAL;
	}
	return count;
}

static ssize_t fwdebug_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	ssize_t count = 0;

	count += sprintf(&buf[count], "fw log status: %s\n",
					 aicbsp_info.fwlog_en ? "on" : "off");

	return count;
}

static ssize_t fwdebug_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	long val;
	int err = kstrtol(buf, 0, &val);

	if (err) {
		pr_err("invalid input\n");
		return err;
	}

	if (val > 1 || val < 0) {
		pr_err("must be 0 or 1\n");
		return -EINVAL;
	}

	aicbsp_info.fwlog_en = val;
	return count;
}

static ssize_t adapt_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	ssize_t count = 0;

	count += sprintf(&buf[count], "adapt status: %s\n",
					 aicbsp_info.adap_test ? "on" : "off");

	return count;
}

static ssize_t adapt_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	long val;
	int err = kstrtol(buf, 0, &val);

	if (err) {
		pr_err("invalid input\n");
		return err;
	}

	if (val > 1 || val < 0) {
		pr_err("must be 0 or 1\n");
		return err;
	}

	aicbsp_info.adap_test = val;
	return count;
}

static DEVICE_ATTR_RW(hwinfo);

static DEVICE_ATTR_RW(fwdebug);

static DEVICE_ATTR_RW(adapt);

static struct attribute *aicbsp_attributes[] = {
	&dev_attr_hwinfo.attr,
	&dev_attr_fwdebug.attr,
	&dev_attr_adapt.attr,
	NULL,
};

static struct attribute_group aicbsp_attribute_group = {
	.name = "aicbsp_info",
	.attrs = aicbsp_attributes,
};

int testmode = AICBSP_CPMODE_DEFAULT;
int adap_test;
module_param(adap_test, int, 0660);

#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
int gpio_pwr;
bool gpio_pwr_assert;

void nxp_pwr_ctrl(int value)
{
	pr_info("aic_bsp: %s: %d\n", __func__, value);
	gpio_set_value(gpio_pwr, value);
}

static int aicbsp_pwr_node(void)
{
	struct device_node *np;
	int err;

	np = of_find_compatible_node(NULL, NULL, "aic,wifi-power");
	if (!np) {
		pr_err("aic_bsp: %s np not found\n", __func__);
		return -ENODEV;
	}

	gpio_pwr = of_get_named_gpio(np, "wifi-power", 0);
	if (!gpio_is_valid(gpio_pwr)) {
		pr_err("aic_bsp: %s is invalid\n", __func__);
		return -ENODEV;
	}

	err = gpio_request(gpio_pwr, "wifi-power");
	if (err < 0) {
		pr_err("aic_bsp: %s can't request gpio_pwr gpio %d\n", __func__,
		       gpio_pwr);
		return -ENODEV;
	}

	err = gpio_direction_output(gpio_pwr, 1);
	if (err < 0) {
		pr_err("aic_bsp: %s can't request output direction gpio_pwr gpio %d\n",
		       __func__, gpio_pwr);
		return -ENODEV;
	}

	return 0;
}
#endif

static int __init aicbsp_init(void)
{
	int ret;

	testmode = AICBSP_CPMODE_WORK;
	aicbsp_info.cpmode = AICBSP_CPMODE_WORK;

	pr_info("AIC_BSP driver, built against Linux %s\n", UTS_RELEASE);

	aicbsp_resv_mem_init();
	ret = platform_driver_register(&aicbsp_driver);
	if (ret) {
		pr_err("register platform driver failed: %d\n", ret);
		return ret;
	}

	aicbsp_pdev = platform_device_alloc("aic-bsp", -1);
	ret = platform_device_add(aicbsp_pdev);
	if (ret) {
		pr_err("register platform device failed: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&aicbsp_pdev->dev.kobj, &aicbsp_attribute_group);
	if (ret) {
		pr_err("register sysfs create group failed!\n");
		return ret;
	}

	mutex_init(&aicbsp_power_lock);

#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
	ret = aicbsp_pwr_node();
	if (ret) {
		pr_err("aic_bsp: aicbsp_pwr_node fail\n");
		return ret;
	}
#endif
	return 0;
}

void aicbsp_sdio_exit(void);

static void __exit aicbsp_exit(void)
{
#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
	gpio_free(gpio_pwr);
#endif

	sysfs_remove_group(&aicbsp_pdev->dev.kobj, &aicbsp_attribute_group);
	platform_device_del(aicbsp_pdev);
	platform_driver_unregister(&aicbsp_driver);
	mutex_destroy(&aicbsp_power_lock);
	aicbsp_resv_mem_deinit();
#ifdef CONFIG_PREALLOC_TXQ
	aicwf_prealloc_txq_free();
#endif
}

module_init(aicbsp_init);
module_exit(aicbsp_exit);

MODULE_FIRMWARE(AIC8800_FW_DIR "fmacfw_8800d80_u02.bin");

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");
