// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bsp sdio declarations
 *
 ******************************************************************************
 */

#include <linux/delay.h>
#include <crypto/md5.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "aic8800d80_compat.h"
#include "aic_bsp_driver.h"
#include "aicsdio.h"
#include "aicsdio_txrxif.h"
#include "aic_chip_ops.h"
#include <linux/fs.h>

int cur_mode;

static void cmd_dump(const struct rwnx_cmd *cmd)
{
	if (!cmd) {
		pr_err("aicbsp %s cmd is null\n", __func__);
		return;
	}
	pr_crit("aicbsp tkn[%d]  flags:%04x  result:%3d  cmd:%4d - reqcfm(%4d)\n",
		cmd->tkn, cmd->flags, cmd->result, cmd->id, cmd->reqid);
}

static void cmd_complete(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	lockdep_assert_held(&cmd_mgr->lock);

	list_del(&cmd->list);
	cmd_mgr->queue_sz--;

	cmd->flags |= RWNX_CMD_FLAG_DONE;
	if (cmd->flags & RWNX_CMD_FLAG_NONBLOCK) {
		kfree(cmd);
	} else {
		if (RWNX_CMD_WAIT_COMPLETE(cmd->flags)) {
			cmd->result = 0;
			complete(&cmd->complete);
		}
	}
}

static int cmd_mgr_queue(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	bool defer_push = false;
	int err = 0;

	spin_lock_bh(&cmd_mgr->lock);

	if (cmd_mgr->state == RWNX_CMD_MGR_STATE_CRASHED) {
		pr_crit("aicbsp cmd queue crashed\n");
		cmd->result = -EPIPE;
		spin_unlock_bh(&cmd_mgr->lock);
		return -EPIPE;
	}

	if (!list_empty(&cmd_mgr->cmds)) {
		struct rwnx_cmd *last;

		if (cmd_mgr->queue_sz == cmd_mgr->max_queue_sz) {
			pr_crit("aicbsp Too many cmds (%d) already queued\n",
				cmd_mgr->max_queue_sz);
			cmd->result = -ENOMEM;
			spin_unlock_bh(&cmd_mgr->lock);
			return -ENOMEM;
		}
		last = list_entry(cmd_mgr->cmds.prev, struct rwnx_cmd, list);
		if (last->flags & (RWNX_CMD_FLAG_WAIT_ACK | RWNX_CMD_FLAG_WAIT_PUSH)) {
			cmd->flags |= RWNX_CMD_FLAG_WAIT_PUSH;
			defer_push = true;
		}
	}

	if (cmd->flags & RWNX_CMD_FLAG_REQ_CFM)
		cmd->flags |= RWNX_CMD_FLAG_WAIT_CFM;

	cmd->tkn = cmd_mgr->next_tkn++;
	cmd->result = -EINTR;

	if (!(cmd->flags & RWNX_CMD_FLAG_NONBLOCK))
		init_completion(&cmd->complete);

	list_add_tail(&cmd->list, &cmd_mgr->cmds);
	cmd_mgr->queue_sz++;
	spin_unlock_bh(&cmd_mgr->lock);

	if (!defer_push) {
		rwnx_set_cmd_tx((void *)(cmd_mgr->sdiodev), cmd->a2e_msg,
				sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
		kfree(cmd->a2e_msg);
	} else {
		pr_err("aicbsp ERR: never defer push!!!!");
		return 0;
	}

	if (!(cmd->flags & RWNX_CMD_FLAG_NONBLOCK)) {
		unsigned long tout =
			msecs_to_jiffies(RWNX_80211_CMD_TIMEOUT_MS * cmd_mgr->queue_sz);
		if (!wait_for_completion_timeout(&cmd->complete, tout)) {
			pr_crit("aicbsp cmd timed-out\n");
			cmd_dump(cmd);
			spin_lock_bh(&cmd_mgr->lock);
			cmd_mgr->state = RWNX_CMD_MGR_STATE_CRASHED;
			if (!(cmd->flags & RWNX_CMD_FLAG_DONE)) {
				cmd->result = -ETIMEDOUT;
				cmd_complete(cmd_mgr, cmd);
			}
			spin_unlock_bh(&cmd_mgr->lock);
			err = -ETIMEDOUT;
		} else {
			kfree(cmd);
		}
	} else {
		cmd->result = 0;
	}

	return err;
}

static int cmd_mgr_run_callback(struct rwnx_cmd_mgr *cmd_mgr,
				struct rwnx_cmd *cmd,
				struct rwnx_cmd_e2amsg *msg, msg_cb_fct cb)
{
	int res;

	if (!cb)
		return 0;
	spin_lock(&cmd_mgr->cb_lock);
	res = cb(cmd, msg);
	spin_unlock(&cmd_mgr->cb_lock);

	return res;
}

static int cmd_mgr_msgind(struct rwnx_cmd_mgr *cmd_mgr,
			  struct rwnx_cmd_e2amsg *msg, msg_cb_fct cb)
{
	struct rwnx_cmd *cmd;
	bool found = false;

	// printk("aicbsp cmd->id=%x\n", msg->id);
	spin_lock(&cmd_mgr->lock);
	list_for_each_entry(cmd, &cmd_mgr->cmds, list) {
		if (cmd->reqid == msg->id && (cmd->flags & RWNX_CMD_FLAG_WAIT_CFM)) {
			if (!cmd_mgr_run_callback(cmd_mgr, cmd, msg, cb)) {
				found = true;
				cmd->flags &= ~RWNX_CMD_FLAG_WAIT_CFM;

				if (WARN(msg->param_len > RWNX_CMD_E2AMSG_LEN_MAX,
					 "Unexpect E2A msg len %d > %d\n", msg->param_len,
						 RWNX_CMD_E2AMSG_LEN_MAX)) {
					msg->param_len = RWNX_CMD_E2AMSG_LEN_MAX;
				}

				if (cmd->e2a_msg && msg->param_len)
					memcpy(cmd->e2a_msg, &msg->param, msg->param_len);

				if (RWNX_CMD_WAIT_COMPLETE(cmd->flags))
					cmd_complete(cmd_mgr, cmd);

				break;
			}
		}
	}
	spin_unlock(&cmd_mgr->lock);

	if (!found)
		cmd_mgr_run_callback(cmd_mgr, NULL, msg, cb);

	return 0;
}

static void cmd_mgr_print(struct rwnx_cmd_mgr *cmd_mgr)
{
	struct rwnx_cmd *cur;

	if (!cmd_mgr) {
		pr_err("aicbsp %s cmd_mgr is null\n", __func__);
		return;
	}

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry(cur, &cmd_mgr->cmds, list) {
		cmd_dump(cur);
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

static void cmd_mgr_drain(struct rwnx_cmd_mgr *cmd_mgr)
{
	struct rwnx_cmd *cur, *nxt;

	if (!cmd_mgr) {
		pr_err("aicbsp %s cmd_mgr is null\n", __func__);
		return;
	}

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry_safe(cur, nxt, &cmd_mgr->cmds, list) {
		list_del(&cur->list);
		cmd_mgr->queue_sz--;
		if (!(cur->flags & RWNX_CMD_FLAG_NONBLOCK))
			complete(&cur->complete);
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr)
{
	cmd_mgr->max_queue_sz = RWNX_CMD_MAX_QUEUED;
	INIT_LIST_HEAD(&cmd_mgr->cmds);
	cmd_mgr->state = RWNX_CMD_MGR_STATE_INITED;
	spin_lock_init(&cmd_mgr->lock);
	spin_lock_init(&cmd_mgr->cb_lock);
	cmd_mgr->queue = &cmd_mgr_queue;
	cmd_mgr->print = &cmd_mgr_print;
	cmd_mgr->drain = &cmd_mgr_drain;
	cmd_mgr->llind = NULL;
	cmd_mgr->msgind = &cmd_mgr_msgind;
}

void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr)
{
	if (!cmd_mgr) {
		pr_err("aicbsp %s cmd_mgr is null\n", __func__);
		return;
	}
	cmd_mgr->print(cmd_mgr);
	cmd_mgr->drain(cmd_mgr);
	cmd_mgr->print(cmd_mgr);
	memset(cmd_mgr, 0, sizeof(*cmd_mgr));
}

void rwnx_set_cmd_tx(void *dev, struct lmac_msg *msg, uint len)
{
	struct aic_sdio_dev *sdiodev = (struct aic_sdio_dev *)dev;
	struct aicwf_bus *bus = sdiodev->bus_if;
	u8 *buffer = bus->cmd_buf;
	u16 index = 0;

	memset(buffer, 0, CMD_BUF_MAX);
	buffer[0] = (len + 4) & 0x00ff;
	buffer[1] = ((len + 4) >> 8) & 0x0f;
	buffer[2] = 0x11;
	if (sdiodev->chip_ops->use_hdr_checksum)
		buffer[3] = crc8_ponl_107(&buffer[0], 3); // crc8
	else
		buffer[3] = 0x0;

	index += 4;
	// there is a dummy word
	index += 4;

	// make sure little endian
	put_u16(&buffer[index], msg->id);
	index += 2;
	put_u16(&buffer[index], msg->dest_id);
	index += 2;
	put_u16(&buffer[index], msg->src_id);
	index += 2;
	put_u16(&buffer[index], msg->param_len);
	index += 2;
	memcpy(&buffer[index], (u8 *)msg->param, msg->param_len);

	aicwf_bus_txmsg(bus, buffer, len + 8);
}

static inline void *rwnx_msg_zalloc(lmac_msg_id_t const id,
				    lmac_task_id_t const dest_id,
									lmac_task_id_t const src_id,
									uint16_t const param_len)
{
	struct lmac_msg *msg;
	gfp_t flags;

	if (in_softirq())
		flags = GFP_ATOMIC;
	else
		flags = GFP_KERNEL;

	msg =
		kzalloc(sizeof(struct lmac_msg) + param_len, flags);
	if (!msg) {
		pr_crit("aicbsp %s: msg allocation failed\n", __func__);
		return NULL;
	}
	msg->id = id;
	msg->dest_id = dest_id;
	msg->src_id = src_id;
	msg->param_len = param_len;

	return msg->param;
}

static void rwnx_msg_free(struct lmac_msg *msg, const void *msg_params)
{
	kfree(msg);
}

static int rwnx_send_msg(struct aic_sdio_dev *sdiodev, const void *msg_params,
			 int reqcfm, lmac_msg_id_t reqid, void *cfm)
{
	struct lmac_msg *msg;
	struct rwnx_cmd *cmd;
	bool nonblock;
	int ret = 0;

	msg = container_of((void *)msg_params, struct lmac_msg, param);
	if (sdiodev->bus_if->state == BUS_DOWN_ST) {
		rwnx_msg_free(msg, msg_params);
		pr_info("aicbsp bus is down\n");
		return 0;
	}

	nonblock = 0;
	cmd = kzalloc_obj(*cmd, nonblock ? GFP_ATOMIC : GFP_KERNEL);
	cmd->result = -EINTR;
	cmd->id = msg->id;
	cmd->reqid = reqid;
	cmd->a2e_msg = msg;
	cmd->e2a_msg = cfm;
	if (nonblock)
		cmd->flags = RWNX_CMD_FLAG_NONBLOCK;
	if (reqcfm)
		cmd->flags |= RWNX_CMD_FLAG_REQ_CFM;

	if (reqcfm) {
		cmd->flags &= ~RWNX_CMD_FLAG_WAIT_ACK; // we don't need ack any more
		ret = sdiodev->cmd_mgr.queue(&sdiodev->cmd_mgr, cmd);
	} else {
		rwnx_set_cmd_tx((void *)(sdiodev), cmd->a2e_msg,
				sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
	}

	if (!reqcfm)
		kfree(cmd);

	return ret;
}

int rwnx_send_dbg_mem_block_write_req(struct aic_sdio_dev *sdiodev,
				      u32 mem_addr, u32 mem_size, u32 *mem_data)
{
	struct dbg_mem_block_write_req *mem_blk_write_req;

	/* Build the DBG_MEM_BLOCK_WRITE_REQ message */
	mem_blk_write_req =
		rwnx_msg_zalloc(DBG_MEM_BLOCK_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
				sizeof(struct dbg_mem_block_write_req));
	if (!mem_blk_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_BLOCK_WRITE_REQ message */
	mem_blk_write_req->memaddr = mem_addr;
	mem_blk_write_req->memsize = mem_size;
	memcpy(mem_blk_write_req->memdata, mem_data, mem_size);

	/* Send the DBG_MEM_BLOCK_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(sdiodev, mem_blk_write_req, 1, DBG_MEM_BLOCK_WRITE_CFM,
						 NULL);
}

int rwnx_send_dbg_mem_read_req(struct aic_sdio_dev *sdiodev, u32 mem_addr,
			       struct dbg_mem_read_cfm *cfm)
{
	struct dbg_mem_read_req *mem_read_req;

	/* Build the DBG_MEM_READ_REQ message */
	mem_read_req = rwnx_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG, DRV_TASK_ID,
				       sizeof(struct dbg_mem_read_req));
	if (!mem_read_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_READ_REQ message */
	mem_read_req->memaddr = mem_addr;

	/* Send the DBG_MEM_READ_REQ message to LMAC FW */
	return rwnx_send_msg(sdiodev, mem_read_req, 1, DBG_MEM_READ_CFM, cfm);
}

int rwnx_send_dbg_mem_write_req(struct aic_sdio_dev *sdiodev, u32 mem_addr,
				u32 mem_data)
{
	struct dbg_mem_write_req *mem_write_req;

	/* Build the DBG_MEM_WRITE_REQ message */
	mem_write_req = rwnx_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
					sizeof(struct dbg_mem_write_req));
	if (!mem_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_WRITE_REQ message */
	mem_write_req->memaddr = mem_addr;
	mem_write_req->memdata = mem_data;

	/* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(sdiodev, mem_write_req, 1, DBG_MEM_WRITE_CFM, NULL);
}

int rwnx_send_dbg_mem_mask_write_req(struct aic_sdio_dev *sdiodev, u32 mem_addr,
				     u32 mem_mask, u32 mem_data)
{
	struct dbg_mem_mask_write_req *mem_mask_write_req;

	/* Build the DBG_MEM_MASK_WRITE_REQ message */
	mem_mask_write_req =
		rwnx_msg_zalloc(DBG_MEM_MASK_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
				sizeof(struct dbg_mem_mask_write_req));
	if (!mem_mask_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_MASK_WRITE_REQ message */
	mem_mask_write_req->memaddr = mem_addr;
	mem_mask_write_req->memmask = mem_mask;
	mem_mask_write_req->memdata = mem_data;

	/* Send the DBG_MEM_MASK_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(sdiodev, mem_mask_write_req, 1, DBG_MEM_MASK_WRITE_CFM,
						 NULL);
}

int rwnx_send_dbg_start_app_req(struct aic_sdio_dev *sdiodev, u32 boot_addr,
				u32 boot_type,
				struct dbg_start_app_cfm *start_app_cfm)
{
	struct dbg_start_app_req *start_app_req;

	/* Build the DBG_START_APP_REQ message */
	start_app_req = rwnx_msg_zalloc(DBG_START_APP_REQ, TASK_DBG, DRV_TASK_ID,
					sizeof(struct dbg_start_app_req));
	if (!start_app_req) {
		pr_err("aicbsp start app nomen\n");
		return -ENOMEM;
	}

	/* Set parameters for the DBG_START_APP_REQ message */
	start_app_req->bootaddr = boot_addr;
	start_app_req->boottype = boot_type;

	/* Send the DBG_START_APP_REQ message to LMAC FW */
	return rwnx_send_msg(sdiodev, start_app_req, 1, DBG_START_APP_CFM,
						 start_app_cfm);
}

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {};

static msg_cb_fct *msg_hdlrs[] = {
	[TASK_DBG] = dbg_hdlrs,
};

void rwnx_rx_handle_msg(struct aic_sdio_dev *sdiodev, struct ipc_e2a_msg *msg)
{
	sdiodev->cmd_mgr.msgind(&sdiodev->cmd_mgr, msg,
							msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
}

static inline void print_md5(const u8 *x)
{
	pr_info("aicbsp file md5:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\r\n",
		x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7],
		x[8], x[9], x[10], x[11], x[12], x[13], x[14], x[15]);
}

static int aicbsp_request_firmware(const struct firmware **fw,
				   const char *name, struct device *dev)
{
	char *fw_name;
	int ret;

	if (!name || !name[0])
		return -EINVAL;

	fw_name = kasprintf(GFP_KERNEL, AIC8800_FW_DIR "%s", name);
	if (!fw_name)
		return -ENOMEM;

	ret = request_firmware(fw, fw_name, dev);
	kfree(fw_name);

	return ret;
}

int rwnx_load_firmware(u32 **fw_buf, const char *name, struct device *device)
{
	const struct firmware *fw = NULL;
	u32 *dst = NULL;
	void *buffer = NULL;
#ifdef CONFIG_TEDIOUS_PRT
	u8 digest[MD5_DIGEST_SIZE];
#endif
	int size = 0;
	int ret = 0;

	pr_info("aicbsp %s: request firmware = %s\n", __func__, name);

	ret = aicbsp_request_firmware(&fw, name, device);

	if (ret < 0) {
		pr_err("aicbsp Load %s fail\n", name);
		return ret;
	}

	size = fw->size;
	dst = (u32 *)fw->data;

	if (size <= 0) {
		pr_err("aicbsp wrong size of firmware file\n");
		release_firmware(fw);
		return -1;
	}

	buffer = vmalloc(size);
	if (!buffer) {
		release_firmware(fw);
		return -ENOMEM;
	}
	memset(buffer, 0, size);
	memcpy(buffer, dst, size);

	*fw_buf = buffer;

#ifdef CONFIG_TEDIOUS_PRT
	md5(buffer, size, digest);
	print_md5(digest);
#endif

	release_firmware(fw);

	return size;
}

extern int testmode;

#ifdef CONFIG_M2D_OTA_AUTO_SUPPORT
int rwnx_plat_m2d_flash_ota(struct aic_sdio_dev *sdiodev,
			    char *filename)
{
	struct device *dev = sdiodev->dev;
	unsigned int i = 0;
	int size;
	u32 *dst = NULL;
	int err = 0;
	int ret;
	u8 bond_id;
	const u32 mem_addr = 0x40500000;
	struct dbg_mem_read_cfm rd_mem_addr_cfm;

	ret = rwnx_send_dbg_mem_read_req(sdiodev, mem_addr, &rd_mem_addr_cfm);
	if (ret) {
		pr_err("aicbsp m2d %x rd fail: %d\n", mem_addr, ret);
		return ret;
	}
	bond_id = (u8)(rd_mem_addr_cfm.memdata >> 24);
	pr_info("aicbsp %x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
	if (bond_id & (1 << 1)) {
		// flash is invalid
		pr_err("aicbsp m2d flash is invalid\n");
		return -1;
	}

	/* load aic firmware */
	size = rwnx_load_firmware(&dst, filename, dev);
	if (size <= 0) {
		pr_err("aicbsp wrong size of m2d file\n");
		vfree(dst);
		dst = NULL;
		return -1;
	}

	/* Copy the file on the Embedded side */
	pr_info("aicbsp ### Upload m2d %s flash, size=%d\n", filename, size);

	/*send info first */
	err = rwnx_send_dbg_mem_block_write_req(sdiodev, AIC_M2D_OTA_INFO_ADDR, 4,
						(u32 *)&size);

	/*send data first */
	if (size > 1024) {                              // > 1KB data
		for (i = 0; i < (size - 1024); i += 1024) { // each time write 1KB
			err = rwnx_send_dbg_mem_block_write_req(sdiodev,
								AIC_M2D_OTA_DATA_ADDR,
								1024, dst + i / 4);
			if (err) {
				pr_err("aicbsp m2d upload fail: %x, err:%d\r\n",
				       AIC_M2D_OTA_DATA_ADDR, err);
				break;
			}
		}
	}

	if (!err && i < size) { // <1KB data
		err = rwnx_send_dbg_mem_block_write_req(sdiodev, AIC_M2D_OTA_DATA_ADDR,
							size - i, dst + i / 4);
		if (err) {
			pr_err("aicbsp m2d upload fail: %x, err:%d\r\n", AIC_M2D_OTA_DATA_ADDR,
			       err);
		}
	}

	if (dst) {
		vfree(dst);
		dst = NULL;
	}
	testmode = FW_NORMAL_MODE;
	aicbsp_info.cpmode = testmode;

	pr_info("aicbsp m2d flash update complete\n\n");

	return err;
}

int rwnx_plat_m2d_flash_ota_check(struct aic_sdio_dev *sdiodev, char *filename)
{
	struct device *dev = sdiodev->dev;
	unsigned int i = 0, j = 0;
	int size;
	u32 *dst = NULL;
	int err = 0;
	int ret = 0;
	u8 bond_id;
	const u32 mem_addr = 0x40500000;
	const u32 mem_addr_code_start = AIC_M2D_OTA_CODE_START_ADDR;
	const u32 mem_addr_sdk_ver = AIC_M2D_OTA_VER_ADDR;
	const u32 driver_code_start_idx =
		(AIC_M2D_OTA_CODE_START_ADDR - AIC_M2D_OTA_FLASH_ADDR) / 4;
	const u32 driver_sdk_ver_idx =
		(AIC_M2D_OTA_VER_ADDR - AIC_M2D_OTA_FLASH_ADDR) / 4;
	u32 driver_sdk_ver_addr_idx = 0;
	u32 code_start_addr = 0xffffffff;
	u32 sdk_ver_addr = 0xffffffff;
	u32 drv_code_start_addr = 0xffffffff;
	u32 drv_sdk_ver_addr = 0xffffffff;
	struct dbg_mem_read_cfm rd_mem_addr_cfm;
	char m2d_sdk_ver[64];
	char flash_sdk_ver[64];
	u32 flash_ver[16];
	u32 ota_ver[16];

	ret = rwnx_send_dbg_mem_read_req(sdiodev, mem_addr, &rd_mem_addr_cfm);
	if (ret) {
		pr_err("aicbsp m2d %x rd fail: %d\n", mem_addr, ret);
		return ret;
	}
	bond_id = (u8)(rd_mem_addr_cfm.memdata >> 24);
	pr_info("aicbsp %x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
	if (bond_id & (1 << 1)) {
		// flash is invalid
		pr_err("aicbsp m2d flash is invalid\n");
		return -1;
	}
	ret = rwnx_send_dbg_mem_read_req(sdiodev, mem_addr_code_start,
					 &rd_mem_addr_cfm);
	if (ret) {
		pr_err("mem_addr_code_start %x rd fail: %d\n", mem_addr_code_start,
		       ret);
		return ret;
	}
	code_start_addr = rd_mem_addr_cfm.memdata;

#if !defined(CONFIG_M2D_OTA_LZMA_SUPPORT)
	ret =
		rwnx_send_dbg_mem_read_req(sdiodev, mem_addr_sdk_ver, &rd_mem_addr_cfm);
	if (ret) {
		pr_err("aicbsp mem_addr_sdk_ver %x rd fail: %d\n", mem_addr_code_start, ret);
		return ret;
	}
	sdk_ver_addr = rd_mem_addr_cfm.memdata;
#else
	sdk_ver_addr = mem_addr_sdk_ver;
#endif
	pr_info("aicbsp code_start_addr: 0x%x,	sdk_ver_addr: 0x%x\n", code_start_addr,
		sdk_ver_addr);

	/* load aic firmware */
	size = rwnx_load_firmware(&dst, filename, dev);
	if (size <= 0) {
		pr_err("aicbsp wrong size of m2d file\n");
		vfree(dst);
		dst = NULL;
		return -1;
	}
	if (code_start_addr == 0xffffffff && sdk_ver_addr == 0xffffffff) {
		pr_warn("aicbsp ########m2d flash old version , must be upgrade\n");
		drv_code_start_addr = dst[driver_code_start_idx];
		drv_sdk_ver_addr = dst[driver_sdk_ver_idx];

		pr_info("aicbsp drv_code_start_addr: 0x%x,	drv_sdk_ver_addr: 0x%x\n",
			drv_code_start_addr, drv_sdk_ver_addr);

		if (drv_sdk_ver_addr == 0xffffffff) {
			pr_err("aicbsp ########driver m2d_ota.bin is old ,not need upgrade\n");
			return -1;
		}

	} else {
		for (i = 0; i < 16; i++) {
			ret = rwnx_send_dbg_mem_read_req(sdiodev, (sdk_ver_addr + i * 4),
							 &rd_mem_addr_cfm);
			if (ret) {
				pr_err("aicbsp mem_addr_sdk_ver %x rd fail: %d\n",
				       mem_addr_code_start, ret);
				return ret;
			}
			flash_ver[i] = rd_mem_addr_cfm.memdata;
		}
		memcpy((u8 *)flash_sdk_ver, (u8 *)flash_ver, 64);
		memcpy((u8 *)saved_sdk_ver, (u8 *)flash_sdk_ver, 64);
		pr_info("aicbsp flash SDK Version: %s\r\n\r\n", flash_sdk_ver);

		drv_code_start_addr = dst[driver_code_start_idx];
		drv_sdk_ver_addr = dst[driver_sdk_ver_idx];

		pr_info("aicbsp drv_code_start_addr: 0x%x,	drv_sdk_ver_addr: 0x%x\n",
			drv_code_start_addr, drv_sdk_ver_addr);

		if (drv_sdk_ver_addr == 0xffffffff) {
			pr_err("aicbsp ########driver m2d_ota.bin is old ,not need upgrade\n");
			return -1;
		}
#if !defined(CONFIG_M2D_OTA_LZMA_SUPPORT)
		driver_sdk_ver_addr_idx = (drv_sdk_ver_addr - drv_code_start_addr) / 4;
#else
		driver_sdk_ver_addr_idx = driver_sdk_ver_idx;
#endif
		pr_info("aicbsp driver_sdk_ver_addr_idx %d\n", driver_sdk_ver_addr_idx);

		if (driver_sdk_ver_addr_idx) {
			for (j = 0; j < 16; j++)
				ota_ver[j] = dst[driver_sdk_ver_addr_idx + j];

			memcpy((u8 *)m2d_sdk_ver, (u8 *)ota_ver, 64);
			pr_info("aicbsp m2d_ota SDK Version: %s\r\n\r\n", m2d_sdk_ver);
		} else {
			return -1;
		}

		if (!strcmp(m2d_sdk_ver, flash_sdk_ver)) {
			pr_err("aicbsp ######## m2d %s flash is not need upgrade\r\n", filename);
			return -1;
		}
	}

	/* Copy the file on the Embedded side */
	pr_info("aicbsp ### Upload m2d %s flash, size=%d\n", filename, size);

	/*send info first */
	err = rwnx_send_dbg_mem_block_write_req(sdiodev, AIC_M2D_OTA_INFO_ADDR, 4,
						(u32 *)&size);

	/*send data first */
	if (size > 1024) {                              // > 1KB data
		for (i = 0; i < (size - 1024); i += 1024) { // each time write 1KB
			err = rwnx_send_dbg_mem_block_write_req(sdiodev, AIC_M2D_OTA_DATA_ADDR,
								1024, dst + i / 4);
			if (err) {
				pr_err("aicbsp m2d upload fail: %x, err:%d\r\n",
				       AIC_M2D_OTA_DATA_ADDR, err);
				break;
			}
		}
	}

	if (!err && i < size) {
		err = rwnx_send_dbg_mem_block_write_req(sdiodev, AIC_M2D_OTA_DATA_ADDR,
							size - i, dst + i / 4);
		if (err) {
			pr_err("aicbsp m2d upload fail: %x, err:%d\r\n", AIC_M2D_OTA_DATA_ADDR,
			       err);
		}
	}

	if (dst) {
		vfree(dst);
		dst = NULL;
	}
	testmode = FW_NORMAL_MODE;

	pr_info("aicbsp m2d flash update complete\n\n");

	return err;
}
#endif // CONFIG_M2D_OTA_AUTO_SUPPORT

#ifdef CONFIG_DPD
/**
 * var dpd_res - DPD calibration result shared with the WLAN driver
 */
struct rf_misc_ram_lite_t dpd_res = {
	{0},
};
EXPORT_SYMBOL_GPL(dpd_res);
#endif

int rwnx_plat_bin_fw_upload(struct aic_sdio_dev *sdiodev, u32 fw_addr,
			    const char *filename)
{
	struct device *dev = sdiodev->dev;
	unsigned int i = 0;
	int size;
	u32 *dst = NULL;
	int err = 0;

	pr_info("aicbsp %s\n", __func__);

	/* load aic firmware */
	size = rwnx_load_firmware(&dst, filename, dev);
	if (size <= 0) {
		pr_err("aicbsp wrong size of firmware file\n");
		vfree(dst);
		dst = NULL;
		return -1;
	}

	/* Copy the file on the Embedded side */
	if (size > 1024) {                              // > 1KB data
		for (i = 0; i < (size - 1024); i += 1024) { // each time write 1KB
			err = rwnx_send_dbg_mem_block_write_req(sdiodev, fw_addr + i, 1024,
								dst + i / 4);
			if (err) {
				pr_err("aicbsp bin upload fail: %x, err:%d\r\n", fw_addr + i, err);
				break;
			}
		}
	}

	if (!err && i < size) { // <1KB data
		err = rwnx_send_dbg_mem_block_write_req(sdiodev, fw_addr + i, size - i,
							dst + i / 4);
		if (err)
			pr_err("aicbsp bin upload fail: %x, err:%d\r\n", fw_addr + i, err);
	}

	if (dst) {
		vfree(dst);
		dst = NULL;
	}

	return err;
}

int aicbt_patch_table_free(struct aicbt_patch_table **head)
{
	struct aicbt_patch_table *p = *head, *n = NULL;

	while (p) {
		n = p->next;
		vfree(p->name);
		vfree(p->data);
		vfree(p);
		p = n;
	}
	*head = NULL;
	return 0;
}

struct aicbt_patch_table *aicbt_patch_table_alloc(const char *filename)
{
	u8 *rawdata = NULL, *p;
	int size;
	struct aicbt_patch_table *head = NULL, *new = NULL, *cur = NULL;

	/* load aic firmware */
	size = rwnx_load_firmware((u32 **)&rawdata, filename, NULL);
	if (size <= 0) {
		pr_err("aicbsp wrong size of firmware file\n");
		goto err;
	}

	p = rawdata;
	if (memcmp(p, AICBT_PT_TAG,
		   sizeof(AICBT_PT_TAG) < 16 ? sizeof(AICBT_PT_TAG) : 16)) {
		pr_err("aicbsp TAG err\n");
		goto err;
	}
	p += 16;

	while (p - rawdata < size) {
		new = vmalloc(sizeof(*new));
		memset(new, 0, sizeof(struct aicbt_patch_table));
		if (!head) {
			head = new;
			cur = new;
		} else {
			cur->next = new;
			cur = cur->next;
		}

		cur->name = vmalloc(sizeof(char) * 16);
		memset(cur->name, 0, sizeof(char) * 16);
		memcpy(cur->name, p, 16);
		p += 16;

		cur->type = *(uint32_t *)p;
		p += 4;

		cur->len = *(uint32_t *)p;
		p += 4;

		if (cur->type >= 1000) { // Temp Workaround
			cur->len = 0;
		} else {
			if (cur->len > 0) {
				cur->data = vmalloc(sizeof(uint8_t) * cur->len * 8);
				memset(cur->data, 0, sizeof(uint8_t) * cur->len * 8);
				memcpy(cur->data, p, cur->len * 8);
				p += cur->len * 8;
			}
		}
	}
	vfree(rawdata);
	return head;

err:
	aicbt_patch_table_free(&head);
	if (rawdata)
		vfree(rawdata);
	return NULL;
}

int aicbt_patch_info_unpack(struct aicbt_patch_info_t *patch_info,
			    struct aicbt_patch_table *head_t)
{
	u8 *patch_info_array = (u8 *)patch_info;

	if (head_t->type == AICBT_PT_INF) {
		patch_info->info_len = head_t->len;
		if (patch_info->info_len == 0)
			return 0;

		memcpy(patch_info_array + sizeof(patch_info->info_len),
		       head_t->data,
		       patch_info->info_len * sizeof(uint32_t) * 2);
		AICWFDBG(LOGDEBUG, "%s adid_addrinf:%x addr_adid:%x \r\n", __func__,
			 ((struct aicbt_patch_info_t *)patch_info_array)->adid_addrinf,
			 ((struct aicbt_patch_info_t *)patch_info_array)->addr_adid);
	}
	return 0;
}

int aicbt_patch_trap_data_load(struct aic_sdio_dev *sdiodev,
			       struct aicbt_patch_table *head)
{
	struct aicbt_patch_info_t patch_info = {
		.info_len = 0,
		.adid_addrinf = 0,
		.addr_adid = 0,
		.patch_addrinf = 0,
		.addr_patch = 0,
		.reset_addr = 0,
		.reset_val = 0,
		.adid_flag_addr = 0,
		.adid_flag = 0,
	};

	int ret;

	if (!head)
		return -1;

	ret = aic_chip_set_patch_info(sdiodev, &patch_info, &aicbsp_info, head);
	if (ret) {
		pr_err("aicbsp chip set patch info fail\n");
		return ret;
	}

	if (rwnx_plat_bin_fw_upload(sdiodev, patch_info.addr_adid,
				    aicbsp_firmware_list[aicbsp_info.cpmode].bt_adid))
		return -1;
	if (rwnx_plat_bin_fw_upload(sdiodev, patch_info.addr_patch,
				    aicbsp_firmware_list[aicbsp_info.cpmode].bt_patch))
		return -1;
	return 0;
}

static struct aicbt_info_t aicbt_info[] = {
	{
		.btmode = AICBT_BTMODE_DEFAULT,
		.btport = AICBT_BTPORT_DEFAULT,
		.uart_baud = AICBT_UART_BAUD_DEFAULT,
		.uart_flowctrl = AICBT_UART_FC_DEFAULT,
		.lpm_enable = AICBT_LPM_ENABLE_DEFAULT,
		.txpwr_lvl = AICBT_TXPWR_LVL_DEFAULT,
	}, // PRODUCT_ID_AIC8801
	{
		.btmode = AICBT_BTMODE_BT_WIFI_COMBO,
		.btport = AICBT_BTPORT_DEFAULT,
		.uart_baud = AICBT_UART_BAUD_DEFAULT,
		.uart_flowctrl = AICBT_UART_FC_DEFAULT,
		.lpm_enable = AICBT_LPM_ENABLE_DEFAULT,
		.txpwr_lvl = AICBT_TXPWR_LVL_DEFAULT_8800dc,
	}, // PRODUCT_ID_AIC8800DC
	{
		.btmode = AICBT_BTMODE_BT_WIFI_COMBO,
		.btport = AICBT_BTPORT_DEFAULT,
		.uart_baud = AICBT_UART_BAUD_DEFAULT,
		.uart_flowctrl = AICBT_UART_FC_DEFAULT,
		.lpm_enable = AICBT_LPM_ENABLE_DEFAULT,
		.txpwr_lvl = AICBT_TXPWR_LVL_DEFAULT_8800dc,
	}, // PRODUCT_ID_AIC8800DW
	{
		.btmode = AICBT_BTMODE_DEFAULT_8800d80,
		.btport = AICBT_BTPORT_DEFAULT,
		.uart_baud = AICBT_UART_BAUD_DEFAULT,
		.uart_flowctrl = AICBT_UART_FC_DEFAULT,
		.lpm_enable = AICBT_LPM_ENABLE_DEFAULT,
		.txpwr_lvl = AICBT_TXPWR_LVL_DEFAULT_8800d80,
	} // PRODUCT_ID_AIC8800D80
};

int aicbt_patch_table_load(struct aic_sdio_dev *sdiodev,
			   struct aicbt_patch_table *head)
{
	struct aicbt_patch_table *p;
	int ret = 0, i;
	u32 *data = NULL;

	if (!head)
		return -1;

	for (p = head; p; p = p->next) {
		data = p->data;
		if (p->type == AICBT_PT_BTMODE) {
			*(data + 1) = aicbsp_info.hwinfo < 0;
			*(data + 3) = aicbsp_info.hwinfo;
			*(data + 5) = (sdiodev->chipid == PRODUCT_ID_AIC8800DC
							   ? aicbsp_info.cpmode
							   : 0); // 0;//aicbsp_info.cpmode;

			*(data + 7) = aicbt_info[sdiodev->chipid].btmode;
			*(data + 9) = aicbt_info[sdiodev->chipid].btport;
			*(data + 11) = aicbt_info[sdiodev->chipid].uart_baud;
			*(data + 13) = aicbt_info[sdiodev->chipid].uart_flowctrl;
			*(data + 15) = aicbt_info[sdiodev->chipid].lpm_enable;
			*(data + 17) = aicbt_info[sdiodev->chipid].txpwr_lvl;

			pr_info("aicbsp %s bt btmode[%d]:%d \r\n", __func__, sdiodev->chipid,
				aicbt_info[sdiodev->chipid].btmode);
			pr_info("aicbsp %s bt uart_baud[%d]:%d \r\n", __func__, sdiodev->chipid,
				aicbt_info[sdiodev->chipid].uart_baud);
			pr_info("aicbsp %s bt uart_flowctrl[%d]:%d \r\n", __func__, sdiodev->chipid,
				aicbt_info[sdiodev->chipid].uart_flowctrl);
			pr_info("aicbsp %s bt lpm_enable[%d]:%d \r\n", __func__, sdiodev->chipid,
				aicbt_info[sdiodev->chipid].lpm_enable);
			pr_info("aicbsp %s bt tx_pwr[%d]:%d \r\n", __func__, sdiodev->chipid,
				aicbt_info[sdiodev->chipid].txpwr_lvl);
		}

		if (p->type == AICBT_PT_VER) {
			pr_info("aicbsp bt patch version: %s\n", (char *)p->data);
			continue;
		}

		for (i = 0; i < p->len; i++) {
			ret = rwnx_send_dbg_mem_write_req(sdiodev, *data, *(data + 1));
			if (ret != 0)
				return ret;
			data += 2;
		}
		if (p->type == AICBT_PT_PWRON)
			usleep_range(50, 100);
	}

	return 0;
}

int aicbt_init(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;
	struct aicbt_patch_table *head =
		aicbt_patch_table_alloc(aicbsp_firmware_list[aicbsp_info.cpmode].bt_table);
	if (!head) {
		pr_err("aicbsp aicbt_patch_table_alloc fail\n");
		return -1;
	}

	if (aicbt_patch_trap_data_load(sdiodev, head)) {
		pr_err("aicbsp aicbt_patch_trap_data_load fail\n");
		ret = -1;
		goto err;
	}

	if (aicbt_patch_table_load(sdiodev, head)) {
		pr_err("aicbsp aicbt_patch_table_load fail\n");
		ret = -1;
		goto err;
	}

err:
	aicbt_patch_table_free(&head);
	return ret;
}

int aicwifi_start_from_bootrom(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;

	/* memory access */
	const u32 fw_addr = RAM_FMAC_FW_ADDR;
	struct dbg_start_app_cfm start_app_cfm;

	/* fw start */
	ret = rwnx_send_dbg_start_app_req(sdiodev, fw_addr, HOST_START_APP_AUTO,
					  &start_app_cfm);
	if (ret)
		return -1;
	aicbsp_info.hwinfo_r = start_app_cfm.bootstatus & 0xFF;

	return 0;
}

static u32 adaptivity_patch_tbl[][2] = {
	{0x0004, 0x0000320A}, // linkloss_thd
	{0x0094, 0x00000000}, // ac_param_conf
	{0x00F8, 0x00010138}, // tx_adaptivity_en
};

static u32 patch_tbl[][2] = {
#if !defined(CONFIG_LINK_DET_5G)
	{0x0104, 0x00000000}, // link_det_5g
#endif
};

static u32 syscfg_tbl_masked[][3] = {
	{0x40506024, 0x000000FF, 0x000000DF}, // for clk gate lp_level
};

static u32 rf_tbl_masked[][3] = {
	{0x40344058, 0x00800000, 0x00000000}, // pll trx
};

int aicwifi_sys_config(struct aic_sdio_dev *sdiodev)
{
	int ret, cnt;
	int syscfg_num = sizeof(syscfg_tbl_masked) / sizeof(u32) / 3;

	for (cnt = 0; cnt < syscfg_num; cnt++) {
		ret = rwnx_send_dbg_mem_mask_write_req(sdiodev, syscfg_tbl_masked[cnt][0],
						       syscfg_tbl_masked[cnt][1],
						       syscfg_tbl_masked[cnt][2]);
		if (ret) {
			pr_err("aicbsp %x mask write fail: %d\n", syscfg_tbl_masked[cnt][0], ret);
			return ret;
		}
	}

	ret = rwnx_send_dbg_mem_mask_write_req(sdiodev, rf_tbl_masked[0][0], rf_tbl_masked[0][1],
					       rf_tbl_masked[0][2]);
	if (ret) {
		pr_err("aicbsp rf config %x write fail: %d\n", rf_tbl_masked[0][0], ret);
		return ret;
	}

	return 0;
}

int aicwifi_patch_config(struct aic_sdio_dev *sdiodev)
{
	const u32 rd_patch_addr = RAM_FMAC_FW_ADDR + 0x0180;
	u32 config_base;
	u32 start_addr = 0x1e6000;
	u32 patch_addr = start_addr;
	u32 patch_num = sizeof(patch_tbl) / 4;
	struct dbg_mem_read_cfm rd_patch_addr_cfm;
	u32 patch_addr_reg = 0x1e5318;
	u32 patch_num_reg = 0x1e531c;
	int ret = 0;
	u16 cnt = 0;
	int tmp_cnt = 0;
	int adap_patch_num = 0;

	if (aicbsp_info.cpmode == AICBSP_CPMODE_TEST) {
		patch_addr_reg = 0x1e5304;
		patch_num_reg = 0x1e5308;
	}

	ret =
		rwnx_send_dbg_mem_read_req(sdiodev, rd_patch_addr, &rd_patch_addr_cfm);
	if (ret) {
		pr_err("aicbsp patch rd fail\n");
		return ret;
	}

	config_base = rd_patch_addr_cfm.memdata;

	ret = rwnx_send_dbg_mem_write_req(sdiodev, patch_addr_reg, patch_addr);
	if (ret) {
		pr_err("aicbsp 0x%x write fail\n", patch_addr_reg);
		return ret;
	}

	if (aicbsp_info.adap_test) {
		pr_err("aicbsp %s for adaptivity test \r\n", __func__);
		adap_patch_num = sizeof(adaptivity_patch_tbl) / 4;
		ret = rwnx_send_dbg_mem_write_req(sdiodev, patch_num_reg,
						  patch_num + adap_patch_num);
	} else {
		ret = rwnx_send_dbg_mem_write_req(sdiodev, patch_num_reg, patch_num);
	}
	if (ret) {
		pr_err("aicbsp 0x%x write fail\n", patch_num_reg);
		return ret;
	}

	for (cnt = 0; cnt < patch_num / 2; cnt += 1) {
		ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr + 8 * cnt,
						  patch_tbl[cnt][0] + config_base);
		if (ret) {
			pr_err("aicbsp %x write fail\n", start_addr + 8 * cnt);
			return ret;
		}

		ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr + 8 * cnt + 4,
						  patch_tbl[cnt][1]);
		if (ret) {
			pr_err("aicbsp %x write fail\n", start_addr + 8 * cnt + 4);
			return ret;
		}
	}

	tmp_cnt = cnt;

	if (aicbsp_info.adap_test) { //(adap_test){
		for (cnt = 0; cnt < adap_patch_num / 2; cnt += 1) {
			ret = rwnx_send_dbg_mem_write_req(sdiodev,
							  start_addr + 8 * (cnt + tmp_cnt),
							  adaptivity_patch_tbl[cnt][0]
							  + config_base);
			if (ret)
				pr_err("aicbsp %x write fail\n", start_addr + 8 * cnt);

			ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr +
							  8 * (cnt + tmp_cnt) + 4,
							  adaptivity_patch_tbl[cnt][1]);
			if (ret)
				pr_err("aicbsp %x write fail\n", start_addr + 8 * cnt + 4);
		}
	}

	return 0;
}

int aicwifi_init(struct aic_sdio_dev *sdiodev)
{
	int ret = 0;

	ret = aic_chip_wifi_init(sdiodev, testmode);
	if (ret) {
		pr_err("aicbsp wifi init fail\n");
		return ret;
	}

#ifdef CONFIG_GPIO_WAKEUP
	if (aicwf_sdio_writeb(sdiodev, sdiodev->sdio_reg.wakeup_reg, 4)) {
		sdio_err("reg:%d write failed!\n", sdiodev->sdio_reg.wakeup_reg);
		return -1;
	}
#endif
	return 0;
}

static u32 aicbsp_syscfg_tbl[][2] = {
	{0x40500014, 0x00000101}, // 1)
	{0x40500018, 0x00000109}, // 2)
	{0x40500004, 0x00000010}, // 3) the order should not be changed

	// def CONFIG_PMIC_SETTING
	// U02 bootrom only
	{0x40040000, 0x00001AC8}, // 1) fix panic
	{0x40040084, 0x00011580},
	{0x40040080, 0x00000001},
	{0x40100058, 0x00000000},

	{0x50000000, 0x03220204}, // 2) pmic interface init
	{0x50019150, 0x00000002}, // 3) for 26m xtal, set div1
	{0x50017008, 0x00000000}, // 4) stop wdg
};

int aicbsp_system_config(struct aic_sdio_dev *sdiodev)
{
	int syscfg_num = sizeof(aicbsp_syscfg_tbl) / sizeof(u32) / 2;
	int ret, cnt;

	for (cnt = 0; cnt < syscfg_num; cnt++) {
		ret = rwnx_send_dbg_mem_write_req(sdiodev, aicbsp_syscfg_tbl[cnt][0],
						  aicbsp_syscfg_tbl[cnt][1]);
		if (ret) {
			sdio_err("%x write fail: %d\n", aicbsp_syscfg_tbl[cnt][0], ret);
			return ret;
		}
	}
	return 0;
}

int aicbsp_platform_init(struct aic_sdio_dev *sdiodev)
{
	rwnx_cmd_mgr_init(&sdiodev->cmd_mgr);
	sdiodev->cmd_mgr.sdiodev = (void *)sdiodev;

	return 0;
}

void aicbsp_platform_deinit(struct aic_sdio_dev *sdiodev)
{
	(void)sdiodev;
}

int aicbsp_driver_fw_init(struct aic_sdio_dev *sdiodev)
{
	u32 btenable = 0;
	int ret = 0;

	aicbsp_info.cpmode = AICBSP_CPMODE_WORK;
	testmode = AICBSP_CPMODE_WORK;
	cur_mode = AICBSP_CPMODE_WORK;

	ret = aic_chip_driver_fw_init(sdiodev, &btenable, &aicbsp_info, &aicbsp_firmware_list);
	if (ret) {
		pr_err("aicbsp chip driver fw init fail\n");
		return -1;
	}

	AICWFDBG(LOGINFO, "aicbsp: %s, chip rev: %d\n", __func__,
		 aicbsp_info.chip_rev);

	ret = aicwifi_init(sdiodev);
	if (ret)
		return ret;

	return 0;
}

/**
 * aicbsp_get_feature - Read BSP settings required by the WLAN driver
 * @feature: Location in which to return the current BSP settings
 *
 * Return: 0 on success, or a negative error code if the SDIO device is absent.
 */
int aicbsp_get_feature(struct aicbsp_feature_t *feature)
{
	if (!aicbsp_sdiodev) {
		sdio_err("%s, aicbsp_sdiodev is null\n", __func__);
		return -1;
	}

	feature->sdio_clock = aicbsp_sdiodev->chip_ops->sdio_clock;

	feature->sdio_phase = FEATURE_SDIO_PHASE;
	feature->cpmode = aicbsp_info.cpmode;
	feature->adapt = aicbsp_info.adap_test;
	feature->hwinfo = aicbsp_info.hwinfo;
	feature->fwlog_en = aicbsp_info.fwlog_en;
	feature->irqf = aicbsp_info.irqf;
	sdio_dbg("%s, set FEATURE_SDIO_CLOCK %d MHz\n", __func__,
		 feature->sdio_clock / 1000000);
	return 0;
}
EXPORT_SYMBOL_GPL(aicbsp_get_feature);

#ifdef CONFIG_RESV_MEM_SUPPORT
static struct skb_buff_pool resv_skb[] = {
	{AIC_RESV_MEM_TXDATA, 1536 * 64, "resv_mem_txdata", 0, NULL},
};

int aicbsp_resv_mem_init(void)
{
	int i = 0;

	pr_info("aicbsp %s\n", __func__);
	for (i = 0; i < ARRAY_SIZE(resv_skb); i++)
		resv_skb[i].skb = dev_alloc_skb(resv_skb[i].size);
	return 0;
}

int aicbsp_resv_mem_deinit(void)
{
	int i = 0;

	pr_info("aicbsp %s\n", __func__);
	for (i = 0; i < ARRAY_SIZE(resv_skb); i++) {
		if (resv_skb[i].used == 0 && resv_skb[i].skb)
			dev_kfree_skb(resv_skb[i].skb);
	}
	return 0;
}

/**
 * aicbsp_resv_mem_alloc_skb - Acquire a reserved socket buffer
 * @length: Minimum required buffer size
 * @id: Reserved-buffer pool identifier
 *
 * Return: The reserved buffer, or %NULL if it is too small, already in use,
 * or cannot be allocated.
 */
struct sk_buff *aicbsp_resv_mem_alloc_skb(unsigned int length, uint32_t id)
{
	if (resv_skb[id].size < length) {
		pr_err("aicbsp: %s, no enough mem\n", __func__);
		goto fail;
	}

	if (resv_skb[id].used) {
		pr_err("aicbsp: %s, mem in use\n", __func__);
		goto fail;
	}

	if (!resv_skb[id].skb) {
		//pr_err("aicbsp: %s, skb not initialazed\n", __func__);
		resv_skb[id].skb =
			dev_alloc_skb(resv_skb[id].size);
		if (!resv_skb[id].skb) {
			pr_err("aicbsp: %s, mem reinitial still fail\n", __func__);
			goto fail;
		}
	}

	pr_info("aicbsp: %s, alloc %s succuss, id: %d, size: %d\n", __func__,
		resv_skb[id].name, resv_skb[id].id, resv_skb[id].size);

	resv_skb[id].used = 1;
	return resv_skb[id].skb;

fail:
	return NULL;
}
EXPORT_SYMBOL_GPL(aicbsp_resv_mem_alloc_skb);

/**
 * aicbsp_resv_mem_kfree_skb - Release a reserved socket buffer for reuse
 * @skb: Reserved buffer returned by aicbsp_resv_mem_alloc_skb()
 * @id: Reserved-buffer pool identifier
 */
void aicbsp_resv_mem_kfree_skb(struct sk_buff *skb, uint32_t id)
{
	resv_skb[id].used = 0;
	pr_info("aicbsp: %s, free %s succuss, id: %d, size: %d\n", __func__,
		resv_skb[id].name, resv_skb[id].id, resv_skb[id].size);
}
EXPORT_SYMBOL_GPL(aicbsp_resv_mem_kfree_skb);

#else

int aicbsp_resv_mem_init(void)
{
	return 0;
}

int aicbsp_resv_mem_deinit(void)
{
	return 0;
}

#endif
