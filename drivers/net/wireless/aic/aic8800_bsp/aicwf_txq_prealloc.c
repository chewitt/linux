// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bsp txq prealloc mem
 *
 ******************************************************************************
 */
#include "aic_bsp_driver.h"
#include "aicsdio_txrxif.h"
#include "aicwf_txq_prealloc.h"
#include <linux/slab.h>

struct prealloc_txq {
	int prealloced;
	void *txq;
	size_t size;
};

static struct prealloc_txq prealloc_txq;
#define MAX_TXQ_SIZE (100 * 1024)

/**
 * aicwf_prealloc_txq_alloc - Allocate or reuse the preallocated TX queue
 * @size: Required queue size in bytes
 *
 * Return: Pointer to a zeroed queue buffer, or %NULL if allocation failed.
 */
void *aicwf_prealloc_txq_alloc(size_t size)
{
	WARN_ON_ONCE(size > MAX_TXQ_SIZE);

	// check prealloc_txq.size
	if ((int)prealloc_txq.size != (int)size) {
		AICWFDBG(LOGINFO, "%s size is diff will to be kzalloc \r\n", __func__);

		if (prealloc_txq.txq) {
			AICWFDBG(LOGINFO, "%s txq to kfree \r\n", __func__);
			kfree(prealloc_txq.txq);
			prealloc_txq.txq = NULL;
		}

		prealloc_txq.size = size;
		prealloc_txq.prealloced = 0;
	}
	// check prealloc or not
	if (!prealloc_txq.prealloced) {
		prealloc_txq.txq = kzalloc(size, GFP_KERNEL);
		if (!prealloc_txq.txq) {
			AICWFDBG(LOGERROR, "%s txq kzalloc fail \r\n", __func__);
		} else {
			AICWFDBG(LOGINFO, "%s txq kzalloc successful \r\n", __func__);
			prealloc_txq.prealloced = 1;
		}
	} else {
		AICWFDBG(LOGINFO, "%s txq not need to kzalloc \r\n", __func__);
	}

	return prealloc_txq.txq;
}

void aicwf_prealloc_txq_free(void)
{
	if (prealloc_txq.txq) {
		AICWFDBG(LOGINFO, "%s txq to kfree \r\n", __func__);
		kfree(prealloc_txq.txq);
		prealloc_txq.txq = NULL;
	}
}
EXPORT_SYMBOL_GPL(aicwf_prealloc_txq_alloc);
