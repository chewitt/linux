/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_SDMMC_H_
#define _AICWF_SDMMC_H_

#ifdef AICWF_SDIO_SUPPORT
#include "aicwf_rx_prealloc.h"
#include "rwnx_cmds.h"
#include <linux/ieee80211.h>
#include <linux/if_ether.h>
#include <linux/semaphore.h>
#include <linux/skbuff.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/wait.h>

#define AICWF_SDIO_NAME         "aicwf_sdio"
#define SDIOWIFI_FUNC_BLOCKSIZE 512

#define SDIOWIFI_BYTEMODE_LEN_REG    0x02
#define SDIOWIFI_INTR_CONFIG_REG     0x04
#define SDIOWIFI_SLEEP_REG           0x05
#define SDIOWIFI_WAKEUP_REG          0x09
#define SDIOWIFI_FLOW_CTRL_REG       0x0A
#define SDIOWIFI_REGISTER_BLOCK      0x0B
#define SDIOWIFI_BYTEMODE_ENABLE_REG 0x11
#define SDIOWIFI_BLOCK_CNT_REG       0x12
#define SDIOWIFI_FLOWCTRL_MASK_REG   0x7F
#define SDIOWIFI_WR_FIFO_ADDR        0x07
#define SDIOWIFI_RD_FIFO_ADDR        0x08

#define SDIOWIFI_INTR_ENABLE_REG_V3      0x00
#define SDIOWIFI_INTR_PENDING_REG_V3     0x01
#define SDIOWIFI_INTR_TO_DEVICE_REG_V3   0x02
#define SDIOWIFI_FLOW_CTRL_Q1_REG_V3     0x03
#define SDIOWIFI_MISC_INT_STATUS_REG_V3  0x04
#define SDIOWIFI_BYTEMODE_LEN_REG_V3     0x05
#define SDIOWIFI_BYTEMODE_LEN_MSB_REG_V3 0x06
#define SDIOWIFI_BYTEMODE_ENABLE_REG_V3  0x07
#define SDIOWIFI_MISC_CTRL_REG_V3        0x08
#define SDIOWIFI_FLOW_CTRL_Q2_REG_V3     0x09
#define SDIOWIFI_CLK_TEST_RESULT_REG_V3  0x0A
#define SDIOWIFI_RD_FIFO_ADDR_V3         0x0F
#define SDIOWIFI_WR_FIFO_ADDR_V3         0x10

#define SDIOCLK_FREE_RUNNING_BIT BIT(6)

#define SDIOWIFI_PWR_CTRL_INTERVAL 30
#define FLOW_CTRL_RETRY_COUNT      50
#define BUFFER_SIZE                1536
#define TAIL_LEN                   4
#define TXQLEN                     (2048 * 4)

#define SDIO_SLEEP_ST  0
#define SDIO_ACTIVE_ST 1

#define DATA_FLOW_CTRL_THRESH 2
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
#define AICWF_SDIO_TX_LOW_WATER  100
#define AICWF_SDIO_TX_HIGH_WATER 500
#endif

#ifdef CONFIG_AIC8800_TEMP_CONTROL
#define TEMP_GET_INTERVAL (10 * 1000)
#define TEMP_THD_1        92  // temperature 1 (celsius)
#define TEMP_THD_2        103 // temperature 2 (celsius)
#define BUFFERING_V1      8
#define BUFFERING_V2      8
#define TMR_INTERVAL_1    60  // timer_1 60ms
#define TMR_INTERVAL_2    180 // timer_2 130ms
#endif

enum sdio_type {
	SDIO_TYPE_DATA = 0X00,
	SDIO_TYPE_CFG = 0X10,
	SDIO_TYPE_CFG_CMD_RSP = 0X11,
	SDIO_TYPE_CFG_DATA_CFM = 0X12,
	SDIO_TYPE_CFG_PRINT = 0X13
};

/* SDIO Device ID */
#define SDIO_VENDOR_ID_AIC8801    0x5449
#define SDIO_VENDOR_ID_AIC8800DC  0xc8a1
#define SDIO_VENDOR_ID_AIC8800D80 0xc8a1

#define SDIO_DEVICE_ID_AIC8801    0x0145
#define SDIO_DEVICE_ID_AIC8800DC  0xc08d
#define SDIO_DEVICE_ID_AIC8800D80 0x0082

enum AICWF_IC {
	PRODUCT_ID_AIC8801 = 0,
	PRODUCT_ID_AIC8800DC,
	PRODUCT_ID_AIC8800DW,
	PRODUCT_ID_AIC8800D80
};

struct rwnx_hw;
struct wakeup_source;

#ifdef CONFIG_AIC8800_TEMP_CONTROL
struct temp_ctrl {
	struct timer_list netif_timer;
	struct timer_list tp_ctrl_timer;
	struct work_struct tp_ctrl_work;
	struct work_struct netif_work;
	/* lock for temp */
	spinlock_t tm_lock;
	s8 cur_temp;
	bool net_stop;
	bool on_off;      // for command, 0 - off, 1 - on
	s8 get_level; // for command, 0 - 100%, 1 - 12%, 2 - 3%
	s8 set_level; // for command, 0 - driver auto, 1 - 12%, 2 - 3%
	int interval_t1;
	int interval_t2;
	u8 cur_stat; // 0--normal temp, 1/2--buffering temp
	s8 tp_thd_1; // temperature threshold 1
	s8 tp_thd_2; // temperature threshold 2
	s8 tm_start; //timer start flag
};
#endif

struct aic_sdio_reg {
	u8 bytemode_len_reg;
	u8 intr_config_reg;
	u8 sleep_reg;
	u8 wakeup_reg;
	u8 flow_ctrl_reg;
	u8 flowctrl_mask_reg;
	u8 register_block;
	u8 bytemode_enable_reg;
	u8 block_cnt_reg;
	u8 misc_int_status_reg;
	u8 rd_fifo_addr;
	u8 wr_fifo_addr;
};

struct aic_sdio_dev {
	struct rwnx_hw *rwnx_hw;
	struct sdio_func *func;
	struct sdio_func *func2;
	struct device *dev;
	struct aicwf_bus *bus_if;
	struct rwnx_cmd_mgr cmd_mgr;

	struct aicwf_rx_priv *rx_priv;
	struct aicwf_tx_priv *tx_priv;
	u32 state;
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
	u8 flowctrl;
	/* lock for tx flow */
	spinlock_t tx_flow_lock;
#endif

#if defined(CONFIG_SDIO_PWRCTRL)
	// for sdio pwr ctrl
	struct timer_list timer;
	uint active_duration;
	struct completion pwrctrl_trgg;
	struct task_struct *pwrctl_tsk;
	/* lock for pwrctl */
	spinlock_t pwrctl_lock;
	struct semaphore pwrctl_wakeup_sema;
#endif
	u16 chipid;
	struct aic_sdio_reg sdio_reg;

	/* lock for ws */
	spinlock_t wslock;
	bool oob_enable;
	atomic_t is_bus_suspend;
	atomic_t suspending;
	atomic_t activity_count;
	wait_queue_head_t activity_waitq;
	/* lock for tx tp */
	spinlock_t tx_tp_lock;

#ifdef CONFIG_AIC8800_TEMP_CONTROL
	struct temp_ctrl tp_ctrl;
#endif

#ifdef CONFIG_PLATFORM_EXTERNAL
	int wf_wake_host;
	bool wake_by_wifi;
	int host_wake_wf;
#endif
};

extern struct aicwf_rx_buff_list aic_rx_buff_list;
int aicwf_sdio_readb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 *val);
int aicwf_sdio_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val);
void aicwf_sdio_hal_irqhandler(struct sdio_func *func);
bool aicwf_sdio_activity_get(struct rwnx_hw *rwnx_hw,
			     struct wakeup_source *ws);
void aicwf_sdio_activity_put(struct rwnx_hw *rwnx_hw,
			    struct wakeup_source *ws);
bool aicwf_sdio_scan_activity_get(struct rwnx_hw *rwnx_hw);
void aicwf_sdio_scan_activity_put(struct rwnx_hw *rwnx_hw);
int aicwf_sdio_func2_readb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 *val);
int aicwf_sdio_func2_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val);

#ifdef CONFIG_PREALLOC_RX_SKB
struct rx_buff *aicwf_sdio_readframes(struct aic_sdio_dev *sdiodev);
#else
struct sk_buff *aicwf_sdio_readframes(struct aic_sdio_dev *sdiodev);
#endif

#ifdef CONFIG_AIC8800_TEMP_CONTROL
void aicwf_netif_worker(struct work_struct *work);
void aicwf_temp_ctrl_worker(struct work_struct *work);
void aicwf_temp_ctrl(struct aic_sdio_dev *sdiodev);
void aicwf_netif_ctrl(struct aic_sdio_dev *sdiodev, int val);
void aicwf_tp_ctrl_init(struct aic_sdio_dev *sdiodev);
void aicwf_tp_ctrl_deinit(struct aic_sdio_dev *sdiodev);
#endif

#if defined(CONFIG_SDIO_PWRCTRL)
void aicwf_sdio_pwrctl_timer(struct aic_sdio_dev *sdiodev, uint duration);
int aicwf_sdio_pwr_stctl(struct aic_sdio_dev *sdiodev, uint target);
int aicwf_sdio_sleep_allow(struct aic_sdio_dev *sdiodev);
int aicwf_sdio_wakeup(struct aic_sdio_dev *sdiodev);
#endif

void aicwf_sdio_probe_(struct sdio_func *func, const struct sdio_device_id *id);
void aicwf_sdio_remove_(struct sdio_func *func);

void aicwf_sdio_reg_init(struct aic_sdio_dev *sdiodev);
int aicwf_sdio_func_init(struct aic_sdio_dev *sdiodev);
int aicwf_sdiov3_func_init(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_func_deinit(struct aic_sdio_dev *sdiodev);
#ifdef CONFIG_AIC8800_TX_NETIF_FLOWCTRL
void aicwf_sdio_tx_netif_flowctrl(struct rwnx_hw *rwnx_hw, bool state);
#endif
int aicwf_sdio_flow_ctrl(struct aic_sdio_dev *sdiodev);
int aicwf_sdio_flow_ctrl_msg(struct aic_sdio_dev *sdiodev);
#ifdef CONFIG_PREALLOC_RX_SKB
int aicwf_sdio_recv_pkt(struct aic_sdio_dev *sdiodev, struct rx_buff *rxbbuf,
			u32 size);
#else
int aicwf_sdio_recv_pkt(struct aic_sdio_dev *sdiodev, struct sk_buff *skbbuf,
			u32 size);
#endif
int aicwf_sdio_send_pkt(struct aic_sdio_dev *sdiodev, u8 *buf, uint count);
void *aicwf_sdio_bus_init(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_release(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_exit(void);
void aicwf_sdio_register(void);
int aicwf_sdio_txpkt(struct aic_sdio_dev *sdiodev, struct sk_buff *pkt);
int sdio_bustx_thread(void *data);
int sdio_busrx_thread(void *data);
#ifdef CONFIG_OOB
// new oob feature
int sdio_busirq_thread(void *data);
#endif // CONFIG_OOB
int aicwf_sdio_aggr(struct aicwf_tx_priv *tx_priv, struct sk_buff *pkt);
int aicwf_sdio_send(struct aicwf_tx_priv *tx_priv, u8 txnow);
void aicwf_sdio_aggr_send(struct aicwf_tx_priv *tx_priv);
void aicwf_sdio_aggrbuf_reset(struct aicwf_tx_priv *tx_priv);
void aicwf_hostif_ready(void);
void aicwf_hostif_fail(void);

#ifdef CONFIG_PLATFORM_EXTERNAL
int regist_wakeup_irq(struct aic_sdio_dev *sdiodev);
void unregist_wakeup_irq(struct aic_sdio_dev *sdiodev);
void disable_wakeup_irq(struct aic_sdio_dev *sdiodev);
void enable_wakeup_irq(struct aic_sdio_dev *sdiodev);
irqreturn_t wakeup_irq_handler(int irq, void *priv);
#endif

u8 crc8_ponl_107(u8 *p_buffer, uint16_t cal_size);

#endif /* AICWF_SDIO_SUPPORT */

#endif /*_AICWF_SDMMC_H_*/
