#ifndef RASTERFALL_TEXT_PANEL_H
#define RASTERFALL_TEXT_PANEL_H

#include "fb_font.h"
#include <string.h>

/* A single opaque plane partitioned into background and glyph-colored quads.
 * facing=+1 means the text reads from +Z; -1 means it reads from -Z. */
typedef int (*rasterfall_text_panel_quad)(void *, int, int, int, int, int);

static int rasterfall_text_panel_emit(int x0, int x1, int y0, int y1,
    int inset_x, int inset_y, const char *text, int facing,
    rasterfall_text_panel_quad emit, void *context)
{
    int chars,cell,px_width,px_height,left,top,bottom;
    if (!emit || x1<=x0 || y1<=y0) return -1;
    chars=text ? (int)strlen(text) : 0;
    if (chars<=0 || x1-x0<=2*inset_x || y1-y0<=2*inset_y)
        return emit(context,x0,x1,y0,y1,0);
    cell=(x1-x0-2*inset_x)/(chars*FB_FONT_W);
    if ((y1-y0-2*inset_y)/FB_FONT_H<cell)
        cell=(y1-y0-2*inset_y)/FB_FONT_H;
    if (cell<1) return emit(context,x0,x1,y0,y1,0);
    px_width=chars*FB_FONT_W;
    px_height=FB_FONT_H;
    left=x0+(x1-x0-px_width*cell)/2;
    top=y1-(y1-y0-px_height*cell)/2;
    bottom=top-px_height*cell;
    if (top<y1 && emit(context,x0,x1,top,y1,0)<0) return -1;
    if (bottom>y0 && emit(context,x0,x1,y0,bottom,0)<0) return -1;
    if (left>x0 && emit(context,x0,left,bottom,top,0)<0) return -1;
    if (left+px_width*cell<x1 &&
        emit(context,left+px_width*cell,x1,bottom,top,0)<0) return -1;
    for (int row=0;row<px_height;++row) {
        int run_start=0,run_color=-1;
        for (int p=0;p<=px_width;++p) {
            int color=-1;
            if (p<px_width) {
                int source=facing>0 ? px_width-1-p : p;
                unsigned char bits=fb_font_glyph_row(
                    (unsigned char)text[source/FB_FONT_W],row);
                color=(bits & (unsigned char)(0x80>>(source%FB_FONT_W)))!=0;
            }
            if (color!=run_color) {
                if (run_color>=0 && emit(context,left+run_start*cell,
                    left+p*cell,top-(row+1)*cell,top-row*cell,
                    run_color)<0) return -1;
                run_start=p;run_color=color;
            }
        }
    }
    return 0;
}
#endif
