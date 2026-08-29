/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_PLATFORM_H_
#define _RWNX_PLATFORM_H_

#include "lmac_msg.h"
#include <linux/pci.h>

#define AIC8800_FW_DIR "aic8800/"

#define RWNX_CONFIG_FW_NAME        "rwnx_settings.ini"
#define RWNX_PHY_CONFIG_TRD_NAME   "rwnx_trident.ini"
#define RWNX_PHY_CONFIG_KARST_NAME "rwnx_karst.ini"
#define RWNX_AGC_FW_NAME           "agcram.bin"
#define RWNX_LDPC_RAM_NAME         "ldpcram.bin"

#define RWNX_MAC_FW_BASE_NAME "fmacfw"

#define RWNX_MAC_FW_NAME  RWNX_MAC_FW_BASE_NAME ".ihex"
#define RWNX_MAC_FW_NAME2 RWNX_MAC_FW_BASE_NAME ".bin"

#define RWNX_FCU_FW_NAME "fcuram.bin"
#if (defined(CONFIG_DPD) && !defined(CONFIG_FORCE_DPD_CALIB))
#define FW_DPDRESULT_NAME_8800DC "aic_dpdresult_lite_8800dc.bin"
#endif

#define POWER_LEVEL_INVALID_VAL (127)

/*
 * Type of memory to access (cf rwnx_plat.get_address)
 *
 * @RWNX_ADDR_CPU To access memory of the embedded CPU
 * @RWNX_ADDR_SYSTEM To access memory/registers of one subsystem of the
 * embedded system
 *
 */
enum rwnx_platform_addr {
	RWNX_ADDR_CPU,
	RWNX_ADDR_SYSTEM,
	RWNX_ADDR_MAX,
};

enum regions_code {
	REGIONS_SRRC,
	REGIONS_FCC,
	REGIONS_ETSI,
	REGIONS_JP,
	REGIONS_DEFAULT,
};

struct rwnx_hw;
struct device;
struct firmware;

int aicwf_request_firmware(const struct firmware **fw, const char *name,
			   struct device *dev);

/**
 * struct rwnx_plat - Operations and state for an RWNX platform
 *
 * @pci_dev: pointer to pci dev
 * @sdiodev: SDIO device data
 * @enabled: Set if embedded platform has been enabled (i.e. fw loaded and
 *          ipc started)
 * @enable: Configure communication with the fw (i.e. configure the transfers
 *         enable and register interrupt)
 * @disable: Stop communication with the fw
 * @deinit: Free all resources allocated for the embedded platform
 * @get_address: Return the virtual address to access the requested address on
 *              the platform.
 * @ack_irq: Acknowledge the irq at link level.
 * @get_config_reg: Return the list (size + pointer) of registers to restore in
 * order to reload the platform while keeping the current configuration.
 *
 * @priv: Private data for the link driver
 */
struct rwnx_plat {
	struct pci_dev *pci_dev;

#ifdef AICWF_SDIO_SUPPORT
	struct aic_sdio_dev *sdiodev;
#endif

	bool enabled;

	int (*enable)(struct rwnx_hw *rwnx_hw);
	int (*disable)(struct rwnx_hw *rwnx_hw);
	void (*deinit)(struct rwnx_plat *rwnx_plat);
	u8 __iomem *(*get_address)(struct rwnx_plat *rwnx_plat, int addr_name,
				   unsigned int offset);
	void (*ack_irq)(struct rwnx_plat *rwnx_plat);
	int (*get_config_reg)(struct rwnx_plat *rwnx_plat, const u32 **list);

	u8 priv[0] __aligned(sizeof(void *));
};

static inline void __iomem *RWNX_ADDR(void *plat, int base, int offset)
{
	struct rwnx_plat *p = (struct rwnx_plat *)plat;

	return p->get_address(p, base, offset);
}

static inline u32 RWNX_REG_READ(void *plat, int base, int offset)
{
	struct rwnx_plat *p = (struct rwnx_plat *)plat;

	return readl(p->get_address(p, base, offset));
}

static inline void RWNX_REG_WRITE(u32 val, void *plat, int base, int offset)
{
	struct rwnx_plat *p = (struct rwnx_plat *)plat;

	writel(val, p->get_address(p, base, offset));
}

extern struct rwnx_plat *g_rwnx_plat;

int rwnx_platform_init(struct rwnx_plat *rwnx_plat, void **platform_data);
void rwnx_platform_deinit(struct rwnx_hw *rwnx_hw);

int rwnx_platform_on(struct rwnx_hw *rwnx_hw, void *config);
void rwnx_platform_off(struct rwnx_hw *rwnx_hw, void **config);

int rwnx_platform_register_drv(void);
void rwnx_platform_unregister_drv(void);

void get_userconfig_txpwr_idx(struct txpwr_idx_conf *txpwr_idx);
void get_userconfig_txpwr_ofst(struct txpwr_ofst_conf *txpwr_ofst);
void get_userconfig_xtal_cap(struct xtal_cap_conf *xtal_cap);
s8 get_txpwr_max(s8 power);
void set_txpwr_loss_ofst(s8 value);

int get_ccode_region(char *ccode);

void rwnx_release_firmware_common(u32 **buffer);

int rwnx_plat_userconfig_upload(struct rwnx_hw *rwnx_hw,
				const char *filename);
void rwnx_plat_userconfig_parsing(struct rwnx_hw *rwnx_hw, char *buffer, int size);
void rwnx_plat_userconfig_parsing2(char *buffer, int size);
void rwnx_plat_userconfig_parsing3(char *buffer, int size);

void get_userconfig_txpwr_lvl_in_fdrv(struct txpwr_lvl_conf *txpwr_lvl);
void get_userconfig_txpwr_lvl_v2_in_fdrv(struct txpwr_lvl_conf_v2 *txpwr_lvl_v2);
void get_userconfig_txpwr_lvl_group_in_fdrv(struct txpwr_lvl_conf_v3 *txpwr_lvl_v3, int index_reg);
void get_userconfig_txpwr_lvl_v3_in_fdrv(struct txpwr_lvl_conf_v3 *txpwr_lvl_v3);
void get_userconfig_txpwr_lvl_adj_in_fdrv(struct txpwr_lvl_adj_conf *txpwr_lvl_adj);

void get_userconfig_txpwr_ofst_in_fdrv(struct txpwr_ofst_conf *txpwr_ofst);
void get_userconfig_txpwr_ofst2x_in_fdrv(struct txpwr_ofst2x_conf *txpwr_ofst2x);
void get_userconfig_txpwr_loss(struct txpwr_loss_conf *txpwr_loss);
#ifdef CONFIG_AIC8800_POWER_LIMIT
int is_all_space_or_tab(u8 *data, u8 size);
int is_comment_string(char *sz_str);
int is_enable_limit(char *sz_str);
int parse_qualified_string(char *in, u32 *start, char *out, char left_qualifier,
			   char right_qualifier);
int get_u1_byte_integer_from_string_in_decimal(char *str, u8 *p_int);
int get_s1_byte_integer_from_string_in_decimal(char *str, s8 *val);
void rwnx_plat_powerlimit_parsing(char *buffer, int size, char *cc);
s8 get_powerlimit_by_freq(u8 band, u16 freq, u8 *enable);
s8 get_powerlimit_by_chnum(u8 chnum, u8 *enable);
u16 phy_channel_to_freq(u8 band, int channel);
#endif

struct device *rwnx_platform_get_dev(struct rwnx_plat *rwnx_plat);

int rwnx_request_firmware_common(struct rwnx_hw *rwnx_hw, u32 **buffer,
				 const char *filename);
int rwnx_plat_bin_fw_upload_2(struct rwnx_hw *rwnx_hw, u32 fw_addr,
			      char *filename);
int rwnx_atoi(char *value);
void rwnx_plat_nvram_set_value(char *command, char *value);
void rwnx_plat_nvram_set_value_group(char *command, char *value);
void rwnx_plat_nvram_set_value_v3(char *command, char *value);

static inline unsigned int rwnx_platform_get_irq(struct rwnx_plat *rwnx_plat)
{
	return rwnx_plat->pci_dev->irq;
}

#endif /* _RWNX_PLATFORM_H_ */
