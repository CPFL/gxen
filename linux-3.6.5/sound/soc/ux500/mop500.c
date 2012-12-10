/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja (ola.o.lilja@stericsson.com)
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/io.h>
#include <linux/spi/spi.h>

#include <sound/soc.h>
#include <sound/initval.h>

#include "ux500_pcm.h"
#include "ux500_msp_dai.h"

#include <mop500_ab8500.h>

/* Define the whole MOP500 soundcard, linking platform to the codec-drivers  */
struct snd_soc_dai_link mop500_dai_links[] = {
	{
		.name = "ab8500_0",
		.stream_name = "ab8500_0",
		.cpu_dai_name = "ux500-msp-i2s.1",
		.codec_dai_name = "ab8500-codec-dai.0",
		.platform_name = "ux500-pcm.0",
		.codec_name = "ab8500-codec.0",
		.init = mop500_ab8500_machine_init,
		.ops = mop500_ab8500_ops,
	},
	{
		.name = "ab8500_1",
		.stream_name = "ab8500_1",
		.cpu_dai_name = "ux500-msp-i2s.3",
		.codec_dai_name = "ab8500-codec-dai.1",
		.platform_name = "ux500-pcm.0",
		.codec_name = "ab8500-codec.0",
		.init = NULL,
		.ops = mop500_ab8500_ops,
	},
};

static struct snd_soc_card mop500_card = {
	.name = "MOP500-card",
	.probe = NULL,
	.dai_link = mop500_dai_links,
	.num_links = ARRAY_SIZE(mop500_dai_links),
};

static int __devinit mop500_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	dev_dbg(&pdev->dev, "%s: Enter.\n", __func__);

	mop500_card.dev = &pdev->dev;

	dev_dbg(&pdev->dev, "%s: Card %s: Set platform drvdata.\n",
		__func__, mop500_card.name);
	platform_set_drvdata(pdev, &mop500_card);

	snd_soc_card_set_drvdata(&mop500_card, NULL);

	dev_dbg(&pdev->dev, "%s: Card %s: num_links = %d\n",
		__func__, mop500_card.name, mop500_card.num_links);
	dev_dbg(&pdev->dev, "%s: Card %s: DAI-link 0: name = %s\n",
		__func__, mop500_card.name, mop500_card.dai_link[0].name);
	dev_dbg(&pdev->dev, "%s: Card %s: DAI-link 0: stream_name = %s\n",
		__func__, mop500_card.name,
		mop500_card.dai_link[0].stream_name);

	ret = snd_soc_register_card(&mop500_card);
	if (ret)
		dev_err(&pdev->dev,
			"Error: snd_soc_register_card failed (%d)!\n",
			ret);

	return ret;
}

static int __devexit mop500_remove(struct platform_device *pdev)
{
	struct snd_soc_card *mop500_card = platform_get_drvdata(pdev);

	pr_debug("%s: Enter.\n", __func__);

	snd_soc_unregister_card(mop500_card);
	mop500_ab8500_remove(mop500_card);
	
	return 0;
}

static struct platform_driver snd_soc_mop500_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-soc-mop500",
	},
	.probe = mop500_probe,
	.remove = __devexit_p(mop500_remove),
};

module_platform_driver(snd_soc_mop500_driver);
