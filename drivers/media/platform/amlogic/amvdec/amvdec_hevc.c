// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * VDEC_HEVC hardware control for HEVC and VP9.
 */

#include <linux/firmware.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "amvdec_1.h"
#include "amvdec_helpers.h"
#include "amvdec_hevc.h"
#include "esparser.h"
#include "hevc_regs.h"
#include "dos_regs.h"

/* AO Registers */
#define AO_RTI_GEN_PWR_SLEEP0	0xe8
#define AO_RTI_GEN_PWR_ISO0	0xec
	#define GEN_PWR_VDEC_HEVC (BIT(7) | BIT(6))
	#define GEN_PWR_VDEC_HEVC_SM1 (BIT(2))

#define MC_SIZE	AMVDEC_FW_SIZE

/* HEVC uses DMC axibus channels 4 (front) and 8 (back) on G12A, G12B and SM1. */
#define DMC_REQ_HEVC		(BIT(4) | BIT(8))

#define G12B_RESET7_LEVEL_ADDR	0xffd0109c	/* level register (low = reset) */
	#define RESET7_LEVEL_HEVC_DMC	(BIT(14) | BIT(13))

/* HEVC stop/reset registers, in byte offsets. */
#define HEVC_SAO_MMU_RESET_CTRL	0x9904
#define HEVC_LMEM_DMA_CTRL	0xcd40
#define HEVC_WRRSP_LMEM		0xcd4c

/*
 * Reset the HEVC parser, processors, DMA and reconstruction sub-engines
 * with the DMC request ports parked.
 */
#define DOS_SW_RESET3_HEVC_QUIESCE					\
	(BIT(3) | BIT(4) | BIT(8) | BIT(10) | BIT(11) | BIT(12) |	\
	 BIT(13) | BIT(14) | BIT(15) | BIT(17) | BIT(18) | BIT(19) |	\
	 BIT(24) | BIT(26))

static int amvdec_hevc_load_firmware(struct amvdec_session *sess,
				     const char *fwname)
{
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev_dec;
	const struct firmware *fw;
	int ret;
	u32 i = 100;

	ret = request_firmware(&fw, fwname, dev);
	if (ret < 0)  {
		dev_err(dev, "Unable to request firmware %s\n", fwname);
		return ret;
	}

	if (fw->size < MC_SIZE) {
		dev_err(dev, "Firmware size %zu is too small. Expected %u.\n",
			fw->size, MC_SIZE);
		ret = -EINVAL;
		goto release_firmware;
	}

	memcpy(core->fw_vaddr, fw->data, MC_SIZE);

	meson_amvdec_write_dos(core, HEVC_MPSR, 0);
	meson_amvdec_write_dos(core, HEVC_CPSR, 0);

	meson_amvdec_write_dos(core, HEVC_IMEM_DMA_ADR, core->fw_paddr);
	meson_amvdec_write_dos(core, HEVC_IMEM_DMA_COUNT, MC_SIZE / 4);
	meson_amvdec_write_dos(core, HEVC_IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	/* Wait up to one second for IMEM DMA before starting the processor. */
	i = 100000;
	while (i && (readl(core->dos_base + HEVC_IMEM_DMA_CTRL) & 0x8000)) {
		udelay(10);
		i--;
	}

	if (i == 0) {
		dev_err(dev, "Firmware load fail (DMA hang?)\n");
		ret = -ENODEV;
	}

release_firmware:
	release_firmware(fw);
	return ret;
}

static void amvdec_hevc_stbuf_init(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	meson_amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 meson_amvdec_read_dos(core, HEVC_STREAM_CONTROL) & ~1);
	meson_amvdec_write_dos(core, HEVC_STREAM_START_ADDR, sess->vififo_paddr);
	meson_amvdec_write_dos(core, HEVC_STREAM_END_ADDR,
			 sess->vififo_paddr + sess->vififo_size);
	meson_amvdec_write_dos(core, HEVC_STREAM_RD_PTR, sess->vififo_paddr);
	meson_amvdec_write_dos(core, HEVC_STREAM_WR_PTR, sess->vififo_paddr);
}

/* VDEC_HEVC specific ESPARSER configuration */
static void amvdec_hevc_conf_esparser(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	/* set vififo_vbuf_rp_sel=>vdec_hevc */
	meson_amvdec_write_dos(core, DOS_GEN_CTRL0, 3 << 1);
	meson_amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 meson_amvdec_read_dos(core, HEVC_STREAM_CONTROL) | BIT(3));
	meson_amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 meson_amvdec_read_dos(core, HEVC_STREAM_CONTROL) | 1);
	meson_amvdec_write_dos(core, HEVC_STREAM_FIFO_CTL,
			 meson_amvdec_read_dos(core, HEVC_STREAM_FIFO_CTL) | BIT(29));
}

static u32 amvdec_hevc_vififo_level(struct amvdec_session *sess)
{
	return readl_relaxed(sess->core->dos_base + HEVC_STREAM_LEVEL);
}

/* Stop AMRISC and drain DMA and write responses before reset or power-down. */
static int amvdec_hevc_stop_armrisc(struct amvdec_core *core)
{
	int ret = 0;
	int i;

	meson_amvdec_write_dos(core, HEVC_MPSR, 0);
	meson_amvdec_write_dos(core, HEVC_CPSR, 0);

	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, HEVC_IMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	if (i == 1000) {
		dev_warn(core->dev, "HEVC IMEM DMA did not drain\n");
		ret = -ETIMEDOUT;
	}
	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, HEVC_LMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	if (i == 1000) {
		dev_warn(core->dev, "HEVC LMEM DMA did not drain\n");
		ret = -ETIMEDOUT;
	}
	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, HEVC_WRRSP_LMEM) & 0xfff))
			break;
		udelay(10);
	}
	if (i == 1000) {
		dev_warn(core->dev, "HEVC write responses did not drain\n");
		ret = -ETIMEDOUT;
	}

	return ret;
}

static bool amvdec_hevc_armrisc_dma_busy(struct amvdec_core *core)
{
	return (meson_amvdec_read_dos(core, HEVC_IMEM_DMA_CTRL) & 0x8000) ||
	       (meson_amvdec_read_dos(core, HEVC_LMEM_DMA_CTRL) & 0x8000) ||
	       (meson_amvdec_read_dos(core, HEVC_WRRSP_LMEM) & 0xfff);
}

static void amvdec_hevc_stop_stream_fetch(struct amvdec_core *core)
{
	meson_amvdec_write_dos(core, HEVC_STREAM_CONTROL, 0);
}

/*
 * Park HEVC DMC request ports and wait for idle. These registers remain
 * accessible while the decoder power domain is off.
 */
static int amvdec_hevc_dmc_park(struct amvdec_core *core)
{
	if (!amvdec_is_g12(core))
		return 0;

	return meson_amvdec_dmc_park(core, DMC_REQ_HEVC);
}

static void amvdec_hevc_dmc_unpark(struct amvdec_core *core)
{
	if (!amvdec_is_g12(core))
		return;

	meson_amvdec_dmc_unpark(core, DMC_REQ_HEVC);
}

/*
 * Hold SAO/MMU reset, reset the HEVC sub-engines, drain writes, then reset
 * the G12B DMC pipelines. Call with the domain powered and DMC ports parked.
 */
static int amvdec_hevc_core_scrub(struct amvdec_core *core)
{
	static void __iomem *reset7_lvl;
	u32 val;
	int i;

	if (!amvdec_is_g12(core))
		return 0;

	if (!reset7_lvl)
		reset7_lvl = ioremap(G12B_RESET7_LEVEL_ADDR, 4);
	if (!reset7_lvl)
		return -ENOMEM;

	/* Hold the SAO/MMU in reset across the sub-engine reset */
	val = meson_amvdec_read_dos(core, HEVC_SAO_MMU_RESET_CTRL);
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_RESET_CTRL, val | 1);

	meson_amvdec_write_dos(core, DOS_SW_RESET3, DOS_SW_RESET3_HEVC_QUIESCE);
	udelay(10);
	meson_amvdec_write_dos(core, DOS_SW_RESET3, 0);

	/* Drain outstanding write responses before releasing anything */
	for (i = 0; i < 1000; i++) {
		if (!(meson_amvdec_read_dos(core, HEVC_WRRSP_LMEM) & 0xfff))
			break;
		udelay(10);
	}
	if (i == 1000) {
		dev_warn(core->dev,
			 "HEVC write responses did not drain after reset\n");
		return -ETIMEDOUT;
	}

	val = meson_amvdec_read_dos(core, HEVC_SAO_MMU_RESET_CTRL);
	meson_amvdec_write_dos(core, HEVC_SAO_MMU_RESET_CTRL, val & ~1);

	/*
	 * G12B: the DMC-side decode pipelines latch state.  Toggle the
	 * LEVEL reset (low = asserted), then pulse the pipeline reset.
	 */
	val = readl(reset7_lvl);
	writel(val & ~RESET7_LEVEL_HEVC_DMC, reset7_lvl);
	udelay(10);
	writel(val | RESET7_LEVEL_HEVC_DMC, reset7_lvl);
	meson_amvdec_dmc_pipeline_reset(core);

	return 0;
}

/*
 * Quiesce the powered core. Keep DMC ports parked after any earlier stop
 * failure so the caller can retain DMA allocations safely.
 */
static int amvdec_hevc_quiesce_reset_locked(struct amvdec_core *core,
					    bool unpark_on_success)
{
	int ret;

	amvdec_hevc_stop_stream_fetch(core);

	ret = amvdec_hevc_dmc_park(core);
	if (ret)
		return ret;

	ret = amvdec_hevc_core_scrub(core);
	if (ret)
		return ret;

	if (unpark_on_success)
		amvdec_hevc_dmc_unpark(core);

	return 0;
}

static int __vdec_hevc_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	int ret, ret2;

	/* Disable interrupt */
	meson_amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 0);
	/* Stop the firmware processors, drain their DMA + writes */
	ret = amvdec_hevc_stop_armrisc(core);

	/*
	 * Quiesce DMA before releasing codec buffers. On timeout, keep the DMC
	 * ports parked and retain allocations the hardware may still address.
	 */
	ret2 = amvdec_hevc_quiesce_reset_locked(core, !ret);
	if (ret && !ret2) {
		/*
		 * A processor halt can leave DMA busy until the core reset. Accept the
		 * stop only after parked DMC ports, completed resets and idle DMA status.
		 */
		if (!amvdec_hevc_armrisc_dma_busy(core)) {
			dev_dbg(core->dev,
				"HEVC AMRISC DMA flags cleared by the core reset; stop completed\n");
			amvdec_hevc_dmc_unpark(core);
			ret = 0;
		}
	}
	if (ret2)
		ret = ret ?: ret2;

	if (ret)
		return ret;

	if (sess->priv)
		codec_ops->stop(sess);

	/* Enable VDEC_HEVC Isolation */
	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_HEVC_SM1,
				   GEN_PWR_VDEC_HEVC_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   0xc00, 0xc00);

	/* VDEC_HEVC Memories */
	meson_amvdec_write_dos(core, DOS_MEM_PD_HEVC, 0xffffffffUL);

	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC_SM1,
				   GEN_PWR_VDEC_HEVC_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC, GEN_PWR_VDEC_HEVC);

	return 0;
}

static int amvdec_hevc_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	int ret;

	ret = __vdec_hevc_stop(sess);
	if (ret)
		return ret;

	clk_disable_unprepare(core->amvdec_hevc_clk);
	if (core->platform->revision == AMVDEC_REVISION_G12A ||
	    core->platform->revision == AMVDEC_REVISION_SM1)
		clk_disable_unprepare(core->amvdec_hevcf_clk);

	return 0;
}

static int __vdec_hevc_start(struct amvdec_session *sess)
{
	int ret;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	clk_set_rate(core->amvdec_hevc_clk, 666666666);
	ret = clk_prepare_enable(core->amvdec_hevc_clk);
	if (ret) {
		if (core->platform->revision == AMVDEC_REVISION_G12A ||
		    core->platform->revision == AMVDEC_REVISION_SM1)
			clk_disable_unprepare(core->amvdec_hevcf_clk);
		return ret;
	}

	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC, 0);
	usleep_range(10, 20);

	/*
	 * Park the DMC ports before powering up. Access HEVC registers only after
	 * the memories are powered and isolation is removed, then reset the core.
	 */
	ret = amvdec_hevc_dmc_park(core);
	if (ret)
		goto stop;

	/* Reset VDEC_HEVC*/
	meson_amvdec_write_dos(core, DOS_SW_RESET3, 0xffffffff);
	udelay(10);
	meson_amvdec_write_dos(core, DOS_SW_RESET3, 0x00000000);

	meson_amvdec_write_dos(core, DOS_GCLK_EN3, 0xffffffff);

	/* VDEC_HEVC Memories */
	meson_amvdec_write_dos(core, DOS_MEM_PD_HEVC, 0x00000000);

	/* Remove VDEC_HEVC Isolation */
	if (core->platform->revision == AMVDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_HEVC_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   0xc00, 0);

	meson_amvdec_write_dos(core, DOS_SW_RESET3, 0xffffffff);
	udelay(10);
	meson_amvdec_write_dos(core, DOS_SW_RESET3, 0x00000000);

	/*
	 * Domain fully up: scrub the sub-engines, then reconnect DDR.  A scrub
	 * that times out leaves the ports parked and the processor halted.
	 */
	ret = amvdec_hevc_core_scrub(core);
	if (ret)
		goto stop;
	amvdec_hevc_dmc_unpark(core);

	amvdec_hevc_stbuf_init(sess);

	ret = amvdec_hevc_load_firmware(sess, sess->fmt_out->firmware_path);
	if (ret)
		goto stop;

	ret = codec_ops->start(sess);
	if (ret)
		goto stop;

	meson_amvdec_write_dos(core, DOS_SW_RESET3, BIT(12) | BIT(11));
	meson_amvdec_write_dos(core, DOS_SW_RESET3, 0);
	meson_amvdec_read_dos(core, DOS_SW_RESET3);

	meson_amvdec_write_dos(core, HEVC_MPSR, 1);
	/* Let the firmware settle */
	usleep_range(10, 20);

	return 0;

stop:
	if (!__vdec_hevc_stop(sess))
		clk_disable_unprepare(core->amvdec_hevc_clk);
	return ret;
}

static int amvdec_hevc_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	int ret;

	if (core->platform->revision == AMVDEC_REVISION_G12A ||
	    core->platform->revision == AMVDEC_REVISION_SM1) {
		clk_set_rate(core->amvdec_hevcf_clk, 666666666);
		ret = clk_prepare_enable(core->amvdec_hevcf_clk);
		if (ret)
			return ret;

		ret = __vdec_hevc_start(sess);
		if (ret)
			clk_disable_unprepare(core->amvdec_hevcf_clk);
		return ret;
	}

	return __vdec_hevc_start(sess);
}

struct amvdec_ops meson_amvdec_hevc_ops = {
	.start = amvdec_hevc_start,
	.stop = amvdec_hevc_stop,
	.conf_esparser = amvdec_hevc_conf_esparser,
	.vififo_level = amvdec_hevc_vififo_level,
};
