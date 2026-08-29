/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_IRQS_H_
#define _RWNX_IRQS_H_

#include <linux/interrupt.h>

/* IRQ handler to be registered by platform driver */
irqreturn_t rwnx_irq_hdlr(int irq, void *dev_id);

void rwnx_task(unsigned long data);

#endif /* _RWNX_IRQS_H_ */
