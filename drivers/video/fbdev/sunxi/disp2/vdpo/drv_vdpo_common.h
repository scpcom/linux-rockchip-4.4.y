/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _DRV_VDPO_COMMON_H
#define _DRV_VDPO_COMMON_H

#include <linux/module.h>
#include <linux/uaccess.h>
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
#include <asm/memory.h>
#endif
#include <asm/unistd.h>
#include "asm-generic/int-ll64.h"
#include "linux/kernel.h"
#include "linux/mm.h"
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/compat.h>
#include <video/sunxi_display2.h>

#define VDPO_DEBUG_LEVEL 0

#define vdpo_wrn(fmt, ...)                                                     \
	pr_warn("[VDPO] %s:%d " fmt "", __func__, __LINE__, ##__VA_ARGS__)

#if VDPO_DEBUG_LEVEL == 2
#define vdpo_here pr_warn("[VDPO] %s:%d\n", __func__, __LINE__)
#else
#define vdpo_here
#endif /*endif VDPO_DEBUG */

#if VDPO_DEBUG_LEVEL >= 1
#define vdpo_dbg(fmt, ...)                                                     \
	pr_warn("[VDPO] %s:%d " fmt "", __func__, __LINE__, ##__VA_ARGS__)
#else
#define vdpo_dbg(fmt, ...)
#endif /*endif VDPO_DEBUG */

#endif /*End of file*/
