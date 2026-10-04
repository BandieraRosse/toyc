#ifndef RF_UI_FONT_H
#define RF_UI_FONT_H

#include "rasterfall_canvas.h"

/* Player-only Sans font. Legacy/world text keeps the fb_font contract. */
int rf_ui_font_load(void);
void rf_ui_font_release(void);
int rf_ui_font_text_width(const char *text,int scale_milli);
int rf_ui_font_text_wrap(struct rasterfall_canvas *canvas,int x,int y,int width,
                         int max_lines,const char *text,unsigned color,int scale_milli);
int rf_ui_font_logic_test(void);

#endif
