// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bsp sdio config
 *
 ******************************************************************************
 */
#include "aicsdio.h"
#include "aic_bsp_driver.h"
#include "aicsdio_txrxif.h"
#include "aic_bsp_export.h"
#include "aic_chip_ops.h"

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/semaphore.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>

static int aicbsp_platform_power_on(void);
static void aicbsp_platform_power_off(void);

struct aic_sdio_dev *aicbsp_sdiodev;
static struct completion *aicbsp_notify_completion;
static struct completion *aicbsp_probe_completion;

static const struct sdio_device_id aicbsp_sdmmc_ids[];
static int bsp_init;
static int bsp_func_init;

#ifdef CONFIG_M2D_OTA_AUTO_SUPPORT
char saved_sdk_ver[64];
module_param_string(saved_sdk_ver, saved_sdk_ver, 64, 0660);
#endif

extern int testmode;

static int aicbsp_dummy_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	if (func && func->num != 2)
		return 0;

	if (aicbsp_notify_completion)
		complete(aicbsp_notify_completion);
	return 0;
}

static void aicbsp_dummy_remove(struct sdio_func *func)
{
}

static struct sdio_driver aicbsp_dummy_sdmmc_driver = {
	.probe = aicbsp_dummy_probe,
	.remove = aicbsp_dummy_remove,
	.name = "aicbsp_dummy_sdmmc",
	.id_table = aicbsp_sdmmc_ids,
};

static int aicbsp_reg_sdio_notify(void *semaphore)
{
	aicbsp_notify_completion = semaphore;
	return sdio_register_driver(&aicbsp_dummy_sdmmc_driver);
}

static void aicbsp_unreg_sdio_notify(void)
{
	mdelay(15);
	sdio_unregister_driver(&aicbsp_dummy_sdmmc_driver);
}

static const char *aicbsp_subsys_name(int subsys)
{
	switch (subsys) {
	case AIC_BLUETOOTH:
		return "AIC_BLUETOOTH";
	case AIC_WIFI:
		return "AIC_WIFI";
	default:
		return "unknown subsys";
	}
}

/**
 * aicbsp_set_subsys - Change the power request of an AIC subsystem
 * @subsys: Subsystem identifier from &enum aicbsp_subsys
 * @state: Requested power state from &enum aicbsp_pwr_state
 *
 * Return: The resulting aggregate power state, or a negative error code.
 */
int aicbsp_set_subsys(int subsys, int state)
{
	static int pre_power_map;
	int cur_power_map;
	int pre_power_state;
	int cur_power_state;

	mutex_lock(&aicbsp_power_lock);
	cur_power_map = pre_power_map;
	if (state)
		cur_power_map |= (1 << subsys);
	else
		cur_power_map &= ~(1 << subsys);

	pre_power_state = pre_power_map > 0;
	cur_power_state = cur_power_map > 0;

	sdio_dbg("%s, subsys: %s, state to: %d\n", __func__,
		 aicbsp_subsys_name(subsys), state);

	if (cur_power_state != pre_power_state) {
		sdio_dbg("%s, power state change to %d dure to %s\n", __func__,
			 cur_power_state, aicbsp_subsys_name(subsys));
		if (cur_power_state) {
			if (aicbsp_platform_power_on() < 0)
				goto err0;
			if (aicbsp_sdio_init())
				goto err1;
			if (aicbsp_driver_fw_init(aicbsp_sdiodev))
				goto err2;
#ifndef CONFIG_FDRV_NO_REG_SDIO
			aicbsp_sdio_release(aicbsp_sdiodev);
#endif
		} else {
			aicbsp_sdio_exit();
			aicbsp_platform_power_off();
		}
	} else {
		sdio_dbg("%s, power state no need to change, current: %d\n", __func__,
			 cur_power_state);
	}
	pre_power_map = cur_power_map;
	mutex_unlock(&aicbsp_power_lock);

	return cur_power_state;

err2:
	aicbsp_sdio_release(aicbsp_sdiodev);
	aicbsp_sdio_exit();

err1:
	aicbsp_platform_power_off();

err0:
	sdio_dbg("%s, fail to set %s power state to %d\n", __func__,
		 aicbsp_subsys_name(subsys), state);
	mutex_unlock(&aicbsp_power_lock);
	return -1;
}
EXPORT_SYMBOL_GPL(aicbsp_set_subsys);

void *aicbsp_get_drvdata(void *args)
{
	(void)args;
	if (aicbsp_sdiodev)
		return aicbsp_sdiodev->bus_if;
	return dev_get_drvdata((const struct device *)args);
}

#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
extern void nxp_pwr_ctrl(int value);
struct mmc_host *g_host;
#endif

static int aicbsp_sdio_probe(struct sdio_func *func,
			     const struct sdio_device_id *id)
{
	struct mmc_host *host;
	struct aic_sdio_dev *sdiodev;
	struct aicwf_bus *bus_if;
	int err = -ENODEV;
	u16 chipid;

	bsp_init = 1;
	bsp_func_init = 1;

	if (!func) {
		sdio_err("%s func is null\n", __func__);
		return err;
	}

	if (!aicbsp_probe_completion) {
		sdio_err("%s bsp_probe_semaphore is null\n", __func__);
		return err;
	}

	sdio_dbg("%s:%d vid:0x%04X  did:0x%04X\n", __func__, func->num,
		 func->vendor, func->device);

	if (func->num != 2) {
		sdio_dbg("%s func num != 2\n", __func__);
		return err;
	}

	host = func->card->host;
	host->caps |= MMC_CAP_NONREMOVABLE;

	func = func->card->sdio_func[1 - 1]; // replace 2 with 1

	sdio_dbg("%s after replace:%d\n", __func__, func->num);

	bus_if =
		kzalloc_obj(*bus_if, GFP_KERNEL);
	if (!bus_if) {
		sdio_err("alloc bus fail\n");
		return -ENOMEM;
	}

	sdiodev =
		kzalloc_obj(*sdiodev, GFP_KERNEL);
	if (!sdiodev) {
		sdio_err("alloc sdiodev fail\n");
		kfree(bus_if);
		return -ENOMEM;
	}
	aicbsp_sdiodev = sdiodev;

	err = aicwf_sdio_chipmatch(func->vendor, func->device, &chipid);
	if (err) {
		sdio_err("unsupported SDIO device %04x:%04x\n",
			 func->vendor, func->device);
		goto fail;
	}
	sdiodev->chipid = chipid;
	sdiodev->chip_ops = aic_chip_ops_select(sdiodev->chipid);
	if (!sdiodev->chip_ops) {
		sdio_err("unsupported AIC chip id %u\n", sdiodev->chipid);
		err = -ENODEV;
		goto fail;
	}

	sdiodev->func = func;
	if (sdiodev->chip_ops->use_func_msg)
		sdiodev->func_msg = func->card->sdio_func[1];
	sdiodev->bus_if = bus_if;
	bus_if->bus_priv.sdio = sdiodev;

	if (sdiodev->chip_ops->use_func_msg) {
		dev_set_drvdata(&sdiodev->func_msg->dev, bus_if);
		pr_info("aicbsp the device is PRODUCT_ID_AIC8800DC\n");
	}
	dev_set_drvdata(&func->dev, bus_if);
	sdiodev->dev = &func->dev;

	if (!sdiodev->chip_ops->use_sdiov3_func)
		err = aicwf_sdio_func_init(sdiodev);
	else
		err = aicwf_sdiov3_func_init(sdiodev);

	if (err < 0) {
		sdio_err("sdio func init fail\n");
		goto fail;
	}

	if (!aicwf_sdio_bus_init(sdiodev)) {
		sdio_err("sdio bus init err\r\n");
		goto fail;
	}

#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
	g_host = host;
#endif

	aicbsp_platform_init(sdiodev);

	complete(aicbsp_probe_completion);

	return 0;
fail:
	aicwf_sdio_func_deinit(sdiodev);
	dev_set_drvdata(&func->dev, NULL);
	if (aicbsp_sdiodev == sdiodev)
		aicbsp_sdiodev = NULL;
	kfree(sdiodev);
	kfree(bus_if);
	return err;
}

static void aicbsp_sdio_remove(struct sdio_func *func)
{
	struct mmc_host *host;
	struct aicwf_bus *bus_if = NULL;
	struct aic_sdio_dev *sdiodev = NULL;

	pr_info("aicbsp %s\n", __func__);

	if (!aicbsp_sdiodev) {
		AICWFDBG(LOGERROR, "%s: allready unregister\n", __func__);
		goto done;
	}
	if (!func) {
		AICWFDBG(LOGERROR, "%s, sdio func is null\n", __func__);
		goto done;
	}

	host = func->card->host;
	host->caps &= ~MMC_CAP_NONREMOVABLE;

	bus_if = aicbsp_get_drvdata(&func->dev);

	if (!bus_if) {
		AICWFDBG(LOGERROR, "%s bus_if is NULL \r\n", __func__);
		goto done;
	}

	sdiodev = bus_if->bus_priv.sdio;
	if (!sdiodev) {
		AICWFDBG(LOGERROR, "%s sdiodev is NULL \r\n", __func__);
		goto done;
	}

	aicwf_sdio_release(sdiodev);
	aicwf_sdio_func_deinit(sdiodev);

	if (bsp_init != 0 && bsp_func_init != 0) {
		dev_set_drvdata(&sdiodev->func->dev, NULL);
		kfree(sdiodev);
	}

done:
	if (bsp_init != 0 && bsp_func_init != 0 && bus_if)
		kfree(bus_if);
	aicbsp_sdiodev = NULL;
	aicbsp_probe_completion = NULL;
}

static int aicbsp_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	int err;
	mmc_pm_flag_t sdio_flags;

	//sdio_dbg("%s, func->num = %d\n", __func__, func->num);
	if (func->num != 2)
		return 0;

	sdio_flags = sdio_get_host_pm_caps(func);
	if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
		sdio_dbg("%s: can't keep power while host is suspended\n", __func__);
		return -EINVAL;
	}

	/* keep power while host suspended */
	err = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (err) {
		sdio_dbg("%s: error while trying to keep power\n", __func__);
		return err;
	}

	return 0;
}

static int aicbsp_sdio_resume(struct device *dev)
{
	return 0;
}

static const struct sdio_device_id aicbsp_sdmmc_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_AIC8800D80,
		      SDIO_DEVICE_ID_AIC8800D80_FUNC2) },
	{ }
};

MODULE_DEVICE_TABLE(sdio, aicbsp_sdmmc_ids);

static const struct dev_pm_ops aicbsp_sdio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aicbsp_sdio_suspend, aicbsp_sdio_resume)};

static struct sdio_driver aicbsp_sdio_driver = {
	.probe = aicbsp_sdio_probe,
	.remove = aicbsp_sdio_remove,
	.name = AICBSP_SDIO_NAME,
	.id_table = aicbsp_sdmmc_ids,
	.drv = {
			.pm = &aicbsp_sdio_pm_ops,
		},
};

static int aicbsp_platform_power_on(void)
{
	int ret = 0;
	struct completion aic_chipup_comp;

#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
	if (g_host) {
		nxp_pwr_ctrl(0);
		mdelay(10);
		nxp_pwr_ctrl(1);
		mdelay(50);
		sdio_dbg("mmc detect change\n");
		mmc_detect_change(g_host, 0);
	}
#endif

	init_completion(&aic_chipup_comp);
	ret = aicbsp_reg_sdio_notify(&aic_chipup_comp);
	if (ret) {
		sdio_dbg("%s aicbsp_reg_sdio_notify fail(%d)\n", __func__, ret);
		return ret;
	}

	if (wait_for_completion_timeout(&aic_chipup_comp, msecs_to_jiffies(2000))) {
		aicbsp_unreg_sdio_notify();
		return 0;
	}

	aicbsp_unreg_sdio_notify();

	return -1;
}

static void aicbsp_platform_power_off(void)
{
// TODO wifi disable and sdio card detection

#if defined CONFIG_PLATFORM_EXTERNAL && defined CONFIG_CUSTOM_PWF
	nxp_pwr_ctrl(0);
#endif
}

int aicbsp_sdio_init(void)
{
	struct completion aic_chipup_comp;

	init_completion(&aic_chipup_comp);
	aicbsp_probe_completion = &aic_chipup_comp;

	if (sdio_register_driver(&aicbsp_sdio_driver))
		return -1;

	if (wait_for_completion_timeout(aicbsp_probe_completion, msecs_to_jiffies(2000)) == 0) {
		pr_err("aicbsp %s aicbsp_sdio_probe fail\r\n", __func__);
		return -1;
	}

	return 0;
}

void aicbsp_sdio_exit(void)
{
	sdio_unregister_driver(&aicbsp_sdio_driver);
}

void aicbsp_sdio_release(struct aic_sdio_dev *sdiodev)
{
	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return;
	}

	sdiodev->bus_if->state = BUS_DOWN_ST;
	sdio_claim_host(sdiodev->func);
	sdio_release_irq(sdiodev->func);
	sdio_release_host(sdiodev->func);
	if (sdiodev->chip_ops->use_func_msg) {
		sdio_claim_host(sdiodev->func_msg);
		sdio_release_irq(sdiodev->func_msg);
		sdio_release_host(sdiodev->func_msg);
	}
}

int aicwf_sdio_readb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 *val)
{
	int ret;

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
	*val = sdio_readb(sdiodev->func, regaddr, &ret);
	sdio_release_host(sdiodev->func);
	return ret;
}

int aicwf_sdio_readb_func2(struct aic_sdio_dev *sdiodev, uint regaddr, u8 *val)
{
	int ret;

	if (!sdiodev->func_msg) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func_msg);
	*val = sdio_readb(sdiodev->func_msg, regaddr, &ret);
	sdio_release_host(sdiodev->func_msg);
	return ret;
}

int aicwf_sdio_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val)
{
	int ret;

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
	sdio_writeb(sdiodev->func, val, regaddr, &ret);
	sdio_release_host(sdiodev->func);
	return ret;
}

int aicwf_sdio_writeb_func2(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val)
{
	int ret;

	if (!sdiodev->func_msg) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}
	sdio_claim_host(sdiodev->func_msg);
	sdio_writeb(sdiodev->func_msg, val, regaddr, &ret);
	sdio_release_host(sdiodev->func_msg);
	return ret;
}

int aicwf_sdio_flow_ctrl(struct aic_sdio_dev *sdiodev)
{
	int ret = -1;
	u8 fc_reg = 0;
	u32 count = 0;

	while (true) {
		ret =
			aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.flow_ctrl_reg, &fc_reg);
		if (ret)
			return -1;

		if (sdiodev->chip_ops->need_flowctrl_mask)
			fc_reg &= SDIOWIFI_FLOWCTRL_MASK_REG;

		if (fc_reg != 0) {
			ret = fc_reg;
			return ret;
		}
		if (count >= FLOW_CTRL_RETRY_COUNT) {
			ret = -fc_reg;
			break;
		}
		count++;
		if (count < 30)
			usleep_range(30, 50);
		else if (count < 40)
			mdelay(1);
		else
			mdelay(10);
	}

	return ret;
}

int aicwf_sdio_send_msg(struct aic_sdio_dev *sdiodev, u8 *buf, uint count)
{
	int ret = 0;

	if (!sdiodev->func_msg) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}
	sdio_claim_host(sdiodev->func_msg);
	ret = sdio_writesb(sdiodev->func_msg, 7, buf, count);
	sdio_release_host(sdiodev->func_msg);

	return ret;
}

int aicwf_sdio_send_pkt(struct aic_sdio_dev *sdiodev, u8 *buf, uint count)
{
	int ret = 0;

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
	ret =
		sdio_writesb(sdiodev->func, sdiodev->sdio_reg.wr_fifo_addr, buf, count);
	sdio_release_host(sdiodev->func);

	return ret;
}

int aicwf_sdio_recv_pkt(struct aic_sdio_dev *sdiodev, struct sk_buff *skbbuf,
			u32 size, u8 msg)
{
	int ret;

	if (!skbbuf || !size)
		return -EINVAL;

	if (!msg) {
		if (!sdiodev->func) {
			pr_warn("aicbsp %s, NULL sdio func\n", __func__);
			return 0;
		}
		sdio_claim_host(sdiodev->func);
		ret = sdio_readsb(sdiodev->func, skbbuf->data,
				  sdiodev->sdio_reg.rd_fifo_addr, size);
		sdio_release_host(sdiodev->func);
	} else {
		if (!sdiodev->func_msg) {
			pr_warn("aicbsp %s, NULL sdio func\n", __func__);
			return 0;
		}
		sdio_claim_host(sdiodev->func_msg);
		ret = sdio_readsb(sdiodev->func_msg, skbbuf->data,
				  sdiodev->sdio_reg.rd_fifo_addr, size);
		sdio_release_host(sdiodev->func_msg);
	}

	if (ret < 0)
		return ret;

	skbbuf->len = size;

	return ret;
}

#if defined(CONFIG_SDIO_PWRCTRL)
int aicwf_sdio_wakeup(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;
	int read_retry;
	int write_retry = 20;
	int wakeup_reg_val = 0;

	wakeup_reg_val = sdiodev->chip_ops->wakeup_reg_val;

	if (sdiodev->state == SDIO_SLEEP_ST) {
		down(&sdiodev->pwrctl_wakeup_sema);
		while (write_retry) {
			ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg,
						wakeup_reg_val);
			if (ret) {
				txrx_err("sdio wakeup fail\n");
				ret = -1;
			} else {
				read_retry = 10;
				while (read_retry) {
					u8 val;

					ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.sleep_reg,
							       &val);
					if (ret == 0 && val & 0x10)
						break;

					read_retry--;
					usleep_range(30, 50);
				}
				if (read_retry != 0)
					break;
			}
			sdio_dbg("write retry:  %d\n", write_retry);
			write_retry--;
			usleep_range(30, 50);
		}
		up(&sdiodev->pwrctl_wakeup_sema);
	}

	sdiodev->state = SDIO_ACTIVE_ST;
	aicwf_sdio_pwrctl_timer(sdiodev, sdiodev->active_duration);

	return ret;
}

int aicwf_sdio_sleep_allow(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;
	struct aicwf_bus *bus_if = sdiodev->bus_if;

	if (bus_if->state == BUS_DOWN_ST) {
		ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.sleep_reg, 0x10);
		if (ret)
			sdio_err("Write sleep fail!\n");

		aicwf_sdio_pwrctl_timer(sdiodev, 0);
		return ret;
	}

	if (sdiodev->state == SDIO_ACTIVE_ST) {
		{
			sdio_dbg("s\n");
			ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.sleep_reg, 0x10);
			if (ret)
				sdio_err("Write sleep fail!\n");
		}
		sdiodev->state = SDIO_SLEEP_ST;
		aicwf_sdio_pwrctl_timer(sdiodev, 0);
	} else {
		aicwf_sdio_pwrctl_timer(sdiodev, sdiodev->active_duration);
	}

	return ret;
}

int aicwf_sdio_pwr_stctl(struct aic_sdio_dev *sdiodev, uint target)
{
	int ret = 0;

	if (sdiodev->bus_if->state == BUS_DOWN_ST)
		return -1;

	if (sdiodev->state == target) {
		if (target == SDIO_ACTIVE_ST)
			aicwf_sdio_pwrctl_timer(sdiodev, sdiodev->active_duration);

		return ret;
	}

	switch (target) {
	case SDIO_ACTIVE_ST:
		aicwf_sdio_wakeup(sdiodev);
		break;
	case SDIO_SLEEP_ST:
		aicwf_sdio_sleep_allow(sdiodev);
		break;
	}

	return ret;
}
#endif

int aicwf_sdio_txpkt(struct aic_sdio_dev *sdiodev, struct sk_buff *pkt)
{
	int ret = 0;
	u32 len = 0;
	struct aicwf_bus *bus_if = dev_get_drvdata(sdiodev->dev);

	if (bus_if->state == BUS_DOWN_ST) {
		sdio_dbg("tx bus is down!\n");
		return -EINVAL;
	}

	len = pkt->len;
	len = (len + SDIOWIFI_FUNC_BLOCKSIZE - 1) / SDIOWIFI_FUNC_BLOCKSIZE *
		  SDIOWIFI_FUNC_BLOCKSIZE;
	ret = aicwf_sdio_send_pkt(sdiodev, pkt->data, len);
	if (ret)
		sdio_err("aicwf_sdio_send_pkt fail%d\n", ret);

	return ret;
}

static int aicwf_sdio_intr_get_len_bytemode(struct aic_sdio_dev *sdiodev,
					    u8 *byte_len)
{
	int ret = 0;

	if (!byte_len)
		return -EBADE;

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		*byte_len = 0;
	} else {
		ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.bytemode_len_reg,
				       byte_len);
		sdiodev->rx_priv->data_len = (*byte_len) * 4;
	}

	return ret;
}

static void aicwf_sdio_bus_stop(struct device *dev)
{
	struct aicwf_bus *bus_if = aicbsp_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	int ret;

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwrctl_timer(sdiodev, 0);
	if (sdiodev->pwrctl_tsk) {
		complete_all(&sdiodev->pwrctrl_trgg);
		kthread_stop(sdiodev->pwrctl_tsk);
		sdiodev->pwrctl_tsk = NULL;
	}
#endif
	bus_if->state = BUS_DOWN_ST;
	ret = down_interruptible(&sdiodev->tx_priv->txctl_sema);
	if (ret)
		sdio_err("down txctl_sema fail\n");

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_SLEEP_ST);
#endif
	if (!ret)
		up(&sdiodev->tx_priv->txctl_sema);
	aicwf_frame_queue_flush(&sdiodev->tx_priv->txq);
}

struct sk_buff *aicwf_sdio_readframes(struct aic_sdio_dev *sdiodev, u8 msg)
{
	int ret = 0;
	u32 size = 0;
	struct sk_buff *skb = NULL;
	struct aicwf_bus *bus_if = dev_get_drvdata(sdiodev->dev);

	if (bus_if->state == BUS_DOWN_ST) {
		sdio_dbg("bus down\n");
		return NULL;
	}

	size = sdiodev->rx_priv->data_len;
	skb = __dev_alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return NULL;

	ret = aicwf_sdio_recv_pkt(sdiodev, skb, size, msg);
	if (ret) {
		dev_kfree_skb(skb);
		skb = NULL;
	}

	return skb;
}

static int aicwf_sdio_tx_msg(struct aic_sdio_dev *sdiodev)
{
	int err = 0;
	u16 len;
	u8 *payload = sdiodev->tx_priv->cmd_buf;
	u16 payload_len = sdiodev->tx_priv->cmd_len;
	u8 adjust_str[4] = {0, 0, 0, 0};
	int adjust_len = 0;
	int buffer_cnt = 0;
	u8 retry = 0;

	len = payload_len;
	if ((len % TX_ALIGNMENT) != 0) {
		adjust_len = roundup(len, TX_ALIGNMENT);
		memcpy(payload + payload_len, adjust_str, (adjust_len - len));
		payload_len += (adjust_len - len);
	}
	len = payload_len;

	// link tail is necessary
	if ((len % SDIOWIFI_FUNC_BLOCKSIZE) != 0) {
		memset(payload + payload_len, 0, TAIL_LEN);
		payload_len += TAIL_LEN;
		len = (payload_len / SDIOWIFI_FUNC_BLOCKSIZE + 1) *
			  SDIOWIFI_FUNC_BLOCKSIZE;
	} else {
		len = payload_len;
	}

	if (sdiodev->chip_ops->use_flowctrl_msg) {
		buffer_cnt = aicwf_sdio_flow_ctrl(sdiodev);
		while ((buffer_cnt <= 0 ||
			(buffer_cnt > 0 && len > (buffer_cnt * BUFFER_SIZE))) &&
			   retry < 10) {
			retry++;
			buffer_cnt = aicwf_sdio_flow_ctrl(sdiodev);
			pr_info("aicbsp buffer_cnt = %d\n", buffer_cnt);
		}
	}
	down(&sdiodev->tx_priv->cmd_txsema);

	if (sdiodev->chip_ops->use_flowctrl_msg) {
		if (buffer_cnt > 0 && len < (buffer_cnt * BUFFER_SIZE)) {
			err = aicwf_sdio_send_pkt(sdiodev, payload, len);
			if (err)
				sdio_err("aicwf_sdio_send_pkt fail%d\n", err);

		} else {
			sdio_err("tx msg fc retry fail:%d, %d\n", buffer_cnt, len);
			up(&sdiodev->tx_priv->cmd_txsema);
			return -1;
		}
	} else {
		err = aicwf_sdio_send_msg(sdiodev, payload, len);
		if (err)
			sdio_err("aicwf_sdio_send_pkt fail%d\n", err);
	}

	sdiodev->tx_priv->cmd_txstate = false;
	if (!err)
		sdiodev->tx_priv->cmd_tx_succ = true;
	else
		sdiodev->tx_priv->cmd_tx_succ = false;

	up(&sdiodev->tx_priv->cmd_txsema);

	return err;
}

static void aicwf_sdio_tx_process(struct aic_sdio_dev *sdiodev)
{
	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		sdio_err("Bus is down\n");
		return;
	}

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_ACTIVE_ST);
#endif

	// config
	sdio_info("send cmd\n");
	if (sdiodev->tx_priv->cmd_txstate) {
		if (down_interruptible(&sdiodev->tx_priv->txctl_sema)) {
			txrx_err("txctl down bus->txctl_sema fail\n");
			return;
		}
		if (sdiodev->state != SDIO_ACTIVE_ST) {
			txrx_err("state err\n");
			up(&sdiodev->tx_priv->txctl_sema);
			txrx_err("txctl up bus->txctl_sema fail\n");
			return;
		}

		if (aicwf_sdio_tx_msg(sdiodev))
			sdio_err("failed to send command\n");
		up(&sdiodev->tx_priv->txctl_sema);
		/* to prevent race conditions with wake-up operations */
		if (waitqueue_active(&sdiodev->tx_priv->cmd_txdone_wait))
			wake_up(&sdiodev->tx_priv->cmd_txdone_wait);
	}

	// data
	sdio_info("send data\n");
	if (down_interruptible(&sdiodev->tx_priv->txctl_sema)) {
		txrx_err("txdata down bus->txctl_sema\n");
		return;
	}

	if (sdiodev->state != SDIO_ACTIVE_ST) {
		txrx_err("sdio state err\n");
		up(&sdiodev->tx_priv->txctl_sema);
		return;
	}

	if (!aicwf_is_framequeue_empty(&sdiodev->tx_priv->txq))
		sdiodev->tx_priv->fw_avail_bufcnt = aicwf_sdio_flow_ctrl(sdiodev);
	while (!aicwf_is_framequeue_empty(&sdiodev->tx_priv->txq)) {
		aicwf_sdio_send(sdiodev->tx_priv);
		if (sdiodev->tx_priv->cmd_txstate)
			break;
	}

	up(&sdiodev->tx_priv->txctl_sema);
}

static int aicwf_sdio_bus_txdata(struct device *dev, struct sk_buff *pkt)
{
	uint prio;
	int ret = -EBADE;
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;

	prio = (pkt->priority & 0x7);
	spin_lock_bh(&sdiodev->tx_priv->txqlock);
	if (!aicwf_frame_enq(sdiodev->dev, &sdiodev->tx_priv->txq, pkt, prio)) {
		spin_unlock_bh(&sdiodev->tx_priv->txqlock);
		return -ENOSR;
	}
	ret = 0;

	if (bus_if->state != BUS_UP_ST) {
		sdio_err("bus_if stopped\n");
		spin_unlock_bh(&sdiodev->tx_priv->txqlock);
		return -1;
	}

	atomic_inc(&sdiodev->tx_priv->tx_pktcnt);
	spin_unlock_bh(&sdiodev->tx_priv->txqlock);
	complete(&bus_if->bustx_trgg);

	return ret;
}

static int aicwf_sdio_bus_txmsg(struct device *dev, u8 *msg, uint msglen)
{
	int ret = -1;
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;

	down(&sdiodev->tx_priv->cmd_txsema);
	sdiodev->tx_priv->cmd_txstate = true;
	sdiodev->tx_priv->cmd_tx_succ = false;
	sdiodev->tx_priv->cmd_buf = msg;
	sdiodev->tx_priv->cmd_len = msglen;
	up(&sdiodev->tx_priv->cmd_txsema);

	if (bus_if->state != BUS_UP_ST) {
		sdio_err("bus has stop\n");
		return -1;
	}

	complete(&bus_if->bustx_trgg);

	if (sdiodev->tx_priv->cmd_txstate) {
		int timeout = msecs_to_jiffies(CMD_TX_TIMEOUT);

		ret = wait_event_timeout(sdiodev->tx_priv->cmd_txdone_wait,
					 !(sdiodev->tx_priv->cmd_txstate), timeout);
	}

	if (!sdiodev->tx_priv->cmd_txstate && sdiodev->tx_priv->cmd_tx_succ) {
		ret = 0;
	} else {
		sdio_err("send failed:%d, %d,%x\n", sdiodev->tx_priv->cmd_txstate,
			 sdiodev->tx_priv->cmd_tx_succ, ret);
		ret = -EIO;
	}

	return ret;
}

int aicwf_sdio_send(struct aicwf_tx_priv *tx_priv)
{
	struct sk_buff *pkt;
	struct aic_sdio_dev *sdiodev = tx_priv->sdiodev;
	u16 aggr_len = 0;
	int retry_times = 0;
	int max_retry_times = 5;

	aggr_len = (tx_priv->tail - tx_priv->head);
	if (((atomic_read(&tx_priv->aggr_count) == 0) && aggr_len != 0) ||
	    ((atomic_read(&tx_priv->aggr_count) != 0) && aggr_len == 0)) {
		if (aggr_len > 0)
			aicwf_sdio_aggrbuf_reset(tx_priv);
		goto done;
	}

	if (tx_priv->fw_avail_bufcnt <= 0) { // flow control failed
		tx_priv->fw_avail_bufcnt = aicwf_sdio_flow_ctrl(sdiodev);
		while (tx_priv->fw_avail_bufcnt <= 0 && retry_times < max_retry_times) {
			retry_times++;
			tx_priv->fw_avail_bufcnt = aicwf_sdio_flow_ctrl(sdiodev);
		}
		if (tx_priv->fw_avail_bufcnt <= 0) {
			sdio_err("fc retry %d fail\n", tx_priv->fw_avail_bufcnt);
			goto done;
		}
	}

	if (atomic_read(&tx_priv->aggr_count) == tx_priv->fw_avail_bufcnt) {
		if (atomic_read(&tx_priv->aggr_count) > 0) {
			tx_priv->fw_avail_bufcnt -= atomic_read(&tx_priv->aggr_count);
			aicwf_sdio_aggr_send(tx_priv); // send and check the next pkt;
		}
	} else {
		spin_lock_bh(&sdiodev->tx_priv->txqlock);
		pkt = aicwf_frame_dequeue(&sdiodev->tx_priv->txq);
		if (!pkt) {
			sdio_err("txq no pkt\n");
			spin_unlock_bh(&sdiodev->tx_priv->txqlock);
			goto done;
		}
		atomic_dec(&sdiodev->tx_priv->tx_pktcnt);
		spin_unlock_bh(&sdiodev->tx_priv->txqlock);

		if (!tx_priv || !tx_priv->tail || !pkt)
			txrx_err("null error\n");
		if (aicwf_sdio_aggr(tx_priv, pkt)) {
			aicwf_sdio_aggrbuf_reset(tx_priv);
			sdio_err("add aggr pkts failed!\n");
			goto done;
		}

		// when aggr finish or there is cmd to send, just send this aggr pkt to
		// fw
		if ((int)atomic_read(&sdiodev->tx_priv->tx_pktcnt) == 0 ||
		    sdiodev->tx_priv->cmd_txstate) { // no more pkt send it!
			tx_priv->fw_avail_bufcnt -= atomic_read(&tx_priv->aggr_count);
			aicwf_sdio_aggr_send(tx_priv);
		} else {
			goto done;
		}
	}

done:
	return 0;
}

int aicwf_sdio_aggr(struct aicwf_tx_priv *tx_priv, struct sk_buff *pkt)
{
	// struct rwnx_txhdr *txhdr = (struct rwnx_txhdr *)pkt->data;
	u8 *start_ptr = tx_priv->tail;
	u8 sdio_header[4];
	u8 adjust_str[4] = {0, 0, 0, 0};
	u16 curr_len = 0;
	int allign_len = 0;

	sdio_header[2] = 0x01; // data
	if (aicbsp_sdiodev->chip_ops->use_hdr_checksum)
		sdio_header[3] = crc8_ponl_107(&sdio_header[0], 3); // crc8
	else
		sdio_header[3] = 0; // reserved

	memcpy(tx_priv->tail, (u8 *)&sdio_header, sizeof(sdio_header));
	tx_priv->tail += sizeof(sdio_header);

	// word alignment
	curr_len = tx_priv->tail - tx_priv->head;
	if (curr_len & (TX_ALIGNMENT - 1)) {
		allign_len = roundup(curr_len, TX_ALIGNMENT) - curr_len;
		memcpy(tx_priv->tail, adjust_str, allign_len);
		tx_priv->tail += allign_len;
	}

	if (aicbsp_sdiodev->chip_ops->need_fix_hdr_len) {
		start_ptr[0] = ((tx_priv->tail - start_ptr - 4) & 0xff);
		start_ptr[1] = (((tx_priv->tail - start_ptr - 4) >> 8) & 0x0f);
	}

	tx_priv->aggr_buf->dev = pkt->dev;

	consume_skb(pkt);
	atomic_inc(&tx_priv->aggr_count);
	return 0;
}

void aicwf_sdio_aggr_send(struct aicwf_tx_priv *tx_priv)
{
	struct sk_buff *tx_buf = tx_priv->aggr_buf;
	int ret = 0;
	int curr_len = 0;

	// link tail is necessary
	curr_len = tx_priv->tail - tx_priv->head;
	if ((curr_len % TXPKT_BLOCKSIZE) != 0) {
		memset(tx_priv->tail, 0, TAIL_LEN);
		tx_priv->tail += TAIL_LEN;
	}

	tx_buf->len = tx_priv->tail - tx_priv->head;
	ret = aicwf_sdio_txpkt(tx_priv->sdiodev, tx_buf);
	if (ret < 0)
		sdio_err("fail to send aggr pkt!\n");

	aicwf_sdio_aggrbuf_reset(tx_priv);
}

void aicwf_sdio_aggrbuf_reset(struct aicwf_tx_priv *tx_priv)
{
	struct sk_buff *aggr_buf = tx_priv->aggr_buf;

	tx_priv->tail = tx_priv->head;
	aggr_buf->len = 0;
	atomic_set(&tx_priv->aggr_count, 0);
}

static int aicwf_sdio_bus_start(struct device *dev)
{
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	int ret = 0;

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	if (sdiodev->chip_ops->use_func_msg) {
		sdio_claim_host(sdiodev->func);

		// since we have func2 we don't register irq handler
		sdio_claim_irq(sdiodev->func, NULL);
		sdio_claim_irq(sdiodev->func_msg, NULL);

		sdiodev->func->irq_handler =
			(sdio_irq_handler_t *)aicwf_sdio_hal_irqhandler;
		sdiodev->func_msg->irq_handler =
			(sdio_irq_handler_t *)aicwf_sdio_hal_irqhandler_func2;
		sdio_release_host(sdiodev->func);

		// enable sdio interrupt
		ret =
			aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x07);

		if (ret != 0)
			sdio_err("intr register failed:%d\n", ret);

		// enable sdio interrupt
		ret = aicwf_sdio_writeb_func2(sdiodev,
					      sdiodev->sdio_reg.intr_config_reg, 0x07);

		if (ret != 0)
			sdio_err("func2 intr register failed:%d\n", ret);
	} else {
		sdio_claim_host(sdiodev->func);
		sdio_claim_irq(sdiodev->func, aicwf_sdio_hal_irqhandler);

		if (sdiodev->chip_ops->need_func0_intr) {
			sdio_f0_writeb(sdiodev->func, 0x07, 0x04, &ret);
			if (ret)
				sdio_err("set func0 int en fail %d\n", ret);
		}
		sdio_release_host(sdiodev->func);
		// enable sdio interrupt
		ret =
			aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x07);
		if (ret != 0)
			sdio_err("intr register failed:%d\n", ret);
	}

	bus_if->state = BUS_UP_ST;
	return ret;
}

int aicwf_sdio_bustx_thread(void *data)
{
	struct aicwf_bus *bus = (struct aicwf_bus *)data;
	struct aic_sdio_dev *sdiodev = bus->bus_priv.sdio;

	while (1) {
		if (kthread_should_stop()) {
			sdio_dbg("sdio bustx thread stop\n");
			break;
		}
		if (!wait_for_completion_interruptible(&bus->bustx_trgg)) {
			if ((int)(atomic_read(&sdiodev->tx_priv->tx_pktcnt) > 0) ||
			    sdiodev->tx_priv->cmd_txstate)
				aicwf_sdio_tx_process(sdiodev);
		}
	}

	return 0;
}

int aicwf_sdio_busrx_thread(void *data)
{
	struct aicwf_rx_priv *rx_priv = (struct aicwf_rx_priv *)data;
	struct aicwf_bus *bus_if = rx_priv->sdiodev->bus_if;

	while (1) {
		if (kthread_should_stop()) {
			sdio_dbg("sdio busrx thread stop\n");
			break;
		}
		if (!wait_for_completion_interruptible(&bus_if->busrx_trgg))
			aicwf_process_rxframes(rx_priv);
	}

	return 0;
}

#if defined(CONFIG_SDIO_PWRCTRL)
static void aicwf_sdio_bus_pwrctl(struct timer_list *t)
{
	//struct aic_sdio_dev *sdiodev = from_timer(sdiodev, t, timer);
	struct aic_sdio_dev *sdiodev = timer_container_of(sdiodev, t, timer);

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		sdio_info("bus down\n");
		return;
	}

	if (sdiodev->pwrctl_tsk)
		complete(&sdiodev->pwrctrl_trgg);
}
#endif

static void aicwf_sdio_enq_rxpkt(struct aic_sdio_dev *sdiodev,
				 struct sk_buff *pkt)
{
	struct aicwf_rx_priv *rx_priv = sdiodev->rx_priv;
	unsigned long flags = 0;

	spin_lock_irqsave(&rx_priv->rxqlock, flags);
	if (!aicwf_rxframe_enqueue(sdiodev->dev, &rx_priv->rxq, pkt)) {
		spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
		aicwf_dev_skb_free(pkt);
		return;
	}
	spin_unlock_irqrestore(&rx_priv->rxqlock, flags);

	atomic_inc(&rx_priv->rx_cnt);
}

#define SDIO_OTHER_INTERRUPT (0x1ul << 7)

void aicwf_sdio_hal_irqhandler(struct sdio_func *func)
{
	struct aicwf_bus *bus_if = NULL;
	struct aic_sdio_dev *sdiodev = NULL;
	u8 intstatus = 0;
	u8 byte_len = 0;
	struct sk_buff *pkt = NULL;
	int ret;

	if (!func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return;
	}
	bus_if = dev_get_drvdata(&func->dev);
	sdiodev = bus_if->bus_priv.sdio;

	if (aicbsp_sdiodev->sdio_hal_irqhandler) {
		aicbsp_sdiodev->sdio_hal_irqhandler(func);
		return;
	}

	if (!bus_if || bus_if->state == BUS_DOWN_ST) {
		sdio_err("bus err\n");
		return;
	}

	if (aicbsp_sdiodev->chip_ops->use_func2) {
		ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.block_cnt_reg,
				       &intstatus);

		while (intstatus) {
			sdiodev->rx_priv->data_len = intstatus * SDIOWIFI_FUNC_BLOCKSIZE;
			if (intstatus > 0) {
				if (intstatus < 64) {
					pkt = aicwf_sdio_readframes(sdiodev, 0);
				} else {
					// byte_len must<= 128
					aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len);
					sdio_info("byte mode len=%d\r\n", byte_len);
					pkt = aicwf_sdio_readframes(sdiodev, 0);
				}
			} else {
				sdio_err("Interrupt but no data\n");
			}

			if (pkt)
				aicwf_sdio_enq_rxpkt(sdiodev, pkt);

			ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.block_cnt_reg,
					       &intstatus);
		}
	} else {
		do {
			ret = aicwf_sdio_readb(sdiodev,
					       sdiodev->sdio_reg.misc_int_status_reg,
					       &intstatus);
			if (!ret)
				break;
			sdio_err("ret=%d, intstatus=%x\r\n", ret, intstatus);
		} while (1);
		if (intstatus & SDIO_OTHER_INTERRUPT) {
			u8 int_pending;

			ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.sleep_reg,
					       &int_pending);
			if (ret < 0)
				sdio_err("reg:%d read failed!\n", sdiodev->sdio_reg.sleep_reg);
			int_pending &= ~0x01; // dev to host soft irq
			ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.sleep_reg,
						int_pending);
			if (ret < 0)
				sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.sleep_reg);
		}

		if (intstatus > 0) {
			u8 intmaskf2 = intstatus | (0x1UL << 3);

			if (intmaskf2 > 120U) {      // func2
				if (intmaskf2 == 127U) { // byte mode
					// byte_len must<= 128
					aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len);
					sdio_info("byte mode len=%d\r\n", byte_len);
					pkt = aicwf_sdio_readframes(sdiodev, 1);
				} else { // block mode
					sdiodev->rx_priv->data_len =
						(intstatus & 0x7U) * SDIOWIFI_FUNC_BLOCKSIZE;
					pkt = aicwf_sdio_readframes(sdiodev, 1);
				}
			} else {                     // func1
				if (intstatus == 120U) { // byte mode
					// byte_len must<= 128
					aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len);
					sdio_info("byte mode len=%d\r\n", byte_len);
					pkt = aicwf_sdio_readframes(sdiodev, 0);
				} else { // block mode
					sdiodev->rx_priv->data_len =
						(intstatus & 0x7FU) * SDIOWIFI_FUNC_BLOCKSIZE;
					pkt = aicwf_sdio_readframes(sdiodev, 0);
				}
			}
		} else {
			sdio_err("Interrupt but no data\n");
		}

		if (pkt)
			aicwf_sdio_enq_rxpkt(sdiodev, pkt);
	}

	complete(&bus_if->busrx_trgg);
}

void aicwf_sdio_hal_irqhandler_func2(struct sdio_func *func)
{
	struct aicwf_bus *bus_if = dev_get_drvdata(&func->dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	u8 intstatus = 0;
	u8 byte_len = 0;
#ifdef CONFIG_PREALLOC_RX_SKB
	struct rx_buff *pkt = NULL;
#else
	struct sk_buff *pkt = NULL;
#endif
	int ret;

	if (!bus_if || bus_if->state == BUS_DOWN_ST) {
		sdio_err("bus err\n");
		return;
	}

#ifdef CONFIG_PREALLOC_RX_SKB
	if (list_empty(&aic_rx_buff_list.rxbuff_list)) {
		pr_warn("aicbsp %s %d, rxbuff list is empty\n", __func__, __LINE__);
		return;
	}
#endif

	ret = aicwf_sdio_readb_func2(sdiodev, sdiodev->sdio_reg.block_cnt_reg,
				     &intstatus);
	if (ret) {
		sdio_err("failed to read interrupt status: %d\n", ret);
		return;
	}

	while (intstatus) {
		sdiodev->rx_priv->data_len = intstatus * SDIOWIFI_FUNC_BLOCKSIZE;
		if (intstatus > 0) {
			if (intstatus < 64) {
				pkt = aicwf_sdio_readframes(sdiodev, 1);
			} else {
				sdio_info("byte mode len=%d\r\n", byte_len);
				// byte_len must<= 128
				aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len);
				pkt = aicwf_sdio_readframes(sdiodev, 1);
			}
		} else {
			sdio_err("Interrupt but no data\n");
		}

		if (pkt)
			aicwf_sdio_enq_rxpkt(sdiodev, pkt);

		ret = aicwf_sdio_readb_func2(sdiodev, sdiodev->sdio_reg.block_cnt_reg,
					     &intstatus);
		if (ret) {
			sdio_err("failed to read interrupt status: %d\n", ret);
			break;
		}
	}

	complete(&bus_if->busrx_trgg);
}

#if defined(CONFIG_SDIO_PWRCTRL)
void aicwf_sdio_pwrctl_timer(struct aic_sdio_dev *sdiodev, uint duration)
{
	uint timeout;

	if (sdiodev->bus_if->state == BUS_DOWN_ST && duration)
		return;

	spin_lock_bh(&sdiodev->pwrctl_lock);
	if (!duration) {
		if (timer_pending(&sdiodev->timer))
			//del_timer_sync(&sdiodev->timer);
			timer_delete_sync(&sdiodev->timer);
	} else {
		sdiodev->active_duration = duration;
		timeout = msecs_to_jiffies(sdiodev->active_duration);
		mod_timer(&sdiodev->timer, jiffies + timeout);
	}
	spin_unlock_bh(&sdiodev->pwrctl_lock);
}
#endif

/* Forward declarations for new bus_ops callbacks */
static int aicwf_bsp_bus_read_reg(struct device *dev, u32 regaddr, u8 *val);
static int aicwf_bsp_bus_write_reg(struct device *dev, u32 regaddr, u8 val);
static int aicwf_bsp_bus_send_pkt(struct device *dev, u8 *buf, uint count);
static int aicwf_bsp_bus_recv_pkt(struct device *dev, u8 *buf, u32 size);
static int aicwf_bsp_bus_enable_irq(struct device *dev);
static void aicwf_bsp_bus_disable_irq(struct device *dev);
static bool aicwf_bsp_bus_flow_ctrl(struct device *dev);
static int aicwf_bsp_bus_sleep_allow(struct device *dev);
static int aicwf_bsp_bus_wakeup(struct device *dev);
static const void *aicwf_bsp_bus_get_hw_props(struct device *dev);

static struct aicwf_bus_ops aicwf_sdio_bus_ops = {
	.start          = aicwf_sdio_bus_start,
	.stop           = aicwf_sdio_bus_stop,
	.txdata         = aicwf_sdio_bus_txdata,
	.txmsg          = aicwf_sdio_bus_txmsg,
	.read_reg       = aicwf_bsp_bus_read_reg,
	.write_reg      = aicwf_bsp_bus_write_reg,
	.send_pkt       = aicwf_bsp_bus_send_pkt,
	.recv_pkt       = aicwf_bsp_bus_recv_pkt,
	.enable_irq     = aicwf_bsp_bus_enable_irq,
	.disable_irq    = aicwf_bsp_bus_disable_irq,
	.flow_ctrl      = aicwf_bsp_bus_flow_ctrl,
	.sleep_allow    = aicwf_bsp_bus_sleep_allow,
	.wakeup         = aicwf_bsp_bus_wakeup,
	.get_hw_props   = aicwf_bsp_bus_get_hw_props,
};

void aicwf_sdio_release_func2(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;

	if (!sdiodev->func_msg) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return;
	}
	sdio_claim_host(sdiodev->func_msg);
	// disable sdio interrupt
	ret = aicwf_sdio_writeb_func2(sdiodev, sdiodev->sdio_reg.intr_config_reg,
				      0x0);
	if (ret < 0)
		sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);

	sdio_release_irq(sdiodev->func_msg);
	sdio_release_host(sdiodev->func_msg);
}

void aicwf_sdio_release(struct aic_sdio_dev *sdiodev)
{
	struct aicwf_bus *bus_if = NULL;

	if (bsp_init == 0) {
		pr_err("aicbsp already sdio_release\n");
		return;
	}

	bsp_init = 0;

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return;
	}

	bus_if = aicbsp_get_drvdata(sdiodev->dev);
	if (bus_if)
		bus_if->state = BUS_DOWN_ST;

	if (aicbsp_sdiodev->chip_ops->use_func_msg)
		aicwf_sdio_release_func2(sdiodev);

	if (sdiodev->dev)
		aicwf_bus_deinit(sdiodev->dev);

	if (sdiodev->tx_priv)
		aicwf_tx_deinit(sdiodev->tx_priv);

	if (sdiodev->rx_priv)
		aicwf_rx_deinit(sdiodev->rx_priv);

	if (sdiodev->cmd_mgr.state == RWNX_CMD_MGR_STATE_INITED)
		rwnx_cmd_mgr_deinit(&sdiodev->cmd_mgr);
}

void aicwf_sdio_reg_init(struct aic_sdio_dev *sdiodev)
{
	if (aicbsp_sdiodev->chip_ops->use_func2) {
		sdiodev->sdio_reg.bytemode_len_reg = SDIOWIFI_BYTEMODE_LEN_REG;
		sdiodev->sdio_reg.intr_config_reg = SDIOWIFI_INTR_CONFIG_REG;
		sdiodev->sdio_reg.sleep_reg = SDIOWIFI_SLEEP_REG;
		sdiodev->sdio_reg.wakeup_reg = SDIOWIFI_WAKEUP_REG;
		sdiodev->sdio_reg.flow_ctrl_reg = SDIOWIFI_FLOW_CTRL_REG;
		sdiodev->sdio_reg.register_block = SDIOWIFI_REGISTER_BLOCK;
		sdiodev->sdio_reg.bytemode_enable_reg = SDIOWIFI_BYTEMODE_ENABLE_REG;
		sdiodev->sdio_reg.block_cnt_reg = SDIOWIFI_BLOCK_CNT_REG;
		sdiodev->sdio_reg.rd_fifo_addr = SDIOWIFI_RD_FIFO_ADDR;
		sdiodev->sdio_reg.wr_fifo_addr = SDIOWIFI_WR_FIFO_ADDR;
	} else {
		sdiodev->sdio_reg.bytemode_len_reg = SDIOWIFI_BYTEMODE_LEN_REG_V3;
		sdiodev->sdio_reg.intr_config_reg = SDIOWIFI_INTR_ENABLE_REG_V3;
		sdiodev->sdio_reg.sleep_reg = SDIOWIFI_INTR_PENDING_REG_V3;
		sdiodev->sdio_reg.wakeup_reg = SDIOWIFI_INTR_TO_DEVICE_REG_V3;
		sdiodev->sdio_reg.flow_ctrl_reg = SDIOWIFI_FLOW_CTRL_Q1_REG_V3;
		sdiodev->sdio_reg.bytemode_enable_reg = SDIOWIFI_BYTEMODE_ENABLE_REG_V3;
		sdiodev->sdio_reg.misc_int_status_reg = SDIOWIFI_MISC_INT_STATUS_REG_V3;
		sdiodev->sdio_reg.rd_fifo_addr = SDIOWIFI_RD_FIFO_ADDR_V3;
		sdiodev->sdio_reg.wr_fifo_addr = SDIOWIFI_WR_FIFO_ADDR_V3;
	}
}

int aicwf_sdio_func_init(struct aic_sdio_dev *sdiodev)
{
	struct mmc_host *host;
	u8 block_bit0 = 0x1;
	u8 byte_mode_disable = 0x1; // 1: no byte mode
	int ret = 0;
	struct aicbsp_feature_t feature;

	aicbsp_get_feature(&feature);
	aicwf_sdio_reg_init(sdiodev);

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	host = sdiodev->func->card->host;

	sdio_claim_host(sdiodev->func);

	ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);
	if (ret < 0) {
		sdio_err("set blocksize fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	ret = sdio_enable_func(sdiodev->func);
	if (ret < 0) {
		sdio_err("enable func fail %d.\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	usleep_range(20, 40);
	// SDIO CLOCK SETTING
	if (feature.sdio_clock > 0) {
		host->ios.clock = feature.sdio_clock;
		host->ops->set_ios(host, &host->ios);
		sdio_dbg("Set SDIO Clock %d MHz\n", host->ios.clock / 1000000);
	}
	sdio_release_host(sdiodev->func);

	if (aicbsp_sdiodev->chip_ops->use_func_msg) {
		sdio_claim_host(sdiodev->func_msg);

		// set sdio blocksize
		ret = sdio_set_block_size(sdiodev->func_msg, SDIOWIFI_FUNC_BLOCKSIZE);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "set func2 blocksize fail %d\n", ret);
			sdio_release_host(sdiodev->func_msg);
			return ret;
		}

		// set sdio enable func
		ret = sdio_enable_func(sdiodev->func_msg);
		if (ret < 0)
			AICWFDBG(LOGERROR, "enable func2 fail %d.\n", ret);

		sdio_release_host(sdiodev->func_msg);

		ret = aicwf_sdio_writeb_func2(sdiodev, sdiodev->sdio_reg.register_block,
					      block_bit0);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "reg:%d write failed!\n",
				 sdiodev->sdio_reg.register_block);
			return ret;
		}

		// 1: no byte mode
		ret = aicwf_sdio_writeb_func2(sdiodev,
					      sdiodev->sdio_reg.bytemode_enable_reg,
					      byte_mode_disable);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "reg:%d write failed!\n",
				 sdiodev->sdio_reg.bytemode_enable_reg);
			return ret;
		}
	}

	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.register_block,
				block_bit0);
	if (ret < 0) {
		sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.register_block);
		return ret;
	}

	// 1: no byte mode
	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.bytemode_enable_reg,
				byte_mode_disable);
	if (ret < 0) {
		sdio_err("reg:%d write failed!\n",
			 sdiodev->sdio_reg.bytemode_enable_reg);
		return ret;
	}

	return ret;
}

int aicwf_sdiov3_func_init(struct aic_sdio_dev *sdiodev)
{
	struct mmc_host *host;
	u8 byte_mode_disable = 0x1; // 1: no byte mode
	int ret = 0;
	struct aicbsp_feature_t feature;
	int val;

	aicbsp_get_feature(&feature);
	aicwf_sdio_reg_init(sdiodev);

	if (!sdiodev->func) {
		pr_warn("aicbsp %s, NULL sdio func\n", __func__);
		return 0;
	}

	host = sdiodev->func->card->host;

	sdio_claim_host(sdiodev->func);
	sdiodev->func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

	ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);
	if (ret < 0) {
		sdio_err("set blocksize fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	ret = sdio_enable_func(sdiodev->func);
	if (ret < 0) {
		sdio_err("enable func fail %d.\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}

	sdio_f0_writeb(sdiodev->func, 0x7F, 0xF2, &ret);
	if (ret) {
		sdio_err("set fn0 0xF2 fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	// mod sdio phase
	if (host->ios.timing == MMC_TIMING_UHS_DDR50)
		val = 0x20; // 0x21; //0x1D; //0x5;
	else
		val = 0x00; // 0x01; //0x19; //0x1;
	val |= SDIOCLK_FREE_RUNNING_BIT;
	sdio_f0_writeb(sdiodev->func, val, 0xF0, &ret);
	if (ret) {
		sdio_err("set iopad ctrl fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	sdio_f0_writeb(sdiodev->func, 0x00, 0xF8, &ret);
	if (ret) {
		sdio_err("set iopad delay2 fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	sdio_f0_writeb(sdiodev->func, 0x00, 0xF1, &ret);
	if (ret) {
		sdio_err("set iopad delay1 fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	usleep_range(1000, 2000);
	sdio_dbg("Set SDIO Clock %d MHz\n", host->ios.clock / 1000000);
	sdio_release_host(sdiodev->func);

	// 1: no byte mode
	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.bytemode_enable_reg,
				byte_mode_disable);
	if (ret < 0) {
		sdio_err("reg:%d write failed!\n",
			 sdiodev->sdio_reg.bytemode_enable_reg);
		return ret;
	}

	return ret;
}

void aicwf_sdio_func_deinit(struct aic_sdio_dev *sdiodev)
{
	struct aicwf_bus *bus_if = NULL;

	if (bsp_func_init == 0) {
		pr_err("aicbsp already func_deinit\n");
		return;
	}

	bsp_func_init = 0;

	if (!sdiodev->func) {
		sdio_info("%s, NULL sdio func\n", __func__);
		return;
	}

	bus_if = dev_get_drvdata(sdiodev->dev);
	if (!bus_if) {
		sdio_info("%s, bus_if is null\n", __func__);
		return;
	}

	if (sdiodev->bus_if == bus_if) {
		sdio_dbg("%s bsp disable\n", __func__);
		sdio_claim_host(sdiodev->func);
		sdio_disable_func(sdiodev->func);
		sdio_release_host(sdiodev->func);
	}

	if (aicbsp_sdiodev->chip_ops->use_func_msg) {
		sdio_claim_host(sdiodev->func_msg);
		sdio_disable_func(sdiodev->func_msg);
		sdio_release_host(sdiodev->func_msg);
	}
}

void *aicwf_sdio_bus_init(struct aic_sdio_dev *sdiodev)
{
	int ret;
	struct aicwf_bus *bus_if;
	struct aicwf_rx_priv *rx_priv;
	struct aicwf_tx_priv *tx_priv;

#if defined(CONFIG_SDIO_PWRCTRL)
	spin_lock_init(&sdiodev->pwrctl_lock);
	sema_init(&sdiodev->pwrctl_wakeup_sema, 1);
#endif

	bus_if = sdiodev->bus_if;
	bus_if->dev = sdiodev->dev;
	bus_if->ops = &aicwf_sdio_bus_ops;
	bus_if->state = BUS_DOWN_ST;
#if defined(CONFIG_SDIO_PWRCTRL)
	sdiodev->state = SDIO_SLEEP_ST;
	sdiodev->active_duration = SDIOWIFI_PWR_CTRL_INTERVAL;
#else
	sdiodev->state = SDIO_ACTIVE_ST;
#endif

	rx_priv = aicwf_rx_init(sdiodev);
	if (!rx_priv) {
		sdio_err("rx init fail\n");
		goto fail;
	}
	sdiodev->rx_priv = rx_priv;

	tx_priv = aicwf_tx_init(sdiodev);
	if (!tx_priv) {
		sdio_err("tx init fail\n");
		goto fail;
	}
	sdiodev->tx_priv = tx_priv;
	aicwf_frame_queue_init(&tx_priv->txq, 8, TXQLEN);
	spin_lock_init(&tx_priv->txqlock);
	sema_init(&tx_priv->txctl_sema, 1);
	sema_init(&tx_priv->cmd_txsema, 1);
	init_waitqueue_head(&tx_priv->cmd_txdone_wait);
	atomic_set(&tx_priv->tx_pktcnt, 0);

#if defined(CONFIG_SDIO_PWRCTRL)
	timer_setup(&sdiodev->timer, aicwf_sdio_bus_pwrctl, 0);
	init_completion(&sdiodev->pwrctrl_trgg);
#endif
	ret = aicwf_bus_init(0, sdiodev->dev);
	if (ret < 0) {
		sdio_err("bus init fail\n");
		goto fail;
	}

	ret = aicwf_bus_start(bus_if);
	if (ret != 0) {
		sdio_err("bus start fail\n");
		goto fail;
	}

	return sdiodev;

fail:
	aicwf_sdio_release(sdiodev);
	return NULL;
}

/**
 * get_testmode - Get the firmware test mode selected by the BSP
 *
 * Return: Current firmware test mode.
 */
int get_testmode(void)
{
	return testmode;
}
EXPORT_SYMBOL_GPL(get_testmode);

/**
 * get_sdio_func - Get the active AIC SDIO function
 *
 * Return: Pointer to the active SDIO function.
 */
struct sdio_func *get_sdio_func(void)
{
	return aicbsp_sdiodev->func;
}
EXPORT_SYMBOL_GPL(get_sdio_func);

/**
 * set_irq_handler - Set the WLAN driver's SDIO interrupt callback
 * @fn: Callback invoked for an SDIO interrupt
 */
void set_irq_handler(void *fn)
{
	aicbsp_sdiodev->sdio_hal_irqhandler = (sdio_irq_handler_t *)fn;
}
EXPORT_SYMBOL_GPL(set_irq_handler);

u8 crc8_ponl_107(u8 *p_buffer, u16 cal_size)
{
	u8 i;
	u8 crc = 0;

	if (cal_size == 0)
		return crc;
	while (cal_size--) {
		for (i = 0x80; i > 0; i /= 2) {
			if (crc & 0x80) {
				crc *= 2;
				crc ^= 0x07; // polynomial X8 + X2 + X + 1,(0x107)
			} else {
				crc *= 2;
			}
			if ((*p_buffer) & i)
				crc ^= 0x07;
		}
		p_buffer++;
	}
	return crc;
}

/* ================================================================
 *  Bus abstraction layer – BSP SDIO bus_ops callbacks
 * ================================================================
 */

static inline struct aic_sdio_dev *aicwf_bsp_bus_to_sdiodev(struct device *dev)
{
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);

	return bus_if->bus_priv.sdio;
}

static int aicwf_bsp_bus_read_reg(struct device *dev, u32 regaddr, u8 *val)
{
	return aicwf_sdio_readb(aicwf_bsp_bus_to_sdiodev(dev), regaddr, val);
}

static int aicwf_bsp_bus_write_reg(struct device *dev, u32 regaddr, u8 val)
{
	return aicwf_sdio_writeb(aicwf_bsp_bus_to_sdiodev(dev), regaddr, val);
}

static int aicwf_bsp_bus_send_pkt(struct device *dev, u8 *buf, uint count)
{
	return aicwf_sdio_send_pkt(aicwf_bsp_bus_to_sdiodev(dev), buf, count);
}

static int aicwf_bsp_bus_recv_pkt(struct device *dev, u8 *buf, u32 size)
{
	struct aic_sdio_dev *sdiodev = aicwf_bsp_bus_to_sdiodev(dev);
	int ret;

	sdio_claim_host(sdiodev->func);
	ret = sdio_readsb(sdiodev->func, buf,
			  sdiodev->sdio_reg.rd_fifo_addr, size);
	sdio_release_host(sdiodev->func);

	return ret;
}

static int aicwf_bsp_bus_enable_irq(struct device *dev)
{
	struct aic_sdio_dev *sdiodev = aicwf_bsp_bus_to_sdiodev(dev);
	int ret;

	sdio_claim_host(sdiodev->func);
	sdio_claim_irq(sdiodev->func, sdiodev->sdio_hal_irqhandler);
	sdio_release_host(sdiodev->func);

	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x07);
	if (ret)
		sdio_err("intr register failed:%d\n", ret);

	return ret;
}

static void aicwf_bsp_bus_disable_irq(struct device *dev)
{
	struct aic_sdio_dev *sdiodev = aicwf_bsp_bus_to_sdiodev(dev);

	sdio_claim_host(sdiodev->func);
	aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x0);
	sdio_release_irq(sdiodev->func);
	sdio_release_host(sdiodev->func);
}

static bool aicwf_bsp_bus_flow_ctrl(struct device *dev)
{
	int ret;

	ret = aicwf_sdio_flow_ctrl(aicwf_bsp_bus_to_sdiodev(dev));
	return (ret > 0);
}

#if defined(CONFIG_SDIO_PWRCTRL)
static int aicwf_bsp_bus_sleep_allow(struct device *dev)
{
	return aicwf_sdio_sleep_allow(aicwf_bsp_bus_to_sdiodev(dev));
}

static int aicwf_bsp_bus_wakeup(struct device *dev)
{
	return aicwf_sdio_wakeup(aicwf_bsp_bus_to_sdiodev(dev));
}
#else
static int aicwf_bsp_bus_sleep_allow(struct device *dev)
{
	return 0;
}

static int aicwf_bsp_bus_wakeup(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_SDIO_PWRCTRL */

static const void *aicwf_bsp_bus_get_hw_props(struct device *dev)
{
	return NULL; /* BSP layer does not expose chip HW props via bus_ops */
}
