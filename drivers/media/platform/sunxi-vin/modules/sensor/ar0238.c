
/*
 * A V4L2 driver for AR0238 Raw cameras.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
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
MODULE_DESCRIPTION("A low-level driver for AR0238 sensors");
MODULE_LICENSE("GPL");

/* define module timing */
#define MCLK              (27*1000*1000)
#define VREF_POL          V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define HREF_POL          V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define CLK_POL           V4L2_MBUS_PCLK_SAMPLE_FALLING
#define V4L2_IDENT_SENSOR  0x1256

/* 1106 for 27Mhz */
#define VMAX 1106

#define HDR 0
/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The AR0238 i2c address
 */
#define I2C_ADDR 0x20

#define SENSOR_NUM 0x2
#define SENSOR_NAME "ar0238"
#define SENSOR_NAME_2 "ar0238_2"

struct cfg_array {		/* coming later */
	struct regval_list *regs;
	int size;
};

/*
 * The default register settings
 *
 */

static struct regval_list sensor_default_regs[] = {

};

#if HDR
static struct regval_list sensor_hdr_regs[] = {
	 /*HDR*/ {0x301A, 0x0001},
	{REG_DLY, 0xd0},
	{0x301A, 0x10D8},
	{REG_DLY, 0x64},
	{0x3088, 0x816A},
	{0x3086, 0x4558},
	{0x3086, 0x729B},
	{0x3086, 0x4A31},
	{0x3086, 0x4342},
	{0x3086, 0x8E03},
	{0x3086, 0x2A14},
	{0x3086, 0x4578},
	{0x3086, 0x7B3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xEA2A},
	{0x3086, 0x043D},
	{0x3086, 0x102A},
	{0x3086, 0x052A},
	{0x3086, 0x1535},
	{0x3086, 0x2A05},
	{0x3086, 0x3D10},
	{0x3086, 0x4558},
	{0x3086, 0x2A04},
	{0x3086, 0x2A14},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DEA},
	{0x3086, 0x2A04},
	{0x3086, 0x622A},
	{0x3086, 0x288E},
	{0x3086, 0x0036},
	{0x3086, 0x2A08},
	{0x3086, 0x3D64},
	{0x3086, 0x7A3D},
	{0x3086, 0x0444},
	{0x3086, 0x2C4B},
	{0x3086, 0x8F00},
	{0x3086, 0x430C},
	{0x3086, 0x2D63},
	{0x3086, 0x4316},
	{0x3086, 0x2A90},
	{0x3086, 0x3E06},
	{0x3086, 0x2A98},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0xFC5C},
	{0x3086, 0x1D57},
	{0x3086, 0x5449},
	{0x3086, 0x5F53},
	{0x3086, 0x0553},
	{0x3086, 0x074D},
	{0x3086, 0x2BF8},
	{0x3086, 0x1016},
	{0x3086, 0x4C08},
	{0x3086, 0x5556},
	{0x3086, 0x2BB8},
	{0x3086, 0x2B98},
	{0x3086, 0x4E11},
	{0x3086, 0x2904},
	{0x3086, 0x2984},
	{0x3086, 0x2994},
	{0x3086, 0x605C},
	{0x3086, 0x195C},
	{0x3086, 0x1B45},
	{0x3086, 0x4845},
	{0x3086, 0x0845},
	{0x3086, 0x8829},
	{0x3086, 0xB68E},
	{0x3086, 0x012A},
	{0x3086, 0xF83E},
	{0x3086, 0x022A},
	{0x3086, 0xFA3F},
	{0x3086, 0x095C},
	{0x3086, 0x1B29},
	{0x3086, 0xB23F},
	{0x3086, 0x0C3E},
	{0x3086, 0x023E},
	{0x3086, 0x135C},
	{0x3086, 0x133F},
	{0x3086, 0x113E},
	{0x3086, 0x0B5F},
	{0x3086, 0x2B90},
	{0x3086, 0x2B80},
	{0x3086, 0x3E06},
	{0x3086, 0x162A},
	{0x3086, 0xF23F},
	{0x3086, 0x103E},
	{0x3086, 0x0160},
	{0x3086, 0x29A2},
	{0x3086, 0x29A3},
	{0x3086, 0x5F4D},
	{0x3086, 0x192A},
	{0x3086, 0xFA29},
	{0x3086, 0x8345},
	{0x3086, 0xA83E},
	{0x3086, 0x072A},
	{0x3086, 0xFB3E},
	{0x3086, 0x2945},
	{0x3086, 0x8821},
	{0x3086, 0x3E08},
	{0x3086, 0x2AFA},
	{0x3086, 0x5D29},
	{0x3086, 0x9288},
	{0x3086, 0x102B},
	{0x3086, 0x048B},
	{0x3086, 0x1685},
	{0x3086, 0x8D48},
	{0x3086, 0x4D4E},
	{0x3086, 0x2B80},
	{0x3086, 0x4C0B},
	{0x3086, 0x3F2B},
	{0x3086, 0x2AF2},
	{0x3086, 0x3F10},
	{0x3086, 0x3E01},
	{0x3086, 0x6029},
	{0x3086, 0x8229},
	{0x3086, 0x8329},
	{0x3086, 0x435C},
	{0x3086, 0x155F},
	{0x3086, 0x4D19},
	{0x3086, 0x2AFA},
	{0x3086, 0x4558},
	{0x3086, 0x8E00},
	{0x3086, 0x2A98},
	{0x3086, 0x3F06},
	{0x3086, 0x1244},
	{0x3086, 0x4A04},
	{0x3086, 0x4316},
	{0x3086, 0x0543},
	{0x3086, 0x1658},
	{0x3086, 0x4316},
	{0x3086, 0x5A43},
	{0x3086, 0x1606},
	{0x3086, 0x4316},
	{0x3086, 0x0743},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x787B},
	{0x3086, 0x3F07},
	{0x3086, 0x2A9D},
	{0x3086, 0x3E2E},
	{0x3086, 0x4558},
	{0x3086, 0x253E},
	{0x3086, 0x068E},
	{0x3086, 0x012A},
	{0x3086, 0x988E},
	{0x3086, 0x0012},
	{0x3086, 0x444B},
	{0x3086, 0x0343},
	{0x3086, 0x2D46},
	{0x3086, 0x4316},
	{0x3086, 0xA343},
	{0x3086, 0x165D},
	{0x3086, 0x0D29},
	{0x3086, 0x4488},
	{0x3086, 0x102B},
	{0x3086, 0x0453},
	{0x3086, 0x0D8B},
	{0x3086, 0x1685},
	{0x3086, 0x448E},
	{0x3086, 0x032A},
	{0x3086, 0xFC5C},
	{0x3086, 0x1D8D},
	{0x3086, 0x6057},
	{0x3086, 0x5417},
	{0x3086, 0xFF17},
	{0x3086, 0x4B2A},
	{0x3086, 0xF43E},
	{0x3086, 0x062A},
	{0x3086, 0xFC49},
	{0x3086, 0x5F53},
	{0x3086, 0x0553},
	{0x3086, 0x074D},
	{0x3086, 0x2BF8},
	{0x3086, 0x1016},
	{0x3086, 0x4C08},
	{0x3086, 0x5556},
	{0x3086, 0x2BB8},
	{0x3086, 0x2B98},
	{0x3086, 0x4E11},
	{0x3086, 0x2904},
	{0x3086, 0x2984},
	{0x3086, 0x2994},
	{0x3086, 0x605C},
	{0x3086, 0x195C},
	{0x3086, 0x1B45},
	{0x3086, 0x4845},
	{0x3086, 0x0845},
	{0x3086, 0x8829},
	{0x3086, 0xB68E},
	{0x3086, 0x012A},
	{0x3086, 0xF83E},
	{0x3086, 0x022A},
	{0x3086, 0xFA3F},
	{0x3086, 0x095C},
	{0x3086, 0x1B29},
	{0x3086, 0xB23F},
	{0x3086, 0x0C3E},
	{0x3086, 0x023E},
	{0x3086, 0x135C},
	{0x3086, 0x133F},
	{0x3086, 0x113E},
	{0x3086, 0x0B5F},
	{0x3086, 0x2B90},
	{0x3086, 0x2B80},
	{0x3086, 0x3E10},
	{0x3086, 0x2AF2},
	{0x3086, 0x3F10},
	{0x3086, 0x3E01},
	{0x3086, 0x6029},
	{0x3086, 0xA229},
	{0x3086, 0xA35F},
	{0x3086, 0x4D1C},
	{0x3086, 0x2AFA},
	{0x3086, 0x2983},
	{0x3086, 0x45A8},
	{0x3086, 0x3E07},
	{0x3086, 0x2AFB},
	{0x3086, 0x3E29},
	{0x3086, 0x4588},
	{0x3086, 0x243E},
	{0x3086, 0x082A},
	{0x3086, 0xFA5D},
	{0x3086, 0x2992},
	{0x3086, 0x8810},
	{0x3086, 0x2B04},
	{0x3086, 0x8B16},
	{0x3086, 0x868D},
	{0x3086, 0x484D},
	{0x3086, 0x4E2B},
	{0x3086, 0x804C},
	{0x3086, 0x0B3F},
	{0x3086, 0x332A},
	{0x3086, 0xF23F},
	{0x3086, 0x103E},
	{0x3086, 0x0160},
	{0x3086, 0x2982},
	{0x3086, 0x2983},
	{0x3086, 0x2943},
	{0x3086, 0x5C15},
	{0x3086, 0x5F4D},
	{0x3086, 0x1C2A},
	{0x3086, 0xFA45},
	{0x3086, 0x588E},
	{0x3086, 0x002A},
	{0x3086, 0x983F},
	{0x3086, 0x064A},
	{0x3086, 0x739D},
	{0x3086, 0x0A43},
	{0x3086, 0x160B},
	{0x3086, 0x4316},
	{0x3086, 0x8E03},
	{0x3086, 0x2A9C},
	{0x3086, 0x4578},
	{0x3086, 0x3F07},
	{0x3086, 0x2A9D},
	{0x3086, 0x3E12},
	{0x3086, 0x4558},
	{0x3086, 0x3F04},
	{0x3086, 0x8E01},
	{0x3086, 0x2A98},
	{0x3086, 0x8E00},
	{0x3086, 0x9176},
	{0x3086, 0x9C77},
	{0x3086, 0x9C46},
	{0x3086, 0x4416},
	{0x3086, 0x1690},
	{0x3086, 0x7A12},
	{0x3086, 0x444B},
	{0x3086, 0x4A00},
	{0x3086, 0x4316},
	{0x3086, 0x6343},
	{0x3086, 0x1608},
	{0x3086, 0x4316},
	{0x3086, 0x5043},
	{0x3086, 0x1665},
	{0x3086, 0x4316},
	{0x3086, 0x6643},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x783F},
	{0x3086, 0x072A},
	{0x3086, 0x9D5D},
	{0x3086, 0x0C29},
	{0x3086, 0x4488},
	{0x3086, 0x102B},
	{0x3086, 0x0453},
	{0x3086, 0x0D8B},
	{0x3086, 0x1686},
	{0x3086, 0x3E1F},
	{0x3086, 0x4558},
	{0x3086, 0x283E},
	{0x3086, 0x068E},
	{0x3086, 0x012A},
	{0x3086, 0x988E},
	{0x3086, 0x008D},
	{0x3086, 0x6012},
	{0x3086, 0x444B},
	{0x3086, 0xB92C},
	{0x3086, 0x2C2C},
	{0x3086, 0x2C2C},
	{0x3086, 0x2C2C},
	{0x30B0, 0x1A38},
	{0x31AC, 0x100C},
	{0x302A, 0x0008},
	{0x302C, 0x0001},
	{0x302E, 0x0002},
	{0x3030, 0x002C},
	{0x3036, 0x000C},
	{0x3038, 0x0001},
	{0x3002, 0x0000},
	{0x3004, 0x0000},
	{0x3006, 0x0437},
	{0x3008, 0x0787},
	{0x300A, 0x04BE},
	{0x300C, 0x07F8},
	{0x3012, 0x0440},
	{0x30A2, 0x0001},
	{0x30A6, 0x0001},
	{0x30AE, 0x0001},
	{0x30A8, 0x0001},
	{0x3082, 0x0008},
	{0x3040, 0x0000},
	{0x31AE, 0x0301},
	{0x3064, 0x1802},
	{0x3EEE, 0xA0AA},
	{0x30BA, 0x762C},
	{0x3F4A, 0x0F70},
	{0x309E, 0x016C},
	{0x3092, 0x006F},
	{0x3EE4, 0x9937},
	{0x3EE6, 0x3863},
	{0x3EEC, 0x3B0C},
	{0x30B0, 0x1A3A},
	{0x30B0, 0x1A3A},
	{0x30BA, 0x762C},
	{0x30B0, 0x1A3A},
	{0x30B0, 0x0A3A},
	{0x3EEA, 0x2837},
	{0x3ECC, 0x4E2D},
	{0x3ED2, 0xFEA6},
	{0x3ED6, 0x2CB3},
	{0x3EEA, 0x2819},
	{0x30B0, 0x1A3A},
	{0x306E, 0x2418},
	{0x3064, 0x1802},
	{0x31AC, 0x0C0C},
	{0x31D0, 0x0000},
	{0x318E, 0x9000},
	{0x301A, 0x10DC},
	{0x3100, 0x0000},
};
#endif

static struct regval_list sensor_1080p30_regs[] = {
	{0x301A, 0x0001},
	{0x301A, 0x10D8},
	{REG_DLY, 0x0020},
	{0x3088, 0x8000},
	{0x3086, 0x4558},
	{0x3086, 0x72A6},
	{0x3086, 0x4A31},
	{0x3086, 0x4342},
	{0x3086, 0x8E03},
	{0x3086, 0x2A14},
	{0x3086, 0x4578},
	{0x3086, 0x7B3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xEA2A},
	{0x3086, 0x043D},
	{0x3086, 0x102A},
	{0x3086, 0x052A},
	{0x3086, 0x1535},
	{0x3086, 0x2A05},
	{0x3086, 0x3D10},
	{0x3086, 0x4558},
	{0x3086, 0x2A04},
	{0x3086, 0x2A14},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DEA},
	{0x3086, 0x2A04},
	{0x3086, 0x622A},
	{0x3086, 0x288E},
	{0x3086, 0x0036},
	{0x3086, 0x2A08},
	{0x3086, 0x3D64},
	{0x3086, 0x7A3D},
	{0x3086, 0x0444},
	{0x3086, 0x2C4B},
	{0x3086, 0xA403},
	{0x3086, 0x430D},
	{0x3086, 0x2D46},
	{0x3086, 0x4316},
	{0x3086, 0x2A90},
	{0x3086, 0x3E06},
	{0x3086, 0x2A98},
	{0x3086, 0x5F16},
	{0x3086, 0x530D},
	{0x3086, 0x1660},
	{0x3086, 0x3E4C},
	{0x3086, 0x2904},
	{0x3086, 0x2984},
	{0x3086, 0x8E03},
	{0x3086, 0x2AFC},
	{0x3086, 0x5C1D},
	{0x3086, 0x5754},
	{0x3086, 0x495F},
	{0x3086, 0x5305},
	{0x3086, 0x5307},
	{0x3086, 0x4D2B},
	{0x3086, 0xF810},
	{0x3086, 0x164C},
	{0x3086, 0x0955},
	{0x3086, 0x562B},
	{0x3086, 0xB82B},
	{0x3086, 0x984E},
	{0x3086, 0x1129},
	{0x3086, 0x9460},
	{0x3086, 0x5C19},
	{0x3086, 0x5C1B},
	{0x3086, 0x4548},
	{0x3086, 0x4508},
	{0x3086, 0x4588},
	{0x3086, 0x29B6},
	{0x3086, 0x8E01},
	{0x3086, 0x2AF8},
	{0x3086, 0x3E02},
	{0x3086, 0x2AFA},
	{0x3086, 0x3F09},
	{0x3086, 0x5C1B},
	{0x3086, 0x29B2},
	{0x3086, 0x3F0C},
	{0x3086, 0x3E03},
	{0x3086, 0x3E15},
	{0x3086, 0x5C13},
	{0x3086, 0x3F11},
	{0x3086, 0x3E0F},
	{0x3086, 0x5F2B},
	{0x3086, 0x902B},
	{0x3086, 0x803E},
	{0x3086, 0x062A},
	{0x3086, 0xF23F},
	{0x3086, 0x103E},
	{0x3086, 0x0160},
	{0x3086, 0x29A2},
	{0x3086, 0x29A3},
	{0x3086, 0x5F4D},
	{0x3086, 0x1C2A},
	{0x3086, 0xFA29},
	{0x3086, 0x8345},
	{0x3086, 0xA83E},
	{0x3086, 0x072A},
	{0x3086, 0xFB3E},
	{0x3086, 0x6745},
	{0x3086, 0x8824},
	{0x3086, 0x3E08},
	{0x3086, 0x2AFA},
	{0x3086, 0x5D29},
	{0x3086, 0x9288},
	{0x3086, 0x102B},
	{0x3086, 0x048B},
	{0x3086, 0x1686},
	{0x3086, 0x8D48},
	{0x3086, 0x4D4E},
	{0x3086, 0x2B80},
	{0x3086, 0x4C0B},
	{0x3086, 0x3F36},
	{0x3086, 0x2AF2},
	{0x3086, 0x3F10},
	{0x3086, 0x3E01},
	{0x3086, 0x6029},
	{0x3086, 0x8229},
	{0x3086, 0x8329},
	{0x3086, 0x435C},
	{0x3086, 0x155F},
	{0x3086, 0x4D1C},
	{0x3086, 0x2AFA},
	{0x3086, 0x4558},
	{0x3086, 0x8E00},
	{0x3086, 0x2A98},
	{0x3086, 0x3F0A},
	{0x3086, 0x4A0A},
	{0x3086, 0x4316},
	{0x3086, 0x0B43},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x783F},
	{0x3086, 0x072A},
	{0x3086, 0x9D3E},
	{0x3086, 0x305D},
	{0x3086, 0x2944},
	{0x3086, 0x8810},
	{0x3086, 0x2B04},
	{0x3086, 0x530D},
	{0x3086, 0x4558},
	{0x3086, 0x3E08},
	{0x3086, 0x8E01},
	{0x3086, 0x2A98},
	{0x3086, 0x8E00},
	{0x3086, 0x76A7},
	{0x3086, 0x77A7},
	{0x3086, 0x4644},
	{0x3086, 0x1616},
	{0x3086, 0xA57A},
	{0x3086, 0x1244},
	{0x3086, 0x4B18},
	{0x3086, 0x4A04},
	{0x3086, 0x4316},
	{0x3086, 0x0643},
	{0x3086, 0x1605},
	{0x3086, 0x4316},
	{0x3086, 0x0743},
	{0x3086, 0x1658},
	{0x3086, 0x4316},
	{0x3086, 0x5A43},
	{0x3086, 0x1645},
	{0x3086, 0x588E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x787B},
	{0x3086, 0x3F07},
	{0x3086, 0x2A9D},
	{0x3086, 0x530D},
	{0x3086, 0x8B16},
	{0x3086, 0x863E},
	{0x3086, 0x2345},
	{0x3086, 0x5825},
	{0x3086, 0x3E10},
	{0x3086, 0x8E01},
	{0x3086, 0x2A98},
	{0x3086, 0x8E00},
	{0x3086, 0x3E10},
	{0x3086, 0x8D60},
	{0x3086, 0x1244},
	{0x3086, 0x4BB9},
	{0x3086, 0x2C2C},
	{0x3086, 0x2C2C},
	{0x301A, 0x10D8},
	{0x30B0, 0x1A38},
	{0x31AC, 0x0C0C},
	{0x302A, 0x0008},
	{0x302C, 0x0001},
	{0x302E, 0x0002},
	{0x3030, 0x002C},
	{0x3036, 0x000C},
	{0x3038, 0x0001},
	{0x3002, 0x0000},
	{0x3004, 0x0000},
	{0x3006, 0x043F},
	{0x3008, 0x0787},
	{0x300A, 0x0452},
	{0x300C, 0x045E},
	{0x3012, 0x0416},
	{0x30A2, 0x0001},
	{0x30A6, 0x0001},
	{0x30AE, 0x0001},
	{0x30A8, 0x0001},
	{0x3040, 0x0000},
	{0x31AE, 0x0301},
	{0x3082, 0x0009},
	{0x30BA, 0x760C},
	{0x3100, 0x0000},
	{0x3060, 0x000B},
	{0x31D0, 0x0000},
	{0x3064, 0x1802},
	{0x3EEE, 0xA0AA},
	{0x30BA, 0x762C},
	{0x3F4A, 0x0F70},
	{0x309E, 0x016C},
	{0x3092, 0x006F},
	{0x3EE4, 0x9937},
	{0x3EE6, 0x3863},
	{0x3EEC, 0x3B0C},
	{0x30B0, 0x1A3A},
	{0x30B0, 0x1A3A},
	{0x30BA, 0x762C},
	{0x30B0, 0x1A3A},
	{0x30B0, 0x0A3A},
	{0x3EEA, 0x2838},
	{0x3ECC, 0x4E2D},
	{0x3ED2, 0xFEA6},
	{0x3ED6, 0x2CB3},
	{0x3EEA, 0x2819},
	{0x30B0, 0x1A3A},
	{0x306E, 0x2418},
	{0x301A, 0x10DC},

	/*slave mode*/
	/*{0x30CE, 0x0010},
	{0x301A, 0x19DC},*/
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
	struct sensor_info *info = to_state(sd);

	sensor_dbg("sensor_set_exposure = %d\n", exp_val);
	if (exp_val > 0xffffff)
		exp_val = 0xfffff0;
	if (exp_val < 16)
		exp_val = 16;

	exp_val = (exp_val) >> 4;	/* rounding to 1 */

	sensor_write(sd, 0x3012, exp_val);	/* coarse integration time */

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
	unsigned short dig_gain = 0x80;	/* 1 times digital gain */

	if (gain_val < 16)
		gain_val = 16;

	gain_val = gain_val * 100;

	if (16 * 100 <= gain_val && gain_val < (103 * 16))
		sensor_write(sd, 0x3060, 0x0000);
	else if ((103 * 16) <= gain_val && gain_val < (107 * 16))
		sensor_write(sd, 0x3060, 0x0001);
	else if ((107 * 16) <= gain_val && gain_val < (110 * 16))
		sensor_write(sd, 0x3060, 0x0002);
	else if ((110 * 16) <= gain_val && gain_val < (114 * 16))
		sensor_write(sd, 0x3060, 0x0003);
	else if ((114 * 16) <= gain_val && gain_val < (119 * 16))
		sensor_write(sd, 0x3060, 0x0004);
	else if ((119 * 16) <= gain_val && gain_val < (123 * 16))
		sensor_write(sd, 0x3060, 0x0005);
	else if ((123 * 16) <= gain_val && gain_val < (128 * 16))
		sensor_write(sd, 0x3060, 0x0006);
	else if ((128 * 16) <= gain_val && gain_val < (133 * 16))
		sensor_write(sd, 0x3060, 0x0007);
	else if ((133 * 16) <= gain_val && gain_val < (139 * 16))
		sensor_write(sd, 0x3060, 0x0008);
	else if ((139 * 16) <= gain_val && gain_val < (145 * 16))
		sensor_write(sd, 0x3060, 0x0009);
	else if ((145 * 16) <= gain_val && gain_val < (152 * 16))
		sensor_write(sd, 0x3060, 0x000a);
	else if ((152 * 16) <= gain_val && gain_val < (160 * 16))
		sensor_write(sd, 0x3060, 0x000b);
	else if ((160 * 16) <= gain_val && gain_val < (168 * 16))
		sensor_write(sd, 0x3060, 0x000c);
	else if ((168 * 16) <= gain_val && gain_val < (178 * 16))
		sensor_write(sd, 0x3060, 0x000d);
	else if ((178 * 16) <= gain_val && gain_val < (188 * 16))
		sensor_write(sd, 0x3060, 0x000e);
	else if ((188 * 16) <= gain_val && gain_val < (200 * 16))
		sensor_write(sd, 0x3060, 0x000f);
	else if ((200 * 16) <= gain_val && gain_val < (213 * 16)) {
		sensor_write(sd, 0x3060, 0x0010);
		dig_gain = gain_val * 128 / (200 * 16);
	} else if ((213 * 16) <= gain_val && gain_val < (229 * 16)) {
		sensor_write(sd, 0x3060, 0x0012);
		dig_gain = gain_val * 128 / (213 * 16);
	} else if ((229 * 16) <= gain_val && gain_val < (246 * 16)) {
		sensor_write(sd, 0x3060, 0x0014);
		dig_gain = gain_val * 128 / (229 * 16);
	} else if ((246 * 16) <= gain_val && gain_val < (267 * 16)) {
		sensor_write(sd, 0x3060, 0x0016);
		dig_gain = gain_val * 128 / (246 * 16);
	} else if ((267 * 16) <= gain_val && gain_val < (291 * 16)) {
		sensor_write(sd, 0x3060, 0x0018);
		dig_gain = gain_val * 128 / (267 * 16);
	} else if ((291 * 16) <= gain_val && gain_val < (320 * 16)) {
		sensor_write(sd, 0x3060, 0x001a);
		dig_gain = gain_val * 128 / (291 * 16);
	} else if ((320 * 16) <= gain_val && gain_val < (356 * 16)) {
		sensor_write(sd, 0x3060, 0x001c);
		dig_gain = gain_val * 128 / (320 * 16);
	} else if ((356 * 16) <= gain_val && gain_val < (400 * 16)) {
		sensor_write(sd, 0x3060, 0x001e);
		dig_gain = gain_val * 128 / (356 * 16);
	} else if ((400 * 16) <= gain_val && gain_val < (457 * 16)) {
		sensor_write(sd, 0x3060, 0x0020);
		dig_gain = gain_val * 128 / (400 * 16);
	} else if ((457 * 16) <= gain_val && gain_val < (533 * 16)) {
		sensor_write(sd, 0x3060, 0x0024);
		dig_gain = gain_val * 128 / (457 * 16);
	} else if ((533 * 16) <= gain_val && gain_val < (640 * 16)) {
		sensor_write(sd, 0x3060, 0x0028);
		dig_gain = gain_val * 128 / (533 * 16);
	} else if ((640 * 16) <= gain_val && gain_val < (800 * 16)) {
		sensor_write(sd, 0x3060, 0x002c);
		dig_gain = gain_val * 128 / (640 * 16);
	} else if ((800 * 16) <= gain_val) {
		sensor_write(sd, 0x3060, 0x0030);
		dig_gain = gain_val * 128 / (800 * 16);
	}

	sensor_write(sd, 0x305e, dig_gain);

	info->gain = gain_val;
	return 0;
}

static int ar0238_sensor_vts;
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
	if (shutter > ar0238_sensor_vts - 4)
		frame_length = shutter + 4;
	else
		frame_length = ar0238_sensor_vts;

	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);
	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}
static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	unsigned short rdtmp;
	ret = sensor_read(sd, 0x301a, &rdtmp);
	if (ret != 0)
		return ret;
	if (on_off == 1)
		sensor_write(sd, 0x301a, (rdtmp & 0xfffb));
	else
		sensor_write(sd, 0x301a, rdtmp | 0x04);
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
		sensor_dbg("CSI_SUBDEV_STBY_ON!\n");
		ret = sensor_s_sw_stby(sd, CSI_GPIO_HIGH);
		if (ret < 0)
			sensor_err("soft stby falied!\n");
		usleep_range(10000, 12000);
		cci_lock(sd);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		cci_unlock(sd);
		vin_set_mclk(sd, OFF);
		break;
	case STBY_OFF:
		sensor_dbg("CSI_SUBDEV_STBY_OFF!\n");
		cci_lock(sd);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		usleep_range(10000, 12000);
		ret = sensor_s_sw_stby(sd, CSI_GPIO_LOW);
		if (ret < 0)
			sensor_err("soft stby off falied!\n");
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_dbg("CSI_SUBDEV_PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_set_status(sd, POWER_EN, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		usleep_range(1000, 1200);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		vin_set_pmu_channel(sd, IOVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);
		usleep_range(1000, 1200);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(2000, 2200);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(2000, 2200);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(1000, 1200);
		sensor_dbg("CSI_SUBDEV_STBY_OFF!\n");
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_dbg("CSI_SUBDEV_PWR_OFF!\n");
		cci_lock(sd);
		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_mclk(sd, OFF);
		cci_unlock(sd);
		usleep_range(10000, 12000);
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
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(10000, 12000);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(10000, 12000);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	unsigned short rdval = 0;
	int cnt = 0;

	sensor_read(sd, 0x3000, &rdval);
	sensor_print("V4L2_IDENT_SENSOR = 0x%x\n", rdval);

	while ((rdval != V4L2_IDENT_SENSOR) && (cnt < 5)) {
		sensor_read(sd, 0x3000, &rdval);
		sensor_print("retry = %d, V4L2_IDENT_SENSOR = %x\n", cnt, rdval);
		cnt++;
	}

	if (rdval != V4L2_IDENT_SENSOR)
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
	info->width = HD1080_WIDTH;
	info->height = HD1080_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30;	/* 30fps */
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
			memcpy(arg,
			       info->current_wins,
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
	 .mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
	 .regs = sensor_fmt_raw,
	 .regs_size = ARRAY_SIZE(sensor_fmt_raw),
	 .bpp = 1,
	 },
};

#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {
	/* 1080P */
	{
	 .width = HD1080_WIDTH,
	 .height = HD1080_HEIGHT,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2236,
	 .vts = VMAX,
	 .pclk = 18562500,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 1 << 4,
	 .intg_max = (VMAX - 4) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 32 << 4,
	 .regs = sensor_1080p30_regs,
	 .regs_size = ARRAY_SIZE(sensor_1080p30_regs),
	 .set_size = NULL,
	 },
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = V4L2_MBUS_MASTER | VREF_POL | HREF_POL | CLK_POL;

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
	ar0238_sensor_vts = wsize->vts;

	sensor_print("s_fmt set width = %d, height = %d\n", wsize->width,
		     wsize->height);

	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);
	sensor_print("%s on = %d, %d*%d %x\n", __func__, enable,
		     info->current_wins->width,
		     info->current_wins->height, info->fmt->mbus_code);

	if (!enable) {
		vin_gpio_set_status(sd, SM_HS, 0);
		vin_gpio_set_status(sd, SM_VS, 0);
		return 0;
	} else {
		vin_gpio_set_status(sd, SM_VS, 2);
		vin_gpio_set_status(sd, SM_HS, 2);
	}
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
