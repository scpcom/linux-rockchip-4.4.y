/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2017 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include "drv_vdpo.h"
#include "../disp/disp_sys_intf.h"
#include <linux/regulator/consumer.h>

static u32 g_vdpo_num;
static u32 pin_enable_count;
struct drv_vdpo_info_t g_vdpo_info[VDPO_NUM];

static int vdpo_pin_config(u32 bon)
{
	return disp_sys_pin_set_state(
	    "vdpo", (bon == 1) ? DISP_PIN_STATE_ACTIVE : DISP_PIN_STATE_SLEEP);
}



static s32 vdpo_get_mode(u32 sel)
{
	vdpo_dbg("output_mode:%d\n", g_vdpo_info[sel].output_mode);
	return g_vdpo_info[sel].output_mode;
}

static s32 vdpo_set_mode(u32 sel, enum disp_tv_mode output_mode)
{
	if (output_mode >= DISP_TV_MODE_NUM)
		return -1;
	mutex_lock(&g_vdpo_info[sel].mlock);
	g_vdpo_info[sel].output_mode = output_mode;
	mutex_unlock(&g_vdpo_info[sel].mlock);
	return 0;
}

/**
 * @name       :vdpo_clk_init
 * @brief      :get vdpo clk's parent
 * @param[IN]  :sel:index of vdpo
 * @return     :0 if success
 */
static s32 vdpo_clk_init(u32 sel)
{
	s32 ret = 0;

	g_vdpo_info[sel].clk_parent = clk_get_parent(g_vdpo_info[sel].clk);
	if (IS_ERR_OR_NULL(g_vdpo_info[sel].clk_parent)) {
		vdpo_wrn("Get vdpo%d's clk parent fail!", sel);
		ret = -1;
	}
	return ret;
}

/**
 * @name       :vdpo_clk_disable
 * @brief      :enable vdpo module's clk
 * @param[IN]  :sel:index of vdpo
 * @param[OUT] :none
 * @return     :0 if success
 */
static s32 vdpo_clk_disable(u32 sel)
{
	s32 ret = 0;

	if (g_vdpo_info[sel].clk)
		clk_disable(g_vdpo_info[sel].clk);
	return ret;
}

/**
 * @name       :vdpo_clk_enable
 * @brief      :enable vdpo module's clk
 * @param[IN]  :sel:index of vdpo
 * @param[OUT] :none
 * @return     :0 if success
 */
static s32 vdpo_clk_enable(u32 sel)
{
	s32 ret = 0;

	ret = clk_prepare_enable(g_vdpo_info[sel].clk);
	if (ret != 0)
		vdpo_wrn("enable vdpo%d's clk fail!", sel);
	return ret;
}

/**
 * @name       vdpo_clk_config
 * @brief      set vdpo's clk rate
 * @param[IN]  output_mode:resolution
 * @param[IN]  sel:index of vdpo module
 * @return     0 if success
 */
static s32 vdpo_clk_config(u32 sel, u32 output_mode)
{
	s32 ret = 0;
	struct disp_video_timings *info = NULL;

	vdpo_get_timing_info(output_mode, &info);
	if (info == NULL) {
		vdpo_wrn("get timing info fail!\n");
		goto OUT;
	}

	clk_set_rate(g_vdpo_info[sel].clk, info->pixel_clk * 2);

OUT:
	return ret;
}

/**
 * @name       vdpo_low_config
 * @brief      config vdpo module in lowlevel
 * @param[IN]  sel:index of vdpo module
 * @param[OUT]
 * @return
 */
s32 vdpo_low_config(u32 sel)
{
	s32 ret = -1;
	struct disp_video_timings *info = NULL;

	vdpo_get_timing_info(g_vdpo_info[sel].output_mode, &info);
	if (info == NULL) {
		vdpo_wrn("get timing info fail!\n");
		goto OUT;
	}

	__vdpo_timing_set(
	    sel, info->x_res, (info->hor_back_porch + info->hor_front_porch),
	    info->y_res, (info->ver_back_porch + info->ver_front_porch),
	    info->ver_total_time, info->b_interlace, g_vdpo_info[sel].protocol);

	__vdpo_fmt_set(sel, g_vdpo_info[sel].data_seq_sel,
		       g_vdpo_info[sel].protocol, g_vdpo_info[sel].output_width,
		       info->b_interlace);

	__vdpo_chroma_spl_set(sel, g_vdpo_info[sel].spl_type_u,
			      g_vdpo_info[sel].spl_type_v);

	__vdpo_clamp_set(sel, 16, 235, 16, 240, 16, 240);

	__vdpo_sync_pol_set(sel, info->hor_sync_polarity,
			    info->ver_sync_polarity, 0);

	if (g_vdpo_info[sel].dclk_dly_num)
		__vdpo_dclk_adjust(sel, g_vdpo_info[sel].dclk_invt, 1,
				   g_vdpo_info[sel].dclk_dly_num);
	else
		__vdpo_dclk_adjust(sel, g_vdpo_info[sel].dclk_invt, 0,
				   g_vdpo_info[sel].dclk_dly_num);

OUT:
	return ret;
}

/**
 * @name       :vdpo_init
 * @brief      :parse sys_config then init vdpo and handle boot_disp
 * @param[IN]  :pdev:platform_device
 * @param[OUT] :none
 * @return     :0 if success
 */
static s32 vdpo_init(struct platform_device *pdev)
{
	s32 ret = -1;
	u32 value, output_type0, output_mode0, sel = pdev->id, output_type1,
					       output_mode1;

	ret = of_property_read_u32(pdev->dev.of_node, "protocol", &value);
	if (ret < 0) {
		dev_err(&pdev->dev, "get protocol from sys_config fail!\n");
		goto OUT;
	}
	g_vdpo_info[sel].protocol = value;


	ret = of_property_read_u32(pdev->dev.of_node, "separate_sync", &value);
	if (ret < 0) {
		dev_err(&pdev->dev, "get separate_sync from sys_config fail!\n");
		goto OUT;
	}
	g_vdpo_info[sel].separate_sync = value;

	ret = of_property_read_u32(pdev->dev.of_node, "output_width", &value);
	if (ret < 0) {
		dev_err(&pdev->dev, "get output_width from sys_config fail!\n");
		goto OUT;
	}
	g_vdpo_info[sel].output_width = value;

	ret = of_property_read_u32(pdev->dev.of_node, "data_seq_sel", &value);
	if (ret < 0)
		dev_err(&pdev->dev, "get data_seq_sel from sys_config fail!\n");
	else
		g_vdpo_info[sel].data_seq_sel = value;

	ret = of_property_read_u32(pdev->dev.of_node, "dclk_invt", &value);
	if (ret < 0)
		dev_err(&pdev->dev, "get dclk_invt from sys_config fail!\n");
	else
		g_vdpo_info[sel].dclk_invt = value;

	ret = of_property_read_u32(pdev->dev.of_node, "dclk_dly_num", &value);
	if (ret < 0)
		dev_err(&pdev->dev, "get dclk_dly_num from sys_config fail!\n");
	else
		g_vdpo_info[sel].dclk_dly_num = value;

	ret = of_property_read_u32(pdev->dev.of_node, "spl_type_u", &value);
	if (ret < 0)
		dev_err(&pdev->dev, "get spl_type_u from sys_config fail!\n");
	else
		g_vdpo_info[sel].spl_type_u = value;

	ret = of_property_read_u32(pdev->dev.of_node, "spl_type_v", &value);
	if (ret < 0)
		dev_err(&pdev->dev, "get spl_type_v from sys_config fail!\n");
	else
		g_vdpo_info[sel].spl_type_v = value;

	vdpo_dbg("protocol:%d\n", g_vdpo_info[sel].protocol);
	vdpo_dbg("separate_sync:%d\n", g_vdpo_info[sel].separate_sync);
	vdpo_dbg("output_width:%d\n", g_vdpo_info[sel].output_width);
	vdpo_dbg("data_seq_sel:%d\n", g_vdpo_info[sel].data_seq_sel);
	vdpo_dbg("spl_type_u:%d\n", g_vdpo_info[sel].spl_type_u);
	vdpo_dbg("spl_type_v:%d\n", g_vdpo_info[sel].spl_type_v);
	vdpo_dbg("dclk_invt:%d\n", g_vdpo_info[sel].dclk_invt);
	vdpo_dbg("dclk_dly_num:%d\n", g_vdpo_info[sel].dclk_dly_num);

	mutex_init(&g_vdpo_info[sel].mlock);
	/*  parse boot para */
	value = disp_boot_para_parse("boot_disp");
	output_type0 = (value >> 8) & 0xff;
	output_mode0 = (value)&0xff;

	output_type1 = (value >> 24) & 0xff;
	output_mode1 = (value >> 16) & 0xff;
	if ((output_type0 == DISP_OUTPUT_TYPE_VDPO) ||
	    (output_type1 == DISP_OUTPUT_TYPE_VDPO)) {
		g_vdpo_info[sel].enable = 1;
		g_vdpo_info[sel].output_mode =
		    (output_type0 == DISP_OUTPUT_TYPE_VDPO) ? output_mode0
							    : output_mode1;
		;
		vdpo_clk_config(sel, g_vdpo_info[sel].output_mode);
	} else {
		g_vdpo_info[sel].output_mode = DISP_TV_MOD_1080P_60HZ;
	}

	if (pin_enable_count == 0) {
		vdpo_pin_config(1);
		++pin_enable_count;
	}
	__vdpo_set_reg_base(sel, g_vdpo_info[sel].base_addr);
	vdpo_clk_init(sel);
	vdpo_clk_enable(sel);
	vdpo_low_config(sel);

OUT:
	return ret;
}

/**
 * @name       vdpo_enable
 * @brief      enable vdpo module
 * @param[IN]  sel:index of vdpo module
 * @param[OUT] none
 * @return     0 if suceess
 */
s32 vdpo_enable(u32 sel)
{
	s32 ret = 0;

	vdpo_here;
	mutex_lock(&g_vdpo_info[sel].mlock);
	if (!g_vdpo_info[sel].enable) {
		if (pin_enable_count == 0) {
			ret = vdpo_pin_config(1);
			++pin_enable_count;
		}
		vdpo_clk_config(sel, g_vdpo_info[sel].output_mode);
		vdpo_clk_enable(sel);
		vdpo_low_config(sel);
		__vdpo_module_en(sel, 1, g_vdpo_info[sel].separate_sync);
		g_vdpo_info[sel].enable = 1;
	}
	mutex_unlock(&g_vdpo_info[sel].mlock);
	return ret;
}

/**
 * @name       vdpo_disable
 * @brief      disable vdpo module
 * @param[IN]  sel:index of vdpo
 * @param[OUT] none
 * @return     0 if success
 */
s32 vdpo_disable(u32 sel)
{
	s32 ret = 0;

	vdpo_here;
	mutex_lock(&g_vdpo_info[sel].mlock);
	if (g_vdpo_info[sel].enable) {
		__vdpo_module_en(sel, 0, g_vdpo_info[sel].separate_sync);
		g_vdpo_info[sel].enable = 0;
		if (pin_enable_count != 0) {
			vdpo_pin_config(0);
			--pin_enable_count;
		}
		vdpo_clk_disable(sel);
	}
	mutex_unlock(&g_vdpo_info[sel].mlock);
	return ret;
}

#if 0
/**
 * @name       vdpo_resume
 * @brief      resume function for vdpo
 * @param[IN]  sel:index of vdpo
 * @param[OUT] none
 * @return     0 if success
 */
static s32 vdpo_resume(u32 sel)
{
	s32 ret = 0;
	vdpo_here;
	mutex_lock(&g_vdpo_info[sel].mlock);
	if (g_vdpo_info[sel].suspend) {
		vdpo_here;
		g_vdpo_info[sel].suspend = false;
		if (pin_enable_count == 0) {
			ret = vdpo_pin_config(1);
			++pin_enable_count;
		}
		vdpo_clk_config(sel, g_vdpo_info[sel].output_mode);
		vdpo_clk_enable(sel);
		vdpo_low_config(sel);
		__vdpo_module_en(sel, 1, g_vdpo_info[sel].separate_sync);
	}

	mutex_unlock(&g_vdpo_info[sel].mlock);
	return ret;
}
#endif

/**
 * @name       vdpo_get_input_csc
 * @brief      tell DE the type of color space to be output
 * @param[IN]  sel:index of vdpo sel
 * @param[OUT] none
 * @return     1:yuv
 */
s32 vdpo_get_input_csc(u32 sel)
{
	s32 ret = 1;

	vdpo_here;
	return ret;
}
#if 0
/**
 * @name       vdpo_suspend
 * @brief      suspend function for vdpo
 * @param[IN]  sel:index of vdpo module
 * @param[OUT] none
 * @return     0 if success
 */
static s32 vdpo_suspend(u32 sel)
{
	s32 ret = 0;

	vdpo_here;
	mutex_lock(&g_vdpo_info[sel].mlock);
	if (!g_vdpo_info[sel].suspend) {
		vdpo_here;
		g_vdpo_info[sel].suspend = true;
		if (pin_enable_count != 0) {
			vdpo_pin_config(0);
			--pin_enable_count;
		}
		__vdpo_module_en(sel, 0, g_vdpo_info[sel].separate_sync);
		vdpo_clk_disable(sel);
	}
	mutex_unlock(&g_vdpo_info[sel].mlock);
	return ret;
}
#endif

/**
 * @name       vdpo_get_video_timing_info
 * @brief      get timing info
 * @param[IN]  sel:index of vdpo module
 * @param[OUT] video_info:timing info
 * @return     0 if success
 */
static s32 vdpo_get_video_timing_info(u32 sel,
				      struct disp_video_timings **video_info)
{
	s32 ret = 0;

	ret = vdpo_get_timing_info(g_vdpo_info[sel].output_mode, video_info);

	vdpo_dbg("ret:%d,out_mode:%d\n", ret, g_vdpo_info[sel].output_mode);
	return ret;
}

int vdpo_irq_enable(u32 sel, u32 irq_id, u32 en)
{
	vdpo_here;
	__vdpo_irq_en(sel, V_INT, en);
	return 0;
}

int vdpo_irq_query(u32 sel)
{
	u32 ret = __vdpo_irq_process(sel);
	__vdpo_clr_irq(sel, V_INT);
	return ret;
}

static s32 vdpo_mode_support(u32 sel, enum disp_tv_mode mode)
{
	s32 ret = 0;

	ret = vdpo_is_mode_support(mode);
	vdpo_dbg("%d\n", ret);
	return ret;
}

unsigned int vdpo_get_cur_line(u32 sel)
{
	return __vdpo_get_curline(sel);
}

s32 vdpo_set_config(u32 sel, struct disp_vdpo_config *p_cfg)
{
	s32 ret = -1;

	if (!p_cfg) {
		vdpo_wrn("NULL arg\n");
		goto OUT;
	}
	vdpo_here;

	g_vdpo_info[sel].spl_type_u = p_cfg->spl_type_u;
	g_vdpo_info[sel].spl_type_v = p_cfg->spl_type_v;
	g_vdpo_info[sel].dclk_invt  = p_cfg->dclk_invt;
	g_vdpo_info[sel].dclk_dly_num  = p_cfg->dclk_dly_num;
	g_vdpo_info[sel].data_seq_sel  = p_cfg->data_seq_sel;
	vdpo_low_config(sel);
OUT:
	return ret;
}

static s32 vdpo_probe(struct platform_device *pdev)
{
	struct disp_tv_func vdpo_func;
	s32 ret = -1;

	dev_warn(&pdev->dev, "Welcome to vdpo_probe %d\n", g_vdpo_num);

	if (!g_vdpo_num)
		memset(&g_vdpo_info, 0,
		       sizeof(struct drv_vdpo_info_t) * VDPO_NUM);

	if (g_vdpo_num > VDPO_NUM - 1) {
		dev_err(&pdev->dev,
			"g_vdpo_num(%d) is greater then VDPO_NUM-1(%d)\n",
			g_vdpo_num, VDPO_NUM - 1);
		goto OUT;
	}

	pdev->id = of_alias_get_id(pdev->dev.of_node, "vdpo");
	if (pdev->id < 0) {
		dev_err(&pdev->dev, "failed to get alias id\n");
		goto OUT;
	}

	g_vdpo_info[g_vdpo_num].base_addr = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR_OR_NULL(g_vdpo_info[g_vdpo_num].base_addr)) {
		dev_err(&pdev->dev, "fail to get addr for vdpo%d!\n", pdev->id);
		goto ERR_IOMAP;
	}

	g_vdpo_info[g_vdpo_num].clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR_OR_NULL(g_vdpo_info[g_vdpo_num].clk)) {
		dev_err(&pdev->dev, "fail to get clk for vdpo%d's!\n",
			pdev->id);
		goto ERR_IOMAP;
	}

	ret = vdpo_init(pdev);
	if (ret)
		goto ERR_IOMAP;

	if (!g_vdpo_num) {
		memset(&vdpo_func, 0, sizeof(struct disp_tv_func));
		vdpo_func.tv_enable = vdpo_enable;
		vdpo_func.tv_disable = vdpo_disable;
		vdpo_func.tv_set_mode = vdpo_set_mode;
		vdpo_func.tv_get_mode = vdpo_get_mode;
		vdpo_func.tv_get_input_csc = vdpo_get_input_csc;
		vdpo_func.tv_get_video_timing_info = vdpo_get_video_timing_info;
		vdpo_func.tv_mode_support = vdpo_mode_support;
		vdpo_func.tv_irq_enable = vdpo_irq_enable;
		vdpo_func.tv_irq_query = vdpo_irq_query;
		vdpo_func.tv_get_cur_line = vdpo_get_cur_line;
		vdpo_func.vdpo_set_config = vdpo_set_config;
		ret = disp_set_vdpo_func(&vdpo_func);
	} else
		ret = 0;
	++g_vdpo_num;
	if (ret == 0)
		goto OUT;

ERR_IOMAP:
	if (g_vdpo_info[g_vdpo_num].base_addr)
		iounmap((char __iomem *)g_vdpo_info[g_vdpo_num].base_addr);
OUT:
	return ret;
}

s32 vdpo_remove(struct platform_device *pdev)
{
	s32 ret = 0;
	u32 i = 0;

	for (i = 0; i < g_vdpo_num; ++i)
		vdpo_disable(i);

	return ret;
}

static const struct of_device_id sunxi_vdpo_match[] = {
	{
		.compatible = "allwinner,sunxi-vdpo",
	},
	{},
};

struct platform_driver vdpo_driver = {
	.probe = vdpo_probe,
	.remove = vdpo_remove,
	.driver = {
		.name = "vdpo",
		.owner = THIS_MODULE,
		.of_match_table = sunxi_vdpo_match,
	},
};

#if !defined(CONFIG_OF)
static struct resource vdpo_resource[1] = {

	[0] = {
		.start = 0x06880000, .end = 0x06880030, .flags = IORESOURCE_MEM,
	    },
};
struct platform_device vdpo_device = {

	.name = "vdpo",
	.id = -1,
	.num_resources = ARRAY_SIZE(vdpo_resource),
	.resource = vdpo_resource,
	.dev = {}
};
#endif


s32 __init vdpo_module_init(void)
{
	s32 ret = 0;

#if !defined(CONFIG_OF)
	ret = platform_device_register(&vdpo_device);
#endif
#ifdef MODULE
	if (ret == 0)
		ret = platform_driver_register(&vdpo_driver);
#endif
	return ret;
}

static void __exit vdpo_module_exit(void)
{
#ifdef MODULE
	platform_driver_unregister(&vdpo_driver);
#endif
#if !defined(CONFIG_OF)
	platform_device_unregister(&vdpo_device);
#endif
}

late_initcall(vdpo_module_init);
module_exit(vdpo_module_exit);

MODULE_AUTHOR("zhengxiaobin");
MODULE_DESCRIPTION("vdpo driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:vdpo");
