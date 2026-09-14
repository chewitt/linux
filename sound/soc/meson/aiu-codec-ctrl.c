// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/meson-aiu.h>
#include "aiu.h"
#include "meson-codec-glue.h"

#define CTRL_CLK_SEL			GENMASK(1, 0)
#define CTRL_CLK_SEL_DISABLE		0x0
#define CTRL_CLK_SEL_PCM		0x1
#define CTRL_CLK_SEL_AIU		0x2
#define CTRL_DATA_SEL			GENMASK(5, 4)
#define CTRL_DATA_SEL_OUTPUT_ZERO	0x0
#define CTRL_DATA_SEL_PCM_DATA		0x1
#define CTRL_DATA_SEL_I2S_DATA		0x2

#define AIU_CLK_CTRL_MORE_HDMI_AMCLK	BIT(6)

struct aiu_hdmi_out {
	unsigned int data_sel;
	bool amclk;
};

static const struct aiu_hdmi_out aiu_hdmi_out_i2s = {
	.data_sel	= CTRL_DATA_SEL_I2S_DATA,
	.amclk		= true,
};

static const struct aiu_hdmi_out aiu_hdmi_out_spdif = {
	.data_sel	= CTRL_DATA_SEL_OUTPUT_ZERO,
	.amclk		= false,
};

static void aiu_codec_ctrl_select(struct snd_soc_component *component,
				  const struct aiu_hdmi_out *out)
{
	snd_soc_component_update_bits(component, AIU_HDMI_CLK_DATA_CTRL,
				      CTRL_CLK_SEL | CTRL_DATA_SEL,
				      FIELD_PREP(CTRL_CLK_SEL,
						 out ? CTRL_CLK_SEL_AIU :
						       CTRL_CLK_SEL_DISABLE) |
				      FIELD_PREP(CTRL_DATA_SEL,
						 out ? out->data_sel :
						       CTRL_DATA_SEL_OUTPUT_ZERO));

	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK,
				      out && out->amclk ?
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK : 0);
}

static int aiu_codec_ctrl_output_startup(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	int ret = meson_codec_glue_output_startup(substream, dai);

	if (ret)
		return ret;

	aiu_codec_ctrl_select(dai->component, dai->id == CTRL_OUT ?
					      &aiu_hdmi_out_i2s :
					      &aiu_hdmi_out_spdif);

	return 0;
}

static void aiu_codec_ctrl_output_shutdown(struct snd_pcm_substream *substream,
					   struct snd_soc_dai *dai)
{
	aiu_codec_ctrl_select(dai->component, NULL);
}

static const struct snd_soc_dai_ops aiu_codec_ctrl_input_ops = {
	.probe		= meson_codec_glue_input_dai_probe,
	.remove		= meson_codec_glue_input_dai_remove,
	.hw_params	= meson_codec_glue_input_hw_params,
	.set_fmt	= meson_codec_glue_input_set_fmt,
};

static const struct snd_soc_dai_ops aiu_codec_ctrl_output_ops = {
	.startup	= aiu_codec_ctrl_output_startup,
	.shutdown	= aiu_codec_ctrl_output_shutdown,
};

#define AIU_CODEC_CTRL_FORMATS					\
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |	\
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE |	\
	 SNDRV_PCM_FMTBIT_S32_LE)

#define AIU_CODEC_CTRL_STREAM(xname, xsuffix)			\
{								\
	.stream_name	= xname " " xsuffix,			\
	.channels_min	= 1,					\
	.channels_max	= 8,					\
	.rate_min       = 5512,					\
	.rate_max	= 192000,				\
	.formats	= AIU_CODEC_CTRL_FORMATS,		\
}

#define AIU_CODEC_CTRL_INPUT(xname) {				\
	.name = "CODEC CTRL " xname,				\
	.playback = AIU_CODEC_CTRL_STREAM(xname, "Playback"),	\
	.ops = &aiu_codec_ctrl_input_ops,			\
}

#define AIU_CODEC_CTRL_OUTPUT(xname) {				\
	.name = "CODEC CTRL " xname,				\
	.capture = AIU_CODEC_CTRL_STREAM(xname, "Capture"),	\
	.ops = &aiu_codec_ctrl_output_ops,			\
}

static struct snd_soc_dai_driver aiu_hdmi_ctrl_dai_drv[] = {
	[CTRL_I2S] = AIU_CODEC_CTRL_INPUT("HDMI I2S IN"),
	[CTRL_PCM] = AIU_CODEC_CTRL_INPUT("HDMI PCM IN"),
	[CTRL_OUT] = AIU_CODEC_CTRL_OUTPUT("HDMI OUT"),
	[CTRL_OUT_SPDIF] = AIU_CODEC_CTRL_OUTPUT("HDMI OUT SPDIF"),
};

static const struct snd_soc_dapm_route aiu_hdmi_ctrl_routes[] = {
	{ "HDMI OUT Capture", NULL, "HDMI I2S IN Playback" },
	{ "HDMI OUT SPDIF Capture", NULL, "HDMI PCM IN Playback" },
};

static int aiu_hdmi_of_xlate_dai_name(struct snd_soc_component *component,
				      const struct of_phandle_args *args,
				      const char **dai_name)
{
	return aiu_of_xlate_dai_name(component, args, dai_name, AIU_HDMI);
}

static const struct snd_soc_component_driver aiu_hdmi_ctrl_component = {
	.name			= "AIU HDMI Codec Control",
	.dapm_routes		= aiu_hdmi_ctrl_routes,
	.num_dapm_routes	= ARRAY_SIZE(aiu_hdmi_ctrl_routes),
	.of_xlate_dai_name	= aiu_hdmi_of_xlate_dai_name,
	.endianness		= 1,
#ifdef CONFIG_DEBUG_FS
	.debugfs_prefix		= "hdmi",
#endif
};

int aiu_hdmi_ctrl_register_component(struct device *dev)
{
	return snd_soc_register_component(dev, &aiu_hdmi_ctrl_component,
					  aiu_hdmi_ctrl_dai_drv,
					  ARRAY_SIZE(aiu_hdmi_ctrl_dai_drv));
}

