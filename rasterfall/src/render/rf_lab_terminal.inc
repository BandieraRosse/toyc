/* Runtime writes display content; consumers copy it into a frame value.
 * Stable channel names decouple the screen from the control object model. */
#define RF_TERMINAL_CHANNEL_CAP 64
static struct { char channel[64],text[TOY_MAP_TEXT_SIZE]; } lab_terminal_channels[RF_TERMINAL_CHANNEL_CAP];
void rasterfall_render_terminal_set(const char *channel,const char *text)
{
    if (!channel || !text) return;
    for (int i=0;i<RF_TERMINAL_CHANNEL_CAP;++i) if (!lab_terminal_channels[i].channel[0] ||
        !strcmp(lab_terminal_channels[i].channel,channel)) {
        snprintf(lab_terminal_channels[i].channel,64,"%s",channel);
        snprintf(lab_terminal_channels[i].text,sizeof(lab_terminal_channels[i].text),"%s",text);return;
    }
}
void rasterfall_render_terminal_freeze(const struct rf_map_runtime_render *r,
    struct toy_map_draw *draw)
{
    const char *channel;
    if (!r || !draw || draw->type!=TOY_MAP_DRAW_SIGN || (draw->style!=4 && draw->style!=5)) return;
    channel=rf_map_runtime_render_attribute(r,"channel");
    if (!channel) return;
    for (int i=0;i<RF_TERMINAL_CHANNEL_CAP;++i) if (!strcmp(channel,lab_terminal_channels[i].channel)) {
        snprintf(draw->text,sizeof(draw->text),"%s",lab_terminal_channels[i].text);return;
    }
}
