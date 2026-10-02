#ifndef RASTERFALL_LAB_TERMINAL_H
#define RASTERFALL_LAB_TERMINAL_H
#include "rasterfall_text_panel.h"
#include "rasterfall_machine_screen.h"
#include <math.h>

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
        /* Corner beacon housing stays in the static WORLD cache.
         * The projection is emitted separately with presentation time. */
        if (rf_terminal_box(&c,x-180,x+180,-896,-856,z-120,z+120,0x263743)<0 ||
            rf_terminal_box(&c,x-130,x+130,-856,-640,z-86,z+86,0x526874)<0 ||
            rf_terminal_box(&c,x-144,x+144,-664,-640,z-98,z+98,d->color)<0 ||
            rf_terminal_box(&c,x-96,x+96,-640,-620,z-60,z+60,0x172833)<0 ||
            rf_terminal_box(&c,x-64,x+64,-620,-608,z-32,z+32,d->color)<0) return -1;
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
    return 0;
}
static inline int rf_lab_projection_text_emit(const struct toy_map_draw *d,
    rf_terminal_quad quad,void *context)
{
    if (d->style!=2 && d->style!=3 && d->style!=4) return 0;
    int large=d->b-d->a>=2400,rail=large ? 24 : 12,icon=large ? 240 : 90;
    unsigned color=0;
    /* Uniform lift toward white preserves each category's hue. Only glyphs
     * receive this color; the authored color still owns backdrop and beam. */
    for (int shift=0;shift<=16;shift+=8) {
        unsigned channel=(d->color>>shift)&255;
        color|=(channel+(255-channel)*45/100)<<shift;
    }
    struct rf_terminal_emit_context c={context,quad,(d->c+d->d)/2,color};
    return rasterfall_text_panel_emit(d->a+icon+rail*5,d->b-rail*2,
        d->e+rail*2,d->f-rail*2,0,0,d->text,d->facing,rf_terminal_glyph,&c);
}
static inline int rf_lab_beacon_projection_emit(const struct toy_map_draw *d,
    unsigned time_ms,rf_terminal_quad quad,void *context)
{
    int x=(d->a+d->b)/2,z=(d->c+d->d)/2;
    double angle=(time_ms%8000)*6.283185307179586/8000.0;
    double cosine=cos(angle),sine=sin(angle);
    int bob=(int)(48*sin((time_ms%3200)*6.283185307179586/3200.0));
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
                int dx=axis ? 0 : uv[i][0],dz=axis ? uv[i][0] : 0;
                q[i][0]=x+(int)(dx*cosine-dz*sine);
                q[i][1]=-300+bob+uv[i][1];
                q[i][2]=z+(int)(dx*sine+dz*cosine);
            }
            if (quad(context,q,d->color)<0) return -1;
        }
    }
    return 0;
}

typedef int (*rf_projection_quad)(void *,const int [4][3],unsigned,int);
/* Presentation-only scattering shell and backdrop. No collision or light source.
 * Backdrop sits behind the glyphs from either viewing side, avoiding coplanar
 * blending over their opaque depth. The beam follows the beacon's frozen clock. */
static inline int rf_lab_projection_light_emit(const struct toy_map_draw *d,
    unsigned time_ms,int camera_z,rf_projection_quad quad,void *context)
{
    if (d->style!=2 && d->style!=3 && d->style!=4 && d->style!=7) return 0;
    int x=(d->a+d->b)/2,z=(d->c+d->d)/2,beacon=d->style==7;
    int large=d->b-d->a>=2400;
    int y0=beacon ? -608 : -896+(large ? 140 : 70)+8;
    int bob=(int)(48*sin((time_ms%3200)*6.283185307179586/3200.0));
    int y1=beacon ? -300+bob : d->e;
    int rx0=beacon ? 64 : (large ? 315 : 135),rz0=beacon ? 32 : 24;
    int rx1=beacon ? 140 : (d->b-d->a)/2,rz1=beacon ? 140 : 16;
    if (!beacon) {
        int back_z=z+(camera_z>=z ? -4 : 4);
        unsigned tint=0;
        for (int shift=0;shift<=16;shift+=8)
            tint|=(8+((d->color>>shift)&255)/10)<<shift;
        int panel[4][3]={{d->a,d->e,back_z},{d->b,d->e,back_z},
            {d->b,d->f,back_z},{d->a,d->f,back_z}};
        if (quad(context,panel,tint,112)<0) return -1;
    }
    if (y1<=y0) return 0;
    const int corners[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    double phase=(time_ms%4000)*6.283185307179586/4000.0+
        ((unsigned)x^(unsigned)z)%17*0.37;
    for (int side=0;side<4;++side) {
        int next=(side+1)%4;
        for (int band=0;band<6;++band) {
            int p[4][3];
            for (int k=0;k<4;++k) {
                int t=band+(k>=2),corner=(k==1 || k==2) ? next : side;
                int rx=rx0+(rx1-rx0)*t/6,rz=rz0+(rz1-rz0)*t/6;
                p[k][0]=x+corners[corner][0]*rx;
                p[k][1]=y0+(y1-y0)*t/6;
                p[k][2]=z+corners[corner][1]*rz;
            }
            /* A slow wave climbs the shell; always faint, never a strobe. */
            int alpha=24-band*2+(int)(3*sin(phase-band*0.8));
            if (quad(context,p,d->color,alpha)<0) return -1;
        }
        /* Fine rays connect the aperture to the projected silhouette. */
        for (int ray=1;ray<=2;++ray) {
            int p[4][3];
            for (int k=0;k<4;++k) {
                int top=k>=2,weight=ray*100+(k==1 || k==2 ? 2 : -2);
                int rx=top ? rx1 : rx0,rz=top ? rz1 : rz0;
                p[k][0]=x+(corners[side][0]*(300-weight)+corners[next][0]*weight)*rx/300;
                p[k][1]=top ? y1 : y0;
                p[k][2]=z+(corners[side][1]*(300-weight)+corners[next][1]*weight)*rz/300;
            }
            if (quad(context,p,d->color,30+(int)(4*sin(phase+ray+side)))<0) return -1;
        }
    }
    return 0;
}
#endif
