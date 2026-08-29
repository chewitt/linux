/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_TXQ_PREALLOC_H_
#define _AICWF_TXQ_PREALLOC_H_

void *aicwf_prealloc_txq_alloc(size_t size);
void aicwf_prealloc_txq_free(void);

#endif
