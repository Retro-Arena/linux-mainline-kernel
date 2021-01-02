/*
 * Rockchip machine ASoC driver for Rockchip built-in HDMI and DP audio output
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * Authors: Sugar Zhang <sugar.zhang@rock-chips.com>,
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/hdmi-codec.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "rockchip_i2s.h"

#define DRV_NAME "rk-hdmi-dp-sound"
#define MAX_CODECS	2

static const struct snd_soc_dapm_widget rk_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("HDMI", NULL),
};

static const struct snd_soc_dapm_route rk_dapm_routes[] = {
	{"HDMI", NULL, "TX"},
};

static const struct snd_kcontrol_new rk_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("HDMI"),
};

static int rk_hdmi_dp_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int mclk;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	case 176400:
		mclk = 11289600 * 2;
		break;
	case 192000:
		mclk = 12288000 * 2;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);

	if (ret && ret != -ENOTSUPP) {
		dev_err(cpu_dai->dev, "Can't set cpu clock %d\n", ret);
		return ret;
	}

	return 0;
}

static int rockchip_sound_register_jack(struct snd_soc_card *card, struct snd_soc_component *codec, int index)
{
	struct snd_soc_jack *jack;
	char jack_name[60];
	const char *dev_name = NULL;
	int ret;

	jack = devm_kzalloc(card->dev, sizeof(*jack), GFP_KERNEL);
	if (!jack) {
		dev_err(card->dev, "Can't allocate HDMI Jack %d\n", ret);
		return -ENOMEM;
	}

	if (codec->dev->parent && codec->dev->parent->of_node) {
		dev_name = of_node_full_name(codec->dev->parent->of_node);
	}

	if (dev_name) {
		snprintf(jack_name, sizeof(jack_name), "%s Jack", dev_name);
	} else {
		snprintf(jack_name, sizeof(jack_name), "Codec %d Jack", index);
	}

	ret = snd_soc_card_jack_new(card, jack_name, SND_JACK_LINEOUT,
						jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "Can't create HDMI Jack %d\n", ret);
		return ret;
	}

	ret = hdmi_codec_set_jack_detect(codec, jack);
	if (ret) {
		dev_warn(codec->dev, "Failed to register HDMI Jack %d\n", ret);
		ret = 0;
	}

	return ret;
}

static int rockchip_sound_hdmi_dp_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	int i;
	int ret;

	for (i = 0; i < runtime->num_codecs; ++i) {
		struct snd_soc_component *component = asoc_rtd_to_codec(runtime, i)->component;

		ret = rockchip_sound_register_jack(card, component, i);
		if (ret) {
			dev_err(card->dev, "Failed to register codec jack: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static struct snd_soc_ops rockchip_sound_hdmidp_ops = {
	.hw_params = rk_hdmi_dp_hw_params,
};

SND_SOC_DAILINK_DEFS(audio,
	DAILINK_COMP_ARRAY(COMP_CPU(NULL)),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, NULL), COMP_CODEC(NULL, NULL)),
	DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

static struct snd_soc_dai_link rk_dailink = {
	.name = "HDMI-DP",
	.stream_name = "HDMI-DP",
	.init = rockchip_sound_hdmi_dp_init,
	.ops = &rockchip_sound_hdmidp_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
	SND_SOC_DAILINK_REG(audio),
};

static struct snd_soc_card snd_soc_card_rk = {
	.name = "rk-hdmi-dp-sound",
	.dai_link = &rk_dailink,
	.num_links = 1,
	.num_aux_devs = 0,
	.dapm_widgets = rk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_dapm_widgets),
	.dapm_routes = rk_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rk_dapm_routes),
	.controls = rk_mc_controls,
	.num_controls = ARRAY_SIZE(rk_mc_controls),
};

static int rk_hdmi_dp_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_rk;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link *link = card->dai_link;
	struct snd_soc_dai_link_component *codec;
	struct of_phandle_args args;
	int count;
	int ret = 0, i = 0;

	card->dev = &pdev->dev;

	count = of_count_phandle_with_args(np, "rockchip,codec", NULL);
	if (count < 0 || count > MAX_CODECS)
		return -EINVAL;

	/* refine codecs, remove unavailable node */
	link->num_codecs = 0;

	for (i = 0; i < count; i++) {
		codec = &link->codecs[link->num_codecs];

		codec->of_node = of_parse_phandle(np, "rockchip,codec", i);
		if (!codec->of_node) {
			dev_err(&pdev->dev,
				"Property 'rockchip,codec' missing or invalid\n");
			return -EINVAL;
		}
	
		if (!of_device_is_available(codec->of_node)) {
			dev_err(&pdev->dev,
				"Property 'rockchip,codec' is not available\n");
			continue;
		}

		ret = of_parse_phandle_with_fixed_args(np, "rockchip,codec",
						       0, i, &args);
		if (ret) {
			dev_err(&pdev->dev,
				"Unable to parse property 'rockchip,codec'\n");
			return ret;
		}

		ret = snd_soc_get_dai_name(&args, &codec->dai_name);
		if (ret)
			return ret;

		link->num_codecs++;
	}

	if (!link->num_codecs) {
		dev_err(&pdev->dev,
			"Property 'rockchip,cpu' missing or invalid\n");
		return -ENODEV;
	}

	link->cpus->of_node = of_parse_phandle(np, "rockchip,cpu", 0);
	if (!link->cpus->of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,cpu' missing or invalid\n");
		return -ENODEV;
	}
	link->platforms->of_node = link->cpus->of_node;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (ret) {
		dev_err(&pdev->dev, "card register failed %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static const struct of_device_id rockchip_sound_of_match[] = {
	{ .compatible = "rockchip,rk3399-hdmi-dp", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_sound_of_match);

static struct platform_driver rockchip_sound_driver = {
	.probe = rk_hdmi_dp_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_sound_of_match,
	},
};

module_platform_driver(rockchip_sound_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Built-in HDMI and DP machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
