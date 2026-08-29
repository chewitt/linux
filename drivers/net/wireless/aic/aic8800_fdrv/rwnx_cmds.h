/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_CMDS_H_
#define _RWNX_CMDS_H_

#include "lmac_msg.h"
#include "ipc_shared.h"
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#define RWNX_80211_CMD_TIMEOUT_MS 6000

#define RWNX_CMD_FLAG_NONBLOCK  BIT(0)
#define RWNX_CMD_FLAG_REQ_CFM   BIT(1)
#define RWNX_CMD_FLAG_WAIT_PUSH BIT(2)
#define RWNX_CMD_FLAG_WAIT_ACK  BIT(3)
#define RWNX_CMD_FLAG_WAIT_CFM  BIT(4)
#define RWNX_CMD_FLAG_DONE      BIT(5)
/* ATM IPC design makes it possible to get the CFM before the ACK,
 * otherwise this could have simply been a state enum
 */
#define RWNX_CMD_WAIT_COMPLETE(flags)                                          \
	(!((flags) & (RWNX_CMD_FLAG_WAIT_ACK | RWNX_CMD_FLAG_WAIT_CFM)))

#define RWNX_CMD_MAX_QUEUED 16

#define rwnx_cmd_e2amsg         ipc_e2a_msg
#define rwnx_cmd_a2emsg         lmac_msg
#define RWNX_CMD_A2EMSG_LEN(m)  (sizeof(struct lmac_msg) + (m)->param_len)
#define RWNX_CMD_E2AMSG_LEN_MAX (IPC_E2A_MSG_PARAM_SIZE * 4)

struct rwnx_hw;
struct rwnx_cmd;
typedef int (*msg_cb_fct)(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
			  struct rwnx_cmd_e2amsg *msg);

static inline void put_u16(u8 *buf, u16 data)
{
	buf[0] = (u8)(data & 0x00ff);
	buf[1] = (u8)((data >> 8) & 0x00ff);
}

enum rwnx_cmd_mgr_state {
	RWNX_CMD_MGR_STATE_DEINIT,
	RWNX_CMD_MGR_STATE_INITED,
	RWNX_CMD_MGR_STATE_CRASHED,
};

struct rwnx_cmd {
	struct list_head list;
	lmac_msg_id_t id;
	lmac_msg_id_t reqid;
	struct rwnx_cmd_a2emsg *a2e_msg;
	char *e2a_msg;
	u32 tkn;
	u16 flags;

	struct completion complete;
	u32 result;
	u8 used;
	int array_id;
};

struct rwnx_cmd_mgr {
	enum rwnx_cmd_mgr_state state;
	spinlock_t lock; //
	u32 next_tkn;
	u32 queue_sz;
	u32 max_queue_sz;

	struct list_head cmds;

	int (*queue)(struct rwnx_cmd_mgr *mgr, struct rwnx_cmd *cmd);
	int (*llind)(struct rwnx_cmd_mgr *mgr, struct rwnx_cmd *cmd);
	int (*msgind)(struct rwnx_cmd_mgr *mgr, struct rwnx_cmd_e2amsg *msg, msg_cb_fct cb);
	void (*print)(struct rwnx_cmd_mgr *mgr);
	void (*drain)(struct rwnx_cmd_mgr *mgr);

	struct work_struct cmd_work;
	struct workqueue_struct *cmd_wq;
};

#define WAKE_CMD_WORK(_cmd_mgr)							\
	do {									\
		struct rwnx_cmd_mgr *__mgr = (_cmd_mgr);                        \
		queue_work((__mgr)->cmd_wq, &__mgr->cmd_work);		        \
	} while (0)

void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr);
void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr);
int cmd_mgr_queue_force_defer(struct rwnx_cmd_mgr *cmd_mgr,
			      struct rwnx_cmd *cmd);
void aicwf_set_cmd_tx(void *dev, struct lmac_msg *msg, uint len);

void cmd_mgr_task_process(struct work_struct *work);

void rwnx_cmd_free(struct rwnx_cmd *cmd);

#endif /* _RWNX_CMDS_H_ */
