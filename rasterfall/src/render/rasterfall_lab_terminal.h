#ifndef RASTERFALL_LAB_TERMINAL_H
#define RASTERFALL_LAB_TERMINAL_H
#include "rasterfall_text_panel.h"

/* Procedural terminal model: projector plinth, floating frame, category icon,
 * and replaceable glyph geometry. Shared CPU/Scene, no texture or asset I/O. */
typedef int (*rf_terminal_quad)(void *, const int [4][3], unsigned);
struct rf_terminal_emit_context {
    void *context;
    rf_terminal_quad quad;
    int z;
    unsigned color;
};
static int rf_terminal_rect(struct rf_terminal_emit_context *c,
    int x0,int x1,int y0,int y1,unsigned color)
{
    int p[4][3]={{x0,y1,c->z},{x1,y1,c->z},{x1,y0,c->z},{x0,y0,c->z}};
    return c->quad(c->context,p,color);
}
static int rf_terminal_glyph(void *context,int x0,int x1,int y0,int y1,int glyph)
{
    struct rf_terminal_emit_context *c=context;
    /* Gaps remain empty: the screen is a projection, not a solid sign plane. */
    return glyph ? rf_terminal_rect(c,x0,x1,y0,y1,c->color) : 0;
}
static int rf_lab_terminal_emit(const struct toy_map_draw *d,
    rf_terminal_quad quad,void *context)
{
    int x=(d->a+d->b)/2,z=(d->c+d->d)/2;
    int large=d->b-d->a>=2400;
    int rail=large ? 24 : 12;
    struct rf_terminal_emit_context c={context,quad,z,d->color};
    if (d->b<=d->a || d->f<=d->e) return -1;
    /* Low projector housing, visible from either side. */
    int bx0=x-(large?360:180),bx1=x+(large?360:180);
    int by0=large ? -900 : d->e-110,by1=by0+(large?140:70);
    int p[4][3]={{bx0,by1,z-90},{bx1,by1,z-90},{bx1,by1,z+90},{bx0,by1,z+90}};
    if (quad(context,p,0x263747)<0) return -1;
    for (int side=0;side<2;++side) {
        c.z=z+(side?90:-90);
        if (rf_terminal_rect(&c,bx0,bx1,by0,by1,0x263747)<0) return -1;
    }
    c.z=z;
    if (rf_terminal_rect(&c,bx0,bx1,by1-12,by1,d->color)<0 ||
        rf_terminal_rect(&c,d->a,d->b,d->e,d->e+rail,d->color)<0 ||
        rf_terminal_rect(&c,d->a,d->b,d->f-rail,d->f,d->color)<0 ||
        rf_terminal_rect(&c,d->a,d->a+rail,d->e,d->f,d->color)<0 ||
        rf_terminal_rect(&c,d->b-rail,d->b,d->e,d->f,d->color)<0) return -1;
    int icon=large ? 240 : 90,ix=d->a+rail*3,iy=(d->e+d->f)/2;
    /* Four silhouettes: model diamond, animation stairs, light cross,
     * performance histogram. Color is redundant with these shapes. */
    if (d->texture_u==1) {
        int q[4][3]={{ix,iy,z},{ix+icon/2,iy+icon/2,z},
            {ix+icon,iy,z},{ix+icon/2,iy-icon/2,z}};
        if (quad(context,q,d->color)<0) return -1;
    } else for (int i=0;i<3;++i) {
        int h=d->texture_u==4 ? icon*(i+1)/3 : icon/3;
        int y0=d->texture_u==2 ? iy+(i-1)*icon/3 : iy-icon/2;
        if (rf_terminal_rect(&c,ix+i*icon/3,ix+i*icon/3+icon/5,y0,y0+h,d->color)<0) return -1;
    }
    if (d->texture_u==3 &&
        rf_terminal_rect(&c,ix,ix+icon,iy-rail,iy+rail,d->color)<0) return -1;
    return rasterfall_text_panel_emit(d->a+icon+rail*5,d->b-rail*2,
        d->e+rail*2,d->f-rail*2,0,0,d->text,d->facing,rf_terminal_glyph,&c);
}
#endif
