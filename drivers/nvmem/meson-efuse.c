// SPDX-License-Identifier: GPL-2.0-only
/*
 * Amlogic Meson GX eFuse Driver
 *
 * Copyright (c) 2016 Endless Computers, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <linux/firmware/meson/meson_sm.h>

#include "generic-efuse-class.h"

static int meson_efuse_read(void *context, unsigned int offset,
			    void *val, size_t bytes)
{
	struct meson_sm_firmware *fw = context;
	int ret;

	ret = meson_sm_call_read(fw, (u8 *)val, bytes, SM_EFUSE_READ, offset,
				 bytes, 0, 0, 0);

	return ret < 0 ? ret : 0;
}

static int meson_efuse_write(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	struct meson_sm_firmware *fw = context;
	int ret;

	ret = meson_sm_call_write(fw, (u8 *)val, bytes, SM_EFUSE_WRITE, offset,
				  bytes, 0, 0, 0);

	return ret < 0 ? ret : 0;
}

static const struct of_device_id meson_efuse_match[] = {
	{ .compatible = "amlogic,meson-gxbb-efuse", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_efuse_match);

static int meson_efuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_sm_firmware *fw;
	struct device_node *sm_np;
	struct nvmem_device *nvmem;
	struct nvmem_config *econfig;
	struct clk *clk;
	unsigned int size;

	sm_np = of_parse_phandle(pdev->dev.of_node, "secure-monitor", 0);
	if (!sm_np) {
		dev_err(&pdev->dev, "no secure-monitor node\n");
		return -ENODEV;
	}

	fw = meson_sm_get(sm_np);
	of_node_put(sm_np);
	if (!fw)
		return -EPROBE_DEFER;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get efuse gate");

	if (meson_sm_call(fw, SM_EFUSE_USER_MAX, &size, 0, 0, 0, 0, 0) < 0) {
		dev_err(dev, "failed to get max user");
		return -EINVAL;
	}

	econfig = devm_kzalloc(dev, sizeof(*econfig), GFP_KERNEL);
	if (!econfig)
		return -ENOMEM;

	econfig->dev = dev;
	econfig->name = dev_name(dev);
	econfig->stride = 1;
	econfig->word_size = 1;
	econfig->reg_read = meson_efuse_read;
	econfig->reg_write = meson_efuse_write;
	econfig->size = size;
	econfig->priv = fw;

	nvmem = devm_nvmem_register(&pdev->dev, econfig);

	generic_efuse_class_probe(pdev, nvmem);

	return PTR_ERR_OR_ZERO(nvmem);
}

static int meson_efuse_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	generic_efuse_class_remove(pdev);

	nvmem_unregister(nvmem);
	return 0;
}

static struct platform_driver meson_efuse_driver = {
	.probe = meson_efuse_probe,
	.remove = meson_efuse_remove,
	.driver = {
		.name = "meson-efuse",
		.of_match_table = meson_efuse_match,
	},
};

module_platform_driver(meson_efuse_driver);

MODULE_AUTHOR("Carlo Caione <carlo@endlessm.com>");
MODULE_DESCRIPTION("Amlogic Meson GX NVMEM driver");
MODULE_LICENSE("GPL v2");
