// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include "kd_camera_typedef.h"

#define PFX "gc02m1_pdafotp_21143"
#define LOG_INF(format, args...) pr_debug(PFX "[%s] " format, __func__, ##args)

#include "kd_imgsensor.h"
#include "kd_imgsensor_define_v4l2.h"
#include "kd_imgsensor_errcode.h"
#include "gc02m1_eeprom_21143.h"
#include "adaptor-subdrv.h"
#include "adaptor-i2c.h"

#define USHORT             unsigned short
#define BYTE               unsigned char
#define Sleep(ms) mdelay(ms)

#define GC02M1_EEPROM_READ_ID_21143 0xA4
#define GC02M1_EEPROM_WRITE_ID_21143 0xA4
#define GC02M1_MAX_OFFSET_21143 0x1FFF

static bool gc02m1_selective_read_eeprom_21143(struct subdrv_ctx *ctx,
        kal_uint16 addr, BYTE *data)
{
    if (addr > GC02M1_MAX_OFFSET_21143)
        return false;
    if (adaptor_i2c_rd_u8(ctx->i2c_client,
        GC02M1_EEPROM_READ_ID_21143 >> 1, addr, data) < 0)
        return false;
    return true;
}

static bool gc02m1_read_eeprom_21143(struct subdrv_ctx *ctx, kal_uint16 addr, BYTE *data, int size)
{
    int i = 0;
    int offset = addr;

    LOG_INF("enter _read_eeprom size = %d\n", size);
    for (i = 0; i < size; i++) {
        if (!gc02m1_selective_read_eeprom_21143(ctx, offset, &data[i]))
            return false;
        /* LOG_INF("read_eeprom 0x%0x %d\n",offset, data[i]); */
        offset++;
    }
    return true;
}

struct eeprom_map_info gc02m1_eeprom_info_21143[] = {
    { EEPROM_META_MODULE_ID, 0x0000, 0x000C, 0x000D, 2, true },
    { EEPROM_META_SENSOR_ID, 0x0006, 0x000C, 0x000D, 2, true },
    { EEPROM_META_LENS_ID, 0x0008, 0x000C, 0x000D, 2, true },
    { EEPROM_META_VCM_ID, 0x000A, 0x000C, 0x000D, 2, true },
    { EEPROM_META_MIRROR_FLIP, 0x000E, 0x000F, 0x0010, 1, false },
    { EEPROM_META_MODULE_SN, 0x00E0, 0x000F1, 0x00F2, 17, true },
};

unsigned int read_gc02m1_eeprom_info_21143(struct subdrv_ctx *ctx, kal_uint16 meta_id,
                     BYTE *data, int size)
{
    kal_uint16 addr;
    int readsize;

    if (meta_id != gc02m1_eeprom_info_21143[meta_id].meta)
        return -1;

    if (size != gc02m1_eeprom_info_21143[meta_id].size)
        return -1;

    addr = gc02m1_eeprom_info_21143[meta_id].start;
    readsize = gc02m1_eeprom_info_21143[meta_id].size;

    gc02m1_read_eeprom_21143(ctx, addr, data, readsize);

    return 0;
}

