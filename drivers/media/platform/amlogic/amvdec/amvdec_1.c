// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * VDEC_1 hardware control for H.264 and MPEG-2.
 */

#include <linux/firmware.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include "amvdec_1.h"
#include "amvdec_helpers.h"
#include "dos_regs.h"

/* AO Registers */
#define AO_RTI_GEN_PWR_SLEEP0	0xe8
#define AO_RTI_GEN_PWR_ISO0	0xec
	#define GEN_PWR_VDEC_1 (BIT(3) | BIT(2))
	#define GEN_PWR_VDEC_1_ISO (BIT(7) | BIT(6))
	#define GEN_PWR_VDEC_1_SM1 (BIT(1))

#define MC_SIZE			AMVDEC_FW_SIZE

/* G12A, G12B and SM1 use DMC ambus channel 5 for VDEC_1. */
#define DMC_REQ_VDEC_1		BIT(21)

/* Reset VLD, VLD_PART, VFIFO, MC, DBLK and PIC_DC with the DMC port parked. */
#define DOS_SW_RESET0_VDEC_1_QUIESCE					\
	(BIT(3) | BIT(4) | BIT(5) | BIT(7) | BIT(8) | BIT(9))

/* Stop AMRISC and drain its DMA engines before reset or power-down. */
static void amvdec_1_stop_armrisc(struct amvdec_core *core)
{
	int i;

	meson_amvdec_write_dos(core, MPSR, 0);
	meson_amvdec_write_dos(core, CPSR, 0);

	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, LMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	if (i == 1000)
		dev_warn(core->dev, "VDEC_1 LMEM DMA did not drain\n");
}

/*
 * Reset VDEC_1 sub-engines and pulse the G12B DMC pipeline reset.
 * Call with the domain powered and the DMC port parked.
 */
static void amvdec_1_core_scrub(struct amvdec_core *core)
{
	meson_amvdec_write_dos(core, DOS_SW_RESET0, DOS_SW_RESET0_VDEC_1_QUIESCE);
	udelay(10);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);

	if (amvdec_is_g12(core))
		meson_amvdec_dmc_pipeline_reset(core);
}

/* Quiesce the powered VDEC_1 core. */
static void amvdec_1_quiesce_reset(struct amvdec_core *core)
{
	if (!amvdec_is_g12(core)) {
		amvdec_1_core_scrub(core);
		return;
	}

	meson_amvdec_dmc_park(core, DMC_REQ_VDEC_1);
	amvdec_1_core_scrub(core);
	meson_amvdec_dmc_unpark(core, DMC_REQ_VDEC_1);
}

static int
amvdec_1_load_firmware(struct amvdec_session *sess, const char *fwname)
{
	const struct firmware *fw;
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev_dec;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	int ret;
	u32 i = 1000;

	ret = request_firmware(&fw, fwname, dev);
	if (ret < 0)
		return -EINVAL;

	/* Pad 8 KiB firmware images to the 16 KiB IMEM transfer size. */
	if (fw->size < MC_SIZE && fw->size != MC_SIZE / 2) {
		dev_err(dev, "Firmware %s size %zu is too small. Expected %u.\n",
			fwname, fw->size, MC_SIZE);
		ret = -EINVAL;
		goto release_firmware;
	}

	memset(core->fw_vaddr, 0, MC_SIZE);
	memcpy(core->fw_vaddr, fw->data, min_t(size_t, fw->size, MC_SIZE));

	meson_amvdec_write_dos(core, MPSR, 0);
	meson_amvdec_write_dos(core, CPSR, 0);

	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));

	meson_amvdec_write_dos(core, IMEM_DMA_ADR, core->fw_paddr);
	meson_amvdec_write_dos(core, IMEM_DMA_COUNT, MC_SIZE / 4);
	meson_amvdec_write_dos(core, IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	/* Wait up to one second for IMEM DMA before starting the processor. */
	i = 100000;
	while (i && (meson_amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000)) {
		udelay(10);
		i--;
	}

	if (i == 0) {
		dev_err(dev, "Firmware load fail (DMA hang?)\n");
		ret = -EINVAL;
		goto release_firmware;
	}

	if (codec_ops->load_firmware)
		ret = codec_ops->load_firmware(sess, fw->data, fw->size);
	else if (codec_ops->load_extended_firmware)
		ret = codec_ops->load_extended_firmware(sess,
						fw->data + MC_SIZE,
						fw->size - MC_SIZE);

release_firmware:
	release_firmware(fw);
	return ret;
}

static int amvdec_1_stbuf_power_up(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	if (!sess->vififo_vaddr)
		return 0;

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_WRAP_COUNT, 0);
	meson_amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_START_PTR, sess->vififo_paddr);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_CURR_PTR, sess->vififo_paddr);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_END_PTR,
			 sess->vififo_paddr + sess->vififo_size - 8);

	meson_amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 1);
	meson_amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 1);

	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_MANUAL);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_WP, sess->vififo_paddr);

	meson_amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
	meson_amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);

	meson_amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
			      (0x11 << MEM_FIFO_CNT_BIT) | MEM_FILL_ON_LEVEL |
			      MEM_CTRL_FILL_EN | MEM_CTRL_EMPTY_EN);

	return 0;
}

static void amvdec_1_conf_esparser(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	/* VDEC_1 specific ESPARSER stuff */
	meson_amvdec_write_dos(core, DOS_GEN_CTRL0, 0);
	meson_amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
	meson_amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
}

static u32 amvdec_1_vififo_level(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	return meson_amvdec_read_dos(core, VLD_MEM_VIFIFO_LEVEL);
}

static void __vdec_1_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	meson_amvdec_write_dos(core, ASSIST_MBOX1_MASK, 0);

	if (codec_ops->pre_stop)
		codec_ops->pre_stop(sess);

	/* Stop the firmware processors and drain their DMA first */
	amvdec_1_stop_armrisc(core);

	meson_amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET0);

	/*
	 * Park the DMC port and reset sub-engines before power-down to clear
	 * outstanding transactions.
	 */
	amvdec_1_quiesce_reset(core);

	/* enable vdec1 isolation */
	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_SM1, GEN_PWR_VDEC_1_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_ISO, GEN_PWR_VDEC_1_ISO);
	/* power off vdec1 memories */
	meson_amvdec_write_dos(core, DOS_MEM_PD_VDEC, 0xffffffff);
	/* power off vdec1 */
	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1_SM1, GEN_PWR_VDEC_1_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1, GEN_PWR_VDEC_1);

	if (sess->priv)
		codec_ops->stop(sess);
}

static int amvdec_1_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	__vdec_1_stop(sess);

	clk_disable_unprepare(core->amvdec_1_clk);

	return 0;
}

static int amvdec_1_start(struct amvdec_session *sess)
{
	int ret;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	/* Configure the vdec clk to the maximum available */
	clk_set_rate(core->amvdec_1_clk, 666666666);
	ret = clk_prepare_enable(core->amvdec_1_clk);
	if (ret)
		return ret;

	/* Enable power for VDEC_1 */
	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1, 0);
	usleep_range(10, 20);

	/*
	 * Park the DMC port before powering up. Access DOS registers only after
	 * the memories are powered and isolation is removed, then reset the core.
	 */
	if (amvdec_is_g12(core))
		meson_amvdec_dmc_park(core, DMC_REQ_VDEC_1);

	/* Reset VDEC1 */
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0xfffffffc);
	meson_amvdec_write_dos(core, DOS_SW_RESET0, 0x00000000);

	meson_amvdec_write_dos(core, DOS_GCLK_EN0, 0x3ff);

	/* enable VDEC Memories */
	meson_amvdec_write_dos(core, DOS_MEM_PD_VDEC, 0);
	/* Remove VDEC1 Isolation */
	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_ISO, 0);

	/* Domain fully up: scrub the sub-engines, then reconnect DDR */
	amvdec_1_core_scrub(core);
	if (amvdec_is_g12(core))
		meson_amvdec_dmc_unpark(core, DMC_REQ_VDEC_1);

	/* Reset DOS top registers */
	meson_amvdec_write_dos(core, DOS_VDEC_MCRCC_STALL_CTRL, 0);

	meson_amvdec_write_dos(core, GCLK_EN, 0x3ff);
	meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));

	amvdec_1_stbuf_power_up(sess);

	ret = amvdec_1_load_firmware(sess, sess->fmt_out->firmware_path);
	if (ret)
		goto stop;

	ret = codec_ops->start(sess);
	if (ret)
		goto stop;

	/* Enable IRQ */
	meson_amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	meson_amvdec_write_dos(core, ASSIST_MBOX1_MASK, 1);

	/* Enable 2-plane output */
	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M)
		meson_amvdec_write_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));
	else
		meson_amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));

	/*
	 * Direct-input codecs release AMRISC only after supplying the first input
	 * and decoder context.
	 */
	if (sess->fmt_out->pixfmt != V4L2_PIX_FMT_MPEG2_SLICE &&
	    !codec_ops->direct_input) {
		meson_amvdec_write_dos(core, MPSR, 1);
		usleep_range(10, 20);
	}

	if (codec_ops->post_start)
		codec_ops->post_start(sess);

	return 0;

stop:
	__vdec_1_stop(sess);
	clk_disable_unprepare(core->amvdec_1_clk);
	return ret;
}

struct amvdec_ops meson_amvdec_1_ops = {
	.start = amvdec_1_start,
	.stop = amvdec_1_stop,
	.conf_esparser = amvdec_1_conf_esparser,
	.vififo_level = amvdec_1_vififo_level,
};
