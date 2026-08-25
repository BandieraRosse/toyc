#include "rasterfall_console.h"
#include "core.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "string.h"
#include "tlibc_everything.h"

#define KEY_ESC 1
#define KEY_ENTER 28
#define KEY_BACKSPACE 14
#define KEY_UP 103
#define KEY_DOWN 108
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_SPACE 57

static int take(struct toy_input *in, unsigned char *pending, unsigned int k)
{ int v = pending[k] || toy_input_pressed(in, k); if (v) { pending[k] = 0; in->key_pressed[k] = 0; } return v; }
static int chr(unsigned int k)
{ static const int keys[] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44}; static const char *s="abcdefghijklmnopqrstuvwxyz"; int i; for(i=0;i<26;i++) if(k==(unsigned)keys[i]) return s[i]; if(k>=2&&k<=10)return '1'+k-2; if(k==11)return '0'; if(k==KEY_MINUS)return '-'; if(k==KEY_EQUAL)return '+'; if(k==KEY_SPACE)return ' '; return 0; }
static unsigned int log_color(enum rasterfall_console_log_level level)
{
    if (level == RASTERFALL_CONSOLE_WARNING) return 0xF0C060;
    if (level == RASTERFALL_CONSOLE_ERROR) return 0xF07070;
    if (level == RASTERFALL_CONSOLE_COMMAND) return 0x80D8FF;
    return 0xC4CCD8;
}

void rasterfall_console_log(struct rasterfall_console *c,
                            enum rasterfall_console_log_level level,
                            const char *s)
{
    int i;
    if (!c || !s) return;
    if (c->output_count < 64) c->output_count++;
    for (i = c->output_count - 1; i > 0; i--)
        memcpy(&c->output[i], &c->output[i - 1], sizeof(c->output[i]));
    strncpy(c->output[0].text, s, sizeof(c->output[0].text) - 1);
    c->output[0].text[sizeof(c->output[0].text) - 1] = 0;
    c->output[0].color = log_color(level);
    __printf("console: %s\n", s);
}

static void out(struct rasterfall_console *c, const char *s)
{ rasterfall_console_log(c, RASTERFALL_CONSOLE_INFO, s); }
static void out_error(struct rasterfall_console *c, const char *s)
{ rasterfall_console_log(c, RASTERFALL_CONSOLE_ERROR, s); }
static int num(const char *s, int *v, int *relative)
{ int sign=1,n=0; *relative=0; if(*s=='+'||*s=='-'){*relative=1;if(*s++=='-')sign=-1;} if(!*s)return 0; while(*s>='0'&&*s<='9'){n=n*10+*s++-'0';} if(*s)return 0; *v=n*sign; return 1; }
static int words(char *s,char **w,int max){int n=0;while(*s&&n<max){while(*s==' ')s++;if(!*s)break;w[n++]=s;while(*s&&*s!=' ')s++;if(*s)*s++=0;}return n;}
static void execute(struct rasterfall_console *c)
{ char *w[6],line[160]; int n,v,r; strcpy(line,c->line); n=words(line,w,6); if(!n)return;
  if(!strcmp(w[0],"killall")){c->killall_requested=1;out(c,"killall requested");return;}
  if (!strncmp(w[0], "give+", 5)) {
      if (!num(w[0] + 5, &v, &r) || r || v <= 0) {
          out_error(c, "usage: give+<positive amount>"); return;
      }
      c->give_requested = v; out(c, "money grant requested"); return;
  }
  if(!strcmp(w[0],"clear")){c->output_count=0;return;}
  if(!strcmp(w[0],"help")){
      out(c,"GENERAL");
      out(c,"  help          show command groups");
      out(c,"  clear         clear console log");
      out(c,"  killall       kill all active enemies");
      out(c,"  give+N        add N money, e.g. give+500");
      out(c,"POSE EDITOR");
      out(c,"  pose          open default Eula + AK editor");
      out(c,"  pose eula ak  edit the Eula + AK rifle pose");
      out(c,"EDITOR");
      out(c,"  pose          open Eula + AK editor");
      out(c,"  pose eula ak  edit this character/weapon pair");
      return;
  }
  if(!strcmp(w[0],"pose") && (n==1 || (n>=3&&!strcmp(w[1],"eula")&&!strcmp(w[2],"ak")))){c->calibration.active=1;c->calibration.character=0;c->calibration.weapon=TOY_GAME_WEAPON_AK;c->calibration.left_ik=1;c->calibration.axes=1;c->calibration.anchors=1;c->pose_hud_request=1;c->close_requested=1;out(c,"Rifle Pose Editor: Eula + AK");return;}
  out_error(c,"unknown command; type help");
}
void rasterfall_console_init(struct rasterfall_console *c){memset(c,0,sizeof(*c));rasterfall_calibration_init(&c->calibration);}
int rasterfall_console_handle_input(struct rasterfall_console *c,struct toy_input *in,unsigned char *pending){int k,ch,len,i;if(take(in,pending,KEY_ESC)){c->open=0;return 1;}if(take(in,pending,KEY_ENTER)){if(c->line[0]){for(i=7;i>0;i--)strcpy(c->history[i],c->history[i-1]);strcpy(c->history[0],c->line);rasterfall_console_log(c,RASTERFALL_CONSOLE_COMMAND,c->line);}c->history_cursor=0;execute(c);rasterfall_calibration_apply_runtime(&c->calibration.weapon_profile);c->line[0]=0;return 1;}if(take(in,pending,KEY_BACKSPACE)){len=strlen(c->line);if(len)c->line[len-1]=0;return 1;}if(take(in,pending,KEY_UP)){if(c->history_cursor<8&&c->history[c->history_cursor][0]){strcpy(c->line,c->history[c->history_cursor]);c->history_cursor++;}return 1;}if(take(in,pending,KEY_DOWN)){if(c->history_cursor>1)c->history_cursor--;else c->history_cursor=0;if(c->history_cursor==0)c->line[0]=0;else strcpy(c->line,c->history[c->history_cursor-1]);return 1;}for(k=0;k<TOY_INPUT_KEY_COUNT;k++)if((ch=chr(k))&&take(in,pending,k)){len=strlen(c->line);if(len<159){c->line[len]=ch;c->line[len+1]=0;}return 1;}return c->open;}
static void rect_alpha(struct toy_surface *s,int x,int y,int w,int h,
                       unsigned int color, int alpha)
{
    int xx, yy;
    unsigned int cr=(color>>16)&255, cg=(color>>8)&255, cb=color&255;
    if (alpha < 0) alpha=0;
    if (alpha > 255) alpha=255;
    for (yy=y; yy<y+h; yy++) if (yy>=0 && yy<s->height)
        for (xx=x; xx<x+w; xx++) if (xx>=0 && xx<s->width) {
            unsigned int *p=(unsigned int *)((unsigned char *)s->pixels+
                                              yy*s->stride)+xx;
            unsigned int old=*p;
            unsigned int or=(old>>16)&255, og=(old>>8)&255, ob=old&255;
            *p=((or*(255-alpha)+cr*alpha)/255<<16)|
               ((og*(255-alpha)+cg*alpha)/255<<8)|
               ((ob*(255-alpha)+cb*alpha)/255);
        }
}
/* fb_draw_string is deliberately a low-level primitive and assumes the caller
 * has already clipped its text.  Console text is user-controlled, so wrap it
 * before calling into the framebuffer layer.  This also gives the console a
 * predictable multi-line alphabetic layout. */
static int draw_wrapped(struct toy_surface *s, const char *text, int x, int y,
                        int columns, int max_lines, unsigned int color)
{
    char line[128]; int n = 0, row = 0; const char *p = text;
    if (columns < 1) return 0;
    if (columns > (int)sizeof(line) - 1) columns = sizeof(line) - 1;
    while (row < max_lines) {
        while (*p && *p != '\n' && n < columns) line[n++] = *p++;
        line[n] = 0;
        if (x + n * FB_FONT_W <= s->width && y + row * FB_FONT_H + FB_FONT_H <= s->height)
            fb_draw_string((unsigned char *)s->pixels, x, y + row * FB_FONT_H,
                           line, color, s->stride);
        row++; n = 0;
        if (*p == '\n') p++;
        if (!*p) break;
    }
    return row;
}

void rasterfall_console_draw(struct toy_surface *s,const struct rasterfall_console *c)
{
    int i,w=s->width,h=s->height,x=20,y=12;
    int columns = (w - 40) / FB_FONT_W;
    int output_y = y + 28, output_lines = (h - 76) / FB_FONT_H;
    char prompt[192]; const char *line = c->line;
    rect_alpha(s,0,0,w,h,0x07101C,190);
    draw_wrapped(s,"DEVELOPER CONSOLE  |  INFO  WARN  ERROR  COMMAND",x,y,
                 columns,1,0xF0B35A);
    for(i=0;i<c->output_count&&output_lines>0;i++) {
        int used = draw_wrapped(s,c->output[i].text,x,output_y,columns,
                                output_lines,c->output[i].color);
        output_y += used * FB_FONT_H;
        output_lines -= used;
    }
    if ((int)strlen(line) > columns - 3)
        line += strlen(line) - (columns - 3);
    snprintf(prompt,sizeof(prompt),"> %s_",line);
    draw_wrapped(s,prompt,x,h-32,columns,1,0xFFFFFF);
    draw_wrapped(s,"INPUT   ENTER execute   UP/DOWN history   ESC close",x,h-16,
                 columns,1,0xD88A32);
}
int rasterfall_console_logic_test(void){struct rasterfall_console c;rasterfall_console_init(&c);if(c.calibration.weapon!=TOY_GAME_WEAPON_AK)return 1;return rasterfall_calibration_logic_test();}
