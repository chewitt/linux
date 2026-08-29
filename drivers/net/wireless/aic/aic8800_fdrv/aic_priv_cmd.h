/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AIC_PRIV_CMD_H_
#define _AIC_PRIV_CMD_H_

#include "rwnx_defs.h"

enum {
	SET_TX,
	SET_TXSTOP,
	SET_TXTONE,
	SET_RX,
	GET_RX_RESULT,
	SET_RXSTOP,
	SET_RX_METER,
	SET_POWER,
	SET_XTAL_CAP,
	SET_XTAL_CAP_FINE,
	GET_EFUSE_BLOCK,
	SET_FREQ_CAL,
	SET_FREQ_CAL_FINE,
	GET_FREQ_CAL,
	SET_MAC_ADDR,
	GET_MAC_ADDR,
	SET_BT_MAC_ADDR,
	GET_BT_MAC_ADDR,
	SET_VENDOR_INFO,
	GET_VENDOR_INFO,
	RDWR_PWRMM,
	RDWR_PWRIDX,
	RDWR_PWRLVL = RDWR_PWRIDX,
	RDWR_PWROFST,
	RDWR_DRVIBIT,
	RDWR_EFUSE_PWROFST,
	RDWR_EFUSE_DRVIBIT,
	SET_PAPR,
	SET_CAL_XTAL,
	GET_CAL_XTAL_RES,
	SET_COB_CAL,
	GET_COB_CAL_RES,
	RDWR_EFUSE_USRDATA,
	SET_NOTCH,
	RDWR_PWROFSTFINE,
	RDWR_EFUSE_PWROFSTFINE,
	RDWR_EFUSE_SDIOCFG,
	RDWR_EFUSE_USBVIDPID,
	SET_SRRC,
	SET_FSS,
	RDWR_EFUSE_HE_OFF,
	SET_USB_OFF,
	SET_PLL_TEST,
	SET_ANT_MODE,
	GET_NOISE,
	RDWR_BT_EFUSE_PWROFST,
	EXEC_FLASH_OPER,
	RDWR_PWRADD2X,
	RDWR_EFUSE_PWRADD2X,

};

struct cmd_rf_settx {
	u8 chan;
	u8 bw;
	u8 mode;
	u8 rate;
	u16 length;
	u16 tx_intv_us;
	s8 max_pwr;
};

struct cmd_rf_setfreq {
	u8 val;
};

struct cmd_rf_rx {
	u8 chan;
	u8 bw;
};

struct cmd_rf_getefuse {
	u8 block;
};

struct cmd_rf_setcobcal {
	u8 dutid;
	u8 chip_num;
	u8 dis_xtal;
};

struct cob_result_ptr {
	u16 dut_rcv_golden_num;
	u8 golden_rcv_dut_num;
	s8 rssi_static;
	s8 snr_static;
	s8 dut_rssi_static;
	u16 reserved;
};

extern int reg_regdb_size;

unsigned int command_strtoul(const char *cp, char **endp, unsigned int base);
int handle_private_cmd(struct net_device *net, char *command, u32 cmd_len);

#endif /* _AIC_PRIV_CMD_H_ */
