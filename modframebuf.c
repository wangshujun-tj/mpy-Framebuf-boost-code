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
#include "py/stream.h"
#include "py/reader.h"
#include "extmod/vfs.h"

#if MICROPY_PY_FRAMEBUF

#include "extmod/font_asc.h"


typedef struct _font_set_t{
  uint8_t f_style;
  uint8_t rotate;		//xuanzhuan
  uint8_t scale;		//fangda 16jinzhi ,gaowei hengxiang  diweizongxiang
  uint8_t inverse;      //fanbai
  uint16_t bg_col;
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
typedef void (*fill_rect_t)(const mp_obj_framebuf_t *, int, int, int, int, uint32_t);

typedef struct _mp_framebuf_p_t {
    setpixel_t setpixel;
    getpixel_t getpixel;
    fill_rect_t fill_rect;
} mp_framebuf_p_t;

// constants for formats
#define FRAMEBUF_MVLSB    (0)
#define FRAMEBUF_RGB565   (1)
#define FRAMEBUF_GS2_HMSB (5)
#define FRAMEBUF_GS4_HMSB (2)
#define FRAMEBUF_GS8      (6)
#define FRAMEBUF_MHLSB    (3)
#define FRAMEBUF_MHMSB    (4)
#define FRAMEBUF_RGB565SW (7)

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


STATIC mp_obj_t framebuf_font_load(mp_obj_t self_in, mp_obj_t name_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    const char *filename = mp_obj_str_get_str(name_in);
    //mp_printf(&mp_plat_print,"%s\n\r",filename);
    mp_obj_t f_args[2] = {
        mp_obj_new_str(filename, strlen(filename)),
        MP_OBJ_NEW_QSTR(MP_QSTR_rb),
    };
    self->font_file = mp_vfs_open(MP_ARRAY_SIZE(f_args), &f_args[0], (mp_map_t *)&mp_const_empty_map);
    int errcode;
    //mp_stream_rw(self->font_file , &self->font_inf, sizeof(font_inf_t), &errcode, MP_STREAM_RW_READ);
    const mp_obj_t s_args[3] = {self->font_file , MP_OBJ_NEW_SMALL_INT(32) , MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
    stream_seek( 3, s_args);
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
    mp_printf(&mp_plat_print,"font file close \n\r");
    if (self->font_file!= NULL){
        mp_stream_close(self->font_file);
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



STATIC void mono_horiz_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    size_t index = (x + y * fb->stride) >> 3;
    int offset = fb->format == FRAMEBUF_MHMSB ? x & 0x07 : 7 - (x & 0x07);
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x01 << offset)) | ((col != 0) << offset);
}

STATIC uint32_t mono_horiz_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    size_t index = (x + y * fb->stride) >> 3;
    int offset = fb->format == FRAMEBUF_MHMSB ? x & 0x07 : 7 - (x & 0x07);
    return (((uint8_t *)fb->buf)[index] >> (offset)) & 0x01;
}

STATIC void mono_horiz_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    int reverse = fb->format == FRAMEBUF_MHMSB;
    int advance = fb->stride >> 3;
    while (w--) {
        uint8_t *b = &((uint8_t *)fb->buf)[(x >> 3) + y * advance];
        int offset = reverse ?  x & 7 : 7 - (x & 7);
        for (int hh = h; hh; --hh) {
            *b = (*b & ~(0x01 << offset)) | ((col != 0) << offset);
            b += advance;
        }
        ++x;
    }
}

// Functions for MVLSB format

STATIC void mvlsb_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    size_t index = (y >> 3) * fb->stride + x;
    uint8_t offset = y & 0x07;
    ((uint8_t *)fb->buf)[index] = (((uint8_t *)fb->buf)[index] & ~(0x01 << offset)) | ((col != 0) << offset);
}

STATIC uint32_t mvlsb_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    return (((uint8_t *)fb->buf)[(y >> 3) * fb->stride + x] >> (y & 0x07)) & 0x01;
}

STATIC void mvlsb_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    while (h--) {
        uint8_t *b = &((uint8_t *)fb->buf)[(y >> 3) * fb->stride + x];
        uint8_t offset = y & 0x07;
        for (int ww = w; ww; --ww) {
            *b = (*b & ~(0x01 << offset)) | ((col != 0) << offset);
            ++b;
        }
        ++y;
    }
}

// Functions for RGB565 format

STATIC void rgb565_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    ((uint16_t *)fb->buf)[x + y * fb->stride] = col;
}
STATIC void rgb565sw_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    uint32_t swap_byte;
    swap_byte=(col>>8)&0xff;
    swap_byte|=(col&0xff)<<8;
    ((uint16_t *)fb->buf)[x + y * fb->stride] = swap_byte;
}



STATIC uint32_t rgb565_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    return ((uint16_t *)fb->buf)[x + y * fb->stride];
}
STATIC uint32_t rgb565sw_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    uint32_t swap_byte,col;
    col=((uint16_t *)fb->buf)[x + y * fb->stride];
    swap_byte=(col>>8)&0xff;
    swap_byte|=(col&0xff)<<8;
    return swap_byte;
}

STATIC void rgb565_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    uint16_t *b = &((uint16_t *)fb->buf)[x + y * fb->stride];
    while (h--) {
        for (int ww = w; ww; --ww) {
            *b++ = col;
        }
        b += fb->stride - w;
    }
}

STATIC void rgb565sw_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    uint32_t swap_byte;
    swap_byte=(col>>8)&0xff;
    swap_byte|=(col&0xff)<<8;
    uint16_t *b = &((uint16_t *)fb->buf)[x + y * fb->stride];
    while (h--) {
        for (int ww = w; ww; --ww) {
            *b++ = swap_byte;
        }
        b += fb->stride - w;
    }
}


// Functions for GS2_HMSB format

STATIC void gs2_hmsb_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 2];
    uint8_t shift = (x & 0x3) << 1;
    uint8_t mask = 0x3 << shift;
    uint8_t color = (col & 0x3) << shift;
    *pixel = color | (*pixel & (~mask));
}

STATIC uint32_t gs2_hmsb_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    uint8_t pixel = ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 2];
    uint8_t shift = (x & 0x3) << 1;
    return (pixel >> shift) & 0x3;
}

STATIC void gs2_hmsb_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    for (int xx = x; xx < x + w; xx++) {
        for (int yy = y; yy < y + h; yy++) {
            gs2_hmsb_setpixel(fb, xx, yy, col);
        }
    }
}

// Functions for GS4_HMSB format

STATIC void gs4_hmsb_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1];

    if (x % 2) {
        *pixel = ((uint8_t)col & 0x0f) | (*pixel & 0xf0);
    } else {
        *pixel = ((uint8_t)col << 4) | (*pixel & 0x0f);
    }
}

STATIC uint32_t gs4_hmsb_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    if (x % 2) {
        return ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1] & 0x0f;
    }

    return ((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1] >> 4;
}

STATIC void gs4_hmsb_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    col &= 0x0f;
    uint8_t *pixel_pair = &((uint8_t *)fb->buf)[(x + y * fb->stride) >> 1];
    uint8_t col_shifted_left = col << 4;
    uint8_t col_pixel_pair = col_shifted_left | col;
    int pixel_count_till_next_line = (fb->stride - w) >> 1;
    bool odd_x = (x % 2 == 1);

    while (h--) {
        int ww = w;

        if (odd_x && ww > 0) {
            *pixel_pair = (*pixel_pair & 0xf0) | col;
            pixel_pair++;
            ww--;
        }

        memset(pixel_pair, col_pixel_pair, ww >> 1);
        pixel_pair += ww >> 1;

        if (ww % 2) {
            *pixel_pair = col_shifted_left | (*pixel_pair & 0x0f);
            if (!odd_x) {
                pixel_pair++;
            }
        }

        pixel_pair += pixel_count_till_next_line;
    }
}

// Functions for GS8 format

STATIC void gs8_setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride)];
    *pixel = col & 0xff;
}

STATIC uint32_t gs8_getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    return ((uint8_t *)fb->buf)[(x + y * fb->stride)];
}

STATIC void gs8_fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    uint8_t *pixel = &((uint8_t *)fb->buf)[(x + y * fb->stride)];
    while (h--) {
        memset(pixel, col, w);
        pixel += fb->stride;
    }
}

STATIC mp_framebuf_p_t formats[] = {
    [FRAMEBUF_MVLSB] = {mvlsb_setpixel, mvlsb_getpixel, mvlsb_fill_rect},
    [FRAMEBUF_RGB565] = {rgb565_setpixel, rgb565_getpixel, rgb565_fill_rect},
    [FRAMEBUF_RGB565SW] = {rgb565sw_setpixel, rgb565sw_getpixel, rgb565sw_fill_rect},
    [FRAMEBUF_GS2_HMSB] = {gs2_hmsb_setpixel, gs2_hmsb_getpixel, gs2_hmsb_fill_rect},
    [FRAMEBUF_GS4_HMSB] = {gs4_hmsb_setpixel, gs4_hmsb_getpixel, gs4_hmsb_fill_rect},
    [FRAMEBUF_GS8] = {gs8_setpixel, gs8_getpixel, gs8_fill_rect},
    [FRAMEBUF_MHLSB] = {mono_horiz_setpixel, mono_horiz_getpixel, mono_horiz_fill_rect},
    [FRAMEBUF_MHMSB] = {mono_horiz_setpixel, mono_horiz_getpixel, mono_horiz_fill_rect},
};

static inline void setpixel(const mp_obj_framebuf_t *fb, int x, int y, uint32_t col) {
    if (x>=0 && x<fb->width && y>=0 && y<fb->height)
        formats[fb->format].setpixel(fb, x, y, col);
}


static inline uint32_t getpixel(const mp_obj_framebuf_t *fb, int x, int y) {
    return formats[fb->format].getpixel(fb, x, y);
}

STATIC void fill_rect(const mp_obj_framebuf_t *fb, int x, int y, int w, int h, uint32_t col) {
    if (h < 1 || w < 1 || x + w <= 0 || y + h <= 0 || y >= fb->height || x >= fb->width) {
        // No operation needed.
        return;
    }

    // clip to the framebuffer
    int xend = MIN(fb->width, x + w);
    int yend = MIN(fb->height, y + h);
    x = MAX(x, 0);
    y = MAX(y, 0);

    formats[fb->format].fill_rect(fb, x, y, xend - x, yend - y, col);
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
    if (n_args >= 5) {
        o->stride = mp_obj_get_int(args[4]);
    } else {
        o->stride = o->width;
    }
    //font style seting
    o->font_set.f_style=0x11;
    o->font_set.scale=1;   
    o->font_set.rotate=0;     
    o->font_set.inverse=0;  
    o->font_file=NULL;
    o->font_inf.Font_Type=0;
    o->font_inf.Base_Addr12=0;		//xuanzhuan 0,12dot font no exist
    o->font_inf.Base_Addr16=0;  	//xuanzhuan 0,16dot font no exist
    o->font_inf.Base_Addr24=0;		//xuanzhuan 0,24dot font no exist
    o->font_inf.Base_Addr32=0;		//xuanzhuan 0,32dot font no exist
       
    switch (o->format) {
        case FRAMEBUF_MVLSB:
        case FRAMEBUF_RGB565:
        case FRAMEBUF_RGB565SW:
            break;
        case FRAMEBUF_MHLSB:
        case FRAMEBUF_MHMSB:
            o->stride = (o->stride + 7) & ~7;
            break;
        case FRAMEBUF_GS2_HMSB:
            o->stride = (o->stride + 3) & ~3;
            break;
        case FRAMEBUF_GS4_HMSB:
            o->stride = (o->stride + 1) & ~1;
            break;
        case FRAMEBUF_GS8:
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid format"));
    }

    return MP_OBJ_FROM_PTR(o);
}

STATIC mp_int_t framebuf_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    (void)flags;
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    bufinfo->buf = self->buf;
    bufinfo->len = self->stride * self->height * (self->format == FRAMEBUF_RGB565 ? 2 : 1);
    bufinfo->typecode = 'B'; // view framebuf as bytes
    return 0;
}

STATIC mp_obj_t framebuf_fill(mp_obj_t self_in, mp_obj_t col_in) {
    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t col = mp_obj_get_int(col_in);
    formats[self->format].fill_rect(self, 0, 0, self->width, self->height, col);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(framebuf_fill_obj, framebuf_fill);

STATIC mp_obj_t framebuf_fill_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t width = mp_obj_get_int(args[3]);
    mp_int_t height = mp_obj_get_int(args[4]);
    mp_int_t col = mp_obj_get_int(args[5]);

    fill_rect(self, x, y, width, height, col);

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

STATIC mp_obj_t framebuf_hline(size_t n_args, const mp_obj_t *args) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t w = mp_obj_get_int(args[3]);
    mp_int_t col = mp_obj_get_int(args[4]);

    fill_rect(self, x, y, w, 1, col);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_hline_obj, 5, 5, framebuf_hline);

STATIC mp_obj_t framebuf_vline(size_t n_args, const mp_obj_t *args) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t h = mp_obj_get_int(args[3]);
    mp_int_t col = mp_obj_get_int(args[4]);

    fill_rect(self, x, y, 1, h, col);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_vline_obj, 5, 5, framebuf_vline);

STATIC mp_obj_t framebuf_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t w = mp_obj_get_int(args[3]);
    mp_int_t h = mp_obj_get_int(args[4]);
    mp_int_t col = mp_obj_get_int(args[5]);

    fill_rect(self, x, y, w, 1, col);
    fill_rect(self, x, y + h - 1, w, 1, col);
    fill_rect(self, x, y, 1, h, col);
    fill_rect(self, x + w - 1, y, 1, h, col);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_rect_obj, 6, 6, framebuf_rect);

STATIC mp_obj_t framebuf_line(size_t n_args, const mp_obj_t *args) {
    (void)n_args;

    mp_obj_framebuf_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x1 = mp_obj_get_int(args[1]);
    mp_int_t y1 = mp_obj_get_int(args[2]);
    mp_int_t x2 = mp_obj_get_int(args[3]);
    mp_int_t y2 = mp_obj_get_int(args[4]);
    mp_int_t col = mp_obj_get_int(args[5]);

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
            if (0 <= y1 && y1 < self->width && 0 <= x1 && x1 < self->height) {
                setpixel(self, y1, x1, col);
            }
        } else {
            if (0 <= x1 && x1 < self->width && 0 <= y1 && y1 < self->height) {
                setpixel(self, x1, y1, col);
            }
        }
        while (e >= 0) {
            y1 += sy;
            e -= 2 * dx;
        }
        x1 += sx;
        e += 2 * dy;
    }

    if (0 <= x2 && x2 < self->width && 0 <= y2 && y2 < self->height) {
        setpixel(self, x2, y2, col);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_line_obj, 6, 6, framebuf_line);

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
            if (col != (uint32_t)key) {
                setpixel(self, cx0, y0, col);
            }
            ++cx1;
        }
        ++y1;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_blit_obj, 4, 5, framebuf_blit);

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
        //mp_printf(&mp_plat_print,"%8.8X",code_index);
        const mp_obj_t gb_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(code_index *2+0x100),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
        stream_seek( 3, gb_args);
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
                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index*24+self->font_inf.Base_Addr12),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, font_args);
                int len=mp_stream_rw(self->font_file ,chr_data, 24 , &errcode, MP_STREAM_RW_READ);
                if (len!=24 && errcode!=0) return 5;
            }else { return 6; }
            break;
        case 0x2:
            if (self->font_inf.Base_Addr16>0){
                * font_width=16;
                * font_high=16;
                * font_stride=2;
                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index*32+self->font_inf.Base_Addr16),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, font_args);
                int len=mp_stream_rw(self->font_file ,chr_data, 32 , &errcode, MP_STREAM_RW_READ);
                if (len!=32 && errcode!=0) return 5;
            }else { return 6; }
            break;
        case 0x3:
            if (self->font_inf.Base_Addr24>0){
                * font_width=24;
                * font_high=24;
                * font_stride=3;
                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index*72+self->font_inf.Base_Addr24),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, font_args);
                int len=mp_stream_rw(self->font_file ,chr_data, 72 , &errcode, MP_STREAM_RW_READ);
                if (len!=72 && errcode!=0) return 5;
            }else { return 6; }
            break;
        case 0x4:
            if (self->font_inf.Base_Addr32>0){
                * font_width=32;
                * font_high=32;
                * font_stride=4;
                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index*128+self->font_inf.Base_Addr32),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, font_args);
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
                const mp_obj_t count_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(68),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, count_args);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                //mp_printf(&mp_plat_print,"%d--",font_count);
                uint32_t font_index[font_count][2];
                const mp_obj_t index_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(self->font_inf.Base_Addr12),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, index_args);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                        if (unicode==font_index[find][0]){
                                //mp_printf(&mp_plat_print,"%d,%x\r\n",font_index[find][0],font_index[find][1]);
                                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index[find][1]),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                                stream_seek( 3, font_args);
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
                const mp_obj_t count_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(72),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, count_args);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                uint32_t font_index[font_count][2];
                const mp_obj_t index_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(self->font_inf.Base_Addr16),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, index_args);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                        if (unicode==font_index[find][0]){
                                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index[find][1]),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                                stream_seek( 3, font_args);
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
                const mp_obj_t count_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(72),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, count_args);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                uint32_t font_index[font_count][2];
                const mp_obj_t index_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(self->font_inf.Base_Addr24),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, index_args);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                        if (unicode==font_index[find][0]){
                                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index[find][1]),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                                stream_seek( 3, font_args);
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
                const mp_obj_t count_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(76),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, count_args);
                int len=mp_stream_rw(self->font_file ,&font_count, 4 , &errcode, MP_STREAM_RW_READ);
                if (len!=4 && errcode!=0) return 5;
                if    (font_count>1024 || font_count==0)   return 5;
                uint32_t font_index[font_count][2];
                const mp_obj_t index_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(self->font_inf.Base_Addr32),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                stream_seek( 3, index_args);
                len=mp_stream_rw(self->font_file ,&font_index, font_count*8 , &errcode, MP_STREAM_RW_READ);
                for(int find=0;find<font_count;find++){
                        if (unicode==font_index[find][0]){
                                const mp_obj_t font_args[3]={self->font_file,MP_OBJ_NEW_SMALL_INT(font_index[find][1]),MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
                                stream_seek( 3, font_args);
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
    int dot_col=0;
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
            ret=gethzk(self,unicode,chr_data,&font_width,&font_high,&font_stride);
        }
        // loop over char data
        
        if (ret==0){
            
            mask=0x00000001<<(font_stride*8-1);
            if (((self->font_set.rotate==0 ||self->font_set.rotate==2)&&(x0 + font_width*self->font_set.scale <= self->width )&&( y0 + font_high*self->font_set.scale <= self->height)) || 
            ((self->font_set.rotate==1 ||self->font_set.rotate==3)&&(x0 + font_high*self->font_set.scale <= self->width )&&( y0 + font_width*self->font_set.scale <= self->height))){
                for (int y = 0; y < font_high; y++){
                    uint vline_data=0;
                    for(int k=0;k<font_stride;k++){
                        vline_data |= chr_data[y*font_stride+k]<<((font_stride-k-1)*8); // each byte 
                    }
                    for (int x=0; x<font_width; x++) { // scan over vertical column
                        if (((vline_data & (mask>>x))&&self->font_set.inverse==0) ||
                        ((~vline_data & (mask>>x))&&self->font_set.inverse==1)){ 
                        // only draw if pixel set
                            dot_col=col;
                        }else{
                            dot_col=self->font_set.bg_col;
                        }
                        for (int x_scale=0;x_scale<self->font_set.scale;x_scale++){
                            for (int y_scale=0;y_scale<self->font_set.scale;y_scale++){
                                switch(self->font_set.rotate){
                                case 0:
                                    setpixel(self, x0+x*self->font_set.scale+x_scale, y0+y*self->font_set.scale+y_scale, dot_col);
                                    break;
                                case 1:
                                    setpixel(self, x0+font_high*self->font_set.scale-y*self->font_set.scale-y_scale, y0+x*self->font_set.scale+x_scale, dot_col);
                                    break;
                                case 2:
                                    setpixel(self, x0+font_width*self->font_set.scale-x*self->font_set.scale-x_scale, y0+font_high*self->font_set.scale-y*self->font_set.scale-y_scale, dot_col);
                                    break;
                                case 3:
                                    setpixel(self, x0+y*self->font_set.scale-y_scale, y0+font_width*self->font_set.scale-x*self->font_set.scale-x_scale, dot_col);
                                    break;
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
    uint16_t biComp;
    uint16_t bfReserved3;
    uint32_t biPSize;
    
} BITMAPFILEHEADER;     

STATIC mp_obj_t framebuf_show_bmp(size_t n_args, const mp_obj_t *args) {
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
        w = MIN(mp_obj_get_int(args[4]),w);
        h = MIN(mp_obj_get_int(args[5]),h);
    }
    mp_obj_t f_args[2] = {
        mp_obj_new_str(filename, strlen(filename)),
        MP_OBJ_NEW_QSTR(MP_QSTR_rb),
    }; 
    //mp_printf(&mp_plat_print,"open file. \r\n");
    mp_obj_t bmp_file = mp_vfs_open(MP_ARRAY_SIZE(f_args), &f_args[0], (mp_map_t *)&mp_const_empty_map);
    int errcode;
    //mp_printf(&mp_plat_print,"write head. \r\n");
    
    int len=mp_stream_rw(bmp_file ,&bmp_h, sizeof(BITMAPFILEHEADER), &errcode, MP_STREAM_OP_READ);
    if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
        mp_raise_OSError(errcode);
        mp_printf(&mp_plat_print,"Write %s error!\r\n",filename);
    }
    //mp_printf(&mp_plat_print,"%d,%d,%d,%d. \r\n",x0,y0,w,h);
    //memset(&bmp_h,0,sizeof(BITMAPFILEHEADER));   
    if (bmp_h.bfType!=0x4d42){
        mp_printf(&mp_plat_print,"File %s not BMP.\r\n",filename);
        return mp_const_none;
    }
    if (self->format==FRAMEBUF_RGB565 || self->format==FRAMEBUF_RGB565SW){
        if (bmp_h.biBitcount!=0x18){
            mp_printf(&mp_plat_print,"File %s color no match.\r\n",filename);
            return mp_const_none;
        }
    }else if(self->format==FRAMEBUF_MVLSB || self->format==FRAMEBUF_MHLSB || self->format==FRAMEBUF_MHMSB){
        if (bmp_h.biBitcount!=0x01){
            mp_printf(&mp_plat_print,"File %s color no match.\r\n",filename);
            return mp_const_none;
        }
    }else {
        mp_printf(&mp_plat_print,"Unsupported format. \r\n");
        return mp_const_none;
    }
    //mp_printf(&mp_plat_print,"x=%d,y=%d,w=%d,h=%d\r\n",x0,y0,w,h);
    if (x0<0 && abs(x0)>bmp_h.biWidth) x0=-bmp_h.biWidth;
    if (y0<0 && abs(y0)>bmp_h.biHeight) y0=-bmp_h.biHeight;
    if (x0>self->width) x0=self->width;
    if (y0>self->height) y0=self->height;
    if (w>self->width-x0) w=self->width-x0;    
    if (h>self->height-y0) h=self->height-y0;   

    //mp_printf(&mp_plat_print,"x=%d,y=%d,w=%d,h=%d\r\n",x0,y0,w,h);
    
    w=MIN(bmp_h.biWidth,w);
    h=MIN(bmp_h.biHeight,h);
    //mp_printf(&mp_plat_print,"x=%d,y=%d,w=%d,h=%d\r\n",x0,y0,w,h);
    uint32_t stride=(bmp_h.biWidth+7)/8*8;
    const mp_obj_t s_args[3] = {bmp_file , MP_OBJ_NEW_SMALL_INT(bmp_h.bfOffBits) , MP_OBJ_NEW_SMALL_INT(SEEK_SET)};
    stream_seek( 3, s_args);
    //mp_printf(&mp_plat_print,"write pixel. \r\n");
    if (self->format==FRAMEBUF_RGB565 || self->format==FRAMEBUF_RGB565SW ){
        uint8_t line_buf[bmp_h.biWidth][3];
        int32_t hh,ww;
        uint16_t dot_col;
        for(hh=bmp_h.biHeight;hh;hh--){
            len=mp_stream_rw(bmp_file ,&line_buf, bmp_h.biWidth*3, &errcode, MP_STREAM_OP_READ);
            for(ww=bmp_h.biWidth;ww;ww--){
                if (ww<=w && hh<h){
                    dot_col=line_buf[ww-1][0]>>3;
                    dot_col|=(line_buf[ww-1][1]&0xfc)<<5;
                    dot_col|=(line_buf[ww-1][2]&0xf8)<<8;
                    setpixel(self, x0+ww-1, y0+hh-1,dot_col);
                }
            }
        }
    }else if(self->format==FRAMEBUF_MVLSB || self->format==FRAMEBUF_MHLSB || self->format==FRAMEBUF_MHMSB){
        uint8_t line_buf[stride/8];
        int32_t hh,ww;
        for(hh=bmp_h.biHeight;hh;--hh){
            len=mp_stream_rw(bmp_file ,&line_buf, stride/8, &errcode, MP_STREAM_OP_READ);
            for(ww=bmp_h.biWidth;ww;--ww){
                if (ww<=w && hh<h){
                    if ((line_buf[ww/8]&(0x80>>(ww%8)))==0){
                        setpixel(self, x0+ww-1, y0+hh-1,0);
                    }else{
                        setpixel(self, x0+ww-1, y0+hh-1,1);
                    }
                }
            }
        }
    }
    //mp_printf(&mp_plat_print,"close file. \r\n");
    mp_stream_close(bmp_file);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_show_bmp_obj, 2, 6, framebuf_show_bmp);


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
    uint32_t stride=(w+7)/8*8;
    //mp_printf(&mp_plat_print,"%d,%d,%d,%d. \r\n",x0,y0,w,h);
    memset(&bmp_h,0,sizeof(BITMAPFILEHEADER));   
    bmp_h.bfType=0x4d42;
    if (self->format==FRAMEBUF_RGB565 || self->format==FRAMEBUF_RGB565SW){
        bmp_h.bfOffBits=0x36;//dian zhen cun chu pian yi
        bmp_h.biBitcount=0x18;
        bmp_h.bfSize=56+w*h*3;   //wen jian zong chi cun
        bmp_h.biPSize=w*h*3;
    }else if(self->format==FRAMEBUF_MVLSB || self->format==FRAMEBUF_MHLSB || self->format==FRAMEBUF_MHMSB){
        bmp_h.bfOffBits=0x3e;//dian zhen cun chu pian yi
        bmp_h.biBitcount=0x01;
        bmp_h.bfSize=62+stride/8*h;   //wen jian zong chi cun
        bmp_h.biPSize=stride/8*h;
    }else {
        mp_printf(&mp_plat_print,"Unsupported format. \r\n");
        return mp_const_none;
    }
    bmp_h.biComp=0x00;
    bmp_h.biHSize=0x28;
    bmp_h.biWidth=w;
    bmp_h.biHeight=h;
    bmp_h.biPlanes=1;
    
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
    uint8_t buf[bmp_h.bfOffBits-sizeof(BITMAPFILEHEADER)];
    memset(&buf,0,bmp_h.bfOffBits-sizeof(BITMAPFILEHEADER)); 
    buf[bmp_h.bfOffBits-sizeof(BITMAPFILEHEADER)-2]=0xff;
    buf[bmp_h.bfOffBits-sizeof(BITMAPFILEHEADER)-3]=0xff;
    buf[bmp_h.bfOffBits-sizeof(BITMAPFILEHEADER)-4]=0xff;
    
    len=mp_stream_rw(bmp_file ,&buf, bmp_h.bfOffBits-sizeof(BITMAPFILEHEADER), &errcode, MP_STREAM_OP_WRITE);
    if (errcode != 0 && len!=sizeof(BITMAPFILEHEADER)) {
        mp_raise_OSError(errcode);
        mp_printf(&mp_plat_print,"Write %s error!\r\n",filename);
    }
    //mp_printf(&mp_plat_print,"write pixel. \r\n");
    if (self->format==FRAMEBUF_RGB565 || self->format==FRAMEBUF_RGB565SW ){
        uint8_t line_buf[w][3];
        uint32_t hh,ww;
        uint16_t dot_col;
        for(hh=h;hh;hh--){
            for(ww=w;ww;ww--){
                dot_col=getpixel(self, x0+ww-1, y0+hh-1);
                //mp_printf(&mp_plat_print,"%4.4x,",dot_col);
                line_buf[ww-1][2]=(dot_col&0xf800)>>8;
                line_buf[ww-1][1]=(dot_col&0x07e0)>>3;
                line_buf[ww-1][0]=(dot_col&0x001e)<<3;
            }
            len=mp_stream_rw(bmp_file ,&line_buf, w*3, &errcode, MP_STREAM_OP_WRITE);
        }
    }else if(self->format==FRAMEBUF_MVLSB || self->format==FRAMEBUF_MHLSB || self->format==FRAMEBUF_MHMSB){
        uint8_t line_buf[stride/8];
        uint32_t hh,ww;
        for(hh=h;hh;--hh){
            memset(&line_buf,0,stride/8);
            for(ww=w;ww;--ww){
                if (getpixel(self, x0+ww-1, y0+hh-1)==0)
                    line_buf[(ww-1)/8]|=(0x80)>>((ww-1)%8);
            }
            len=mp_stream_rw(bmp_file ,&line_buf, stride/8, &errcode, MP_STREAM_OP_WRITE);
        }
    }
    //mp_printf(&mp_plat_print,"close file. \r\n");
    mp_stream_close(bmp_file);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(framebuf_save_bmp_obj, 2, 6, framebuf_save_bmp);

#if !MICROPY_ENABLE_DYNRUNTIME
STATIC const mp_rom_map_elem_t framebuf_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_show_bmp), MP_ROM_PTR(&framebuf_show_bmp_obj) },
    { MP_ROM_QSTR(MP_QSTR_save_bmp), MP_ROM_PTR(&framebuf_save_bmp_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_load), MP_ROM_PTR(&framebuf_font_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_free), MP_ROM_PTR(&framebuf_font_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_set), MP_ROM_PTR(&framebuf_font_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&framebuf_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect), MP_ROM_PTR(&framebuf_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&framebuf_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_hline), MP_ROM_PTR(&framebuf_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_vline), MP_ROM_PTR(&framebuf_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&framebuf_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&framebuf_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_blit), MP_ROM_PTR(&framebuf_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll), MP_ROM_PTR(&framebuf_scroll_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&framebuf_text_obj) },
};
STATIC MP_DEFINE_CONST_DICT(framebuf_locals_dict, framebuf_locals_dict_table);

STATIC const mp_obj_type_t mp_type_framebuf = {
    { &mp_type_type },
    .name = MP_QSTR_FrameBuffer,
    .make_new = framebuf_make_new,
    .buffer_p = { .get_buffer = framebuf_get_buffer },
    .locals_dict = (mp_obj_dict_t *)&framebuf_locals_dict,
};
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
    o->format = FRAMEBUF_MVLSB;
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
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_framebuf) },
    { MP_ROM_QSTR(MP_QSTR_FrameBuffer), MP_ROM_PTR(&mp_type_framebuf) },
    { MP_ROM_QSTR(MP_QSTR_FrameBuffer1), MP_ROM_PTR(&legacy_framebuffer1_obj) },
    { MP_ROM_QSTR(MP_QSTR_MVLSB), MP_ROM_INT(FRAMEBUF_MVLSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_VLSB), MP_ROM_INT(FRAMEBUF_MVLSB) },
    { MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(FRAMEBUF_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_RGB565SW), MP_ROM_INT(FRAMEBUF_RGB565SW) },
    { MP_ROM_QSTR(MP_QSTR_GS2_HMSB), MP_ROM_INT(FRAMEBUF_GS2_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_HMSB), MP_ROM_INT(FRAMEBUF_GS4_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS8), MP_ROM_INT(FRAMEBUF_GS8) },
    { MP_ROM_QSTR(MP_QSTR_MONO_HLSB), MP_ROM_INT(FRAMEBUF_MHLSB) },
    { MP_ROM_QSTR(MP_QSTR_MONO_HMSB), MP_ROM_INT(FRAMEBUF_MHMSB) },
    
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

#endif

#endif // MICROPY_PY_FRAMEBUF
