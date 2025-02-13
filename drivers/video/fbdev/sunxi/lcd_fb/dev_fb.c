/*
 * dev_fb/dev_fb.c
 *
 * Copyright (c) 2007-2019 Allwinnertech Co., Ltd.
 * Author: zhengxiaobin <zhengxiaobin@allwinnertech.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "include.h"
#include "dev_fb.h"
#include "disp_display.h"
#include "logo.h"
#include <linux/pinctrl/pinctrl-sunxi.h>

#ifndef dma_mmap_writecombine
#define dma_mmap_writecombine dma_mmap_wc
#endif

struct fb_info_t {
	struct device *dev;
	bool fb_enable[LCD_FB_MAX];
	struct fb_info *fbinfo[LCD_FB_MAX];
	int blank[LCD_FB_MAX];
	int fb_index[LCD_FB_MAX];
	u32 pseudo_palette[LCD_FB_MAX][16];
};

static struct fb_info_t g_fbi;

static int lcd_fb_open(struct fb_info *info, int user)
{
	return 0;
}
static int lcd_fb_release(struct fb_info *info, int user)
{
	return 0;
}

void lcd_fb_black_screen(u32 sel)
{
	if (sel >= LCD_FB_MAX) {
		lcd_fb_wrn("Exceed max fb:%d >= %d\n", sel, LCD_FB_MAX);
		return;
	}

	bsp_disp_lcd_set_layer(sel, g_fbi.fbinfo[sel]);
}

static int lcd_fb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	u32 sel = 0;
	struct fb_info tmp_info;
	memcpy(&tmp_info, info, sizeof(struct fb_info));
	memcpy(&tmp_info.var, var, sizeof(struct fb_var_screeninfo));

	for (sel = 0; sel < LCD_FB_MAX; sel++) {
		if (sel == g_fbi.fb_index[info->node]) {
			bsp_disp_lcd_set_layer(sel, &tmp_info);
			bsp_disp_lcd_wait_for_vsync(sel);
		}
	}
	return 0;
}

static int lcd_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned int offset = vma->vm_pgoff << PAGE_SHIFT;

	if (offset < info->fix.smem_len) {
		return dma_mmap_writecombine(g_fbi.dev, vma, info->screen_base,
					     info->fix.smem_start,
					     info->fix.smem_len);
	}

	return -EINVAL;
}

static int lcd_fb_set_par(struct fb_info *info)
{
	return bsp_disp_lcd_set_var(info->node, info);
}

static int lcd_fb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	return 0;
}
static int lcd_fb_blank(int blank_mode, struct fb_info *info)
{
	if (blank_mode == FB_BLANK_POWERDOWN)
		return bsp_disp_lcd_blank(info->node, 1);
	else
		return bsp_disp_lcd_blank(info->node, 0);
}

static inline u32 convert_bitfield(int val, struct fb_bitfield *bf)
{
	u32 mask = ((1 << bf->length) - 1) << bf->offset;

	return (val << bf->offset) & mask;
}

static int lcd_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp,
			      struct fb_info *info)
{
	u32 val;
	u32 ret = 0;

	switch (info->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		ret = -EINVAL;
		break;
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			val = convert_bitfield(transp, &info->var.transp) |
			    convert_bitfield(red, &info->var.red) |
			    convert_bitfield(green, &info->var.green) |
			    convert_bitfield(blue, &info->var.blue);
			((u32 *) info->pseudo_palette)[regno] = val;
		} else {
			ret = 0;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int lcd_fb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	unsigned int j, r = 0;
	unsigned char hred, hgreen, hblue, htransp = 0xff;
	unsigned short *red, *green, *blue, *transp;


	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;

	for (j = 0; j < cmap->len; j++) {
		hred = *red++;
		hgreen = *green++;
		hblue = *blue++;
		if (transp)
			htransp = (*transp++) & 0xff;
		else
			htransp = 0xff;

		r = lcd_fb_setcolreg(cmap->start + j, hred, hgreen, hblue,
				       htransp, info);
		if (r)
			return r;
	}

	return 0;
}

static struct fb_ops lcdfb_ops = {
	.owner = THIS_MODULE,
	.fb_open = lcd_fb_open,
	.fb_release = lcd_fb_release,
	.fb_pan_display = lcd_fb_pan_display,
	/*.fb_ioctl = lcd_fb_ioctl,*/
	.fb_check_var = lcd_fb_check_var,
	.fb_set_par = lcd_fb_set_par,
	.fb_blank = lcd_fb_blank,
	/*.fb_cursor = lcd_fb_cursor,*/
	.fb_mmap = lcd_fb_mmap,
#if defined(CONFIG_FB_CONSOLE_SUNXI)
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
#endif
	.fb_setcmap = lcd_fb_setcmap,
	.fb_setcolreg = lcd_fb_setcolreg,

};

static s32 pixel_format_to_var(enum lcdfb_pixel_format format,
			  struct fb_var_screeninfo *var)
{
	switch (format) {
	case LCDFB_FORMAT_ARGB_8888:
		var->bits_per_pixel = 32;
		var->transp.length = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->blue.offset = 0;
		var->green.offset = var->blue.offset + var->blue.length;
		var->red.offset = var->green.offset + var->green.length;
		var->transp.offset = var->red.offset + var->red.length;
		break;
	case LCDFB_FORMAT_ABGR_8888:
		var->bits_per_pixel = 32;
		var->transp.length = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->red.offset = 0;
		var->green.offset = var->red.offset + var->red.length;
		var->blue.offset = var->green.offset + var->green.length;
		var->transp.offset = var->blue.offset + var->blue.length;
		break;
	case LCDFB_FORMAT_RGBA_8888:
		var->bits_per_pixel = 32;
		var->transp.length = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->blue.offset = var->transp.offset + var->transp.length;
		var->green.offset = var->blue.offset + var->blue.length;
		var->red.offset = var->green.offset + var->green.length;
		break;
	case LCDFB_FORMAT_BGRA_8888:
		var->bits_per_pixel = 32;
		var->transp.length = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->red.offset = var->transp.offset + var->transp.length;
		var->green.offset = var->red.offset + var->red.length;
		var->blue.offset = var->green.offset + var->green.length;
		break;
	case LCDFB_FORMAT_RGB_888:
		var->bits_per_pixel = 24;
		var->transp.length = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->blue.offset = 0;
		var->green.offset = var->blue.offset + var->blue.length;
		var->red.offset = var->green.offset + var->green.length;

		break;
	case LCDFB_FORMAT_BGR_888:
		var->bits_per_pixel = 24;
		var->transp.length = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->red.offset = 0;
		var->green.offset = var->red.offset + var->red.length;
		var->blue.offset = var->green.offset + var->green.length;

		break;
	case LCDFB_FORMAT_RGB_565:
		var->bits_per_pixel = 16;
		var->transp.length = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		var->blue.offset = 0;
		var->green.offset = var->blue.offset + var->blue.length;
		var->red.offset = var->green.offset + var->green.length;

		break;
	case LCDFB_FORMAT_BGR_565:
		var->bits_per_pixel = 16;
		var->transp.length = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		var->red.offset = 0;
		var->green.offset = var->red.offset + var->red.length;
		var->blue.offset = var->green.offset + var->green.length;

		break;
	default:
		lcd_fb_wrn("[FB]not support format %d\n", format);
	}

	lcd_fb_inf
	    ("fmt%d para: %dbpp, a(%d,%d),r(%d,%d),g(%d,%d),b(%d,%d)\n",
	     (int)format, (int)var->bits_per_pixel, (int)var->transp.offset,
	     (int)var->transp.length, (int)var->red.offset,
	     (int)var->red.length, (int)var->green.offset,
	     (int)var->green.length, (int)var->blue.offset,
	     (int)var->blue.length);

	return 0;
}

static int fb_map_video_memory(struct fb_info *info)
{
	info->screen_base =
	    (char __iomem *)lcd_fb_dma_malloc(info->fix.smem_len,
					(u32 *) (&info->fix.smem_start));
	if (info->screen_base) {
		lcd_fb_inf("%s(reserve),va=0x%p, pa=0x%p size:0x%x\n", __func__,
		      (void *)info->screen_base,
		      (void *)info->fix.smem_start,
		      (unsigned int)info->fix.smem_len);
		memset((void *__force)info->screen_base, 0x0,
		       info->fix.smem_len);


		return 0;
	}

	lcd_fb_wrn("%s fail!\n", __func__);
	return -ENOMEM;

	return 0;
}

static inline void fb_unmap_video_memory(struct fb_info *info)
{
	if (!info->screen_base) {
		lcd_fb_wrn("%s: screen_base is null\n", __func__);
		return;
	}
	lcd_fb_inf("%s: screen_base=0x%p, smem=0x%p, len=0x%x\n", __func__,
	      (void *)info->screen_base,
	      (void *)info->fix.smem_start, info->fix.smem_len);
	lcd_fb_dma_free((void *__force)info->screen_base,
		  (void *)info->fix.smem_start, info->fix.smem_len);
	info->screen_base = 0;
	info->fix.smem_start = 0;
}

#ifdef CONFIG_FB_DEFERRED_IO
static void lcdfb_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	u32 sel = 0;
	for (sel = 0; sel < LCD_FB_MAX; sel++) {
		if (sel == g_fbi.fb_index[info->node]) {
			bsp_disp_lcd_set_layer(sel, g_fbi.fbinfo[sel]);
		}
	}

}
#endif

int fb_init(struct dev_lcd_fb_t *p_info)
{
	int i = 0, buffer_num = 2, pixel_format = 0;
	struct disp_panel_para info;
#ifdef CONFIG_FB_DEFERRED_IO
	struct fb_deferred_io *fbdefio = NULL;
#endif

	g_fbi.dev = p_info->device;

	if (p_info->lcd_fb_num > LCD_FB_MAX) {
		lcd_fb_wrn("Only %d fb devices is support\n", LCD_FB_MAX);
		return -1;
	}
	for (i = 0; i < p_info->lcd_fb_num; i++) {
		g_fbi.fbinfo[i] = framebuffer_alloc(0, g_fbi.dev);
		g_fbi.fbinfo[i]->fbops = &lcdfb_ops;
		g_fbi.fbinfo[i]->flags = 0;
		g_fbi.fbinfo[i]->device = g_fbi.dev;
		g_fbi.fbinfo[i]->par = &g_fbi;
		g_fbi.fbinfo[i]->var.xoffset = 0;
		g_fbi.fbinfo[i]->var.yoffset = 0;
		g_fbi.fbinfo[i]->var.xres = 800;
		g_fbi.fbinfo[i]->var.yres = 480;
		g_fbi.fbinfo[i]->var.xres_virtual = 800;
		g_fbi.fbinfo[i]->var.yres_virtual = 480 * 2;
		g_fbi.fbinfo[i]->var.nonstd = 0;
		g_fbi.fbinfo[i]->var.bits_per_pixel = 32;
		g_fbi.fbinfo[i]->var.transp.length = 8;
		g_fbi.fbinfo[i]->var.red.length = 8;
		g_fbi.fbinfo[i]->var.green.length = 8;
		g_fbi.fbinfo[i]->var.blue.length = 8;
		g_fbi.fbinfo[i]->var.transp.offset = 24;
		g_fbi.fbinfo[i]->var.red.offset = 16;
		g_fbi.fbinfo[i]->var.green.offset = 8;
		g_fbi.fbinfo[i]->var.blue.offset = 0;
		g_fbi.fbinfo[i]->var.activate = FB_ACTIVATE_FORCE;
		g_fbi.fbinfo[i]->fix.type = FB_TYPE_PACKED_PIXELS;
		g_fbi.fbinfo[i]->fix.type_aux = 0;
		g_fbi.fbinfo[i]->fix.visual = FB_VISUAL_TRUECOLOR;
		g_fbi.fbinfo[i]->fix.xpanstep = 1;
		g_fbi.fbinfo[i]->fix.ypanstep = 1;
		g_fbi.fbinfo[i]->fix.ywrapstep = 0;
		g_fbi.fbinfo[i]->fix.accel = FB_ACCEL_NONE;
		g_fbi.fbinfo[i]->fix.line_length =
		    g_fbi.fbinfo[i]->var.xres_virtual * 4;
		g_fbi.fbinfo[i]->fix.smem_len =
		    g_fbi.fbinfo[i]->fix.line_length *
		    g_fbi.fbinfo[i]->var.yres_virtual * 2;
		g_fbi.fbinfo[i]->screen_base = NULL;
		g_fbi.fbinfo[i]->pseudo_palette = g_fbi.pseudo_palette[i];
		g_fbi.fbinfo[i]->fix.smem_start = 0x0;
		g_fbi.fbinfo[i]->fix.mmio_start = 0;
		g_fbi.fbinfo[i]->fix.mmio_len = 0;

		if (fb_alloc_cmap(&g_fbi.fbinfo[i]->cmap, 256, 1) < 0)
			return -ENOMEM;
		buffer_num = 2;
		pixel_format = LCDFB_FORMAT_ARGB_8888;
		memset(&info, 0, sizeof(struct disp_panel_para));
		if (!bsp_disp_get_panel_info(i, &info)) {
			buffer_num = info.fb_buffer_num;
			pixel_format = info.lcd_pixel_fmt;
		}

#ifdef CONFIG_FB_DEFERRED_IO
		fbdefio = NULL;
		fbdefio = devm_kzalloc(g_fbi.dev, sizeof(struct fb_deferred_io), GFP_KERNEL);
		if (!fbdefio)
			return -1;
		g_fbi.fbinfo[i]->fbdefio = fbdefio;
		fbdefio->delay =           HZ/info.lcd_fps;
		fbdefio->deferred_io =     lcdfb_deferred_io;
		fb_deferred_io_init(g_fbi.fbinfo[i]);
#endif

		pixel_format_to_var(pixel_format,
			       &(g_fbi.fbinfo[i]->var));

		g_fbi.fbinfo[i]->var.xoffset = 0;
		g_fbi.fbinfo[i]->var.yoffset = 0;
		g_fbi.fbinfo[i]->var.xres = bsp_disp_get_screen_width(i);
		g_fbi.fbinfo[i]->var.yres = bsp_disp_get_screen_height(i);
		g_fbi.fbinfo[i]->var.xres_virtual = bsp_disp_get_screen_width(i);
		g_fbi.fbinfo[i]->fix.line_length =
		    (g_fbi.fbinfo[i]->var.xres * g_fbi.fbinfo[i]->var.bits_per_pixel) >> 3;

		g_fbi.fbinfo[i]->fix.smem_len =
			g_fbi.fbinfo[i]->fix.line_length * g_fbi.fbinfo[i]->var.yres * buffer_num;

		if (g_fbi.fbinfo[i]->fix.line_length != 0)
			g_fbi.fbinfo[i]->var.yres_virtual =
				g_fbi.fbinfo[i]->fix.smem_len / g_fbi.fbinfo[i]->fix.line_length;
		fb_map_video_memory(g_fbi.fbinfo[i]);

		/*TODO:set fb timing*/

		g_fbi.fbinfo[i]->var.width =
			bsp_disp_get_screen_physical_width(i);
		g_fbi.fbinfo[i]->var.height =
			bsp_disp_get_screen_physical_height(i);
		g_fbi.fb_enable[i] = 1;
		/*TODO:display something?*/
		g_fbi.fb_index[i] = i;
		register_framebuffer(g_fbi.fbinfo[i]);
		logo_parse(g_fbi.fbinfo[i]);
	}

	return 0;
}

int fb_exit(void)
{
	unsigned int fb_id = 0;

	for (fb_id = 0; fb_id < LCD_FB_MAX; fb_id++) {
		if (g_fbi.fbinfo[fb_id]) {
#ifdef CONFIG_FB_DEFERRED_IO
			fb_deferred_io_cleanup(g_fbi.fbinfo[fb_id]);
#endif
			fb_dealloc_cmap(&g_fbi.fbinfo[fb_id]->cmap);
			fb_unmap_video_memory(g_fbi.fbinfo[fb_id]);
			unregister_framebuffer(g_fbi.fbinfo[fb_id]);
			framebuffer_release(g_fbi.fbinfo[fb_id]);
			g_fbi.fbinfo[fb_id] = NULL;
		}
	}

	return 0;
}
