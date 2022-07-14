/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __MTKCAM_FMT_H
#define __MTKCAM_FMT_H

/* camsys supported format */
enum mtkcam_ipi_fmt {
	MTKCAM_IPI_IMG_FMT_UNKNOWN		= -1,
	MTKCAM_IPI_IMG_FMT_BAYER8		= 0,
	MTKCAM_IPI_IMG_FMT_BAYER10		= 1,
	MTKCAM_IPI_IMG_FMT_BAYER12		= 2,
	MTKCAM_IPI_IMG_FMT_BAYER14		= 3,
	MTKCAM_IPI_IMG_FMT_BAYER16		= 4,
	MTKCAM_IPI_IMG_FMT_BAYER10_UNPACKED	= 5,
	MTKCAM_IPI_IMG_FMT_BAYER12_UNPACKED	= 6,
	MTKCAM_IPI_IMG_FMT_BAYER14_UNPACKED	= 7,
	MTKCAM_IPI_IMG_FMT_RGB565		= 8,
	MTKCAM_IPI_IMG_FMT_RGB888		= 9,
	MTKCAM_IPI_IMG_FMT_JPEG			= 10,
	MTKCAM_IPI_IMG_FMT_FG_BAYER8		= 11,
	MTKCAM_IPI_IMG_FMT_FG_BAYER10		= 12,
	MTKCAM_IPI_IMG_FMT_FG_BAYER12		= 13,
	MTKCAM_IPI_IMG_FMT_FG_BAYER14		= 14,
	MTKCAM_IPI_IMG_FMT_YUYV			= 15,
	MTKCAM_IPI_IMG_FMT_YVYU			= 16,
	MTKCAM_IPI_IMG_FMT_UYVY			= 17,
	MTKCAM_IPI_IMG_FMT_VYUY			= 18,
	MTKCAM_IPI_IMG_FMT_YUV_422_2P		= 19,
	MTKCAM_IPI_IMG_FMT_YVU_422_2P		= 20,
	MTKCAM_IPI_IMG_FMT_YUV_422_3P		= 21,
	MTKCAM_IPI_IMG_FMT_YVU_422_3P		= 22,
	MTKCAM_IPI_IMG_FMT_YUV_420_2P		= 23,
	MTKCAM_IPI_IMG_FMT_YVU_420_2P		= 24,
	MTKCAM_IPI_IMG_FMT_YUV_420_3P		= 25,
	MTKCAM_IPI_IMG_FMT_YVU_420_3P		= 26,
	MTKCAM_IPI_IMG_FMT_Y8			= 27,
	MTKCAM_IPI_IMG_FMT_YUYV_Y210		= 28,
	MTKCAM_IPI_IMG_FMT_YVYU_Y210		= 29,
	MTKCAM_IPI_IMG_FMT_UYVY_Y210		= 30,
	MTKCAM_IPI_IMG_FMT_VYUY_Y210		= 31,
	MTKCAM_IPI_IMG_FMT_YUYV_Y210_PACKED	= 32,
	MTKCAM_IPI_IMG_FMT_YVYU_Y210_PACKED	= 33,
	MTKCAM_IPI_IMG_FMT_UYVY_Y210_PACKED	= 34,
	MTKCAM_IPI_IMG_FMT_VYUY_Y210_PACKED	= 35,
	MTKCAM_IPI_IMG_FMT_YUV_P210		= 36,
	MTKCAM_IPI_IMG_FMT_YVU_P210		= 37,
	MTKCAM_IPI_IMG_FMT_YUV_P010		= 38,
	MTKCAM_IPI_IMG_FMT_YVU_P010		= 39,
	MTKCAM_IPI_IMG_FMT_YUV_P210_PACKED	= 40,
	MTKCAM_IPI_IMG_FMT_YVU_P210_PACKED	= 41,
	MTKCAM_IPI_IMG_FMT_YUV_P010_PACKED	= 42,
	MTKCAM_IPI_IMG_FMT_YVU_P010_PACKED	= 43,
	MTKCAM_IPI_IMG_FMT_YUV_P212		= 44,
	MTKCAM_IPI_IMG_FMT_YVU_P212		= 45,
	MTKCAM_IPI_IMG_FMT_YUV_P012		= 46,
	MTKCAM_IPI_IMG_FMT_YVU_P012		= 47,
	MTKCAM_IPI_IMG_FMT_YUV_P212_PACKED	= 48,
	MTKCAM_IPI_IMG_FMT_YVU_P212_PACKED	= 49,
	MTKCAM_IPI_IMG_FMT_YUV_P012_PACKED	= 50,
	MTKCAM_IPI_IMG_FMT_YVU_P012_PACKED	= 51,
	MTKCAM_IPI_IMG_FMT_RGB_8B_3P		= 52,
	MTKCAM_IPI_IMG_FMT_RGB_10B_3P		= 53,
	MTKCAM_IPI_IMG_FMT_RGB_12B_3P		= 54,
	MTKCAM_IPI_IMG_FMT_RGB_10B_3P_PACKED	= 55,
	MTKCAM_IPI_IMG_FMT_RGB_12B_3P_PACKED	= 56,
	MTKCAM_IPI_IMG_FMT_FG_BAYER8_3P		= 57,
	MTKCAM_IPI_IMG_FMT_FG_BAYER10_3P	= 58,
	MTKCAM_IPI_IMG_FMT_FG_BAYER12_3P	= 59,
	MTKCAM_IPI_IMG_FMT_FG_BAYER10_3P_PACKED	= 60,
	MTKCAM_IPI_IMG_FMT_FG_BAYER12_3P_PACKED	= 61,
	MTKCAM_IPI_IMG_FMT_UFBC_NV12		= 62,
	MTKCAM_IPI_IMG_FMT_UFBC_NV21		= 63,
	MTKCAM_IPI_IMG_FMT_UFBC_YUV_P010	= 64,
	MTKCAM_IPI_IMG_FMT_UFBC_YVU_P010	= 65,
	MTKCAM_IPI_IMG_FMT_UFBC_YUV_P012	= 66,
	MTKCAM_IPI_IMG_FMT_UFBC_YVU_P012	= 67,
	MTKCAM_IPI_IMG_FMT_UFBC_BAYER8		= 68,
	MTKCAM_IPI_IMG_FMT_UFBC_BAYER10		= 69,
	MTKCAM_IPI_IMG_FMT_UFBC_BAYER12		= 70,
	MTKCAM_IPI_IMG_FMT_UFBC_BAYER14		= 71,
	MTKCAM_IPI_IMG_FMT_BAYER10_MIPI		= 72
};

#endif /* __MTKCAM_FMT_H */