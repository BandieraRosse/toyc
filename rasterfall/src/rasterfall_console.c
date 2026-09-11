#include "tlibc_everything.h"
#include "rf_core_host.h"
#include "rasterfall_console.h"
#include "rf_game_lifecycle.h"
#include "core.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "string.h"

#define KEY_ESC 1
#define KEY_ENTER 28
#define KEY_BACKSPACE 14
#define KEY_UP 103
#define KEY_DOWN 108
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_SPACE 57

static int take(struct rf_input_frame *in, unsigned char *pending, unsigned int k)
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

void rf_command_output_init(struct rf_command_output *output)
{ if (output) memset(output, 0, sizeof(*output)); }
void rf_command_output_write(struct rf_command_output *output,
                             enum rf_command_output_level level,
                             const char *text)
{ struct rf_command_output_line *line;
  if (!output || !text || output->count >= RF_COMMAND_OUTPUT_MAX_LINES) return;
  line = &output->lines[output->count++]; line->level = level;
  strncpy(line->text, text, sizeof(line->text) - 1);
  line->text[sizeof(line->text) - 1] = 0;
}
void rf_terminal_session_init(struct rf_terminal_session *session)
{ if (!session) return; memset(session, 0, sizeof(*session)); }
int rf_terminal_session_set_input(struct rf_terminal_session *session,
                                   const char *input)
{ if (!session || !input) return -1;
  strncpy(session->input, input, sizeof(session->input) - 1);
  session->input[sizeof(session->input) - 1] = 0;
  return 0;
}
void rf_terminal_session_push_history(struct rf_terminal_session *session)
{ int i;
  if (!session || !session->input[0]) return;
  for (i = RF_TERMINAL_HISTORY_MAX - 1; i > 0; i--)
      strcpy(session->history[i], session->history[i - 1]);
  strcpy(session->history[0], session->input);
  session->history_cursor = 0;
}
static struct rasterfall_console *state_console(const struct rf_command_context *context)
{ return context ? (struct rasterfall_console *)context->command_state : NULL; }
static void out(struct rf_command_output *output, const char *s)
{ rf_command_output_write(output, RF_COMMAND_OUTPUT_NORMAL, s); }
static void out_error(struct rf_command_output *output, const char *s)
{ rf_command_output_write(output, RF_COMMAND_OUTPUT_ERROR, s); }
static int num(const char *s, int *v, int *relative)
{ int sign=1,n=0; *relative=0; if(*s=='+'||*s=='-'){*relative=1;if(*s++=='-')sign=-1;} if(!*s)return 0; while(*s>='0'&&*s<='9'){n=n*10+*s++-'0';} if(*s)return 0; *v=n*sign; return 1; }
static int words(char *s,char **w,int max){int n=0;while(*s&&n<max){while(*s==' ')s++;if(!*s)break;w[n++]=s;while(*s&&*s!=' ')s++;if(*s)*s++=0;}return n;}
static int command_killall(const struct rf_command_context *context,
                           struct rf_command_output *output, int argc, char **argv)
{ struct rasterfall_console *c=state_console(context); (void)argc; (void)argv;
  if (!c) { out_error(output,"command state unavailable"); return -1; }
  c->killall_requested=1; out(output,"killall requested"); return 0; }
static int command_give(const struct rf_command_context *context,
                        struct rf_command_output *output, int argc, char **argv)
{ struct rasterfall_console *c=state_console(context); int v,r; if (!c) {
      out_error(output, "command state unavailable"); return -1; }
  if (argc != 1 || strncmp(argv[0], "give+", 5) != 0 ||
      !num(argv[0] + 5, &v, &r) || r || v <= 0) {
      out_error(output, "usage: give+<positive amount>"); return -1;
  }
  c->give_requested = v; out(output, "money grant requested"); return 0;
}
static int command_clear(const struct rf_command_context *context,
                         struct rf_command_output *output, int argc, char **argv)
{ struct rasterfall_console *c=state_console(context); (void)output; (void)argc; (void)argv;
  if (c) c->output_count=0;
  return 0; }
static int command_help(const struct rf_command_context *context,
                        struct rf_command_output *output, int argc, char **argv)
{ unsigned int i, count; char line[192];
  const struct rasterfall_console_command *commands;
  (void)context; (void)argc; (void)argv; out(output,"COMMANDS");
  commands = rasterfall_console_commands(&count);
  for (i=0; i<count; i++) {
      snprintf(line, sizeof(line), "  %-12s %s", commands[i].name,
               commands[i].description);
      out(output, line);
  }
  return 0;
}
static int command_status(const struct rf_command_context *context,
                          struct rf_command_output *output, int argc, char **argv)
{ struct rf_core_status core_status; struct rf_game_runtime_status game_status;
  char line[192]; (void)argc; (void)argv;
  if (!context || !context->core || !context->game_runtime ||
      rf_core_get_status(context->core, &core_status) < 0 ||
      rf_game_runtime_get_status(context->game_runtime, &game_status) < 0) {
      out_error(output, "status unavailable"); return -1;
  }
  out(output, "CORE");
  snprintf(line, sizeof(line), "  window=%s filesystem=%s renderer=%s",
           core_status.window_ready ? "ready" : "not-ready",
           core_status.filesystem_ready ? "ready" : "not-ready",
           core_status.renderer_ready ? "ready" : "not-ready");
  out(output, line);
  snprintf(line, sizeof(line), "  clock=%s audio=%s initialized=%s",
           core_status.clock_ready ? "ready" : "not-ready",
           core_status.audio_ready ? "ready" : "not-ready",
           core_status.initialized ? "yes" : "no");
  out(output, line); out(output, "GAME");
  snprintf(line, sizeof(line), "  runtime=%s running=%s paused=%s session=%s network=%d",
           game_status.initialized ? "initialized" : "not-initialized",
           game_status.running ? "yes" : "no", game_status.paused ? "yes" : "no",
           game_status.session_active ? "active" : "inactive", game_status.network_mode);
  out(output, line);
  snprintf(line, sizeof(line), "  player=active:%s state:%d hp:%d",
           game_status.local_player_active ? "yes" : "no",
           game_status.local_player_state, game_status.local_player_hp);
  out(output, line);
  return 0;
}
static int command_pose(const struct rf_command_context *context,
                        struct rf_command_output *output, int argc, char **argv)
{ struct rasterfall_console *c=state_console(context); int character; const struct rasterfall_pose_calibration *profile;
  if (!c) { out_error(output, "command state unavailable"); return -1; }
  if (!(argc == 0 || (argc == 2 && (!strcmp(argv[0],"eula") ||
                                    !strcmp(argv[0],"maid")) && !strcmp(argv[1],"ak"))))
      { out_error(output,"unknown command; type help"); return -1; }
  character=argc==2&&!strcmp(argv[0],"maid")?1:0;
  profile=rasterfall_pose_calibration_resolve(NULL,character,TOY_GAME_WEAPON_AK);
  c->calibration.active=1;c->calibration.character=character;c->calibration.weapon=TOY_GAME_WEAPON_AK;
  memcpy(&c->calibration.pose,profile,sizeof(c->calibration.pose));c->calibration.left_ik=c->calibration.pose.left_ik;
  c->calibration.axes=1;c->calibration.anchors=1;c->calibration.upper_body_lock=1;c->calibration.animation_base=0;
  c->calibration.animation_playing=0;c->pose_hud_request=1;c->close_requested=1;
  out(output,character?"Rifle Pose Editor: Maid + AK":"Rifle Pose Editor: Eula + AK"); return 0;
}
static const struct rasterfall_console_command command_registry[] = {
    { "help", command_help, "show command groups", RF_COMMAND_PERMISSION_USER },
    { "clear", command_clear, "clear console log", RF_COMMAND_PERMISSION_USER },
    { "status", command_status, "show Core and Game status", RF_COMMAND_PERMISSION_USER },
    { "killall", command_killall, "kill all active enemies", RF_COMMAND_PERMISSION_ADMIN },
    { "give+", command_give, "add positive money", RF_COMMAND_PERMISSION_ADMIN },
    { "pose", command_pose, "open rifle pose editor", RF_COMMAND_PERMISSION_ADMIN }
};
const struct rasterfall_console_command *rasterfall_console_commands(unsigned int *count)
{ if (count) *count = sizeof(command_registry) / sizeof(command_registry[0]); return command_registry; }
int rf_terminal_session_execute(struct rf_terminal_session *session,
                                const struct rf_command_context *context)
{ char *w[6],line[RF_TERMINAL_INPUT_MAX]; unsigned int i,count; int n, result = 0;
  const struct rasterfall_console_command *commands;
  if (!session) return -1;
  strcpy(line, session->input); n=words(line,w,6); if(!n) {
      rf_command_output_init(&session->output); return 0;
  }
  commands=rasterfall_console_commands(&count);
  rf_command_output_init(&session->output);
  for (i=0; i<count; i++) {
      if (!strcmp(w[0], commands[i].name) ||
          (!strcmp(commands[i].name, "give+") && !strncmp(w[0], "give+", 5))) {
          if (context && context->permission_level < commands[i].permission) {
              out_error(&session->output, "permission denied"); break;
          }
          if (!strcmp(commands[i].name, "give+")) {
              result = commands[i].handler(context, &session->output, 1, w); break;
          }
          result = commands[i].handler(context, &session->output, n - 1, w + 1); break;
      }
  }
  if (i == count) out_error(&session->output,"unknown command; type help");
  return i == count ? -1 : result;
}
static void execute(struct rasterfall_console *c,
                    const struct rf_command_context *context)
{ struct rf_terminal_session session; unsigned int j;
  rf_terminal_session_init(&session);
  rf_terminal_session_set_input(&session, c->line);
  rf_terminal_session_execute(&session, context);
  for (j=0; j<session.output.count; j++)
      rasterfall_console_log(c,
          session.output.lines[j].level == RF_COMMAND_OUTPUT_ERROR ?
              RASTERFALL_CONSOLE_ERROR : RASTERFALL_CONSOLE_INFO,
          session.output.lines[j].text);
}
void rasterfall_console_init(struct rasterfall_console *c){memset(c,0,sizeof(*c));rasterfall_calibration_init(&c->calibration);}
int rasterfall_console_handle_input_context(struct rasterfall_console *c,struct rf_input_frame *in,unsigned char *pending,const struct rf_command_context *context){int k,ch,len,i;if(take(in,pending,KEY_ESC)){c->open=0;return 1;}if(take(in,pending,KEY_ENTER)){if(c->line[0]){for(i=7;i>0;i--)strcpy(c->history[i],c->history[i-1]);strcpy(c->history[0],c->line);rasterfall_console_log(c,RASTERFALL_CONSOLE_COMMAND,c->line);}c->history_cursor=0;execute(c,context);c->line[0]=0;return 1;}if(take(in,pending,KEY_BACKSPACE)){len=strlen(c->line);if(len)c->line[len-1]=0;return 1;}if(take(in,pending,KEY_UP)){if(c->history_cursor<8&&c->history[c->history_cursor][0]){strcpy(c->line,c->history[c->history_cursor]);c->history_cursor++;}return 1;}if(take(in,pending,KEY_DOWN)){if(c->history_cursor>1)c->history_cursor--;else c->history_cursor=0;if(c->history_cursor==0)c->line[0]=0;else strcpy(c->line,c->history[c->history_cursor-1]);return 1;}for(k=0;k<RF_INPUT_KEY_COUNT;k++)if((ch=chr(k))&&take(in,pending,k)){len=strlen(c->line);if(len<159){c->line[len]=ch;c->line[len+1]=0;}return 1;}return c->open;}
int rasterfall_console_handle_input(struct rasterfall_console *c,struct rf_input_frame *in,unsigned char *pending)
{ return rasterfall_console_handle_input_context(c, in, pending, NULL); }
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
int rasterfall_console_logic_test(void){struct rasterfall_console c; struct rf_terminal_session t; rasterfall_console_init(&c); rf_terminal_session_init(&t); if(c.calibration.weapon!=TOY_GAME_WEAPON_AK)return 1; if(rf_terminal_session_set_input(&t,"help")<0||rf_terminal_session_execute(&t,NULL)<0||t.output.count<2)return 2; rf_terminal_session_set_input(&t,"status"); if(rf_terminal_session_execute(&t,NULL)==0)return 3; return rasterfall_calibration_logic_test();}
