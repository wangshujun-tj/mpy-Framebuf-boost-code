/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "py/runtime.h"
#include "py/binary.h"
#include "py/stream.h"
#include "py/reader.h"
#include "extmod/vfs.h"

#include "lib/oofatfs/ff.h"

#if MICROPY_PY_FRAMEBUF

#include "extmod/font_asc.h"


typedef struct _font_set_t{
  uint8_t f_style;
  uint8_t rotate;		//xuanzhuan
  uint8_t scale;		//fangda 16jinzhi ,gaowei hengxiang  diweizongxiang
  uint8_t inverse;      //fanbai
  uint16_t bg_col;
  uint8_t transparent;
}font_set_t;

typedef struct _font_inf_t{
  uint32_t Font_Type;       //1  GB2312,2  GB18030,3  small font ,0  None font
  uint32_t Base_Addr12;		//xuanzhuan 0,12dot font no exist
  uint32_t Base_Addr16;  	//xuanzhuan 0,16dot font no exist
  uint32_t Base_Addr24;		//xuanzhuan 0,24dot font no exist
  uint32_t Base_Addr32;		//xuanzhuan 0,32dot font no exist
}font_inf_t;

typedef struct _mp_obj_framebuf_t {
    mp_obj_base_t base;
    mp_obj_t buf_obj; // need to store this to prevent GC from reclaiming buf
    mp_obj_t font_file;
    font_set_t font_set;
    font_inf_t font_inf;
    void *buf;
    uint16_t width, height, stride;
    uint8_t format;
} mp_obj_framebuf_t;

#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_obj_type_t mp_type_framebuf;
#endif

typedef void (*setpixel_t)(const mp_obj_framebuf_t *, int, int, uint32_t);
typedef uint32_t (*getpixel_t)(const mp_obj_framebuf_t *, int, int);

typedef struct _mp_framebuf_p_t {
    setpixel_t setpixel;
    getpixel_t getpixel;
} mp_framebuf_p_t;

// constants for formats
//
#define FRAMEBUF_MON_VLSB   (0X00)
#define FRAMEBUF_MON_VMSB   (0X01)
#define FRAMEBUF_MON_HLSB   (0X02)
#define FRAMEBUF_MON_HMSB   (0X03)

#define FRAMEBUF_GS2_VLSB   (0X20)
#define FRAMEBUF_GS2_VMSB   (0X21)
#define FRAMEBUF_GS2_HLSB   (0X22)
#define FRAMEBUF_GS2_HMSB   (0X23)

#define FRAMEBUF_GS4_VLSB   (0X40)
#define FRAMEBUF_GS4_VMSB   (0X41)
#define FRAMEBUF_GS4_HLSB   (0X42)
#define FRAMEBUF_GS4_HMSB   (0X43)

#define FRAMEBUF_GS8_V      (0X60)
#define FRAMEBUF_GS8_H      (0X62)

#define FRAMEBUF_RGB565     (0X80)
#define FRAMEBUF_RGB565SW   (0X81)
#define FRAMEBUF_RGB888     (0X82)
#define FRAMEBUF_RGB8888    (0X83)

#define FRAMEBUF_ST7302     (0XA0)

#define FRAMEBUF_MX         (0X04)
#define FRAMEBUF_MY         (0X08)
#define FRAMEBUF_MV         (0X10)


// constants for formats
#define Font_S12    (0x11)
#define Font_C12    (0x21)
#define Font_A12    (0x31)
#define Font_R12    (0x41)

#define Font_S16    (0x12)
#define Font_C16    (0x22)
#define Font_A16    (0x32)
#define Font_R16    (0x42)

#define Font_S24    (0x13)
#define Font_C24    (0x23)
#define Font_A24    (0x33)
#define Font_R24    (0x43)

#define Font_S32    (0x14)
#define Font_C32    (0x24)
#define Font_A32    (0x34)
#define Font_R32    (0x44)



mp_uint_t f_seek(mp_obj_t filename, mp_uint_t offset,mp_uint_t whence) {
    struct mp_stream_seek_t seek_s;

    seek_s.offset = offset;
    seek_s.whence = whence;

    // In POSIX, it's error to seek before end of stream, we enforce it here.
    if (seek_s.whence == SEEK_SET && seek_s.offset < 0) {
        mp_raise_OSError(MP_EINVAL);
    }

    const mp_stream_p_t *stream_p = mp_get_stream(filename);
    int error;
    mp_uint_t res = stream_p->ioctl(filename, MP_STREAM_SEEK, (mp_uint_t)(uintptr_t)&seek_s, &error);
    if (res == MP_STREAM_ERROR) {
        mp_raise_OSError(error);
    }

    return seek_s.offset;
}

STATIC mp_obj_t framebuf_font_load(mp_obj_t self_in, mp_obj_t name_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    const char *filename = mp_obj_str_get_str(name_in);
    //mp_printf(&mp_plat_print,"%s\n\r",filename);
    mp_obj_t f_args[2] = {
        mp_obj_new_str(filename, strlen(filename)),
        MP_OBJ_NEW_QSTR(MP_QSTR_rb),
    };
    self->font_file = mp_vfs_open(MP_ARRAY_SIZE(f_args), &f_args[0], (mp_map_t *)&mp_const_empty_map);
    //尝试添加判断
    f_seek(self->font_file, 32,SEEK_SET);
    int errcode;
    int len=mp_stream_rw(self->font_file ,&self->font_inf, 20, &errcode, MP_STREAM_RW_READ);
    if (errcode != 0 && len!=20) {
        mp_raise_OSError(errcode);
        memset(&self->font_inf,0,20);
        self->font_file=NULL;
        mp_printf(&mp_plat_print,"Read %s error!\n\r",filename);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(framebuf_font_load_obj, framebuf_font_load);

STATIC mp_obj_t framebuf_font_free(mp_obj_t self_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->font_file != NULL){
        mp_stream_close(self->font_file);
        mp_printf(&mp_plat_print,"font file close \n\r");
    	self->font_file=NULL; 
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(framebuf_font_free_obj, framebuf_font_free);

// Functions for MHLSB and MHMSB

STATIC mp_obj_t framebuf_font_set(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t prompt=0;
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t style = mp_obj_get_int(args[1]);
    mp_int_t rotate =0;
    if (n_args > 2) {
        rotate = mp_obj_get_int(args[2]);
    }
    mp_int_t scale=1;
    if (n_args > 3) {
        scale = mp_obj_get_int(args[3]);
    }
    mp_int_t inverse =0;
    if (n_args > 4) {
        inverse = mp_obj_get_int(args[4]);
    }
    mp_int_t bg_col =0;
    if (n_args > 5) {
        bg_col = mp_obj_get_int(args[5]);
        self->font_set.transparent=0;
    }else{
        self->font_set.transparent=1;
    }    
    if ((style&0x0f)>4) {
        style=0x11;
        prompt=1;
    }
    self->font_set.f_style=style;
    
    if (scale==0){
        scale=1;
        prompt=1;
    }
    if (scale>4){ 
        scale=4;
        prompt=1;
    }
    self->font_set.scale=scale;   
       
    if (rotate>3){
        rotate=3;
        prompt=1;
    }
    self->font_set.rotate=rotate;     
    if (inverse>1){ 
        inverse=1;
        prompt=1;
    }
    self->font_set.inverse=inverse; 
    self->font_set.bg_col=bg_col; 
    if  (prompt==1){
        mp_printf(&mp_plat_print,"style=%d,rotate=%d,scale=%d,inverse=%d,bg_col=%d\n\r",self->font_set.f_style,self->font_set.rotate,self->font_set.scale,self->font_set.inverse,self->font_set.bg_col);    
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_font_set_obj, 2, 6, framebuf_font_set);
// Functions for st7302 format

STATIC void st7302_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    //bit0设置位顺序，0低位在前，bit1设置排列方式，0为垂直方式方式
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        index  = ((y >> 1) * fb->stride + x) >> 2;
        offset = 7 - (((x<<1) & 0x06) + (y & 0x01));
    }else{
        index  = ((x >> 1) * fb->stride + y) >> 2;
        offset = 7 - (((y<<1) & 0x06) + (x & 0x01));
    }
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x01 << offset)) | ((col != 0) << offset);
}

STATIC uint32_t st7302_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        index  = ((y >> 1) * fb->stride + x) >> 2;
        offset = 7 - (((x<<1) & 0x06) + (y & 0x01));
    }else{
        index  = ((x >> 1) * fb->stride + y) >> 2;
        offset = 7 - (((y<<1) & 0x06) + (x & 0x01));
    }
    return (((uint8_t *)fb->buf)[index] >> (offset)) & 0x01;
}

// Functions for mon format

STATIC void mon_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    //bit0设置位顺序，0低位在前，bit1设置排列方式，0为垂直方式方式
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0x00){
            index  =  x + (y >> 3) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? y & 0x07 : 7 - (y & 0x07);
        }else{
            index  = (x + y * fb->stride) >> 3;
            offset = (fb->format & 0x01) == 0 ? x & 0x07 : 7 - (x & 0x07);
        }
    }else{
        if ((fb->format & 0x02) == 0x00){
            index  = (y + x * fb->stride) >> 3;
            offset = (fb->format & 0x01) == 0 ? y & 0x07 : 7 - (y & 0x07);
        }else{
            index  =  y + (x >> 3) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? x & 0x07 : 7 - (x & 0x07);
        }
    }
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x01 << offset)) | ((col != 0) << offset);
}

STATIC uint32_t mon_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0x00){
            index  = x + (y >> 3) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? y & 0x07 : 7 - (y & 0x07);
        }else{
            index  = (x + y * fb->stride) >> 3;
            offset = (fb->format & 0x01) == 0 ? x & 0x07 : 7 - (x & 0x07);
        }
    }else{
        if ((fb->format & 0x02) == 0x00){
            index  = (y + x * fb->stride) >> 3;
            offset = (fb->format & 0x01) == 0 ? y & 0x07 : 7 - (y & 0x07);
        }else{
            index  =  y + (x >> 3) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? x & 0x07 : 7 - (x & 0x07);
        }
    }
    return (((uint8_t *)fb->buf)[index] >> (offset)) & 0x01;
}

// Functions for GS2 format

STATIC void gs2_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    //bit0设置位顺序，0低位在前，bit1设置排列方式，0为垂直方式
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0x00){
            index  = x + (y >> 2) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? y & 0x03 : 3 - (y & 0x03);
        }else{
            index  = (x + y * fb->stride) >> 2;
            offset = (fb->format & 0x01) == 0 ? x & 0x03 : 3 - (x & 0x03);
        }
    }else{
        if ((fb->format & 0x02) == 0x00){
            index  = (y + x * fb->stride) >> 2;
            offset = (fb->format & 0x01) == 0 ? y & 0x03 : 3 - (y & 0x03);
        }else{
            index  =  y + (x >> 2) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? x & 0x03 : 3 - (x & 0x03);
        }
    }
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x03 << (offset*2))) | ((col &0x03) <<  (offset*2));
}

STATIC uint32_t gs2_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0x00){
            index  = x + (y >> 2) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? y & 0x03 : 3 - (y & 0x03);
        }else{
            index  = (x + y * fb->stride) >> 2;
            offset = (fb->format & 0x01) == 0 ? x & 0x03 : 3 - (x & 0x03);
        }
    }else{
        if ((fb->format & 0x02) == 0x00){
            index  = (y + x * fb->stride) >> 2;
            offset = (fb->format & 0x01) == 0 ? y & 0x03 : 3 - (y & 0x03);
        }else{
            index  =  y + (x >> 2) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? x & 0x03 : 3 - (x & 0x03);
        }
    }
    return (((uint8_t *)fb->buf)[index] >> (offset*2)) & 0x03;
}

// Functions for GS4 format

STATIC void gs4_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    //bit0设置位顺序，0低位在前，bit1设置排列方式，0为垂直方式
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0x00){
            index  = x + (y >> 1) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? y & 0x01 : 1 - (y & 0x01);
        }else{
            index  = (x + y * fb->stride) >> 1;
            offset = (fb->format & 0x01) == 0 ? x & 0x01 : 1 - (x & 0x01);
        }
    }else{
        if ((fb->format & 0x02) == 0x00){
            index  = (y + x * fb->stride) >> 1;
            offset = (fb->format & 0x01) == 0 ? y & 0x01 : 1 - (y & 0x01);
        }else{
            index  =  y + (x >> 1) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? x & 0x01 : 1 - (x & 0x01);
        }
    }
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x0f << (offset*4))) | ((col &0x0f) <<  (offset*4));
}

STATIC uint32_t gs4_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    int index;
    int offset;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0x00){
            index  = x + (y >> 1) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? y & 0x01 : 1 - (y & 0x01);
        }else{
            index  = (x + y * fb->stride) >> 1;
            offset = (fb->format & 0x01) == 0 ? x & 0x01 : 1 - (x & 0x01);
        }
    }else{
        if ((fb->format & 0x02) == 0x00){
            index  = (y + x * fb->stride) >> 1;
            offset = (fb->format & 0x01) == 0 ? y & 0x01 : 1 - (y & 0x01);
        }else{
            index  =  y + (x >> 1) * fb->stride;
            offset = (fb->format & 0x01) == 0 ? x & 0x01 : 1 - (x & 0x01);
        }
    }
    return (((uint8_t *)fb->buf)[index] >> (offset*4)) & 0x0f;
}

// Functions for GS8 format

STATIC void gs8_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    int index;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0){
            index  = y * fb->stride + x;
        }else{
            index  = x * fb->stride + y;
        }
    }else{
        if ((fb->format & 0x02) == 0){
            index  = y * fb->stride + x;
        }else{
            index  = x * fb->stride + y;
        }
    }
    ((uint8_t *)fb->buf)[index] = col & 0xff;
}

STATIC uint32_t gs8_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    int index;
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        if ((fb->format & 0x02) == 0){
            index  = y * fb->stride + x;
        }else{
            index  = x * fb->stride + y;
        }
    }else{
        if ((fb->format & 0x02) == 0){
            index  = y * fb->stride + x;
        }else{
            index  = x * fb->stride + y;
        }
    }
    return ((uint8_t *)fb->buf)[index];
}

// Functions for RGB format

STATIC void rgb_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        switch (fb->format&0x1F){
            case 0:
            ((uint16_t *)fb->buf)[x + y * fb->stride] = col;
            break;
            case 1:
            ((uint8_t *)fb->buf)[(x + y * fb->stride)*2]   = (col>>8)&0xff;
            ((uint8_t *)fb->buf)[(x + y * fb->stride)*2+1] = col&0xff;
            break;
            case 2:
            ((uint8_t *)fb->buf)[(x + y * fb->stride)*3]   = (col>>16)&0xff;
            ((uint8_t *)fb->buf)[(x + y * fb->stride)*3+1] = (col>> 8)&0xff;
            ((uint8_t *)fb->buf)[(x + y * fb->stride)*3+2] = col&0xff;
            break;
            case 3:
            ((uint32_t *)fb->buf)[x + y * fb->stride] = col;
            break;
        }
    }else{
        switch (fb->format&0x1F){
            case 0:
            ((uint16_t *)fb->buf)[y + x * fb->stride] = col;
            break;
            case 1:
            ((uint8_t *)fb->buf)[(y + x * fb->stride)*2]   = (col>>8)&0xff;
            ((uint8_t *)fb->buf)[(y + x * fb->stride)*2+1] = col&0xff;
            break;
            case 2:
            ((uint8_t *)fb->buf)[(y + x * fb->stride)*3]   = (col>>16)&0xff;
            ((uint8_t *)fb->buf)[(y + x * fb->stride)*3+1] = (col>> 8)&0xff;
            ((uint8_t *)fb->buf)[(y + x * fb->stride)*3+2] = col&0xff;
            break;
            case 3:
            ((uint32_t *)fb->buf)[y + x * fb->stride] = col;
            break;
        }

    }
}

STATIC uint32_t rgb_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    if ((fb->format & FRAMEBUF_MV) == 0x00){
        switch (fb->format&0x1F){
            case 0:
            return ((uint16_t *)fb->buf)[x + y * fb->stride];
            break;
            case 1:
            return ((((uint8_t *)fb->buf)[(x + y * fb->stride)*2]<<8)|((uint8_t *)fb->buf)[(x + y * fb->stride)*2+1] );
            break;
            case 2:
            return ((((uint8_t *)fb->buf)[(x + y * fb->stride)*3]<<16)|(((uint8_t *)fb->buf)[(x + y * fb->stride)*3+1]<<8)
            ||((uint8_t *)fb->buf)[(x + y * fb->stride)*3+2]);
            break;
            case 3:
            return ((uint32_t *)fb->buf)[x + y * fb->stride];
            break;
        }
    }else{
        switch (fb->format&0x1F){
            case 0:
            return ((uint16_t *)fb->buf)[y + x * fb->stride];
            break;
            case 1:
            return ((((uint8_t *)fb->buf)[(y + x * fb->stride)*2]<<8)|((uint8_t *)fb->buf)[(y + x * fb->stride)*2+1] );
            break;
            case 2:
            return ((((uint8_t *)fb->buf)[(y + x * fb->stride)*3]<<16)|(((uint8_t *)fb->buf)[(y + x * fb->stride)*3+1]<<8)
            ||((uint8_t *)fb->buf)[(y + x * fb->stride)*3+2]);
            break;
            case 3:
            return ((uint32_t *)fb->buf)[y + x * fb->stride];
            break;
        }
    }
    return 0;
}


STATIC mp_framebuf_p_t formats[] = {
    [FRAMEBUF_MON_VLSB&0xE0] 	= {mon_setpixel, mon_getpixel},
    [FRAMEBUF_GS2_HMSB&0xE0] 	= {gs2_setpixel, gs2_getpixel},
    [FRAMEBUF_GS4_HMSB&0xE0] 	= {gs4_setpixel, gs4_getpixel},
    [FRAMEBUF_GS8_H&0xE0] 		= {gs8_setpixel, gs8_getpixel},
    [FRAMEBUF_RGB565&0xE0] 	    = {rgb_setpixel, rgb_getpixel},
    [FRAMEBUF_ST7302&0xE0] 	    = {st7302_setpixel, st7302_getpixel},
    
};

static inline void setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    if (0 <= x && x < fb->width && 0 <= y && y < fb->height){
        if ((fb->format&FRAMEBUF_MX)==FRAMEBUF_MX)
            x=fb->width-x-1;
        if ((fb->format&FRAMEBUF_MY)==FRAMEBUF_MY)
            y=fb->height-y-1;
        if (x>=0 && x<fb->width && y>=0 && y<fb->height)
            formats[fb->format&0xE0].setpixel(fb, x, y, col);
    }
}

STATIC void setpixel_checked(const mp_obj_framebuf_t *fb, mp_int_t x, mp_int_t y, mp_int_t col, mp_int_t mask) {
    if (mask && 0 <= x && x < fb->width && 0 <= y && y < fb->height) {
        setpixel(fb, x, y, col);
    }
}

static inline uint32_t getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    if (0 <= x && x < fb->width && 0 <= y && y < fb->height){
        if ((fb->format&FRAMEBUF_MX)==FRAMEBUF_MX)
            x=fb->width-x-1;
        if ((fb->format&FRAMEBUF_MY)==FRAMEBUF_MY)
            y=fb->height-y-1;
        return formats[fb->format&0xE0].getpixel(fb, x, y);
    }else{
        return 0;
    }
    
}

STATIC void fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    if (h < 1 || w < 1 || x + w <= 0 || y + h <= 0 || y >= fb->height || x >= fb->width) {
        // No operation needed.
        return;
    }
    for (int xx = x; xx < x + w; xx++) {
        for (int yy = y; yy < y + h; yy++) {
            setpixel(fb, xx, yy, col);
        }
    }
}

STATIC mp_obj_t framebuf_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 4, 5, false);

    mp_obj_framebuf_t *o = m_new_obj(mp_obj_framebuf_t);
    o->base.type = type;
    o->buf_obj = args[0];

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_WRITE);
    o->buf = bufinfo.buf;

    o->width = mp_obj_get_int(args[1]);
    o->height = mp_obj_get_int(args[2]);
    o->format = mp_obj_get_int(args[3]);
    if (n_args > 4) {
        o->stride = mp_obj_get_int(args[4]);
    } else {
        if ((o->format & FRAMEBUF_MV) == 0x00){
            o->stride = o->width;
        }else{
            o->stride = o->height;
        }
    }
    //font style seting
    o->font_set.f_style=0x11;
    o->font_set.scale=1;   
    o->font_set.rotate=0;     
    o->font_set.inverse=0;  
    o->font_set.transparent=1;
    o->font_set.bg_col=0;
    o->font_file=NULL;
    o->font_inf.Font_Type=0;
    o->font_inf.Base_Addr12=0;		//xuanzhuan 0,12dot font no exist
    o->font_inf.Base_Addr16=0;  	//xuanzhuan 0,16dot font no exist
    o->font_inf.Base_Addr24=0;		//xuanzhuan 0,24dot font no exist
    o->font_inf.Base_Addr32=0;		//xuanzhuan 0,32dot font no exist
    //确认垂直方式也需要处理stride，另外定义buffer时也需要处理
    switch (o->format&0xE0) {
        case FRAMEBUF_ST7302:
            o->stride = (o->stride + 11) / 12 * 12;
            break;
        case FRAMEBUF_GS8_H:
        case FRAMEBUF_GS8_V:
        case FRAMEBUF_RGB565:
        case FRAMEBUF_RGB565SW:
        case FRAMEBUF_RGB888:
        case FRAMEBUF_RGB8888:
            break;
        case FRAMEBUF_MON_VLSB:
        case FRAMEBUF_MON_VMSB:
        case FRAMEBUF_MON_HLSB:
        case FRAMEBUF_MON_HMSB:
            o->stride = (o->stride + 7) & ~7;
            break;
        case FRAMEBUF_GS2_VLSB:
        case FRAMEBUF_GS2_VMSB:
        case FRAMEBUF_GS2_HLSB:
        case FRAMEBUF_GS2_HMSB:
            o->stride = (o->stride + 3) & ~3;
            break;
        case FRAMEBUF_GS4_VLSB:
        case FRAMEBUF_GS4_VMSB:
        case FRAMEBUF_GS4_HLSB:
        case FRAMEBUF_GS4_HMSB:
            o->stride = (o->stride + 1) & ~1;
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid format"));
    }
    mp_printf(&mp_plat_print,"w=%d,h=%d,f=%d,s=%d\n\r",o->width,o->height,o->format,o->stride);
    return MP_OBJ_FROM_PTR(o);
}

STATIC void framebuf_args(const mp_obj_t *args_in, mp_int_t *args_out, int n) {
    for (int i = 0; i < n; ++i) {
        args_out[i] = mp_obj_get_int(args_in[i + 1]);
    }
}

STATIC mp_int_t framebuf_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    (void)flags;
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    bufinfo->buf = self->buf;
    u_int8_t size=1;
    if ((self->format == FRAMEBUF_RGB565)||(self->format == FRAMEBUF_RGB565)){
        size=2;
    }else if(self->format == FRAMEBUF_RGB888){
        size=3;
    }else if(self->format == FRAMEBUF_RGB8888){
        size=4;
    }
    bufinfo->len = self->stride * self->height * size;
    bufinfo->typecode = 'B'; // view framebuf as bytes
    return 0;
}

STATIC mp_obj_t framebuf_fill(mp_obj_t self_in, mp_obj_t col_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t col = mp_obj_get_int(col_in);
    fill_rect(self, 0, 0, self->width, self->height, col);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(framebuf_fill_obj, framebuf_fill);

STATIC mp_obj_t framebuf_fill_rect(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5]; // x, y, w, h, col
    framebuf_args(args_in, args, 5);
    fill_rect(self, args[0], args[1], args[2], args[3], args[4]);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_fill_rect_obj, 6, 6, framebuf_fill_rect);

STATIC mp_obj_t framebuf_pixel(size_t n_args, const mp_obj_t *args) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    if (0 <= x && x < self->width && 0 <= y && y < self->height) {
        if (n_args == 3) {
            // get
            return MP_OBJ_NEW_SMALL_INT(getpixel(self, x, y));
        } else {
            // set
            setpixel(self, x, y, mp_obj_get_int(args[3]));
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_pixel_obj, 3, 4, framebuf_pixel);

STATIC mp_obj_t framebuf_hline(size_t n_args, const mp_obj_t *args_in) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[4]; // x, y, w, col
    framebuf_args(args_in, args, 4);

    fill_rect(self, args[0], args[1], args[2], 1, args[3]);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_hline_obj, 5, 5, framebuf_hline);

STATIC mp_obj_t framebuf_vline(size_t n_args, const mp_obj_t *args_in) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[4]; // x, y, h, col
    framebuf_args(args_in, args, 4);

    fill_rect(self, args[0], args[1], 1, args[2], args[3]);

    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_vline_obj, 5, 5, framebuf_vline);

STATIC mp_obj_t framebuf_rect(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5]; // x, y, w, h, col
    framebuf_args(args_in, args, 5);
    if (n_args > 6 && mp_obj_is_true(args_in[6])) {
        fill_rect(self, args[0], args[1], args[2], args[3], args[4]);
    } else {
        fill_rect(self, args[0], args[1], args[2], 1, args[4]);
        fill_rect(self, args[0], args[1] + args[3] - 1, args[2], 1, args[4]);
        fill_rect(self, args[0], args[1], 1, args[3], args[4]);
        fill_rect(self, args[0] + args[2] - 1, args[1], 1, args[3], args[4]);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_rect_obj, 6, 6, framebuf_rect);

STATIC void line(const mp_obj_framebuf_t *fb, mp_int_t x1, mp_int_t y1, mp_int_t x2, mp_int_t y2, mp_int_t col) {
    mp_int_t dx = x2 - x1;
    mp_int_t sx;
    if (dx > 0) {
        sx = 1;
    } else {
        dx = -dx;
        sx = -1;
    }

    mp_int_t dy = y2 - y1;
    mp_int_t sy;
    if (dy > 0) {
        sy = 1;
    } else {
        dy = -dy;
        sy = -1;
    }

    bool steep;
    if (dy > dx) {
        mp_int_t temp;
        temp = x1;
        x1 = y1;
        y1 = temp;
        temp = dx;
        dx = dy;
        dy = temp;
        temp = sx;
        sx = sy;
        sy = temp;
        steep = true;
    } else {
        steep = false;
    }

    mp_int_t e = 2 * dy - dx;
    for (mp_int_t i = 0; i < dx; ++i) {
        if (steep) {
            if (0 <= y1 && y1 < fb->width && 0 <= x1 && x1 < fb->height) {
                setpixel(fb, y1, x1, col);
            }
        } else {
            if (0 <= x1 && x1 < fb->width && 0 <= y1 && y1 < fb->height) {
                setpixel(fb, x1, y1, col);
            }
        }
        while (e >= 0) {
            y1 += sy;
            e -= 2 * dx;
        }
        x1 += sx;
        e += 2 * dy;
    }

    if (0 <= x2 && x2 < fb->width && 0 <= y2 && y2 < fb->height) {
        setpixel(fb, x2, y2, col);
    }
}

STATIC mp_obj_t framebuf_line(size_t n_args, const mp_obj_t *args_in) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5]; // x1, y1, x2, y2, col
    framebuf_args(args_in, args, 5);

    line(self, args[0], args[1], args[2], args[3], args[4]);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_line_obj, 6, 6, framebuf_line);

// Q2 Q1
// Q3 Q4
#define ELLIPSE_MASK_FILL (0x10)
#define ELLIPSE_MASK_ALL (0x0f)
#define ELLIPSE_MASK_Q1 (0x01)
#define ELLIPSE_MASK_Q2 (0x02)
#define ELLIPSE_MASK_Q3 (0x04)
#define ELLIPSE_MASK_Q4 (0x08)

STATIC void draw_ellipse_points(const mp_obj_framebuf_t *fb, mp_int_t cx, mp_int_t cy, mp_int_t x, mp_int_t y, mp_int_t col, mp_int_t mask) {
    if (mask & ELLIPSE_MASK_FILL) {
        if (mask & ELLIPSE_MASK_Q1) {
            fill_rect(fb, cx, cy - y, x + 1, 1, col);
        }
        if (mask & ELLIPSE_MASK_Q2) {
            fill_rect(fb, cx - x, cy - y, x + 1, 1, col);
        }
        if (mask & ELLIPSE_MASK_Q3) {
            fill_rect(fb, cx - x, cy + y, x + 1, 1, col);
        }
        if (mask & ELLIPSE_MASK_Q4) {
            fill_rect(fb, cx, cy + y, x + 1, 1, col);
        }
    } else {
        setpixel_checked(fb, cx + x, cy - y, col, mask & ELLIPSE_MASK_Q1);
        setpixel_checked(fb, cx - x, cy - y, col, mask & ELLIPSE_MASK_Q2);
        setpixel_checked(fb, cx - x, cy + y, col, mask & ELLIPSE_MASK_Q3);
        setpixel_checked(fb, cx + x, cy + y, col, mask & ELLIPSE_MASK_Q4);
    }
}

STATIC mp_obj_t framebuf_ellipse(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t args[5];
    framebuf_args(args_in, args, 5); // cx, cy, xradius, yradius, col
    mp_int_t mask = (n_args > 6 && mp_obj_is_true(args_in[6])) ? ELLIPSE_MASK_FILL : 0;
    if (n_args > 7) {
        mask |= mp_obj_get_int(args_in[7]) & ELLIPSE_MASK_ALL;
    } else {
        mask |= ELLIPSE_MASK_ALL;
    }
    mp_int_t two_asquare = 2 * args[2] * args[2];
    mp_int_t two_bsquare = 2 * args[3] * args[3];
    mp_int_t x = args[2];
    mp_int_t y = 0;
    mp_int_t xchange = args[3] * args[3] * (1 - 2 * args[2]);
    mp_int_t ychange = args[2] * args[2];
    mp_int_t ellipse_error = 0;
    mp_int_t stoppingx = two_bsquare * args[2];
    mp_int_t stoppingy = 0;
    while (stoppingx >= stoppingy) {   // 1st set of points,  y' > -1
        draw_ellipse_points(self, args[0], args[1], x, y, args[4], mask);
        y += 1;
        stoppingy += two_asquare;
        ellipse_error += ychange;
        ychange += two_asquare;
        if ((2 * ellipse_error + xchange) > 0) {
            x -= 1;
            stoppingx -= two_bsquare;
            ellipse_error += xchange;
            xchange += two_bsquare;
        }
    }
    // 1st point set is done start the 2nd set of points
    x = 0;
    y = args[3];
    xchange = args[3] * args[3];
    ychange = args[2] * args[2] * (1 - 2 * args[3]);
    ellipse_error = 0;
    stoppingx = 0;
    stoppingy = two_asquare * args[3];
    while (stoppingx <= stoppingy) {  // 2nd set of points, y' < -1
        draw_ellipse_points(self, args[0], args[1], x, y, args[4], mask);
        x += 1;
        stoppingx += two_bsquare;
        ellipse_error += xchange;
        xchange += two_bsquare;
        if ((2 * ellipse_error + ychange) > 0) {
            y -= 1;
            stoppingy -= two_asquare;
            ellipse_error += ychange;
            ychange += two_asquare;
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_ellipse_obj, 6, 8, framebuf_ellipse);

#if MICROPY_PY_ARRAY && !MICROPY_ENABLE_DYNRUNTIME
// TODO: poly needs mp_binary_get_size & mp_binary_get_val_array which aren't
// available in dynruntime.h yet.

STATIC mp_int_t poly_int(mp_buffer_info_t *bufinfo, size_t index) {
    return mp_obj_get_int(mp_binary_get_val_array(bufinfo->typecode, bufinfo->buf, index));
}

STATIC mp_obj_t framebuf_poly(size_t n_args, const mp_obj_t *args_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args_in[0]);

    mp_int_t x = mp_obj_get_int(args_in[1]);
    mp_int_t y = mp_obj_get_int(args_in[2]);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args_in[3], &bufinfo, MP_BUFFER_READ);
    // If an odd number of values was given, this rounds down to multiple of two.
    int n_poly = bufinfo.len / (mp_binary_get_size('@', bufinfo.typecode, NULL) * 2);

    if (n_poly == 0) {
        return mp_const_none;
    }

    mp_int_t col = mp_obj_get_int(args_in[4]);
    bool fill = n_args > 5 && mp_obj_is_true(args_in[5]);

    if (fill) {
        // This implements an integer version of http://alienryderflex.com/polygon_fill/

        // The idea is for each scan line, compute the sorted list of x
        // coordinates where the scan line intersects the polygon edges,
        // then fill between each resulting pair.

        // Restrict just to the scan lines that include the vertical extent of
        // this polygon.
        mp_int_t y_min = INT_MAX, y_max = INT_MIN;
        for (int i = 0; i < n_poly; i++) {
            mp_int_t py = poly_int(&bufinfo, i * 2 + 1);
            y_min = MIN(y_min, py);
            y_max = MAX(y_max, py);
        }

        for (mp_int_t row = y_min; row <= y_max; row++) {
            // Each node is the x coordinate where an edge crosses this scan line.
            mp_int_t nodes[n_poly];
            int n_nodes = 0;
            mp_int_t px1 = poly_int(&bufinfo, 0);
            mp_int_t py1 = poly_int(&bufinfo, 1);
            int i = n_poly * 2 - 1;
            do {
                mp_int_t py2 = poly_int(&bufinfo, i--);
                mp_int_t px2 = poly_int(&bufinfo, i--);

                // Don't include the bottom pixel of a given edge to avoid
                // duplicating the node with the start of the next edge. This
                // will miss some pixels on the boundary, and in particular
                // at a local minima or inflection point.
                if (py1 != py2 && ((py1 > row && py2 <= row) || (py1 <= row && py2 > row))) {
                    mp_int_t node = (32 * px1 + 32 * (px2 - px1) * (row - py1) / (py2 - py1) + 16) / 32;
                    nodes[n_nodes++] = node;
                } else if (row == MAX(py1, py2)) {
                    // At local-minima, try and manually fill in the pixels that get missed above.
                    if (py1 < py2) {
                        setpixel_checked(self, x + px2, y + py2, col, 1);
                    } else if (py2 < py1) {
                        setpixel_checked(self, x + px1, y + py1, col, 1);
                    } else {
                        // Even though this is a hline and would be faster to
                        // use fill_rect, use line() because it handles x2 <
                        // x1.
                        line(self, x + px1, y + py1, x + px2, y + py2, col);
                    }
                }

                px1 = px2;
                py1 = py2;
            } while (i >= 0);

            if (!n_nodes) {
                continue;
            }

            // Sort the nodes left-to-right (bubble-sort for code size).
            i = 0;
            while (i < n_nodes - 1) {
                if (nodes[i] > nodes[i + 1]) {
                    mp_int_t swap = nodes[i];
                    nodes[i] = nodes[i + 1];
                    nodes[i + 1] = swap;
                    if (i) {
                        i--;
                    }
                } else {
                    i++;
                }
            }

            // Fill between each pair of nodes.
            for (i = 0; i < n_nodes; i += 2) {
                fill_rect(self, x + nodes[i], y + row, (nodes[i + 1] - nodes[i]) + 1, 1, col);
            }
        }
    } else {
        // Outline only.
        mp_int_t px1 = poly_int(&bufinfo, 0);
        mp_int_t py1 = poly_int(&bufinfo, 1);
        int i = n_poly * 2 - 1;
        do {
            mp_int_t py2 = poly_int(&bufinfo, i--);
            mp_int_t px2 = poly_int(&bufinfo, i--);
            line(self, x + px1, y + py1, x + px2, y + py2, col);
            px1 = px2;
            py1 = py2;
        } while (i >= 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_poly_obj, 5, 6, framebuf_poly);
#endif // MICROPY_PY_ARRAY && !MICROPY_ENABLE_DYNRUNTIME

STATIC mp_obj_t framebuf_blit(size_t n_args, const mp_obj_t *args) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_t source_in = mp_obj_cast_to_native_base(args[1], MP_OBJ_FROM_PTR(&mp_type_framebuf));
    if (source_in == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_framebuf_t *source = MP_OBJ_TO_PTR(source_in);

    mp_int_t x = mp_obj_get_int(args[2]);
    mp_int_t y = mp_obj_get_int(args[3]);
    mp_int_t key = -1;
    if (n_args > 4) {
        key = mp_obj_get_int(args[4]);
    }
    mp_obj_framebuf_t *palette = NULL;
    if (n_args > 5 && args[5] != mp_const_none) {
        palette = MP_OBJ_TO_PTR(mp_obj_cast_to_native_base(args[5], MP_OBJ_FROM_PTR(&mp_type_framebuf)));
    }

    if (
        (x >= self->width) ||
        (y >= self->height) ||
        (-x >= source->width) ||
        (-y >= source->height)
        ) {
        // Out of bounds, no-op.
        return mp_const_none;
    }

    // Clip.
    int x0 = MAX(0, x);
    int y0 = MAX(0, y);
    int x1 = MAX(0, -x);
    int y1 = MAX(0, -y);
    int x0end = MIN(self->width, x + source->width);
    int y0end = MIN(self->height, y + source->height);

    for (; y0 < y0end; ++y0) {
        int cx1 = x1;
        for (int cx0 = x0; cx0 < x0end; ++cx0) {
            uint32_t col = getpixel(source, cx1, y1);
            if (palette) {
                col = getpixel(palette, col, 0);
            }
            if (col != (uint32_t)key) {
                setpixel(self, cx0, y0, col);
            }
            ++cx1;
        }
        ++y1;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_blit_obj, 4, 6, framebuf_blit);

STATIC mp_obj_t framebuf_scroll(mp_obj_t self_in, mp_obj_t xstep_in, mp_obj_t ystep_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t xstep = mp_obj_get_int(xstep_in);
    mp_int_t ystep = mp_obj_get_int(ystep_in);
    int sx, y, xend, yend, dx, dy;
    if (xstep < 0) {
        sx = 0;
        xend = self->width + xstep;
        dx = 1;
    } else {
        sx = self->width - 1;
        xend = xstep - 1;
        dx = -1;
    }
    if (ystep < 0) {
        y = 0;
        yend = self->height + ystep;
        dy = 1;
    } else {
        y = self->height - 1;
        yend = ystep - 1;
        dy = -1;
    }
    for (; y != yend; y += dy) {
        for (int x = sx; x != xend; x += dx) {
            setpixel(self, x, y, getpixel(self, x - xstep, y - ystep));
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(framebuf_scroll_obj, framebuf_scroll);


uint32_t gethzk(mp_obj_framebuf_t *self,uint32_t unicode,uint8_t * chr_data,uint8_t * font_width,uint8_t * font_high,uint8_t * font_stride){
uint8_t gbk[2];
int len;
uint32_t font_index,code_index;
int errcode;

    switch(self->font_inf.Font_Type){
    case 0x0:
        return 1;
        break;
    case 0x1:   //gb2312
    case 0x2:   //gbk
        //printf("unicode to gbk or gb2312\r\n");
        if       ((unicode>=0x000080) && (unicode<=0x00047f)){
          code_index= unicode-0x80  ;
        }else if ((unicode>=0x004e00) && (unicode<=0x009fa5)){
          code_index= unicode-0x4e00+0x0d00;
        }else if ((unicode>=0x00ff00) && (unicode<=0x010000)){
          code_index= unicode-0xff00+0x0C00;
        }else if ((unicode>=0x003000) && (unicode<=0x0030ff)){
          code_index= unicode-0x3000+0x0B00;
        }else if ((unicode>=0x002000) && (unicode<=0x0026ff)){
          code_index= unicode-0x2000+0x0400;
        }else{
          return 2;
        }
        f_seek(self->font_file, code_index *2+0x100,SEEK_SET);
        len=mp_stream_rw(self->font_file ,gbk,2, &errcode, MP_STREAM_RW_READ);
        if (len!=2 && errcode!=0) return 3;
        if (self->font_inf.Font_Type==1){
            if       (gbk[0]>=0xa1 && gbk[0]<=0xa9 && gbk[1]>=0xa1 && gbk[1]<=0xfe ){
                font_index=(gbk[0]-0xa1)*94+gbk[1]-0xa1;
            } else if (gbk[0]>=0xb0 && gbk[0]<=0xf7 && gbk[1]>=0xa1 && gbk[1]<=0xfe ){
                font_index=(gbk[0]-0xb0)*94+gbk[1]-0xa1+846;
            } else {     return 4;        }
        }else if (self->font_inf.Font_Type==2){
            if(gbk[0]>=0x81 && gbk[0]<=0xfe && gbk[1]>=40 && gbk[1]<=0xfe){
                if(gbk[1]==0x7f) return 1;
                if(gbk[1]>0x7f) gbk[1]-=1;
                font_index=(gbk[0]-0x81)*190+gbk[1]-0x40;
            }else{       return 4;      }
        }else{  return 4; }
        //mp_printf(&mp_plat_print,"%8.8X",font_index);
        switch(self->font_set.f_style&0x0f){
        case 0x1:
            if (self->font_inf.Base_Addr12>0){
                * font_width=12;
                * font_high=12;
                * font_stride=2;
                f_seek(self->font_file, font_index*24+self->font_inf.Base_Addr12,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,chr_data, 24 , &errcode, MP_STREAM_RW_READ);
                if (len!=24 && errcode!=0) return 5;
            }else { return 6; }
            break;
        case 0x2:
            if (self->font_inf.Base_Addr16>0){
                * font_width=16;
                * font_high=16;
                * font_stride=2;
                f_seek(self->font_file, font_index*32+self->font_inf.Base_Addr16,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,chr_data, 32 , &errcode, MP_STREAM_RW_READ);
                if (len!=32 && errcode!=0) return 5;
            }else { return 6; }
            break;
        case 0x3:
            if (self->font_inf.Base_Addr24>0){
                * font_width=24;
                * font_high=24;
                * font_stride=3;
                f_seek(self->font_file, font_index*72+self->font_inf.Base_Addr24,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,chr_data, 72 , &errcode, MP_STREAM_RW_READ);
                if (len!=72 && errcode!=0) return 5;
            }else { return 6; }
            break;
        case 0x4:
            if (self->font_inf.Base_Addr32>0){
                * font_width=32;
                * font_high=32;
                * font_stride=4;
                f_seek(self->font_file, font_index*128+self->font_inf.Base_Addr32,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,chr_data, 128 , &errcode, MP_STREAM_RW_READ);
                if (len!=128 && errcode!=0) return 5;
            }else { return 6; }
            break;
        default:
            return 7;
            break;
        }
        break;
    case 0x3:  //small font
        switch(self->font_set.f_style&0x0f){
        case 0x1:
            if (self->font_inf.Base_Addr12>0){
                * font_width=12;
                * font_high=12;
                * font_stride=2;
                uint32_t font_count=0;
                f_seek(self->font_file, 68,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                //mp_printf(&mp_plat_print,"%d--",font_count);
                uint32_t font_index[font_count][2];
                f_seek(self->font_file, self->font_inf.Base_Addr12,SEEK_SET);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                        if (unicode==font_index[find][0]){
                            f_seek(self->font_file, font_index[find][1],SEEK_SET);
                            len=mp_stream_rw(self->font_file ,chr_data, 24 , &errcode, MP_STREAM_RW_READ);
                            if (len!=24 && errcode!=0) { return 5;}
                            else {return 0;  }              
                        }
                }
                return 6; 
            }else { return 6; }
            break;
        case 0x2:
              if (self->font_inf.Base_Addr16>0){
                * font_width=16;
                * font_high=16;
                * font_stride=2;
                uint32_t font_count=0;
                f_seek(self->font_file, 72,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                uint32_t font_index[font_count][2];
                f_seek(self->font_file, self->font_inf.Base_Addr16,SEEK_SET);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                    if (unicode==font_index[find][0]){
                        f_seek(self->font_file, font_index[find][1],SEEK_SET);
                        len=mp_stream_rw(self->font_file ,chr_data, 32 , &errcode, MP_STREAM_RW_READ);
                        if (len!=32 && errcode!=0) {return 5;  }
                        else {return 0;  }                
                    }
                }
                return 6; 
            }else { return 6; }
            break;
        case 0x3:
            if (self->font_inf.Base_Addr24>0){
                * font_width=24;
                * font_high=24;
                * font_stride=3;
                uint32_t font_count=0;
                f_seek(self->font_file, 72,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                uint32_t font_index[font_count][2];
                f_seek(self->font_file, self->font_inf.Base_Addr24,SEEK_SET);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                        if (unicode==font_index[find][0]){
                            f_seek(self->font_file, font_index[find][1],SEEK_SET);
                            len=mp_stream_rw(self->font_file ,chr_data, 72, &errcode, MP_STREAM_RW_READ);
                            if (len!=72 && errcode!=0) {return 5;  }
                            else {return 0;  }               
                        }
                }
                return 6; 
            }else { return 6; }
            break;
        case 0x4:
            if (self->font_inf.Base_Addr32>0){
                * font_width=32;
                * font_high=32;
                * font_stride=4;
                uint32_t font_count=0;
                f_seek(self->font_file, 76,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                uint32_t font_index[font_count][2];
                f_seek(self->font_file, self->font_inf.Base_Addr32,SEEK_SET);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                    if (unicode==font_index[find][0]){
                        f_seek(self->font_file, font_index[find][1],SEEK_SET);
                        len=mp_stream_rw(self->font_file ,chr_data, 128 , &errcode, MP_STREAM_RW_READ);
                        if (len!=128 && errcode!=0) {return 5;  }
                        else {return 0;  }              
                    }
                }
                return 6; 
            }else { return 6; }
            break;
        }
        break;
    default:
        return 1;
        break;
    }
    return 0;
}


uint32_t getasc(uint8_t f_style,uint8_t chr,uint8_t * chr_data,uint8_t * font_width,uint8_t * font_high,uint8_t * font_stride){
    switch(f_style&0x0f){
    case 0x0:
	    * font_width=6;
	    * font_high=8;
	    * font_stride=1;
	    memcpy(chr_data,&font_s_6X8[(chr - 32) * 8],8);
	    break;    	
    case 0x1:
        switch((f_style>>4)&0x0f){
        case 0x01:
            * font_width=6;
            * font_high=12;
            * font_stride=1;
            memcpy(chr_data,&font_s_6X12[(chr - 32) * 12],12);
            break;
        case 0x02:
            * font_width=6;
            * font_high=12;
            * font_stride=1;
            memcpy(chr_data,&font_c_6X12[(chr - 32) * 12],12);
            break;
        case 0x03:
            * font_width=font_a_6X12[(chr - 32) * 26+1];
            * font_high=12;
            * font_stride=2;
            memcpy(chr_data,&font_a_6X12[(chr - 32) * 26+2],24);
            break;
        case 0x04:
            * font_width=font_a_6X12[(chr - 32) * 26+1];
            * font_high=12;
            * font_stride=2;
            memcpy(chr_data,&font_r_6X12[(chr - 32) * 26+2],24);
            break;
        default :
            return 1;
        }
        break;
    case 0x2:
        switch((f_style>>4)&0x0f){
        case 0x01:
            * font_width=8;
            * font_high=16;
            * font_stride=1;
            memcpy(chr_data,&font_s_8X16[(chr - 32) * 16],16);
            break;
        case 0x02:
            * font_width=8;
            * font_high=16;
            * font_stride=1;
            memcpy(chr_data,&font_c_8X16[(chr - 32) * 16],16);
            break;
        case 0x03:
            * font_width=font_a_8X16[(chr - 32) * 34+1];
            * font_high=16;
            * font_stride=2;
            memcpy(chr_data,&font_a_8X16[(chr - 32) * 34+2],32);
            break;
        case 0x04:
            * font_width=font_a_8X16[(chr - 32) * 34+1];
            * font_high=16;
            * font_stride=2;
            memcpy(chr_data,&font_r_8X16[(chr - 32) * 34+2],32);
            break;
        default :
            return 1;
        }        
        break;            
    case 0x3:
        switch((f_style>>4)&0x0f){
        case 0x01:
            * font_width=12;
            * font_high=24;
            * font_stride=2;
            memcpy(chr_data,&font_s_12X24[(chr - 32) * 48],48);
            break;
        case 0x02:
            * font_width=12;
            * font_high=24;
            * font_stride=2;
            memcpy(chr_data,&font_c_12X24[(chr - 32) * 48],48);
            break;
        case 0x03:
            * font_width=font_a_12X24[(chr - 32) * 74+1];
            * font_high=24;
            * font_stride=3;
            memcpy(chr_data,&font_a_12X24[(chr - 32) * 74+2],72);
            break;
        case 0x04:
            * font_width=font_a_12X24[(chr - 32) * 74+1];
            * font_high=24;
            * font_stride=3;
            memcpy(chr_data,&font_r_12X24[(chr - 32) * 74+2],72);
            break;
        default :
            return 1;
        }        
        break;            
    case 0x4:
        switch((f_style>>4)&0x0f){
        case 0x01:
            * font_width=16;
            * font_high=32;
            * font_stride=2;
            memcpy(chr_data,&font_s_16X32[(chr - 32) * 64],64);
            break;
        case 0x02:
            * font_width=16;
            * font_high=32;
            * font_stride=2;
            memcpy(chr_data,&font_c_16X32[(chr - 32) * 64],64);
            break;
        case 0x03:
            * font_width=font_a_16X32[(chr - 32) * 130+1];
            * font_high=32;
            * font_stride=4;
            memcpy(chr_data,&font_a_16X32[(chr - 32) * 130+2],128);
            break;
        case 0x04:
            * font_width=font_a_16X32[(chr - 32) * 130+1];
            * font_high=32;
            * font_stride=4;
            memcpy(chr_data,&font_r_16X32[(chr - 32) * 130+2],128);
            break;
        default :
            return 1;
        }        
        break;            
    default:
        return 1;
    } 
    return 0;
}

STATIC mp_obj_t framebuf_text(size_t n_args, const mp_obj_t *args) {
    // extract arguments
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    const char *str = mp_obj_str_get_str(args[1]);
    mp_int_t x0 = mp_obj_get_int(args[2]);
    mp_int_t y0 = mp_obj_get_int(args[3]);
    mp_int_t col = 1;
    uint8_t chr_data[130];
    uint32_t mask,unicode;
    uint8_t utf8len;
    uint8_t ret=1;
    uint8_t font_width,font_high,font_stride=2;
    if (n_args >= 5) {
        col = mp_obj_get_int(args[4]);
    }

    // loop over chars
    for (; *str; ++str) {
        // get char and make sure its in range of font
        int chr = *(uint8_t *)str;
        //panding tiaojian duqu yingwen he zhongwenziku
        if (chr<0x80){
            if (chr < 32 || chr > 127) {
                //mp_printf(&mp_plat_print,"%2.2X",chr);    
                continue;
            }
            ret=getasc(self->font_set.f_style,chr,chr_data,&font_width,&font_high,&font_stride);

        }else{
            if (chr>=0xC0 && chr<0xE0) {  //2
                utf8len=2;
                unicode=(chr&0x1f)<<6*(utf8len-1);
            }else if(chr>=0xE0 && chr<0xF0){
                utf8len=3;
                unicode=(chr&0x0f)<<6*(utf8len-1);
            }else if(chr>=0xF0 && chr<0xF8){
                utf8len=4;
                unicode=(chr&0x07)<<6*(utf8len-1);
            }else if(chr>=0xF8 && chr<0xFC){
                utf8len=5;
                unicode=(chr&0x03)<<6*(utf8len-1);
            }else if(chr>=0xFC){
                utf8len=6;
                unicode=(chr&0x01)<<6*(utf8len-1);
            }else {
                unicode=0;
                continue;
            }
            for (uint32_t i=0;i<utf8len-1;i++){
                ++str;
                chr = *(uint8_t *)str;
                if ( (chr & 0xC0) != 0x80 ){
                    unicode=0;
                    continue;
                }
                else{
                    unicode|=(chr&0x3f)<<6*(utf8len-2-i);
                }	                
            }
            //mp_printf(&mp_plat_print,"u%4.4X,",unicode);
            ret=gethzk(self,unicode,chr_data,&font_width,&font_high,&font_stride);
        }
        // loop over char data
        
        if (ret==0){
            mask=0x00000001<<(font_stride*8-1);
            if (((self->font_set.rotate==0 ||self->font_set.rotate==2)&&
                (x0 < self->width) && (y0 < self->height)&&
                (x0 > -font_width*self->font_set.scale)&&
                (y0 > -font_high*self->font_set.scale)) || 
                ((self->font_set.rotate==1 ||self->font_set.rotate==3)&&
                (x0  < self->width) && (y0 < self->height)&&
                (x0 > -font_high*self->font_set.scale)&&
                (y0 > -font_width*self->font_set.scale))){
                for (int y = 0; y < font_high; y++){
                    uint line_data=0;
                    for(int k=0;k<font_stride;k++){
                        line_data |= chr_data[y*font_stride+k]<<((font_stride-k-1)*8); // each byte 
                    }
                    for (int x=0; x<font_width; x++) { // scan over vertical column
                        if (((line_data & (mask>>x))&&self->font_set.inverse==0) ||
                        ((~line_data & (mask>>x))&&self->font_set.inverse==1)){ 
                        // only draw if pixel set
                            for (int x_scale=0;x_scale<self->font_set.scale;x_scale++){
                                for (int y_scale=0;y_scale<self->font_set.scale;y_scale++){
                                    switch(self->font_set.rotate){
                                    case 0:
                                        setpixel(self, x0+x*self->font_set.scale+x_scale, y0+y*self->font_set.scale+y_scale, col);
                                        break;
                                    case 1:
                                        setpixel(self, x0+font_high*self->font_set.scale-y*self->font_set.scale-y_scale, y0+x*self->font_set.scale+x_scale, col);
                                        break;
                                    case 2:
                                        setpixel(self, x0+font_width*self->font_set.scale-x*self->font_set.scale-x_scale, y0+font_high*self->font_set.scale-y*self->font_set.scale-y_scale, col);
                                        break;
                                    case 3:
                                        setpixel(self, x0+y*self->font_set.scale-y_scale, y0+font_width*self->font_set.scale-x*self->font_set.scale-x_scale, col);
                                        break;
                                    }
                                }
                            }
                        }else{
                            if (self->font_set.transparent==0){
                                for (int x_scale=0;x_scale<self->font_set.scale;x_scale++){
                                    for (int y_scale=0;y_scale<self->font_set.scale;y_scale++){
                                        switch(self->font_set.rotate){
                                        case 0:
                                            setpixel(self, x0+x*self->font_set.scale+x_scale, y0+y*self->font_set.scale+y_scale, self->font_set.bg_col);
                                            break;
                                        case 1:
                                            setpixel(self, x0+font_high*self->font_set.scale-y*self->font_set.scale-y_scale, y0+x*self->font_set.scale+x_scale, self->font_set.bg_col);
                                            break;
                                        case 2:
                                            setpixel(self, x0+font_width*self->font_set.scale-x*self->font_set.scale-x_scale, y0+font_high*self->font_set.scale-y*self->font_set.scale-y_scale, self->font_set.bg_col);
                                            break;
                                        case 3:
                                            setpixel(self, x0+y*self->font_set.scale-y_scale, y0+font_width*self->font_set.scale-x*self->font_set.scale-x_scale, self->font_set.bg_col);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if ((self->font_set.rotate==0 ||self->font_set.rotate==2)){
                x0+=font_width*self->font_set.scale;
            }else{
                x0+=font_high*self->font_set.scale;
            }
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_text_obj, 4, 5, framebuf_text);


typedef struct __attribute__((packed)) tagBITMAPFILEHEADER 
{  
    uint16_t bfType;    
    uint32_t bfSize; 
    uint16_t bfReserved1; 
    uint16_t bfReserved2; 
    uint32_t bfOffBits;
    uint32_t biHSize;
    uint32_t biWidth;
    uint32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitcount;
    uint32_t biComp;
    uint32_t biPSize;
    uint32_t biXPelPerMeter;
    uint32_t biYPelPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
       
} BITMAPFILEHEADER;     

STATIC mp_obj_t framebuf_show_bmp(size_t n_args, const mp_obj_t *args) {
    // extract arguments
    BITMAPFILEHEADER bmp_h;
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    const char *filename = mp_obj_str_get_str(args[1]);
    mp_int_t inv = 0;
    if (n_args > 2) {
        inv = mp_obj_get_int(args[2]);
    }
    mp_int_t x0 = 0;
    mp_int_t y0 = 0;
    if (n_args > 4) {
        x0 = mp_obj_get_int(args[3]);
        y0 = mp_obj_get_int(args[4]);
    } 
    mp_int_t w = self->width-x0;
    mp_int_t h = self->height-y0;
    if (n_args > 6) {
        w = MIN(mp_obj_get_int(args[5]),w);
        h = MIN(mp_obj_get_int(args[6]),h);
    }
    mp_obj_t f_args[2] = {
        mp_obj_new_str(filename, strlen(filename)),
        MP_OBJ_NEW_QSTR(MP_QSTR_rb),
    }; 
    mp_obj_t bmp_file = mp_vfs_open(MP_ARRAY_SIZE(f_args), &f_args[0], (mp_map_t *)&mp_const_empty_map);
    int errcode;
    int len=mp_stream_rw(bmp_file ,&bmp_h, sizeof(BITMAPFILEHEADER), &errcode, MP_STREAM_OP_READ);
    if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
        mp_raise_OSError(errcode);
        mp_printf(&mp_plat_print,"read file hard %s error!\r\n",filename);
    }
    if (bmp_h.bfType!=0x4d42){
        mp_printf(&mp_plat_print,"File %s not BMP.\r\n",filename);
        return mp_const_none;
    }
    if ((self->format&0xE0)==(FRAMEBUF_RGB565&0xE0)){
        if (bmp_h.biBitcount!=0x18){
            mp_printf(&mp_plat_print,"File %s color no match.\r\n",filename);
            return mp_const_none;
        }
    }else if(((self->format&0xE0)==(FRAMEBUF_MON_VLSB&0xE0))||((self->format&0xE0)==(FRAMEBUF_ST7302&0xE0))){
        if (bmp_h.biBitcount!=0x01){
            mp_printf(&mp_plat_print,"File %s color no match.\r\n",filename);
            return mp_const_none;
        }
    }else if (((self->format&0xE0)==(FRAMEBUF_GS2_HMSB&0xE0))  || ((self->format&0xE0)==(FRAMEBUF_GS4_HMSB&0xE0)) \
        || ((self->format&0xE0)==(FRAMEBUF_GS8_H&0xE0))){
        if (bmp_h.biBitcount!=0x08){
            mp_printf(&mp_plat_print,"File %s color no match.\r\n",filename);
            return mp_const_none;
        }        
    }else {
        mp_printf(&mp_plat_print,"Unsupported format. \r\n");
        return mp_const_none;
    }
    if (x0<0 && abs(x0)>bmp_h.biWidth) x0=-bmp_h.biWidth;
    if (y0<0 && abs(y0)>bmp_h.biHeight) y0=-bmp_h.biHeight;
    if (x0>self->width) x0=self->width;
    if (y0>self->height) y0=self->height;
    if (w>self->width-x0) w=self->width-x0;    
    if (h>self->height-y0) h=self->height-y0;   

    
    w=MIN(bmp_h.biWidth,w);
    h=MIN(bmp_h.biHeight,h);
    f_seek(bmp_file, bmp_h.bfOffBits,SEEK_SET);
    if ((self->format&0xE0)==(FRAMEBUF_RGB565&0xE0)){
        uint32_t stride=(bmp_h.biWidth+3)&(~0x03);
        uint8_t line_buf[stride][3];
        int32_t hh,ww;
        uint16_t dot_col;
        for(hh=bmp_h.biHeight;hh;hh--){
            len=mp_stream_rw(bmp_file ,&line_buf, stride*3, &errcode, MP_STREAM_OP_READ);
            if (errcode != 0 && len!=stride*3) {
                mp_raise_OSError(errcode);
                return mp_const_none;
            }            
            for(ww=bmp_h.biWidth;ww;ww--){
                if (ww<=w && hh<=h){
                    switch(self->format&0x0F){
                        case 0:
                        case 1:
                        dot_col=line_buf[ww-1][0]>>3;
                        dot_col|=(line_buf[ww-1][1]&0xfc)<<3;
                        dot_col|=(line_buf[ww-1][2]&0xf8)<<8;
                        setpixel(self, x0+ww-1, y0+hh-1,dot_col);
                        break;
                        case 2:
                        case 3:
                        dot_col=line_buf[ww-1][0];
                        dot_col|=line_buf[ww-1][1]<<8;
                        dot_col|=line_buf[ww-1][2]<<16;
                        setpixel(self, x0+ww-1, y0+hh-1,dot_col);
                    }
                }
            }
        }
    }else if(((self->format&0xE0)==(FRAMEBUF_MON_VLSB&0xE0))||((self->format&0xE0)==(FRAMEBUF_ST7302&0xE0))){
        uint32_t stride=(bmp_h.biWidth+0x1F)&(~0x1F);
        uint8_t line_buf[stride/8];
        int32_t hh,ww;
        int32_t len;
        for(hh=bmp_h.biHeight;hh;--hh){
            len=mp_stream_rw(bmp_file ,&line_buf, stride/8, &errcode, MP_STREAM_OP_READ);
            if ((errcode != 0) && (len!=stride/8)){
                mp_printf(&mp_plat_print,"read file %s error!\r\n",filename);
                return mp_const_none;
            }
             for(ww=bmp_h.biWidth;ww;--ww){
                if (ww<w && hh<h){
                    if ((line_buf[ww/8]&(0x80>>(ww%8)))==0){
                        setpixel(self, x0+ww-1, y0+hh-1,inv?0:1);
                    }else{
                        setpixel(self, x0+ww-1, y0+hh-1,inv?1:0);
                    }
                }
            }
        }
    }else if ((self->format&0xE0)==(FRAMEBUF_GS2_HMSB&0xE0)){
        uint32_t stride=(bmp_h.biWidth+0x0F)&(~0x0F);
        uint8_t line_buf[stride/4];
        int32_t hh,ww;
        for(hh=bmp_h.biHeight;hh;--hh){

            len=mp_stream_rw(bmp_file ,&line_buf, stride/4, &errcode, MP_STREAM_OP_READ);
            if ((errcode != 0) && (len!=stride/4)){
                mp_printf(&mp_plat_print,"read file %s error!\r\n",filename);
                return mp_const_none;
            }
            for(ww=bmp_h.biWidth;ww;--ww){
                if (ww<w && hh<h){
                    if (inv==0){
                        setpixel(self, x0+ww-1, y0+hh-1,((line_buf[ww-1])>>6)&0x03);
                    }else{
                        setpixel(self, x0+ww-1, y0+hh-1,((~line_buf[ww-1])>>6)&0x03);
                    }
                }
            }
        }
    }else if ((self->format&0xE0)==(FRAMEBUF_GS4_HMSB&0xE0)){
        uint32_t stride=(bmp_h.biWidth+0x07)&(~0x07);
        uint8_t line_buf[stride/2];
        int32_t hh,ww;
        for(hh=bmp_h.biHeight;hh;--hh){
            len=mp_stream_rw(bmp_file ,&line_buf, stride/2, &errcode, MP_STREAM_OP_READ);
            if ((errcode != 0) && (len!=stride/2)){
                mp_printf(&mp_plat_print,"read file %s error!\r\n",filename);
                return mp_const_none;
            }
            for(ww=bmp_h.biWidth;ww;--ww){
                if (ww<w && hh<h){
                    if (inv==0){
                        setpixel(self, x0+ww-1, y0+hh-1,((line_buf[ww-1])>>4)&0x0f);
                    }else{
                        setpixel(self, x0+ww-1, y0+hh-1,((~line_buf[ww-1])>>4)&0x0f);
                    }
                }
            }
            if (bmp_h.biWidth%8>0){  
                len=mp_stream_rw(bmp_file ,&line_buf, 3-(bmp_h.biWidth%8+1)/2, &errcode, MP_STREAM_OP_READ);
                if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
                    mp_raise_OSError(errcode);
                    return mp_const_none;          
                }
            }
        }
    }else if ((self->format&0xE0)==(FRAMEBUF_GS8_V&0xE0)){
        uint32_t stride=(bmp_h.biWidth+0x03)&(~0x03);
        uint8_t line_buf[stride];
        int32_t hh,ww;
        for(hh=bmp_h.biHeight;hh;--hh){
            len=mp_stream_rw(bmp_file ,&line_buf, stride, &errcode, MP_STREAM_OP_READ);
            if ((errcode != 0) && (len!=stride)){
                mp_printf(&mp_plat_print,"read file %s error!\r\n",filename);
                return mp_const_none;
            }
            for(ww=bmp_h.biWidth;ww;--ww){
                if (ww<w && hh<h){
                    if (inv==0){
                        setpixel(self, x0+ww-1, y0+hh-1,line_buf[ww-1]);
                    }else{
                        setpixel(self, x0+ww-1, y0+hh-1,~line_buf[ww-1]);
                    }
                    
                }
            }
        }
    }
    mp_stream_close(bmp_file);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_show_bmp_obj, 2, 7, framebuf_show_bmp);


STATIC mp_obj_t framebuf_save_bmp(size_t n_args, const mp_obj_t *args) {
    // extract arguments
    BITMAPFILEHEADER bmp_h;
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    const char *filename = mp_obj_str_get_str(args[1]);
    mp_int_t x0 = 0;
    mp_int_t y0 = 0;
    if (n_args >= 4) {
        x0 = mp_obj_get_int(args[2]);
        y0 = mp_obj_get_int(args[3]);
    } 
    mp_int_t w = self->width-x0;
    mp_int_t h = self->height-y0;
    if (n_args >= 6) {
        w = MIN(mp_obj_get_int(args[4]),self->width-x0);
        h = MIN(mp_obj_get_int(args[5]),self->height-y0);
    }

    //mp_printf(&mp_plat_print,"%d,%d,%d,%d. \r\n",x0,y0,w,h);
    memset(&bmp_h,0,sizeof(BITMAPFILEHEADER));   
    bmp_h.bfType=0x4d42;
    if (self->format==FRAMEBUF_RGB565 || self->format==FRAMEBUF_RGB565SW){
        bmp_h.bfOffBits=0x36;//dian zhen cun chu pian yi
        bmp_h.biBitcount=0x18;
        bmp_h.bfSize=bmp_h.bfOffBits + w*h*3;   //wen jian zong chi cun
        bmp_h.biPSize=w*h*3;
    }else if(((self->format&0xE0)==(FRAMEBUF_MON_VLSB&0xE0))||((self->format&0xE0)==(FRAMEBUF_ST7302&0xE0))){
        bmp_h.bfOffBits=0x3e;//dian zhen cun chu pian yi
        bmp_h.biBitcount=0x01;
        bmp_h.bfSize=bmp_h.bfOffBits +(w+7)*h;   //wen jian zong chi cun
        bmp_h.biPSize=(w+7)/8*h;
    }else if (((self->format&0xE0)==(FRAMEBUF_GS2_HMSB&0xE0)) || ((self->format&0xE0)==(FRAMEBUF_GS4_HMSB&0xE0)) || ((self->format&0xE0)==(FRAMEBUF_GS8_H&0xE0))){
        bmp_h.bfOffBits=0x436;//dian zhen cun chu pian yi
        bmp_h.biBitcount=0x08;
        bmp_h.bfSize=bmp_h.bfOffBits + w*h;   //wen jian zong chi cun
        bmp_h.biPSize=w*h;
    }else {
        mp_printf(&mp_plat_print,"Unsupported format. \r\n");
        return mp_const_none;
    }
    bmp_h.biComp=0x00;
    bmp_h.biHSize=0x28;
    bmp_h.biWidth=w;
    bmp_h.biHeight=h;
    bmp_h.biPlanes=1;
    bmp_h.biXPelPerMeter=0;
    bmp_h.biYPelPerMeter=0;
    bmp_h.biClrUsed=0x100;
    bmp_h.biClrImportant=0x100;
    mp_obj_t f_args[2] = {
        mp_obj_new_str(filename, strlen(filename)),
        MP_OBJ_NEW_QSTR(MP_QSTR_wb),
    }; 
    //mp_printf(&mp_plat_print,"open file. \r\n");
    mp_obj_t bmp_file = mp_vfs_open(MP_ARRAY_SIZE(f_args), &f_args[0], (mp_map_t *)&mp_const_empty_map);
    int errcode;
    //mp_printf(&mp_plat_print,"write head. \r\n");
    int len=mp_stream_rw(bmp_file ,&bmp_h, sizeof(BITMAPFILEHEADER), &errcode, MP_STREAM_OP_WRITE);
    if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
        mp_raise_OSError(errcode);
        mp_printf(&mp_plat_print,"Write %s error!\r\n",filename);
    }
    uint8_t buf[4];
    if (bmp_h.biBitcount==0x08){
        uint32_t clr;
        for (clr=0;clr<0x100;clr++){
            buf[0]=clr;
            buf[1]=clr;
            buf[2]=clr;
            buf[3]=0;
            len=mp_stream_rw(bmp_file ,&buf, 4, &errcode, MP_STREAM_OP_WRITE);
            if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
                mp_raise_OSError(errcode);
                mp_printf(&mp_plat_print,"Write %s error!\r\n",filename);
            }
        }
    }
    if (bmp_h.biBitcount==0x01){
        uint32_t clr;
        for (clr=0;clr<0x02;clr++){
            buf[0]=0xff*clr;
            buf[1]=0xff*clr;
            buf[2]=0xff*clr;
            buf[3]=0;
            len=mp_stream_rw(bmp_file ,&buf, 4, &errcode, MP_STREAM_OP_WRITE);
            if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
                mp_raise_OSError(errcode);
                mp_printf(&mp_plat_print,"Write %s error!\r\n",filename);
            }
        }
    }
    //mp_printf(&mp_plat_print,"write pixel. \r\n");
    if ((self->format&0xE0)==(FRAMEBUF_RGB565&0xE0)){
        uint32_t stride=(w+0x03)&(~0x03);
        uint8_t line_buf[stride][3];
        uint32_t hh,ww;
        uint16_t dot_col;
        for(hh=h;hh;hh--){
            memset(&line_buf,0,stride*3);
            for(ww=w;ww;ww--){
                dot_col=getpixel(self, x0+ww-1, y0+hh-1);
                switch(self->format&0x0F) {
                    case 0:
                    case 1:
                    line_buf[ww-1][2]=(dot_col&0xf800)>>8;
                    line_buf[ww-1][1]=(dot_col&0x07e0)>>3;
                    line_buf[ww-1][0]=(dot_col&0x001e)<<3;
                    break;
                    case 2:
                    case 3:
                    line_buf[ww-1][2]=(dot_col&0xff0000)>>16;
                    line_buf[ww-1][1]=(dot_col&0xff00)>>8;
                    line_buf[ww-1][0]=(dot_col&0x00ff);
                    break;
                }
            }
            len=mp_stream_rw(bmp_file ,&line_buf, stride*3, &errcode, MP_STREAM_OP_WRITE);
        }
    }else if(((self->format&0xE0)==(FRAMEBUF_MON_VLSB&0xE0))||((self->format&0xE0)==(FRAMEBUF_ST7302&0xE0))){
        uint32_t stride=(w+0x1F)&(~0x1F);
        uint8_t line_buf[stride];
        uint32_t hh,ww;
        for(hh=h;hh;--hh){
            memset(&line_buf,0,stride);
            for(ww=w;ww;--ww){
                if (getpixel(self, x0+ww-1, y0+hh-1)==0)
                    line_buf[(ww-1)/8]|=(0x80)>>((ww-1)%8);
            }
            len=mp_stream_rw(bmp_file ,&line_buf, stride, &errcode, MP_STREAM_OP_WRITE);
        }
    }else if ((self->format&0xE0)==(FRAMEBUF_GS2_HMSB&0xE0)){
        uint32_t stride=(w+0x03)&(~0x03);
        uint8_t line_buf[stride];
        uint32_t hh,ww;
        for(hh=h;hh;--hh){
            memset(&line_buf,0,stride);
            for(ww=w;ww;--ww){
                line_buf[(ww-1)]=getpixel(self, x0+ww-1, y0+hh-1)<<6;
            }
            len=mp_stream_rw(bmp_file ,&line_buf, stride, &errcode, MP_STREAM_OP_WRITE);
        }
    }else if ((self->format&0xE0)==(FRAMEBUF_GS4_HMSB&0xE0)){
        uint32_t stride=(w+0x03)&(~0x03);
        uint8_t line_buf[stride];
        uint32_t hh,ww;
        for(hh=h;hh;--hh){
            memset(&line_buf,0,stride);
            for(ww=w;ww;--ww){
                line_buf[(ww-1)]=getpixel(self, x0+ww-1, y0+hh-1)<<4;
            }
            len=mp_stream_rw(bmp_file ,&line_buf, stride, &errcode, MP_STREAM_OP_WRITE);
        }
    }else if ((self->format&0xE0)==(FRAMEBUF_GS8_H&0xE0)){
        uint32_t stride=(w+0x03)&(~0x03);
        uint8_t line_buf[stride];
        uint32_t hh,ww;
        for(hh=h;hh;--hh){
            memset(&line_buf,0,stride);
            for(ww=w;ww;--ww){
                line_buf[(ww-1)]=getpixel(self, x0+ww-1, y0+hh-1);
            }
            len=mp_stream_rw(bmp_file ,&line_buf, stride, &errcode, MP_STREAM_OP_WRITE);
        }
    }    //mp_printf(&mp_plat_print,"close file. \r\n");
    mp_stream_close(bmp_file);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_save_bmp_obj, 2, 6, framebuf_save_bmp);


STATIC mp_obj_t framebuf_line_LUT(mp_obj_t self_in, mp_obj_t line, mp_obj_t lut_in) {
    // 行转换，输入参数：行，查找表，返回行数据
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t lut;
    mp_get_buffer_raise(lut_in, &lut, MP_BUFFER_READ);
    uint16_t line_num = mp_obj_get_int(line);
    uint16_t buff[self->width];
    mp_obj_t  ret_buf = mp_obj_new_bytearray(32,(uint8_t*)&buff);
    uint16_t col_dot;
    if (lut.len!=32) {
        mp_printf(&mp_plat_print,"LUT table Error. \r\n");
        return mp_const_none;
    }
    if ((self->format&0xF0)==(FRAMEBUF_GS4_HMSB&0xF0)){
        for(int ww=0;ww<self->width;ww++)
        {
            col_dot=getpixel(self, ww, line_num);
            buff[ww] =((uint16_t *) lut.buf)[col_dot];
        }
    } else{
        mp_printf(&mp_plat_print,"Only 4 bit mode is supported. \r\n");
        return mp_const_none;
    }
    return ret_buf;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(framebuf_line_LUT_obj, framebuf_line_LUT);


STATIC mp_obj_t framebuf_ToGBK(mp_obj_t self_in, mp_obj_t  str_in) {
    mp_obj_framebuf_t *self =MP_OBJ_TO_PTR(self_in);
    uint8_t gbk_str[202]; 
    uint8_t gbk[2];
    uint32_t p=0;
    uint32_t unicode,code_index,utf8len;
    uint8_t chr;
    int errcode;
    const char *str = mp_obj_str_get_str(str_in);
    for (; *str; ++str) {
        if (p>200) break;
        chr = *(uint8_t *)str;
        if (chr<0x80){
            gbk_str[p] = *(uint8_t *)str;
            p++;
        }else{
            if (chr>=0xC0 && chr<0xE0) {  //2
                utf8len=2;
                unicode=(chr&0x1f)<<6*(utf8len-1);
            }else if(chr>=0xE0 && chr<0xF0){
                utf8len=3;
                unicode=(chr&0x0f)<<6*(utf8len-1);
            }else if(chr>=0xF0 && chr<0xF8){
                utf8len=4;
                unicode=(chr&0x07)<<6*(utf8len-1);
            }else if(chr>=0xF8 && chr<0xFC){
                utf8len=5;
                unicode=(chr&0x03)<<6*(utf8len-1);
            }else if(chr>=0xFC){
                utf8len=6;
                unicode=(chr&0x01)<<6*(utf8len-1);
            }else {
                unicode=0;
                continue;
            }
            for (uint32_t i=0;i<utf8len-1;i++){
                ++str;
                chr = *(uint8_t *)str;
                if ( (chr & 0xC0) != 0x80 ){
                    unicode=0;
                    continue;
                }
                else{
                    unicode|=(chr&0x3f)<<6*(utf8len-2-i);
                }	                
            }
            switch(self->font_inf.Font_Type){
            case 0x1:   //gb2312
            case 0x2:   //gbk
                //printf("unicode to gbk or gb2312\r\n");
                if       ((unicode>=0x000080) && (unicode<=0x00047f)){
                    code_index= unicode-0x80  ;
                }else if ((unicode>=0x004e00) && (unicode<=0x009fa5)){
                    code_index= unicode-0x4e00+0x0d00;
                }else if ((unicode>=0x00ff00) && (unicode<=0x010000)){
                    code_index= unicode-0xff00+0x0C00;
                }else if ((unicode>=0x003000) && (unicode<=0x0030ff)){
                    code_index= unicode-0x3000+0x0B00;
                }else if ((unicode>=0x002000) && (unicode<=0x0026ff)){
                    code_index= unicode-0x2000+0x0400;
                }else{
                    continue;
                }
                f_seek(self->font_file, code_index *2+0x100,SEEK_SET);
                int len=mp_stream_rw(self->font_file ,gbk,2, &errcode, MP_STREAM_RW_READ);
                if (len==2 && errcode==0){
                    gbk_str[p++] = gbk[0];
                    gbk_str[p++] = gbk[1];
                }
                break;
            default:
                mp_printf(&mp_plat_print,"To convert utf8 to gbk, you need to load a complete font library.\r\n");
                break;
            }
        }
    }
    mp_obj_t  ret_buf = mp_obj_new_bytearray(p,gbk_str);
    return ret_buf;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(framebuf_ToGBK_obj, framebuf_ToGBK);

STATIC mp_obj_t framebuf_curve(size_t n_args, const mp_obj_t *args) {
    //输入数据，x,y,x_scale,y_scale,mode,col
    //
    // 使用列表或者bytearray输入绘制曲线
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_t Data_obj = args[1];
    //if((mp_obj_is_type(Chart_obj, &mp_type_tuple) || mp_obj_is_type(Chart_obj, &mp_type_list)))
    //mp_obj_get_array(Chart_obj, &duration_length, &duration_ptr);

    mp_buffer_info_t Data;
    if (n_args > 1) {
        mp_get_buffer_raise(Data_obj, &Data, MP_BUFFER_READ);
    }else{
        mp_printf(&mp_plat_print,"d\r\n");
        return mp_const_none;
    }
    mp_int_t mode=0;
    if (n_args > 2) {
        mode = mp_obj_get_int(args[2]);
        if (mode>2){   mode=0;   }
    }
    mp_int_t col=0;
    if (n_args > 3) {
        col = mp_obj_get_int(args[3]);
    }
    mp_int_t x0 = 0;
    mp_int_t y0 = self->height/2;
    if (n_args > 5) {
        x0 = mp_obj_get_int(args[4]);
        y0 = mp_obj_get_int(args[5]);
    } 
    mp_int_t x_scale = 1;
    if (n_args > 6) {
        x_scale = mp_obj_get_int(args[6]);
    }
    mp_int_t y_scale = self->height/2;
    if (n_args > 7) {
        y_scale = mp_obj_get_int(args[7]);
    }
    mp_int_t lenght=0;
    mp_int_t y_shift=0;
    if ((Data.typecode==BYTEARRAY_TYPECODE) || (Data.typecode=='B')){
        lenght=Data.len;
        y_shift=256;
    }else if (Data.typecode=='b'){
        lenght=Data.len;
        y_shift=128;
    }else if (Data.typecode=='H') {
        lenght=Data.len/2;
        y_shift=65536;    
    }else if(Data.typecode=='h'){
        lenght=Data.len/2;
        y_shift=32768;
    }else{
        mp_printf(&mp_plat_print,"input buffer must in B b H h\r\n");
        return mp_const_none;
    }
   
    mp_int_t prev=0;
    mp_int_t curr=0;
    for (int count=0;count<lenght;count++){
        curr=poly_int(&Data, count);
        switch (mode){
            case 0:
                setpixel(self,x0+count*x_scale,y0+curr*y_scale/y_shift,col);
            break;
            case 1:
                if (count>0){
                    line(self,x0+(count-1)*x_scale,y0+prev*y_scale/y_shift,x0+count*x_scale,y0+curr*y_scale/y_shift,col);
                }
                prev=curr;
            break;
            case 2:
                if (curr>0){
                    fill_rect(self,(x0+count)*x_scale,y0,x_scale,abs(curr*y_scale/y_shift),col);
                }else{
                    fill_rect(self,(x0+count)*x_scale,y0+curr*y_scale/y_shift,x_scale,abs(curr*y_scale/y_shift),col);
                }
            break;
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_curve_obj, 2, 8, framebuf_curve);
#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_rom_map_elem_t framebuf_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_show_bmp),    MP_ROM_PTR(&framebuf_show_bmp_obj) },
    { MP_ROM_QSTR(MP_QSTR_save_bmp),    MP_ROM_PTR(&framebuf_save_bmp_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_load),   MP_ROM_PTR(&framebuf_font_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_free),   MP_ROM_PTR(&framebuf_font_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_set),    MP_ROM_PTR(&framebuf_font_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_ToGBK),       MP_ROM_PTR(&framebuf_ToGBK_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill),        MP_ROM_PTR(&framebuf_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),   MP_ROM_PTR(&framebuf_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel),       MP_ROM_PTR(&framebuf_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_hline),       MP_ROM_PTR(&framebuf_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_vline),       MP_ROM_PTR(&framebuf_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect),        MP_ROM_PTR(&framebuf_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line),        MP_ROM_PTR(&framebuf_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_ellipse),     MP_ROM_PTR(&framebuf_ellipse_obj) },
    #if MICROPY_PY_ARRAY
    { MP_ROM_QSTR(MP_QSTR_poly),        MP_ROM_PTR(&framebuf_poly_obj) },
    { MP_ROM_QSTR(MP_QSTR_curve),       MP_ROM_PTR(&framebuf_curve_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_blit),        MP_ROM_PTR(&framebuf_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll),      MP_ROM_PTR(&framebuf_scroll_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),        MP_ROM_PTR(&framebuf_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_line_LUT),    MP_ROM_PTR(&framebuf_line_LUT_obj) },

};
STATIC MP_DEFINE_CONST_DICT(framebuf_locals_dict, framebuf_locals_dict_table);

STATIC MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_framebuf,
    MP_QSTR_FrameBuffer,
    MP_TYPE_FLAG_NONE,
    make_new, framebuf_make_new,
    buffer, framebuf_get_buffer,
    locals_dict, &framebuf_locals_dict
    );


#endif

// this factory function is provided for backwards compatibility with old FrameBuffer1 class
STATIC mp_obj_t legacy_framebuffer1(size_t n_args, const mp_obj_t *args) {
    mp_obj_framebuf_t *o = m_new_obj(mp_obj_framebuf_t);
    o->base.type = &mp_type_framebuf;

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_WRITE);
    o->buf = bufinfo.buf;

    o->width = mp_obj_get_int(args[1]);
    o->height = mp_obj_get_int(args[2]);
    o->format = FRAMEBUF_MON_VLSB;
    if (n_args >= 4) {
        o->stride = mp_obj_get_int(args[3]);
    } else {
        o->stride = o->width;
    }

    return MP_OBJ_FROM_PTR(o);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(legacy_framebuffer1_obj, 3, 4, legacy_framebuffer1);

#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_rom_map_elem_t framebuf_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_framebuf) },
    { MP_ROM_QSTR(MP_QSTR_FrameBuffer), MP_ROM_PTR(&mp_type_framebuf) },
    { MP_ROM_QSTR(MP_QSTR_FrameBuffer1), MP_ROM_PTR(&legacy_framebuffer1_obj) },
    { MP_ROM_QSTR(MP_QSTR_MONO_HLSB),   MP_ROM_INT(FRAMEBUF_MON_HLSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_HMSB),   MP_ROM_INT(FRAMEBUF_MON_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_VLSB),   MP_ROM_INT(FRAMEBUF_MON_VLSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_VMSB),   MP_ROM_INT(FRAMEBUF_MON_VMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS2_HLSB),    MP_ROM_INT(FRAMEBUF_GS2_HLSB) },
    { MP_ROM_QSTR(MP_QSTR_GS2_HMSB),    MP_ROM_INT(FRAMEBUF_GS2_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS2_VLSB),    MP_ROM_INT(FRAMEBUF_GS2_VLSB) },
    { MP_ROM_QSTR(MP_QSTR_GS2_VMSB),    MP_ROM_INT(FRAMEBUF_GS2_VMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_HLSB),    MP_ROM_INT(FRAMEBUF_GS4_HLSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_HMSB),    MP_ROM_INT(FRAMEBUF_GS4_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_VLSB),    MP_ROM_INT(FRAMEBUF_GS4_VLSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_VMSB),    MP_ROM_INT(FRAMEBUF_GS4_VMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS8_H),       MP_ROM_INT(FRAMEBUF_GS8_H) },
    { MP_ROM_QSTR(MP_QSTR_GS8_V),       MP_ROM_INT(FRAMEBUF_GS8_V) },
    { MP_ROM_QSTR(MP_QSTR_RGB565),      MP_ROM_INT(FRAMEBUF_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_RGB565SW),    MP_ROM_INT(FRAMEBUF_RGB565SW) },
    { MP_ROM_QSTR(MP_QSTR_RGB888),      MP_ROM_INT(FRAMEBUF_RGB888) },
    { MP_ROM_QSTR(MP_QSTR_RGB8888),     MP_ROM_INT(FRAMEBUF_RGB8888) },
    { MP_ROM_QSTR(MP_QSTR_ST7302),      MP_ROM_INT(FRAMEBUF_ST7302) },
    { MP_ROM_QSTR(MP_QSTR_MX),          MP_ROM_INT(FRAMEBUF_MX) },
    { MP_ROM_QSTR(MP_QSTR_MY),          MP_ROM_INT(FRAMEBUF_MY) },
    { MP_ROM_QSTR(MP_QSTR_MV),          MP_ROM_INT(FRAMEBUF_MV) },
    
    { MP_ROM_QSTR(MP_QSTR_Font_S12), MP_ROM_INT(Font_S12) },
    { MP_ROM_QSTR(MP_QSTR_Font_C12), MP_ROM_INT(Font_C12) },
    { MP_ROM_QSTR(MP_QSTR_Font_A12), MP_ROM_INT(Font_A12) },
    { MP_ROM_QSTR(MP_QSTR_Font_R12), MP_ROM_INT(Font_R12) },
    
    { MP_ROM_QSTR(MP_QSTR_Font_S16), MP_ROM_INT(Font_S16) },
    { MP_ROM_QSTR(MP_QSTR_Font_C16), MP_ROM_INT(Font_C16) },
    { MP_ROM_QSTR(MP_QSTR_Font_A16), MP_ROM_INT(Font_A16) },
    { MP_ROM_QSTR(MP_QSTR_Font_R16), MP_ROM_INT(Font_R16) },
    
    { MP_ROM_QSTR(MP_QSTR_Font_S24), MP_ROM_INT(Font_S24) },
    { MP_ROM_QSTR(MP_QSTR_Font_C24), MP_ROM_INT(Font_C24) },
    { MP_ROM_QSTR(MP_QSTR_Font_A24), MP_ROM_INT(Font_A24) },
    { MP_ROM_QSTR(MP_QSTR_Font_R24), MP_ROM_INT(Font_R24) },
    
    { MP_ROM_QSTR(MP_QSTR_Font_S32), MP_ROM_INT(Font_S32) },
    { MP_ROM_QSTR(MP_QSTR_Font_C32), MP_ROM_INT(Font_C32) },
    { MP_ROM_QSTR(MP_QSTR_Font_A32), MP_ROM_INT(Font_A32) },
    { MP_ROM_QSTR(MP_QSTR_Font_R32), MP_ROM_INT(Font_R32) }
  
};

STATIC MP_DEFINE_CONST_DICT(framebuf_module_globals, framebuf_module_globals_table);

const mp_obj_module_t mp_module_framebuf = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&framebuf_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_framebuf, mp_module_framebuf);
#endif

#endif // MICROPY_PY_FRAMEBUF
