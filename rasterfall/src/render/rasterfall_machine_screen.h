#ifndef RASTERFALL_MACHINE_SCREEN_H
#define RASTERFALL_MACHINE_SCREEN_H
#include "fb_font.h"
#include <string.h>

/* Fixed-resolution, world-surface console. Content never changes pixel pitch.
 * ASCII + LF; ESC 1/2/3 choose size, ESC B/N bold/regular, ESC A/M/F ink.
 * Each scanline partitions the face once (no coplanar background/glyph layers).
 * Clipping is in display pixels; future input owners only need to publish text. */
typedef int (*rf_screen_span)(void *,int,int,int,int,unsigned);
static int rf_machine_screen_emit(int width,int height,const char *text,
    unsigned foreground,rf_screen_span emit,void *context)
{
    unsigned row[1024],previous[1024];
    int start=0;
    if (!emit || !text || width<1 || width>1024 || height<1 || height>1024) return -1;
    for (int py=0;py<=height;++py) {
        if (py<height) {
            int x=8,y=8,size=1,bold=0,line_height=FB_FONT_H;
            unsigned ink=foreground;
            for (int px=0;px<width;++px) row[px]=0x101D27;
            for (const unsigned char *s=(const unsigned char *)text;*s;++s) {
                if (*s==27 && s[1]) {
                    ++s;
                    if (*s>='1' && *s<='3') size=*s-'0';
                    else if (*s=='B') bold=1;
                    else if (*s=='N') bold=0;
                    else if (*s=='A') ink=foreground;
                    else if (*s=='M') ink=0x73939E;
                    else if (*s=='F') ink=0xDAE9E9;
                    continue;
                }
                if (*s=='\n') {x=8;y+=line_height+6;line_height=FB_FONT_H*size;continue;}
                if (FB_FONT_H*size>line_height) line_height=FB_FONT_H*size;
                if (py>=y && py<y+FB_FONT_H*size) {
                    unsigned bits=fb_font_glyph_row(*s,(py-y)/size);
                    if (bold) bits|=bits>>1;
                    for (int bit=0;bit<FB_FONT_W;++bit) if (bits & (0x80>>bit))
                        for (int k=0;k<size;++k) {
                            int px=x+bit*size+k;
                            if (px>=0 && px<width) row[px]=ink;
                        }
                }
                x+=FB_FONT_W*size;
            }
        }
        if (py>0 && (py==height || memcmp(row,previous,width*sizeof(unsigned)))) {
            int left=0;
            for (int px=1;px<=width;++px) if (px==width || previous[px]!=previous[left]) {
                if (emit(context,left,px,start,py,previous[left])<0) return -1;
                left=px;
            }
            start=py;
        }
        if (py<height) memcpy(previous,row,width*sizeof(unsigned));
    }
    return 0;
}
#endif
