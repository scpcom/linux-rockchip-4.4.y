/*
 * A V4L2 driver for s5k5e9_mipi cameras.
 *
 * Copyright (c) 2018 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zheng ZeQun <zequnzheng@allwinnertech.com>
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
MODULE_DESCRIPTION("A low-level driver for s5k5e9 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0x559b

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 15

/*
 * The s5k5e9_mipi sits on i2c with ID 0x20
 */
#define I2C_ADDR 0x20

#define SENSOR_NAME "s5k5e9"



struct cfg_array { /* coming later */
	struct regval_list *regs;
	int size;
};

/*
 * The default register settings
 *
 */
static struct regval_list sensor_default_regs[] = {

};

/* 1280*720 15fps */
static struct regval_list sensor_720p_regs[] = {
	{0x0100, 0x00},
	{0x3B45, 0x01},
	{0x0B05, 0x01},
	{0x392F, 0x01},
	{0x3930, 0x00},
	{0x3924, 0x7F},
	{0x3925, 0xFD},
	{0x3C08, 0xFF},
	{0x3C09, 0xFF},
	{0x3C31, 0xFF},
	{0x3C32, 0xFF},
	{0x3290, 0x10},
	{0x3200, 0x01},
	{0x3074, 0x06},
	{0x3075, 0x2F},
	{0x308A, 0x20},
	{0x308B, 0x08},
	{0x308C, 0x0B},
	{0x3081, 0x07},
	{0x307B, 0x85},
	{0x307A, 0x0A},
	{0x3079, 0x0A},
	{0x306E, 0x71},
	{0x306F, 0x28},
	{0x301F, 0x20},
	{0x3012, 0x4E},
	{0x306B, 0x9A},
	{0x3091, 0x16},
	{0x30C4, 0x06},
	{0x306A, 0x79},
	{0x30B0, 0xFF},
	{0x306D, 0x08},
	{0x3084, 0x16},
	{0x3070, 0x0F},
	{0x30C2, 0x05},
	{0x3069, 0x87},
	{0x3C0F, 0x00},
	{0x0A02, 0x3F},
	{0x3083, 0x14},
	{0x3080, 0x08},
	{0x3C34, 0xEA},
	{0x3C35, 0x5C},

	/* mode */
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0x5F},
	{0x030D, 0x04},
	{0x030E, 0x00},
	{0x030F, 0x92},
	{0x3C1F, 0x00},
	{0x3C17, 0x00},
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x01},
	{0x0820, 0x03},
	{0x0821, 0x6C},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3929, 0x0F},
	{0x0344, 0x00},
	{0x0345, 0x18},
	{0x0346, 0x01},
	{0x0347, 0x04},
	{0x0348, 0x0A},
	{0x0349, 0x17},
	{0x034A, 0x06},
	{0x034B, 0xA3},
	{0x034C, 0x05},
	{0x034D, 0x00},
	{0x034E, 0x02},
	{0x034F, 0xD0},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x03},
	{0x0101, 0x00},
	{0x0340, 0x0F},
	{0x0341, 0xE4},
	{0x0342, 0x0C},
	{0x0343, 0x28},
	{0x0200, 0x0B},
	{0x0201, 0x9C},
	{0x0202, 0x00},
	{0x0203, 0x02},
	{0x30B8, 0x2A},
	{0x30BA, 0x2E},
	{0x0100, 0x01},
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
	sensor_print("sensor_get_exposure = %d\n", info->exp);
	return 0;
}

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, exphigh, tmp1, tmp2;
	unsigned short tmp = 0;
	struct sensor_info *info = to_state(sd);

	if (exp_val > 0xffffff)
		exp_val = 0xfffff0;
	if (exp_val < 16)
		exp_val = 16;
	exp_val = (exp_val + 8) >> 4; /* rounding to 1 */
	exphigh = (unsigned char)((0xff00 & exp_val) >> 8);
	explow  = (unsigned char)((0x00ff & exp_val));


	sensor_write(sd, 0x0104, 0x01);
	sensor_write(sd, 0x0203, explow);/* coarse integration time */
	sensor_write(sd, 0x0202, exphigh);

	sensor_write(sd, 0x0104, 0x00);
	sensor_read(sd, 0x0203, &tmp1);
	sensor_read(sd, 0x0202, &tmp2);
	tmp = ((tmp2 << 8) | tmp1);

	sensor_dbg("sensor_s_exp info->exp %d\n", exp_val);
	info->exp = exp_val;
	return 0;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->gain;
	sensor_print("sensor_get_gain = %d\n", info->gain);
	return 0;
}


static int sensor_s_gain(struct v4l2_subdev *sd, unsigned int gain_val)
{
	struct sensor_info *info = to_state(sd);
	data_type gainlow = 0;
	data_type gainhigh = 0;

	gain_val = gain_val * 2;
	gainlow = (unsigned char)(gain_val & 0xff);
	gainhigh = (unsigned char)((gain_val >> 8) & 0xff);

	sensor_write(sd, 0x0104, 0x01);
	sensor_write(sd, 0x0205, gainlow);
	sensor_write(sd, 0x0204, gainhigh);
	sensor_write(sd, 0x0104, 0x00);

	sensor_dbg("sensor_s_gain info->gain %d\n", gain_val);
	info->gain = gain_val;

	return 0;
}

static int s5k5e9_sensor_vts;
static int sensor_s_exp_gain(struct v4l2_subdev *sd,
			     struct sensor_exp_gain *exp_gain)
{
	int exp_val, gain_val, shutter;
	unsigned char explow, exphigh;
	unsigned char gainlow = 0;
	unsigned char gainhigh = 0;
	struct sensor_info *info = to_state(sd);

	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	exp_val = exp_val >> 4; /* rounding to 1 */
	shutter = exp_val;
	gain_val = gain_val * 2; /* shift to 1/32 step */

	exphigh = (unsigned char)((0xff00 & exp_val) >> 8);
	explow  = (unsigned char)((0x00ff & exp_val));
	gainlow = (unsigned char)(gain_val & 0xff);
	gainhigh = (unsigned char)((gain_val >> 8) & 0xff);

	sensor_write(sd, 0x0104, 0x01);
	sensor_write(sd, 0x0203, explow);
	sensor_write(sd, 0x0202, exphigh);
	sensor_write(sd, 0x0205, gainlow);
	sensor_write(sd, 0x0204, gainhigh);

	sensor_write(sd, 0x0104, 0x00);

	sensor_dbg("sensor_s_exp_gain info->exp %d info->gain %d\n",
									exp_val, gain_val);

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
	int ret;

	ret = 0;
	switch (on) {
	case STBY_ON:
		ret = sensor_s_sw_stby(sd, 1);
		if (ret < 0)
			sensor_err("soft stby falied!\n");
		usleep_range(10000, 12000);

		cci_lock(sd);
		/* standby on io */
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		cci_unlock(sd);
		/* inactive mclk after stadby in */
		vin_set_mclk(sd, OFF);
		break;
	case STBY_OFF:
		cci_lock(sd);

		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);

		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(10000, 12000);

		cci_unlock(sd);
		ret = sensor_s_sw_stby(sd, 0);
		if (ret < 0)
			sensor_err("soft stby off falied!\n");
		usleep_range(10000, 12000);

		break;
	case PWR_ON:
		sensor_print("PWR_ON!\n");

		cci_lock(sd);
		vin_set_pmu_channel(sd, CAMERAVDD, ON);

		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);

		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(1000, 1200);

		vin_set_mclk(sd, ON);
		usleep_range(100, 120);
		vin_set_mclk_freq(sd, MCLK);
		usleep_range(10000, 12000);

		vin_set_pmu_channel(sd, IOVDD, ON);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);
		vin_set_pmu_channel(sd, AFVDD, ON);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(10000, 12000);

		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(30000, 31000);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_print("PWR_OFF!\n");
		cci_lock(sd);

		vin_set_mclk(sd, OFF);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);

		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		vin_set_pmu_channel(sd, AFVDD, OFF);
		vin_set_pmu_channel(sd, CAMERAVDD, OFF);

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

	sensor_read(sd, 0x0000, &rdval);
	if (rdval != 0x55)
		return -ENODEV;

	sensor_read(sd, 0x0001, &rdval);
	if (rdval != 0x9b)
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
	info->width = HD720_WIDTH;
	info->height = HD720_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;
	info->exp = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = SENSOR_FRAME_RATE; /* 15fps */
	info->preview_first_flag = 1;

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
		.width      = HD720_WIDTH,
		.height     = HD720_HEIGHT,
		.hoffset    = 0,
		.voffset    = 0,
		.hts        = 3112,
		.vts        = 4068,
		.pclk       = 190 * 1000 * 1000,
		.mipi_bps   = 70 * 1000 * 1000,
		.fps_fixed  = 15,
		.bin_factor = 1,
		.intg_min   = 3 << 4,
		.intg_max   = (4068-10) << 4,
		.gain_min   = 1 << 4,
		.gain_max   = 16 << 4,
		.regs       = sensor_720p_regs,
		.regs_size  = ARRAY_SIZE(sensor_720p_regs),
		.set_size   = NULL,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct sensor_info *info = to_state(sd);

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
	s5k5e9_sensor_vts = wsize->vts;
	info->exp = 0;
	info->gain = 0;

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
	.data_width = CCI_BITS_8,
};

static const struct v4l2_ctrl_config sensor_custom_ctrls[] = {
	{
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_FRAME_RATE,
		.name = "frame rate",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 15,
		.max = 120,
		.step = 1,
		.def = 120,
	},
};

static int sensor_init_controls(struct v4l2_subdev *sd, const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 2);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 32, 16 * 32, 1, 32);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 3 * 16, 65536 * 16, 1, 3 * 16);
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

	return cci_dev_init_helper(&sensor_driver);
}

static __exit void exit_sensor(void)
{

	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);
