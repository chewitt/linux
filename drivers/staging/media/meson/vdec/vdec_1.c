// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * VDEC_1 is a video decoding block that allows decoding of
 * MPEG 1/2/4, H.263, H.264, MJPEG, VC1
 */

#include <linux/firmware.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include "vdec_1.h"
#include "vdec_helpers.h"
#include "dos_regs.h"

/* AO Registers */
#define AO_RTI_GEN_PWR_SLEEP0	0xe8
#define AO_RTI_GEN_PWR_ISO0	0xec
	#define GEN_PWR_VDEC_1 (BIT(3) | BIT(2))
	#define GEN_PWR_VDEC_1_SM1 (BIT(1))

#define MC_SIZE			(4096 * 4)

/*
 * DMC request port of this unit: ambus channel 5, "DOS VDEC interface"
 * (A311D datasheet, memory-interface chapter).  Confirmed on silicon:
 * the channel reads busy in 72 % of samples during an H.264 decode and
 * is completely idle otherwise, while the VDEC_HEVC channels (4 and 8)
 * stay idle throughout - and vice versa.
 *
 * Pre-G12A parts use bit 13 for the same port; only G12A+ is wired up
 * here because that is what has been validated.
 */
#define DMC_REQ_VDEC_1		BIT(21)

/*
 * Targeted VDEC_1 sub-engine reset set (vendor vdec_reset_core()):
 * vld(3), vld_part(4), vfifo(5), mc(7), dblk(8), pic_dc(9).  Notably
 * this does NOT include ddr(13) or mcpu/ccpu(11/12) - the blanket
 * DOS_SW_RESET0 that mainline fires does, which is exactly what makes
 * an unparked reset lethal.
 */
#define DOS_SW_RESET0_VDEC_1_QUIESCE					\
	(BIT(3) | BIT(4) | BIT(5) | BIT(7) | BIT(8) | BIT(9))

static bool vdec_1_is_g12(struct amvdec_core *core)
{
	return core->platform->revision == VDEC_REVISION_G12A ||
	       core->platform->revision == VDEC_REVISION_SM1;
}

/*
 * Stop the AMRISC processors and wait for their DMA engines to drain
 * (vendor vdec_stop_armrisc()).  Resetting or power-gating with these
 * in flight wedges the unit's DDR-controller port - fatally, beyond
 * even a watchdog reset.
 */
static void vdec_1_stop_armrisc(struct amvdec_core *core)
{
	int i;

	amvdec_write_dos(core, MPSR, 0);
	amvdec_write_dos(core, CPSR, 0);

	for (i = 0; i < 1000; i++) {
		if (!(amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	for (i = 0; i < 1000; i++) {
		if (!(amvdec_read_dos(core, LMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	if (i == 1000)
		dev_warn(core->dev, "VDEC_1 LMEM DMA did not drain\n");
}

/*
 * Scrub the VDEC_1 sub-engines: targeted reset plus the G12B DMC
 * pipeline pulse.  Touches DOS registers, so the power domain MUST be
 * fully up (memories powered, isolation removed).  Call only between
 * park and unpark.
 */
static void vdec_1_core_scrub(struct amvdec_core *core)
{
	amvdec_write_dos(core, DOS_SW_RESET0, DOS_SW_RESET0_VDEC_1_QUIESCE);
	udelay(10);
	amvdec_write_dos(core, DOS_SW_RESET0, 0);

	amvdec_dmc_pipeline_reset(core);
}

/*
 * Full bracket for use when the power domain is up.  Harmless on a
 * healthy, idle core.
 */
static void vdec_1_quiesce_reset(struct amvdec_core *core)
{
	if (!vdec_1_is_g12(core)) {
		vdec_1_core_scrub(core);
		return;
	}

	amvdec_dmc_park(core, DMC_REQ_VDEC_1);
	vdec_1_core_scrub(core);
	amvdec_dmc_unpark(core, DMC_REQ_VDEC_1);
}

static int
vdec_1_load_firmware(struct amvdec_session *sess, const char *fwname)
{
	const struct firmware *fw;
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev_dec;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	static void *mc_addr;
	static dma_addr_t mc_addr_map;
	int ret;
	u32 i = 1000;

	ret = request_firmware(&fw, fwname, dev);
	if (ret < 0)
		return -EINVAL;

	if (fw->size < MC_SIZE) {
		dev_err(dev, "Firmware size %zu is too small. Expected %u.\n",
			fw->size, MC_SIZE);
		ret = -EINVAL;
		goto release_firmware;
	}

	mc_addr = dma_alloc_coherent(core->dev, MC_SIZE,
				     &mc_addr_map, GFP_KERNEL);
	if (!mc_addr) {
		ret = -ENOMEM;
		goto release_firmware;
	}

	memcpy(mc_addr, fw->data, MC_SIZE);

	amvdec_write_dos(core, MPSR, 0);
	amvdec_write_dos(core, CPSR, 0);

	amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));

	amvdec_write_dos(core, IMEM_DMA_ADR, mc_addr_map);
	amvdec_write_dos(core, IMEM_DMA_COUNT, MC_SIZE / 4);
	amvdec_write_dos(core, IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	/*
	 * The vendor waits up to a full second with real delays; a bare
	 * iteration spin can declare success while the IMEM DMA is still
	 * running, and starting the processor on half-loaded microcode
	 * means arbitrary DMA from a bus master - a fatal DMC wedge on a
	 * session restart.
	 */
	i = 100000;
	while (i && (amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000)) {
		udelay(10);
		i--;
	}

	if (i == 0) {
		dev_err(dev, "Firmware load fail (DMA hang?)\n");
		ret = -EINVAL;
		goto free_mc;
	}

	if (codec_ops->load_extended_firmware)
		ret = codec_ops->load_extended_firmware(sess,
							fw->data + MC_SIZE,
							fw->size - MC_SIZE);

free_mc:
	dma_free_coherent(core->dev, MC_SIZE, mc_addr, mc_addr_map);
release_firmware:
	release_firmware(fw);
	return ret;
}

static int vdec_1_stbuf_power_up(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_WRAP_COUNT, 0);
	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));

	amvdec_write_dos(core, VLD_MEM_VIFIFO_START_PTR, sess->vififo_paddr);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_CURR_PTR, sess->vififo_paddr);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_END_PTR,
			 sess->vififo_paddr + sess->vififo_size - 8);

	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 1);
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 1);

	amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_MANUAL);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_WP, sess->vififo_paddr);

	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);

	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
			      (0x11 << MEM_FIFO_CNT_BIT) | MEM_FILL_ON_LEVEL |
			      MEM_CTRL_FILL_EN | MEM_CTRL_EMPTY_EN);

	return 0;
}

static void vdec_1_conf_esparser(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	/* VDEC_1 specific ESPARSER stuff */
	amvdec_write_dos(core, DOS_GEN_CTRL0, 0);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
}

static u32 vdec_1_vififo_level(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	return amvdec_read_dos(core, VLD_MEM_VIFIFO_LEVEL);
}

static void __vdec_1_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	amvdec_write_dos(core, ASSIST_MBOX1_MASK, 0);

	/* Stop the firmware processors and drain their DMA first */
	vdec_1_stop_armrisc(core);

	amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
	amvdec_write_dos(core, DOS_SW_RESET0, 0);
	amvdec_read_dos(core, DOS_SW_RESET0);

	/*
	 * A session can end with a sub-engine stuck mid-transaction.
	 * Power-gating in that state wedges the DDR-controller port and
	 * locks the SoC solid, beyond even a hardware watchdog reset, so
	 * park the port and scrub the core before the domain goes down.
	 */
	vdec_1_quiesce_reset(core);

	/* enable vdec1 isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_SM1, GEN_PWR_VDEC_1_SM1);
	else
		regmap_write(core->regmap_ao, AO_RTI_GEN_PWR_ISO0, 0xc0);
	/* power off vdec1 memories */
	amvdec_write_dos(core, DOS_MEM_PD_VDEC, 0xffffffff);
	/* power off vdec1 */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1_SM1, GEN_PWR_VDEC_1_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1, GEN_PWR_VDEC_1);

	if (sess->priv)
		codec_ops->stop(sess);
}

static int vdec_1_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	__vdec_1_stop(sess);

	clk_disable_unprepare(core->vdec_1_clk);

	return 0;
}

static int vdec_1_start(struct amvdec_session *sess)
{
	int ret;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	/* Configure the vdec clk to the maximum available */
	clk_set_rate(core->vdec_1_clk, 666666666);
	ret = clk_prepare_enable(core->vdec_1_clk);
	if (ret)
		return ret;

	/* Enable power for VDEC_1 */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1, 0);
	usleep_range(10, 20);

	/*
	 * The previous session may have died with sub-engines stuck
	 * mid-transaction.  NEVER pulse a reset with the DMC request port
	 * live - park it first (that register is outside this power
	 * domain, so it is safe while the domain is still down), power
	 * the domain up, THEN scrub the core and reconnect.  DOS
	 * registers must not be touched before the memories are powered
	 * and isolation is removed.
	 */
	if (vdec_1_is_g12(core))
		amvdec_dmc_park(core, DMC_REQ_VDEC_1);

	/* Reset VDEC1 */
	amvdec_write_dos(core, DOS_SW_RESET0, 0xfffffffc);
	amvdec_write_dos(core, DOS_SW_RESET0, 0x00000000);

	amvdec_write_dos(core, DOS_GCLK_EN0, 0x3ff);

	/* enable VDEC Memories */
	amvdec_write_dos(core, DOS_MEM_PD_VDEC, 0);
	/* Remove VDEC1 Isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_SM1, 0);
	else
		regmap_write(core->regmap_ao, AO_RTI_GEN_PWR_ISO0, 0);

	/* Domain fully up: scrub the sub-engines, then reconnect DDR */
	vdec_1_core_scrub(core);
	if (vdec_1_is_g12(core))
		amvdec_dmc_unpark(core, DMC_REQ_VDEC_1);

	/* Reset DOS top registers */
	amvdec_write_dos(core, DOS_VDEC_MCRCC_STALL_CTRL, 0);

	amvdec_write_dos(core, GCLK_EN, 0x3ff);
	amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));

	vdec_1_stbuf_power_up(sess);

	ret = vdec_1_load_firmware(sess, sess->fmt_out->firmware_path);
	if (ret)
		goto stop;

	ret = codec_ops->start(sess);
	if (ret)
		goto stop;

	/* Enable IRQ */
	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	amvdec_write_dos(core, ASSIST_MBOX1_MASK, 1);

	/* Enable 2-plane output */
	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M)
		amvdec_write_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));
	else
		amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));

	/* Enable firmware processor */
	amvdec_write_dos(core, MPSR, 1);
	/* Let the firmware settle */
	usleep_range(10, 20);

	return 0;

stop:
	__vdec_1_stop(sess);
	clk_disable_unprepare(core->vdec_1_clk);
	return ret;
}

struct amvdec_ops vdec_1_ops = {
	.start = vdec_1_start,
	.stop = vdec_1_stop,
	.conf_esparser = vdec_1_conf_esparser,
	.vififo_level = vdec_1_vififo_level,
};
