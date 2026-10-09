#ifndef RASTERFALL_CANVAS_H
#define RASTERFALL_CANVAS_H
#include "core.h"
#include "toy_window.h"
#include "fb_font.h"
#include "fb_draw.h"
/* Presentation layout emits screen primitives, never owns a framebuffer. */
struct rasterfall_canvas {
    int width,height,failed;
    void *context;
    int (*rectangle)(void *,int,int,int,int,uint32_t,int);
    /* Optional accelerated, already clipped screen-space line. */
    int (*line)(void *,int,int,int,int,uint32_t,int);
};
static inline void rasterfall_canvas_rect(struct rasterfall_canvas *c,int x,int y,
    int w,int h,uint32_t color,int alpha)
{
    if (!c || c->failed || w<=0 || h<=0 || alpha<=0) return;
    if (x<0) { w+=x;x=0; } if (y<0) { h+=y;y=0; }
    if (w>c->width-x) w=c->width-x;
    if (h>c->height-y) h=c->height-y;
    if (w>0 && h>0 && c->rectangle(c->context,x,y,w,h,color,alpha)<0) c->failed=1;
}
/* Measurement, wrapping and painting share the same UTF-8 decoder and font
 * advance. A malformed sequence consumes one byte and displays a replacement. */
static inline unsigned rasterfall_canvas_codepoint(const char **cursor)
{
    const unsigned char *s=(const unsigned char *)*cursor;
    unsigned cp=s[0], minimum=0;
    int count=0;
    if (!cp) return 0;
    if (cp<128) { *cursor+=1;return cp; }
    if (cp>=0xc2 && cp<=0xdf) { cp&=31;count=1;minimum=0x80; }
    else if (cp>=0xe0 && cp<=0xef) { cp&=15;count=2;minimum=0x800; }
    else if (cp>=0xf0 && cp<=0xf4) { cp&=7;count=3;minimum=0x10000; }
    else { *cursor+=1;return '?'; }
    for (int i=1;i<=count;++i) {
        if (!s[i] || (s[i]&0xc0)!=0x80) { *cursor+=1;return '?'; }
        cp=(cp<<6)|(s[i]&63);
    }
    if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) {
        *cursor+=1;return '?';
    }
    *cursor+=count+1;
    return cp;
}
static inline int rasterfall_canvas_glyph_width(unsigned cp)
{
    int width=8;
    if (cp=='\t') return 32;
    fb_font_glyph_row_utf8(cp,0,&width);
    return width;
}
static inline int rasterfall_canvas_text_width(const char *text,int scale_milli)
{
    int width=0,max_width=0;
    if (scale_milli<=0) scale_milli=1000;
    while (text && *text) {
        unsigned cp=rasterfall_canvas_codepoint(&text);
        if (cp=='\n') { if (width>max_width) max_width=width;width=0; }
        else if (cp=='\t') width=(width/32+1)*32;
        else width+=rasterfall_canvas_glyph_width(cp);
    }
    if (width>max_width) max_width=width;
    return (max_width*scale_milli+999)/1000;
}
static inline void rasterfall_canvas_glyph(struct rasterfall_canvas *c,int x,int y,
    unsigned cp,uint32_t color,int scale_milli)
{
    int width=8;
    fb_font_glyph_row_utf8(cp,0,&width);
    if (cp>=128 && width==8) cp='?';
    for (int row=0;row<16;++row) {
        unsigned bits=fb_font_glyph_row_utf8(cp,row,&width);
        for (int col=0;col<width;++col) {
            int end=col;
            if (!(bits&(0x8000u>>col))) continue;
            while (end+1<width && (bits&(0x8000u>>(end+1)))) ++end;
            rasterfall_canvas_rect(c,x+col*scale_milli/1000,y+row*scale_milli/1000,
                (end+1)*scale_milli/1000-col*scale_milli/1000,
                (row+1)*scale_milli/1000-row*scale_milli/1000,color,255);
            col=end;
        }
    }
}
static inline void rasterfall_canvas_text_scaled(struct rasterfall_canvas *c,
    int x,int y,const char *text,uint32_t color,int scale_milli)
{
    int advance=0;
    if (scale_milli<=0) scale_milli=1000;
    while (text && *text) {
        unsigned cp=rasterfall_canvas_codepoint(&text);
        if (cp=='\n') { advance=0;y+=16*scale_milli/1000;continue; }
        if (cp=='\t') { advance=(advance/32+1)*32;continue; }
        rasterfall_canvas_glyph(c,x+advance*scale_milli/1000,y,cp,color,scale_milli);
        advance+=rasterfall_canvas_glyph_width(cp);
    }
}
static inline void rasterfall_canvas_text(struct rasterfall_canvas *c,int x,int y,
    const char *text,uint32_t color)
{
    rasterfall_canvas_text_scaled(c,x,y,text,color,1000);
}
static inline void rasterfall_canvas_text_ellipsis(struct rasterfall_canvas *c,
    int x,int y,int max_width,const char *text,uint32_t color,int scale_milli)
{
    int advance=0,ellipsis;
    if (scale_milli<=0) scale_milli=1000;
    if (!text || max_width<=0) return;
    ellipsis=rasterfall_canvas_text_width(text,scale_milli)>max_width;
    for (const char *line=text;*line;++line)
        if (*line=='\n' && line[1]) { ellipsis=1;break; }
    if (ellipsis) max_width-=24*scale_milli/1000;
    while (*text) {
        unsigned cp=rasterfall_canvas_codepoint(&text);
        int width=rasterfall_canvas_glyph_width(cp);
        if (cp=='\n' || (advance+width)*scale_milli/1000>max_width) break;
        rasterfall_canvas_glyph(c,x+advance*scale_milli/1000,y,cp,color,scale_milli);
        advance+=width;
    }
    if (ellipsis && max_width>=0)
        rasterfall_canvas_text_scaled(c,x+advance*scale_milli/1000,y,"...",color,scale_milli);
}
static inline int rasterfall_canvas_text_wrap(struct rasterfall_canvas *c,
    int x,int y,int max_width,int max_lines,const char *text,uint32_t color,int scale_milli)
{
    int line=0;
    if (scale_milli<=0) scale_milli=1000;
    if (max_width<8*scale_milli/1000 || max_lines<=0) return 0;
    while (text && *text && line<max_lines) {
        const char *end=text,*space=0,*scan=text;
        int advance=0;
        if (line==max_lines-1) {
            rasterfall_canvas_text_ellipsis(c,x,y+line*20*scale_milli/1000,
                max_width,text,color,scale_milli);
            return line+1;
        }
        while (*scan && *scan!='\n') {
            const char *before=scan;
            unsigned cp=rasterfall_canvas_codepoint(&scan);
            int width=rasterfall_canvas_glyph_width(cp);
            if ((advance+width)*scale_milli/1000>max_width) break;
            if (cp==' ') space=before;
            advance+=width;end=scan;
        }
        if (*end && *end!='\n' && space && space>text) end=space;
        if (end==text && *text!='\n') break;
        advance=0;
        while (text<end) {
            unsigned cp=rasterfall_canvas_codepoint(&text);
            rasterfall_canvas_glyph(c,x+advance*scale_milli/1000,
                y+line*20*scale_milli/1000,cp,color,scale_milli);
            advance+=rasterfall_canvas_glyph_width(cp);
        }
        if (*text=='\n') ++text;
        else while (*text==' ') ++text;
        ++line;
    }
    return line;
}
static inline int rasterfall_canvas_surface_rect(void *context,int x,int y,int w,int h,
    uint32_t color,int alpha)
{
    struct toy_surface *s=context;
    for (int yy=y;yy<y+h;++yy) for (int xx=x;xx<x+w;++xx)
        if (alpha==255) fb_put_pixel((unsigned char *)s->pixels,xx,yy,color,s->stride);
        else fb_put_pixel_alpha((unsigned char *)s->pixels,xx,yy,color,(unsigned)alpha,s->stride);
    return 0;
}
static inline struct rasterfall_canvas rasterfall_canvas_surface(struct toy_surface *s)
{
    struct rasterfall_canvas c={s->width,s->height,0,s,rasterfall_canvas_surface_rect,NULL};
    return c;
}
#endif
