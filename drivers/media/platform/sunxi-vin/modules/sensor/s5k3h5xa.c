/*
 * A V4L2 driver for s5k3h5xa Raw cameras.
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

MODULE_AUTHOR("myf");
MODULE_DESCRIPTION("A low-level driver for s5k3h5xa sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0x0535

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The s5k3h5xa i2c address
 */
#define I2C_ADDR 0x20

#define SENSOR_NUM 0x2
#define SENSOR_NAME "s5k3h5xa"
#define SENSOR_NAME_2 "s5k3h5xa_2"

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {
	{0xFCFC, 0xD000},
	{0x6010, 0x0001},

	{0x6028, 0x7000},
	{0x602a, 0x1870},
	{0x6F12, 0x2DE9},
	{0x6F12, 0xF84F},
	{0x6F12, 0x9FE5},
	{0x6F12, 0x6081},
	{0x6F12, 0xA0E1},
	{0x6F12, 0x0090},
	{0x6F12, 0x98E5},
	{0x6F12, 0x00A0},
	{0x6F12, 0x9FE5},
	{0x6F12, 0x5071},
	{0x6F12, 0x9FE5},
	{0x6F12, 0x5401},
	{0x6F12, 0xD7E1},
	{0x6F12, 0xBE5E},
	{0x6F12, 0xD0E1},
	{0x6F12, 0xBC03},
	{0x6F12, 0xDAE5},
	{0x6F12, 0x9D10},
	{0x6F12, 0xDAE5},
	{0x6F12, 0x9FB0},
	{0x6F12, 0x40E0},
	{0x6F12, 0x0100},
	{0x6F12, 0x00E0},
	{0x6F12, 0x9500},
	{0x6F12, 0x55E1},
	{0x6F12, 0x0B00},
	{0x6F12, 0xA0E1},
	{0x6F12, 0x2005},
	{0x6F12, 0x002A},
	{0x6F12, 0x1B00},
	{0x6F12, 0x5BE1},
	{0x6F12, 0x0000},
	{0x6F12, 0x008A},
	{0x6F12, 0x0B00},
	{0x6F12, 0xA0E3},
	{0x6F12, 0x0040},
	{0x6F12, 0xC7E1},
	{0x6F12, 0xBEBE},
	{0x6F12, 0x87E0},
	{0x6F12, 0x0461},
	{0x6F12, 0x96E5},
	{0x6F12, 0xF800},
	{0x6F12, 0xA0E1},
	{0x6F12, 0x0B10},
	{0x6F12, 0x00E0},
	{0x6F12, 0x9500},
	{0x6F12, 0x00EB},
	{0x6F12, 0x4800},
	{0x6F12, 0x84E2},
	{0x6F12, 0x0140},
	{0x6F12, 0x54E3},
	{0x6F12, 0x0400},
	{0x6F12, 0x86E5},
	{0x6F12, 0xF800},
	{0x6F12, 0xFF3A},
	{0x6F12, 0xF6FF},
	{0x6F12, 0x00EA},
	{0x6F12, 0x0D00},
	{0x6F12, 0xDAE5},
	{0x6F12, 0x9E00},
	{0x6F12, 0x50E1},
	{0x6F12, 0x0500},
	{0x6F12, 0x002A},
	{0x6F12, 0x0A00},
	{0x6F12, 0xA0E3},
	{0x6F12, 0x0040},
	{0x6F12, 0xC7E1},
	{0x6F12, 0xBE0E},
	{0x6F12, 0x87E0},
	{0x6F12, 0x0461},
	{0x6F12, 0x96E5},
	{0x6F12, 0xF800},
	{0x6F12, 0xDAE5},
	{0x6F12, 0x9E10},
	{0x6F12, 0x00E0},
	{0x6F12, 0x9500},
	{0x6F12, 0x00EB},
	{0x6F12, 0x3900},
	{0x6F12, 0x84E2},
	{0x6F12, 0x0140},
	{0x6F12, 0x54E3},
	{0x6F12, 0x0400},
	{0x6F12, 0x86E5},
	{0x6F12, 0xF800},
	{0x6F12, 0xFF3A},
	{0x6F12, 0xF6FF},
	{0x6F12, 0x9FE5},
	{0x6F12, 0xC000},
	{0x6F12, 0x87E2},
	{0x6F12, 0xC830},
	{0x6F12, 0x8DE5},
	{0x6F12, 0x0030},
	{0x6F12, 0xD0E5},
	{0x6F12, 0xB920},
	{0x6F12, 0xD0E1},
	{0x6F12, 0xB618},
	{0x6F12, 0x9FE5},
	{0x6F12, 0xB000},
	{0x6F12, 0xA0E1},
	{0x6F12, 0x0930},
	{0x6F12, 0x90E5},
	{0x6F12, 0x28C0},
	{0x6F12, 0x87E2},
	{0x6F12, 0xE800},
	{0x6F12, 0xA0E1},
	{0x6F12, 0x0FE0},
	{0x6F12, 0x2FE1},
	{0x6F12, 0x1CFF},
	{0x6F12, 0x50E3},
	{0x6F12, 0x0000},
	{0x6F12, 0x001B},
	{0x6F12, 0x2A00},
	{0x6F12, 0x9FE5},
	{0x6F12, 0x9400},
	{0x6F12, 0x98E5},
	{0x6F12, 0x0010},
	{0x6F12, 0xD0E1},
	{0x6F12, 0xBA05},
	{0x6F12, 0xD1E5},
	{0x6F12, 0x9C10},
	{0x6F12, 0x9FE5},
	{0x6F12, 0x8830},
	{0x6F12, 0x50E1},
	{0x6F12, 0x0100},
	{0x6F12, 0xA0E3},
	{0x6F12, 0x0000},
	{0x6F12, 0x002A},
	{0x6F12, 0x1300},
	{0x6F12, 0x00EA},
	{0x6F12, 0x0500},
	{0x6F12, 0xD1E1},
	{0x6F12, 0xB210},
	{0x6F12, 0x82E2},
	{0x6F12, 0x0D22},
	{0x6F12, 0xC2E1},
	{0x6F12, 0xB010},
	{0x6F12, 0x80E2},
	{0x6F12, 0x0100},
	{0x6F12, 0x50E3},
	{0x6F12, 0x1400},
	{0x6F12, 0x00AA},
	{0x6F12, 0x0400},
	{0x6F12, 0x80E0},
	{0x6F12, 0x8010},
	{0x6F12, 0x83E0},
	{0x6F12, 0x8110},
	{0x6F12, 0xD1E1},
	{0x6F12, 0xB020},
	{0x6F12, 0x52E3},
	{0x6F12, 0x0000},
	{0x6F12, 0xFF1A},
	{0x6F12, 0xF4FF},
	{0x6F12, 0xBDE8},
	{0x6F12, 0xF84F},
	{0x6F12, 0x2FE1},
	{0x6F12, 0x1EFF},
	{0x6F12, 0xD1E1},
	{0x6F12, 0xB410},
	{0x6F12, 0x82E2},
	{0x6F12, 0x0D22},
	{0x6F12, 0xC2E1},
	{0x6F12, 0xB010},
	{0x6F12, 0x80E2},
	{0x6F12, 0x0100},
	{0x6F12, 0x50E3},
	{0x6F12, 0x1400},
	{0x6F12, 0xFFAA},
	{0x6F12, 0xF7FF},
	{0x6F12, 0x80E0},
	{0x6F12, 0x8010},
	{0x6F12, 0x83E0},
	{0x6F12, 0x8110},
	{0x6F12, 0xD1E1},
	{0x6F12, 0xB020},
	{0x6F12, 0x52E3},
	{0x6F12, 0x0000},
	{0x6F12, 0xFF1A},
	{0x6F12, 0xF4FF},
	{0x6F12, 0xFFEA},
	{0x6F12, 0xF1FF},
	{0x6F12, 0x0070},
	{0x6F12, 0x7014},
	{0x6F12, 0x0070},
	{0x6F12, 0x0000},
	{0x6F12, 0x0070},
	{0x6F12, 0x800C},
	{0x6F12, 0x0070},
	{0x6F12, 0x3013},
	{0x6F12, 0x0070},
	{0x6F12, 0xF804},
	{0x6F12, 0x0070},
	{0x6F12, 0x7015},
	{0x6F12, 0x0070},
	{0x6F12, 0x002D},
	{0x6F12, 0x1FE5},
	{0x6F12, 0x04F0},
	{0x6F12, 0x0000},
	{0x6F12, 0x20E1},
	{0x6F12, 0x1FE5},
	{0x6F12, 0x04F0},
	{0x6F12, 0x0000},
	{0x6F12, 0x641C},
	{0x6028, 0xD000},


	{0x3902, 0x0002},
	{0x3158, 0x0215},
	{0x32B4, 0xF4B6},
	{0x32B6, 0xF466},
	{0x32B8, 0xF456},
	{0x32BA, 0xF45E},
	{0x32BC, 0x10  },
	{0x32BD, 0x00  },
	{0x32BE, 0x00  },
	{0x3338, 0x0214},
	{0x6218, 0xF1D0},
	{0x6214, 0xF9F0},
	{0x6226, 0x0001},
	{0xF446, 0x0029},
	{0xF448, 0x001D},
	{0xF440, 0x0071},
	{0xF42E, 0x00C1},
	{0xF42A, 0x0802},
	{0xB0C8, 0x0044},
	{0x6226, 0x0000},
	{0x6218, 0xF9F0},
	{0x34A2, 0x00D6},
	{0x34B2, 0x01FA},
	{0x34CA, 0x00D6},
	{0x34DA, 0x01FA},
	{0x3522, 0x00D6},
	{0x3532, 0x01FA},
	{0x3254, 0x79D3},
	{0x3256, 0x79D3},
	{0x3258, 0x79D3},
	{0x325A, 0x79D3},
	{0x325C, 0x79D3},
	{0x325E, 0x79D3},
	{0x357A, 0x00BD},
	{0x32F6, 0x1110},
	{0x012C, 0x60  },
	{0x012D, 0x4F  },
	{0x012E, 0x2F  },
	{0x012F, 0x40  },
	{0x6028, 0x7000},
	{0x602A, 0x2D00},
	{0x6F12, 0x30F4},
	{0x6F12, 0xD370},
	{0x6F12, 0xD379},
	{0x6F12, 0x12F4},
	{0x6F12, 0x0500},
	{0x6F12, 0x0100},
	{0x6F12, 0x4638},
	{0x6F12, 0x0007},
	{0x6F12, 0xF004},
	{0x6F12, 0x5038},
	{0x6F12, 0x0000},
	{0x6F12, 0x1002},
	{0x6F12, 0xF838},
	{0x6F12, 0xFAFF},
	{0x6F12, 0x0000},
	{0x6F12, 0x4C38},
	{0x6F12, 0x7805},
	{0x6F12, 0x9C04},
	{0x6F12, 0x78F4},
	{0x6F12, 0x0700},
	{0x6F12, 0x0700},
	{0x6F12, 0x9AF4},
	{0x6F12, 0x3100},
	{0x6F12, 0x3100},
	{0x6F12, 0x36F4},
	{0x6F12, 0x0600},
	{0x6F12, 0x0600},
	{0x6F12, 0x0000},
	{0x6028, 0xD000},
	{0x6226, 0x0001},
	{0x6100, 0x0003},
	{0x6110, 0x1CA0},
	{0x6112, 0x1CA4},
	{0x6150, 0x172C},
	{0x6152, 0x1730},
	{0x6028, 0x7000},
	{0x602A, 0x172C},
	{0x6F12, 0x1FE5},
	{0x6F12, 0x04F0},
	{0x6F12, 0x0070},
	{0x6F12, 0x7018},
	{0x6028, 0xD000},
	{0x6226, 0x0000},
};

#if 0
static struct regval_list sensor_8M_30fps_regs[] = {
	/* 3264 * 2448 _ 30fps */
	{0x0100, 0x00  },// software stanby
	{0x0112, 0x0A0A},// 0a0a: Top 10bit of pixel data,RAW10
	{0x0114, 0x03  },
	{0x0120, 0x00  },//0:Global Analogue Gain
	{0x0200, 0x0BEF},//Fine integration time(pixels)  = 3055
	{0x0202, 0x09D9},//Coarse integration time(lines) = 2521
	{0x0204, 0x0020},//Global Analogue Gain Code = 32
	{0x0300, 0x0002},//Value Timing Pixel Clock Divider = 2
	{0x0302, 0x0001},//Video Timing system Clock Divider Value = 1
	{0x0304, 0x0006},//Pre PLL clock Divider Value = 6
	{0x0306, 0x008C},//PLL multiplier Value = 140
	{0x0308, 0x0008},//output Pixel Clock Divider = 8
	{0x030A, 0x0001},//output system clock Divider Value = 1
	{0x030C, 0x0006},//2nd PLL Pre clock Divider value = 6
	{0x030E, 0x00A5},//2nd PLL multiplier value = 165

	{0x0340, 0x09E2},//frame_length_lines = 2530 Frame_Length --- VTS
	{0x0342, 0x0E68},//line_length_pck = 3688 Line Length --- HTS

	{0x32CE, 0x0094},
	{0x32D0, 0x0024},
	{0x0344, 0x0008},//x_addr_start = 8
	{0x0346, 0x0008},//y_addr_start = 8
	{0x0348, 0x0CC7},//x_addr_end = 3271
	{0x034A, 0x0997},//y_addr_end = 2445
	{0x034C, 0x0CC0},//x_output_size = 3264
	{0x034E, 0x0990},//y_output_size = 2448
	{0x0380, 0x0001},//x_even_inc = 1
	{0x0382, 0x0001},//x_odd_inc = 1
	{0x0384, 0x0001},//y_even_inc = 1 Increment for even pixels = 0,2,4 etc
	{0x0386, 0x0001},//y_odd_inc = 1 Increment for odd pixels = 1,3,5etc
	{0x0900, 0x01  },
	{0x0901, 0x11  },
	{0x0902, 0x01  },
	//scale
	{0x0400, 0x0000},//horizontal scaling
	{0x3011, 0x01  },//No binning
	{0x3293, 0x00  },
	{0x0100, 0x01  },//streaming
};
#endif

static struct regval_list sensor_6M_25fps_regs[] = {
	/* 3264 * 1836 _ 25fps */
	{0x0100, 0x00  },// software stanby
	{0x0112, 0x0A0A},// 0a0a: Top 10bit of pixel data,RAW10
	{0x0114, 0x03  },
	{0x0120, 0x00  },//0:Global Analogue Gain
	{0x0200, 0x0BEF},//Fine integration time(pixels)  = 3055
	{0x0202, 0x09D9},//Coarse integration time(lines) = 2521
	{0x0204, 0x0020},//Global Analogue Gain Code = 32
	{0x0300, 0x0002},//Value Timing Pixel Clock Divider = 2
	{0x0302, 0x0001},//Video Timing system Clock Divider Value = 1
	{0x0304, 0x0006},//Pre PLL clock Divider Value = 6
	{0x0306, 0x008C},//PLL multiplier Value = 140
	{0x0308, 0x0008},//output Pixel Clock Divider = 8
	{0x030A, 0x0001},//output system clock Divider Value = 1
	{0x030C, 0x0006},//2nd PLL Pre clock Divider value = 6
	{0x030E, 0x00A5},//2nd PLL multiplier value = 165

	{0x0340, 0x09e2},//frame_length_lines = 2530 Frame_Length --- VTS
	{0x0342, 0x1144},//line_length_pck = 4420 Line Length --- HTS

	{0x32CE, 0x0094},
	{0x32D0, 0x0024},

	{0x0344, 0x0008},//x_addr_start = 8
	{0x0346, 0x0008},//y_addr_start = 314
	{0x0348, 0x0CC7},//x_addr_end = 3271
	{0x034A, 0x0997},//y_addr_end = 2149
	{0x034C, 0x0CC0},//x_output_size = 3264
	{0x034E, 0x072c},//y_output_size = 1836

	{0x0380, 0x0001},//x_even_inc = 1
	{0x0382, 0x0001},//x_odd_inc = 1
	{0x0384, 0x0001},//y_even_inc = 1 Increment for even pixels = 0,2,4 etc
	{0x0386, 0x0001}, //y_odd_inc = 1 Increment for odd pixels = 1,3,5etc

	{0x0900, 0x01  },//binning_mode  1:ENABLE
	{0x0901, 0x11  },//binning type
	{0x0902, 0x01  },// binning_weighting
	//scale
	{0x0400, 0x0000},//horizontal scaling
	{0x3011, 0x01  },//No binning
	{0x3293, 0x00  },
	{0x0100, 0x01  },//streaming

};

static int sensor_read_byte(struct v4l2_subdev *sd, unsigned short reg,
	unsigned char *value)
{
	int ret = 0, cnt = 0;

	if (!sd || !sd->entity.use_count) {
		sensor_print("%s error! sensor is not used!\n", __func__);
		return -1;
	}

	ret = cci_read_a16_d8(sd, reg, value);
	while ((ret != 0) && (cnt < 2)) {
		ret = cci_read_a16_d8(sd, reg, value);
		cnt++;
	}
	if (cnt > 0)
		pr_info("%s sensor read retry = %d\n", sd->name, cnt);

	return ret;
}

static int sensor_write_byte(struct v4l2_subdev *sd, unsigned short reg,
	unsigned char value)
{
	int ret = 0, cnt = 0;

	if (!sd || !sd->entity.use_count) {
		sensor_print("%s error! sensor is not used!\n", __func__);
		return -1;
	}

	ret = cci_write_a16_d8(sd, reg, value);
	while ((ret != 0) && (cnt < 2)) {
		ret = cci_write_a16_d8(sd, reg, value);
		cnt++;
	}
	if (cnt > 0)
		pr_info("%s sensor write retry = %d\n", sd->name, cnt);

	return ret;
}

static int s5k3h5xa_write_array(struct v4l2_subdev *sd, struct regval_list *regs, int array_size)
{
	int i = 0, ret = 0;

	if (!regs)
		return -EINVAL;

	while (i < array_size) {
		if (regs->addr == REG_DLY) {
			usleep_range(regs->data * 1000, regs->data * 1000 + 100);
		} else {
			//if(regs->width==16)
			//	ret = sensor_write(sd, regs->addr, regs->data);
			//else if(regs->width==8)
			//	ret = sensor_write_byte(sd, regs->addr, regs->data);
			if (regs->addr == 0x32bc || regs->addr == 0x32bd || regs->addr == 0x32be ||
				regs->addr == 0x012c || regs->addr == 0x012d || regs->addr == 0x012e ||
				regs->addr == 0x012f || regs->addr == 0x0100 || regs->addr == 0x0114 ||
				regs->addr == 0x0120 || regs->addr == 0x0900 || regs->addr == 0x0901 ||
				regs->addr == 0x0902 || regs->addr == 0x3011 || regs->addr == 0x3293)
				ret = sensor_write_byte(sd, regs->addr, regs->data);
			else
				ret = sensor_write(sd, regs->addr, regs->data);

			if (ret < 0) {
				sensor_print("%s sensor write array error, array_size %d!\n", sd->name, array_size);
				return -1;
			}
		}
		i++;
		regs++;
	}
	return 0;
}

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

static int s5k3h5xa_sensor_vts;
static int s5k3h5xa_sensor_hts;

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	unsigned int exp_coarse;
	unsigned short exp_fine;
	struct sensor_info *info = to_state(sd);

	if (exp_val > 0xffffff)
		exp_val = 0xfffff0;
	if (exp_val < 16)
		exp_val = 16;

	if (info->exp == exp_val)
		return 0;

	exp_coarse = exp_val >> 4;//rounding to 1
	//exp_fine = (exp_val - exp_coarse * 16) * info->current_wins->hts / 16;
	exp_fine = (unsigned short) (((exp_val - exp_coarse * 16) * s5k3h5xa_sensor_hts) / 16);

	sensor_write(sd, 0x0200, exp_fine);
	sensor_write(sd, 0x0202, (unsigned short)exp_coarse);

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
	//	unsigned short gain;
	int ana_gain = 0, digi_gain = 0;
	unsigned short GR_GAIN, R_GAIN, B_GAIN, GB_GAIN;

	if (info->gain == gain_val)
		return 0;

	if (gain_val <= 1 * 16) {
		ana_gain = 16;
		digi_gain = 256;
	} else if (gain_val > 16 && gain_val <= (16 * 16 - 1)) {
		ana_gain = gain_val;
		digi_gain = 256;
	} else if (gain_val > (16 * 16 - 1) && gain_val < (32 * 16 - 1)) {
		ana_gain = 16 * 16 - 1;
		digi_gain = (gain_val - 255 + 16) * 1 + 256;
	} else {
		ana_gain = 16 * 16 - 1;
		digi_gain = 512;
	}
	ana_gain *= 2;//shift to 1/32 step
	GR_GAIN = (unsigned short)digi_gain;
	R_GAIN  = (unsigned short)digi_gain;
	B_GAIN  = (unsigned short)digi_gain;
	GB_GAIN = (unsigned short)digi_gain;
//	gain = gain_val * 2;//shift to 1/32 step

//	sensor_write(sd, 0x0204, gain);
	sensor_write(sd, 0x0204, (unsigned short)ana_gain);
	sensor_write(sd, 0x020e, GR_GAIN);
	sensor_write(sd, 0x0210, R_GAIN);
	sensor_write(sd, 0x0212, B_GAIN);
	sensor_write(sd, 0x0214, GB_GAIN);

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

	shutter = exp_val >> 4;

	if (shutter  > s5k3h5xa_sensor_vts - 4)
		frame_length = shutter + 4;
	else
		frame_length = s5k3h5xa_sensor_vts;

	sensor_write_byte(sd, 0x0104, 0x01);
    sensor_write(sd, 0x0340, frame_length);
    sensor_s_gain(sd, gain_val);
    sensor_s_exp(sd, exp_val);
    sensor_write_byte(sd, 0x0104, 0x00);

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	unsigned char rdval;

	ret = sensor_read_byte(sd, 0x0100, &rdval);
	if (ret != 0)
		return ret;

	if (on_off == STBY_ON)
		ret = sensor_write_byte(sd, 0x0100, rdval & 0xfe);
	else
		ret = sensor_write_byte(sd, 0x0100, rdval | 0x01);
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
		break;
	case STBY_OFF:
		sensor_dbg("STBY_OFF!\n");
		cci_lock(sd);
		usleep_range(10000, 12000);
		ret = sensor_s_sw_stby(sd, STBY_OFF);
		if (ret < 0)
			sensor_err("soft stby off falied!\n");
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_dbg("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1); /* RESET port set to output mode and pull-up */
		vin_gpio_set_status(sd, POWER_EN, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		usleep_range(1000, 1200);
		vin_set_pmu_channel(sd, IOVDD, ON);
		usleep_range(1000, 1200);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(10000, 12000);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vin_set_mclk_freq(sd, MCLK);
		usleep_range(30000, 32000);
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
	unsigned short rdval = 0;

	sensor_read(sd, 0x0000, &rdval);
	if (rdval != 0x3085) {
		sensor_dbg("read 0x0000: 0x%04x != 0x3085\n", rdval);
		return -ENODEV;
	}
	sensor_dbg("sensor detect success ID = 0x%04x \n", rdval);
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
	info->gain = 0;
	info->exp = 0;

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
		ret = 0;
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
#if 0
	{
	.width         = 3264,
	.height        = 2448,
	.hoffset       = 0,
	.voffset       = 0,
	.hts           = 3688,
	.vts           = 2530,
	.pclk          = 280*1000*1000,
	.mipi_bps      = 660*1000*1000,
	.fps_fixed     = 30,
	.bin_factor    = 1,
	.intg_min      = 16,
	.intg_max      = (2530-4)<<4,
	.gain_min      = 16,
	.gain_max      = (32<<4),
	.regs          = sensor_8M_30fps_regs,
	.regs_size     = ARRAY_SIZE(sensor_8M_30fps_regs),
	.set_size      = NULL,
	},
#endif
	{
	.width         = 3200,
	.height        = 1800,
	.hoffset       = 32,
	.voffset       = 18,
	.hts           = 4420,
	.vts           = 2530,
	.pclk          = 280*1000*1000,
	.mipi_bps      = 660*1000*1000,
	.fps_fixed     = 25,
	.bin_factor    = 1,
	.intg_min      = 16,
	.intg_max      = (2530-4)<<4,
	.gain_min      = 16,
	.gain_max      = (32<<4),
	.regs          = sensor_6M_25fps_regs,
	.regs_size     = ARRAY_SIZE(sensor_6M_25fps_regs),
	.set_size      = NULL,
	.top_clk       = 310*1000*1000,
	.isp_clk       = 286*1000*1000,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2_DPHY;

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

	ret = s5k3h5xa_write_array(sd, sensor_default_regs,
				 ARRAY_SIZE(sensor_default_regs));
	if (ret < 0) {
		sensor_err("write sensor_default_regs error\n");
		return ret;
	}

	sensor_dbg("sensor_reg_init\n");

	s5k3h5xa_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	if (wsize->regs)
		s5k3h5xa_write_array(sd, wsize->regs, wsize->regs_size);

	if (wsize->set_size)
		wsize->set_size(sd);

	info->width = wsize->width;
	info->height = wsize->height;
	s5k3h5xa_sensor_vts = wsize->vts;
	s5k3h5xa_sensor_hts = wsize->hts;

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
	.get_mbus_config = sensor_get_mbus_config,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code = sensor_enum_mbus_code,
	.enum_frame_size = sensor_enum_frame_size,
	.get_fmt = sensor_get_fmt,
	.set_fmt = sensor_set_fmt,
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
		.data_width = CCI_BITS_16,
	}, {
		.name = SENSOR_NAME_2,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_16,
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
	info->stream_seq = MIPI_BEFORE_SENSOR;
	info->combo_mode = CMB_PHYA_OFFSET1 | MIPI_NORMAL_MODE;
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
