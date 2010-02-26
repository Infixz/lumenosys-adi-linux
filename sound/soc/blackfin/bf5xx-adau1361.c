/*
 * board driver for adau1361 sound chip
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>

#include <asm/dma.h>
#include <asm/portmux.h>
#include <linux/gpio.h>
#include "../codecs/adau1361.h"
#include "bf5xx-sport.h"
#include "bf5xx-i2s-pcm.h"
#include "bf5xx-i2s.h"

static int bf5xx_adau1361_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int pll_in = 0, pll_out = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		pll_out = ADAU1361_PLL_FREQ_441;
		break;
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		pll_out = ADAU1361_PLL_FREQ_48;
		break;
	default:
		pll_out = ADAU1361_PLL_FREQ_441;
		break;
	}

	/* set codec DAI configuration */
	ret = codec_dai->ops->set_fmt(codec_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = cpu_dai->ops->set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	pll_in = ADAU1361_MCLK_RATE; /* fixed rate MCLK */
	ret = codec_dai->ops->set_sysclk(codec_dai, ADAU1361_MCLK_ID, pll_in,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* codec PLL input is PCLK/4 */
	ret = codec_dai->ops->set_pll(codec_dai, 0, pll_in, pll_out);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops bf5xx_adau1361_ops = {
	.hw_params = bf5xx_adau1361_hw_params,
};

static struct snd_soc_dai_link bf5xx_adau1361_dai = {
	.name = "adau1361",
	.stream_name = "adau1361",
	.cpu_dai = &bf5xx_i2s_dai,
	.codec_dai = &adau1361_dai,
	.ops = &bf5xx_adau1361_ops,
};

static struct snd_soc_card bf5xx_adau1361 = {
	.name = "bf5xx_adau1361",
	.platform = &bf5xx_i2s_soc_platform,
	.dai_link = &bf5xx_adau1361_dai,
	.num_links = 1,
};

static struct snd_soc_device bf5xx_adau1361_snd_devdata = {
	.card = &bf5xx_adau1361,
	.codec_dev = &soc_codec_dev_adau1361,
};

static struct platform_device *bf5xx_adau1361_snd_device;

static int __init bf5xx_adau1361_init(void)
{
	int ret;

	pr_debug("%s enter\n", __func__);
	bf5xx_adau1361_snd_device = platform_device_alloc("soc-audio", -1);
	if (!bf5xx_adau1361_snd_device)
		return -ENOMEM;

	platform_set_drvdata(bf5xx_adau1361_snd_device,
				&bf5xx_adau1361_snd_devdata);
	bf5xx_adau1361_snd_devdata.dev = &bf5xx_adau1361_snd_device->dev;
	ret = platform_device_add(bf5xx_adau1361_snd_device);

	if (ret)
		platform_device_put(bf5xx_adau1361_snd_device);

	return ret;
}
module_init(bf5xx_adau1361_init);

static void __exit bf5xx_adau1361_exit(void)
{
	pr_debug("%s enter\n", __func__);
	platform_device_unregister(bf5xx_adau1361_snd_device);
}
module_exit(bf5xx_adau1361_exit);

/* Module information */
MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("ALSA SoC adau1361");
MODULE_LICENSE("GPL");