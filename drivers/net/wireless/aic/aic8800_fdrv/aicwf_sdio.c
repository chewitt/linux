// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief SDIO function declarations
 *
 ******************************************************************************
 */

#include <linux/completion.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm.h>
#include <linux/semaphore.h>
#include <linux/suspend.h>

#include "aicwf_sdio.h"
#include "aicwf_txrxif.h"
#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_platform.h"
#include "sdio_host.h"
#include <linux/pm_wakeirq.h>

#include "aic_bsp_export.h"
#include "aicwf_chip_ops.h"

static const struct aic_sdio_chip_hw *aic_sdio_hw;

/*
 * Wakeup sources report PM events; they are not a driver-side completion
 * mechanism. Track SDIO work explicitly so suspend can quiesce it without
 * inspecting wakeup-source internals.
 */
bool aicwf_sdio_activity_get(struct rwnx_hw *rwnx_hw,
			     struct wakeup_source *ws)
{
	struct aic_sdio_dev *sdiodev;

	if (!rwnx_hw || !ws)
		return false;

	sdiodev = rwnx_hw->sdiodev;
	if (!sdiodev || unlikely(atomic_read(&sdiodev->suspending)))
		return false;

	atomic_inc(&sdiodev->activity_count);
	smp_mb__after_atomic();
	if (unlikely(atomic_read(&sdiodev->suspending))) {
		if (atomic_dec_and_test(&sdiodev->activity_count))
			wake_up_all(&sdiodev->activity_waitq);
		return false;
	}

	__pm_stay_awake(ws);
	return true;
}

void aicwf_sdio_activity_put(struct rwnx_hw *rwnx_hw,
			    struct wakeup_source *ws)
{
	struct aic_sdio_dev *sdiodev;

	if (!rwnx_hw || !ws)
		return;

	sdiodev = rwnx_hw->sdiodev;
	if (!sdiodev)
		return;

	__pm_relax(ws);
	/*
	 * Keep the PM release before exposing count == 0 to a suspend waiter.
	 * atomic_add_unless() prevents a bad duplicate put from underflowing the
	 * accounting and making all later suspend attempts time out.
	 */
	if (WARN_ON_ONCE(!atomic_add_unless(&sdiodev->activity_count, -1, 0)))
		return;

	if (atomic_read(&sdiodev->activity_count) == 0)
		wake_up_all(&sdiodev->activity_waitq);
}

bool aicwf_sdio_scan_activity_get(struct rwnx_hw *rwnx_hw)
{
	if (!rwnx_hw)
		return false;

	/* Reserve the terminal put before taking the shared wakeup source. */
	if (WARN_ON_ONCE(atomic_cmpxchg(&rwnx_hw->scan_activity_held, 0, 1)))
		return false;

	if (!aicwf_sdio_activity_get(rwnx_hw, rwnx_hw->ws_scan)) {
		atomic_set(&rwnx_hw->scan_activity_held, 0);
		return false;
	}

	return true;
}

void aicwf_sdio_scan_activity_put(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw && atomic_xchg(&rwnx_hw->scan_activity_held, 0))
		aicwf_sdio_activity_put(rwnx_hw, rwnx_hw->ws_scan);
}


static int tx_aggr_counter = 32;
module_param_named(tx_aggr_counter, tx_aggr_counter, int, 0644);

#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
int tx_fc_low_water = AICWF_SDIO_TX_LOW_WATER;
module_param_named(tx_fc_low_water, tx_fc_low_water, int, 0644);

int tx_fc_high_water = AICWF_SDIO_TX_HIGH_WATER;
module_param_named(tx_fc_high_water, tx_fc_high_water, int, 0644);
#endif


int aicwf_sdio_readb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 *val)
{
	int ret;

	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}
	sdio_claim_host(sdiodev->func);
	*val = sdio_readb(sdiodev->func, regaddr, &ret);
	sdio_release_host(sdiodev->func);
	return ret;
}

int aicwf_sdio_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val)
{
	int ret;

	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}
	sdio_claim_host(sdiodev->func);
	sdio_writeb(sdiodev->func, val, regaddr, &ret);
	sdio_release_host(sdiodev->func);
	return ret;
}

int aicwf_sdio_func2_readb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 *val)
{
	int ret;

	sdio_claim_host(sdiodev->func2);
	*val = sdio_readb(sdiodev->func2, regaddr, &ret);
	sdio_release_host(sdiodev->func2);
	return ret;
}

int aicwf_sdio_func2_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val)
{
	int ret;

	sdio_claim_host(sdiodev->func2);
	sdio_writeb(sdiodev->func2, val, regaddr, &ret);
	sdio_release_host(sdiodev->func2);
	return ret;
}

#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
void aicwf_sdio_tx_netif_flowctrl(struct rwnx_hw *rwnx_hw, bool state)
{
	struct rwnx_vif *rwnx_vif;

	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (!rwnx_vif->up)
			continue;
		if (state)
			netif_tx_stop_all_queues(rwnx_vif->ndev);
		else
			netif_tx_wake_all_queues(rwnx_vif->ndev);
	}
}
#endif

#ifdef CONFIG_AIC8800_TEMP_CONTROL
static int update_state(struct aic_sdio_dev *sdiodev)
{
	s8 value = sdiodev->tp_ctrl.cur_temp;
	u8 current_state = sdiodev->tp_ctrl.cur_stat;
	s8 thd_1 = sdiodev->tp_ctrl.tp_thd_1;
	s8 thd_2 = sdiodev->tp_ctrl.tp_thd_2;

	if (value > thd_2)
		return 2;
	else if (value > (thd_2 - BUFFERING_V2) && (current_state == 2))
		return 2;
	else if (value > thd_1 && current_state != 2)
		return 1;
	else if (value > (thd_1 - BUFFERING_V1) && current_state == 1)
		return 1;
	else if (current_state == 0)
		return 0;
	else
		return 1;
}

void aicwf_netif_ctrl(struct aic_sdio_dev *sdiodev, int val)
{
	unsigned long flags;
	struct rwnx_vif *rwnx_vif;

	if (sdiodev->tp_ctrl.net_stop)
		return;

	spin_lock_irqsave(&sdiodev->tx_tp_lock, flags);
	list_for_each_entry(rwnx_vif, &sdiodev->rwnx_hw->vifs, list) {
		if (!rwnx_vif || !rwnx_vif->ndev || !rwnx_vif->up)
			continue;
		netif_tx_stop_all_queues(rwnx_vif->ndev);
	}
	spin_unlock_irqrestore(&sdiodev->tx_tp_lock, flags);
	sdiodev->tp_ctrl.net_stop = true;
	mod_timer(&sdiodev->tp_ctrl.netif_timer, jiffies + msecs_to_jiffies(val));
}

void aicwf_temp_ctrl(struct aic_sdio_dev *sdiodev)
{
	if (sdiodev->tp_ctrl.set_level) {
		if (sdiodev->tp_ctrl.set_level == 1) {
			sdiodev->tp_ctrl.get_level = 1;
			aicwf_netif_ctrl(sdiodev,
					 sdiodev->tp_ctrl.interval_t1 /* TMR_INTERVAL_1 */);
		} else if (sdiodev->tp_ctrl.set_level == 2) {
			sdiodev->tp_ctrl.get_level = 2;
			aicwf_netif_ctrl(sdiodev,
					 sdiodev->tp_ctrl.interval_t2 /* TMR_INTERVAL_2 */);
		}
		return;
	}
	if (sdiodev->tp_ctrl.cur_temp >
		(sdiodev->tp_ctrl.tp_thd_1 - BUFFERING_V1)) {
		if (update_state(sdiodev) == 1) {
			sdiodev->tp_ctrl.get_level = 1;
			sdiodev->tp_ctrl.cur_stat = 1;
			aicwf_netif_ctrl(sdiodev,
					 sdiodev->tp_ctrl.interval_t1 /*TMR_INTERVAL_1 */);
		} else if (update_state(sdiodev) == 2) {
			sdiodev->tp_ctrl.get_level = 2;
			sdiodev->tp_ctrl.cur_stat = 2;
			aicwf_netif_ctrl(sdiodev,
					 sdiodev->tp_ctrl.interval_t2 /*TMR_INTERVAL_2 */);
		}
		return;
	}

	if (sdiodev->tp_ctrl.cur_stat) {
		AICWFDBG(LOGINFO, "reset cur_stat");
		sdiodev->tp_ctrl.cur_stat = 0;
		sdiodev->tp_ctrl.get_level = 0;
	}
}

void aicwf_netif_worker(struct work_struct *work)
{
	struct temp_ctrl *tc = container_of(work, struct temp_ctrl, netif_work);
	struct aic_sdio_dev *sdiodev =
		container_of(tc, struct aic_sdio_dev, tp_ctrl);
	unsigned long flags;
	struct rwnx_vif *rwnx_vif;

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		AICWFDBG(LOGERROR, "%s bus down\n", __func__);
		return;
	}

	spin_lock_irqsave(&sdiodev->tx_tp_lock, flags);
	list_for_each_entry(rwnx_vif, &sdiodev->rwnx_hw->vifs, list) {
		if (!rwnx_vif || !rwnx_vif->ndev || !rwnx_vif->up)
			continue;
		netif_tx_wake_all_queues(rwnx_vif->ndev); // netif_wake_queue(rwnx_vif->ndev);
	}
	spin_unlock_irqrestore(&sdiodev->tx_tp_lock, flags);
	sdiodev->tp_ctrl.net_stop = false;
}

static void aicwf_netif_timer(struct timer_list *t)
{
	struct temp_ctrl *tc = container_of(t, struct temp_ctrl, netif_timer);
	struct aic_sdio_dev *sdiodev =
		container_of(tc, struct aic_sdio_dev, tp_ctrl);

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		AICWFDBG(LOGERROR, "%s bus down\n", __func__);
		return;
	}

	if (!work_pending(&sdiodev->tp_ctrl.netif_work))
		schedule_work(&sdiodev->tp_ctrl.netif_work);
}

void aicwf_temp_ctrl_worker(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw;
	struct mm_set_vendor_swconfig_cfm cfm;
	struct temp_ctrl *tc = container_of(work, struct temp_ctrl, tp_ctrl_work);
	struct aic_sdio_dev *sdiodev =
		container_of(tc, struct aic_sdio_dev, tp_ctrl);

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		AICWFDBG(LOGERROR, "%s bus down\n", __func__);
		return;
	}

	spin_lock_bh(&sdiodev->tp_ctrl.tm_lock);
	if (!sdiodev->tp_ctrl.tm_start) {
		spin_unlock_bh(&sdiodev->tp_ctrl.tm_lock);
		AICWFDBG(LOGERROR, "tp_timer should stop_1\n");
		return;
	}
	spin_unlock_bh(&sdiodev->tp_ctrl.tm_lock);

	rwnx_hw = sdiodev->rwnx_hw;
	rwnx_hw->started_jiffies = jiffies;

	rwnx_send_get_temp_req(rwnx_hw, &cfm);
	sdiodev->tp_ctrl.cur_temp = cfm.temp_comp_get_cfm.degree;

	spin_lock_bh(&sdiodev->tp_ctrl.tm_lock);
	if (sdiodev->tp_ctrl.tm_start)
		mod_timer(&sdiodev->tp_ctrl.tp_ctrl_timer,
			  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
	else
		AICWFDBG(LOGERROR, "tp_timer should stop_2\n");
	spin_unlock_bh(&sdiodev->tp_ctrl.tm_lock);
}

static void aicwf_temp_ctrl_timer(struct timer_list *t)
{
	struct temp_ctrl *tc = container_of(t, struct temp_ctrl, tp_ctrl_timer);
	struct aic_sdio_dev *sdiodev =
		container_of(tc, struct aic_sdio_dev, tp_ctrl);

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		AICWFDBG(LOGERROR, "%s bus down\n", __func__);
		return;
	}

	if (!work_pending(&sdiodev->tp_ctrl.tp_ctrl_work))
		schedule_work(&sdiodev->tp_ctrl.tp_ctrl_work);
}

void aicwf_tp_ctrl_init(struct aic_sdio_dev *sdiodev)
{
	spin_lock_init(&sdiodev->tp_ctrl.tm_lock);
	sdiodev->tp_ctrl.net_stop = false;
	sdiodev->tp_ctrl.on_off = true;
	sdiodev->tp_ctrl.cur_temp = 0;
	sdiodev->tp_ctrl.get_level = 0;
	sdiodev->tp_ctrl.set_level = 0;
	sdiodev->tp_ctrl.interval_t1 = TMR_INTERVAL_1;
	sdiodev->tp_ctrl.interval_t2 = TMR_INTERVAL_2;
	sdiodev->tp_ctrl.cur_stat = 0;
	sdiodev->tp_ctrl.tp_thd_1 = TEMP_THD_1;
	sdiodev->tp_ctrl.tp_thd_2 = TEMP_THD_2;
	sdiodev->tp_ctrl.tm_start = 1;

	timer_setup(&sdiodev->tp_ctrl.tp_ctrl_timer, aicwf_temp_ctrl_timer, 0);
	timer_setup(&sdiodev->tp_ctrl.netif_timer, aicwf_netif_timer, 0);

	INIT_WORK(&sdiodev->tp_ctrl.tp_ctrl_work, aicwf_temp_ctrl_worker);
	INIT_WORK(&sdiodev->tp_ctrl.netif_work, aicwf_netif_worker);
	mod_timer(&sdiodev->tp_ctrl.tp_ctrl_timer,
		  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
}

void aicwf_tp_ctrl_deinit(struct aic_sdio_dev *sdiodev)
{
	spin_lock_bh(&sdiodev->tp_ctrl.tm_lock);
	sdiodev->tp_ctrl.tm_start = 0;
	if (timer_pending(&sdiodev->tp_ctrl.tp_ctrl_timer)) {
		AICWFDBG(LOGINFO, "del tp_ctrl_timer\n");
		//del_timer_sync(&sdiodev->tp_ctrl.tp_ctrl_timer);
		timer_delete_sync(&sdiodev->tp_ctrl.tp_ctrl_timer);
	}
	spin_unlock_bh(&sdiodev->tp_ctrl.tm_lock);

	cancel_work_sync(&sdiodev->tp_ctrl.tp_ctrl_work);

	if (timer_pending(&sdiodev->tp_ctrl.netif_timer)) {
		AICWFDBG(LOGINFO, "del netif_timer\n");
		//del_timer_sync(&sdiodev->tp_ctrl.netif_timer);
		timer_delete_sync(&sdiodev->tp_ctrl.netif_timer);
	}

	cancel_work_sync(&sdiodev->tp_ctrl.netif_work);
}

#endif

int aicwf_sdio_flow_ctrl_msg(struct aic_sdio_dev *sdiodev)
{
	int ret = -1;
	u8 fc_reg = 0;
	u32 count = 0;

	while (true) {
		ret =
			aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.flow_ctrl_reg, &fc_reg);
		if (ret)
			return -1;

		if (aic_sdio_hw->need_flowctrl_mask)
			fc_reg &= SDIOWIFI_FLOWCTRL_MASK_REG;

		if (fc_reg != 0) {
			ret = fc_reg;
			if (ret > tx_aggr_counter)
				ret = tx_aggr_counter;
			return ret;
		}
		if (count >= FLOW_CTRL_RETRY_COUNT) {
			ret = -fc_reg;
			break;
		}
		count++;
		if (count < 30)
			usleep_range(50, 100);
		else if (count < 40)
			usleep_range(2000, 2500);
		else
			usleep_range(10000, 12000);
	}

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

		if (aic_sdio_hw->need_flowctrl_mask)
			fc_reg &= SDIOWIFI_FLOWCTRL_MASK_REG;

		if (fc_reg > DATA_FLOW_CTRL_THRESH) {
			ret = fc_reg;
			if (ret > tx_aggr_counter)
				ret = tx_aggr_counter;
			return ret;
		}
		if (count >= FLOW_CTRL_RETRY_COUNT) {
			ret = -fc_reg;
			break;
		}
		count++;
		if (count < 30)
			usleep_range(50, 50);
		else if (count < 40)
			usleep_range(2000, 2500);
		else
			usleep_range(10000, 12000);
	}

	return ret;
}

int aicwf_sdio_send_pkt(struct aic_sdio_dev *sdiodev, u8 *buf, uint count)
{
	int ret = 0;

	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
	ret =
		sdio_writesb(sdiodev->func, sdiodev->sdio_reg.wr_fifo_addr, buf, count);
	sdio_release_host(sdiodev->func);

	return ret;
}

#ifdef CONFIG_PREALLOC_RX_SKB
int aicwf_sdio_recv_pkt(struct aic_sdio_dev *sdiodev, struct rx_buff *rxbuff,
			u32 size)
{
	int ret;

	if (!rxbuff->data || !size)
		return -EINVAL;
	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
	ret = sdio_readsb(sdiodev->func, rxbuff->data,
			  sdiodev->sdio_reg.rd_fifo_addr, size);
	sdio_release_host(sdiodev->func);

	if (ret < 0)
		return ret;
	rxbuff->len = size;

	return ret;
}
#else
int aicwf_sdio_recv_pkt(struct aic_sdio_dev *sdiodev, struct sk_buff *skbbuf,
			u32 size)
{
	int ret;

	if (!skbbuf || !size)
		return -EINVAL;
	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
	ret = sdio_readsb(sdiodev->func, skbbuf->data,
			  sdiodev->sdio_reg.rd_fifo_addr, size);
	sdio_release_host(sdiodev->func);

	if (ret < 0)
		return ret;
	skbbuf->len = size;

	return ret;
}
#endif

#ifndef CONFIG_PLATFORM_EXTERNAL
#ifdef CONFIG_GPIO_WAKEUP
static int wakeup_enable;
static u32 hostwake_irq_num;
#endif
#endif

/* debug trace */
// static struct wakeup_source *ws_rx_sdio;
// static struct wakeup_source *ws_sdio_pwrctrl;
// static struct wakeup_source *ws_tx_sdio;
#ifdef CONFIG_GPIO_WAKEUP
// static struct wakeup_source *ws;
#endif

#ifdef CONFIG_PLATFORM_EXTERNAL

#define ENTER() (pr_info("AICWF enter %s\n", __func__))
#define LEAVE() (pr_info("AICWF exit %s\n", __func__))

int regist_wakeup_irq(struct aic_sdio_dev *sdiodev)
{
	int ret = -EINVAL;
	struct device *dev = sdiodev->dev;
	struct device_node *node;

	ENTER();

	node = of_find_compatible_node(NULL, NULL, "aic,wifi-wake-host");
	if (!node)
		goto err_exit;

	sdiodev->wf_wake_host = irq_of_parse_and_map(node, 0);
	if (!sdiodev->wf_wake_host) {
		AICWFDBG(LOGERROR, "fail to parse wf_wake_host from device tree\n");
		goto err_exit;
	}

	ret = devm_request_threaded_irq(dev, sdiodev->wf_wake_host,
					NULL, wakeup_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					"wifi_wakeup", sdiodev);
	if (ret) {
		AICWFDBG(LOGERROR, "Failed to request wf_wake_host %d (%d)\n",
			 sdiodev->wf_wake_host, ret);
		goto err_exit;
	}

	disable_irq(sdiodev->wf_wake_host);

	sdiodev->host_wake_wf =
		desc_to_gpio(gpiod_get_index_optional(dev, "host_wake_wifi", 0,
						      GPIOD_OUT_HIGH));

	if (!gpio_is_valid(sdiodev->host_wake_wf)) {
		AICWFDBG(LOGERROR, "get gpio host_wake_wifi failed\n");
		goto err_exit;
	}
	ret = devm_gpio_request_one(dev, sdiodev->host_wake_wf, GPIOD_OUT_HIGH, "host_wake_wifi");
	if (ret < 0) {
		AICWFDBG(LOGERROR, "can't request host_wake_wifi gpio %d\n",
			 sdiodev->host_wake_wf);
		goto err_exit;
	}
	ret = gpio_direction_output(sdiodev->host_wake_wf, 1);
	if (ret < 0) {
		AICWFDBG(LOGERROR,
			 "can't request output direction host_wake_bt gpio %d\n",
				 sdiodev->host_wake_wf);
		goto err_exit;
	}

	// debug trace
	// AICWFDBG(LOGINFO, "%s GPIO: %d, %d\n", __func__, sdiodev->wf_wake_host,
	// sdiodev->host_wake_wf);

	LEAVE();
	return ret;

err_exit:
	sdiodev->wf_wake_host = -1;
	sdiodev->host_wake_wf = -1;
	return ret;
}

void unregist_wakeup_irq(struct aic_sdio_dev *sdiodev)
{
	// struct device *dev = sdiodev->dev;

	// ENTER();
	//LEAVE();
}

void disable_wakeup_irq(struct aic_sdio_dev *sdiodev)
{
	if (sdiodev->wf_wake_host >= 0) {
		if (sdiodev->wake_by_wifi) {
			disable_irq_wake(sdiodev->wf_wake_host);
		} else {
			disable_irq_wake(sdiodev->wf_wake_host);
			disable_irq(sdiodev->wf_wake_host);
		}
	}
}

void enable_wakeup_irq(struct aic_sdio_dev *sdiodev)
{
	if (sdiodev->wf_wake_host >= 0) {
		sdiodev->wake_by_wifi = false;
		enable_irq(sdiodev->wf_wake_host);
		enable_irq_wake(sdiodev->wf_wake_host);
	}
}

irqreturn_t wakeup_irq_handler(int irq, void *priv)
{
	struct aic_sdio_dev *sdiodev = priv;
	struct device *dev = sdiodev->dev;

	// ENTER(); //debug trace

	sdiodev->wake_by_wifi = true;
	disable_irq_nosync(irq);

	pm_wakeup_event(dev, 0);
	pm_system_wakeup();

	// LEAVE(); //debug trace
	return IRQ_HANDLED;
}
#endif


#ifndef CONFIG_PLATFORM_EXTERNAL
#ifdef CONFIG_GPIO_WAKEUP
static irqreturn_t rwnx_hostwake_irq_handler(int irq, void *para)
{
	static int wake_cnt;

	wake_cnt++;

	/* One-shot OOB wake event; it is not a paired SDIO activity unit. */
	__pm_wakeup_event(g_rwnx_plat->sdiodev->rwnx_hw->ws_rx, 1000);

	AICWFDBG(LOGIRQ, "%s(%d): wake_irq_cnt = %d\n", __func__, __LINE__,
		 wake_cnt);

#ifdef CONFIG_OOB
	if (g_rwnx_plat->sdiodev->oob_enable)
		complete(&g_rwnx_plat->sdiodev->bus_if->busirq_trgg);
#endif

	return IRQ_HANDLED;
}

static int rwnx_disable_hostwake_irq(void);
static int rwnx_enable_hostwake_irq(void);
#endif

static int rwnx_register_hostwake_irq(struct device *dev)
{
	int ret = 0; //-1;
#ifdef CONFIG_GPIO_WAKEUP
	unsigned long flag_edge;
	struct aicbsp_feature_t aicwf_feature;

	aicbsp_get_feature(&aicwf_feature);
	if (aicwf_feature.irqf == 0)
		flag_edge = IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND;
	else
		flag_edge = IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND;

	if (wakeup_enable) {
		/* debug func */
		// ws = wakeup_source_register(dev, "wifisleep");
		// ws_tx_sdio = wakeup_source_register(dev, "wifi_tx_sleep");
		// ws_rx_sdio = wakeup_source_register(dev, "wifi_rx_sleep");
		// ws_sdio_pwrctrl = wakeup_source_register(dev, "sdio_pwrctrl_sleep");
		ret = device_init_wakeup(dev, true);
		if (ret < 0) {
			pr_err("%s(%d): device init wakeup failed!\n", __func__, __LINE__);
			return ret;
		}

		ret = dev_pm_set_wake_irq(dev, hostwake_irq_num);
		if (ret < 0) {
			pr_err("%s(%d): can't enable wakeup src!\n", __func__, __LINE__);
			goto fail1;
		}

		ret = request_irq(hostwake_irq_num, rwnx_hostwake_irq_handler,
				  flag_edge, "rwnx_hostwake_irq", NULL);

		if (ret < 0) {
			pr_err("%s(%d): request_irq fail! ret = %d\n", __func__, __LINE__,
			       ret);
			goto fail2;
		}
	}
	rwnx_disable_hostwake_irq();
	dev_pm_clear_wake_irq(dev);
	rwnx_enable_hostwake_irq();
	AICWFDBG(LOGINFO, "%s(%d)\n", __func__, __LINE__);
	return ret;

fail2:
	dev_pm_clear_wake_irq(dev);
fail1:
	device_init_wakeup(dev, false);
	/* debug trace */
	// wakeup_source_unregister(ws);
	// wakeup_source_unregister(ws_tx_sdio);
	// wakeup_source_unregister(ws_rx_sdio);
	// wakeup_source_unregister(ws_sdio_pwrctrl);
#endif // CONFIG_GPIO_WAKEUP
	return ret;
}

static int rwnx_unregister_hostwake_irq(struct device *dev)
{
#ifdef CONFIG_GPIO_WAKEUP
	rwnx_disable_hostwake_irq();
	if (wakeup_enable) {
		device_init_wakeup(dev, false);
		dev_pm_clear_wake_irq(dev);
		/* debug trace */
		// wakeup_source_unregister(ws);
		// wakeup_source_unregister(ws_tx_sdio);
		// wakeup_source_unregister(ws_rx_sdio);
		// wakeup_source_unregister(ws_sdio_pwrctrl);
	}
	free_irq(hostwake_irq_num, NULL);
#endif // CONFIG_GPIO_WAKEUP
	AICWFDBG(LOGINFO, "%s(%d)\n", __func__, __LINE__);
	return 0;
}

#ifdef CONFIG_GPIO_WAKEUP
static int rwnx_enable_hostwake_irq(void)
{
#ifdef CONFIG_GPIO_WAKEUP
	enable_irq(hostwake_irq_num);
	enable_irq_wake(hostwake_irq_num);
#endif // CONFIG_GPIO_WAKEUP
	AICWFDBG(LOGINFO, "%s(%d)\n", __func__, __LINE__);
	return 0;
}

static int rwnx_disable_hostwake_irq(void)
{
	AICWFDBG(LOGINFO, "%s(%d)\n", __func__, __LINE__);
#ifdef CONFIG_GPIO_WAKEUP
	disable_irq_nosync(hostwake_irq_num);
#endif // CONFIG_GPIO_WAKEUP
	return 0;
}
#endif
#endif

//extern int rwnx_send_me_set_lp_level(struct rwnx_hw *rwnx_hw, u8 lp_level, u8 disable_filter);

static int aicwf_sdio_probe(struct sdio_func *func,
			    const struct sdio_device_id *id)
{
	struct mmc_host *host;
	struct aic_sdio_dev *sdiodev;
	struct aicwf_bus *bus_if;
	int err = -ENODEV;
	u16 chipid;

	AICWFDBG(LOGDEBUG, "%s:%d\n", __func__, func->num);
	AICWFDBG(LOGDEBUG, "Class=%x\n", func->class);
	AICWFDBG(LOGDEBUG, "sdio vendor ID: 0x%04x\n", func->vendor);
	AICWFDBG(LOGDEBUG, "sdio device ID: 0x%04x\n", func->device);
	AICWFDBG(LOGDEBUG, "Function#: %d\n", func->num);

	if (!func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	host = func->card->host;
	if (func->num != 1)
		return err;

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

	err = aicwf_sdio_chipmatch(func->vendor, func->device, &chipid);
	sdiodev->chipid = chipid;
	aic_sdio_hw = aic_sdio_get_props(chipid);

	sdiodev->func = func;
	sdiodev->bus_if = bus_if;

#ifdef CONFIG_OOB
	sdiodev->oob_enable = aic_sdio_hw->oob_support;
#else
	sdiodev->oob_enable = false;
#endif

	atomic_set(&sdiodev->is_bus_suspend, 0);
	bus_if->bus_priv.sdio = sdiodev;

	dev_set_drvdata(&func->dev, bus_if);
	sdiodev->dev = &func->dev;

	if (aic_sdio_hw->use_func2)
		sdiodev->func2 = func->card->sdio_func[1];

	//sdio func init start
	if (!aic_sdio_hw->use_sdiov3_func)
		err = aicwf_sdio_func_init(sdiodev);
	else
		err = aicwf_sdiov3_func_init(sdiodev);

	if (err < 0) {
		sdio_err("sdio func init fail\n");
		goto fail;
	}
	// sdio func init end

	if (!aicwf_sdio_bus_init(sdiodev)) {
		sdio_err("sdio bus init fail\n");
		err = -1;
		goto fail;
	}

	host->caps |= MMC_CAP_NONREMOVABLE;
	aicwf_rwnx_sdio_platform_init(sdiodev);
	aicwf_hostif_ready();
#ifdef CONFIG_PLATFORM_EXTERNAL
	regist_wakeup_irq(sdiodev);
#else
	err = rwnx_register_hostwake_irq(sdiodev->dev);
#endif
	if (err != 0)
		return err;

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_OOB
	if (sdiodev->oob_enable) {
		AICWFDBG(LOGINFO, "%s SDIOWIFI_INTR_CONFIG_REG Disable\n", __func__);
		sdio_claim_host(sdiodev->func);
		// disable sdio interrupt
		err = aicwf_sdio_writeb(sdiodev, SDIOWIFI_INTR_CONFIG_REG, 0x0);
		if (err < 0)
			sdio_err("reg:%d write failed!\n", SDIOWIFI_INTR_CONFIG_REG);
		sdio_release_irq(sdiodev->func);
		sdio_release_host(sdiodev->func);
	}
#endif

#endif // CONFIG_GPIO_WAKEUP
	device_disable_async_suspend(sdiodev->dev);

	return 0;
fail:
	aicwf_sdio_func_deinit(sdiodev);
	dev_set_drvdata(&func->dev, NULL);
	kfree(sdiodev);
	kfree(bus_if);
	aicwf_hostif_fail();
	return err;
}

void aicwf_sdio_probe_(struct sdio_func *func, const struct sdio_device_id *id)
{
	aicwf_sdio_probe(func, NULL);
}

static void aicwf_sdio_remove(struct sdio_func *func)
{
	struct mmc_host *host;
	struct aicwf_bus *bus_if = NULL;
	struct aic_sdio_dev *sdiodev = NULL;

	host = func->card->host;
	host->caps &= ~MMC_CAP_NONREMOVABLE;
	bus_if = dev_get_drvdata(&func->dev);
	if (!bus_if)
		return;

	sdiodev = bus_if->bus_priv.sdio;
	if (!sdiodev)
		return;

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_SLEEP_ST);
#endif

	sdiodev->bus_if->state = BUS_DOWN_ST;
	aicwf_sdio_release(sdiodev);
	aicwf_sdio_func_deinit(sdiodev);
#ifdef CONFIG_PLATFORM_EXTERNAL
	unregist_wakeup_irq(sdiodev);
#else
	rwnx_unregister_hostwake_irq(sdiodev->dev);
#endif
	dev_set_drvdata(&sdiodev->func->dev, NULL);
	kfree(sdiodev);
	kfree(bus_if);
}

void aicwf_sdio_remove_(struct sdio_func *func)
{
	aicwf_sdio_remove(func);
}

#ifndef CONFIG_FDRV_NO_REG_SDIO
static int aicwf_sdio_suspend(struct device *dev)
{
	int ret = 0;
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	mmc_pm_flag_t sdio_flags;
	struct rwnx_vif *rwnx_vif, *tmp;
#ifdef CONFIG_AIC8800_NO_WAKE_COUNT
	int cnt = 0;
#endif
	int sus_abort = 0;

	list_for_each_entry_safe(rwnx_vif, tmp, &sdiodev->rwnx_hw->vifs, list) {
		if (rwnx_vif->ndev)
			netif_device_detach(rwnx_vif->ndev);
	}

	if (sdiodev->rwnx_hw->testmode == 1)
		pr_err("AICWF %s, sleep cmd is not supported in RF test mode\n", __func__);
#ifdef CONFIG_AIC8800_NO_WAKE_COUNT //move to when set supend is received
	else
		rwnx_send_me_set_lp_level(sdiodev->rwnx_hw, 1, 0); //dynamic switch
#endif

#if (defined(CONFIG_AIC8800_AUTO_POWERSAVE) && defined(CONFIG_SDIO_PWRCTRL))
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_ACTIVE_ST);
	if (aic_sdio_hw->auto_ps_support) {
		AICWFDBG(LOGDEBUG, "auto_ps enter\n");
		ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 0x8);
		if (ret)
			AICWFDBG(LOGERROR, "sdio enter auto_ps fail\n");
	}
#endif

#ifdef CONFIG_AIC8800_NO_WAKE_COUNT
	while (&sdiodev->rwnx_hw->scan_request && rwnx_hw->scanning) {
		/* move to when set supend is received, the theory does not enforce this condition,
		 * and the code is retained for now
		 */
		pr_info("AICWF wait scan_cfm\n");
		if (cnt == 0) {
			rwnx_send_scanu_cancel_req(sdiodev->rwnx_hw, NULL);
			continue;
		}
		msleep(20);
		cnt += 1;
		if (cnt >= 40)
			break;
	}
#endif

	sdio_flags = sdio_get_host_pm_caps(sdiodev->func);
	if (!(sdio_flags & MMC_PM_KEEP_POWER))
		return -EINVAL;
	ret = sdio_set_host_pm_flags(sdiodev->func, MMC_PM_KEEP_POWER);
	if (ret)
		return ret;

#ifdef CONFIG_AIC8800_TEMP_CONTROL
	//del_timer_sync(&sdiodev->tp_ctrl.tp_ctrl_timer);
	timer_delete_sync(&sdiodev->tp_ctrl.tp_ctrl_timer);
	cancel_work_sync(&sdiodev->tp_ctrl.tp_ctrl_work);
#endif

	atomic_set(&sdiodev->suspending, 1);
	smp_mb__after_atomic();
	if (!wait_event_timeout(sdiodev->activity_waitq,
				atomic_read(&sdiodev->activity_count) == 0,
				msecs_to_jiffies(600)))
		sus_abort = 1;
	if (sus_abort) {
		atomic_set(&sdiodev->suspending, 0);
#ifdef CONFIG_AIC8800_TEMP_CONTROL
		mod_timer(&sdiodev->tp_ctrl.tp_ctrl_timer,
			  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
#endif
		pr_info("AICWF ws active dont suspend\n");

#if defined(CONFIG_AIC8800_AUTO_POWERSAVE)
		aicwf_sdio_pwr_stctl(sdiodev, SDIO_ACTIVE_ST);
		if (aic_sdio_hw->auto_ps_support) {
			AICWFDBG(LOGDEBUG, "auto_ps abort\n");
			ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 0x8);
			if (ret)
				AICWFDBG(LOGERROR, "sdio exit auto_ps fail\n");
		}
#elif defined(CONFIG_AIC8800_NO_WAKE_COUNT)
		if (sdiodev->rwnx_hw->testmode == 0)
			rwnx_send_me_set_lp_level(sdiodev->rwnx_hw, 0, 1); //dynamic switch
#endif
		list_for_each_entry_safe(rwnx_vif, tmp, &sdiodev->rwnx_hw->vifs, list) {
			if (rwnx_vif->ndev) {
				pr_info("AICWF suspend restore\n");
				netif_device_attach(rwnx_vif->ndev);
			}
		}
		return -EBUSY;
	}

#ifdef CONFIG_AIC8800_TEMP_CONTROL
	//del_timer_sync(&sdiodev->tp_ctrl.netif_timer);
	timer_delete_sync(&sdiodev->tp_ctrl.netif_timer);
	cancel_work_sync(&sdiodev->tp_ctrl.netif_work);
#endif

#ifndef CONFIG_AIC8800_AUTO_POWERSAVE
	while (sdiodev->state == SDIO_ACTIVE_ST) {
		if (down_interruptible(&sdiodev->tx_priv->txctl_sema))
			continue;
#if defined(CONFIG_SDIO_PWRCTRL)
		aicwf_sdio_pwr_stctl(sdiodev, SDIO_SLEEP_ST);
#endif
		up(&sdiodev->tx_priv->txctl_sema);
		break;
	}
#else
#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_SLEEP_ST);
#endif
#endif

#ifdef CONFIG_GPIO_WAKEUP
//  rwnx_enable_hostwake_irq();
#endif

#ifdef CONFIG_PLATFORM_EXTERNAL
	enable_wakeup_irq(sdiodev);
#endif

	atomic_set(&sdiodev->is_bus_suspend, 1);

	return 0;
}

static int aicwf_sdio_resume(struct device *dev)
{
#if defined(CONFIG_AIC8800_AUTO_POWERSAVE) && defined(CONFIG_SDIO_PWRCTRL)
	int ret;
#endif
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct rwnx_vif *rwnx_vif, *tmp;

#ifdef CONFIG_AIC8800_NO_WAKE_COUNT
	if (sdiodev->rwnx_hw->testmode == 0)
		rwnx_send_me_set_lp_level(sdiodev->rwnx_hw, 0, 1); //dynamic switch
#endif
#ifdef CONFIG_AIC8800_TEMP_CONTROL
	mod_timer(&sdiodev->tp_ctrl.tp_ctrl_timer,
		  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
#endif
	list_for_each_entry_safe(rwnx_vif, tmp, &sdiodev->rwnx_hw->vifs, list) {
		if (rwnx_vif->ndev) {
			AICWFDBG(LOGDEBUG, "resume netif_attach\n");
			netif_device_attach(rwnx_vif->ndev);
		}
	}

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_ACTIVE_ST);
#endif

#if defined(CONFIG_AIC8800_AUTO_POWERSAVE) && defined(CONFIG_SDIO_PWRCTRL)
	if (aic_sdio_hw->auto_ps_support) {
		AICWFDBG(LOGDEBUG, "auto_ps exit\n");
		ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 0x8);
		if (ret)
			AICWFDBG(LOGERROR, "sdio exit auto_ps fail\n");
	}
#endif

	atomic_set(&sdiodev->is_bus_suspend, 0);
	atomic_set(&sdiodev->suspending, 0);

#ifdef CONFIG_PLATFORM_EXTERNAL
	disable_wakeup_irq(sdiodev);
#endif

	return 0;
}
#endif

static const struct sdio_device_id aicwf_sdmmc_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_AIC8800D80, SDIO_DEVICE_ID_AIC8800D80)},
	{},
};

MODULE_DEVICE_TABLE(sdio, aicwf_sdmmc_ids);

#ifndef CONFIG_FDRV_NO_REG_SDIO
static const struct dev_pm_ops aicwf_sdio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aicwf_sdio_suspend, aicwf_sdio_resume)
};

static struct sdio_driver aicwf_sdio_driver = {
	.probe = aicwf_sdio_probe,
	.remove = aicwf_sdio_remove,
	.name = AICWF_SDIO_NAME,
	.id_table = aicwf_sdmmc_ids,
	.drv = {
			.pm = &aicwf_sdio_pm_ops,
		},
};
#endif

#ifdef CONFIG_FDRV_NO_REG_SDIO
extern struct sdio_func *get_sdio_func(void);
#endif

void aicwf_sdio_register(void)
{
#ifndef CONFIG_FDRV_NO_REG_SDIO
	if (sdio_register_driver(&aicwf_sdio_driver))
		AICWFDBG(LOGERROR, "%s sdio_register_driver\r\n", __func__);
#else
	aicwf_sdio_probe_(get_sdio_func(), NULL);
#endif
}

void aicwf_sdio_exit(void)
{
	if (g_rwnx_plat && g_rwnx_plat->enabled)
		rwnx_platform_deinit(g_rwnx_plat->sdiodev->rwnx_hw);
	else
		AICWFDBG(LOGERROR, "%s g_rwnx_plat is not ready \r\n", __func__);

	usleep_range(50, 60);

#ifndef CONFIG_FDRV_NO_REG_SDIO
	sdio_unregister_driver(&aicwf_sdio_driver);
#else
	aicwf_sdio_remove_(get_sdio_func());
#endif

	kfree(g_rwnx_plat);
}

#if defined(CONFIG_SDIO_PWRCTRL)
int aicwf_sdio_wakeup(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;
	int read_retry;
	int write_retry = 1;
	int wakeup_reg_val = 0;

	wakeup_reg_val = aic_sdio_hw->wakeup_reg_val;

	if (sdiodev->state == SDIO_SLEEP_ST) {
		AICWFDBG(LOGSDPWRC, "%s w\n", __func__);

		while (write_retry) {
			ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg,
						wakeup_reg_val);
			if (ret) {
				txrx_err("sdio wakeup fail\n");
				ret = -1;
			} else {
				read_retry = 50;
				while (read_retry) {
					u8 val;

					ret = aicwf_sdio_readb(sdiodev,
							       sdiodev->sdio_reg.wakeup_reg,
							       &val);
					if (ret < 0)
						txrx_err("sdio wakeup read fail\n");
					else if ((val & 0x1) == 0)
						break;
					read_retry--;
					usleep_range(50, 60);
				}
				if (read_retry != 0)
					break;
			}
			sdio_dbg("write retry:  %d\n", write_retry);
			write_retry--;
			usleep_range(40, 50);
		}

		sdiodev->state = SDIO_ACTIVE_ST;
		aicwf_sdio_pwrctl_timer(sdiodev, sdiodev->active_duration);
	}
	return ret;
}

int aicwf_sdio_sleep_allow(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;
	struct aicwf_bus *bus_if = sdiodev->bus_if;
	struct rwnx_hw *rwnx_hw = sdiodev->rwnx_hw;
	u8 read_retry;
	u8 val;

	if (bus_if->state == BUS_DOWN_ST) {
		ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.sleep_reg, 0x10);
		if (ret)
			sdio_err("Write sleep fail!\n");
		aicwf_sdio_pwrctl_timer(sdiodev, 0);
		return ret;
	}

	sdio_info("sleep: %d, %d\n", sdiodev->state, rwnx_hw->scanning);
	if (sdiodev->state == SDIO_ACTIVE_ST  && !rwnx_hw->scanning && !rwnx_hw->is_p2p_alive &&
	    !rwnx_hw->is_p2p_connected &&
	    (int)(atomic_read(&sdiodev->tx_priv->tx_pktcnt) <= 0) &&
	    !sdiodev->tx_priv->cmd_txstate &&
	    (int)(atomic_read(&sdiodev->rx_priv->rx_cnt) == 0)) {
		AICWFDBG(LOGSDPWRC, "%s s\n", __func__);
		if (!aic_sdio_hw->use_func2) {
			if (aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 0x02) < 0)
				sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.wakeup_reg);
		} else {
			if (aicwf_sdio_func2_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 0x2) < 0)
				sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.wakeup_reg);
			read_retry = 100;
			while (read_retry) {
				val = 0;
				if (aicwf_sdio_func2_readb(sdiodev,
							   sdiodev->sdio_reg.wakeup_reg,
							   &val) < 0)
					sdio_err("reg %d read fail\n",
						 sdiodev->sdio_reg.wakeup_reg);
				else if ((val & 0x2) == 0)
					break;
				sdio_err("val:%d\n", val);
				read_retry--;
				if (read_retry < 90)
					pr_warn("warning: read cnt %d\n", read_retry);
				usleep_range(50, 60);
			}
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

	down(&sdiodev->pwrctl_wakeup_sema);

	if (sdiodev->state == target) {
		if (target == SDIO_ACTIVE_ST)
			aicwf_sdio_pwrctl_timer(sdiodev, sdiodev->active_duration);
		up(&sdiodev->pwrctl_wakeup_sema);
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

	up(&sdiodev->pwrctl_wakeup_sema);
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
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	int ret = 0;

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwrctl_timer(sdiodev, 0);
#endif

	bus_if->state = BUS_DOWN_ST;
	if (sdiodev->tx_priv) {
		ret = down_interruptible(&sdiodev->tx_priv->txctl_sema);
		if (ret)
			AICWFDBG(LOGERROR, "down txctl_sema fail\n");
	}

#if defined(CONFIG_SDIO_PWRCTRL)
	aicwf_sdio_pwr_stctl(sdiodev, SDIO_SLEEP_ST);
#endif

	if (sdiodev->tx_priv) {
		if (!ret)
			up(&sdiodev->tx_priv->txctl_sema);
		aicwf_frame_queue_flush(&sdiodev->tx_priv->txq);
	}
}

#ifdef CONFIG_PREALLOC_RX_SKB
struct rx_buff *aicwf_sdio_readframes(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;
	u32 size = 0;
	struct aicwf_bus *bus_if = dev_get_drvdata(sdiodev->dev);
	struct rx_buff *rxbuff;

	if (bus_if->state == BUS_DOWN_ST) {
		sdio_dbg("bus down\n");
		return NULL;
	}

	size = sdiodev->rx_priv->data_len;
	rxbuff = aicwf_prealloc_rxbuff_alloc(&sdiodev->rx_priv->rxbuff_lock);
	if (!rxbuff) {
		pr_err("failed to alloc rxbuff\n");
		return NULL;
	}
	rxbuff->len = 0;
	rxbuff->start = rxbuff->data;
	rxbuff->read = rxbuff->start;
	rxbuff->end = rxbuff->data + size;

	ret = aicwf_sdio_recv_pkt(sdiodev, rxbuff, size);
	if (ret) {
		pr_err("%s %d, sdio recv pkt fail\n", __func__, __LINE__);
		aicwf_prealloc_rxbuff_free(rxbuff, &sdiodev->rx_priv->rxbuff_lock);
		return NULL;
	}

	return rxbuff;
}
#else
struct sk_buff *aicwf_sdio_readframes(struct aic_sdio_dev *sdiodev)
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

	ret = aicwf_sdio_recv_pkt(sdiodev, skb, size);
	if (ret) {
		dev_kfree_skb(skb);
		skb = NULL;
	}

	return skb;
}
#endif

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

	if (aic_sdio_hw->use_flowctrl_msg) {
		buffer_cnt = aicwf_sdio_flow_ctrl_msg(sdiodev);
		while ((buffer_cnt <= 0 ||
			(buffer_cnt > 0 && len > (buffer_cnt * BUFFER_SIZE))) &&
			   retry < 10) {
			retry++;
			buffer_cnt = aicwf_sdio_flow_ctrl_msg(sdiodev);
			pr_info("buffer_cnt = %d\n", buffer_cnt);
		}
	}
	down(&sdiodev->tx_priv->cmd_txsema);

	if (aic_sdio_hw->use_flowctrl_msg) {
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
		err = aicwf_sdio_send_pkt(sdiodev, payload, len);
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
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	unsigned long flags;
#endif

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
		// Only wake up if someone is actually waiting
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
		if (sdiodev->bus_if->state == BUS_DOWN_ST)
			break;
#ifdef CONFIG_AIC8800_TEMP_CONTROL
		if (sdiodev->tp_ctrl.on_off)
			aicwf_temp_ctrl(sdiodev);
#endif
		if (sdiodev->tx_priv->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
			if (sdiodev->tx_priv->cmd_txstate)
				break;
			sdiodev->tx_priv->fw_avail_bufcnt = aicwf_sdio_flow_ctrl(sdiodev);
		} else {
			if (sdiodev->tx_priv->cmd_txstate) {
				aicwf_sdio_send(sdiodev->tx_priv, 1);
				break;
			}
			aicwf_sdio_send(sdiodev->tx_priv, 0);
		}
	}

#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	spin_lock_irqsave(&sdiodev->tx_flow_lock, flags);
	if (atomic_read(&sdiodev->tx_priv->tx_pktcnt) < tx_fc_low_water) {
		/* debug trace */
		// printk("sdiodev->tx_priv->tx_pktcnt < tx_fc_low_water:%d %d\r\n",
		//     atomic_read(&sdiodev->tx_priv->tx_pktcnt), tx_fc_low_water);
		if (sdiodev->flowctrl) {
			sdiodev->flowctrl = 0;
			aicwf_sdio_tx_netif_flowctrl(sdiodev->rwnx_hw, false);
		}
	}
	spin_unlock_irqrestore(&sdiodev->tx_flow_lock, flags);
#endif

	up(&sdiodev->tx_priv->txctl_sema);
}

static int aicwf_sdio_bus_txdata(struct device *dev, struct sk_buff *pkt)
{
	uint prio;
	int ret = -EBADE;
	struct rwnx_txhdr *txhdr = NULL;
	int headroom = 0;
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	unsigned long flags;
#endif

	if (bus_if->state == BUS_DOWN_ST) {
		sdio_err("bus_if stopped\n");
		txhdr = (struct rwnx_txhdr *)pkt->data;
		headroom = txhdr->sw_hdr->headroom;
		kmem_cache_free(txhdr->sw_hdr->rwnx_vif->rwnx_hw->sw_txhdr_cache,
				txhdr->sw_hdr);
		skb_pull(pkt, headroom);
		consume_skb(pkt);
		return -1;
	}

	prio = (pkt->priority & 0x7);
	spin_lock_bh(&sdiodev->tx_priv->txqlock);
	if (!aicwf_frame_enq(sdiodev->dev, &sdiodev->tx_priv->txq, pkt, prio)) {
		txhdr = (struct rwnx_txhdr *)pkt->data;
		headroom = txhdr->sw_hdr->headroom;
		kmem_cache_free(txhdr->sw_hdr->rwnx_vif->rwnx_hw->sw_txhdr_cache,
				txhdr->sw_hdr);
		skb_pull(pkt, headroom);
		consume_skb(pkt);
		spin_unlock_bh(&sdiodev->tx_priv->txqlock);
		return -ENOSR;
		goto flowctrl;
	} else {
		ret = 0;
	}

	atomic_inc(&sdiodev->tx_priv->tx_pktcnt);
	spin_unlock_bh(&sdiodev->tx_priv->txqlock);
	complete(&bus_if->bustx_trgg);

flowctrl:
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	spin_lock_irqsave(&sdiodev->tx_flow_lock, flags);
	if (atomic_read(&sdiodev->tx_priv->tx_pktcnt) >= tx_fc_high_water) {
		/* debug trace */
		// printk("sdiodev->tx_priv->tx_pktcnt >= tx_fc_high_water:%d %d\r\n",
		//   atomic_read(&sdiodev->tx_priv->tx_pktcnt), tx_fc_high_water);
		if (!sdiodev->flowctrl) {
			sdiodev->flowctrl = 1;
			aicwf_sdio_tx_netif_flowctrl(sdiodev->rwnx_hw, true);
		}
	}
	spin_unlock_irqrestore(&sdiodev->tx_flow_lock, flags);
#endif

	return ret;
}

static int aicwf_sdio_bus_txmsg(struct device *dev, u8 *msg, uint msglen)
{
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
	return 0;
}

int aicwf_sdio_send(struct aicwf_tx_priv *tx_priv, u8 txnow)
{
	struct sk_buff *pkt;
	struct aic_sdio_dev *sdiodev = tx_priv->sdiodev;
	u32 aggr_len = 0;
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	unsigned long flags;
#endif

	aggr_len = (tx_priv->tail - tx_priv->head);
	if (((atomic_read(&tx_priv->aggr_count) == 0) && aggr_len != 0) ||
	    ((atomic_read(&tx_priv->aggr_count) != 0) && aggr_len == 0)) {
		if (aggr_len > 0)
			aicwf_sdio_aggrbuf_reset(tx_priv);
		return 0;
	}

	if (atomic_read(&tx_priv->aggr_count) ==
	    (tx_priv->fw_avail_bufcnt - DATA_FLOW_CTRL_THRESH)) {
		if (atomic_read(&tx_priv->aggr_count) > 0) {
			tx_priv->fw_avail_bufcnt -= atomic_read(&tx_priv->aggr_count);
			aicwf_sdio_aggr_send(tx_priv); // send and check the next pkt;
		}
		return 0;
	}
	spin_lock_bh(&sdiodev->tx_priv->txqlock);
	pkt = aicwf_frame_dequeue(&sdiodev->tx_priv->txq);
	if (!pkt) {
		sdio_err("txq no pkt\n");
		spin_unlock_bh(&sdiodev->tx_priv->txqlock);
		return 0;
	}
	//atomic_dec(&sdiodev->tx_priv->tx_pktcnt);
	spin_unlock_bh(&sdiodev->tx_priv->txqlock);

#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	spin_lock_irqsave(&sdiodev->tx_flow_lock, flags);
	if (atomic_read(&sdiodev->tx_priv->tx_pktcnt) < tx_fc_low_water) {
		/* debug trace */
		// printk("sdiodev->tx_priv->tx_pktcnt < tx_fc_low_water:%d %d\r\n",
		//     atomic_read(&sdiodev->tx_priv->tx_pktcnt), tx_fc_low_water);
		if (sdiodev->flowctrl) {
			sdiodev->flowctrl = 0;
			aicwf_sdio_tx_netif_flowctrl(sdiodev->rwnx_hw, false);
		}
	}
	spin_unlock_irqrestore(&sdiodev->tx_flow_lock, flags);
#endif

	if (!tx_priv || !tx_priv->tail || !pkt)
		txrx_err("null error\n");
	if (aicwf_sdio_aggr(tx_priv, pkt)) {
		aicwf_sdio_aggrbuf_reset(tx_priv);
		sdio_err("add aggr pkts failed!\n");
		atomic_dec(&sdiodev->tx_priv->tx_pktcnt);
		return 0;
	}

	//when aggr finish or there is cmd to send, just send this aggr pkt to fw
	if ((int)atomic_read(&sdiodev->tx_priv->tx_pktcnt) == 1 ||
	    txnow || (atomic_read(&tx_priv->aggr_count) ==
	    (tx_priv->fw_avail_bufcnt - DATA_FLOW_CTRL_THRESH))) {
		tx_priv->fw_avail_bufcnt -= atomic_read(&tx_priv->aggr_count);
		aicwf_sdio_aggr_send(tx_priv);
		atomic_dec(&sdiodev->tx_priv->tx_pktcnt);
		return 0;
	}
	atomic_dec(&sdiodev->tx_priv->tx_pktcnt);
	return 0;
}

int aicwf_sdio_aggr(struct aicwf_tx_priv *tx_priv, struct sk_buff *pkt)
{
	struct rwnx_txhdr *txhdr = (struct rwnx_txhdr *)pkt->data;
	u8 *start_ptr = tx_priv->tail;
	u8 sdio_header[4];
	u8 adjust_str[4] = {0, 0, 0, 0};
	u32 curr_len = 0;
	int allign_len = 0;
	int headroom;

	sdio_header[0] =
		((pkt->len - txhdr->sw_hdr->headroom + sizeof(struct txdesc_api)) &
		 0xff);
	sdio_header[1] =
		(((pkt->len - txhdr->sw_hdr->headroom + sizeof(struct txdesc_api)) >>
		  8) &
		 0x0f);
	sdio_header[2] = 0x01; // data
	if (!aic_sdio_hw->use_hdr_checksum)
		sdio_header[3] = 0; // reserved
	else if (aic_sdio_hw->use_hdr_checksum)
		sdio_header[3] = crc8_ponl_107(&sdio_header[0], 3); // crc8

	memcpy(tx_priv->tail, (u8 *)&sdio_header, sizeof(sdio_header));
	tx_priv->tail += sizeof(sdio_header);
	// payload
	memcpy(tx_priv->tail, (u8 *)(long)&txhdr->sw_hdr->desc,
	       sizeof(struct txdesc_api));
	tx_priv->tail += sizeof(struct txdesc_api); // hostdesc
	memcpy(tx_priv->tail, (u8 *)((u8 *)txhdr + txhdr->sw_hdr->headroom),
	       pkt->len - txhdr->sw_hdr->headroom);
	tx_priv->tail += (pkt->len - txhdr->sw_hdr->headroom);

	// word alignment
	curr_len = tx_priv->tail - tx_priv->head;
	if (curr_len & (TX_ALIGNMENT - 1)) {
		allign_len = roundup(curr_len, TX_ALIGNMENT) - curr_len;
		memcpy(tx_priv->tail, adjust_str, allign_len);
		tx_priv->tail += allign_len;
	}

	if (aic_sdio_hw->need_fix_hdr_len) {
		start_ptr[0] = ((tx_priv->tail - start_ptr - 4) & 0xff);
		start_ptr[1] = (((tx_priv->tail - start_ptr - 4) >> 8) & 0x0f);
	}
	tx_priv->aggr_buf->dev = pkt->dev;

	if (!txhdr->sw_hdr->need_cfm) {
		headroom = txhdr->sw_hdr->headroom;
		kmem_cache_free(txhdr->sw_hdr->rwnx_vif->rwnx_hw->sw_txhdr_cache,
				txhdr->sw_hdr);
		skb_pull(pkt, headroom);
		consume_skb(pkt);
	}

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

extern void set_irq_handler(void *fn);

static int aicwf_sdio_bus_start(struct device *dev)
{
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);
	struct aic_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	int ret = 0;

	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	sdio_claim_host(sdiodev->func);
#ifndef CONFIG_FDRV_NO_REG_SDIO
	sdio_claim_irq(sdiodev->func, aicwf_sdio_hal_irqhandler);
#else
	set_irq_handler(aicwf_sdio_hal_irqhandler);
#endif
	if (aic_sdio_hw->need_func0_intr) {
		sdio_f0_writeb(sdiodev->func, 0x07, 0x04, &ret);
		if (ret)
			sdio_err("set func0 int en fail %d\n", ret);
	}
	sdio_release_host(sdiodev->func);

	// enable sdio interrupt
	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x07);
	if (ret != 0)
		sdio_err("intr register failed:%d\n", ret);

	bus_if->state = BUS_UP_ST;

	return ret;
}

#ifdef CONFIG_AIC8800_TXRX_THREAD_PRIO

#include "uapi/linux/sched/types.h"

static int bustx_thread_prio = 1;
module_param_named(bustx_thread_prio, bustx_thread_prio, int, 0644);
// module_param(bustx_thread_prio, int, 0);
static int busrx_thread_prio = 1;
module_param_named(busrx_thread_prio, busrx_thread_prio, int, 0644);
// module_param(busrx_thread_prio, int, 0);
#endif

#ifdef CONFIG_OOB
static int rx_thread_wait_to = 1000;
module_param_named(rx_thread_wait_to, rx_thread_wait_to, int, 0644);

// new oob feature
int sdio_busirq_thread(void *data)
{
	struct aicwf_rx_priv *rx_priv = (struct aicwf_rx_priv *)data;
	struct aicwf_bus *bus_if = rx_priv->sdiodev->bus_if;

#ifdef CONFIG_AIC8800_TXRX_THREAD_PRIO
	if (busrx_thread_prio > 0)
		sched_set_fifo_low(current);
#endif

	AICWFDBG(LOGINFO, "%s the policy of current thread is:%d\n", __func__,
		 current->policy);
	AICWFDBG(LOGINFO, "%s the rt_priority of current thread is:%d\n", __func__,
		 current->rt_priority);
	AICWFDBG(LOGINFO, "%s the current pid is:%d\n", __func__, current->pid);

	while (1) {
		if (kthread_should_stop()) {
			AICWFDBG(LOGERROR, "sdio busirq thread stop\n");
			break;
		}

		if (!wait_for_completion_timeout(&bus_if->busirq_trgg,
						 msecs_to_jiffies(rx_thread_wait_to))) {
			AICWFDBG(LOGRXPOLL, "%s wait for completion timeout \r\n", __func__);
		}

		if (bus_if->state == BUS_DOWN_ST)
			continue;

#ifdef CONFIG_SDIO_PWRCTRL
		while (atomic_read(&bus_if->bus_priv.sdio->is_bus_suspend) == 1) {
			AICWFDBG(LOGDEBUG, "%s waiting for sdio bus resume \r\n", __func__);
			msleep(100);
		}
		aicwf_sdio_pwr_stctl(bus_if->bus_priv.sdio, SDIO_ACTIVE_ST);
#endif // CONFIG_SDIO_PWRCTRL
		aicwf_sdio_hal_irqhandler(bus_if->bus_priv.sdio->func);
	}

	return 0;
}

#endif // CONFIG_OOB

int sdio_bustx_thread(void *data)
{
	struct aicwf_bus *bus = (struct aicwf_bus *)data;
	struct aic_sdio_dev *sdiodev = bus->bus_priv.sdio;

#ifdef CONFIG_AIC8800_TXRX_THREAD_PRIO
	if (bustx_thread_prio > 0)
		sched_set_fifo_low(current);
#endif

	AICWFDBG(LOGINFO, "%s the policy of current thread is:%d\n", __func__,
		 current->policy);
	AICWFDBG(LOGINFO, "%s the rt_priority of current thread is:%d\n", __func__,
		 current->rt_priority);
	AICWFDBG(LOGINFO, "%s the current pid is:%d\n", __func__, current->pid);

	while (1) {
		if (kthread_should_stop()) {
			AICWFDBG(LOGERROR, "sdio bustx thread stop\n");
			break;
		}

		if (!wait_for_completion_interruptible(&bus->bustx_trgg)) {
			if (sdiodev->bus_if->state == BUS_DOWN_ST)
				continue;

			if (!aicwf_sdio_activity_get(sdiodev->rwnx_hw,
						     sdiodev->rwnx_hw->ws_tx))
				continue;
			if ((int)(atomic_read(&sdiodev->tx_priv->tx_pktcnt) > 0) ||
			    sdiodev->tx_priv->cmd_txstate)
				aicwf_sdio_tx_process(sdiodev);
			aicwf_sdio_activity_put(sdiodev->rwnx_hw,
						sdiodev->rwnx_hw->ws_tx);
		}
	}

	return 0;
}

int sdio_busrx_thread(void *data)
{
	struct aicwf_rx_priv *rx_priv = (struct aicwf_rx_priv *)data;
	struct aicwf_bus *bus_if = rx_priv->sdiodev->bus_if;

#ifdef CONFIG_AIC8800_TXRX_THREAD_PRIO
	if (busrx_thread_prio > 0)
		sched_set_fifo_low(current);
#endif

	AICWFDBG(LOGINFO, "%s the policy of current thread is:%d\n", __func__,
		 current->policy);
	AICWFDBG(LOGINFO, "%s the rt_priority of current thread is:%d\n", __func__,
		 current->rt_priority);
	AICWFDBG(LOGINFO, "%s the current pid is:%d\n", __func__, current->pid);

	while (1) {
		if (kthread_should_stop()) {
			AICWFDBG(LOGERROR, "sdio busrx thread stop\n");
			break;
		}
		if (!wait_for_completion_interruptible(&bus_if->busrx_trgg)) {
			if (bus_if->state == BUS_DOWN_ST)
				continue;
			if (!aicwf_sdio_activity_get(rx_priv->sdiodev->rwnx_hw,
						     rx_priv->sdiodev->rwnx_hw->ws_rx))
				continue;
			aicwf_process_rxframes(rx_priv);
			aicwf_sdio_activity_put(rx_priv->sdiodev->rwnx_hw,
						rx_priv->sdiodev->rwnx_hw->ws_rx);
		}
	}
	return 0;
}

#if defined(CONFIG_SDIO_PWRCTRL)
static int aicwf_sdio_pwrctl_thread(void *data)
{
	struct aic_sdio_dev *sdiodev = (struct aic_sdio_dev *)data;

	while (1) {
		if (kthread_should_stop()) {
			AICWFDBG(LOGINFO, "sdio pwrctl thread stop\n");
			break;
		}
		if (!wait_for_completion_interruptible(&sdiodev->pwrctrl_trgg)) {
			if (sdiodev->bus_if->state == BUS_DOWN_ST)
				continue;

			if (!aicwf_sdio_activity_get(sdiodev->rwnx_hw,
						     sdiodev->rwnx_hw->ws_pwrctrl))
				continue;

			if ((int)(atomic_read(&sdiodev->tx_priv->tx_pktcnt) <= 0) &&
			    !sdiodev->tx_priv->cmd_txstate &&
				atomic_read(&sdiodev->rx_priv->rx_cnt) == 0)
				aicwf_sdio_pwr_stctl(sdiodev, SDIO_SLEEP_ST);
			else
				aicwf_sdio_pwrctl_timer(sdiodev, sdiodev->active_duration);

			aicwf_sdio_activity_put(sdiodev->rwnx_hw,
						sdiodev->rwnx_hw->ws_pwrctrl);
		}
	}

	return 0;
}

static void aicwf_sdio_bus_pwrctl(struct timer_list *t)
{
	//struct aic_sdio_dev *sdiodev = from_timer(sdiodev, t, timer);
	struct aic_sdio_dev *sdiodev = timer_container_of(sdiodev, t, timer);

	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		sdio_err("bus down\n");
		return;
	}

	if (sdiodev->pwrctl_tsk)
		complete(&sdiodev->pwrctrl_trgg);
}
#endif

#ifdef CONFIG_PREALLOC_RX_SKB
static void aicwf_sdio_enq_rxpkt(struct aic_sdio_dev *sdiodev,
				 struct rx_buff *pkt)
#else
static void aicwf_sdio_enq_rxpkt(struct aic_sdio_dev *sdiodev,
				 struct sk_buff *pkt)
#endif
{
	struct aicwf_rx_priv *rx_priv = sdiodev->rx_priv;
	unsigned long flags = 0;

	spin_lock_irqsave(&rx_priv->rxqlock, flags);
#ifdef CONFIG_PREALLOC_RX_SKB
	if (!aicwf_rxbuff_enqueue(sdiodev->dev, &rx_priv->rxq, pkt)) {
		spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
		pr_err("%s %d, enqueue rxq fail\n", __func__, __LINE__);
		aicwf_prealloc_rxbuff_free(pkt, &rx_priv->rxbuff_lock);
		return;
	}
#else
	if (!aicwf_rxframe_enqueue(sdiodev->dev, &rx_priv->rxq, pkt)) {
		spin_unlock_irqrestore(&rx_priv->rxqlock, flags);
		aicwf_dev_skb_free(pkt);
		return;
	}
#endif
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
#ifdef CONFIG_PREALLOC_RX_SKB
	struct rx_buff *pkt = NULL;
#else
	struct sk_buff *pkt = NULL;
#endif
	int ret;

	if (!func) {
		AICWFDBG(LOGERROR, "fdrv %s func is null\n", __func__);
		return;
	}

	bus_if = dev_get_drvdata(&func->dev);

	if (!bus_if->bus_priv.sdio) {
		AICWFDBG(LOGERROR, "fdrv %s sdiodev is null\n", __func__);
		return;
	}

	sdiodev = bus_if->bus_priv.sdio;

	if (!aicwf_sdio_activity_get(sdiodev->rwnx_hw,
					     sdiodev->rwnx_hw->ws_irqrx))
		return;

	if (!bus_if || bus_if->state == BUS_DOWN_ST) {
		sdio_err("bus err\n");
		aicwf_sdio_activity_put(sdiodev->rwnx_hw,
						sdiodev->rwnx_hw->ws_irqrx);
		return;
	}

#ifdef CONFIG_PREALLOC_RX_SKB
	if (list_empty(&aic_rx_buff_list.rxbuff_list)) {
		pr_warn("%s %d, rxbuff list is empty\n", __func__, __LINE__);
		aicwf_sdio_activity_put(sdiodev->rwnx_hw,
						sdiodev->rwnx_hw->ws_irqrx);
		return;
	}
#endif

	if (aic_sdio_hw->use_func2) {
		ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.block_cnt_reg,
				       &intstatus);
		while (ret || (intstatus & SDIO_OTHER_INTERRUPT)) {
			sdio_err("ret=%d, intstatus=%x\r\n", ret, intstatus);
			ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.block_cnt_reg,
					       &intstatus);
		}
		sdiodev->rx_priv->data_len = intstatus * SDIOWIFI_FUNC_BLOCKSIZE;

		if (intstatus > 0) {
			if (intstatus < 64) {
				pkt = aicwf_sdio_readframes(sdiodev);
			} else {
				// byte_len must<= 128
				aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len);
				sdio_info("byte mode len=%d\r\n", byte_len);
				pkt = aicwf_sdio_readframes(sdiodev);
			}
		} else {
			// sdio_err("Interrupt but no data\n"); //debug trace
		}

		if (pkt)
			aicwf_sdio_enq_rxpkt(sdiodev, pkt);

		if (atomic_read(&sdiodev->rx_priv->rx_cnt) == 1)
			complete(&bus_if->busrx_trgg);

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
					// aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len,
					// 1);//byte_len must<= 128
					// byte_len must<= 128
					aicwf_sdio_intr_get_len_bytemode(sdiodev,
									 &byte_len);
					sdio_info("byte mode len=%d\r\n", byte_len);
					// pkt = aicwf_sdio_readframes(sdiodev, 1);
					pkt = aicwf_sdio_readframes(sdiodev);
				} else { // block mode
					sdiodev->rx_priv->data_len =
						(intstatus & 0x7U) * SDIOWIFI_FUNC_BLOCKSIZE;
					// pkt = aicwf_sdio_readframes(sdiodev, 1);
					pkt = aicwf_sdio_readframes(sdiodev);
				}
			} else {                     // func1
				if (intstatus == 120U) { // byte mode
					// aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len,
					// 0);//byte_len must<= 128
					// byte_len must<= 128
					aicwf_sdio_intr_get_len_bytemode(sdiodev, &byte_len);
					sdio_info("byte mode len=%d\r\n", byte_len);
					// pkt = aicwf_sdio_readframes(sdiodev, 0);
					pkt = aicwf_sdio_readframes(sdiodev);
				} else { // block mode
					sdiodev->rx_priv->data_len =
						(intstatus & 0x7FU) * SDIOWIFI_FUNC_BLOCKSIZE;
					// pkt = aicwf_sdio_readframes(sdiodev, 0);
					pkt = aicwf_sdio_readframes(sdiodev);
				}
			}
		} else {
			// sdio_err("Interrupt but no data\n");
		}

		if (pkt)
			aicwf_sdio_enq_rxpkt(sdiodev, pkt);

		if (atomic_read(&sdiodev->rx_priv->rx_cnt) == 1)
			complete(&bus_if->busrx_trgg);
	}

	aicwf_sdio_activity_put(sdiodev->rwnx_hw,
					sdiodev->rwnx_hw->ws_irqrx);
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

/* Forward declarations for bus_ops callbacks */
static int aicwf_sdio_bus_read_reg(struct device *dev, u32 regaddr, u8 *val);
static int aicwf_sdio_bus_write_reg(struct device *dev, u32 regaddr, u8 val);
static int aicwf_sdio_bus_send_pkt(struct device *dev, u8 *buf, uint count);
static int aicwf_sdio_bus_recv_pkt(struct device *dev, u8 *buf, u32 size);
static int aicwf_sdio_bus_enable_irq(struct device *dev);
static void aicwf_sdio_bus_disable_irq(struct device *dev);
static bool aicwf_sdio_bus_flow_ctrl(struct device *dev);
static int aicwf_sdio_bus_sleep_allow(struct device *dev);
static int aicwf_sdio_bus_wakeup(struct device *dev);
static const void *aicwf_sdio_bus_get_hw_props(struct device *dev);

static struct aicwf_bus_ops aicwf_sdio_bus_ops = {
	.start          = aicwf_sdio_bus_start,
	.stop           = aicwf_sdio_bus_stop,
	.txdata         = aicwf_sdio_bus_txdata,
	.txmsg          = aicwf_sdio_bus_txmsg,
	.read_reg       = aicwf_sdio_bus_read_reg,
	.write_reg      = aicwf_sdio_bus_write_reg,
	.send_pkt       = aicwf_sdio_bus_send_pkt,
	.recv_pkt       = aicwf_sdio_bus_recv_pkt,
	.enable_irq     = aicwf_sdio_bus_enable_irq,
	.disable_irq    = aicwf_sdio_bus_disable_irq,
	.flow_ctrl      = aicwf_sdio_bus_flow_ctrl,
	.sleep_allow    = aicwf_sdio_bus_sleep_allow,
	.wakeup         = aicwf_sdio_bus_wakeup,
	.get_hw_props   = aicwf_sdio_bus_get_hw_props,
};

void aicwf_sdio_release(struct aic_sdio_dev *sdiodev)
{
	struct aicwf_bus *bus_if;
	int ret;

	bus_if = dev_get_drvdata(sdiodev->dev);
	bus_if->state = BUS_DOWN_ST;
#ifdef CONFIG_OOB
	if (sdiodev->oob_enable) {
		if (!sdiodev->func) {
			pr_warn("%s, NULL sdio func\n", __func__);
			return;
		}
		sdio_claim_host(sdiodev->func);
		// disable sdio interrupt
		ret =
			aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x0);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "reg:%d write failed!\n",
				 sdiodev->sdio_reg.intr_config_reg);
		}
		sdio_release_irq(sdiodev->func);
		sdio_release_host(sdiodev->func);
	}
#else
	sdio_claim_host(sdiodev->func);
	// disable sdio interrupt
	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x0);
	if (ret < 0)
		sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
	sdio_release_irq(sdiodev->func);
	sdio_release_host(sdiodev->func);
#endif
	if (sdiodev->dev)
		aicwf_bus_deinit(sdiodev->dev);

	if (sdiodev->tx_priv)
		aicwf_tx_deinit(sdiodev->tx_priv);

	if (sdiodev->rx_priv)
		aicwf_rx_deinit(sdiodev->rx_priv);

#if defined(CONFIG_SDIO_PWRCTRL)
	if (sdiodev->pwrctl_tsk) {
		complete_all(&sdiodev->pwrctrl_trgg);
		kthread_stop(sdiodev->pwrctl_tsk);
		sdiodev->pwrctl_tsk = NULL;
	}

	AICWFDBG(LOGINFO, "%s:pwrctl stopped\n", __func__);
#endif

	if (sdiodev->cmd_mgr.state == RWNX_CMD_MGR_STATE_INITED)
		rwnx_cmd_mgr_deinit(&sdiodev->cmd_mgr);
}

void aicwf_sdio_reg_init(struct aic_sdio_dev *sdiodev)
{
	if (aic_sdio_hw->use_func2) {
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
	//u8 val = 0;

	aicbsp_get_feature(&feature);
	aicwf_sdio_reg_init(sdiodev);
	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	host = sdiodev->func->card->host;

	sdio_claim_host(sdiodev->func);
	ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "set blocksize fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	ret = sdio_enable_func(sdiodev->func);
	if (ret < 0) {
		sdio_release_host(sdiodev->func);
		AICWFDBG(LOGERROR, "enable func fail %d.\n", ret);
		return ret;
	}

	if (feature.sdio_clock > 0) {
		host->ios.clock = feature.sdio_clock;
		host->ops->set_ios(host, &host->ios);
		AICWFDBG(LOGINFO, "Set SDIO Clock %d MHz\n", host->ios.clock / 1000000);
	}

	sdio_release_host(sdiodev->func);

	if (aic_sdio_hw->use_func2) {
		sdio_claim_host(sdiodev->func2);
		//set sdio blocksize
		ret = sdio_set_block_size(sdiodev->func2, SDIOWIFI_FUNC_BLOCKSIZE);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "set func2 blocksize fail %d\n", ret);
			sdio_release_host(sdiodev->func2);
			return ret;
		}

		//set sdio enable func
		ret = sdio_enable_func(sdiodev->func2);
		if (ret < 0) {
			sdio_release_host(sdiodev->func2);
			AICWFDBG(LOGERROR, "enable func2 fail %d.\n", ret);
			return ret;
		}

		sdio_release_host(sdiodev->func2);

		ret = aicwf_sdio_func2_writeb(sdiodev,
					      sdiodev->sdio_reg.register_block, block_bit0);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "reg:%d write failed!\n",
				 sdiodev->sdio_reg.register_block);
			return ret;
		}

		//1: no byte mode
		ret = aicwf_sdio_func2_writeb(sdiodev, sdiodev->sdio_reg.bytemode_enable_reg,
					      byte_mode_disable);
		if (ret < 0) {
			AICWFDBG(LOGERROR, "reg:%d write failed!\n",
				 sdiodev->sdio_reg.bytemode_enable_reg);
			return ret;
		}
	}

	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.register_block, block_bit0);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "reg:%d write failed!\n", sdiodev->sdio_reg.register_block);
		return ret;
	}

	//1: no byte mode
	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.bytemode_enable_reg, byte_mode_disable);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "reg:%d write failed!\n", sdiodev->sdio_reg.bytemode_enable_reg);
		return ret;
	}

	mdelay(10);
	return ret;
}

int aicwf_sdiov3_func_init(struct aic_sdio_dev *sdiodev)
{
	struct mmc_host *host;
	u8 byte_mode_disable = 0x1; // 1: no byte mode
	int ret = 0;
	struct aicbsp_feature_t feature;
	u8 val1 = 0;
	int val;

	ret = aicbsp_get_feature(&feature);
	if (ret < 0)
		return ret;
	aicwf_sdio_reg_init(sdiodev);

	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return 0;
	}

	host = sdiodev->func->card->host;

	sdio_claim_host(sdiodev->func);
	sdiodev->func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

	ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "set blocksize fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
	ret = sdio_enable_func(sdiodev->func);
	if (ret < 0) {
		sdio_release_host(sdiodev->func);
		AICWFDBG(LOGERROR, "enable func fail %d.\n", ret);
		return ret;
	}

	sdio_f0_writeb(sdiodev->func, 0x7F, 0xF2, &ret);
	if (ret) {
		sdio_err("set fn0 0xF2 fail %d\n", ret);
		sdio_release_host(sdiodev->func);
		return ret;
	}
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
	sdio_f0_writeb(sdiodev->func, 0x0, 0xF8, &ret);
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
	usleep_range(1000, 1500);
	sdio_release_host(sdiodev->func);

	// 1: no byte mode
	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.bytemode_enable_reg,
				byte_mode_disable);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "reg:%d write failed!\n",
			 sdiodev->sdio_reg.bytemode_enable_reg);
		return ret;
	}

	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 0x11);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "reg:%d write failed!\n",
			 sdiodev->sdio_reg.wakeup_reg);
		return ret;
	}

	mdelay(5);
	ret = aicwf_sdio_readb(sdiodev, sdiodev->sdio_reg.sleep_reg, &val1);
	if (ret < 0) {
		AICWFDBG(LOGERROR, "reg:%d read failed!\n",
			 sdiodev->sdio_reg.sleep_reg);
		return ret;
	}

	if (!(val1 & 0x10))
		AICWFDBG(LOGERROR, "wakeup fail\n");
	else
		AICWFDBG(LOGINFO, "sdio ready\n");

	return ret;
}

void aicwf_sdio_func_deinit(struct aic_sdio_dev *sdiodev)
{
	if (!sdiodev->func) {
		pr_warn("%s, NULL sdio func\n", __func__);
		return;
	}

	sdio_claim_host(sdiodev->func);
	sdio_disable_func(sdiodev->func);
	sdio_release_host(sdiodev->func);

	if (aic_sdio_hw->use_func2) {
		sdio_claim_host(sdiodev->func2);
		sdio_disable_func(sdiodev->func2);
		sdio_release_host(sdiodev->func2);
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
#ifdef AICWF_SDIO_SUPPORT
	sdiodev->pwrctl_tsk =
		kthread_run(aicwf_sdio_pwrctl_thread, sdiodev, "aicwf_pwrctl");
#endif
	if (IS_ERR(sdiodev->pwrctl_tsk))
		sdiodev->pwrctl_tsk = NULL;
#endif
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	sdiodev->flowctrl = 0;
	spin_lock_init(&sdiodev->tx_flow_lock);
#endif

	spin_lock_init(&sdiodev->tx_tp_lock);
	atomic_set(&sdiodev->suspending, 0);
	atomic_set(&sdiodev->activity_count, 0);
	init_waitqueue_head(&sdiodev->activity_waitq);
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

u8 crc8_ponl_107(u8 *p_buffer, uint16_t cal_size)
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
 *  Bus abstraction layer - SDIO bus_ops callbacks
 *
 *  These thin wrappers translate the generic bus_ops interface
 *  (which receives a struct device *) back to the SDIO-specific
 *  aic_sdio_dev.  Upper layers never see the SDIO internals.
 * ================================================================
 */

static inline struct aic_sdio_dev *aicwf_bus_to_sdiodev(struct device *dev)
{
	struct aicwf_bus *bus_if = dev_get_drvdata(dev);

	return bus_if->bus_priv.sdio;
}

static int aicwf_sdio_bus_read_reg(struct device *dev, u32 regaddr, u8 *val)
{
	return aicwf_sdio_readb(aicwf_bus_to_sdiodev(dev), regaddr, val);
}

static int aicwf_sdio_bus_write_reg(struct device *dev, u32 regaddr, u8 val)
{
	return aicwf_sdio_writeb(aicwf_bus_to_sdiodev(dev), regaddr, val);
}

static int aicwf_sdio_bus_send_pkt(struct device *dev, u8 *buf, uint count)
{
	return aicwf_sdio_send_pkt(aicwf_bus_to_sdiodev(dev), buf, count);
}

static int aicwf_sdio_bus_recv_pkt(struct device *dev, u8 *buf, u32 size)
{
	struct aic_sdio_dev *sdiodev = aicwf_bus_to_sdiodev(dev);
	int ret;

	sdio_claim_host(sdiodev->func);
	ret = sdio_readsb(sdiodev->func, buf,
			  sdiodev->sdio_reg.rd_fifo_addr, size);
	sdio_release_host(sdiodev->func);

	return ret;
}

static int aicwf_sdio_bus_enable_irq(struct device *dev)
{
	struct aic_sdio_dev *sdiodev = aicwf_bus_to_sdiodev(dev);
	int ret;

	sdio_claim_host(sdiodev->func);
#ifndef CONFIG_FDRV_NO_REG_SDIO
	sdio_claim_irq(sdiodev->func, aicwf_sdio_hal_irqhandler);
#else
	set_irq_handler(aicwf_sdio_hal_irqhandler);
#endif
	if (aic_sdio_hw->need_func0_intr) {
		sdio_f0_writeb(sdiodev->func, 0x07, 0x04, &ret);
		if (ret)
			sdio_err("set func0 int en fail %d\n", ret);
	}
	sdio_release_host(sdiodev->func);

	ret = aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x07);
	return ret;
}

static void aicwf_sdio_bus_disable_irq(struct device *dev)
{
	struct aic_sdio_dev *sdiodev = aicwf_bus_to_sdiodev(dev);

	sdio_claim_host(sdiodev->func);
	aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x0);
	sdio_release_irq(sdiodev->func);
	sdio_release_host(sdiodev->func);
}

static bool aicwf_sdio_bus_flow_ctrl(struct device *dev)
{
	int ret;

	ret = aicwf_sdio_flow_ctrl(aicwf_bus_to_sdiodev(dev));
	return (ret > 0);
}

#if defined(CONFIG_SDIO_PWRCTRL)
static int aicwf_sdio_bus_sleep_allow(struct device *dev)
{
	return aicwf_sdio_sleep_allow(aicwf_bus_to_sdiodev(dev));
}

static int aicwf_sdio_bus_wakeup(struct device *dev)
{
	return aicwf_sdio_wakeup(aicwf_bus_to_sdiodev(dev));
}
#else
static int aicwf_sdio_bus_sleep_allow(struct device *dev)
{
	return 0;
}

static int aicwf_sdio_bus_wakeup(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_SDIO_PWRCTRL */

static const void *aicwf_sdio_bus_get_hw_props(struct device *dev)
{
	return aic_sdio_hw;
}
