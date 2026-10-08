/* Pause pages share layout, hit testing and sliders between CPU and GPU Scene. */
#define RF_AUDIO_MENU_RANGE RF_AUDIO_CONTROL_COUNT
#define RF_AUDIO_MENU_PREVIEW (RF_AUDIO_CONTROL_COUNT+1)
#define RF_AUDIO_MENU_DEFAULTS (RF_AUDIO_CONTROL_COUNT+2)
#define RF_AUDIO_MENU_BACK (RF_AUDIO_CONTROL_COUNT+3)
#define RF_AUDIO_MENU_COUNT (RF_AUDIO_CONTROL_COUNT+4)
struct rf_pause_layout {
    struct rf_ui_rect panel;
    int scale, padding, top, step, count;
};
static int rf_pause_px(int value,int scale) { return value*scale/1000; }
static void rf_pause_geometry(struct rf_pause_layout *out,
    const struct rf_player_ui_state *ui,int width,int height,int page)
{
    struct rf_ui_layout layout;
    int dw=page==RF_PAUSE_MAIN?520:660;
    int count=page==RF_PAUSE_AUDIO?RF_AUDIO_MENU_COUNT:
        page==RF_PAUSE_MAIN?PAUSE_ITEM_COUNT:SUBMENU_ITEM_COUNT;
    int dh=128+count*34;
    rf_ui_layout_resolve(&layout,ui,width,height,0);
    int scale=layout.text_scale_milli;
    if(scale>(width-24)*1000/dw)scale=(width-24)*1000/dw;
    if(scale>(height-24)*1000/dh)scale=(height-24)*1000/dh;
    if(scale<500)scale=500;
    int w=rf_pause_px(dw,scale),h=rf_pause_px(dh,scale);
    out->panel=(struct rf_ui_rect){(width-w)/2,(height-h)/2,w,h};
    out->scale=scale;out->padding=rf_pause_px(16,scale);
    out->top=out->panel.y+rf_pause_px(54,scale);
    out->step=rf_pause_px(34,scale);out->count=count;
}
static struct rf_ui_rect rf_pause_row(const struct rf_pause_layout *layout,int item)
{
    return (struct rf_ui_rect){layout->panel.x+layout->padding,
        layout->top+item*layout->step,layout->panel.w-2*layout->padding,
        layout->step-rf_pause_px(4,layout->scale)};
}
static int rf_pause_hit(const struct rf_pause_layout *layout,int x,int y)
{
    for(int i=0;i<layout->count;++i)
        if(rf_ui_rect_contains(rf_pause_row(layout,i),x,y))return i;
    return -1;
}
static struct rf_ui_rect rf_pause_bar(const struct rf_pause_layout *layout,int item)
{
    struct rf_ui_rect row=rf_pause_row(layout,item);
    int pad=rf_pause_px(12,layout->scale);
    return (struct rf_ui_rect){row.x+row.w/2,row.y,row.w/2-pad,row.h};
}
static void rf_pause_slider_draw(struct rasterfall_canvas *c,const struct rf_pause_layout *layout,
    const struct rf_ui_theme *theme,int item,int value,int maximum,int selected)
{
    struct rf_ui_rect bar=rf_pause_bar(layout,item);
    int h=rf_pause_px(6,layout->scale),knob=rf_pause_px(4,layout->scale);
    int y=bar.y+(bar.h-h)/2,fill=bar.w*value/maximum;
    rasterfall_canvas_rect(c,bar.x,y,bar.w,h,theme->border,255);
    rasterfall_canvas_rect(c,bar.x,y,fill,h,theme->accent,255);
    rasterfall_canvas_rect(c,bar.x+clampi(fill-knob/2,0,bar.w-knob),
        y-knob/2,knob,h+knob,selected?theme->selected:theme->text,255);
}
static void rf_pause_back(struct pause_menu *menu)
{
    int parent=menu->page==RF_PAUSE_AUDIO?PAUSE_ITEM_AUDIO:
        menu->page==RF_PAUSE_CONTROL?PAUSE_ITEM_CONTROL:PAUSE_ITEM_DEVELOPER;
    menu->page=RF_PAUSE_MAIN;menu->selected=parent;menu->slider_drag=-1;
}
static void rf_pause_slider_set(struct rasterfall_audio *audio,struct control_settings *settings,
    const struct pause_menu *menu,const struct rf_pause_layout *layout,int x)
{
    struct rf_ui_rect bar=rf_pause_bar(layout,menu->selected);
    int maximum=menu->page==RF_PAUSE_AUDIO?100:15;
    int position=clampi(x-bar.x,0,bar.w-1);
    int value=(position*maximum+(bar.w-1)/2)/(bar.w-1);
    int *target=menu->page==RF_PAUSE_AUDIO?&audio->settings.volume[menu->selected]:
        menu->selected==CONTROL_ITEM_MOUSE?&settings->mouse_level:&settings->keyboard_level;
    if(*target==value)return;
    *target=value;
    if(menu->page==RF_PAUSE_AUDIO)rasterfall_audio_settings_apply(audio,1);
    rasterfall_audio_ui(audio,RF_UI_SOUND_CHANGE);
}
static void rf_pause_footer(struct rasterfall_canvas *c,const struct rf_pause_layout *layout,
    const struct rf_ui_theme *theme,const char *help,const char *status,int error)
{
    int x=layout->panel.x+layout->padding,w=layout->panel.w-2*layout->padding;
    int y=layout->top+layout->count*layout->step+rf_pause_px(8,layout->scale);
    rf_ui_text(c,(struct rf_ui_rect){x,y,w,rf_pause_px(22,layout->scale)},
        help,theme->muted,layout->scale,1);
    rf_ui_text(c,(struct rf_ui_rect){x,y+rf_pause_px(24,layout->scale),w,rf_pause_px(22,layout->scale)},
        status,error?theme->danger:theme->accent,layout->scale,1);
}
static void rf_audio_menu_draw(struct rasterfall_canvas *c,
    const struct pause_menu *menu,const struct rasterfall_audio *a,
    const struct rf_player_ui_state *ui)
{
    static const char *labels[]={"总音量","音乐","游戏音效","枪械与爆炸",
        "自身枪声","环境与机器","战斗反馈","界面音效"};
    static const char *ranges[]={"宽广","标准","舒适"};
    struct rf_pause_layout layout;char text[96];
    const struct rf_ui_theme *theme=&ui->theme;
    rf_pause_geometry(&layout,ui,c->width,c->height,RF_PAUSE_AUDIO);
    rf_ui_window(c,layout.panel,theme,"音频设置",layout.scale);
    for(int i=0;i<RF_AUDIO_MENU_COUNT;++i) {
        struct rf_ui_rect row=rf_pause_row(&layout,i);
        if(i<RF_AUDIO_CONTROL_COUNT) {
            snprintf(text,sizeof(text),"%s  %d%%",labels[i],a->settings.volume[i]);
        } else if(i==RF_AUDIO_MENU_RANGE)
            snprintf(text,sizeof(text),"动态范围  < %s >",ranges[a->settings.range]);
        else snprintf(text,sizeof(text),"%s",i==RF_AUDIO_MENU_PREVIEW?"试听枪声与换弹":
            i==RF_AUDIO_MENU_DEFAULTS?"恢复默认音频设置":"返回暂停菜单");
        rf_ui_button(c,row,theme,text,layout.scale,i==menu->selected,1);
        if(i<RF_AUDIO_CONTROL_COUNT)
            rf_pause_slider_draw(c,&layout,theme,i,a->settings.volume[i],100,i==menu->selected);
    }
    rf_pause_footer(c,&layout,theme,"上下选择 · 左右调整 · 点击音量条",
        a->save_error?"保存失败 · 当前设置仍生效":a->persistence?
        "Esc 返回 · 设置自动保存":"Esc 返回 · 临时设置",a->save_error);
}
static void rf_audio_menu_change(struct rasterfall_audio *a,int item,int change)
{
    if(item<RF_AUDIO_CONTROL_COUNT)a->settings.volume[item]=clampi(a->settings.volume[item]+change*5,0,100);
    else if(item==RF_AUDIO_MENU_RANGE)a->settings.range=clampi(a->settings.range+change,RF_AUDIO_WIDE,RF_AUDIO_COMFORT);
    else return;
    rasterfall_audio_settings_apply(a,1);
    rasterfall_audio_ui(a,RF_UI_SOUND_CHANGE);
}
static void rf_audio_menu_confirm(struct rasterfall_audio *a,struct pause_menu *menu)
{
    if(menu->selected==RF_AUDIO_MENU_BACK) {
        rf_pause_back(menu);
        rasterfall_audio_ui(a,RF_UI_SOUND_EXIT);
    }
    else if(menu->selected==RF_AUDIO_MENU_DEFAULTS) {
        rf_audio_settings_default(&a->settings);rasterfall_audio_settings_apply(a,1);
        rasterfall_audio_ui(a,RF_UI_SOUND_CONFIRM);
    } else if(menu->selected==RF_AUDIO_MENU_PREVIEW) {
        rasterfall_audio_preview(a);
    } else if(menu->selected==RF_AUDIO_MENU_RANGE) {
        a->settings.range=(a->settings.range+1)%3;rasterfall_audio_settings_apply(a,1);
        rasterfall_audio_ui(a,RF_UI_SOUND_CHANGE);
    }
}
