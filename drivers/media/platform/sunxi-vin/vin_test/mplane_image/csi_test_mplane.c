/*
 * drivers/media/platform/sunxi-vin/vin_test/mplane_image/csi_test_mplane.c
 *
 * Copyright (c) 2014 softwinner.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * zw
 * for csi & isp test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/version.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>

#include "../sunxi_camera_v2.h"
#include "../sunxi_display2.h"

#define CLEAR(x) (memset(&(x), 0, sizeof(x)))
#define ALIGN_4K(x) (((x) + (4095)) & ~(4095))
#define ALIGN_16B(x) (((x) + (15)) & ~(15))

#define display_frame 0
#define video_s_ctrl 0

struct size {
	int width;
	int height;
};
struct buffer {
	void *start[3];
	int length[3];
};

typedef enum {
	TVD_PL_YUV420 = 0,
	TVD_MB_YUV420 = 1,
	TVD_PL_YUV422 = 2,
} TVD_FMT_T;

struct disp_screen {
	int x;
	int y;
	int w;
	int h;
};

struct test_layer_info {
	int screen_id;
	int layer_id;
	int mem_id;
	disp_layer_config layer_config;
	int addr_map;
	int width, height;/* screen size */
	int dispfh;/* device node handle */
	int fh;/* picture resource file handle */
	int mem;
	int clear;/* is clear layer */
	char filename[32];
	int full_screen;
	unsigned int pixformat;
	disp_output_type output_type;
};

/**
 * tvd_dev info
 */
struct tvd_dev {
	unsigned int ch_id;
	unsigned int height;
	unsigned int width;
	unsigned int interface;
	unsigned int system;
	unsigned int row;
	unsigned int column;
	unsigned int ch0_en;
	unsigned int ch1_en;
	unsigned int ch2_en;
	unsigned int ch3_en;
	unsigned int pixformat;
	struct test_layer_info layer_info;
	int frame_no_to_grap;
	FILE *raw_fp;
};
struct tvd_dev dev;

static char path_name[20];
static char dev_name[20];
static int fd = -1;
static int isp0_fd = -1;
static int isp1_fd = -1;

struct buffer *buffers;
static unsigned int n_buffers;

struct size input_size;

unsigned int req_frame_num = 5;
unsigned int read_num = 20;
unsigned int count;
unsigned int nplanes;
unsigned int save_flag;
int dev_id;
unsigned int fps = 30;
unsigned int wdr_mode;

#define ROT_90 0

static void yuv_r90(char *dst, char *src, int width, int height)
{
	int i = 0, j = 0;

	for (i = 0; i < width; i++) {
		for (j = 0; j < height; j++)
			*(char *)(dst + j + i * height) = *(char *)(src + (height - j - 1) * width + i);
	}
}

static void uv_r90(char *dst, char *src, int width, int height)
{
	int i = 0, j = 0;

	for (i = 0; i < width/2; i++) {
		for (j = 0; j < height/2; j++)
			*(char *)(dst + j * 2 + i * height) = *(char *)(src + (height/2 - j - 1) * width + i * 2);
	}

	for (i = 0; i < width/2; i++) {
		for (j = 0; j < height/2; j++)
			*(char *)(dst + j * 2 + 1 + i * height) = *(char *)(src + (height/2 - j - 1) * width + i * 2 + 1);
	}
}

static int disp_set_addr(int width, int height, struct v4l2_buffer *buf)

{
	unsigned long arg[6];
	int ret;

	if (dev.layer_info.pixformat == TVD_PL_YUV420) {
		/* printf("******YUV420!\n"); */
		dev.layer_info.layer_config.info.fb.size[0].width = width;
		dev.layer_info.layer_config.info.fb.size[0].height = height;
		dev.layer_info.layer_config.info.fb.size[1].width = width / 2;
		dev.layer_info.layer_config.info.fb.size[1].height = height / 2;
		dev.layer_info.layer_config.info.fb.size[2].width = width / 2;
		dev.layer_info.layer_config.info.fb.size[2].height = height / 2;
		dev.layer_info.layer_config.info.fb.crop.width =
		    (unsigned long long)width << 32;
		dev.layer_info.layer_config.info.fb.crop.height =
		    (unsigned long long)height << 32;

		dev.layer_info.layer_config.info.fb.addr[0] = buf->m.planes[0].m.mem_offset;
		dev.layer_info.layer_config.info.fb.addr[1] = buf->m.planes[1].m.mem_offset;
		dev.layer_info.layer_config.info.fb.addr[2] = buf->m.planes[2].m.mem_offset;

		/* dev.layer_info.layer_config.info.fb.addr[0] = (*addr);
		dev.layer_info.layer_config.info.fb.addr[1] =
		    (dev.layer_info.layer_config.info.fb.addr[0] + width * height);
		dev.layer_info.layer_config.info.fb.addr[2] =
		    dev.layer_info.layer_config.info.fb.addr[0] +
		    width * height * 5 / 4;
		dev.layer_info.layer_config.info.fb.trd_right_addr[0] =
		    (dev.layer_info.layer_config.info.fb.addr[0] +
		     width * height * 3 / 2);
		dev.layer_info.layer_config.info.fb.trd_right_addr[1] =
		    (dev.layer_info.layer_config.info.fb.addr[0] + width * height);
		dev.layer_info.layer_config.info.fb.trd_right_addr[2] =
		    (dev.layer_info.layer_config.info.fb.addr[0] +
		     width * height * 5 / 4); */
	} else {
		dev.layer_info.layer_config.info.fb.size[0].width = width;
		dev.layer_info.layer_config.info.fb.size[0].height = height;
		dev.layer_info.layer_config.info.fb.size[1].width = width / 2;
		dev.layer_info.layer_config.info.fb.size[1].height = height;
		dev.layer_info.layer_config.info.fb.size[2].width = width / 2;
		dev.layer_info.layer_config.info.fb.size[2].height = height;
		dev.layer_info.layer_config.info.fb.crop.width =
		    (unsigned long long)width << 32;
		dev.layer_info.layer_config.info.fb.crop.height =
		    (unsigned long long)height << 32;

		dev.layer_info.layer_config.info.fb.addr[0] = buf->m.planes[0].m.mem_offset;
		dev.layer_info.layer_config.info.fb.addr[1] = buf->m.planes[1].m.mem_offset;
		dev.layer_info.layer_config.info.fb.addr[2] = buf->m.planes[2].m.mem_offset;

		/* dev.layer_info.layer_config.info.fb.addr[0] = (*addr);
		dev.layer_info.layer_config.info.fb.addr[1] =
		    (dev.layer_info.layer_config.info.fb.addr[0] + width * height);
		dev.layer_info.layer_config.info.fb.addr[2] =
		    dev.layer_info.layer_config.info.fb.addr[0] +
		    width * height * 2 / 2;
		dev.layer_info.layer_config.info.fb.trd_right_addr[0] =
		    (dev.layer_info.layer_config.info.fb.addr[0] +
		     width * height * 2);
		dev.layer_info.layer_config.info.fb.trd_right_addr[1] =
		    (dev.layer_info.layer_config.info.fb.addr[0] + width * height); */
	}

	dev.layer_info.layer_config.enable = 1;

	arg[0] = dev.layer_info.screen_id;
	arg[1] = (unsigned long)&dev.layer_info.layer_config;
	arg[2] = 1;
	arg[3] = 0;
	ret = ioctl(dev.layer_info.dispfh, DISP_LAYER_SET_CONFIG, (void *)arg);
	if (ret != 0)
		printf("disp_set_addr fail to set layer info\n");

	return 0;
}

static int read_frame(int mode)
{
	struct v4l2_buffer buf;
	char fdstr[50];
	FILE *file_fd = NULL;
	char *dst = NULL;

	CLEAR(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = nplanes;
	buf.m.planes =
	    (struct v4l2_plane *)calloc(nplanes, sizeof(struct v4l2_plane));

	if (-1 == ioctl(fd, VIDIOC_DQBUF, &buf)) {
		free(buf.m.planes);
		printf("VIDIOC_DQBUF failed\n");
		return -1;
	}

	assert(buf.index < n_buffers);

	if (save_flag == 0) {
		if ((count == read_num / 2) || ((count > 0) && (nplanes == 1))) {
			printf("file length = %d %d %d\n", buffers[buf.index].length[0],
			       buffers[buf.index].length[1],
			       buffers[buf.index].length[2]);
			printf("file start = %p %p %p\n", buffers[buf.index].start[0],
			       buffers[buf.index].start[1],
			       buffers[buf.index].start[2]);

			switch (nplanes) {
			case 1:
				sprintf(fdstr, "%s/fb%d_y%d_%d_%d_%u.bin", path_name, dev_id, mode, input_size.width, input_size.height, count);
				file_fd = fopen(fdstr, "w");
				fwrite(buffers[buf.index].start[0], buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				break;
			case 2:
#if ROT_90
				dst = (char *)malloc(buffers[buf.index].length[0]);
				yuv_r90(dst, buffers[buf.index].start[0], input_size.width, input_size.height);
				sprintf(fdstr, "%s/fb%d_y%d_%d_%d.bin", path_name, dev_id, mode, input_size.height, input_size.width);
				file_fd = fopen(fdstr, "w");
				fwrite(dst, buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				free(dst);

				dst = (char *)malloc(buffers[buf.index].length[1]);
				uv_r90(dst, buffers[buf.index].start[1], input_size.width, input_size.height);
				sprintf(fdstr, "%s/fb%d_uv%d_%d_%d.bin", path_name, dev_id, mode, input_size.height, input_size.width);
				file_fd = fopen(fdstr, "w");
				fwrite(dst, buffers[buf.index].length[1], 1, file_fd);
				fclose(file_fd);
				free(dst);
#else
				sprintf(fdstr, "%s/fb%d_y%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "w");
				fwrite(buffers[buf.index].start[0], buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				sprintf(fdstr, "%s/fb%d_uv%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "w");
				fwrite(buffers[buf.index].start[1], buffers[buf.index].length[1], 1, file_fd);
				fclose(file_fd);
#endif
				break;
			case 3:
#if ROT_90
				dst = (char *)malloc(buffers[buf.index].length[0]);
				yuv_r90(dst, buffers[buf.index].start[0], input_size.width, input_size.height);
				sprintf(fdstr, "%s/fb%d_y%d_%d_%d.bin", path_name, dev_id, mode, input_size.height, input_size.width);
				file_fd = fopen(fdstr, "w");
				fwrite(dst, buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				free(dst);

				dst = (char *)malloc(buffers[buf.index].length[1]);
				yuv_r90(dst, buffers[buf.index].start[1], input_size.width/2, input_size.height/2);
				sprintf(fdstr, "%s/fb%d_u%d_%d_%d.bin", path_name, dev_id, mode, input_size.height, input_size.width);
				file_fd = fopen(fdstr, "w");
				fwrite(dst, buffers[buf.index].length[1], 1, file_fd);
				fclose(file_fd);
				free(dst);

				dst = (char *)malloc(buffers[buf.index].length[2]);
				yuv_r90(dst, buffers[buf.index].start[2], input_size.width/2, input_size.height/2);
				sprintf(fdstr, "%s/fb%d_v%d_%d_%d.bin", path_name, dev_id, mode, input_size.height, input_size.width);
				file_fd = fopen(fdstr, "w");
				fwrite(dst, buffers[buf.index].length[2], 1, file_fd);
				fclose(file_fd);
				free(dst);
#else
				sprintf(fdstr, "%s/fb%d_y%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "w");
				fwrite(buffers[buf.index].start[0], buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);

				sprintf(fdstr, "%s/fb%d_u%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "w");
				fwrite(buffers[buf.index].start[1], buffers[buf.index].length[1], 1, file_fd);
				fclose(file_fd);

				sprintf(fdstr, "%s/fb%d_v%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "w");
				fwrite(buffers[buf.index].start[2], buffers[buf.index].length[2], 1, file_fd);
				fclose(file_fd);
#endif
				break;
			default:
				break;
			}
		}
	} else if (save_flag == 1) {
		//if ((count > 0) && (count % 4 == 0)) {
		if ((count > 0)) {
			switch (nplanes) {
			case 1:
				sprintf(fdstr, "%s/fb%d_yuv%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "ab");
				fwrite(buffers[buf.index].start[0], buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				break;
			case 2:
				sprintf(fdstr, "%s/fb%d_yuv%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "ab");
				fwrite(buffers[buf.index].start[0], buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				file_fd = fopen(fdstr, "ab");
				fwrite(buffers[buf.index].start[1], buffers[buf.index].length[1], 1, file_fd);
				fclose(file_fd);
				break;
			case 3:
				sprintf(fdstr, "%s/fb%d_yuv%d_%d_%d.bin", path_name, dev_id, mode, input_size.width, input_size.height);
				file_fd = fopen(fdstr, "ab");
				fwrite(buffers[buf.index].start[0], buffers[buf.index].length[0], 1, file_fd);
				fclose(file_fd);
				file_fd = fopen(fdstr, "ab");
				fwrite(buffers[buf.index].start[1], buffers[buf.index].length[1], 1, file_fd);
				fclose(file_fd);
				file_fd = fopen(fdstr, "ab");
				fwrite(buffers[buf.index].start[2], buffers[buf.index].length[2], 1, file_fd);
				fclose(file_fd);
				break;
			default:
				break;
			}
		}
	} else if (save_flag == 2) {
		if (count <= 1)
		count = read_num;
#if display_frame
		disp_set_addr(input_size.width, input_size.height, &buf);
#endif
	} else {
		count = 0;
	}

	if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)) {
		printf("VIDIOC_QBUF buf.index %d failed\n", buf.index);
		free(buf.m.planes);
		return -1;
	}

	free(buf.m.planes);

	return 0;
}

static struct disp_screen get_disp_screen(int w1, int h1, int w2, int h2)
{
	struct disp_screen screen;
	float r1, r2;

	r1 = (float)w1/(float)w2;
	r2 = (float)h1/(float)h2;
	if (r1 < r2) {
		screen.w = w2*r1;
		screen.h = h2*r1;
	} else {
		screen.w = w2*r2;
		screen.h = h2*r2;
	}

	screen.x = (w1 - screen.w)/2;
	screen.y = (h1 - screen.h)/2;

	return screen;
}

static int disp_disable(void)
{
#if display_frame
	int ret;
	unsigned long arg[6];
	struct disp_layer_config disp;

	/* release memory && clear layer */
	arg[0] = 0;
	arg[1] = 0;
	arg[2] = 0;
	arg[3] = 0;
	ioctl(dev.layer_info.dispfh, DISP_LAYER_DISABLE, (void *)arg);

	/*close channel 0*/
	memset(&disp, 0, sizeof(disp_layer_config));
	disp.channel = 0;
	disp.layer_id = 0;
	disp.enable = 0;
	arg[0] = dev.layer_info.screen_id;
	arg[1] = (unsigned long)&disp;
	arg[2] = 1;
	arg[3] = 0;
	ret = ioctl(dev.layer_info.dispfh, DISP_LAYER_SET_CONFIG, (void *)arg);
	if (ret != 0)
		printf("disp_disable:disp_set_addr fail to set layer info\n");

	/*close channel 2*/
	memset(&disp, 0, sizeof(disp_layer_config));
	disp.channel = 2;
	disp.layer_id = 0;
	disp.enable = 0;
	arg[0] = dev.layer_info.screen_id;
	arg[1] = (unsigned long)&disp;
	arg[2] = 1;
	arg[3] = 0;
	ret = ioctl(dev.layer_info.dispfh, DISP_LAYER_SET_CONFIG, (void *)arg);
	if (ret != 0)
		printf("disp_disable:disp_set_addr fail to set layer info\n");

	return ret;
#else
	return 0;
#endif
}

static int disp_init(int width, int height, unsigned int pixformat)
{
#if display_frame
	/* display_handle* disp = (display_handle*)display; */
	unsigned int arg[6] = {0};
	int layer_id = 0;

	dev.layer_info.screen_id = 0;

	if (dev.layer_info.screen_id < 0)
		return 0;

	/* open device /dev/disp */
	dev.layer_info.dispfh = open("/dev/disp", O_RDWR);
	if (dev.layer_info.dispfh == -1) {
		printf("open display device fail!\n");
		return -1;
	}

	/* open disp */
	arg[0] = dev.layer_info.screen_id;
	arg[1] = 1;
	arg[2] = 4;
	ioctl(
	    dev.layer_info.dispfh, DISP_DEVICE_SWITCH, (void *)arg);

	/* get current output type */
	arg[0] = dev.layer_info.screen_id;
	dev.layer_info.output_type = (disp_output_type)ioctl(
	    dev.layer_info.dispfh, DISP_GET_OUTPUT_TYPE, (void *)arg);
	if (dev.layer_info.output_type == DISP_OUTPUT_TYPE_NONE) {
		printf("the output type is DISP_OUTPUT_TYPE_NONE %d\n",
		       dev.layer_info.output_type);
		return -1;
	}

	disp_disable();

	dev.layer_info.pixformat = pixformat;
	dev.layer_info.layer_config.channel = 0;
	dev.layer_info.layer_config.layer_id = layer_id;
	dev.layer_info.layer_config.info.zorder = 1;
	dev.layer_info.layer_config.info.alpha_mode = 1;
	dev.layer_info.layer_config.info.alpha_value = 0xff;
	dev.layer_info.width =
	    ioctl(dev.layer_info.dispfh, DISP_GET_SCN_WIDTH, (void *)arg);
	dev.layer_info.height =
	    ioctl(dev.layer_info.dispfh, DISP_GET_SCN_HEIGHT, (void *)arg);

	dev.layer_info.layer_config.info.mode = LAYER_MODE_BUFFER;

	if (dev.layer_info.pixformat == TVD_PL_YUV420)
		dev.layer_info.layer_config.info.fb.format = DISP_FORMAT_YUV420_P;  /*DISP_FORMAT_YUV420_P ---- V4L2_PIX_FMT_YUV420M*/
		    //DISP_FORMAT_YUV420_SP_UVUV;  /*DISP_FORMAT_YUV420_SP_UVUV ---- V4L2_PIX_FMT_NV12*/
	else
		dev.layer_info.layer_config.info.fb.format =
		    DISP_FORMAT_YUV422_SP_VUVU;

	if (dev.layer_info.full_screen == 0 && width < dev.layer_info.width &&
	    height < dev.layer_info.height) {
		dev.layer_info.layer_config.info.screen_win.x =
		    (dev.layer_info.width - width) / 2;
		dev.layer_info.layer_config.info.screen_win.y =
		    (dev.layer_info.height - height) / 2;
		if (!dev.layer_info.layer_config.info.screen_win.width) {
			dev.layer_info.layer_config.info.screen_win.width = width;
			dev.layer_info.layer_config.info.screen_win.height =
			    height;
		}
	} else {
		/* struct disp_screen screen; */
		get_disp_screen(dev.layer_info.width, dev.layer_info.height,
				width, height);
		dev.layer_info.layer_config.info.screen_win.x = 0; /* screen.x; */
		dev.layer_info.layer_config.info.screen_win.y = 0; /* screen.y; */
		dev.layer_info.layer_config.info.screen_win.width =
		    dev.layer_info.width;
		dev.layer_info.layer_config.info.screen_win.height =
		    dev.layer_info.height;
		/* printf("x: %d, y: %d, w: %d, h: %d\n",screen.x,screen.y,screen.w,screen.h); */
	}
#endif
	return 0;
}

static void terminate(int sig_no)
{
	printf("Got signal %d, exiting ...\n", sig_no);
	disp_disable();
	usleep(20*1000);
	exit(1);
}

static void install_sig_handler(void)
{
	signal(SIGBUS, terminate);
	signal(SIGFPE, terminate);
	signal(SIGHUP, terminate);
	signal(SIGILL, terminate);
	signal(SIGKILL, terminate);
	signal(SIGINT, terminate);
	signal(SIGIOT, terminate);
	signal(SIGPIPE, terminate);
	signal(SIGQUIT, terminate);
	signal(SIGSEGV, terminate);
	signal(SIGSYS, terminate);
	signal(SIGTERM, terminate);
	signal(SIGTRAP, terminate);
	signal(SIGUSR1, terminate);
	signal(SIGUSR2, terminate);
}

static int req_frame_buffers(void)
{
	unsigned int i;
	struct v4l2_requestbuffers req;
	struct v4l2_exportbuffer exp;

	CLEAR(req);
	req.count = req_frame_num;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_MMAP;
	if (-1 == ioctl(fd, VIDIOC_REQBUFS, &req)) {
		printf("VIDIOC_REQBUFS error\n");
		return -1;
	}

	buffers = calloc(req.count, sizeof(*buffers));

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		buf.length = nplanes;
		buf.m.planes =
		    (struct v4l2_plane *)calloc(nplanes,
						sizeof(struct v4l2_plane));
		if (buf.m.planes == NULL) {
			printf("buf.m.planes calloc failed!\n");
			return -1;
		}
		if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &buf)) {
			printf("VIDIOC_QUERYBUF error\n");
			free(buf.m.planes);
			return -1;
		}

		for (i = 0; i < nplanes; i++) {
			buffers[n_buffers].length[i] = buf.m.planes[i].length;
			buffers[n_buffers].start[i] =
			    mmap(NULL,/* start anywhere */
				 buf.m.planes[i].length,
				 PROT_READ | PROT_WRITE,/* required */
				 MAP_SHARED, /* recommended */
				 fd, buf.m.planes[i].m.mem_offset);

			if (buffers[n_buffers].start[i] == MAP_FAILED) {
				printf("mmap failed\n");
				free(buf.m.planes);
				return -1;
			}
#if 0
			CLEAR(exp);
			exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			exp.index = n_buffers;
			exp.plane = i;
			exp.flags = O_CLOEXEC;
			if (-1 == ioctl(fd, VIDIOC_EXPBUF, &exp)) {
				printf("VIDIOC_EXPBUF error\n");
				return -1;
			}
			printf("buffer %d plane %d DMABUF fd is %d\n", n_buffers, i, exp.fd);
#endif
		}
		free(buf.m.planes);
	}

	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.length = nplanes;
		buf.m.planes =
		    (struct v4l2_plane *)calloc(nplanes,
						sizeof(struct v4l2_plane));

		if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)) {
			printf("VIDIOC_QBUF failed\n");
			free(buf.m.planes);
			return -1;
		}
		free(buf.m.planes);
	}
	return 0;
}

static int free_frame_buffers(void)
{
	unsigned int i, j;

	for (i = 0; i < n_buffers; ++i) {
		for (j = 0; j < nplanes; j++)
			if (-1 ==
			    munmap(buffers[i].start[j], buffers[i].length[j])) {
				printf("munmap error");
				return -1;
			}
	}
	free(buffers);
	return 0;
}

static int subdev_open(int *sub_fd, char *str)
{
	char subdev[20] = {'\0'};
	char node[50] = {'\0'};
	char data[20] = {'\0'};
	int i, fs = -1;

	for (i = 0; i < 255; i++) {
		sprintf(node, "/sys/class/video4linux/v4l-subdev%d/name", i);
		fs = open(node, O_RDONLY/* required */| O_NONBLOCK, 0);
		if (fs < 0) {
			printf("open %s falied\n", node);
			continue;
		}
		/*data_length = lseek(fd, 0, SEEK_END);*/
		lseek(fs, 0L, SEEK_SET);
		read(fs, data, 20);
		close(fs);
		if (!strncmp(str, data, strlen(str))) {
			sprintf(subdev, "/dev/v4l-subdev%d", i);
			printf("find %s is %s\n", str, subdev);
			*sub_fd = open(subdev, O_RDWR | O_NONBLOCK, 0);
			if (*sub_fd < 0) {
				printf("open %s falied\n", str);
				return -1;
			}
			printf("open %s fd = %d\n", str, *sub_fd);
			return 0;
		}
	}
	printf("can not find %s!\n", str);
	return -1;
}

static int camera_init(int sel, int mode)
{
	struct v4l2_input inp;
	struct v4l2_streamparm parms;

	fd = open(dev_name, O_RDWR /* required */  | O_NONBLOCK, 0);

	if (fd < 0) {
		printf("open falied\n");
		return -1;
	}
	printf("open %s fd = %d\n", dev_name, fd);

#ifdef SUBDEV_TEST
	if (-1 == subdev_open(&isp0_fd, "sunxi_isp.0"))
		return -1;
	if (-1 == subdev_open(&isp1_fd, "sunxi_isp.1"))
		return -1;
#endif

	inp.index = sel;
	if (-1 == ioctl(fd, VIDIOC_S_INPUT, &inp)) {
		printf("VIDIOC_S_INPUT %d error!\n", sel);
		return -1;
	}

	CLEAR(parms);
	parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	parms.parm.capture.timeperframe.numerator = 1;
	parms.parm.capture.timeperframe.denominator = fps;
	parms.parm.capture.capturemode = V4L2_MODE_VIDEO;
	/* parms.parm.capture.capturemode = V4L2_MODE_IMAGE; */
	/*when different video have the same sensor source, 1:use sensor current win, 0:find the nearest win*/
	parms.parm.capture.reserved[0] = 0;
	parms.parm.capture.reserved[1] = wdr_mode;/*2:command, 1: wdr, 0: normal*/

	if (-1 == ioctl(fd, VIDIOC_S_PARM, &parms)) {
		printf("VIDIOC_S_PARM error\n");
		return -1;
	}

	return 0;
}

static int camera_fmt_set(int mode)
{
	struct v4l2_format fmt;

	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width = input_size.width;
	fmt.fmt.pix_mp.height = input_size.height;
	switch (mode) {
	case 0:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_SBGGR8;
		break;
	case 1:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420M;
		break;
	case 2:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
		break;
	case 3:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
		break;
	case 4:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
		break;
	case 5:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_SBGGR10;
		break;
	case 6:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_SBGGR12;
		break;
	case 7:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_LBC_2_5X;
		break;
	default:
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420M;
		break;
	}
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

	if (-1 == ioctl(fd, VIDIOC_S_FMT, &fmt)) {
		printf("VIDIOC_S_FMT error!\n");
		return -1;
	}

	if (-1 == ioctl(fd, VIDIOC_G_FMT, &fmt)) {
		printf("VIDIOC_G_FMT error!\n");
		return -1;
	} else {
		nplanes = fmt.fmt.pix_mp.num_planes;
		printf("resolution got from sensor = %d*%d num_planes = %d\n",
		       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		       fmt.fmt.pix_mp.num_planes);
	}

	return 0;
}

static int video_set_control(int cmd, int value)
{
	struct v4l2_control control;

	control.id = cmd;
	control.value = value;
	if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
		printf("VIDIOC_S_CTRL failed\n");
		return -1;
	}
	return 0;
}

static int video_get_control(int cmd)
{
	struct v4l2_control control;

	control.id = cmd;
	if (-1 == ioctl(fd, VIDIOC_G_CTRL, &control)) {
		printf("VIDIOC_G_CTRL failed\n");
		return -1;
	}
	return control.value;
}

static int main_test(int sel, int mode)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct v4l2_ext_control ctrls[4];
	struct v4l2_ext_controls ext_ctrls;
	struct v4l2_control control;
	unsigned int pixformat;
	int ret;
	int i, j = 0;

	if (-1 == camera_init(sel, mode))
		return -1;
	if (-1 == camera_fmt_set(mode))
		return -1;
	if (-1 == req_frame_buffers())
		return -1;

	pixformat = TVD_PL_YUV420;
	ret = disp_init(input_size.width, input_size.height, pixformat);

	if (-1 == ioctl(fd, VIDIOC_STREAMON, &type)) {
		printf("VIDIOC_STREAMON failed\n");
		return -1;
	} else
		printf("VIDIOC_STREAMON ok\n");

	count = read_num;
	while (count-- > 0) {
		for (;;) {
			fd_set fds;
			struct timespec64 tv;
			int r;

			FD_ZERO(&fds);
			FD_SET(fd, &fds);

			tv.tv_sec = 2; /* Timeout. */
			tv.tv_nsec = 0;
#if video_s_ctrl
			if (count%3 == 0) {
				if (j == 0) {
					video_set_control(V4L2_CID_VFLIP, 0);
					video_set_control(V4L2_CID_HFLIP, 0);
					j = 1;
					printf("V4L2_CID_VFLIP done, j = %d, count = %d\n", j, count);
				} else {
					video_set_control(V4L2_CID_VFLIP, 1);
					video_set_control(V4L2_CID_HFLIP, 1);
					j = 0;
					printf("V4L2_CID_VFLIP no done, j = %d, count = %d\n", j, count);
				}
			}
#endif
#ifdef SUBDEV_TEST
			for (i = 0; i < 4; i++) {
				ctrls[i].id = V4L2_CID_R_GAIN + i;
				ctrls[i].value = count % 256;
			}
			memset(&ext_ctrls, 0, sizeof(ext_ctrls));
			ext_ctrls.ctrl_class = V4L2_CID_R_GAIN;
			ext_ctrls.count = 4;
			ext_ctrls.controls = ctrls;
			ioctl(isp0_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);

			for (i = 0; i < 4; i++) {
				ctrls[i].id = V4L2_CID_AE_WIN_X1 + i;
				ctrls[i].value = count*16 % 256;
			}
			memset(&ext_ctrls, 0, sizeof(ext_ctrls));
			ext_ctrls.ctrl_class = V4L2_CID_AE_WIN_X1;
			ext_ctrls.count = 4;
			ext_ctrls.controls = ctrls;
			ioctl(isp0_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);

			for (i = 0; i < 4; i++) {
				ctrls[i].id = V4L2_CID_AF_WIN_X1 + i;
				ctrls[i].value = count*16 % 256;
			}
			memset(&ext_ctrls, 0, sizeof(ext_ctrls));
			ext_ctrls.ctrl_class = V4L2_CID_AF_WIN_X1;
			ext_ctrls.count = 4;
			ext_ctrls.controls = ctrls;
			ioctl(isp0_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);

			if (count == read_num / 4) {
				control.id = V4L2_CID_VFLIP;
				control.value = 1;
				if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
					printf("VIDIOC_S_CTRL failed\n");
					return -1;
				} else
					printf("VIDIOC_S_CTRL ok\n");
			}

			if (count == read_num / 2) {
				control.id = V4L2_CID_HFLIP;
				control.value = 1;
				if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
					printf("VIDIOC_S_CTRL failed\n");
					return -1;
				} else
					printf("VIDIOC_S_CTRL ok\n");
			}
#endif

			r = select(fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (errno == EINTR)
					continue;
				printf("select err\n");
			}
			if (r == 0) {
				fprintf(stderr, "select timeout\n");
#ifdef TIMEOUT
				if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &type))
					printf("VIDIOC_STREAMOFF failed\n");
				else
					printf("VIDIOC_STREAMOFF ok\n");
				free_frame_buffers();
				return -1;
#else
				continue;
#endif
			}

			if (!read_frame(mode))
				break;
			else
				return -1;
		}
	}
	disp_disable();
	usleep(20*1000);

	if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &type)) {
		printf("VIDIOC_STREAMOFF failed\n");
		return -1;
	} else
		printf("VIDIOC_STREAMOFF ok\n");

	if (-1 == free_frame_buffers())
		return -1;
#if SUBDEV_TEST
	close(isp0_fd);
	close(isp1_fd);
#endif
	return 0;
}

int main(int argc, char *argv[])
{
	int i, test_cnt = 1;
	int sel = 0;
	int width = 640;
	int height = 480;
	int mode = 1;
	struct timespec64 tv1, tv2;
	float tv;

	install_sig_handler();

	CLEAR(dev_name);
	CLEAR(path_name);
	if (argc == 1) {
		sprintf(dev_name, "/dev/video0");
		sprintf(path_name, "/mnt/sdcard");
	} else if (argc == 3) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		sprintf(path_name, "/mnt/sdcard");
	} else if (argc == 5) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		width = atoi(argv[3]);
		height = atoi(argv[4]);
		sprintf(path_name, "/mnt/sdcard");
	} else if (argc == 6) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		width = atoi(argv[3]);
		height = atoi(argv[4]);
		sprintf(path_name, "%s", argv[5]);
	} else if (argc == 7) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		width = atoi(argv[3]);
		height = atoi(argv[4]);
		sprintf(path_name, "%s", argv[5]);
		mode = atoi(argv[6]);
	} else if (argc == 8) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		width = atoi(argv[3]);
		height = atoi(argv[4]);
		sprintf(path_name, "%s", argv[5]);
		mode = atoi(argv[6]);
		test_cnt = atoi(argv[7]);
	} else if (argc == 9) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		width = atoi(argv[3]);
		height = atoi(argv[4]);
		sprintf(path_name, "%s", argv[5]);
		mode = atoi(argv[6]);
		test_cnt = atoi(argv[7]);
		fps = atoi(argv[8]);
	} else if (argc == 10) {
		dev_id = atoi(argv[1]);
		sprintf(dev_name, "/dev/video%d", dev_id);
		sel = atoi(argv[2]);
		width = atoi(argv[3]);
		height = atoi(argv[4]);
		sprintf(path_name, "%s", argv[5]);
		mode = atoi(argv[6]);
		test_cnt = atoi(argv[7]);
		fps = atoi(argv[8]);
		wdr_mode = atoi(argv[9]);
	} else {
		printf("please select the video device: 0-video0 1-video1 ......\n");
		scanf("%d", &dev_id);
		sprintf(dev_name, "/dev/video%d", dev_id);

		printf("please select the camera: 0-dev0 1-dev1 ......\n");
		scanf("%d", &sel);

		printf("please input the resolution: width height......\n");
		scanf("%d %d", &width, &height);

		printf("please input the frame saving path......\n");
		scanf("%15s", path_name);

		printf("please input the test mode: 0~3......\n");
		scanf("%d", &mode);

		printf("please input the test_cnt: >=1......\n");
		scanf("%d", &test_cnt);
	}

	input_size.width = width;
	input_size.height = height;

	if (test_cnt < read_num) {
		read_num = test_cnt;
		save_flag = 0;
		test_cnt = 1;
	} else if (test_cnt < 1000) {
		read_num = test_cnt;
		/*if output is raw then save one frame*/
		if (mode < 4)
			save_flag = 1;
		else
			save_flag = 0;
		test_cnt = 1;
	} else if (test_cnt < 10000) {
		read_num = test_cnt;
		save_flag = 3;
		test_cnt = 10;
	} else {
		read_num = test_cnt;
		save_flag = 2;
		test_cnt = 1;
	}

	for (i = 0; i < test_cnt; i++) {
		ktime_get_real_ts64(&tv1, NULL);
		if (0 == main_test(sel, mode))
			printf("mode %d test done at the %d time!!\n", mode, i);
		else
			printf("mode %d test failed at the %d time!!\n", mode, i);
		close(fd);
		ktime_get_real_ts64(&tv2, NULL);
		tv = (float)((tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_nsec - tv1.tv_nsec) / NSEC_PER_USEC) / 1000000;
		printf("time cost %f(s)\n", tv);
	}
	return 0;
}
