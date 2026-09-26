// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/control.h>
#include <sound/soc.h>

#include "meson-card.h"

int meson_card_i2s_set_sysclk(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      unsigned int mclk_fs)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	unsigned int mclk;
	int ret, i;

	if (!mclk_fs)
		return 0;

	mclk = params_rate(params) * mclk_fs;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					     SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP)
			return ret;
	}

	ret = snd_soc_dai_set_sysclk(snd_soc_rtd_to_cpu(rtd, 0), 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(meson_card_i2s_set_sysclk);

int meson_card_reallocate_links(struct snd_soc_card *card,
				unsigned int num_links)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link *links;
	void **ldata;

	links = krealloc(priv->card.dai_link,
			 num_links * sizeof(*priv->card.dai_link),
			 GFP_KERNEL | __GFP_ZERO);
	if (!links)
		return -ENOMEM;

	priv->card.dai_link = links;
	priv->card.num_links = num_links;

	ldata = krealloc(priv->link_data,
			 num_links * sizeof(*priv->link_data),
			 GFP_KERNEL | __GFP_ZERO);
	/* meson_card_clean_references() will free the links on this error path */
	if (!ldata)
		return -ENOMEM;

	priv->link_data = ldata;
	return 0;
}
EXPORT_SYMBOL_GPL(meson_card_reallocate_links);

int meson_card_parse_dai(struct snd_soc_card *card,
			 struct device_node *node,
			 struct snd_soc_dai_link_component *dlc)
{
	struct device *dev = card->dev;
	int ret;

	if (!dlc || !node)
		return -EINVAL;

	ret = snd_soc_of_get_dlc(node, NULL, dlc, 0);
	if (ret)
		return dev_err_probe(dev, ret, "can't parse dai\n");

	return ret;
}
EXPORT_SYMBOL_GPL(meson_card_parse_dai);

static int meson_card_set_link_name(struct snd_soc_card *card,
				    struct snd_soc_dai_link *link,
				    struct device_node *node,
				    const char *prefix)
{
	struct device *dev = card->dev;
	char *name = devm_kasprintf(dev, GFP_KERNEL, "%s.%s",
				    prefix, node->full_name);
	if (!name)
		return -ENOMEM;

	link->name = name;
	link->stream_name = name;

	return 0;
}

unsigned int meson_card_parse_daifmt(struct device_node *node,
				     struct device_node *cpu_node)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int daifmt;

	daifmt = snd_soc_daifmt_parse_format(node, NULL);

	snd_soc_daifmt_parse_clock_provider_as_phandle(node, NULL, &bitclkmaster, &framemaster);

	/* If no master is provided, default to cpu master */
	if (!bitclkmaster || bitclkmaster == cpu_node) {
		daifmt |= (!framemaster || framemaster == cpu_node) ?
			SND_SOC_DAIFMT_CBC_CFC : SND_SOC_DAIFMT_CBC_CFP;
	} else {
		daifmt |= (!framemaster || framemaster == cpu_node) ?
			SND_SOC_DAIFMT_CBP_CFC : SND_SOC_DAIFMT_CBP_CFP;
	}

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	return daifmt;
}
EXPORT_SYMBOL_GPL(meson_card_parse_daifmt);

int meson_card_set_be_link(struct snd_soc_card *card,
			   struct snd_soc_dai_link *link,
			   struct device_node *node)
{
	struct snd_soc_dai_link_component *codec;
	struct device *dev = card->dev;
	int ret, num_codecs;

	num_codecs = of_get_child_count(node);
	if (!num_codecs) {
		dev_err(dev, "be link %s has no codec\n",
			node->full_name);
		return -EINVAL;
	}

	codec = devm_kcalloc(dev, num_codecs, sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	link->codecs = codec;
	link->num_codecs = num_codecs;

	for_each_child_of_node_scoped(node, np) {
		ret = meson_card_parse_dai(card, np, codec);
		if (ret)
			return ret;

		codec++;
	}

	ret = meson_card_set_link_name(card, link, node, "be");
	if (ret)
		dev_err(dev, "error setting %pOFn link name\n", node);

	return ret;
}
EXPORT_SYMBOL_GPL(meson_card_set_be_link);

int meson_card_set_fe_link(struct snd_soc_card *card,
			   struct snd_soc_dai_link *link,
			   struct device_node *node,
			   bool is_playback)
{
	link->codecs = &snd_soc_dummy_dlc;
	link->num_codecs = 1;

	link->dynamic = 1;
	link->dpcm_merged_format = 1;
	link->dpcm_merged_chan = 1;
	link->dpcm_merged_rate = 1;

	if (is_playback)
		link->playback_only = 1;
	else
		link->capture_only = 1;

	return meson_card_set_link_name(card, link, node, "fe");
}
EXPORT_SYMBOL_GPL(meson_card_set_fe_link);

static int meson_card_add_links(struct snd_soc_card *card)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct device *dev = card->dev;
	struct device_node *node = dev->of_node;
	int num, i, ret;

	num = of_get_child_count(node);
	if (!num) {
		dev_err(dev, "card has no links\n");
		return -EINVAL;
	}

	ret = meson_card_reallocate_links(card, num);
	if (ret)
		return ret;

	i = 0;
	for_each_child_of_node_scoped(node, np) {
		ret = priv->match_data->add_link(card, np, &i);
		if (ret)
			return ret;

		i++;
	}

	return 0;
}

static int meson_card_parse_of_optional(struct snd_soc_card *card,
					const char *propname,
					int (*func)(struct snd_soc_card *c,
						    const char *p))
{
	struct device *dev = card->dev;

	/* If property is not provided, don't fail ... */
	if (!of_property_present(dev->of_node, propname))
		return 0;

	/* ... but do fail if it is provided and the parsing fails */
	return func(card, propname);
}

static void meson_card_clean_references(struct meson_card *priv)
{
	struct snd_soc_card *card = &priv->card;
	struct snd_soc_dai_link *link;
	struct snd_soc_dai_link_component *codec;
	struct snd_soc_aux_dev *aux;
	int i, j;

	if (card->dai_link) {
		for_each_card_prelinks(card, i, link) {
			if (link->cpus)
				of_node_put(link->cpus->of_node);
			for_each_link_codecs(link, j, codec)
				of_node_put(codec->of_node);
		}
	}

	if (card->aux_dev) {
		for_each_card_pre_auxs(card, i, aux)
			of_node_put(aux->dlc.of_node);
	}

	kfree(card->dai_link);
	kfree(priv->link_data);
}

static int meson_card_alias_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct snd_kcontrol *target = snd_kcontrol_chip(kcontrol);
	struct snd_ctl_elem_id id = uinfo->id;
	int ret;

	snd_ctl_build_ioff(&uinfo->id, target, snd_ctl_get_ioff(kcontrol, &id));
	ret = target->info(target, uinfo);
	uinfo->id = id;

	return ret;
}

static int meson_card_alias_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_kcontrol *target = snd_kcontrol_chip(kcontrol);
	struct snd_ctl_elem_id id = ucontrol->id;
	int ret;

	snd_ctl_build_ioff(&ucontrol->id, target, snd_ctl_get_ioff(kcontrol, &id));
	ret = target->get(target, ucontrol);
	ucontrol->id = id;

	return ret;
}

static int meson_card_alias_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_kcontrol *target = snd_kcontrol_chip(kcontrol);
	struct snd_ctl_elem_id id = ucontrol->id;
	int ret;

	snd_ctl_build_ioff(&ucontrol->id, target, snd_ctl_get_ioff(kcontrol, &id));
	ret = target->put(target, ucontrol);
	ucontrol->id = id;

	return ret;
}

static int meson_card_alias_tlv(struct snd_kcontrol *kcontrol, int op_flag,
				unsigned int size, unsigned int __user *tlv)
{
	struct snd_kcontrol *target = snd_kcontrol_chip(kcontrol);

	return target->tlv.c(target, op_flag, size, tlv);
}

static int meson_card_alias_ctl(struct snd_card *card,
				struct snd_kcontrol *target,
				unsigned int device)
{
	struct snd_kcontrol_new knew = {
		.iface		= target->id.iface,
		.name		= target->id.name,
		.device		= device,
		.subdevice	= target->id.subdevice,
		.index		= target->id.index,
		.count		= target->count,
		.access		= target->vd[0].access,
		.info		= meson_card_alias_info,
		.get		= meson_card_alias_get,
	};
	struct snd_ctl_elem_id id = target->id;
	struct snd_kcontrol *kctl;

	id.numid = 0;
	id.device = device;
	if (snd_ctl_find_id(card, &id))
		return 0;

	if (target->put)
		knew.put = meson_card_alias_put;

	if (knew.access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK)
		knew.tlv.c = meson_card_alias_tlv;
	else
		knew.tlv.p = target->tlv.p;

	kctl = snd_ctl_new1(&knew, target);
	if (!kctl)
		return -ENOMEM;

	return snd_ctl_add(card, kctl);
}

static int meson_card_alias_be_ctls(struct snd_soc_card *card,
				    struct snd_pcm *be)
{
	struct snd_card *snd_card = card->snd_card;
	struct snd_soc_pcm_runtime *fe;
	struct snd_kcontrol **targets, *kctl;
	unsigned int i, n = 0;
	int ret = 0;

	targets = kcalloc(snd_card->controls_count, sizeof(*targets),
			  GFP_KERNEL);
	if (!targets)
		return -ENOMEM;

	scoped_guard(rwsem_read, &snd_card->controls_rwsem) {
		list_for_each_entry(kctl, &snd_card->controls, list) {
			if (kctl->id.iface == SNDRV_CTL_ELEM_IFACE_PCM &&
			    kctl->id.device == be->device &&
			    n < snd_card->controls_count)
				targets[n++] = kctl;
		}
	}

	for_each_card_rtds(card, fe) {
		if (!fe->dai_link->dynamic || !fe->pcm ||
		    !fe->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count)
			continue;

		for (i = 0; i < n; i++) {
			ret = meson_card_alias_ctl(snd_card, targets[i],
						   fe->pcm->device);
			if (ret)
				goto out;
		}
	}

out:
	kfree(targets);
	return ret;
}

static int meson_card_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	int ret;

	for_each_card_rtds(card, rtd) {
		if (!rtd->pcm || !rtd->pcm->internal)
			continue;

		ret = meson_card_alias_be_ctls(card, rtd->pcm);
		if (ret)
			return ret;
	}

	return 0;
}

int meson_card_probe(struct platform_device *pdev)
{
	const struct meson_card_match_data *data;
	struct device *dev = &pdev->dev;
	struct meson_card *priv;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	snd_soc_card_set_drvdata(&priv->card, priv);

	priv->card.owner = THIS_MODULE;
	priv->card.dev = dev;
	priv->card.driver_name = dev->driver->name;
	priv->card.late_probe = meson_card_late_probe;
	priv->match_data = data;

	ret = snd_soc_of_parse_card_name(&priv->card, "model");
	if (ret < 0)
		return ret;

	ret = meson_card_parse_of_optional(&priv->card, "audio-routing",
					   snd_soc_of_parse_audio_routing);
	if (ret) {
		dev_err(dev, "error while parsing routing\n");
		return ret;
	}

	ret = meson_card_parse_of_optional(&priv->card, "audio-widgets",
					   snd_soc_of_parse_audio_simple_widgets);
	if (ret) {
		dev_err(dev, "error while parsing widgets\n");
		return ret;
	}

	ret = meson_card_add_links(&priv->card);
	if (ret)
		goto out_err;

	ret = snd_soc_of_parse_aux_devs(&priv->card, "audio-aux-devs");
	if (ret)
		goto out_err;

	ret = devm_snd_soc_register_card(dev, &priv->card);
	if (ret)
		goto out_err;

	return 0;

out_err:
	meson_card_clean_references(priv);
	return ret;
}
EXPORT_SYMBOL_GPL(meson_card_probe);

void meson_card_remove(struct platform_device *pdev)
{
	struct meson_card *priv = platform_get_drvdata(pdev);

	meson_card_clean_references(priv);
}
EXPORT_SYMBOL_GPL(meson_card_remove);

MODULE_DESCRIPTION("Amlogic Sound Card Utils");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
