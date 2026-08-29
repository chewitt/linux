/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_bus.h
 * @brief Common bus abstraction layer for AIC8800 series.
 *
 * This header defines the bus-level operations that abstract the
 * underlying transport (SDIO, USB, etc.) from the upper driver layers.
 * The design follows the pattern used by silabs/wfx and broadcom/b43.
 *
 * Key principles:
 *  - struct aicwf_bus_ops provides a complete set of low-level bus
 *    primitives (read/write registers, send/recv packets, flow control,
 *    power management, interrupt handling).
 *  - Upper-layer code accesses the bus only through the ops table and
 *    inline wrappers defined here.  It MUST NOT dereference the
 *    bus-private structure (aic_sdio_dev etc.) directly.
 *  - When a new transport (e.g. USB) is added, only the bus_ops
 *    implementation needs to change; upper layers are unaffected.
 */

#ifndef _AIC_BUS_H_
#define _AIC_BUS_H_

#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/types.h>

/* Bus operational state */
enum aicwf_bus_state { BUS_DOWN_ST, BUS_UP_ST };

struct aicwf_bus {
	union {
		struct aic_sdio_dev *sdio;
		struct aic_usb_dev *usb;
	} bus_priv;
	struct device *dev;
	struct aicwf_bus_ops *ops;
	enum aicwf_bus_state state;
	u8 *cmd_buf;
	struct completion bustx_trgg;
	struct completion busrx_trgg;
	struct completion busirq_trgg; // new oob feature
	struct task_struct *bustx_thread;
	struct task_struct *busrx_thread;
	struct task_struct *busirq_thread; // new oob feature
};

/* ================================================================
 *  Bus-level operation vectors
 *
 *  Every transport (SDIO, USB, ...) provides one instance of this
 *  struct.  The bus-private data pointer is carried inside
 *  struct aicwf_bus so that ops callbacks can retrieve it via
 *  bus->bus_priv.
 * ================================================================
 */

struct aicwf_bus_ops {
	/* ----- Lifecycle ----- */
	int  (*start)(struct device *dev);
	void (*stop)(struct device *dev);

	/* ----- Data / message transmission ----- */
	int (*txdata)(struct device *dev, struct sk_buff *skb);
	int (*txmsg)(struct device *dev, u8 *msg, uint len);

	/* ----- Low-level register access -----
	 * These replace the direct aicwf_sdio_readb/writeb calls.
	 */
	int (*read_reg)(struct device *dev, u32 regaddr, u8 *val);
	int (*write_reg)(struct device *dev, u32 regaddr, u8 val);

	/* ----- Bulk data transfer -----
	 * send_pkt  – write a buffer to the chip's TX FIFO
	 * recv_pkt  – read  a buffer from the chip's RX FIFO
	 */
	int (*send_pkt)(struct device *dev, u8 *buf, uint count);
	int (*recv_pkt)(struct device *dev, u8 *buf, u32 size);

	/* ----- Interrupt handling ----- */
	int  (*enable_irq)(struct device *dev);
	void (*disable_irq)(struct device *dev);

	/* ----- Flow control -----
	 * Returns true if the device is ready to accept more data.
	 */
	bool (*flow_ctrl)(struct device *dev);

	/* ----- Power management ----- */
	int (*sleep_allow)(struct device *dev);
	int (*wakeup)(struct device *dev);

	/* ----- Chip-specific hardware info -----
	 * Returns a pointer to chip hardware properties (SDIO register
	 * layout, feature flags, etc.).  The caller treats this as
	 * opaque; only the bus layer interprets it.
	 */
	const void *(*get_hw_props)(struct device *dev);
};

/* ================================================================
 *  Inline wrappers – upper layers call these instead of touching
 *  bus_priv internals.
 * ================================================================
 */

static inline int aicwf_bus_start(struct aicwf_bus *bus)
{
	return bus->ops->start(bus->dev);
}

static inline void aicwf_bus_stop(struct aicwf_bus *bus)
{
	bus->ops->stop(bus->dev);
}

static inline int aicwf_bus_txdata(struct aicwf_bus *bus, struct sk_buff *skb)
{
	return bus->ops->txdata(bus->dev, skb);
}

static inline int aicwf_bus_txmsg(struct aicwf_bus *bus, u8 *msg, uint len)
{
	return bus->ops->txmsg(bus->dev, msg, len);
}

static inline int aicwf_bus_read_reg(struct aicwf_bus *bus,
				     u32 regaddr, u8 *val)
{
	return bus->ops->read_reg(bus->dev, regaddr, val);
}

static inline int aicwf_bus_write_reg(struct aicwf_bus *bus,
				      u32 regaddr, u8 val)
{
	return bus->ops->write_reg(bus->dev, regaddr, val);
}

static inline int aicwf_bus_send_pkt(struct aicwf_bus *bus,
				     u8 *buf, uint count)
{
	return bus->ops->send_pkt(bus->dev, buf, count);
}

static inline int aicwf_bus_recv_pkt(struct aicwf_bus *bus,
				     u8 *buf, u32 size)
{
	return bus->ops->recv_pkt(bus->dev, buf, size);
}

static inline int aicwf_bus_enable_irq(struct aicwf_bus *bus)
{
	return bus->ops->enable_irq(bus->dev);
}

static inline void aicwf_bus_disable_irq(struct aicwf_bus *bus)
{
	bus->ops->disable_irq(bus->dev);
}

static inline bool aicwf_bus_flow_ctrl(struct aicwf_bus *bus)
{
	return bus->ops->flow_ctrl(bus->dev);
}

static inline int aicwf_bus_sleep_allow(struct aicwf_bus *bus)
{
	return bus->ops->sleep_allow(bus->dev);
}

static inline int aicwf_bus_wakeup(struct aicwf_bus *bus)
{
	return bus->ops->wakeup(bus->dev);
}

static inline const void *aicwf_bus_get_hw_props(struct aicwf_bus *bus)
{
	return bus->ops->get_hw_props(bus->dev);
}

/* ================================================================
 *  Bus-level init / deinit (shared across transports)
 * ================================================================
 */

int aicwf_bus_init(uint bus_hdrlen, struct device *dev);
void aicwf_bus_deinit(struct device *dev);

#endif /* _AIC_BUS_H_ */
