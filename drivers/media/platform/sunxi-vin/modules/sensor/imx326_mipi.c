/*
 * A V4L2 driver for imx326 Raw cameras.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 *    Liang WeiJie <liangweijie@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <linux/io.h>

#include "camera.h"
#include "sensor_helper.h"

MODULE_AUTHOR("lwj");
MODULE_DESCRIPTION("A low-level driver for IMX326 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0x0326

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The IMX326 i2c address
 */
#define I2C_ADDR 0x34

#define SENSOR_NUM 0x2
#define SENSOR_NAME "imx326_mipi"
#define SENSOR_NAME_2 "imx326_mipi_2"

#define DOL_RHS1	8
#define DOL_RATIO	16

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};

static struct regval_list sensor_10b4k30_regs[] = {

	{0x3000, 0x12},

	{0x303E, 0x02},
	{0x3120, 0xF0},
	{0x3121, 0x00},
	{0x3122, 0x02},
	{0x3123, 0x01},
	{0x3129, 0x9C},
	{0x312A, 0x02},
	{0x312D, 0x02},
	{0x3AC4, 0x01},
	{0x310B, 0x00},
	{0x30EE, 0x01},
	{0x3304, 0x32},
	{0x3305, 0x00},
	{0x3306, 0x32},
	{0x3307, 0x00},
	{0x3590, 0x32},
	{0x3591, 0x00},
	{0x3686, 0x32},
	{0x3687, 0x00},
	{0x3045, 0x32},
	{0x301A, 0x00},
	{0x304C, 0x00},
	{0x304D, 0x03},
	{0x331C, 0x1A},
	{0x331D, 0x00},
	{0x3502, 0x02},
	{0x3529, 0x0E},
	{0x352A, 0x0E},
	{0x352B, 0x0E},
	{0x3538, 0x0E},
	{0x3539, 0x0E},
	{0x3553, 0x00},
	{0x357D, 0x05},
	{0x357F, 0x05},
	{0x3581, 0x04},
	{0x3583, 0x76},
	{0x3587, 0x01},
	{0x35BB, 0x0E},
	{0x35BC, 0x0E},
	{0x35BD, 0x0E},
	{0x35BE, 0x0E},
	{0x35BF, 0x0E},
	{0x366E, 0x00},
	{0x366F, 0x00},
	{0x3670, 0x00},
	{0x3671, 0x00},
	{0x3004, 0x01},
	{0x3005, 0x01},
	{0x3006, 0x00},
	{0x3007, 0x02},
	{0x300E, 0x00},
	{0x300F, 0x00},
	{0x3037, 0x00},
	{0x3038, 0x00},
	{0x3039, 0x00},
	{0x303A, 0x00},
	{0x303B, 0x00},
	{0x30DD, 0x00},
	{0x30DE, 0x00},
	{0x30DF, 0x00},
	{0x30E0, 0x00},
	{0x30E1, 0x00},
	{0x30E2, 0x01},
	{0x30F6, 0x10},
	{0x30F7, 0x02},
	{0x30F8, 0xC6},
	{0x30F9, 0x11},
	{0x30FA, 0x00},
	{0x3130, 0x86},
	{0x3131, 0x08},
	{0x3132, 0x7E},
	{0x3133, 0x08},
	{0x3A54, 0x18},
	{0x3A55, 0x0F},
	{0x3342, 0x0A},
	{0x3343, 0x00},
	{0x3344, 0x16},
	{0x3345, 0x00},
	{0x3528, 0x0E},
	{0x3554, 0x1F},
	{0x3555, 0x01},
	{0x3556, 0x01},
	{0x3557, 0x01},
	{0x3558, 0x01},
	{0x3559, 0x00},
	{0x355A, 0x00},
	{0x35BA, 0x0E},
	{0x366A, 0x1B},
	{0x366B, 0x1A},
	{0x366C, 0x19},
	{0x366D, 0x17},
	{0x33A6, 0x01},
	{0x306B, 0x05},
	{0x3A41, 0x08},
	{0x3019, 0x10},

	{0x302E, 0x04},
	{0x302F, 0x00},
	{0x3030, 0x43},
	{0x3031, 0x05},
	{0x3032, 0x08},
	{0x3033, 0x00},
	{0x3041, 0x30},
	{0x3042, 0x08},
	{0x3043, 0x01},
	{0x30E9, 0x00},

	{0x3134, 0x77},
	{0x3135, 0x00},
	{0x3136, 0x67},
	{0x3137, 0x00},
	{0x3138, 0x00},
	{0x3139, 0x00},
	{0x313A, 0x37},
	{0x313B, 0x00},
	{0x313C, 0x57},
	{0x313D, 0x00},
	{0x313E, 0xDF},
	{0x313F, 0x00},
	{0x3140, 0x00},
	{0x3141, 0x00},
	{0x3142, 0x2F},
	{0x3143, 0x00},
	{0x3144, 0x0F},
	{0x3145, 0x00},
	{0x3A85, 0x03},
	{0x3A86, 0x47},
	{0x3A87, 0x00},

	{REG_DLY, 0x10},

	{0x3000, 0x00},

	{0x303E, 0x02},

	{REG_DLY, 0x10},

	{0x30F4, 0x00},
	{0x3018, 0x02},
	{0x300C, 0x0C},
	{0x300D, 0x00},

	{0x300A, 0xA5},
	{0x300B, 0x07},
	{0x3012, 0x03},

};

static struct regval_list sensor_10b4k25_regs[] = {

	{0x3000, 0x12},

	{0x303E, 0x02},
	{0x3120, 0xC8},
	{0x3121, 0x00},
	{0x3122, 0x02},
	{0x3123, 0x01},
	{0x3129, 0x9C},
	{0x312A, 0x02},
	{0x312D, 0x02},
	{0x3AC4, 0x01},
	{0x310B, 0x00},
	{0x30EE, 0x01},
	{0x3304, 0x32},
	{0x3305, 0x00},
	{0x3306, 0x32},
	{0x3307, 0x00},
	{0x3590, 0x32},
	{0x3591, 0x00},
	{0x3686, 0x32},
	{0x3687, 0x00},
	{0x3045, 0x32},
	{0x301A, 0x00},
	{0x304C, 0x00},
	{0x304D, 0x03},
	{0x331C, 0x1A},
	{0x331D, 0x00},
	{0x3502, 0x02},
	{0x3529, 0x0E},
	{0x352A, 0x0E},
	{0x352B, 0x0E},
	{0x3538, 0x0E},
	{0x3539, 0x0E},
	{0x3553, 0x00},
	{0x357D, 0x05},
	{0x357F, 0x05},
	{0x3581, 0x04},
	{0x3583, 0x76},
	{0x3587, 0x01},
	{0x35BB, 0x0E},
	{0x35BC, 0x0E},
	{0x35BD, 0x0E},
	{0x35BE, 0x0E},
	{0x35BF, 0x0E},
	{0x366E, 0x00},
	{0x366F, 0x00},
	{0x3670, 0x00},
	{0x3671, 0x00},
	{0x3004, 0x01},
	{0x3005, 0x01},
	{0x3006, 0x00},
	{0x3007, 0x02},
	{0x300E, 0x00},
	{0x300F, 0x00},
	{0x3037, 0x00},
	{0x3038, 0x00},
	{0x3039, 0x00},
	{0x303A, 0x00},
	{0x303B, 0x00},
	{0x30DD, 0x00},
	{0x30DE, 0x00},
	{0x30DF, 0x00},
	{0x30E0, 0x00},
	{0x30E1, 0x00},
	{0x30E2, 0x01},
	{0x30F6, 0x10},
	{0x30F7, 0x02},
	{0x30F8, 0xC6},
	{0x30F9, 0x11},
	{0x30FA, 0x00},
	{0x3130, 0x86},
	{0x3131, 0x08},
	{0x3132, 0x7E},
	{0x3133, 0x08},
	{0x3A54, 0x18},
	{0x3A55, 0x0F},
	{0x3342, 0x0A},
	{0x3343, 0x00},
	{0x3344, 0x16},
	{0x3345, 0x00},
	{0x3528, 0x0E},
	{0x3554, 0x1F},
	{0x3555, 0x01},
	{0x3556, 0x01},
	{0x3557, 0x01},
	{0x3558, 0x01},
	{0x3559, 0x00},
	{0x355A, 0x00},
	{0x35BA, 0x0E},
	{0x366A, 0x1B},
	{0x366B, 0x1A},
	{0x366C, 0x19},
	{0x366D, 0x17},
	{0x33A6, 0x01},
	{0x306B, 0x05},
	{0x3A41, 0x08},
	{0x3019, 0x10},

	{0x302E, 0x04},
	{0x302F, 0x00},
	{0x3030, 0x43},
	{0x3031, 0x05},
	{0x3032, 0x08},
	{0x3033, 0x00},
	{0x3041, 0x30},
	{0x3042, 0x08},
	{0x3043, 0x01},
	{0x30E9, 0x00},

	{0x3134, 0x77},
	{0x3135, 0x00},
	{0x3136, 0x67},
	{0x3137, 0x00},
	{0x3138, 0x00},
	{0x3139, 0x00},
	{0x313A, 0x37},
	{0x313B, 0x00},
	{0x313C, 0x57},
	{0x313D, 0x00},
	{0x313E, 0xDF},
	{0x313F, 0x00},
	{0x3140, 0x00},
	{0x3141, 0x00},
	{0x3142, 0x2F},
	{0x3143, 0x00},
	{0x3144, 0x0F},
	{0x3145, 0x00},
	{0x3A85, 0x03},
	{0x3A86, 0x47},
	{0x3A87, 0x00},

	{REG_DLY, 0x10},

	{0x3000, 0x00},

	{0x303E, 0x02},

	{REG_DLY, 0x10},

	{0x30F4, 0x00},
	{0x3018, 0x02},
	{0x300C, 0x0C},
	{0x300D, 0x00},

	{0x300A, 0xA5},
	{0x300B, 0x07},
	{0x3012, 0x03},

};

/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */

static struct regval_list sensor_fmt_raw[] = {

};


/*
 * Code for dealing with controls.
 * fill with different sensor module
 * different sensor module has different settings here
 * if not support the follow function ,retrun -EINVAL
 */

static int sensor_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->exp;
	sensor_dbg("sensor_get_exposure = %d\n", info->exp);
	return 0;
}

static int imx326_sensor_vts;
static int imx326_sensor_svr;
static int shutter_delay = 1;
static int shutter_delay_cnt;
static int fps_change_flag;

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, exphigh;
	int shutter, exp_val_m;
	struct sensor_info *info = to_state(sd);

	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE) {
		/*LEF*/
		shutter = imx326_sensor_vts - (exp_val>>4) - 1;
		if (shutter < DOL_RHS1 + 6) {
			shutter = DOL_RHS1 + 6;
			exp_val = (imx326_sensor_vts - shutter - 1) << 4;
		}
		sensor_dbg("long exp_val: %d, shutter: %d\n", exp_val, shutter);

		exphigh = (unsigned char)((0xff00 & shutter) >> 8);
		explow	= (unsigned char)((0x00ff & shutter));

		sensor_write(sd, 0x3030, explow);
		sensor_write(sd, 0x3031, exphigh);

		/*SEF*/
		exp_val_m = exp_val / DOL_RATIO;
		if (exp_val_m < 2 * 16)
			exp_val_m = 2 * 16;
		shutter = DOL_RHS1 - (exp_val_m >> 4) - 1;
		if (shutter < 6)
			shutter = 6;

		sensor_dbg("short exp_val: %d, shutter: %d\n", exp_val_m, shutter);

		exphigh = (unsigned char)((0xff00&shutter)>>8);
		explow = (unsigned char)((0x00ff&shutter));

		sensor_write(sd, 0x302E, explow);
		sensor_write(sd, 0x302F, exphigh);

	} else {
		shutter = imx326_sensor_vts - ((exp_val + 8) >> 4);

		if (shutter > imx326_sensor_vts - 4)
			shutter = imx326_sensor_vts - 4;
		if (shutter < 12)
			shutter = 12;

		exphigh = (unsigned char) ((0xff00 & shutter) >> 8);
		explow = (unsigned char) ((0x00ff & shutter));

		sensor_write(sd, 0x300c, explow);
		sensor_write(sd, 0x300d, exphigh);
		sensor_dbg("sensor_set_exp = %d line Done!\n", exp_val);
	}

	info->exp = exp_val;
	return 0;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->gain;
	sensor_dbg("sensor_get_gain = %d\n", info->gain);
	return 0;
}

static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);
	data_type gainlow = 0;
	data_type gainhigh = 0;
	data_type gaindig = 0;
	int gainana = gain_val;

	if (gainana < 22*16) {
		gaindig = 0;
	} else if (gainana < 22*32) {
		gainana /= 2;
		gaindig = 1;
	} else if (gainana < 22*64) {
		gainana /= 4;
		gaindig = 2;
	} else if (gainana < 22*128) {
		gainana /= 8;
		gaindig = 3;
	} else if (gainana < 22*256) {
		gainana /= 16;
		gaindig = 4;
	} else if (gainana < 22*512) {
		gainana /= 32;
		gaindig = 5;
	} else {
		gainana /= 64;
		gaindig = 6;
	}

	gainana = 2048 - 32768 / gainana;
	gainlow = (unsigned char)(gainana & 0xff);
	gainhigh = (unsigned char)((gainana>>8) & 0x07);

	sensor_write(sd, 0x300a, gainlow);
	sensor_write(sd, 0x300b, gainhigh);
	sensor_write(sd, 0x3012, gaindig);

	sensor_dbg("sensor_set_gain = %d, Done!\n", gain_val);
	info->gain = gain_val;

	return 0;
}

static int sensor_s_exp_gain(struct v4l2_subdev *sd,
			     struct sensor_exp_gain *exp_gain)
{
	struct sensor_info *info = to_state(sd);
	int exp_val, gain_val;

	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	if (gain_val < 1 * 16)
		gain_val = 16;
	if (exp_val > 0xfffff)
		exp_val = 0xfffff;
	if (fps_change_flag) {
		if (shutter_delay_cnt == shutter_delay) {
			sensor_write(sd, 0x30f8, imx326_sensor_vts / (imx326_sensor_svr + 1) & 0xFF);
			sensor_write(sd, 0x30f9, imx326_sensor_vts / (imx326_sensor_svr + 1) >> 8 & 0xFF);
			sensor_write(sd, 0x30fa, imx326_sensor_vts / (imx326_sensor_svr + 1) >> 16);
			sensor_write(sd, 0x302d, 0);
			shutter_delay_cnt = 0;
			fps_change_flag = 0;
		} else
			shutter_delay_cnt++;
	}

	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);

	sensor_dbg("sensor_set_gain exp = %d, %d Done!\n", gain_val, exp_val);

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static int sensor_s_fps(struct v4l2_subdev *sd,
			struct sensor_fps *fps)
{
	data_type rdval1, rdval2, rdval3;
	struct sensor_info *info = to_state(sd);
	struct sensor_win_size *wsize = info->current_wins;

	imx326_sensor_vts = wsize->pclk/fps->fps/wsize->hts;
	fps_change_flag = 1;
	sensor_write(sd, 0x302d, 1);

	sensor_read(sd, 0x30f8, &rdval1);
	sensor_read(sd, 0x30f9, &rdval2);
	sensor_read(sd, 0x30fa, &rdval3);

	sensor_dbg("imx326_sensor_svr: %d, vts: %d.\n", imx326_sensor_svr, (rdval1 | (rdval2<<8) | (rdval3<<16)));
	return 0;
}
static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	data_type rdval;

	ret = sensor_read(sd, 0x3000, &rdval);
	if (ret != 0)
		return ret;

	if (on_off == STBY_ON)
		ret = sensor_write(sd, 0x3000, rdval | 0x01);
	else
		ret = sensor_write(sd, 0x3000, rdval & 0xfe);
	return ret;
}

/*
 * Stuff that knows about the sensor.
 */
static int sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;

	switch (on) {
	case STBY_ON:
		sensor_dbg("STBY_ON!\n");
		cci_lock(sd);
		ret = sensor_s_sw_stby(sd, STBY_ON);
		if (ret < 0)
			sensor_err("soft stby falied!\n");
		usleep_range(1000, 1200);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		sensor_dbg("STBY_OFF!\n");
		cci_lock(sd);
		usleep_range(1000, 1200);
		ret = sensor_s_sw_stby(sd, STBY_OFF);
		if (ret < 0)
			sensor_err("soft stby off falied!\n");
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_dbg("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_set_status(sd, POWER_EN, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		vin_set_pmu_channel(sd, IOVDD, ON);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(100, 120);
		vin_set_mclk(sd, ON);
		usleep_range(100, 120);
		vin_set_mclk_freq(sd, MCLK);
		usleep_range(3000, 3200);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_dbg("PWR_OFF!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_set_mclk(sd, OFF);
		vin_set_pmu_channel(sd, AFVDD, OFF);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_gpio_set_status(sd, RESET, 0);
		vin_gpio_set_status(sd, PWDN, 0);
		vin_gpio_set_status(sd, POWER_EN, 0);
		cci_unlock(sd);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
	switch (val) {
	case 0:
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(100, 120);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(100, 120);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type rdval = 0;

	sensor_read(sd, 0x3008, &rdval);
	sensor_dbg("%s read value is 0x%x\n", __func__, rdval);
	sensor_read(sd, 0x3004, &rdval);
	sensor_dbg("%s read value is 0x%x\n", __func__, rdval);
	sensor_read(sd, 0x3005, &rdval);
	sensor_dbg("%s read value is 0x%x\n", __func__, rdval);
	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	sensor_dbg("sensor_init\n");

	/*Make sure it is a target sensor */
	ret = sensor_detect(sd);
	if (ret) {
		sensor_err("chip found is not an target chip.\n");
		return ret;
	}

	info->focus_status = 0;
	info->low_speed = 0;
	info->width = HD1080_WIDTH;
	info->height = HD1080_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30;	/* 30fps */

	return 0;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;
	struct sensor_info *info = to_state(sd);

	switch (cmd) {
	case GET_CURRENT_WIN_CFG:
		if (info->current_wins != NULL) {
			memcpy(arg, info->current_wins,
				sizeof(struct sensor_win_size));
			ret = 0;
		} else {
			sensor_err("empty wins!\n");
			ret = -1;
		}
		break;
	case SET_FPS:
		ret = 0;
		break;
	case VIDIOC_VIN_SENSOR_EXP_GAIN:
		ret = sensor_s_exp_gain(sd, (struct sensor_exp_gain *)arg);
		break;
	case VIDIOC_VIN_SENSOR_SET_FPS:
		ret = sensor_s_fps(sd, (struct sensor_fps *)arg);
		break;
	case VIDIOC_VIN_SENSOR_CFG_REQ:
		sensor_cfg_req(sd, (struct sensor_config *)arg);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

/*
 * Store information about the video data format.
 */
static struct sensor_format_struct sensor_formats[] = {
	{
		.desc = "Raw RGB Bayer",
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.regs = sensor_fmt_raw,
		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
		.bpp = 1
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {

	{
	 .width = 2880,
	 .height = 2160,
	 .hoffset = 480,
	 .voffset = 0,
	 .hts = 4224,
	 .vts = 4550,
	 .pclk = 576 * 1000 * 1000,
	 .mipi_bps = 720 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 4 << 4,
	 .intg_max = (4550 - 12) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_10b4k30_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b4k30_regs),
	 .set_size = NULL,
	 },

	{
	 .width = 2880,
	 .height = 2160,
	 .hoffset = 480,
	 .voffset = 0,
	 .hts = 5064,
	 .vts = 2275,
	 .pclk = 288 * 1000 * 1000,
	 .mipi_bps = 576 * 1000 * 1000,
	 .fps_fixed = 25,
	 .bin_factor = 1,
	 .intg_min = 4 << 4,
	 .intg_max = (2275 - 12) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_10b4k25_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b4k25_regs),
	 .set_size = NULL,
	 },

	{
	 .width = 3072,
	 .height = 1728,
	 .hoffset = 384,
	 .voffset = 216,
	 .hts = 4224,
	 .vts = 4550,
	 .pclk = 576 * 1000 * 1000,
	 .mipi_bps = 720 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 4 << 4,
	 .intg_max = (4550 - 12) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .vipp_hoff = 8,
	 .vipp_voff = 8,
	 .regs = sensor_10b4k30_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b4k30_regs),
	 .set_size = NULL,
	 },

	{
	 .width = 3072,
	 .height = 1728,
	 .hoffset = 384,
	 .voffset = 216,
	 .hts = 5064,
	 .vts = 2275,
	 .pclk = 288 * 1000 * 1000,
	 .mipi_bps = 576 * 1000 * 1000,
	 .fps_fixed = 25,
	 .bin_factor = 1,
	 .intg_min = 4 << 4,
	 .intg_max = (2275 - 12) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .vipp_hoff = 8,
	 .vipp_voff = 8,
	 .regs = sensor_10b4k25_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b4k25_regs),
	 .set_size = NULL,
	 },
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct sensor_info *info = to_state(sd);

	cfg->type = V4L2_MBUS_CSI2_DPHY;
	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE)
		cfg->flags = 0 | V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0 | V4L2_MBUS_CSI2_CHANNEL_1;
	else
		cfg->flags = 0 | V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0;

	return 0;
}

static int sensor_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_info *info =
			container_of(ctrl->handler, struct sensor_info, handler);
	struct v4l2_subdev *sd = &info->sd;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return sensor_g_gain(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE:
		return sensor_g_exp(sd, &ctrl->val);
	}
	return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_info *info =
			container_of(ctrl->handler, struct sensor_info, handler);
	struct v4l2_subdev *sd = &info->sd;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return sensor_s_gain(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return sensor_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static int sensor_reg_init(struct sensor_info *info)
{
	int ret;
	data_type rdval_l, rdval_h;
	struct v4l2_subdev *sd = &info->sd;
	struct sensor_format_struct *sensor_fmt = info->fmt;
	struct sensor_win_size *wsize = info->current_wins;

	ret = sensor_write_array(sd, sensor_default_regs,
				 ARRAY_SIZE(sensor_default_regs));
	if (ret < 0) {
		sensor_err("write sensor_default_regs error\n");
		return ret;
	}

	sensor_dbg("sensor_reg_init\n");

	sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	if (wsize->regs)
		sensor_write_array(sd, wsize->regs, wsize->regs_size);

	if (wsize->set_size)
		wsize->set_size(sd);

	info->width = wsize->width;
	info->height = wsize->height;
	imx326_sensor_vts = wsize->vts;
	sensor_read(sd, 0x300E, &rdval_l);
	sensor_read(sd, 0x300F, &rdval_h);
	imx326_sensor_svr = (rdval_h << 8) | rdval_l;
	sensor_dbg("s_fmt set width = %d, height = %d\n", wsize->width,
		     wsize->height);

	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);

	sensor_dbg("%s on = %d, %d*%d fps: %d code: %x\n", __func__, enable,
		     info->current_wins->width, info->current_wins->height,
		     info->current_wins->fps_fixed, info->fmt->mbus_code);

	if (!enable)
		return 0;

	return sensor_reg_init(info);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.g_volatile_ctrl = sensor_g_ctrl,
	.s_ctrl = sensor_s_ctrl,
};

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.reset = sensor_reset,
	.init = sensor_init,
	.s_power = sensor_power,
	.ioctl = sensor_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sensor_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.s_stream = sensor_s_stream,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code = sensor_enum_mbus_code,
	.enum_frame_size = sensor_enum_frame_size,
	.get_fmt = sensor_get_fmt,
	.set_fmt = sensor_set_fmt,
	.get_mbus_config = sensor_get_mbus_config,
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.pad = &sensor_pad_ops,
};

/* ----------------------------------------------------------------------- */
static struct cci_driver cci_drv[] = {
	{
		.name = SENSOR_NAME,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_8,
	}, {
		.name = SENSOR_NAME_2,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_8,
	}
};

static int sensor_init_controls(struct v4l2_subdev *sd, const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 2);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 1600,
			      256 * 1600, 1, 1 * 1600);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 0,
			      65536 * 16, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
	}

	sd->ctrl_handler = handler;

	return ret;
}

static int sensor_dev_id;

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;
	int i;

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	if (client) {
		for (i = 0; i < SENSOR_NUM; i++) {
			if (!strcmp(cci_drv[i].name, client->name))
				break;
		}
		cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv[i]);
	} else {
		cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv[sensor_dev_id++]);
	}

	sensor_init_controls(sd, &sensor_ctrl_ops);

	mutex_init(&info->lock);

#ifdef CONFIG_SAME_I2C
	info->sensor_i2c_addr = I2C_ADDR >> 1;
#endif
	info->fmt = &sensor_formats[0];
	info->fmt_pt = &sensor_formats[0];
	info->win_pt = &sensor_win_sizes[0];
	info->fmt_num = N_FMTS;
	info->win_size_num = N_WIN_SIZES;
	info->sensor_field = V4L2_FIELD_NONE;
	info->combo_mode = CMB_TERMINAL_RES | CMB_PHYA_OFFSET3 | MIPI_NORMAL_MODE;
	info->stream_seq = MIPI_BEFORE_SENSOR;
	info->af_first_flag = 1;
	info->exp = 0;
	info->gain = 0;

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd;
	int i;

	if (client) {
		for (i = 0; i < SENSOR_NUM; i++) {
			if (!strcmp(cci_drv[i].name, client->name))
				break;
		}
		sd = cci_dev_remove_helper(client, &cci_drv[i]);
	} else {
		sd = cci_dev_remove_helper(client, &cci_drv[sensor_dev_id++]);
	}

	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME, 0},
	{}
};

static const struct i2c_device_id sensor_id_2[] = {
	{SENSOR_NAME_2, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sensor_id);
MODULE_DEVICE_TABLE(i2c, sensor_id_2);

static struct i2c_driver sensor_driver[] = {
	{
		.driver = {
			   .owner = THIS_MODULE,
			   .name = SENSOR_NAME,
			   },
		.probe = sensor_probe,
		.remove = sensor_remove,
		.id_table = sensor_id,
	}, {
		.driver = {
			   .owner = THIS_MODULE,
			   .name = SENSOR_NAME_2,
			   },
		.probe = sensor_probe,
		.remove = sensor_remove,
		.id_table = sensor_id_2,
	},
};
static __init int init_sensor(void)
{
	int i, ret = 0;

	sensor_dev_id = 0;

	for (i = 0; i < SENSOR_NUM; i++)
		ret = cci_dev_init_helper(&sensor_driver[i]);

	return ret;
}

static __exit void exit_sensor(void)
{
	int i;

	sensor_dev_id = 0;

	for (i = 0; i < SENSOR_NUM; i++)
		cci_dev_exit_helper(&sensor_driver[i]);
}

module_init(init_sensor);
module_exit(exit_sensor);
