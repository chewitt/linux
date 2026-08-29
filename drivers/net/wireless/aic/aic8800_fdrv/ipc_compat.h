/* SPDX-License-Identifier: GPL-2.0 */
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 ****************************************************************************************
 */

#ifndef _IPC_H_
#define _IPC_H_

#define __INLINE inline

#define __ALIGN4 __aligned(4)

#define ASSERT_ERR(condition)                                                  \
	do {                                                                       \
		if (unlikely(!(condition))) {                                          \
			pr_err("%s:%d:ASSERT_ERR(" #condition ")\n", __FILE__,    \
				   __LINE__);                                                  \
		}                                                                      \
	} while (0)

#endif /* _IPC_H_ */
