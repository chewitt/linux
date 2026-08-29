// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief IRQ handler
 *
 ******************************************************************************
 */
#include <linux/interrupt.h>

#include "rwnx_irqs.h"
#include "ipc_host.h"
#include "rwnx_defs.h"
#include "rwnx_prof.h"

/**
 * rwnx_irq_hdlr - IRQ handler
 * @irq: Interrupt number
 * @dev_id: Driver private data registered for the interrupt
 *
 * Handler registered by the platform driver
 *
 * Return: %IRQ_HANDLED after scheduling the interrupt bottom half.
 */
irqreturn_t rwnx_irq_hdlr(int irq, void *dev_id)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)dev_id;

	disable_irq_nosync(irq);
	tasklet_schedule(&rwnx_hw->task);
	return IRQ_HANDLED;
}

/**
 * rwnx_task - Bottom half for IRQ handler
 * @data: Driver private data passed to the tasklet
 *
 * Read irq status and process accordingly
 */
void rwnx_task(unsigned long data)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)data;

	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_hwq_process_all(rwnx_hw);
	spin_unlock_bh(&rwnx_hw->tx_lock);
}
