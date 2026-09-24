#ifndef RASTERFALL_CANVAS_H
#define RASTERFALL_CANVAS_H
#include "core.h"
#include "toy_window.h"
#include "fb_font.h"
#include "fb_draw.h"
/* Presentation layout emits rectangles, never owns a framebuffer. */
struct rasterfall_canvas {
    int width,height,failed;
    void *context;
    int (*rectangle)(void *,int,int,int,int,uint32_t,int);
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
static inline void rasterfall_canvas_text(struct rasterfall_canvas *c,int x,int y,
    const char *text,uint32_t color)
{
    int origin=x;
    const unsigned char *s=(const unsigned char *)text;
    while (s && *s) {
        unsigned cp=*s++;int width=8,continuations=0;
        if (cp=='\n') { x=origin;y+=16;continue; }
        if (cp=='\t') { x=origin+((x-origin)/32+1)*32;continue; }
        if (cp>=0xc2 && cp<=0xdf) { cp&=31;continuations=1; }
        else if (cp>=0xe0 && cp<=0xef) { cp&=15;continuations=2; }
        else if (cp>=0xf0 && cp<=0xf4) { cp&=7;continuations=3; }
        else if (cp>=128) cp='?';
        while (continuations--) {
            if ((*s&0xc0)!=0x80) { cp='?';break; }
            cp=(cp<<6)|(*s++&63);
        }
        for (int row=0;row<16;++row) {
            unsigned bits=fb_font_glyph_row_utf8(cp,row,&width);
            for (int col=0;col<width;++col) {
                int end=col;
                if (!(bits&(0x8000u>>col))) continue;
                while (end+1<width && (bits&(0x8000u>>(end+1)))) ++end;
                rasterfall_canvas_rect(c,x+col,y+row,end-col+1,1,color,255);
                col=end;
            }
        }
        x+=width;
    }
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
    struct rasterfall_canvas c={s->width,s->height,0,s,rasterfall_canvas_surface_rect};
    return c;
}
#endif
