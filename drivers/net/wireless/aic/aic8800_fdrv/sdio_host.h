/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SDIO_HOST_H_
#define _SDIO_HOST_H_

#include "aicwf_sdio.h"
#include "lmac_types.h"

#define SDIO_TXQUEUE_CNT NX_TXQ_CNT
#define SDIO_TXDESC_CNT  NX_TXDESC_CNT

/// Definition of the IPC Host environment structure.
struct sdio_host_env_tag {
	// Index used that points to the first free TX desc
	u32 txdesc_free_idx[SDIO_TXQUEUE_CNT];
	// Index used that points to the first used TX desc
	u32 txdesc_used_idx[SDIO_TXQUEUE_CNT];
	// Array storing the currently pushed host ids, per IPC queue
	u64 tx_host_id[SDIO_TXQUEUE_CNT][SDIO_TXDESC_CNT];

	/// Pointer to the attached object (used in callbacks and register accesses)
	void *pthis;
};

struct txdesc_host *
aicwf_sdio_host_txdesc_get(struct sdio_host_env_tag *env, const int queue_idx);

void aicwf_sdio_host_init(struct sdio_host_env_tag *env, void *cb,
			  void *shared_env_ptr, void *pthis);

void aicwf_sdio_host_txdesc_push(struct sdio_host_env_tag *env,
				 const int queue_idx,
				 const uint64_t host_id);

void aicwf_sdio_host_tx_cfm_handler(struct sdio_host_env_tag *env,
				    u32 *data);

int aicwf_rwnx_sdio_platform_init(struct aic_sdio_dev *sdiodev);

#endif
