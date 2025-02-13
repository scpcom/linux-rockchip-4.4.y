/*
 * A V4L2 driver for imx179_mipi Raw cameras.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
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
MODULE_DESCRIPTION("A low-level driver for IMX179 mipi sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR  0x0179

#define DGAIN_R  0x0100
#define DGAIN_G  0x0100
#define DGAIN_B  0x0100


#define SENSOR_SIZE_MORE 0
/*
 * Our nominal (default) frame rate.
 */
#define SENSOR_FRAME_RATE 30

/*
 * The IMX179 i2c address
 */
#define I2C_ADDR 0x20

#define SENSOR_NUM 0X02
#define SENSOR_NAME "imx179_mipi"
#define SENSOR_NAME_2 "imx179_mipi_2"

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {
/* sensor_1280_1080_30_regs and sensor_1080p30_regs do not need it */
#if 0
	{0x0100, 0x00},
	{0x0101, 0x00},

	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0305, 0x06},
	{0x0309, 0x05},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0xA2},

	{0x0342, 0x0D},
	{0x0343, 0x70},
	{0x0344, 0x00},

	{0x0348, 0x0C},

	{0x0383, 0x01},
	{0x0387, 0x01},

	{0x0401, 0x00},
	{0x0405, 0x10},
	{0x3020, 0x10},
	{0x3041, 0x15},
	{0x3042, 0x87},
	{0x3089, 0x4F},
	{0x3309, 0x9A},
	{0x3344, 0x57},
	{0x3345, 0x1F},
	{0x3362, 0x0A},
	{0x3363, 0x0A},
	{0x3364, 0x00},
	{0x3368, 0x18},
	{0x3369, 0x00},
	{0x3370, 0x77},
	{0x3371, 0x2F},
	{0x3372, 0x4F},
	{0x3373, 0x2F},
	{0x3374, 0x2F},
	{0x3375, 0x37},
	{0x3376, 0x9F},
	{0x3377, 0x37},
	{0x33C8, 0x00},

	{0x4100, 0x0E},
	{0x4108, 0x00},
	{0x4109, 0x1C},
	{0x0100, 0x01},
#endif
};

#if SENSOR_SIZE_MORE
/* for capture */
static struct regval_list sensor_8M15_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},

	{0x0202, 0x13},
	{0x0203, 0x9B},

	{0x0340, 0x13},
	{0x0341, 0x9F},

	{0x0345, 0x08},
	{0x0346, 0x00},
	{0x0347, 0x08},
	{0x0348, 0x0C},
	{0x0349, 0xC7},
	{0x034A, 0x09},
	{0x034B, 0x97},
	{0x034C, 0x0C},
	{0x034D, 0xC0},
	{0x034E, 0x09},
	{0x034F, 0x90},

	{0x0390, 0x00},

	{0x33D4, 0x0C},
	{0x33D5, 0xC0},
	{0x33D6, 0x09},
	{0x33D7, 0x90},

	{0x0100, 0x01},
};

static struct regval_list sensor_8M25_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x0B},
	{0x0203, 0xC2},

	{0x0340, 0x0B},
	{0x0341, 0xC6},

	{0x0345, 0x08},
	{0x0346, 0x00},
	{0x0347, 0x08},
	{0x0348, 0x0C},
	{0x0349, 0xC7},
	{0x034A, 0x09},
	{0x034B, 0x97},
	{0x034C, 0x0C},
	{0x034D, 0xC0},
	{0x034E, 0x09},
	{0x034F, 0x90},

	{0x0390, 0x00},

	{0x33D4, 0x0C},
	{0x33D5, 0xC0},
	{0x33D6, 0x09},
	{0x33D7, 0x90},

	{0x0100, 0x01},
};

static struct regval_list sensor_8M30_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x99},
	{0x0203, 0xCC},

	{0x0340, 0x09},
	{0x0341, 0xD0},

	{0x0345, 0x08},
	{0x0346, 0x00},
	{0x0347, 0x08},
	{0x0348, 0x0C},
	{0x0349, 0xC7},
	{0x034A, 0x09},
	{0x034B, 0x97},
	{0x034C, 0x0C},
	{0x034D, 0xC0},
	{0x034E, 0x09},
	{0x034F, 0x90},

	{0x0390, 0x00},

	{0x33D4, 0x0C},
	{0x33D5, 0xC0},
	{0x33D6, 0x09},
	{0x33D7, 0x90},

	{0x0100, 0x01},
};
#endif

static struct regval_list sensor_1280_1080_30_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x04},
	{0x0203, 0xE4},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0305, 0x06},
	{0x0309, 0x05},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0x5d},
	{0x0340, 0x05},
	{0x0341, 0x90},
	{0x0342, 0x0D},
	{0x0343, 0x70},
	{0x0344, 0x00},
	{0x0345, 0xc8},
	{0x0346, 0x00},
	{0x0347, 0x08},
	{0x0348, 0x0C},
	{0x0349, 0x07},
	{0x034A, 0x09},
	{0x034B, 0x97},
	{0x034C, 0x05},
	{0x034D, 0x00},
	{0x034E, 0x04},
	{0x034F, 0x38},
	{0x0383, 0x01},
	{0x0387, 0x01},
	{0x0390, 0x01},
	{0x0401, 0x02},
	{0x0405, 0x12},
	{0x3020, 0x10},
	{0x3041, 0x15},
	{0x3042, 0x87},
	{0x3089, 0x4F},
	{0x3309, 0x9A},
	{0x3344, 0x57},
	{0x3345, 0x1F},
	{0x3362, 0x0A},
	{0x3363, 0x0A},
	{0x3364, 0x00},
	{0x3368, 0x18},
	{0x3369, 0x00},
	{0x3370, 0x77},
	{0x3371, 0x2F},
	{0x3372, 0x4F},
	{0x3373, 0x2F},
	{0x3374, 0x2F},
	{0x3375, 0x37},
	{0x3376, 0x9F},
	{0x3377, 0x37},
	{0x33C8, 0x00},
	{0x33D4, 0x05},
	{0x33D5, 0xa0},
	{0x33D6, 0x04},
	{0x33D7, 0xc8},
	{0x4100, 0x0E},
	{0x4108, 0x01},
	{0x4109, 0x7C},
	{0x0100, 0x01},
};

#if SENSOR_SIZE_MORE
static struct regval_list sensor_1080p30_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x07},
	{0x0203, 0x38},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0305, 0x06},
	{0x0309, 0x05},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0x76},
	{0x0340, 0x07},
	{0x0341, 0x3c},
	{0x0342, 0x0D},
	{0x0343, 0x70},
	{0x0344, 0x00},
	{0x0345, 0x14},
	{0x0346, 0x01},
	{0x0347, 0x3a},
	{0x0348, 0x0C},
	{0x0349, 0xbb},
	{0x034A, 0x08},
	{0x034B, 0x65},
	{0x034C, 0x07},
	{0x034D, 0x80},
	{0x034E, 0x04},
	{0x034F, 0x38},
	{0x0383, 0x01},
	{0x0387, 0x01},
	{0x0390, 0x00},
	{0x0401, 0x02},
	{0x0405, 0x1b},
	{0x3020, 0x10},
	{0x3041, 0x15},
	{0x3042, 0x87},
	{0x3089, 0x4F},
	{0x3309, 0x9A},
	{0x3344, 0x57},
	{0x3345, 0x1F},
	{0x3362, 0x0A},
	{0x3363, 0x0A},
	{0x3364, 0x00},
	{0x3368, 0x18},
	{0x3369, 0x00},
	{0x3370, 0x77},
	{0x3371, 0x2F},
	{0x3372, 0x4F},
	{0x3373, 0x2F},
	{0x3374, 0x2F},
	{0x3375, 0x37},
	{0x3376, 0x9F},
	{0x3377, 0x37},
	{0x33C8, 0x00},
	{0x33D4, 0x0c},
	{0x33D5, 0xa8},
	{0x33D6, 0x07},
	{0x33D7, 0x2c},
	{0x4100, 0x0E},
	{0x4108, 0x01},
	{0x4109, 0x7C},
	{0x0100, 0x01},

};

static struct regval_list sensor_1080p60_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x04},
	{0x0203, 0xE4},

	{0x0340, 0x04},
	{0x0341, 0xE8},

	{0x0345, 0x30},
	{0x0346, 0x02},
	{0x0347, 0x70},
	{0x0348, 0x0A},
	{0x0349, 0x9F},
	{0x034A, 0x07},
	{0x034B, 0x2F},
	{0x034C, 0x07},
	{0x034D, 0x80},
	{0x034E, 0x04},
	{0x034F, 0x38},

	{0x0390, 0x00},


	{0x33D4, 0x08},
	{0x33D5, 0x70},
	{0x33D6, 0x04},
	{0x33D7, 0xC0},

	{0x0100, 0x01},
};

static struct regval_list sensor_FOV60fps_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x04},
	{0x0203, 0xE4},

	{0x0340, 0x04},
	{0x0341, 0xE8},

	{0x0345, 0x08},
	{0x0346, 0x01},
	{0x0347, 0x3A},
	{0x0348, 0x0C},
	{0x0349, 0xC7},
	{0x034A, 0x08},
	{0x034B, 0x65},
	{0x034C, 0x06},
	{0x034D, 0x60},
	{0x034E, 0x03},
	{0x034F, 0x96},

	{0x0390, 0x01},

	{0x33D4, 0x06},
	{0x33D5, 0x60},
	{0x33D6, 0x03},
	{0x33D7, 0x96},

	{0x0100, 0x01},
};

static struct regval_list sensor_binning_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x09},
	{0x0203, 0xCC},

	{0x0340, 0x09},
	{0x0341, 0xD0},

	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0C},
	{0x0349, 0xCF},
	{0x034A, 0x09},
	{0x034B, 0x9F},
	{0x034C, 0x06},
	{0x034D, 0x68},
	{0x034E, 0x04},
	{0x034F, 0xD0},

	{0x0390, 0x01},

	{0x33D4, 0x06},
	{0x33D5, 0x68},
	{0x33D6, 0x04},
	{0x33D7, 0xD0},

	{0x0100, 0x01},
};

static struct regval_list sensor_720p90_regs[] = {
	{0x0100, 0x00},
	{0x0101, 0x00},
	{0x0202, 0x03},
	{0x0203, 0x41},

	{0x0340, 0x03},
	{0x0341, 0x45},

	{0x0345, 0xC8},
	{0x0346, 0x01},
	{0x0347, 0xA6},
	{0x0348, 0x0C},
	{0x0349, 0x07},
	{0x034A, 0x07},
	{0x034B, 0xF9},
	{0x034C, 0x05},
	{0x034D, 0xA0},
	{0x034E, 0x03},
	{0x034F, 0x2A},

	{0x0390, 0x01},

	{0x33D4, 0x05},
	{0x33D5, 0xA0},
	{0x33D6, 0x03},
	{0x33D7, 0x2A},

	{0x0100, 0x01},
};
#endif

/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
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

static int imx179_mipi_sensor_vts;
static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, exphigh;
	struct sensor_info *info = to_state(sd);

	if (exp_val > 0xffff)
		exp_val = 0xffff;
	if (exp_val < 16)
		exp_val = 16;

	exp_val = (exp_val + 8) >> 4;	/*rounding to 1 */

	exphigh = (unsigned char)((0xff00 & exp_val) >> 8);
	explow = (unsigned char)((0x00ff & exp_val));

	sensor_write(sd, 0x0203, explow);	/*coarse integration time */
	sensor_write(sd, 0x0202, exphigh);
	sensor_dbg("sensor_set_exp = %d line Done!\n", exp_val);

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
	unsigned char gainlow = 0;
	unsigned char gainhigh = 0;
	long gainr = 0, gaing = 0, gainb = 0, gain_digi = 0;
	int gainana = 0;

	if (gain_val <= 128) {
		gainana = 256 - 4096 / gain_val;
		gain_digi = 256;
	} else {
		gainana = 224;
		gain_digi = gain_val * 2;
	}

	gainlow = (unsigned char)(gainana & 0xff);
	gainhigh = (unsigned char)((gainana >> 8) & 0xff);
	sensor_write(sd, 0x0205, gainlow);
	sensor_write(sd, 0x0204, gainhigh);

	gainr = (gain_digi * DGAIN_R) >> 8;
	gaing = (gain_digi * DGAIN_G) >> 8;
	gainb = (gain_digi * DGAIN_B) >> 8;
	sensor_write(sd, 0x020F, gaing & 0xff);
	sensor_write(sd, 0x020E, gaing >> 8);
	sensor_write(sd, 0x0211, gainr & 0xff);
	sensor_write(sd, 0x0210, gainr >> 8);
	sensor_write(sd, 0x0213, gainb & 0xff);
	sensor_write(sd, 0x0212, gainb >> 8);
	sensor_write(sd, 0x0215, gaing & 0xff);
	sensor_write(sd, 0x0214, gaing >> 8);

	sensor_dbg("sensor_set_gain = %d, Done!\n", gain_val);
	info->gain = gain_val;

	return 0;
}

static int sensor_s_exp_gain(struct v4l2_subdev *sd,
			     struct sensor_exp_gain *exp_gain)
{
	int exp_val, gain_val, shutter, frame_length;
	struct sensor_info *info = to_state(sd);

	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	if (gain_val < 1 * 16)
		gain_val = 16;
	if (gain_val > 64 * 16 - 1)
		gain_val = 64 * 16 - 1;

	if (exp_val > 0xfffff)
		exp_val = 0xfffff;

	shutter = exp_val / 16;
	if (shutter > imx179_mipi_sensor_vts - 4)
		frame_length = shutter;
	else
		frame_length = imx179_mipi_sensor_vts;

	sensor_write(sd, 0x0341, (frame_length & 0xff));
	sensor_write(sd, 0x0340, (frame_length >> 8));

	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);

	sensor_dbg("sensor_set_gain exp = %d, %d Done!\n", gain_val, exp_val);

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	data_type rdval;

	ret = sensor_read(sd, 0x0100, &rdval);
	if (ret != 0)
		return ret;

	if (on_off == STBY_ON)
		ret = sensor_write(sd, 0x0100, rdval & 0xfe);
	else
		ret = sensor_write(sd, 0x0100, rdval | 0x01);

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
		usleep_range(10000, 12000);
		cci_unlock(sd);
		vin_set_mclk(sd, OFF);
		break;
	case STBY_OFF:
		sensor_dbg("STBY_OFF!\n");
		cci_lock(sd);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		ret = sensor_s_sw_stby(sd, STBY_OFF);
		if (ret < 0)
			sensor_err("soft stby off falied!\n");
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_print("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_set_status(sd, POWER_EN, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_set_pmu_channel(sd, IOVDD, ON);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		vin_set_pmu_channel(sd, DVDD, ON);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(10000, 12000);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_print("PWR_OFF!\n");
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
		usleep_range(10000, 12000);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(10000, 12000);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type rdval = 0;

	sensor_read(sd, 0x0002, &rdval);
	if ((rdval & 0x0f) != 0x01)
		return -ENODEV;

	sensor_read(sd, 0x0003, &rdval);
	if (rdval != 0x79)
		return -ENODEV;

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
	info->width = 3264;
	info->height = 2448;
	info->hflip = 0;
	info->vflip = 0;
	info->exp = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30;	/* 30fps */

	ret = sensor_write_array(sd, sensor_default_regs,
				 ARRAY_SIZE(sensor_default_regs));
	if (ret < 0) {
		sensor_err("write sensor_default_regs error\n");
		return ret;
	}

	info->preview_first_flag = 1;

	return 0;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
			 void *arg)
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
		ret = sensor_s_exp_gain(sd, (struct sensor_exp_gain *) arg);
		break;
	case VIDIOC_VIN_SENSOR_CFG_REQ:
		sensor_cfg_req(sd, (struct sensor_config *) arg);
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
		.desc		= "Raw RGB Bayer",
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.regs		= sensor_fmt_raw,
		.regs_size	= ARRAY_SIZE(sensor_fmt_raw),
		.bpp		= 1
	},
};

#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {
#if SENSOR_SIZE_MORE
	/* Fullsize: 3264*2448 */
	{
		.width = 3264,
		.height = 2448,
		.hoffset = 0,
		.voffset = 0,
		.hts = 3440,
		.vts = 5023,
		.pclk = 260 * 1000 * 1000,
		.mipi_bps = 651 * 1000 * 1000,
		.fps_fixed = 10,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (5023 - 4) << 4,
		.gain_min = 16,
		.gain_max = (16 << 4),
		.regs = sensor_8M15_regs,
		.regs_size = ARRAY_SIZE(sensor_8M15_regs),
		.set_size = NULL,
	},

	{
		.width = 3264,
		.height = 2448,
		.hoffset = 0,
		.voffset = 0,
		.hts = 3440,
		.vts = 3436,
		.pclk = 260 * 1000 * 1000,
		.mipi_bps = 649 * 1000 * 1000,
		.fps_fixed = 25,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (3436 - 4) << 4,
		.gain_min = 16,
		.gain_max = (16 << 4),
		.regs = sensor_8M25_regs,
		.regs_size = ARRAY_SIZE(sensor_8M25_regs),
		.set_size = NULL,
	},

	{
		.width = 2288,
		.height = 2288,
		.hoffset = 488,
		.voffset = 80,
		.hts = 3440,
		.vts = 2512,
		.pclk = 260 * 1000 * 1000,
		.mipi_bps = 648 * 1000 * 1000,
		.fps_fixed = 30,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (2512 - 4) << 4,
		.gain_min = 16,
		.gain_max = (16 << 4),
		.regs = sensor_8M30_regs,
		.regs_size = ARRAY_SIZE(sensor_8M30_regs),
		.set_size = NULL,
	},

	{
		.width = 1920,
		.height = 1080,
		.hoffset = 0,
		.voffset = 0,
		.hts = 3440,
		.vts = 1852,
		.pclk = 192 * 1000 * 1000,
		.mipi_bps = 156 * 1000 * 1000,
		.fps_fixed = 30,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (1852 - 4) << 4,
		.gain_min = 16,
		.gain_max = (16 << 4),
		.regs = sensor_1080p30_regs,
		.regs_size = ARRAY_SIZE(sensor_1080p30_regs),
		.set_size = NULL,
	},
#endif

	{
		.width = 1280,
		.height = 1080,
		.hoffset = 0,
		.voffset = 0,
		.hts = 3440,
		.vts = 1424,
		.pclk = 147 * 1000 * 1000,
		.mipi_bps = 104 * 1000 * 1000,
		.fps_fixed = 30,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (1424 - 4) << 4,
		.gain_min = 16,
		.gain_max = (16 << 4),
		.regs = sensor_1280_1080_30_regs,
		.regs_size = ARRAY_SIZE(sensor_1280_1080_30_regs),
		.set_size = NULL,
	},

#if SENSOR_SIZE_MORE
	{
		.width = 1632,
		.height = 918,
		.hoffset = 0,
		.voffset = 0,
		.hts = 3440,
		.vts = 1256,
		.pclk = 260 * 1000 * 1000,
		.mipi_bps = 648 * 1000 * 1000,
		.fps_fixed = 60,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (1256 - 4) << 4,
		.gain_min = 16,
		.gain_max = (32 << 4),
		.regs = sensor_FOV60fps_regs,
		.regs_size = ARRAY_SIZE(sensor_FOV60fps_regs),
		.set_size = NULL,
	},

	{
		.width = 1440,
		.height = 810,
		.hoffset = 0,
		.voffset = 0,
		.hts = 3440,
		.vts = 837,
		.pclk = 260 * 1000 * 1000,
		.mipi_bps = 648 * 1000 * 1000,
		.fps_fixed = 90,
		.bin_factor = 1,
		.intg_min = 16,
		.intg_max = (837 - 4) << 4,
		.gain_min = 1 << 4,
		.gain_max = 32 << 4,
		.regs = sensor_720p90_regs,
		.regs_size = ARRAY_SIZE(sensor_720p90_regs),
		.set_size = NULL,
	},
#endif
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct sensor_info *info = to_state(sd);

	cfg->type = V4L2_MBUS_CSI2;
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
	struct v4l2_subdev *sd = &info->sd;
	struct sensor_format_struct *sensor_fmt = info->fmt;
	struct sensor_win_size *wsize = info->current_wins;
	struct sensor_exp_gain exp_gain;

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
	imx179_mipi_sensor_vts = wsize->vts;

	exp_gain.exp_val = 0;
	exp_gain.gain_val = 0;
	sensor_s_exp_gain(sd, &exp_gain);

	sensor_print("s_fmt set width = %d, height = %d\n",
		     wsize->width, wsize->height);

	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);

	vin_gpio_set_status(sd, SM_VS, 3);
	vin_gpio_set_status(sd, SM_HS, 3);

	sensor_print("%s on = %d, %d*%d %x\n", __func__, enable,
		     info->current_wins->width, info->current_wins->height,
		     info->fmt->mbus_code);

	if (!enable)
		return 0;

	return sensor_reg_init(info);
}

/* ----------------------------------------------------------------------- */
static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.g_volatile_ctrl = sensor_g_ctrl,
	.s_ctrl = sensor_s_ctrl,
};

static const struct v4l2_ctrl_config sensor_custom_ctrls[] = {
	{
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_AUTO_FOCUS_INIT,
		.name = "AutoFocus Initial",
		.type = V4L2_CTRL_TYPE_BUTTON,
		.min = 0,
		.max = 0,
		.step = 0,
		.def = 0,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_AUTO_FOCUS_RELEASE,
		.name = "AutoFocus Release",
		.type = V4L2_CTRL_TYPE_BUTTON,
		.min = 0,
		.max = 0,
		.step = 0,
		.def = 0,
	},
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
	.s_parm = sensor_s_parm,
	.g_parm = sensor_g_parm,
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
	},
	{
		.name = SENSOR_NAME_2,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_8,
	}
};

static int sensor_init_controls(struct v4l2_subdev *sd,
				const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int i;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 2 + ARRAY_SIZE(sensor_custom_ctrls));

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 16,
			  128 * 16 - 1, 1, 1 * 16);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 0,
				 65536 * 16, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	for (i = 0; i < ARRAY_SIZE(sensor_custom_ctrls); i++)
		v4l2_ctrl_new_custom(handler, &sensor_custom_ctrls[i], NULL);

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
	int i = 0;

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
	info->stream_seq = MIPI_BEFORE_SENSOR;
	info->combo_mode = CMB_TERMINAL_RES
			   | CMB_PHYA_OFFSET2 | MIPI_NORMAL_MODE;
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
