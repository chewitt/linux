/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_MSG_RX_H_
#define _RWNX_MSG_RX_H_

void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
void rwnx_rx_handle_print(struct rwnx_hw *rwnx_hw, u8 *msg, u32 len);

#endif /* _RWNX_MSG_RX_H_ */
