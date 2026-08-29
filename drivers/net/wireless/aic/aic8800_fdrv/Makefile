# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_AIC8800_WLAN_SUPPORT) += aic8800_fdrv.o

aic8800_fdrv-y := \
	rwnx_msg_tx.o \
	rwnx_msg_rx.o \
	rwnx_utils.o \
	rwnx_cmds.o \
	rwnx_irqs.o \
	rwnx_cfgfile.o \
	rwnx_strs.o \
	rwnx_rx.o \
	rwnx_tx.o \
	rwnx_txq.o \
	rwnx_main.o \
	rwnx_mod_params.o \
	rwnx_platform.o \
	aicwf_chip_8800d80.o \
	aicwf_chip_ops.o \
	rwnx_v7.o \
	ipc_host.o \
	aicwf_compat_8800d80.o \
	regdb.o \
	aic_priv_cmd.o \
	sdio_host.o \
	aicwf_txrxif.o \
	aicwf_sdio.o \
	aicwf_rx_prealloc.o

aic8800_fdrv-$(CONFIG_AIC8800_NONSTA_SUPPORT) += rwnx_mesh.o rwnx_tdls.o

aic8800_fdrv-$(CONFIG_AIC8800_GENL) += aicwf_genl.o

ccflags-y += -I$(srctree)
ccflags-y += -I$(src)
ccflags-y += -I$(src)/../aic8800_bsp
ccflags-y += -DAIC_TRACE_INCLUDE_PATH=$(src)
ccflags-y += -DAICWF_SDIO_SUPPORT
ccflags-y += -DCONFIG_SDIO_PWRCTRL
ccflags-y += -DCONFIG_RWNX_FULLMAC
ccflags-y += -DUSE_5G
ccflags-y += -DAICWF_RX_REORDER
ccflags-y += -DAICWF_ARP_OFFLOAD
ccflags-y += -DCONFIG_RX_NETIF_RECV_SKB
ccflags-y += -DCONFIG_AIC8800_RX_NETIF_RECV_SKB
