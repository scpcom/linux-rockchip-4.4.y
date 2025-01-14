/*
 * A V4L2 driver for sp5509 Raw cameras.
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
MODULE_DESCRIPTION("A low-level driver for sp5509 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0x0556

#define I2C_ADDR 0x40

#define SENSOR_NAME "sp5509_mipi"


/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};

static struct regval_list sensor_5M_p30_regs[] = {
	{0x0a00, 0x0000},
	{0x0e00, 0x0102},
	{0x0e02, 0x0102},
	{0x0e0c, 0x0100},
	{0x2000, 0x7400},
	{0x2002, 0x001c},
	{0x2004, 0x0242},
	{0x2006, 0x0942},
	{0x2008, 0x7007},
	{0x200a, 0x0fd9},
	{0x200c, 0x0259},
	{0x200e, 0x7008},
	{0x2010, 0x160e},
	{0x2012, 0x0047},
	{0x2014, 0x2118},
	{0x2016, 0x0041},
	{0x2018, 0x00d8},
	{0x201a, 0x0145},
	{0x201c, 0x0006},
	{0x201e, 0x0181},
	{0x2020, 0x13cc},
	{0x2022, 0x2057},
	{0x2024, 0x7001},
	{0x2026, 0x0fca},
	{0x2028, 0x00cb},
	{0x202a, 0x009f},
	{0x202c, 0x7002},
	{0x202e, 0x13cc},
	{0x2030, 0x019b},
	{0x2032, 0x014d},
	{0x2034, 0x2987},
	{0x2036, 0x2766},
	{0x2038, 0x0020},
	{0x203a, 0x2060},
	{0x203c, 0x0e5d},
	{0x203e, 0x181d},
	{0x2040, 0x2066},
	{0x2042, 0x20c4},
	{0x2044, 0x5000},
	{0x2046, 0x0005},
	{0x2048, 0x0000},
	{0x204a, 0x01db},
	{0x204c, 0x025a},
	{0x204e, 0x00c0},
	{0x2050, 0x0005},
	{0x2052, 0x0006},
	{0x2054, 0x0ad9},
	{0x2056, 0x0259},
	{0x2058, 0x0618},
	{0x205a, 0x0258},
	{0x205c, 0x2266},
	{0x205e, 0x20c8},
	{0x2060, 0x2060},
	{0x2062, 0x707b},
	{0x2064, 0x0fdd},
	{0x2066, 0x81b8},
	{0x2068, 0x5040},
	{0x206a, 0x0020},
	{0x206c, 0x5060},
	{0x206e, 0x3143},
	{0x2070, 0x5081},
	{0x2072, 0x025c},
	{0x2074, 0x7800},
	{0x2076, 0x7400},
	{0x2078, 0x001c},
	{0x207a, 0x0242},
	{0x207c, 0x0942},
	{0x207e, 0x0bd9},
	{0x2080, 0x0259},
	{0x2082, 0x7008},
	{0x2084, 0x160e},
	{0x2086, 0x0047},
	{0x2088, 0x2118},
	{0x208a, 0x0041},
	{0x208c, 0x00d8},
	{0x208e, 0x0145},
	{0x2090, 0x0006},
	{0x2092, 0x0181},
	{0x2094, 0x13cc},
	{0x2096, 0x2057},
	{0x2098, 0x7001},
	{0x209a, 0x0fca},
	{0x209c, 0x00cb},
	{0x209e, 0x009f},
	{0x20a0, 0x7002},
	{0x20a2, 0x13cc},
	{0x20a4, 0x019b},
	{0x20a6, 0x014d},
	{0x20a8, 0x2987},
	{0x20aa, 0x2766},
	{0x20ac, 0x0020},
	{0x20ae, 0x2060},
	{0x20b0, 0x0e5d},
	{0x20b2, 0x181d},
	{0x20b4, 0x2066},
	{0x20b6, 0x20c4},
	{0x20b8, 0x50a0},
	{0x20ba, 0x0005},
	{0x20bc, 0x0000},
	{0x20be, 0x01db},
	{0x20c0, 0x025a},
	{0x20c2, 0x00c0},
	{0x20c4, 0x0005},
	{0x20c6, 0x0006},
	{0x20c8, 0x0ad9},
	{0x20ca, 0x0259},
	{0x20cc, 0x0618},
	{0x20ce, 0x0258},
	{0x20d0, 0x2266},
	{0x20d2, 0x20c8},
	{0x20d4, 0x2060},
	{0x20d6, 0x707b},
	{0x20d8, 0x0fdd},
	{0x20da, 0x86b8},
	{0x20dc, 0x50e0},
	{0x20de, 0x0020},
	{0x20e0, 0x5100},
	{0x20e2, 0x3143},
	{0x20e4, 0x5121},
	{0x20e6, 0x7800},
	{0x20e8, 0x3140},
	{0x20ea, 0x01c4},
	{0x20ec, 0x01c1},
	{0x20ee, 0x01c0},
	{0x20f0, 0x01c4},
	{0x20f2, 0x2700},
	{0x20f4, 0x3d40},
	{0x20f6, 0x7800},
	{0x20f8, 0xffff},
	{0x27fe, 0xe000},
	{0x3000, 0x60f8},
	{0x3002, 0x187f},
	{0x3004, 0x7060},
	{0x3006, 0x0114},
	{0x3008, 0x60b0},
	{0x300a, 0x1473},
	{0x300c, 0x0013},
	{0x300e, 0x140f},
	{0x3010, 0x0040},
	{0x3012, 0x100f},
	{0x3014, 0x60f8},
	{0x3016, 0x187f},
	{0x3018, 0x7060},
	{0x301a, 0x0114},
	{0x301c, 0x60b0},
	{0x301e, 0x1473},
	{0x3020, 0x0013},
	{0x3022, 0x140f},
	{0x3024, 0x0040},
	{0x3026, 0x000f},
	{0x0b00, 0x0000},
	{0x0b02, 0x0045},
	{0x0b04, 0xb405},
	{0x0b06, 0xc403},
	{0x0b08, 0x0081},
	{0x0b0a, 0x8252},
	{0x0b0c, 0xf814},
	{0x0b0e, 0xc618},
	{0x0b10, 0xa828},
	{0x0b12, 0x002c},
	{0x0b14, 0x4068},
	{0x0b16, 0x0000},
	{0x0f30, 0x6e25},
	{0x0f32, 0x7067},
	{0x0954, 0x0009},
	{0x0956, 0x0000},
	{0x0958, 0xbb80},
	{0x095a, 0x5140},
	{0x0c00, 0x1111},
	{0x0c02, 0x0011},
	{0x0c04, 0x0000},
	{0x0c06, 0x0200},
	{0x0c10, 0x0040},
	{0x0c12, 0x0040},
	{0x0c14, 0x0040},
	{0x0c16, 0x0040},
	{0x0a10, 0x4000},
	{0x0c08, 0x01c0},
	{0x0c0a, 0x01c0},
	{0x0c0c, 0x01c0},
	{0x0c0e, 0x01c0},
	{0x3068, 0xf800},
	{0x306a, 0xf876},
	{0x006c, 0x0000},
	{0x005e, 0x0200},
	{0x000e, 0X0200},
	{0x0e0a, 0x0001},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0100},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a31},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0028},
	{0x002a, 0x002d},
	{0x0026, 0x0030},
	{0x002c, 0x07c7},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x07bc},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x0798},
	{0x003e, 0x0000},
	{0x0074, 0x080e},
	{0x0070, 0x0407},
	{0x0002, 0x0000},
	{0x0a02, 0x0100},
	{0x0a24, 0x0100},
	{0x0046, 0x0000},
	{0x0076, 0x0000},
	{0x0060, 0x0000},
	{0x0062, 0x0530},
	{0x0064, 0x0500},
	{0x0066, 0x0530},
	{0x0068, 0x0500},
	{0x0122, 0x0300},
	{0x015a, 0xff08},
	{0x0804, 0x0200},
	{0x005c, 0x0182},
	{0x0a1a, 0x0800},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a31},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0028},
	{0x002a, 0x002d},
	{0x0026, 0x0030},
	{0x002c, 0x07c7},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x0823},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x0798},
	{0x003e, 0x0000},
	{0x0804, 0x0200},
	{0x0a04, 0x0148},
	{0x090c, 0x0fdc},
	{0x090e, 0x002d},
	{0x0902, 0x4319},
	{0x0914, 0xc10a},
	{0x0916, 0x071f},
	{0x0918, 0x0408},
	{0x091a, 0x0c0d},
	{0x091c, 0x0f09},
	{0x091e, 0x0a00},
	{0x0a00, 0x0100},
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

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	unsigned int all_exp;
	struct sensor_info *info = to_state(sd);
	all_exp = exp_val >> 4;
	sensor_write(sd, 0x0074, all_exp);
	sensor_dbg("sensor_set_exp = %d, Done!\n", all_exp);
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



static int sensor_s_gain(struct v4l2_subdev *sd, unsigned int gain_val)
{
	gain_val = (gain_val&0xFFFF);
	sensor_write(sd, 0x0076, gain_val-16);
	return 0;
}

static int sensor_s_exp_gain(struct v4l2_subdev *sd,
				struct sensor_exp_gain *exp_gain)
{
	int exp_val, gain_val;
	struct sensor_info *info = to_state(sd);

	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	if (gain_val < 16)
		gain_val = 16;
	if (gain_val > 128*16-1)
		gain_val = 128*16-1;

	if (exp_val > 0x3fff*16)
		exp_val = 0x3fff*16;

	sensor_write(sd, 0x0046, 0x0100); //group para hold o
	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);
	sensor_write(sd, 0x0046, 0x0000); //group para hold off
	sensor_dbg("sensor_set_gain exp = %d, gain = %d Done!\n", exp_val, gain_val);
	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static void sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
}

/*
 * Stuff that knows about the sensor.
 */
static int sensor_power(struct v4l2_subdev *sd, int on)
{
	switch (on) {
	case STBY_ON:
		sensor_print("STBY_ON!\n");
		cci_lock(sd);
		sensor_s_sw_stby(sd, STBY_ON);
		usleep_range(1000, 1200);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		sensor_print("STBY_OFF!\n");
		cci_lock(sd);
		usleep_range(1000, 1200);
		sensor_s_sw_stby(sd, STBY_OFF);
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_print("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_set_pmu_channel(sd, IOVDD, ON);
		usleep_range(5000, 10000);
		vin_set_pmu_channel(sd, AVDD, ON);
		usleep_range(5000, 10000);
		vin_set_pmu_channel(sd, DVDD, ON);
		usleep_range(5000, 12000);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(10000, 20000);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(10000, 20000);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_print("PWR_OFF!\n");
		cci_lock(sd);
		usleep_range(100, 120);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(100, 120);
		vin_set_mclk(sd, OFF);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		usleep_range(100, 120);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		usleep_range(100, 120);
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
	unsigned int SENSOR_ID = 0;
	data_type val;
	int cnt = 0;

	sensor_read(sd, 0x0f16, &val);
	SENSOR_ID = val;
	sensor_print("retry = %d, val = %x\n", cnt, SENSOR_ID);
	while ((SENSOR_ID != V4L2_IDENT_SENSOR) && (cnt < 5)) {
		sensor_read(sd, 0x0f16, &val);
		SENSOR_ID = val << 8;
		sensor_read(sd, 0x0f17, &val);
		SENSOR_ID |= val;
		sensor_print("retry = %d, val = %x\n", cnt, SENSOR_ID);
		cnt++;
	}
	if (SENSOR_ID != V4L2_IDENT_SENSOR)
		return -ENODEV;
	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	sensor_print("sensor_init\n");

	/*Make sure it is a target sensor */
	ret = sensor_detect(sd);
	if (ret) {
		sensor_err("chip found is not an target chip.\n");
		return ret;
	}

	info->focus_status = 0;
	info->low_speed = 0;
	info->width = 1280;
	info->height = 720;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30; /* 30fps */

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
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
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
	 .width = 2592,
	 .height = 1944,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2816,
	 .vts = 2083,
	 .pclk = 176 * 1000 * 1000,
	 .mipi_bps = 880*1000*1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 1 << 4,
	 .intg_max = (2083) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 16 << 4,
	.regs       = sensor_5M_p30_regs,
	.regs_size  = ARRAY_SIZE(sensor_5M_p30_regs),
	.set_size   = NULL,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
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

	sensor_print("sensor_reg_init\n");

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

	sensor_print("s_fmt set width = %d, height = %d\n", wsize->width,
				wsize->height);

	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);

	sensor_print("%s on = %d, %d*%d fps: %d code: %x\n", __func__, enable,
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
static struct cci_driver cci_drv = {
		.name = SENSOR_NAME,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_16,
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
