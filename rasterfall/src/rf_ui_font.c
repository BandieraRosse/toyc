#include "tlibc_everything.h"
#include "rf_ui_font.h"
#include "toy_assets.h"
#include "core.h"
#include "string.h"

struct rf_ui_font_glyph { unsigned codepoint,advance,first,count; };
static struct {
    unsigned char *blob;
    unsigned size,count,index,spans,span_count;
    int attempted;
    struct rf_ui_font_glyph lookup[512];
} ui_font;

static unsigned f_u32(const unsigned char *p)
{ return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }

int rf_ui_font_load(void)
{
    unsigned char *blob;
    unsigned size=0,count,index,spans,total,previous=0;
    if (ui_font.blob) return 0;
    if (ui_font.attempted) return -1;
    ui_font.attempted=1;
    blob=toy_asset_load_file("rasterfall/assets/fonts/ui-sans18.rff",&size);
    if (!blob || size<32) goto fail;
    count=f_u32(blob+16);index=f_u32(blob+20);spans=f_u32(blob+24);total=f_u32(blob+28);
    if (memcmp(blob,"RFUIS18\0",8) || f_u32(blob+8)!=1 || f_u32(blob+12)!=20 ||
        count>10000 || index!=32 || spans!=32+count*16 || spans>size ||
        total>(size-spans)/5 || total*5!=size-spans) goto fail;
    for (unsigned i=0;i<count;++i) {
        const unsigned char *g=blob+index+i*16;
        unsigned cp=f_u32(g),advance=f_u32(g+4),first=f_u32(g+8),length=f_u32(g+12);
        if (cp<=previous || cp>0x10ffff || !advance || advance>32 || first>total || length>total-first) goto fail;
        previous=cp;
    }
    for (unsigned i=0;i<total;++i) {
        const unsigned char *s=blob+spans+i*5;
        if (!s[2] || !s[3] || s[0]+s[2]>32 || s[1]+s[3]>20 || !s[4]) goto fail;
    }
    ui_font.blob=blob;ui_font.size=size;ui_font.count=count;
    ui_font.index=index;ui_font.spans=spans;ui_font.span_count=total;
    return 0;
fail:
    if (blob) tlibc_free(blob);
    return -1;
}

void rf_ui_font_release(void)
{
    if (ui_font.blob) tlibc_free(ui_font.blob);
    memset(&ui_font,0,sizeof(ui_font));
}

static struct rf_ui_font_glyph f_glyph(unsigned cp)
{
    struct rf_ui_font_glyph *cached=&ui_font.lookup[cp%512],glyph={cp,9,0,0};
    unsigned low=0,high=ui_font.count;
    if (cp && cached->codepoint==cp) return *cached;
    while (low<high) {
        unsigned mid=low+(high-low)/2;
        unsigned codepoint=f_u32(ui_font.blob+ui_font.index+mid*16);
        if (codepoint<cp) low=mid+1;else high=mid;
    }
    if (low<ui_font.count) {
        const unsigned char *entry=ui_font.blob+ui_font.index+low*16;
        if (f_u32(entry)==cp) {
            glyph.advance=f_u32(entry+4);glyph.first=f_u32(entry+8);glyph.count=f_u32(entry+12);
            *cached=glyph;return glyph;
        }
    }
    if (cp!='?') { glyph=f_glyph('?');glyph.codepoint=cp; }
    *cached=glyph;
    return glyph;
}

int rf_ui_font_text_width(const char *text,int scale_milli)
{
    int advance=0,maximum=0;
    if (scale_milli<=0) scale_milli=1000;
    if (rf_ui_font_load()<0) return rasterfall_canvas_text_width(text,scale_milli);
    while (text && *text) {
        unsigned cp=rasterfall_canvas_codepoint(&text);
        if (cp=='\n') { if (advance>maximum) maximum=advance;advance=0; }
        else if (cp=='\t') advance=(advance/36+1)*36;
        else advance+=(int)f_glyph(cp).advance;
    }
    if (advance>maximum) maximum=advance;
    return (maximum*scale_milli+999)/1000;
}

static void f_draw(struct rasterfall_canvas *canvas,int x,int y,int max_right,
                   struct rf_ui_font_glyph glyph,unsigned color,int scale)
{
    for (unsigned i=0;i<glyph.count;++i) {
        const unsigned char *s=ui_font.blob+ui_font.spans+(glyph.first+i)*5;
        int left=x+s[0]*scale/1000,top=y+s[1]*scale/1000;
        int right=x+(s[0]+s[2])*scale/1000,bottom=y+(s[1]+s[3])*scale/1000;
        if (right>max_right) right=max_right;
        rasterfall_canvas_rect(canvas,left,top,right-left,bottom-top,color,s[4]);
    }
}

int rf_ui_font_text_wrap(struct rasterfall_canvas *canvas,int x,int y,int width,
                         int max_lines,const char *text,unsigned color,int scale)
{
    int line=0;
    if (!text || !*text || width<=0 || max_lines<=0) return 0;
    if (scale<=0) scale=1000;
    if (rf_ui_font_load()<0) return rasterfall_canvas_text_wrap(canvas,x,y,width,max_lines,text,color,scale);
    while (*text && line<max_lines) {
        const char *end=text,*scan=text;
        int advance=0,truncate=0,budget=width;
        struct rf_ui_font_glyph ellipsis=f_glyph(0x2026);
        if (line==max_lines-1) {
            if (rf_ui_font_text_width(text,scale)>width) truncate=1;
            for (const char *s=text;*s;++s) if (*s=='\n' && s[1]) truncate=1;
            if (truncate) budget-=(int)ellipsis.advance*scale/1000;
        }
        while (*scan && *scan!='\n') {
            unsigned cp=rasterfall_canvas_codepoint(&scan);
            int step=cp=='\t'?36:(int)f_glyph(cp).advance;
            if ((advance+step)*scale/1000>budget) break;
            advance+=step;end=scan;
        }
        if (end==text && *text!='\n' && !truncate) break;
        advance=0;
        while (text<end) {
            unsigned cp=rasterfall_canvas_codepoint(&text);
            struct rf_ui_font_glyph glyph=f_glyph(cp);
            if (cp!='\t') f_draw(canvas,x+advance*scale/1000,y+line*22*scale/1000,x+width,glyph,color,scale);
            advance+=cp=='\t'?36:(int)glyph.advance;
        }
        if (truncate) {
            f_draw(canvas,x+advance*scale/1000,y+line*22*scale/1000,x+width,ellipsis,color,scale);
            return line+1;
        }
        if (*text=='\n') ++text;
        else while (*text==' ') ++text;
        ++line;
    }
    return line;
}

int rf_ui_font_logic_test(void)
{
    struct rf_ui_font_glyph chinese,replacement;
    if (rf_ui_font_load()<0) return -1;
    chinese=f_glyph(0x4e2d);replacement=f_glyph(0x10ffff);
    if (chinese.advance!=18 || !chinese.count || !replacement.count ||
        replacement.first!=f_glyph('?').first) return -2;
    if (rf_ui_font_text_width("中",1500)!=27 ||
        rf_ui_font_text_width("AB\nA",1000)!=rf_ui_font_text_width("AB",1000)) return -3;
    return 0;
}
