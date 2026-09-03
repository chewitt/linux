// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * VDEC_HEVC is a video decoding block that allows decoding of
 * HEVC, VP9
 */

#include <linux/firmware.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "vdec_1.h"
#include "vdec_helpers.h"
#include "vdec_hevc.h"
#include "hevc_regs.h"
#include "dos_regs.h"

/* AO Registers */
#define AO_RTI_GEN_PWR_SLEEP0	0xe8
#define AO_RTI_GEN_PWR_ISO0	0xec
	#define GEN_PWR_VDEC_HEVC (BIT(7) | BIT(6))
	#define GEN_PWR_VDEC_HEVC_SM1 (BIT(2))

#define MC_SIZE	(4096 * 4)

/*
 * DMC glue for safely stopping a wedged core.  The DMC is documented by
 * WORD offset: byte = base + offset*4, so CHAN_STS (0x36) is at 0xd8.
 */
#define G12A_DMC_BASE		0xff638000
#define G12A_DMC_SIZE		0x100
	#define DMC_REQ_CTRL	0x00
	#define DMC_CHAN_STS	0xd8
	#define DMC_REQ_HEVC	(BIT(4) | BIT(8))  /* hevc + hevcb ports */
#define G12A_RESET7_ADDR	0xffd01020	/* pulse register */
	#define RESET7_DMC_PIPEL	GENMASK(15, 11)
#define G12B_RESET7_LEVEL_ADDR	0xffd0109c	/* level register (low = reset) */
	#define RESET7_LEVEL_HEVC_DMC	(BIT(14) | BIT(13))

/* Registers the vendor stop/reset discipline needs (dos byte offsets) */
#define HEVC_SAO_MMU_RESET_CTRL	0x9904
#define HEVC_LMEM_DMA_CTRL	0xcd40
#define HEVC_WRRSP_LMEM		0xcd4c

/*
 * Targeted HEVC sub-engine reset set used by the vendor's
 * hevc_reset_core(): parser(3), parser_state(4), dblk(8),
 * wrrsp-lmem(10), mcpu(11), ccpu(12), ddr(13), iqit(14), ipp(15),
 * qdct(17), mpred(18), sao(19), hevc_afifo(24), rst_mmu_n(26).
 */
#define DOS_SW_RESET3_HEVC_QUIESCE					\
	(BIT(3) | BIT(4) | BIT(8) | BIT(10) | BIT(11) | BIT(12) |	\
	 BIT(13) | BIT(14) | BIT(15) | BIT(17) | BIT(18) | BIT(19) |	\
	 BIT(24) | BIT(26))

static int vdec_hevc_load_firmware(struct amvdec_session *sess,
				   const char *fwname)
{
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev_dec;
	const struct firmware *fw;
	static void *mc_addr;
	static dma_addr_t mc_addr_map;
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

	mc_addr = dma_alloc_coherent(core->dev, MC_SIZE, &mc_addr_map,
				     GFP_KERNEL);
	if (!mc_addr) {
		ret = -ENOMEM;
		goto release_firmware;
	}

	memcpy(mc_addr, fw->data, MC_SIZE);

	amvdec_write_dos(core, HEVC_MPSR, 0);
	amvdec_write_dos(core, HEVC_CPSR, 0);

	amvdec_write_dos(core, HEVC_IMEM_DMA_ADR, mc_addr_map);
	amvdec_write_dos(core, HEVC_IMEM_DMA_COUNT, MC_SIZE / 4);
	amvdec_write_dos(core, HEVC_IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	/*
	 * Vendor waits up to a full second with real delays; the old
	 * bare 100-iteration spin could declare success while the IMEM
	 * DMA was still running - and starting the processor on
	 * half-loaded microcode means arbitrary DMA from a bus master
	 * (a fatal DMC wedge on a session restart).
	 */
	i = 100000;
	while (i && (readl(core->dos_base + HEVC_IMEM_DMA_CTRL) & 0x8000)) {
		udelay(10);
		i--;
	}

	if (i == 0) {
		dev_err(dev, "Firmware load fail (DMA hang?)\n");
		ret = -ENODEV;
	}

	dma_free_coherent(core->dev, MC_SIZE, mc_addr, mc_addr_map);
release_firmware:
	release_firmware(fw);
	return ret;
}

static void vdec_hevc_stbuf_init(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 amvdec_read_dos(core, HEVC_STREAM_CONTROL) & ~1);
	amvdec_write_dos(core, HEVC_STREAM_START_ADDR, sess->vififo_paddr);
	amvdec_write_dos(core, HEVC_STREAM_END_ADDR,
			 sess->vififo_paddr + sess->vififo_size);
	amvdec_write_dos(core, HEVC_STREAM_RD_PTR, sess->vififo_paddr);
	amvdec_write_dos(core, HEVC_STREAM_WR_PTR, sess->vififo_paddr);
}

/* VDEC_HEVC specific ESPARSER configuration */
static void vdec_hevc_conf_esparser(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	/* set vififo_vbuf_rp_sel=>vdec_hevc */
	amvdec_write_dos(core, DOS_GEN_CTRL0, 3 << 1);
	amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 amvdec_read_dos(core, HEVC_STREAM_CONTROL) | BIT(3));
	amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 amvdec_read_dos(core, HEVC_STREAM_CONTROL) | 1);
	amvdec_write_dos(core, HEVC_STREAM_FIFO_CTL,
			 amvdec_read_dos(core, HEVC_STREAM_FIFO_CTL) | BIT(29));
}

static u32 vdec_hevc_vififo_level(struct amvdec_session *sess)
{
	return readl_relaxed(sess->core->dos_base + HEVC_STREAM_LEVEL);
}

/*
 * A decode session can end with a hardware sub-engine stuck
 * mid-transaction (observed in the field: firmware frozen at
 * HEVC_NAL_UNIT_CODED_SLICE_SEGMENT on a hostile stream, MPSR still
 * running, stream FIFO full, no interrupt).  Power-gating the core in
 * that state - or powering it back up for the next session - wedges
 * its DDR-controller port and locks the SoC solid, beyond even a
 * hardware-watchdog reset.  Do what the vendor driver does on every
 * power transition: disconnect the HEVC request ports on the DMC,
 * wait for the idle ack, hard-reset every HEVC sub-engine (aborting
 * any stuck transaction), pulse the G12B DMC pipeline reset, then
 * reconnect.  Harmless on a healthy, idle core.
 */
/*
 * Stop the AMRISC processors and wait for their DMA engines and
 * outstanding write responses to drain (vendor amhevc_stop()).
 * Resetting or power-gating with these in flight wedges the DDR
 * controller port - fatally, beyond even a watchdog reset.
 */
static void vdec_hevc_stop_armrisc(struct amvdec_core *core)
{
	int i;

	amvdec_write_dos(core, HEVC_MPSR, 0);
	amvdec_write_dos(core, HEVC_CPSR, 0);

	for (i = 0; i < 1000; i++) {
		if (!(amvdec_read_dos(core, HEVC_IMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	for (i = 0; i < 1000; i++) {
		if (!(amvdec_read_dos(core, HEVC_LMEM_DMA_CTRL) & 0x8000))
			break;
		udelay(10);
	}
	for (i = 0; i < 1000; i++) {
		if (!(amvdec_read_dos(core, HEVC_WRRSP_LMEM) & 0xfff))
			break;
		udelay(10);
	}
	if (i == 1000)
		dev_warn(core->dev, "HEVC write responses did not drain\n");
}

/*
 * The vendor hevc_reset_core() bracket, in full.  A decode session
 * can die with a hardware sub-engine stuck mid-transaction (observed:
 * firmware frozen at HEVC_NAL_UNIT_CODED_SLICE_SEGMENT on a hostile
 * stream).  Any reset or power transition without this bracket can
 * wedge the SoC's DDR controller.  Sequence: stream fetch off ->
 * park the DMC request ports and wait for the idle ack -> hold the
 * SAO/MMU reset -> targeted sub-engine reset -> drain write
 * responses -> release SAO/MMU -> G12B DMC-pipeline level+pulse
 * resets -> reconnect.  Harmless on a healthy, idle core.
 */
static void __iomem *vdec_hevc_map(u64 addr, u32 size,
				   void __iomem **cache)
{
	if (!*cache)
		*cache = ioremap(addr, size);
	return *cache;
}

static bool vdec_hevc_is_g12(struct amvdec_core *core)
{
	return core->platform->revision == VDEC_REVISION_G12A ||
	       core->platform->revision == VDEC_REVISION_SM1;
}

/*
 * Park the HEVC request ports on the DMC and wait for the idle ack.
 * These registers live OUTSIDE the VDEC_HEVC power domain and are
 * safe to touch at any point of a power transition.
 */
static void vdec_hevc_dmc_park(struct amvdec_core *core)
{
	static void __iomem *dmc_base;
	u32 val;
	int i;

	if (!vdec_hevc_is_g12(core) ||
	    !vdec_hevc_map(G12A_DMC_BASE, G12A_DMC_SIZE, &dmc_base))
		return;

	val = readl(dmc_base + DMC_REQ_CTRL);
	writel(val & ~DMC_REQ_HEVC, dmc_base + DMC_REQ_CTRL);

	for (i = 0; i < 100; i++) {
		if ((readl(dmc_base + DMC_CHAN_STS) & DMC_REQ_HEVC) ==
		    DMC_REQ_HEVC)
			break;
		udelay(10);
	}
	if (i == 100)
		dev_warn(core->dev,
			 "HEVC DMC ports did not idle before reset\n");
}

static void vdec_hevc_dmc_unpark(struct amvdec_core *core)
{
	static void __iomem *dmc_base;
	u32 val;

	if (!vdec_hevc_is_g12(core) ||
	    !vdec_hevc_map(G12A_DMC_BASE, G12A_DMC_SIZE, &dmc_base))
		return;

	val = readl(dmc_base + DMC_REQ_CTRL);
	writel(val | DMC_REQ_HEVC, dmc_base + DMC_REQ_CTRL);
}

/*
 * Scrub the HEVC sub-engines: targeted reset, write-response drain,
 * and the G12B DMC-pipeline LEVEL+pulse resets.  Touches HEVC core
 * registers - the power domain MUST be fully up (memories powered,
 * isolation removed).  Call only between dmc_park/dmc_unpark.
 */
static void vdec_hevc_core_scrub(struct amvdec_core *core)
{
	static void __iomem *reset7;
	static void __iomem *reset7_lvl;
	u32 val;
	int i;

	if (!vdec_hevc_is_g12(core) ||
	    !vdec_hevc_map(G12A_RESET7_ADDR, 4, &reset7) ||
	    !vdec_hevc_map(G12B_RESET7_LEVEL_ADDR, 4, &reset7_lvl))
		return;

	/* Stop the stream fetch engine */
	amvdec_write_dos(core, HEVC_STREAM_CONTROL, 0);

	/* Hold the SAO/MMU in reset across the sub-engine reset */
	val = amvdec_read_dos(core, HEVC_SAO_MMU_RESET_CTRL);
	amvdec_write_dos(core, HEVC_SAO_MMU_RESET_CTRL, val | 1);

	amvdec_write_dos(core, DOS_SW_RESET3, DOS_SW_RESET3_HEVC_QUIESCE);
	udelay(10);
	amvdec_write_dos(core, DOS_SW_RESET3, 0);

	/* Drain outstanding write responses before releasing anything */
	for (i = 0; i < 1000; i++) {
		if (!(amvdec_read_dos(core, HEVC_WRRSP_LMEM) & 0xfff))
			break;
		udelay(10);
	}

	val = amvdec_read_dos(core, HEVC_SAO_MMU_RESET_CTRL);
	amvdec_write_dos(core, HEVC_SAO_MMU_RESET_CTRL, val & ~1);

	/*
	 * G12B: the DMC-side decode pipelines latch state.  Toggle the
	 * LEVEL reset (low = asserted), then pulse the pipeline reset.
	 */
	val = readl(reset7_lvl);
	writel(val & ~RESET7_LEVEL_HEVC_DMC, reset7_lvl);
	udelay(10);
	writel(val | RESET7_LEVEL_HEVC_DMC, reset7_lvl);
	writel(RESET7_DMC_PIPEL, reset7);
}

/* Full bracket for use when the power domain is up (session stop,
 * in-session stall recovery). */
void vdec_hevc_quiesce_reset(struct amvdec_core *core)
{
	vdec_hevc_dmc_park(core);
	vdec_hevc_core_scrub(core);
	vdec_hevc_dmc_unpark(core);
}

static void __vdec_hevc_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	/* Disable interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 0);
	/* Stop the firmware processors, drain their DMA + writes */
	vdec_hevc_stop_armrisc(core);

	if (sess->priv)
		codec_ops->stop(sess);

	vdec_hevc_quiesce_reset(core);

	/* Enable VDEC_HEVC Isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_HEVC_SM1,
				   GEN_PWR_VDEC_HEVC_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   0xc00, 0xc00);

	/* VDEC_HEVC Memories */
	amvdec_write_dos(core, DOS_MEM_PD_HEVC, 0xffffffffUL);

	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC_SM1,
				   GEN_PWR_VDEC_HEVC_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC, GEN_PWR_VDEC_HEVC);
}

static int vdec_hevc_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	__vdec_hevc_stop(sess);

	clk_disable_unprepare(core->vdec_hevc_clk);
	if (core->platform->revision == VDEC_REVISION_G12A ||
	    core->platform->revision == VDEC_REVISION_SM1)
		clk_disable_unprepare(core->vdec_hevcf_clk);

	return 0;
}

static int __vdec_hevc_start(struct amvdec_session *sess)
{
	int ret;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	clk_set_rate(core->vdec_hevc_clk, 666666666);
	ret = clk_prepare_enable(core->vdec_hevc_clk);
	if (ret) {
		if (core->platform->revision == VDEC_REVISION_G12A ||
		    core->platform->revision == VDEC_REVISION_SM1)
			clk_disable_unprepare(core->vdec_hevcf_clk);
		return ret;
	}

	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC, 0);
	usleep_range(10, 20);

	/*
	 * The previous session may have died with sub-engines stuck
	 * mid-transaction (hostile stream).  NEVER pulse a reset with
	 * the DMC request ports live - park them (registers outside
	 * this power domain, safe while it is still down), power the
	 * domain up, THEN scrub the core and reconnect.  HEVC core
	 * registers must not be touched before the memories are
	 * powered and isolation is removed.
	 */
	vdec_hevc_dmc_park(core);

	/* Reset VDEC_HEVC*/
	amvdec_write_dos(core, DOS_SW_RESET3, 0xffffffff);
	udelay(10);
	amvdec_write_dos(core, DOS_SW_RESET3, 0x00000000);

	amvdec_write_dos(core, DOS_GCLK_EN3, 0xffffffff);

	/* VDEC_HEVC Memories */
	amvdec_write_dos(core, DOS_MEM_PD_HEVC, 0x00000000);

	/* Remove VDEC_HEVC Isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_HEVC_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   0xc00, 0);

	amvdec_write_dos(core, DOS_SW_RESET3, 0xffffffff);
	udelay(10);
	amvdec_write_dos(core, DOS_SW_RESET3, 0x00000000);

	/* Domain fully up: scrub the sub-engines, then reconnect DDR */
	vdec_hevc_core_scrub(core);
	vdec_hevc_dmc_unpark(core);

	vdec_hevc_stbuf_init(sess);

	ret = vdec_hevc_load_firmware(sess, sess->fmt_out->firmware_path);
	if (ret)
		goto stop;

	ret = codec_ops->start(sess);
	if (ret)
		goto stop;

	amvdec_write_dos(core, DOS_SW_RESET3, BIT(12) | BIT(11));
	amvdec_write_dos(core, DOS_SW_RESET3, 0);
	amvdec_read_dos(core, DOS_SW_RESET3);

	amvdec_write_dos(core, HEVC_MPSR, 1);
	/* Let the firmware settle */
	usleep_range(10, 20);

	return 0;

stop:
	__vdec_hevc_stop(sess);
	clk_disable_unprepare(core->vdec_hevc_clk);
	return ret;
}

static int vdec_hevc_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	int ret;

	if (core->platform->revision == VDEC_REVISION_G12A ||
	    core->platform->revision == VDEC_REVISION_SM1) {
		clk_set_rate(core->vdec_hevcf_clk, 666666666);
		ret = clk_prepare_enable(core->vdec_hevcf_clk);
		if (ret)
			return ret;

		ret = __vdec_hevc_start(sess);
		if (ret)
			clk_disable_unprepare(core->vdec_hevcf_clk);
		return ret;
	}

	return __vdec_hevc_start(sess);
}

struct amvdec_ops vdec_hevc_ops = {
	.start = vdec_hevc_start,
	.stop = vdec_hevc_stop,
	.conf_esparser = vdec_hevc_conf_esparser,
	.vififo_level = vdec_hevc_vififo_level,
};
