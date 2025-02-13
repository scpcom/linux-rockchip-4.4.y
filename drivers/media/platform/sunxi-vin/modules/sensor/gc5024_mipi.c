/*
 * A V4L2 driver for GC5024 Raw cameras.
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
MODULE_DESCRIPTION("A low-level driver for GC5024 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0x5024

#define I2C_ADDR 0x6e

#define SENSOR_NAME "gc5024_mipi"


//define the voltage level of control signal
#define CSI_STBY_ON     1
#define CSI_STBY_OFF    0
#define CSI_RST_ON      0
#define CSI_RST_OFF     1
#define CSI_PWR_ON      1
#define CSI_PWR_OFF     0

//define the registers
#define EXP_HIGH		0xff
#define EXP_MID			0x03
#define EXP_LOW			0x04
#define GAIN_HIGH		0xff
#define GAIN_LOW		0x24
//#define FRACTION_EXP
#define ID_REG_HIGH		0xf0
#define ID_REG_LOW		0xf1
#define ID_VAL_HIGH		((V4L2_IDENT_SENSOR) >> 8)
#define ID_VAL_LOW		((V4L2_IDENT_SENSOR) & 0xff)


#define IMAGE_V_MIRROR

#ifdef IMAGE_NORMAL_MIRROR
#define MIRROR		  0xd4
#define PH_SWITCH	  0x1b
#define BLK_VAL_H	  0x3c
#define BLK_VAL_L	  0x00
#define STARTX		  0x0d
#define STARTY		  0x03
#endif

#ifdef IMAGE_H_MIRROR
#define MIRROR		  0xd5
#define PH_SWITCH	  0x1a
#define BLK_VAL_H	  0x3c
#define BLK_VAL_L	  0x00
#define STARTX		  0x02
#define STARTY		  0x03
#endif

#ifdef IMAGE_V_MIRROR
#define MIRROR		  0xd6
#define PH_SWITCH	  0x1b
#define BLK_VAL_H	  0x00
#define BLK_VAL_L	  0x3c
#define STARTX		  0x0d
#define STARTY		  0x02
#endif

#ifdef IMAGE_HV_MIRROR
#define MIRROR		  0xd7
#define PH_SWITCH	  0x1a
#define BLK_VAL_H	  0x00
#define BLK_VAL_L	  0x3c
#define STARTX		  0x02
#define STARTY		  0x02
#endif



static int sensor_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->exp;
	sensor_dbg("sensor_get_exposure = %d\n", info->exp);
	return 0;
}

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	unsigned char explow, expmid, exphigh;
	unsigned int all_exp;
	struct sensor_info *info = to_state(sd);

	all_exp = exp_val >> 4;

	if (all_exp > 0x3fff)
		all_exp = 0x3fff;
	if (all_exp < 7)
		all_exp = 7;


	if (all_exp <= 20) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x32, 0x09);
		sensor_write(sd, 0xb0, 0x53);
	} else {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x32, 0x49);
		sensor_write(sd, 0xb0, 0x4b);
	}

	exphigh = 0;
	expmid  = (unsigned char)((0x003f00&all_exp)>>8);
	explow  = (unsigned char)((0x0000ff&all_exp));

	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, EXP_HIGH, exphigh);
	sensor_write(sd, EXP_MID, expmid);
	sensor_write(sd, EXP_LOW, explow);

	info->exp = all_exp;
	return 0;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->gain;
	sensor_dbg("sensor_get_gain = %d\n", info->gain);
	return 0;
}

#define ANALOG_GAIN_1 64  /*1.00x*/
#define ANALOG_GAIN_2 88  /*1.37x*/
#define ANALOG_GAIN_3 121 /*1.89x*/
#define ANALOG_GAIN_4 168 /*2.63x*/
#define ANALOG_GAIN_5 239 /*3.73x*/
#define ANALOG_GAIN_6 330 /*5.15x*/
#define ANALOG_GAIN_7 470 /*7.34x*/
static int sensor_s_gain(struct v4l2_subdev *sd, unsigned int gain_val)
{
	struct sensor_info *info = to_state(sd);
	unsigned char tmp;
	unsigned int All_gain;

	All_gain = gain_val*4;

	if (All_gain < 0x40)
		All_gain = 0x40;
	else if ((All_gain >= ANALOG_GAIN_1) && (All_gain < ANALOG_GAIN_2)) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x0f);
		sensor_write(sd, 0x29, 0x0f);
		sensor_write(sd, 0xe8, 0x03);
		sensor_write(sd, 0xe9, 0x01);
		sensor_write(sd, 0xea, 0x02);
		sensor_write(sd, 0xeb, 0x02);
		sensor_write(sd, 0xec, 0x03);
		sensor_write(sd, 0xed, 0x01);
		sensor_write(sd, 0xee, 0x02);
		sensor_write(sd, 0xef, 0x02);
		sensor_write(sd, 0xb6, 0x00);
		tmp = All_gain;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	} else if ((All_gain >= ANALOG_GAIN_2) && (All_gain < ANALOG_GAIN_3)) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x0f);
		sensor_write(sd, 0x29, 0x0f);
		sensor_write(sd, 0xe8, 0x03);
		sensor_write(sd, 0xe9, 0x01);
		sensor_write(sd, 0xea, 0x02);
		sensor_write(sd, 0xeb, 0x02);
		sensor_write(sd, 0xec, 0x03);
		sensor_write(sd, 0xed, 0x01);
		sensor_write(sd, 0xee, 0x02);
		sensor_write(sd, 0xef, 0x02);
		sensor_write(sd, 0xb6,	0x01);
		tmp = 64*All_gain/ANALOG_GAIN_2;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	} else if ((All_gain >= ANALOG_GAIN_3) && (All_gain < ANALOG_GAIN_4)) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x0b);
		sensor_write(sd, 0x29, 0x1b);
		sensor_write(sd, 0xe8, 0x03);
		sensor_write(sd, 0xe9, 0x01);
		sensor_write(sd, 0xea, 0x02);
		sensor_write(sd, 0xeb, 0x02);
		sensor_write(sd, 0xec, 0x03);
		sensor_write(sd, 0xed, 0x01);
		sensor_write(sd, 0xee, 0x02);
		sensor_write(sd, 0xef, 0x02);

		sensor_write(sd, 0xb6,	0x02);
		tmp = 64*All_gain/ANALOG_GAIN_3;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	} else if ((All_gain >= ANALOG_GAIN_4) && (All_gain < ANALOG_GAIN_5)) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x0d);
		sensor_write(sd, 0x29, 0x1d);
		sensor_write(sd, 0xe8, 0x02);
		sensor_write(sd, 0xe9, 0x02);
		sensor_write(sd, 0xea, 0x02);
		sensor_write(sd, 0xeb, 0x02);
		sensor_write(sd, 0xec, 0x02);
		sensor_write(sd, 0xed, 0x02);
		sensor_write(sd, 0xee, 0x02);
		sensor_write(sd, 0xef, 0x02);
		sensor_write(sd, 0xb6,	0x03);
		tmp = 64*All_gain/ANALOG_GAIN_4;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	} else if ((All_gain >= ANALOG_GAIN_5) && (All_gain < ANALOG_GAIN_6)) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x08);
		sensor_write(sd, 0x29, 0x38);
		sensor_write(sd, 0xe8, 0x03);
		sensor_write(sd, 0xe9, 0x03);
		sensor_write(sd, 0xea, 0x03);
		sensor_write(sd, 0xeb, 0x03);
		sensor_write(sd, 0xec, 0x03);
		sensor_write(sd, 0xed, 0x03);
		sensor_write(sd, 0xee, 0x03);
		sensor_write(sd, 0xef, 0x03);
		sensor_write(sd, 0xb6,	0x04);
		tmp = 64*All_gain/ANALOG_GAIN_5;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	} else if ((All_gain >= ANALOG_GAIN_6) && (All_gain < ANALOG_GAIN_7)) {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x08);
		sensor_write(sd, 0x29, 0x38);
		sensor_write(sd, 0xe8, 0x03);
		sensor_write(sd, 0xe9, 0x03);
		sensor_write(sd, 0xea, 0x03);
		sensor_write(sd, 0xeb, 0x03);
		sensor_write(sd, 0xec, 0x03);
		sensor_write(sd, 0xed, 0x03);
		sensor_write(sd, 0xee, 0x03);
		sensor_write(sd, 0xef, 0x03);
		sensor_write(sd, 0xb6,	0x05);
		tmp = 64*All_gain/ANALOG_GAIN_6;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	} else {
		sensor_write(sd, 0xfe, 0x00);
		sensor_write(sd, 0x21, 0x08);
		sensor_write(sd, 0x29, 0x38);
		sensor_write(sd, 0xe8, 0x03);
		sensor_write(sd, 0xe9, 0x03);
		sensor_write(sd, 0xea, 0x03);
		sensor_write(sd, 0xeb, 0x03);
		sensor_write(sd, 0xec, 0x03);
		sensor_write(sd, 0xed, 0x03);
		sensor_write(sd, 0xee, 0x03);
		sensor_write(sd, 0xef, 0x03);
		//analog gain
		sensor_write(sd, 0xb6,	0x06);
		tmp = 64*All_gain/ANALOG_GAIN_7;
		sensor_write(sd, 0xb1, tmp>>6);
		sensor_write(sd, 0xb2, (tmp<<2)&0xfc);
	}

	info->gain = All_gain;

	return 0;
}



static int sensor_s_exp_gain(struct v4l2_subdev *sd,
				struct sensor_exp_gain *exp_gain)
{
	int exp_val, gain_val;
	struct sensor_info *info = to_state(sd);


	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	if (gain_val < 1*16)
		gain_val = 16;

	if (gain_val > 64*16-1)
		gain_val = 64*16-1;

	if (exp_val > 0xfffff)
		exp_val = 0xfffff;

	if (exp_val < 7)
		exp_val = 7;

	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);

	info->exp = exp_val;
	info->gain = gain_val;

	return 0;
}

/*
 * Stuff that knows about the sensor.
 */
static int sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;

	switch (on) {
	case STBY_ON:
		if (ret < 0)
			usleep_range(10000, 12000);
		cci_lock(sd);
		vin_gpio_write(sd, PWDN, CSI_STBY_ON);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		cci_lock(sd);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, PWDN, CSI_STBY_OFF);
		usleep_range(10000, 12000);
		cci_unlock(sd);
		usleep_range(10000, 12000);
		break;
	case PWR_ON:
		sensor_dbg("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_write(sd, PWDN, CSI_STBY_ON);
		vin_gpio_write(sd, RESET, CSI_RST_ON);
		usleep_range(1000, 1200);
		vin_gpio_write(sd, POWER_EN, CSI_PWR_ON);
		vin_set_pmu_channel(sd, IOVDD, ON);
		usleep_range(10000, 12000);
		vin_set_pmu_channel(sd, DVDD, ON);
		usleep_range(10000, 12000);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, AFVDD, ON);
		usleep_range(10000, 12000);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, PWDN, CSI_STBY_OFF);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, RESET, CSI_RST_OFF);
		usleep_range(30000, 31000);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_dbg("PWR_OFF!\n");
		cci_lock(sd);
		vin_set_mclk(sd, OFF);
		vin_gpio_write(sd, PWDN, CSI_PWR_OFF);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		vin_set_pmu_channel(sd, AFVDD, OFF);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, RESET, CSI_RST_ON);
		vin_gpio_write(sd, POWER_EN, CSI_STBY_OFF);
		vin_gpio_set_status(sd, RESET, 0);
		vin_gpio_set_status(sd, PWDN, 0);
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
		vin_gpio_write(sd, RESET, CSI_RST_OFF);
		usleep_range(100, 120);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_RST_ON);
		usleep_range(100, 120);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type val = 0;

	sensor_read(sd, ID_REG_HIGH, &val);
	if (val != ID_VAL_HIGH)
		return -ENODEV;
	sensor_read(sd, ID_REG_LOW, &val);
	if (val != ID_VAL_LOW)
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
	info->width = QSXGA_WIDTH;
	info->height = QSXGA_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 15; /* 30fps */

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
	case VIDIOC_VIN_SENSOR_CFG_REQ:
		sensor_cfg_req(sd, (struct sensor_config *)arg);
		break;
	case VIDIOC_VIN_ACT_INIT:
		ret = actuator_init(sd, (struct actuator_para *)arg);
		break;
	case VIDIOC_VIN_ACT_SET_CODE:
		ret = actuator_set_code(sd, (struct actuator_ctrl *)arg);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

/*
 * Store information about the video data format.
 */
static struct regval_list sensor_fmt_raw[] = {

};

static struct sensor_format_struct sensor_formats[] = {
	{
		.desc = "Raw RGB Bayer",
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.regs = sensor_fmt_raw,
		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
		.bpp = 1
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct regval_list sensor_default_regs[] = {

};

static struct sensor_win_size sensor_win_sizes[] = {
	{
	 .width = QSXGA_WIDTH,
	 .height = QSXGA_HEIGHT,
	 .hoffset = 0,
	 .voffset = 4,
	 .hts = 1000,
	 .vts = 1984,
	 .pclk = 45*1000*1000,
	 .mipi_bps = 720*1000*1000,
	.fps_fixed = 30,
	 .bin_factor = 0,
	 .intg_min = 4 << 4,
	 .intg_max = (1984)<<4,
	 .gain_min = 1 << 4,
	 .gain_max = 20<<4,
	.regs       = sensor_default_regs,
	.regs_size  = ARRAY_SIZE(sensor_default_regs),
	.set_size   = NULL,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = 0 | V4L2_MBUS_CSI2_2_LANE | V4L2_MBUS_CSI2_CHANNEL_0;

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

	struct v4l2_subdev *sd = &info->sd;
	struct sensor_format_struct *sensor_fmt = info->fmt;
	struct sensor_win_size *wsize = info->current_wins;
	struct sensor_exp_gain exp_gain;

	sensor_dbg("sensor_reg_init\n");

	sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	if (wsize->regs)
		sensor_write_array(sd, wsize->regs, wsize->regs_size);

	if (wsize->set_size)
		wsize->set_size(sd);

	info->width = wsize->width;
	info->height = wsize->height;

	exp_gain.exp_val = 16000;
	exp_gain.gain_val = 128;
	/* sensor_s_exp_gain(sd, &exp_gain); */
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xf7, 0x01);
	sensor_write(sd, 0xf8, 0x0e);
	sensor_write(sd, 0xf9, 0xae);
	sensor_write(sd, 0xfa, 0x84);
	sensor_write(sd, 0xfc, 0xae);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0x88, 0x03);
	sensor_write(sd, 0xe7, 0xc0);

	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0xfe, 0x00);
	sensor_write(sd, 0x03, 0x08);
	sensor_write(sd, 0x04, 0xca);
	sensor_write(sd, 0x05, 0x01);
	sensor_write(sd, 0x06, 0xf4);
	sensor_write(sd, 0x07, 0x00);
	sensor_write(sd, 0x08, 0x08);
	sensor_write(sd, 0x0a, 0x00);
	sensor_write(sd, 0x0c, 0x00);
	sensor_write(sd, 0x0d, 0x07);
	sensor_write(sd, 0x0e, 0xa8);
	sensor_write(sd, 0x0f, 0x0a);
	sensor_write(sd, 0x10, 0x40);

	sensor_write(sd, 0x11, 0x31);
	sensor_write(sd, 0x12, 0x28);
	sensor_write(sd, 0x13, 0x10);
	sensor_write(sd, 0x17, MIRROR);
	sensor_write(sd, 0x18, 0x02);
	sensor_write(sd, 0x19, 0x0d);
	sensor_write(sd, 0x1a, PH_SWITCH);
	sensor_write(sd, 0x1b, 0x41);
	sensor_write(sd, 0x1c, 0x2b);
	sensor_write(sd, 0x21, 0x0f);
	sensor_write(sd, 0x24, 0xb0);
	sensor_write(sd, 0x29, 0x38);
	sensor_write(sd, 0x2d, 0x16);
	sensor_write(sd, 0x2f, 0x16);
	sensor_write(sd, 0x32, 0x49);

	sensor_write(sd, 0xcd, 0xaa);
	sensor_write(sd, 0xd0, 0xc2);
	sensor_write(sd, 0xd1, 0xc4);
	sensor_write(sd, 0xd2, 0xcb);
	sensor_write(sd, 0xd3, 0x73);
	sensor_write(sd, 0xd8, 0x18);
	sensor_write(sd, 0xdc, 0xba);
	sensor_write(sd, 0xe2, 0x20);
	sensor_write(sd, 0xe4, 0x78);
	sensor_write(sd, 0xe6, 0x08);

	/*ISP*/
	sensor_write(sd, 0x80, 0x50);
	sensor_write(sd, 0x8d, 0x07);
	sensor_write(sd, 0x90, 0x01);
	sensor_write(sd, 0x92, STARTY);
	sensor_write(sd, 0x94, STARTX);
	sensor_write(sd, 0x95, 0x07);
	sensor_write(sd, 0x96, 0x98);
	sensor_write(sd, 0x97, 0x0a);
	sensor_write(sd, 0x98, 0x20);

	/*Gain*/
	sensor_write(sd, 0x99, 0x01);
	sensor_write(sd, 0x9a, 0x02);
	sensor_write(sd, 0x9b, 0x03);
	sensor_write(sd, 0x9c, 0x04);
	sensor_write(sd, 0x9d, 0x0d);
	sensor_write(sd, 0x9e, 0x15);
	sensor_write(sd, 0x9f, 0x1d);
	sensor_write(sd, 0xb0, 0x4b);
	sensor_write(sd, 0xb1, 0x01);
	sensor_write(sd, 0xb2, 0x00);
	sensor_write(sd, 0xb6, 0x00);

	/*BLK*/
	sensor_write(sd, 0x40, 0x22);
	sensor_write(sd, 0x4e, 0x3c);
	sensor_write(sd, 0x4f, 0x00);
	sensor_write(sd, 0x60, 0x00);
	sensor_write(sd, 0x61, 0x80);
	sensor_write(sd, 0xfe, 0x02);
	sensor_write(sd, 0xa4, 0x30);
	sensor_write(sd, 0xa5, 0x00);

	/*Dark Sun*/
	sensor_write(sd, 0x40, 0x00); //96 20160527
	sensor_write(sd, 0x42, 0x0f);
	sensor_write(sd, 0x45, 0xca);
	sensor_write(sd, 0x47, 0xff);
	sensor_write(sd, 0x48, 0xc8);

	/*DD*/
	sensor_write(sd, 0x80, 0x98);
	sensor_write(sd, 0x81, 0x50);
	sensor_write(sd, 0x82, 0x60);
	sensor_write(sd, 0x84, 0x20);
	sensor_write(sd, 0x85, 0x10);
	sensor_write(sd, 0x86, 0x04);
	sensor_write(sd, 0x87, 0x20);
	sensor_write(sd, 0x88, 0x10);
	sensor_write(sd, 0x89, 0x04);

	/*Degrid*/
	sensor_write(sd, 0x8a, 0x0a);

	/*MIPI*/
	sensor_write(sd, 0xfe, 0x03);
	sensor_write(sd, 0xfe, 0x03);
	sensor_write(sd, 0xfe, 0x03);
	sensor_write(sd, 0x01, 0x07);
	sensor_write(sd, 0x02, 0x34);
	sensor_write(sd, 0x03, 0x13);
	sensor_write(sd, 0x04, 0x04);
	sensor_write(sd, 0x05, 0x00);
	sensor_write(sd, 0x06, 0x80);
	sensor_write(sd, 0x11, 0x2b);
	sensor_write(sd, 0x12, 0xa8);
	sensor_write(sd, 0x13, 0x0c);
	sensor_write(sd, 0x15, 0x00);
	sensor_write(sd, 0x16, 0x09);
	sensor_write(sd, 0x18, 0x01);
	sensor_write(sd, 0x21, 0x10);
	sensor_write(sd, 0x22, 0x05);
	sensor_write(sd, 0x23, 0x30);
	sensor_write(sd, 0x24, 0x10);
	sensor_write(sd, 0x25, 0x14);
	sensor_write(sd, 0x26, 0x08);
	sensor_write(sd, 0x29, 0x05);
	sensor_write(sd, 0x2a, 0x0a);
	sensor_write(sd, 0x2b, 0x08);
	sensor_write(sd, 0x42, 0x20);
	sensor_write(sd, 0x43, 0x0a);
	sensor_write(sd, 0x10, 0x91);
	sensor_write(sd, 0xfe, 0x00);

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
static struct cci_driver cci_drv = {
		.name = SENSOR_NAME,
		.addr_width = CCI_BITS_8,
		.data_width = CCI_BITS_8,
};

static int sensor_init_controls(struct v4l2_subdev *sd,
					const struct v4l2_ctrl_ops *ops)
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

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv);
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
	info->af_first_flag = 1;
	info->exp = 0;
	info->gain = 0;

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd;

	sd = cci_dev_remove_helper(client, &cci_drv);

	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
		.driver = {
				.owner = THIS_MODULE,
				.name = SENSOR_NAME,
				},
		.probe = sensor_probe,
		.remove = sensor_remove,
		.id_table = sensor_id,
};
static __init int init_sensor(void)
{
	int ret = 0;

	ret = cci_dev_init_helper(&sensor_driver);

	return ret;
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);
