#ifndef RASTERFALL_LAB_TERMINAL_H
#define RASTERFALL_LAB_TERMINAL_H
#include "rasterfall_text_panel.h"
#include "rasterfall_machine_screen.h"

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
struct rf_machine_screen_context {
    struct rf_terminal_emit_context emit;
    const struct toy_map_draw *draw;
};
static int rf_machine_screen_span(void *context,int x0,int x1,int y0,int y1,unsigned color)
{
    struct rf_machine_screen_context *c=context;
    const struct toy_map_draw *d=c->draw;
    int pitch_x=(d->b-d->a)/d->texture_u,pitch_y=(d->f-d->e)/d->texture_v;
    int left=d->facing>0 ? d->b-x1*pitch_x : d->a+x0*pitch_x;
    int right=d->facing>0 ? d->b-x0*pitch_x : d->a+x1*pitch_x;
    return rf_terminal_rect(&c->emit,left,right,d->f-y1*pitch_y,d->f-y0*pitch_y,color);
}
static int rf_terminal_box(struct rf_terminal_emit_context *c,
    int x0,int x1,int y0,int y1,int z0,int z1,unsigned color)
{
    int p[8][3]={{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
                 {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
    const int faces[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{3,7,6,2},{0,1,5,4}};
    for (int f=0;f<6;++f) {
        int q[4][3];
        for (int k=0;k<4;++k) for (int a=0;a<3;++a) q[k][a]=p[faces[f][k]][a];
        if (c->quad(c->context,q,color)<0) return -1;
    }
    return 0;
}
static int rf_lab_terminal_emit(const struct toy_map_draw *d,
    rf_terminal_quad quad,void *context)
{
    int x=(d->a+d->b)/2,z=(d->c+d->d)/2;
    int large=d->b-d->a>=2400;
    int rail=large ? 24 : 12;
    struct rf_terminal_emit_context c={context,quad,z,d->color};
    if (d->f<=d->e) return -1;
    if (d->style==6) {
        if (d->a==d->b && d->d>d->c) {
            int p[4][3]={{x,d->f,d->c},{x,d->f,d->d},{x,d->e,d->d},{x,d->e,d->c}};
            return quad(context,p,d->color);
        }
        if (d->b>d->a && d->c==d->d)
            return rf_terminal_rect(&c,d->a,d->b,d->e,d->f,d->color);
        return -1;
    }
    if (d->b<=d->a) return -1;
    if (d->style==7) {
        /* Corner beacon: grounded optical pedestal and two crossed, open
         * diamond projections. No solid screen; readable along either road. */
        if (rf_terminal_box(&c,x-180,x+180,-896,-856,z-120,z+120,0x263743)<0 ||
            rf_terminal_box(&c,x-130,x+130,-856,-640,z-86,z+86,0x526874)<0 ||
            rf_terminal_box(&c,x-144,x+144,-664,-640,z-98,z+98,d->color)<0 ||
            rf_terminal_box(&c,x-96,x+96,-640,-620,z-60,z+60,0x172833)<0 ||
            rf_terminal_box(&c,x-64,x+64,-620,-608,z-32,z+32,d->color)<0) return -1;
        for (int axis=0;axis<2;++axis) {
            const int outer[4][2]={{0,200},{140,0},{0,-200},{-140,0}};
            const int inner[4][2]={{0,164},{112,0},{0,-164},{-112,0}};
            for (int side=0;side<4;++side) {
                int next=(side+1)%4;
                int uv[4][2]={{outer[side][0],outer[side][1]},
                    {outer[next][0],outer[next][1]},
                    {inner[next][0],inner[next][1]},
                    {inner[side][0],inner[side][1]}};
                int q[4][3];
                for (int i=0;i<4;++i) {
                    q[i][0]=x+(axis ? 0 : uv[i][0]);
                    q[i][1]=-300+uv[i][1];
                    q[i][2]=z+(axis ? uv[i][0] : 0);
                }
                if (quad(context,q,d->color)<0) return -1;
            }
        }
        return 0;
    }
    if (d->style==5) {
        struct rf_machine_screen_context screen={c,d};
        if (d->texture_u<1 || d->texture_v<1 ||
            (d->b-d->a)%d->texture_u || (d->f-d->e)%d->texture_v ||
            d->b-d->a<d->texture_u || d->f-d->e<d->texture_v) return -1;
        return rf_machine_screen_emit(d->texture_u,d->texture_v,d->text,d->color,
            rf_machine_screen_span,&screen);
    }
    /* Low projector housing, visible from either side. */
    int bx0=x-(large?360:180),bx1=x+(large?360:180);
    int by0=-896,by1=by0+(large?140:70);
    /* Grounded complete enclosure, recessed optical cassette and protective feet. */
    if (rf_terminal_box(&c,bx0,bx1,by0+12,by1-16,z-100,z+100,0x526874)<0 ||
        rf_terminal_box(&c,bx0+18,bx1-18,by1-16,by1,z-78,z+78,0x172833)<0 ||
        rf_terminal_box(&c,bx0+45,bx1-45,by1,by1+8,z-24,z+24,d->color)<0) return -1;
    for (int side=0;side<2;++side) {
        int foot=side ? bx1-48 : bx0;
        if (rf_terminal_box(&c,foot,foot+48,by0,by0+20,z-120,z+120,0x253641)<0) return -1;
    }
    c.z=z;
    if (rf_terminal_rect(&c,bx0,bx1,by1-12,by1,d->color)<0 ||
        rf_terminal_rect(&c,d->a,d->b,d->e,d->e+rail,d->color)<0 ||
        rf_terminal_rect(&c,d->a,d->a+rail*5,d->f-rail,d->f,d->color)<0 ||
        rf_terminal_rect(&c,d->b-rail*5,d->b,d->f-rail,d->f,d->color)<0 ||
        rf_terminal_rect(&c,d->a,d->a+rail,d->f-rail*4,d->f,d->color)<0 ||
        rf_terminal_rect(&c,d->b-rail,d->b,d->f-rail*4,d->f,d->color)<0) return -1;
    int icon=large ? 240 : 90,ix=d->a+rail*3,iy=(d->e+d->f)/2;
    /* Four silhouettes: model diamond, animation stairs, light cross,
     * performance histogram. Color is redundant with these shapes. */
    if (d->texture_u==1) {
        int q[4][3]={{ix,iy,z},{ix+icon/2,iy+icon/2,z},
            {ix+icon,iy,z},{ix+icon/2,iy-icon/2,z}};
        if (quad(context,q,d->color)<0) return -1;
    } else if (d->texture_u==3) {
        int mid=ix+icon/2,r=icon/6;
        if (rf_terminal_rect(&c,mid-r,mid+r,iy-r,iy+r,d->color)<0 ||
            rf_terminal_rect(&c,ix,mid-r-rail,iy-rail,iy+rail,d->color)<0 ||
            rf_terminal_rect(&c,mid+r+rail,ix+icon,iy-rail,iy+rail,d->color)<0 ||
            rf_terminal_rect(&c,mid-rail,mid+rail,iy+r+rail,iy+icon/2,d->color)<0 ||
            rf_terminal_rect(&c,mid-rail,mid+rail,iy-icon/2,iy-r-rail,d->color)<0) return -1;
    } else for (int i=0;i<3;++i) {
        if (d->texture_u==2) {
            int tx=ix+i*icon/3,ty=iy+(i-1)*icon/6;
            int q[4][3]={{tx,ty-icon/4,z},{tx+icon/4,ty,z},
                        {tx,ty+icon/4,z},{tx+rail,ty,z}};
            if (quad(context,q,d->color)<0) return -1;
        } else if (rf_terminal_rect(&c,ix+i*icon/3,ix+i*icon/3+icon/5,
                       iy-icon/2,iy-icon/2+icon*(i+1)/3,d->color)<0) return -1;
    }
    return rasterfall_text_panel_emit(d->a+icon+rail*5,d->b-rail*2,
        d->e+rail*2,d->f-rail*2,0,0,d->text,d->facing,rf_terminal_glyph,&c);
}
#endif
