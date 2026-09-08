/* SPDX-License-Identifier: MIT
 * UTF-8 framebuffer text backed by Rasterfall's GB2312 8/16x16 bitmap asset. */
#include "fb_font.h"
#include "fb_draw.h"
#include "toy_assets.h"
#include "core.h"
#include "string.h"

#define RF_FONT_HEADER_SIZE 32u
#define RF_FONT_ASCII_OFFSET 32u
#define RF_FONT_ASCII_COUNT 95u
#define RF_FONT_GB_ROWS 87u
#define RF_FONT_GB_COLUMNS 94u
#define RF_FONT_GB_BYTES (RF_FONT_GB_ROWS * RF_FONT_GB_COLUMNS * 32u)

static unsigned char *font_blob;
static uint32_t font_gb_offset, font_index_offset, font_index_count;
static int font_load_attempted;

static uint32_t read_u32(const unsigned char *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int fb_font_load(const char *path)
{
    unsigned char *blob;
    uint32_t size = 0, gb_offset, index_offset, index_count;
    font_load_attempted = 1;
    blob = toy_asset_load_file(path, &size);
    if (!blob || size < RF_FONT_HEADER_SIZE || memcmp(blob, "RFHZK16\0", 8) != 0 ||
        read_u32(blob + 8) != 1 || read_u32(blob + 12) != RF_FONT_ASCII_OFFSET ||
        read_u32(blob + 20) != RF_FONT_GB_ROWS) {
        if (blob) tlibc_free(blob);
        return -1;
    }
    gb_offset = read_u32(blob + 16);
    index_offset = read_u32(blob + 24);
    index_count = read_u32(blob + 28);
    if (gb_offset != RF_FONT_ASCII_OFFSET + RF_FONT_ASCII_COUNT * 16u ||
        index_offset != gb_offset + RF_FONT_GB_BYTES || index_count > (size - index_offset) / 8u) {
        tlibc_free(blob); return -1;
    }
    if (font_blob) tlibc_free(font_blob);
    font_blob = blob; font_gb_offset = gb_offset;
    font_index_offset = index_offset; font_index_count = index_count;
    return 0;
}

void fb_font_unload(void)
{
    if (font_blob) tlibc_free(font_blob);
    font_blob = NULL; font_gb_offset = font_index_offset = font_index_count = 0;
    font_load_attempted = 0;
}

static void ensure_font(void)
{
    if (!font_blob && !font_load_attempted)
        (void)fb_font_load("rasterfall/assets/fonts/gb2312-16.rfh");
}

static const unsigned char *ascii_glyph(unsigned int cp)
{
    ensure_font();
    if (!font_blob || cp < 0x20 || cp > 0x7e) return NULL;
    return font_blob + RF_FONT_ASCII_OFFSET + (cp - 0x20) * 16u;
}

static const unsigned char *wide_glyph(unsigned int cp)
{
    ensure_font();
    uint32_t low = 0, high = font_index_count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        const unsigned char *entry = font_blob + font_index_offset + mid * 8u;
        if (read_u32(entry) < cp) low = mid + 1; else high = mid;
    }
    if (low < font_index_count) {
        const unsigned char *entry = font_blob + font_index_offset + low * 8u;
        uint32_t slot = read_u32(entry + 4);
        if (read_u32(entry) == cp && slot < RF_FONT_GB_ROWS * RF_FONT_GB_COLUMNS)
            return font_blob + font_gb_offset + slot * 32u;
    }
    return NULL;
}

static unsigned int utf8_next(const char **cursor)
{
    const unsigned char *s = (const unsigned char *)*cursor;
    unsigned int cp;
    if (s[0] < 0x80) { *cursor += 1; return s[0]; }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f); *cursor += 2;
        return cp >= 0x80 ? cp : 0xfffd;
    }
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        cp = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); *cursor += 3;
        return cp >= 0x800 ? cp : 0xfffd;
    }
    if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 &&
        (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
        cp = ((s[0] & 7) << 18) | ((s[1] & 0x3f) << 12) |
             ((s[2] & 0x3f) << 6) | (s[3] & 0x3f); *cursor += 4;
        return cp >= 0x10000 && cp <= 0x10ffff ? cp : 0xfffd;
    }
    *cursor += 1; return 0xfffd;
}

static int glyph_width(unsigned int cp)
{ return cp >= 0x20 && cp <= 0x7e ? FB_FONT_W : (wide_glyph(cp) ? FB_FONT_CJK_W : FB_FONT_W); }

uint16_t fb_font_glyph_row_utf8(unsigned int cp, int row, int *width)
{
    const unsigned char *glyph;
    if (row < 0 || row >= FB_FONT_H) return 0;
    glyph = ascii_glyph(cp);
    if (glyph) { if (width) *width = FB_FONT_W; return (uint16_t)glyph[row] << 8; }
    glyph = wide_glyph(cp);
    if (glyph) {
        if (width) *width = FB_FONT_CJK_W;
        return ((uint16_t)glyph[row * 2] << 8) | glyph[row * 2 + 1];
    }
    if (width) *width = FB_FONT_W;
    return 0;
}

unsigned char fb_font_glyph_row(unsigned char ch, int row)
{
    const unsigned char *glyph = ascii_glyph(ch);
    return glyph && row >= 0 && row < FB_FONT_H ? glyph[row] : 0;
}

static void draw_codepoint(unsigned char *fbp, int x, int y, unsigned int cp,
                           uint32_t fg, uint32_t bg, int with_bg, int stride, int scale)
{
    int row, col, width = glyph_width(cp);
    for (row = 0; row < FB_FONT_H; row++) {
        uint16_t bits = fb_font_glyph_row_utf8(cp, row, NULL);
        for (col = 0; col < width; col++) {
            int on = bits & (0x8000u >> col), dx, dy;
            if (!on && !with_bg) continue;
            for (dy = 0; dy < scale; dy++) for (dx = 0; dx < scale; dx++)
                fb_put_pixel(fbp, x + col * scale + dx, y + row * scale + dy,
                             on ? fg : bg, stride);
        }
    }
}

void fb_draw_char(unsigned char *p, int x, int y, unsigned char ch, uint32_t c, int s)
{ draw_codepoint(p, x, y, ch, c, 0, 0, s, 1); }
void fb_draw_char_bg(unsigned char *p, int x, int y, unsigned char ch, uint32_t fg, uint32_t bg, int s)
{ draw_codepoint(p, x, y, ch, fg, bg, 1, s, 1); }

static void draw_string_inner(unsigned char *p, int x, int y, const char *str,
                              uint32_t fg, uint32_t bg, int with_bg, int stride, int scale)
{
    int start_x = x;
    while (*str) {
        unsigned int cp = utf8_next(&str);
        if (cp == '\n') { x = start_x; y += FB_FONT_H * scale; }
        else if (cp == '\t') { int tab = 4 * FB_FONT_W * scale; x = ((x / tab) + 1) * tab; }
        else if (cp >= 0x20) {
            draw_codepoint(p, x, y, cp, fg, bg, with_bg, stride, scale);
            x += glyph_width(cp) * scale;
        }
    }
}

void fb_draw_string(unsigned char *p, int x, int y, const char *str, uint32_t c, int s)
{ draw_string_inner(p, x, y, str, c, 0, 0, s, 1); }
void fb_draw_string_bg(unsigned char *p, int x, int y, const char *str, uint32_t fg, uint32_t bg, int s)
{ draw_string_inner(p, x, y, str, fg, bg, 1, s, 1); }
void fb_draw_char_scaled(unsigned char *p, int x, int y, unsigned char ch, uint32_t c, int s, int scale)
{ if (scale < 1) scale = 1; draw_codepoint(p, x, y, ch, c, 0, 0, s, scale); }
void fb_draw_string_scaled(unsigned char *p, int x, int y, const char *str, uint32_t c, int s, int scale)
{ if (scale < 1) scale = 1; draw_string_inner(p, x, y, str, c, 0, 0, s, scale); }

int fb_string_width(const char *str)
{
    int width = 0, line = 0;
    while (*str) {
        unsigned int cp = utf8_next(&str);
        if (cp == '\n') { if (line > width) width = line; line = 0; }
        else if (cp == '\t') line = (line / (4 * FB_FONT_W) + 1) * (4 * FB_FONT_W);
        else if (cp >= 0x20) line += glyph_width(cp);
    }
    return line > width ? line : width;
}
int fb_char_width(void) { return FB_FONT_W; }
int fb_font_height(void) { return FB_FONT_H; }
int fb_char_width_scaled(int scale) { return FB_FONT_W * (scale < 1 ? 1 : scale); }
int fb_font_height_scaled(int scale) { return FB_FONT_H * (scale < 1 ? 1 : scale); }
int fb_string_width_scaled(const char *str, int scale)
{ return fb_string_width(str) * (scale < 1 ? 1 : scale); }
