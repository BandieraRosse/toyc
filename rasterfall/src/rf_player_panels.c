#include "tlibc_everything.h"
#include "rf_player_panels.h"
#include "rf_game_lifecycle.h"
#include "rf_input_bindings.h"
#include "rf_weaver_blueprints_generated.h"
#include "rf_ui_font.h"
#include "rf_device_commands.h"
#include "string.h"

static int p_min(int a,int b) { return a<b?a:b; }
static int p_max(int a,int b) { return a>b?a:b; }
static int p_px(int n,int scale) { return (n*scale+500)/1000; }
static struct rf_ui_rect p_rect(int x,int y,int w,int h)
{ struct rf_ui_rect r={x,y,w,h};return r; }

/* Four opaque strips leave the GPU image fully visible. The same layout
 * object is used for GPU viewport, painting and pointer hit tests. */
static void p_panel_hole(struct rasterfall_canvas *c,struct rf_ui_rect r,
                         struct rf_ui_rect hole,const struct rf_ui_theme *theme)
{
    int bottom=r.y+r.h,right=r.x+r.w;
    rasterfall_canvas_rect(c,r.x,r.y,r.w,hole.y-r.y,theme->panel,theme->panel_alpha);
    rasterfall_canvas_rect(c,r.x,hole.y,hole.x-r.x,hole.h,theme->panel,theme->panel_alpha);
    rasterfall_canvas_rect(c,hole.x+hole.w,hole.y,right-hole.x-hole.w,hole.h,theme->panel,theme->panel_alpha);
    rasterfall_canvas_rect(c,r.x,hole.y+hole.h,r.w,bottom-hole.y-hole.h,theme->panel,theme->panel_alpha);
    rasterfall_canvas_rect(c,r.x,r.y,r.w,1,theme->border,255);
    rasterfall_canvas_rect(c,r.x,r.y+r.h-1,r.w,1,theme->border,255);
    rasterfall_canvas_rect(c,r.x,r.y,1,r.h,theme->border,255);
    rasterfall_canvas_rect(c,r.x+r.w-1,r.y,1,r.h,theme->border,255);
    rasterfall_canvas_rect(c,hole.x-1,hole.y-1,hole.w+2,1,theme->border,255);
    rasterfall_canvas_rect(c,hole.x-1,hole.y+hole.h,hole.w+2,1,theme->border,255);
    rasterfall_canvas_rect(c,hole.x-1,hole.y,1,hole.h,theme->border,255);
    rasterfall_canvas_rect(c,hole.x+hole.w,hole.y,1,hole.h,theme->border,255);
}

void rf_player_weaver_layout(struct rf_player_weaver_rects *o,int width,int height,
                             const struct rf_player_ui_state *ui)
{
    struct rf_ui_layout shared;
    int scale,pad,gap,w,h,x,y,header,footer,left,right,middle,content_y,content_h,row;
    memset(o,0,sizeof(*o));
    rf_ui_layout_resolve(&shared,ui,width,height,0);
    scale=shared.scale_milli;
    scale=p_min(scale,p_max(1000,(height-40)*1000/580));
    o->scale_milli=shared.text_scale_milli;o->padding=pad=p_px(12,scale);gap=p_px(12,scale);
    w=p_min(width-24,p_px(1120,scale));h=p_min(height-24,p_px(580,scale));
    x=(width-w)/2;y=(height-h)/2;
    header=p_max(p_px(52,scale),p_px(30,o->scale_milli)+pad*2);footer=p_px(62,scale);
    o->window=p_rect(x,y,w,h);
    left=p_min(p_px(220,scale),w*23/100);
    right=p_min(p_px(300,scale),w*32/100);
    middle=w-left-right-pad*2-gap*2;
    content_y=y+header;content_h=h-header-footer;
    o->catalog=p_rect(x+pad,content_y,left,content_h);
    o->conditions=p_rect(x+w-pad-right,content_y,right,content_h);
    o->preview=p_rect(o->catalog.x+left+gap,content_y+p_px(37,scale),middle,
        p_min(content_h-p_px(111,scale),middle*9/16));
    /* The shared auxiliary target is 16:9; keep destination aspect identical
     * so changing the window width does not stretch the real model. */
    if (o->preview.h<middle*9/16) {
        o->preview.w=o->preview.h*16/9;
        o->preview.x+=(middle-o->preview.w)/2;
    }
    o->status=p_rect(x+pad,y+h-footer+gap,w-pad*2,footer-gap-pad);
    row=p_min(p_px(67,scale),(content_h-p_px(40,o->scale_milli))/(int)RF_WEAVER_BLUEPRINT_COUNT);
    for (int i=0;i<(int)RF_WEAVER_BLUEPRINT_COUNT && i<RF_PLAYER_BLUEPRINT_ROWS;++i)
        o->blueprints[i]=p_rect(o->catalog.x+pad,o->catalog.y+p_px(40,o->scale_milli)+i*row,
            left-pad*2,row-p_px(6,scale));
    o->buttons[RF_WEAVER_HIT_CLOSE]=p_rect(x+w-pad-p_px(64,o->scale_milli),y+pad,
        p_px(64,o->scale_milli),p_px(28,o->scale_milli));
    o->buttons[RF_WEAVER_HIT_MINIMIZE]=o->buttons[RF_WEAVER_HIT_CLOSE];
    o->buttons[RF_WEAVER_HIT_MINIMIZE].x-=gap+o->buttons[RF_WEAVER_HIT_CLOSE].w;
    o->buttons[RF_WEAVER_HIT_TERMINAL]=o->buttons[RF_WEAVER_HIT_MINIMIZE];
    o->buttons[RF_WEAVER_HIT_TERMINAL].x-=gap+o->buttons[RF_WEAVER_HIT_CLOSE].w;
    {
        int bx=o->conditions.x+pad,bw=right-pad*2;
        int small=(bw-gap*2)/3,bottom=content_y+content_h-pad;
        o->buttons[RF_WEAVER_HIT_START]=p_rect(bx,bottom-p_px(40,scale),bw,p_px(40,scale));
        o->buttons[RF_WEAVER_HIT_COLLECT]=o->buttons[RF_WEAVER_HIT_START];
        o->buttons[RF_WEAVER_HIT_DETAILS]=p_rect(bx,bottom-p_px(76,scale),bw,p_px(28,scale));
        for (int i=0;i<3;++i) o->buttons[RF_WEAVER_HIT_POWER+i]=p_rect(bx+i*(small+gap),
            bottom-p_px(112,scale),small,p_px(28,scale));
    }
    o->buttons[RF_WEAVER_HIT_ROTATE_LEFT]=p_rect(o->preview.x,
        o->preview.y+o->preview.h+pad,p_min(o->preview.w/2-gap,p_px(130,scale)),p_px(28,scale));
    o->buttons[RF_WEAVER_HIT_ROTATE_RIGHT]=o->buttons[RF_WEAVER_HIT_ROTATE_LEFT];
    o->buttons[RF_WEAVER_HIT_ROTATE_RIGHT].x=o->preview.x+o->preview.w-o->buttons[RF_WEAVER_HIT_ROTATE_RIGHT].w;
}

int rf_player_weaver_hit(const struct rf_player_weaver_rects *o,int x,int y)
{
    if (!o) return RF_WEAVER_HIT_NONE;
    /* Start/collect share a slot; the runtime resolves the authority phase. */
    for (int i=1;i<RF_WEAVER_HIT_COUNT;++i)
        if (i!=RF_WEAVER_HIT_COLLECT && rf_ui_rect_contains(o->buttons[i],x,y)) return i;
    for (int i=0;i<(int)RF_WEAVER_BLUEPRINT_COUNT && i<RF_PLAYER_BLUEPRINT_ROWS;++i)
        if (rf_ui_rect_contains(o->blueprints[i],x,y)) return RF_WEAVER_HIT_BLUEPRINT_BASE+i;
    return RF_WEAVER_HIT_NONE;
}

static const char *p_phase(int phase)
{
    switch (phase) {
    case TOY_WEAVER_CALIBRATING:return "校准";
    case TOY_WEAVER_WEAVING:return "制造中";
    case TOY_WEAVER_DELIVERING:return "出料中";
    case TOY_WEAVER_READY:return "制造完成 · 等待领取";
    default:return "待机 · 选择蓝图";
    }
}

static const char *p_reason(int reason)
{
    switch (reason) {
    case TOY_WEAVER_DISABLED:return "设备不可用";
    case TOY_WEAVER_NO_POWER:return "等待电源与可用能源";
    case TOY_WEAVER_NO_COMPUTE:return "等待计算设备连接";
    case TOY_WEAVER_NO_STORAGE:return "可用存储不足";
    case TOY_WEAVER_BAD_BLUEPRINT:return "此蓝图尚不支持制造";
    case TOY_WEAVER_TOO_LARGE:return "物品超出制造空间";
    case TOY_WEAVER_BUSY:return "设备正在制造其他物品";
    case TOY_WEAVER_OUTPUT_OCCUPIED:return "请先领取出料台成品";
    case TOY_WEAVER_NO_OUTPUT:return "当前没有可领取成品";
    default:return "";
    }
}

static void p_value(struct rasterfall_canvas *c,struct rf_ui_rect area,int row,
                    int scale,const char *text,unsigned color)
{
    int pad=p_px(12,scale);
    rf_ui_text(c,p_rect(area.x+pad,area.y+pad+row*p_px(26,scale),
        area.w-pad*2,p_px(20,scale)),text,color,scale,1);
}

void rf_player_weaver_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *runtime,
                           int focus,int pointer_x,int pointer_y,int confirm_replace)
{
    struct rf_player_weaver_rects o;
    struct rf_player_weaver_query query;
    const struct rf_ui_theme *theme=&runtime->player_ui.theme;
    const struct rf_weaver_blueprint *blueprint;
    char line[256];
    int scale,pad,hover;
    if (!runtime->session) return;
    rf_player_weaver_layout(&o,c->width,c->height,&runtime->player_ui);
    rf_player_weaver_query(runtime,&query);
    blueprint=&rf_weaver_blueprints[query.selected];scale=o.scale_milli;pad=o.padding;
    hover=rf_player_weaver_hit(&o,pointer_x,pointer_y);
    p_panel_hole(c,o.window,o.preview,theme);
    rf_ui_text(c,p_rect(o.window.x+pad,o.window.y+pad,
        o.buttons[RF_WEAVER_HIT_TERMINAL].x-o.window.x-pad*2,p_px(24,scale)),
        "网格编织机 / MESH WEAVER",theme->text,scale,1);
    rf_ui_panel(c,o.catalog,theme,0);p_value(c,o.catalog,0,scale,"蓝图库",theme->accent);
    for (int i=0;i<(int)RF_WEAVER_BLUEPRINT_COUNT && i<RF_PLAYER_BLUEPRINT_ROWS;++i) {
        const struct rf_weaver_blueprint *item=&rf_weaver_blueprints[i];
        struct rf_ui_rect r=o.blueprints[i];
        int selected=i==query.selected;
        rf_ui_panel(c,r,theme,selected || hover==RF_WEAVER_HIT_BLUEPRINT_BASE+i ||
            focus==RF_WEAVER_HIT_BLUEPRINT_BASE+i);
        rf_ui_text(c,p_rect(r.x+pad,r.y+p_px(6,scale),r.w-pad*2,p_px(20,scale)),
            item->name,selected?theme->selected:theme->text,scale,1);
        if (r.h>=p_px(46,scale))
            rf_ui_text(c,p_rect(r.x+pad,r.y+p_px(25,scale),r.w-pad*2,r.h-p_px(25,scale)),
                item->manufacturable&&item->size_supported?"可制造":"条件未满足",
                item->manufacturable&&item->size_supported?theme->success:theme->muted,scale,1);
    }
    rf_ui_text(c,p_rect(o.preview.x,o.preview.y-p_px(29,scale),o.preview.w,p_px(20,scale)),
        query.name,theme->text,scale,1);
    if (!runtime->ui_video_live) {
        rasterfall_canvas_rect(c,o.preview.x,o.preview.y,o.preview.w,o.preview.h,theme->panel_raised,255);
        rf_ui_text(c,p_rect(o.preview.x+pad,o.preview.y+o.preview.h/2-p_px(18,scale),
            o.preview.w-pad*2,p_px(42,scale)),runtime->ui_video_state==3?
            "预览不可用\n制造条件仍以设备状态为准":"模型预览连接中\n正在准备真实资产",theme->muted,scale,2);
    }
    rf_ui_button(c,o.buttons[RF_WEAVER_HIT_ROTATE_LEFT],theme,"< 旋转",scale,
        hover==RF_WEAVER_HIT_ROTATE_LEFT || focus==RF_WEAVER_HIT_ROTATE_LEFT,1);
    rf_ui_button(c,o.buttons[RF_WEAVER_HIT_ROTATE_RIGHT],theme,"旋转 >",scale,
        hover==RF_WEAVER_HIT_ROTATE_RIGHT || focus==RF_WEAVER_HIT_ROTATE_RIGHT,1);
    rf_ui_text(c,p_rect(o.preview.x,o.buttons[RF_WEAVER_HIT_ROTATE_LEFT].y+p_px(38,scale),
        o.preview.w,p_px(38,scale)),"选择与预览不会开始制造",theme->muted,scale,2);
    rf_ui_panel(c,o.conditions,theme,0);
    p_value(c,o.conditions,0,scale,"制造条件与操作",theme->accent);
    if (query.estimate_seconds>=0) snprintf(line,sizeof(line),"预计时间   %.1f 秒",query.estimate_seconds);
    else snprintf(line,sizeof(line),"预计时间   等待设备连接");
    p_value(c,o.conditions,1,scale,line,theme->text);
    snprintf(line,sizeof(line),"预计消耗   %.1f kJ",query.expected_kj);
    p_value(c,o.conditions,2,scale,line,theme->text);
    snprintf(line,sizeof(line),"可用能源   %.1f kJ",query.available_kj);
    p_value(c,o.conditions,3,scale,line,query.available_kj<query.expected_kj?theme->warning:theme->success);
    snprintf(line,sizeof(line),"初始装弹   %d 发",query.rounds);
    p_value(c,o.conditions,4,scale,line,theme->text);
    snprintf(line,sizeof(line),"%s",query.reason?p_reason(query.reason):p_phase(query.phase));
    p_value(c,o.conditions,5,scale,line,query.reason?theme->warning:theme->accent);
    if (query.phase!=TOY_WEAVER_IDLE) {
        snprintf(line,sizeof(line),"进度 %.1f%%",query.progress*100);
        if (query.remaining_seconds>=0) snprintf(line,sizeof(line),"进度 %.1f%% · 余 %.1f 秒",query.progress*100,query.remaining_seconds);
        p_value(c,o.conditions,6,scale,line,theme->text);
    } else if (!query.near) p_value(c,o.conditions,6,scale,"请靠近设备后操作",theme->warning);
    else if (!blueprint->manufacturable || !blueprint->size_supported)
        p_value(c,o.conditions,6,scale,"蓝图不满足成型或尺寸条件",theme->warning);
    if (runtime->player_controls.details) {
        struct rf_ui_rect detail=p_rect(o.preview.x,o.preview.y+pad,o.preview.w,p_px(148,scale));
        /* Explicit engineering overlay may cover the preview while open. */
        rf_ui_panel(c,detail,theme,0);
        snprintf(line,sizeof(line),"顶点 %u  三角形 %u",blueprint->geometry.vertex_count,blueprint->geometry.triangle_count);
        p_value(c,detail,0,scale,line,theme->text);
        snprintf(line,sizeof(line),"体积 %llu mm3",blueprint->geometry.volume_mm3);p_value(c,detail,1,scale,line,theme->muted);
        snprintf(line,sizeof(line),"工作量 %.1f",query.cost.work);p_value(c,detail,2,scale,line,theme->muted);
        snprintf(line,sizeof(line),"成型能耗 %.1f kJ",query.cost.form_kj);p_value(c,detail,3,scale,line,theme->muted);
        snprintf(line,sizeof(line),"累计消耗 %.1f kJ",query.used_kj);p_value(c,detail,4,scale,line,theme->muted);
    }
    for (int i=0;i<3;++i) {
        int hit=RF_WEAVER_HIT_POWER+i;
        int active=i==0?query.power_on:i==1?query.cpu_on:query.x1_on;
        snprintf(line,sizeof(line),"%s%s",i==0?"电源":i==1?"CPU":"X1",active?"+":"-");
        rf_ui_button(c,o.buttons[hit],theme,line,scale,active||focus==hit,query.available&&query.near);
    }
    rf_ui_button(c,o.buttons[RF_WEAVER_HIT_DETAILS],theme,
        runtime->player_controls.details?"收起工程详情":"工程详情",scale,
        runtime->player_controls.details||focus==RF_WEAVER_HIT_DETAILS,1);
    if (query.phase==TOY_WEAVER_READY) rf_ui_button(c,o.buttons[RF_WEAVER_HIT_COLLECT],theme,
        confirm_replace?"确认替换并领取":"领取成品",scale,1,query.can_collect);
    else rf_ui_button(c,o.buttons[RF_WEAVER_HIT_START],theme,
        query.phase==TOY_WEAVER_IDLE?"开始制造":"制造进行中",scale,1,query.can_start);
    rf_ui_button(c,o.buttons[RF_WEAVER_HIT_CLOSE],theme,"关闭",scale,hover==RF_WEAVER_HIT_CLOSE||focus==RF_WEAVER_HIT_CLOSE,1);
    rf_ui_button(c,o.buttons[RF_WEAVER_HIT_MINIMIZE],theme,"收起",scale,hover==RF_WEAVER_HIT_MINIMIZE||focus==RF_WEAVER_HIT_MINIMIZE,1);
    rf_ui_button(c,o.buttons[RF_WEAVER_HIT_TERMINAL],theme,"终端",scale,hover==RF_WEAVER_HIT_TERMINAL||focus==RF_WEAVER_HIT_TERMINAL,1);
    if (confirm_replace && query.replace_weapon>=0)
        snprintf(line,sizeof(line),"领取将替换当前武器：%s。再次点击确认。",toy_game_weapon_name(query.replace_weapon));
    else if (runtime->player_controls.last.message[0])
        snprintf(line,sizeof(line),"%s",runtime->player_controls.last.message);
    else snprintf(line,sizeof(line),"%s",query.phase==TOY_WEAVER_IDLE?
        "选择蓝图，检查时间与能源，然后开始制造。":"收起或关闭窗口后，真实制造继续运行。");
    rf_ui_text(c,o.status,line,confirm_replace?theme->warning:
        runtime->player_controls.last.code?theme->warning:theme->muted,scale,2);
    {
        struct rf_ui_rect selected={0};
        if (focus>=RF_WEAVER_HIT_BLUEPRINT_BASE &&
            focus<RF_WEAVER_HIT_BLUEPRINT_BASE+(int)RF_WEAVER_BLUEPRINT_COUNT)
            selected=o.blueprints[focus-RF_WEAVER_HIT_BLUEPRINT_BASE];
        else if (focus>0 && focus<RF_WEAVER_HIT_COUNT) selected=o.buttons[focus];
        if (selected.w>0) {
            rasterfall_canvas_rect(c,selected.x-2,selected.y-2,selected.w+4,2,theme->success,255);
            rasterfall_canvas_rect(c,selected.x-2,selected.y+selected.h,selected.w+4,2,theme->success,255);
            rasterfall_canvas_rect(c,selected.x-2,selected.y,2,selected.h,theme->success,255);
            rasterfall_canvas_rect(c,selected.x+selected.w,selected.y,2,selected.h,theme->success,255);
        }
    }
}

void rf_player_weaver_card(struct rasterfall_canvas *c,const struct rf_game_runtime *runtime)
{
    struct rf_player_weaver_query q;
    struct rf_ui_layout layout;
    const struct rf_ui_theme *theme=&runtime->player_ui.theme;
    struct rf_ui_rect r;
    char line[128];
    rf_player_weaver_query(runtime,&q);
    if (!q.available || q.phase==TOY_WEAVER_IDLE) return;
    rf_ui_layout_resolve(&layout,&runtime->player_ui,c->width,c->height,runtime->rts_active);
    r=p_rect(c->width-layout.margin-p_px(260,layout.scale_milli),
        c->height-p_px(runtime->rts_active?300:252,layout.scale_milli),
        p_px(260,layout.scale_milli),p_px(102,layout.scale_milli));
    rf_ui_panel(c,r,theme,0);
    p_value(c,r,0,layout.text_scale_milli,"网格编织机",theme->text);
    snprintf(line,sizeof(line),"%s  %.1f%%",q.reason?p_reason(q.reason):p_phase(q.phase),q.progress*100);
    p_value(c,r,1,layout.text_scale_milli,line,q.reason?theme->warning:theme->accent);
    snprintf(line,sizeof(line),"%s",q.phase==TOY_WEAVER_READY?"返回设备领取成品":"靠近设备可重新展开");
    p_value(c,r,2,layout.text_scale_milli,line,theme->muted);
}

void rf_player_comms_layout(struct rf_player_comms_rects *o,int width,int height,
                            const struct rf_player_ui_state *ui,int compact,int collapsed)
{
    struct rf_ui_layout layout;
    int scale,geometry,pad,gap,w,x,y,header,video_w,video_h,text_w,choice_h,available,footer_h,content_h;
    memset(o,0,sizeof(*o));
    rf_ui_layout_resolve(&layout,ui,width,height,0);
    scale=layout.text_scale_milli;o->scale_milli=scale;
    y=layout.margin+p_px(48,scale);
    available=p_max(240,p_min(layout.weapon.y,
        height-layout.margin-p_min(p_px(ui->layout.rts_height,scale),height/3))-y-layout.gap);
    geometry=p_min(scale,available*1000/340);
    pad=o->padding=p_px(12,geometry);gap=p_px(12,geometry);
    header=p_max(p_px(36,geometry),p_px(30,scale));
    w=p_min(width-layout.margin*2,p_px(compact?350:560,scale));
    if (compact) w=p_min(w,width/2-layout.margin);
    else w=p_min(w,width-layout.margin-layout.map.x-
        p_min(layout.map.w,p_px(ui->layout.map_size,layout.scale_milli))-layout.gap);
    x=width-layout.margin-w;
    if (collapsed) {
        w=p_min(w,p_px(350,scale));x=width-layout.margin-w;
        o->window=p_rect(x,y,w,p_px(40,scale));
        o->collapse=o->window;return;
    }
    video_w=p_px(compact?128:224,geometry);
    if (!compact && w<p_px(560,scale)) video_w=p_min(video_w,w/3);
    video_h=video_w*9/16;
    choice_h=p_max(p_px(compact?29:33,geometry),p_px(24,scale));
    footer_h=p_max(p_px(36,geometry),p_px(24,scale));
    content_h=p_max(p_px(20,scale),available-header-choice_h*3-p_px(5,geometry)*2-gap*2-footer_h-pad);
    if (video_h>content_h) {video_h=content_h;video_w=video_h*16/9;}
    text_w=w-video_w-pad*2-gap;
    o->video=p_rect(x+pad,y+header,video_w,video_h);
    o->text=p_rect(o->video.x+video_w+gap,y+header,text_w,p_min(content_h,p_max(video_h,p_px(60,scale))));
    if (text_w<p_px(150,scale) && !compact) {
        o->video.w=p_min(w-pad*2,p_px(288,scale));o->video.h=o->video.w*9/16;
        o->text=p_rect(x+pad,o->video.y+o->video.h+gap,w-pad*2,p_px(72,scale));
    }
    {
        int choices_y=p_max(o->video.y+o->video.h,o->text.y+o->text.h)+gap;
        for (int i=0;i<3;++i) o->choices[i]=p_rect(x+pad,choices_y+i*(choice_h+p_px(5,geometry)),w-pad*2,choice_h);
        o->footer=p_rect(x+pad,o->choices[2].y+choice_h+gap,w-pad*2,footer_h);
        o->window=p_rect(x,y,w,o->footer.y+o->footer.h+pad-y);
    }
    o->close=p_rect(x+w-pad-p_px(32,scale),y+p_px(6,scale),p_px(32,scale),p_px(24,scale));
    o->collapse=p_rect(o->close.x-gap-p_px(32,scale),o->close.y,p_px(32,scale),p_px(24,scale));
    o->history=p_rect(o->collapse.x-gap-p_px(56,scale),o->close.y,p_px(56,scale),p_px(24,scale));
}

int rf_player_comms_hit(const struct rf_player_comms_rects *o,int x,int y,int choice_count)
{
    if (rf_ui_rect_contains(o->collapse,x,y)) return RF_COMMS_HIT_COLLAPSE;
    if (rf_ui_rect_contains(o->close,x,y)) return RF_COMMS_HIT_CLOSE;
    if (rf_ui_rect_contains(o->history,x,y)) return RF_COMMS_HIT_HISTORY;
    for (int i=0;i<choice_count && i<3;++i)
        if (rf_ui_rect_contains(o->choices[i],x,y)) return RF_COMMS_HIT_CHOICE_BASE+i;
    if (!choice_count && rf_ui_rect_contains(o->choices[0],x,y)) return RF_COMMS_HIT_CONTINUE;
    return RF_COMMS_HIT_NONE;
}

static void p_key(const struct rf_input_bindings *bindings,int action,char *key,unsigned capacity)
{
    struct rf_input_bindings defaults;
    if (!bindings) { rf_input_bindings_defaults(&defaults);bindings=&defaults; }
    rf_input_action_label(bindings,(enum rf_input_action)action,key,capacity);
}

void rf_player_comms_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *runtime,
                          const struct rf_input_bindings *bindings)
{
    const struct rf_story *story=&runtime->story;
    const struct rf_story_node *node=rf_story_current_node(story);
    const struct rf_ui_theme *theme=&runtime->player_ui.theme;
    struct rf_player_comms_rects o;
    char line[256],key[24],hide[24],answer[24];
    int scale;
    if (!node) return;
    rf_player_comms_layout(&o,c->width,c->height,&runtime->player_ui,story->combat,story->collapsed);
    scale=o.scale_milli;
    if (!story->collapsed && node->choice_count<3) {
        int rows=node->choice_count>0?node->choice_count:1;
        int spare=(3-rows)*(o.choices[1].y-o.choices[0].y);
        o.footer.y-=spare;o.window.h-=spare;
    }
    p_key(bindings,RF_ACTION_COMMS_FOCUS,key,sizeof(key));
    p_key(bindings,RF_ACTION_COMMS_HIDE,hide,sizeof(hide));
    p_key(bindings,RF_ACTION_COMMS_ANSWER,answer,sizeof(answer));
    if (story->collapsed) {
        rf_ui_panel(c,o.window,theme,0);
        snprintf(line,sizeof(line),"NULL 通讯已收起  [%s] 恢复",hide);
        rf_ui_text(c,p_rect(o.window.x+o.padding,o.window.y+o.padding,
            o.window.w-o.padding*2,p_px(18,scale)),line,theme->accent,scale,1);return;
    }
    p_panel_hole(c,o.window,o.video,theme);
    snprintf(line,sizeof(line),"NULL / %s",node->location);
    rf_ui_text(c,p_rect(o.window.x+o.padding,o.window.y+p_px(10,scale),
        o.history.x-o.window.x-o.padding-p_px(8,scale),p_px(18,scale)),line,theme->text,scale,1);
    rf_ui_button(c,o.collapse,theme,"-",scale,0,1);
    rf_ui_button(c,o.close,theme,"X",scale,0,1);
    rf_ui_button(c,o.history,theme,"记录",scale,0,1);
    if (!runtime->ui_video_live || story->link!=RF_STORY_LINK_LIVE) {
        const char *status=runtime->ui_video_state==3?"视频不可用\n对话仍可继续":
            story->link==RF_STORY_LINK_UNAVAILABLE?"镜头不可用":
            story->link==RF_STORY_LINK_INTERRUPTED?"连接已中断":"正在连接真实镜头";
        rasterfall_canvas_rect(c,o.video.x,o.video.y,o.video.w,o.video.h,theme->panel_raised,255);
        rf_ui_text(c,p_rect(o.video.x+o.padding,o.video.y+o.video.h/2-p_px(10,scale),
            o.video.w-o.padding*2,p_px(44,scale)),status,theme->muted,scale,2);
    }
    rf_ui_text(c,o.text,node->line,theme->text,scale,story->combat?4:6);
    for (int i=0;i<node->choice_count;++i)
        rf_ui_button(c,o.choices[i],theme,node->choices[i].text,scale,
            runtime->comms_choice==i,runtime->comms_focus || story->combat);
    if (!node->choice_count) rf_ui_button(c,o.choices[0],theme,"继续",scale,runtime->comms_focus,1);
    snprintf(line,sizeof(line),"[%s] 回应  [%s] 确认  [%s] 收起",key,answer,hide);
    rf_ui_text(c,o.footer,line,theme->muted,scale,2);
}

void rf_player_terminal_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *runtime,
                             const struct rf_input_bindings *bindings)
{
    const struct rasterfall_console *console=&runtime->console;
    const struct rf_ui_theme *theme=&runtime->player_ui.theme;
    struct rf_ui_layout layout;
    struct rf_ui_rect window;
    int scale,pad,line_height,rows,start,y,used=0,text_width;
    char line[RF_TERMINAL_INPUT_MAX+24],key[24];
    rf_ui_layout_resolve(&layout,&runtime->player_ui,c->width,c->height,0);
    scale=layout.text_scale_milli;pad=layout.padding;line_height=p_px(22,scale);
    window=p_rect(layout.margin,p_px(72,scale),c->width-layout.margin*2,
        c->height-p_px(110,scale));
    rf_ui_window(c,window,theme,"RF TERMINAL / 玩家命令",scale);
    p_key(bindings,RF_ACTION_CONSOLE,key,sizeof(key));
    snprintf(line,sizeof(line),"[%s] 返回 | help / ui / weaver / rts / comms / task / notices / render / table / devices",key);
    rf_ui_text(c,p_rect(window.x+pad,window.y+p_px(48,scale),window.w-pad*2,
        p_px(22,scale)),line,theme->muted,scale,1);
    y=window.y+p_px(83,scale);
    rows=(window.h-p_px(138,scale))/line_height;
    text_width=p_max(1,window.w-pad*2);
    start=console->output_count;
    while (start>0) {
        int needed=p_min(3,p_max(1,(rf_ui_font_text_width(console->output[start-1].text,scale)+text_width-1)/text_width));
        if (used+needed>rows) break;
        used+=needed;--start;
    }
    for (int i=start;i<console->output_count && i<64;++i) {
        int needed=p_min(3,p_max(1,(rf_ui_font_text_width(console->output[i].text,scale)+text_width-1)/text_width));
        rf_ui_text(c,p_rect(window.x+pad,y,text_width,line_height*needed),
            console->output[i].text,console->output[i].color?console->output[i].color:theme->text,scale,needed);
        y+=line_height*needed;
    }
    rasterfall_canvas_rect(c,window.x+pad,window.y+window.h-p_px(43,scale),window.w-pad*2,1,theme->border,255);
    snprintf(line,sizeof(line),"> %s_",console->terminal.input);
    rf_ui_text(c,p_rect(window.x+pad,window.y+window.h-p_px(31,scale),window.w-pad*2,
        p_px(20,scale)),line,theme->accent,scale,1);
}

void rf_player_notice_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *runtime)
{
    const struct rf_player_controls *controls=&runtime->player_controls;
    const struct rf_ui_theme *theme=&runtime->player_ui.theme;
    const struct rf_player_notice *notice;
    struct rf_ui_layout layout;
    struct rf_ui_rect rect;
    unsigned color;
    int width,height,top;
    char title[96];
    if (controls->notice_ms<=0 || controls->notice_count<=0 || runtime->console.open) return;
    notice=&controls->notices[0];
    rf_ui_layout_resolve(&layout,&runtime->player_ui,c->width,c->height,runtime->rts_active);
    width=p_min(c->width-layout.margin*2,p_px(360,layout.scale_milli));
    height=p_px(96,layout.scale_milli);
    top=(runtime->rts_active?layout.selection.y:layout.weapon.y)-layout.gap-height;
    if (runtime->rts_active && runtime->player_ui.rts_collapsed)
        top=layout.dock_toggle.y-layout.gap-height;
    rect=p_rect(c->width-layout.margin-width,top,width,height);
    if (runtime->story.active_story && !runtime->story.collapsed) {
        struct rf_player_comms_rects comms;
        rf_player_comms_layout(&comms,c->width,c->height,&runtime->player_ui,runtime->story.combat,0);
        if (rect.y<comms.window.y+comms.window.h && rect.y+rect.h>comms.window.y)
            rect.x=layout.margin;
    }
    color=notice->kind>=3?theme->danger:notice->kind==2?theme->warning:
        notice->kind==1?theme->success:theme->accent;
    rf_ui_panel(c,rect,theme,0);
    rasterfall_canvas_rect(c,rect.x,rect.y,3,rect.h,color,255);
    snprintf(title,sizeof(title),"%s%s",notice->kind>=3?"危险":notice->kind==2?"需要处理":
        notice->kind==1?"已完成":"通知",controls->notice_count>1?" · 终端可查看记录":"");
    if (notice->count>1) snprintf(title,sizeof(title),"通知 · 相同事件 %d 次",notice->count);
    rf_ui_text(c,p_rect(rect.x+layout.padding,rect.y+layout.padding,
        rect.w-layout.padding*2,p_px(22,layout.text_scale_milli)),title,color,layout.text_scale_milli,1);
    rf_ui_text(c,p_rect(rect.x+layout.padding,rect.y+layout.padding+p_px(30,layout.text_scale_milli),
        rect.w-layout.padding*2,rect.h-layout.padding*2-p_px(30,layout.text_scale_milli)),
        notice->text,theme->text,layout.text_scale_milli,2);
}

void rf_player_device_layout(struct rf_player_device_rects *o,int width,int height,
                             const struct rf_player_ui_state *ui,int kind)
{
    struct rf_ui_layout shared;
    int scale,pad,gap,w,h,x,y,header,footer,row;
    memset(o,0,sizeof(*o));
    rf_ui_layout_resolve(&shared,ui,width,height,0);
    o->kind=kind;o->scale_milli=scale=shared.text_scale_milli;
    o->padding=pad=shared.padding;gap=shared.gap;
    w=p_min(width-24,p_px(920,shared.scale_milli));
    h=p_min(height-24,p_px(600,shared.scale_milli));
    x=(width-w)/2;y=(height-h)/2;
    header=p_px(92,scale);footer=p_px(kind==RF_PLAYER_DEVICE_TABLE?108:72,scale);
    o->window=p_rect(x,y,w,h);
    o->close=p_rect(x+w-pad-p_px(66,scale),y+pad,p_px(66,scale),p_px(30,scale));
    o->terminal=p_rect(o->close.x-gap-p_px(66,scale),y+pad,p_px(66,scale),p_px(30,scale));
    o->content=p_rect(x+pad,y+header,w-pad*2,p_max(1,h-header-footer));
    o->status=p_rect(x+pad,y+h-p_px(58,scale),w-pad*2,p_px(48,scale));
    if (kind==RF_PLAYER_DEVICE_TABLE) {
        row=o->content.h/3;
        for (int i=0;i<3;++i) o->maps[i]=p_rect(o->content.x,o->content.y+i*row,o->content.w,row-gap);
        o->deploy=p_rect(x+pad,o->content.y+o->content.h+gap,p_min(w-pad*2,p_px(280,scale)),p_px(32,scale));
    } else {
        row=o->content.h/6;
        for (int i=0;i<6;++i) o->features[i]=p_rect(o->content.x,o->content.y+i*row,o->content.w,row-gap/2);
    }
}

int rf_player_device_hit(const struct rf_player_device_rects *o,int x,int y)
{
    if (!o) return RF_DEVICE_HIT_NONE;
    if (rf_ui_rect_contains(o->close,x,y)) return RF_DEVICE_HIT_CLOSE;
    if (rf_ui_rect_contains(o->terminal,x,y)) return RF_DEVICE_HIT_TERMINAL;
    if (o->kind==RF_PLAYER_DEVICE_TABLE) {
        if (rf_ui_rect_contains(o->deploy,x,y)) return RF_DEVICE_HIT_DEPLOY;
        for (int i=0;i<RF_PLAYER_DEVICE_MAP_ROWS;++i)
            if (rf_ui_rect_contains(o->maps[i],x,y)) return RF_DEVICE_HIT_MAP_BASE+i;
    } else for (int i=0;i<RF_PLAYER_DEVICE_FEATURE_ROWS;++i)
        if (rf_ui_rect_contains(o->features[i],x,y)) return RF_DEVICE_HIT_FEATURE_BASE+i;
    return RF_DEVICE_HIT_NONE;
}

static void p_focus(struct rasterfall_canvas *c,struct rf_ui_rect r,unsigned color)
{
    if (r.w<=0 || r.h<=0) return;
    rasterfall_canvas_rect(c,r.x,r.y,r.w,2,color,255);
    rasterfall_canvas_rect(c,r.x,r.y+r.h-2,r.w,2,color,255);
    rasterfall_canvas_rect(c,r.x,r.y,2,r.h,color,255);
    rasterfall_canvas_rect(c,r.x+r.w-2,r.y,2,r.h,color,255);
}

void rf_player_device_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *runtime,
                           int kind,int focus)
{
    const struct rf_ui_theme *theme=&runtime->player_ui.theme;
    const struct rf_player_result *last=&runtime->device_service.last;
    struct rf_player_device_rects o;
    struct rf_device_query q;
    struct rf_ui_rect focused={0};
    int scale,pad;
    char line[224];
    rf_player_device_layout(&o,c->width,c->height,&runtime->player_ui,kind);
    rf_device_query(runtime,&q);scale=o.scale_milli;pad=o.padding;
    rf_ui_panel(c,o.window,theme,0);
    rf_ui_text(c,p_rect(o.window.x+pad,o.window.y+pad,o.terminal.x-o.window.x-pad*2,p_px(28,scale)),
        kind==RF_PLAYER_DEVICE_TABLE?"指挥桌 / 部署":"画面设置 / 渲染终端",theme->text,scale,1);
    rf_ui_button(c,o.close,theme,"关闭",scale,focus==RF_DEVICE_HIT_CLOSE,1);
    rf_ui_button(c,o.terminal,theme,"终端",scale,focus==RF_DEVICE_HIT_TERMINAL,1);
    snprintf(line,sizeof(line),"%s",kind==RF_PLAYER_DEVICE_TABLE?
        "选择地图，再确认部署；关闭窗口会保留选择。":
        q.scene_backend?"GPU Scene · 选择功能切换 · 支持状态由当前渲染器提供":
        "CPU · 选择功能查看当前支持状态");
    rf_ui_text(c,p_rect(o.window.x+pad,o.window.y+p_px(55,scale),o.window.w-pad*2,p_px(26,scale)),
        line,theme->muted,scale,1);
    if (kind==RF_PLAYER_DEVICE_TABLE) {
        for (int i=0;i<q.map_count && i<RF_PLAYER_DEVICE_MAP_ROWS;++i) {
            const struct rf_device_map *map=&q.maps[i];
            struct rf_ui_rect r=o.maps[i];
            int selected=i==q.selected_map;
            if (r.h<=0) continue;
            rf_ui_panel(c,r,theme,selected);
            rf_ui_text(c,p_rect(r.x+pad,r.y+pad,r.w-pad*2,p_px(23,scale)),map->name,
                selected?theme->selected:theme->text,scale,1);
            snprintf(line,sizeof(line),"%s  ·  %s",map->id,map->available?
                (selected?"已选择 · 等待确认部署":"可部署"):"当前不可用");
            rf_ui_text(c,p_rect(r.x+pad,r.y+pad+p_px(29,scale),r.w-pad*2,p_px(23,scale)),
                line,map->available?theme->muted:theme->warning,scale,1);
            if (focus==RF_DEVICE_HIT_MAP_BASE+i) focused=r;
        }
        rf_ui_button(c,o.deploy,theme,q.pending_world>=0?"部署请求待处理":"确认部署",scale,
            focus==RF_DEVICE_HIT_DEPLOY,q.deployable && q.pending_world<0);
        if (focus==RF_DEVICE_HIT_DEPLOY) focused=o.deploy;
    } else {
        for (int i=0;i<q.feature_count && i<RF_PLAYER_DEVICE_FEATURE_ROWS;++i) {
            const struct rf_device_feature *feature=&q.features[i];
            struct rf_ui_rect r=o.features[i];
            int status_w=p_min(r.w/2,p_px(330,scale));
            const char *support=feature->support==RF_DEVICE_FORMAL?"正式支持":
                feature->support==RF_DEVICE_EXPERIMENTAL?"实验支持":
                feature->support==RF_DEVICE_UNSUPPORTED?"当前后端不支持":"尚未实现";
            if (r.h<=0) continue;
            rf_ui_panel(c,r,theme,feature->enabled);
            rf_ui_text(c,p_rect(r.x+pad,r.y+(r.h-p_px(20,scale))/2,r.w-status_w-pad*2,p_px(22,scale)),
                feature->name,feature->adjustable?theme->text:theme->muted,scale,1);
            snprintf(line,sizeof(line),"%s · %s",feature->enabled?"开启":"关闭",support);
            rf_ui_text(c,p_rect(r.x+r.w-status_w,r.y+(r.h-p_px(20,scale))/2,status_w-pad,p_px(22,scale)),
                line,feature->support>=RF_DEVICE_UNSUPPORTED?theme->warning:
                feature->enabled?theme->success:theme->muted,scale,1);
            if (focus==RF_DEVICE_HIT_FEATURE_BASE+i) focused=r;
        }
    }
    if (focus==RF_DEVICE_HIT_CLOSE) focused=o.close;
    else if (focus==RF_DEVICE_HIT_TERMINAL) focused=o.terminal;
    p_focus(c,focused,theme->success);
    rf_ui_text(c,o.status,last->message[0]?last->message:
        kind==RF_PLAYER_DEVICE_TABLE?q.reason:"设置立即作用于当前画面；终端命令使用同一功能状态。",
        last->code?theme->warning:theme->muted,scale,2);
}
