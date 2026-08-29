# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_AIC8800_BSP_SUPPORT) += aic8800_bsp.o

aic8800_bsp-y := \
	aic_chip_8800d80.o \
	aic_chip_ops.o \
	aic8800d80_compat.o \
	aic_bsp_main.o \
	aic_bsp_driver.o \
	aicsdio.o \
	aicsdio_txrxif.o

ccflags-y += -I$(src)/../aic8800_fdrv
ccflags-y += -DAICWF_SDIO_SUPPORT
ccflags-y += -DCONFIG_SDIO_PWRCTRL
