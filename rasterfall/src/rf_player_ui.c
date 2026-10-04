#include "tlibc_everything.h"
#include "rf_player_ui.h"
#include "rasterfall_hud.h"
#include "rf_input_bindings.h"
#include "rasterfall_units.h"
#include "rf_ui_font.h"
#include "string.h"

static int ui_min(int a,int b) { return a<b?a:b; }
static int ui_max(int a,int b) { return a>b?a:b; }
static int ui_clamp(int n,int a,int b) { return n<a?a:n>b?b:n; }
static int ui_px(int n,int scale) { return (n*scale+500)/1000; }
static struct rf_ui_rect ui_rect(int x,int y,int w,int h)
{ struct rf_ui_rect rect={x,y,w,h};return rect; }

void rf_player_ui_init(struct rf_player_ui_state *state)
{
    static const struct rf_ui_theme theme={
        0x102332,0x1B3545,0x426071,0xECF4F7,0xA8BDC9,0x68D6E7,
        0x6FD88B,0xF2CB73,0xF07170,0xE7B55F,216
    };
    static const struct rf_ui_layout_config layout={
        18,10,12,144,360,320,116,228,106,145,260,350,184
    };
    if (!state) return;
    memset(state,0,sizeof(*state));
    state->mode=RF_PLAYER_UI_PLAYER;state->scale_percent=100;
    state->theme=theme;state->layout=layout;
    rf_minimap_init(&state->minimap);
}

const char *rf_player_ui_mode_name(int mode)
{
    switch (mode) {
    case RF_PLAYER_UI_PLAYER:return "玩家界面";
    case RF_PLAYER_UI_TERMINAL:return "终端界面";
    case RF_PLAYER_UI_EXPERIMENT:return "实验界面";
    case RF_PLAYER_UI_LEGACY:return "经典界面";
    default:return "玩家界面";
    }
}

void rf_player_ui_prepare(struct rf_player_ui_state *state,const struct toy_map *map,
                          const struct toy_game *game,unsigned int map_generation)
{
    if (!state) return;
    rf_minimap_prepare(&state->minimap,map,map_generation);
    rf_minimap_collect_allies(&state->minimap,game);
}

void rf_ui_layout_resolve(struct rf_ui_layout *out,const struct rf_player_ui_state *state,
                          int width,int height,int rts_active)
{
    const struct rf_ui_layout_config *config=&state->layout;
    int scale=ui_clamp(height*1000/720,1000,2200)*ui_clamp(state->scale_percent,75,175)/100;
    int margin,gap,pad,map_size,phase_width,resource_width,vital_width,weapon_width;
    /* Reflow on narrow windows rather than shrinking Chinese below 16 px. */
    memset(out,0,sizeof(*out));
    out->scale_milli=scale;out->text_scale_milli=ui_max(1000,scale);
    out->margin=margin=ui_px(config->margin,scale);
    out->gap=gap=ui_px(config->gap,scale);
    out->padding=pad=ui_px(config->padding,scale);
    map_size=ui_px(state->map_expanded?config->map_expanded_size:config->map_size,scale);
    map_size=ui_min(map_size,ui_min(width-margin*2,height-margin*2-96));
    resource_width=ui_min(ui_px(config->resource_width,scale),width/3);
    phase_width=ui_min(ui_px(config->phase_width,scale),width-resource_width-margin*3-gap);
    out->phase=ui_rect((width-phase_width)/2,margin,phase_width,ui_px(38,scale));
    out->resources=ui_rect(width-margin-resource_width,margin,resource_width,ui_px(38,scale));
    if (out->phase.x+out->phase.w+gap>out->resources.x)
        out->phase.x=out->resources.x-gap-out->phase.w;
    out->map=ui_rect(margin,margin+ui_px(24,scale),map_size,map_size);
    if (out->map.x+out->map.w+gap>out->phase.x)
        out->map.y=out->phase.y+out->phase.h+gap;
    out->objective=ui_rect(margin,out->map.y+map_size+gap,
        ui_min(ui_px(config->objective_width,scale),width/2-margin),ui_px(56,scale));
    if (width<ui_px(1000,scale)) out->objective.w=map_size;
    vital_width=ui_min(ui_px(config->vital_width,scale),(width-margin*2-gap)/2);
    weapon_width=ui_min(ui_px(config->weapon_width,scale),(width-margin*2-gap)/2);
    out->vitals=ui_rect(margin,height-margin-ui_px(config->vital_height,scale),
        vital_width,ui_px(config->vital_height,scale));
    out->weapon=ui_rect(width-margin-weapon_width,height-margin-ui_px(config->weapon_height,scale),
        weapon_width,ui_px(config->weapon_height,scale));
    out->hints=ui_rect(out->vitals.x+out->vitals.w+gap,height-margin-ui_px(30,scale),
        out->weapon.x-out->vitals.x-out->vitals.w-gap*2,ui_px(30,scale));
    if (out->hints.w<ui_px(200,scale)) out->hints=ui_rect(width/4,
        ui_min(out->vitals.y,out->weapon.y)-gap-ui_px(26,scale),width/2,ui_px(26,scale));
    if (rts_active) {
        int dock_h=ui_min(ui_px(config->rts_height,scale),height/3);
        int dock_y=height-margin-dock_h;
        int map_width=ui_min(ui_px(190,scale),width/4);
        int commands_width=ui_min(ui_px(278,scale),width/3);
        int selection_x=margin+map_width+gap;
        out->map=ui_rect(margin,dock_y,map_width,ui_max(44,dock_h-ui_px(50,scale)-gap));
        out->objective=ui_rect(margin,out->map.y+out->map.h+gap,map_width,
            dock_h-out->map.h-gap);
        out->commands=ui_rect(width-margin-commands_width,dock_y,commands_width,dock_h);
        out->selection=ui_rect(selection_x,dock_y,
            out->commands.x-selection_x-gap,dock_h);
        out->hints=ui_rect(width/4,dock_y-gap-ui_px(26,scale),width/2,ui_px(26,scale));
        out->dock_toggle=ui_rect(width-margin-ui_px(100,scale),
            (state->rts_collapsed?height-margin:dock_y-gap)-ui_px(26,scale),
            ui_px(100,scale),ui_px(26,scale));
        if (state->map_expanded) {
            out->map=ui_rect(margin,margin+ui_px(48,scale),map_size,map_size);
            out->objective.y=out->map.y+map_size+gap;
            out->objective.w=map_size;out->objective.h=ui_px(44,scale);
        }
        if (state->rts_collapsed) {
            out->selection=out->commands=out->hints=ui_rect(0,0,0,0);
            if (!state->map_expanded) out->map=out->objective=ui_rect(0,0,0,0);
        }
    }
    (void)pad;
}

int rf_ui_rect_contains(struct rf_ui_rect rect,int x,int y)
{ return rect.w>0 && rect.h>0 && x>=rect.x && x<rect.x+rect.w && y>=rect.y && y<rect.y+rect.h; }

void rf_ui_panel(struct rasterfall_canvas *canvas,struct rf_ui_rect r,
                 const struct rf_ui_theme *theme,int selected)
{
    unsigned border=selected?theme->selected:theme->border;
    if (r.w<4 || r.h<4) return;
    rasterfall_canvas_rect(canvas,r.x+1,r.y+1,r.w-2,r.h-2,theme->panel,theme->panel_alpha);
    rasterfall_canvas_rect(canvas,r.x+3,r.y,r.w-6,1,border,210);
    rasterfall_canvas_rect(canvas,r.x+3,r.y+r.h-1,r.w-6,1,border,210);
    rasterfall_canvas_rect(canvas,r.x,r.y+3,1,r.h-6,border,210);
    rasterfall_canvas_rect(canvas,r.x+r.w-1,r.y+3,1,r.h-6,border,210);
    rasterfall_canvas_rect(canvas,r.x+1,r.y+1,ui_min(22,r.w/3),2,
        selected?theme->selected:theme->accent,255);
}

void rf_ui_text(struct rasterfall_canvas *canvas,struct rf_ui_rect rect,
                const char *text,unsigned color,int scale_milli,int max_lines)
{
    int height=ui_px(16,scale_milli),leading=ui_px(22,scale_milli);
    int lines=rect.h<height?0:1+(rect.h-height)/ui_max(1,leading);
    lines=ui_min(lines,max_lines);
    if (lines>0) rf_ui_font_text_wrap(canvas,rect.x,rect.y,rect.w,
        lines,text,color,scale_milli);
}

void rf_ui_button(struct rasterfall_canvas *canvas,struct rf_ui_rect rect,
                  const struct rf_ui_theme *theme,const char *label,
                  int scale_milli,int selected,int enabled)
{
    int pad=ui_px(rect.w<ui_px(100,scale_milli)?4:8,scale_milli),text_h=ui_px(20,scale_milli);
    rf_ui_panel(canvas,rect,theme,selected && enabled);
    rf_ui_text(canvas,ui_rect(rect.x+pad,rect.y+(rect.h-text_h)/2,rect.w-2*pad,text_h),
        label,enabled?(selected?theme->selected:theme->text):theme->muted,scale_milli,1);
}

void rf_ui_window(struct rasterfall_canvas *canvas,struct rf_ui_rect rect,
                  const struct rf_ui_theme *theme,const char *title,int scale_milli)
{
    int pad=ui_px(12,scale_milli),header=ui_px(42,scale_milli);
    rf_ui_panel(canvas,rect,theme,0);
    rasterfall_canvas_rect(canvas,rect.x+1,rect.y+1,rect.w-2,header,theme->panel_raised,230);
    rf_ui_text(canvas,ui_rect(rect.x+pad,rect.y+pad,rect.w-2*pad,header-pad),
        title,theme->text,scale_milli,1);
}

static void ui_label(const struct rf_player_ui_view *view,int action,char *out,unsigned capacity)
{
    struct rf_input_bindings defaults;
    const struct rf_input_bindings *bindings=view->bindings;
    if (!bindings) { rf_input_bindings_defaults(&defaults);bindings=&defaults; }
    rf_input_action_label(bindings,(enum rf_input_action)action,out,capacity);
}

static struct rf_ui_rect ui_rts_button(const struct rf_ui_layout *layout,int index)
{
    struct rf_ui_rect rect=layout->commands;
    int pad=layout->padding,row=ui_px(28,layout->scale_milli),gap=ui_px(5,layout->scale_milli);
    rect.x+=pad;rect.y+=ui_px(37,layout->scale_milli)+(row+gap)*index;
    rect.w-=pad*2;rect.h=row;
    return rect;
}

int rf_player_ui_hit_test(const struct rf_player_ui_state *state,
                          int width,int height,int rts_active,int x,int y)
{
    struct rf_ui_layout layout;
    if (!state || !rts_active || state->mode>=RF_PLAYER_UI_EXPERIMENT) return RF_PLAYER_UI_HIT_NONE;
    rf_ui_layout_resolve(&layout,state,width,height,rts_active);
    if (rf_ui_rect_contains(layout.dock_toggle,x,y)) return RF_PLAYER_UI_HIT_DOCK;
    if (state->rts_collapsed) return RF_PLAYER_UI_HIT_NONE;
    for (int i=0;i<3;++i) if (rf_ui_rect_contains(ui_rts_button(&layout,i),x,y))
        return RF_PLAYER_UI_HIT_STOP+i;
    return RF_PLAYER_UI_HIT_NONE;
}

static void ui_bar(struct rasterfall_canvas *canvas,int x,int y,int w,int h,
                    int value,int maximum,unsigned color)
{
    rasterfall_canvas_rect(canvas,x,y,w,h,0x233D4A,255);
    if (maximum>0) rasterfall_canvas_rect(canvas,x,y,
        (int)((long long)ui_clamp(value,0,maximum)*w/maximum),h,color,255);
}

static void ui_vitals(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                       const struct rf_ui_layout *layout)
{
    const struct toy_game_actor *player=toy_game_local_player_actor_const(hud->game);
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct rf_ui_rect r=layout->vitals;
    struct toy_game_capabilities caps;
    int scale=layout->text_scale_milli,pad=layout->padding,x=r.x+pad,y=r.y+pad;
    char line[128];
    if (!player) return;
    rf_ui_panel(canvas,r,theme,0);
    snprintf(line,sizeof(line),"%s",hud->player_name&&*hud->player_name?hud->player_name:"PLAYER");
    rf_ui_text(canvas,ui_rect(x,y,r.w-pad*2,ui_px(18,scale)),line,theme->text,scale,1);
    y+=ui_px(25,scale);
    rf_ui_text(canvas,ui_rect(x,y+ui_px(5,scale),ui_px(48,scale),ui_px(20,scale)),
        "生命",theme->muted,scale,1);
    snprintf(line,sizeof(line),"%d / %d",player->hp,player->max_hp);
    rf_ui_text(canvas,ui_rect(x+ui_px(52,scale),y,r.w-pad*2-ui_px(52,scale),ui_px(30,scale)),line,
        player->hp*4<player->max_hp?theme->danger:theme->success,scale*3/2,1);
    ui_bar(canvas,x,y+ui_px(30,scale),r.w-pad*2,ui_px(5,scale),player->hp,player->max_hp,theme->success);
    toy_game_actor_capabilities(player,toy_game_actor_current_weapon(player),&caps);
    if (caps.evasion_capacity>0) {
        int reserve=ui_max(0,player->evasion.reserve_milli);
        const char *status=reserve==0?"耗尽":player->evasion.animation_ms>0?"回避中":
            player->evasion.pressure_ms>0?"恢复等待":"就绪";
        y+=ui_px(43,scale);
        snprintf(line,sizeof(line),"回避 %d/%d  %s",(reserve+999)/1000,caps.evasion_capacity,status);
        rf_ui_text(canvas,ui_rect(x,y,r.w-pad*2,ui_px(18,scale)),line,
            reserve<caps.evasion_capacity*250?theme->warning:theme->accent,scale,1);
        ui_bar(canvas,x,y+ui_px(22,scale),r.w-pad*2,ui_px(4,scale),reserve,
            caps.evasion_capacity*1000,theme->accent);
    }
}

static void ui_weapon(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                       const struct rf_ui_layout *layout)
{
    const struct toy_game_actor *player=toy_game_local_player_actor_const(hud->game);
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct rf_ui_rect r=layout->weapon;
    const struct toy_game_slot *slot;
    char line[96],key[24];
    int scale=layout->text_scale_milli,pad=layout->padding;
    if (!player || player->current_slot<0 || player->current_slot>=TOY_GAME_WEAPON_SLOTS) return;
    slot=&player->slots[player->current_slot];
    rf_ui_panel(canvas,r,theme,0);
    if (slot->reserve==TOY_GAME_AMMO_INFINITE) snprintf(line,sizeof(line),"%d / INF",slot->mag);
    else snprintf(line,sizeof(line),"%d / %d",slot->mag,slot->reserve);
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+pad,r.w-pad*2,ui_px(32,scale)),line,
        slot->mag<=0?theme->danger:theme->text,scale*3/2,1);
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+ui_px(47,scale),r.w-pad*2,ui_px(18,scale)),
        slot->weapon>=0?toy_game_weapon_name(slot->weapon):"未装备",theme->muted,scale,1);
    if (player->reloading) {
        ui_label(&hud->player_ui_view,RF_ACTION_RELOAD,key,sizeof(key));
        snprintf(line,sizeof(line),"[%s] 换弹中",key);
        rf_ui_text(canvas,ui_rect(r.x+pad,r.y+ui_px(69,scale),r.w-pad*2,ui_px(18,scale)),line,theme->warning,scale,1);
    } else {
        int card_w=(r.w-pad*2-ui_px(6,scale))/3;
        for (int i=0;i<3;++i) {
            const struct toy_game_weapon_info *info=toy_game_weapon_info_or_null(player->slots[i].weapon);
            ui_label(&hud->player_ui_view,RF_ACTION_SLOT_1+i,key,sizeof(key));
            snprintf(line,sizeof(line),"%s %s",key,info?info->short_name:"--");
            rf_ui_button(canvas,ui_rect(r.x+pad+i*(card_w+ui_px(3,scale)),
                r.y+r.h-pad-ui_px(25,scale),card_w,ui_px(25,scale)),theme,line,
                scale,i==player->current_slot,1);
        }
    }
}

static void ui_top(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                    const struct rf_ui_layout *layout)
{
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    const struct toy_game *game=hud->game;
    int scale=layout->text_scale_milli,pad=layout->padding;
    int seconds=((game->campaign_phase==TOY_GAME_PHASE_CALM?game->spawn_timer_ms:
        game->phase_timer_ms)+999)/1000;
    const char *phase=game->campaign_phase==TOY_GAME_PHASE_HORDE?"战斗阶段":
        game->campaign_phase==TOY_GAME_PHASE_BUILDUP?"来袭预警":
        game->wave>=TOY_GAME_WAVE_MAX?"波次完成":"准备阶段";
    char line[160],key[24];
    struct rf_ui_rect r=layout->phase;
    if (hud->player_ui_view.phase_visible) {
        rf_ui_panel(canvas,r,theme,0);
        if (game->campaign_phase==TOY_GAME_PHASE_HORDE)
            snprintf(line,sizeof(line),"%s  剩余敌人 %d  波次 %d/%d",phase,game->enemies_alive,game->wave,TOY_GAME_WAVE_MAX);
        else if (seconds>0) snprintf(line,sizeof(line),"%s  %02d:%02d  波次 %d/%d",phase,seconds/60,seconds%60,game->wave,TOY_GAME_WAVE_MAX);
        else snprintf(line,sizeof(line),"%s  波次 %d/%d",phase,game->wave,TOY_GAME_WAVE_MAX);
        rf_ui_text(canvas,ui_rect(r.x+pad,r.y+(r.h-ui_px(16,scale))/2,r.w-pad*2,ui_px(18,scale)),line,theme->text,scale,1);
    }
    r=layout->resources;rf_ui_panel(canvas,r,theme,0);
    ui_label(&hud->player_ui_view,RF_ACTION_CANCEL,key,sizeof(key));
    snprintf(line,sizeof(line),"$ %d   [%s] 菜单",game->money,key);
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+(r.h-ui_px(16,scale))/2,r.w-pad*2,ui_px(18,scale)),line,theme->warning,scale,1);
}

void rf_player_ui_map_view(struct rf_minimap_view *m,const struct rf_player_ui_state *state,
                           const struct rf_player_ui_view *view,
                           const struct toy_game_actor *player,int width,int height)
{
    struct rf_ui_layout layout;
    const struct rf_minimap_state *map=&state->minimap;
    int pad;
    rf_ui_layout_resolve(&layout,state,width,height,view->rts_active);
    pad=ui_px(6,layout.text_scale_milli);
    memset(m,0,sizeof(*m));
    m->x=layout.map.x+pad;m->y=layout.map.y+pad;
    m->width=ui_max(1,layout.map.w-pad*2);m->height=ui_max(1,layout.map.h-pad*2);
    m->center_x=player?player->x:view->camera_x;m->center_z=player?player->z:view->camera_z;
    m->span=RASTERFALL_RFU_PER_METER*100;m->rotation_cy=1024;
    m->reference_y=player?player->ground_y:0;m->layer_threshold=RASTERFALL_RFU_PER_METER*2;
    if (view->rts_active || state->map_expanded) {
        m->center_x=map->minx+(map->maxx-map->minx)/2;
        m->center_z=map->minz+(map->maxz-map->minz)/2;
        m->span=ui_max(map->maxx-map->minx,(map->maxz-map->minz)*m->width/m->height);
        m->span=ui_max(1,m->span+m->span/20);
    }
}

static void ui_map(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                    const struct rf_ui_layout *layout)
{
    const struct rf_player_ui_state *state=hud->player_ui;
    const struct rf_player_ui_view *view=&hud->player_ui_view;
    const struct toy_game_actor *player=toy_game_local_player_actor_const(hud->game);
    const struct rf_minimap_state *map=&state->minimap;
    const struct rf_ui_theme *theme=&state->theme;
    struct rf_ui_rect r=layout->map;
    struct rf_minimap_view m;
    char line[192],key[24];
    int scale=layout->text_scale_milli,pad=ui_px(6,scale);
    rf_ui_panel(canvas,r,theme,0);
    rf_player_ui_map_view(&m,state,view,player,canvas->width,canvas->height);
    rf_minimap_layout(canvas,map,&m,0x577181,theme->accent);
    if (view->objective_active) {
        int x,y;
        rf_minimap_world_to_screen(&m,view->objective_x,view->objective_z,&x,&y);
        x=ui_clamp(x,m.x+4,m.x+m.width-5);y=ui_clamp(y,m.y+4,m.y+m.height-5);
        rasterfall_canvas_rect(canvas,x-4,y-4,9,9,theme->warning,255);
        rasterfall_canvas_rect(canvas,x-2,y-2,5,5,theme->panel,255);
    }
    if (view->rts_active && view->rts_move_active) {
        int x,y;
        if (rf_minimap_world_to_screen(&m,view->rts_move_x,view->rts_move_z,&x,&y)) {
            rasterfall_canvas_rect(canvas,x-5,y,11,1,theme->accent,255);
            rasterfall_canvas_rect(canvas,x,y-5,1,11,theme->accent,255);
        }
    }
    rf_ui_text(canvas,ui_rect(layout->map.x,layout->map.y-ui_px(24,scale),layout->map.w,ui_px(22,scale)),
        view->region_name&&*view->region_name?view->region_name:"探索区域",theme->text,scale,1);
    r=layout->objective;rf_ui_panel(canvas,r,theme,0);
    if (view->objective_title && *view->objective_title)
        snprintf(line,sizeof(line),"%s",view->objective_title);
    else snprintf(line,sizeof(line),"自由探索");
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+pad,r.w-2*pad,ui_px(18,scale)),line,
        view->objective_active?theme->warning:theme->text,scale,1);
    if (view->objective_detail && *view->objective_detail)
        snprintf(line,sizeof(line),"%s",view->objective_detail);
    else {
        ui_label(view,RF_ACTION_MAP_EXPAND,key,sizeof(key));
        snprintf(line,sizeof(line),r.w<ui_px(220,scale)?"[%s] 地图":"[%s] 按住查看地图",key);
    }
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+pad+ui_px(21,scale),r.w-2*pad,
        r.h-pad-ui_px(21,scale)),line,theme->muted,scale,1);
}

static void ui_rts(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                    const struct rf_ui_layout *layout)
{
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    const struct rf_player_ui_view *view=&hud->player_ui_view;
    const struct toy_game_actor *player=toy_game_local_player_actor_const(hud->game);
    struct rf_ui_rect r=layout->selection;
    int scale=layout->text_scale_milli,pad=layout->padding,count=0,hp=0,max_hp=0,moving=0;
    int visible=0,y=r.y+pad;
    char line[192],key[24];
    rf_ui_panel(canvas,r,theme,0);
    if (view->rts_selected<0) {
        rf_ui_text(canvas,ui_rect(r.x+pad,y,r.w-2*pad,r.h-pad*2),
            "战术指挥\n左键选择玩家或旗帜\n右键指定移动目标",theme->muted,scale,3);
    } else {
        for (int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
            const struct toy_game_actor *a=&hud->game->actors[i];
            int selected=view->rts_selected==0?a==player:a->flag_index==view->rts_selected-1;
            if (!selected || !a->active || a->developer_only) continue;
            ++count;hp+=ui_max(0,a->hp);max_hp+=a->max_hp;
            if (a->nav_active || a->moving) ++moving;
        }
        if (view->rts_selected==0) snprintf(line,sizeof(line),"%s",hud->player_name&&*hud->player_name?hud->player_name:"PLAYER");
        else snprintf(line,sizeof(line),"旗帜 %d  ·  %d 名队员",view->rts_selected,count);
        rf_ui_text(canvas,ui_rect(r.x+pad,y,r.w-pad*2,ui_px(20,scale)),line,theme->text,scale,1);
        y+=ui_px(27,scale);
        snprintf(line,sizeof(line),"生命 %d / %d",hp,max_hp);
        rf_ui_text(canvas,ui_rect(r.x+pad,y,r.w-pad*2,ui_px(18,scale)),line,theme->success,scale,1);
        ui_bar(canvas,r.x+pad,y+ui_px(21,scale),r.w-2*pad,ui_px(5,scale),hp,max_hp,theme->success);
        y+=ui_px(37,scale);
        snprintf(line,sizeof(line),"当前命令: %s",view->rts_move_active?"移动到目标":
            (player && player->moving && view->rts_selected==0)?"移动中":"待命 / 自主行动");
        if (view->rts_selected>0) snprintf(line,sizeof(line),"队员移动中 %d / %d",moving,count);
        rf_ui_text(canvas,ui_rect(r.x+pad,y,r.w-pad*2,ui_px(18,scale)),line,theme->text,scale,1);
        y+=ui_px(26,scale);
        if (view->rts_selected>0) for (int i=0;i<TOY_GAME_MAX_ACTORS && visible<2;++i) {
            const struct toy_game_actor *a=&hud->game->actors[i];
            if (!a->active || a->developer_only || a->flag_index!=view->rts_selected-1) continue;
            snprintf(line,sizeof(line),"%s  %d/%d",a->name,a->hp,a->max_hp);
            rf_ui_text(canvas,ui_rect(r.x+pad,y,r.w-pad*2,ui_px(18,scale)),line,theme->muted,scale,1);
            y+=ui_px(20,scale);++visible;
        }
        if (view->command_feedback && *view->command_feedback)
            rf_ui_text(canvas,ui_rect(r.x+pad,r.y+r.h-pad-ui_px(18,scale),r.w-pad*2,ui_px(18,scale)),
                view->command_feedback,theme->warning,scale,1);
    }
    r=layout->commands;rf_ui_panel(canvas,r,theme,0);
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+pad,r.w-pad*2,ui_px(18,scale)),
        "单位指令  ·  右键移动",theme->muted,scale,1);
    ui_label(view,RF_ACTION_RTS_STOP,key,sizeof(key));
    snprintf(line,sizeof(line),"[%s] 停止移动",key);
    rf_ui_button(canvas,ui_rts_button(layout,0),theme,line,scale,0,view->rts_selected==0);
    ui_label(view,RF_ACTION_RTS_FOLLOW,key,sizeof(key));
    snprintf(line,sizeof(line),"视角 [%s] %s",key,view->rts_follow_player?"跟随中":"跟随玩家");
    rf_ui_button(canvas,ui_rts_button(layout,1),theme,line,scale,view->rts_follow_player,1);
    ui_label(view,RF_ACTION_COMMAND_MODE,key,sizeof(key));
    snprintf(line,sizeof(line),"视角 [%s] 返回 FPS",key);
    rf_ui_button(canvas,ui_rts_button(layout,2),theme,line,scale,0,1);
}

void rf_player_ui_layout(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud)
{
    const struct rf_player_ui_view *view=&hud->player_ui_view;
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct rf_ui_layout layout;
    char line[192],mode[24],terminal[24],map[24];
    if (!canvas || !hud->game) return;
    rf_ui_layout_resolve(&layout,hud->player_ui,canvas->width,canvas->height,view->rts_active);
    ui_top(canvas,hud,&layout);
    if (view->rts_active) {
        rf_ui_button(canvas,layout.dock_toggle,theme,hud->player_ui->rts_collapsed?"展开底栏":"收起底栏",
            layout.text_scale_milli,0,1);
        if (!hud->player_ui->rts_collapsed) {ui_map(canvas,hud,&layout);ui_rts(canvas,hud,&layout);}
        else if (hud->player_ui->map_expanded) ui_map(canvas,hud,&layout);
    } else { ui_map(canvas,hud,&layout);ui_vitals(canvas,hud,&layout);ui_weapon(canvas,hud,&layout); }
    ui_label(view,RF_ACTION_COMMAND_MODE,mode,sizeof(mode));
    ui_label(view,RF_ACTION_CONSOLE,terminal,sizeof(terminal));
    ui_label(view,RF_ACTION_UI_MODE,map,sizeof(map));
    snprintf(line,sizeof(line),"[%s] %s  [%s] 终端  [%s] 界面",mode,
        view->rts_active?"FPS":"战术视角",terminal,map);
    if (view->hints && !(view->rts_active && hud->player_ui->rts_collapsed)) {
    rf_ui_panel(canvas,layout.hints,theme,0);
    rf_ui_text(canvas,ui_rect(layout.hints.x+layout.padding,
        layout.hints.y+(layout.hints.h-ui_px(16,layout.text_scale_milli))/2,
        layout.hints.w-layout.padding*2,ui_px(18,layout.text_scale_milli)),
        line,theme->muted,layout.text_scale_milli,1);
    }
    if (hud->net && hud->net->mode!=RASTERFALL_NET_OFF) {
        int scale=layout.text_scale_milli;
        snprintf(line,sizeof(line),"%s  %s  %d ms",hud->net->mode==RASTERFALL_NET_HOST?"HOST":"CLIENT",
            hud->net->mode==RASTERFALL_NET_HOST||hud->net->connected?"已连接":"连接中",hud->net->rtt_ms);
        rf_ui_text(canvas,ui_rect(layout.resources.x,layout.resources.y+layout.resources.h+layout.gap,
            layout.resources.w,ui_px(18,scale)),line,theme->muted,scale,1);
    }
    if (hud->horde_banner_ms>0 && hud->interaction_banner) {
        int scale=layout.text_scale_milli,width=ui_min(canvas->width/2,ui_px(460,scale));
        struct rf_ui_rect r=ui_rect(view->comms_visible?layout.margin:canvas->width-width-layout.margin,
            view->comms_visible?layout.objective.y+layout.objective.h+layout.gap:
            layout.resources.y+layout.resources.h+layout.gap+ui_px(28,scale),width,ui_px(54,scale));
        rf_ui_panel(canvas,r,theme,0);
        rf_ui_text(canvas,ui_rect(r.x+layout.padding,r.y+layout.padding,
            r.w-layout.padding*2,r.h-layout.padding*2),hud->interaction_banner,
            hud->interaction_banner_success?theme->success:theme->warning,scale,2);
    }
}

void rf_player_ui_prompt_layout(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud)
{
    const struct rf_player_ui_view *view=&hud->player_ui_view;
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct rf_ui_layout layout;
    const char *title=view->interaction_title,*action=view->interaction_action;
    char line[192],key[24];
    int scale,width,y;
    if (view->rts_active || view->modal || hud->shop_open) return;
    rf_ui_layout_resolve(&layout,hud->player_ui,canvas->width,canvas->height,0);
    scale=layout.text_scale_milli;
    if (!title || !*title) {
        if (hud->flag_carried || hud->flag_near) { title="队伍旗帜";action=hud->flag_carried?"放置旗帜":"携带旗帜"; }
        else if (hud->highlighted>=0 && hud->highlighted<hud->interactable_count) {
            int kind=hud->interactables[hud->highlighted].kind;
            title=kind==TOY_MAP_PICKUP_SHOP?"军械库":kind==TOY_MAP_PICKUP_AMMO?"弹药补给":
                kind==TOY_MAP_PICKUP_OPERATIONS_TERMINAL?"行动部署":
                kind==TOY_MAP_PICKUP_STATION_TERMINAL?"前哨站终端":
                kind==TOY_MAP_PICKUP_RETURN_OUTPOST?"返回前哨站":"可交互对象";
            action="交互";
        } else return;
    }
    width=ui_min(ui_px(340,scale),canvas->width-layout.margin*2);
    y=canvas->height*61/100;
    rf_ui_panel(canvas,ui_rect((canvas->width-width)/2,y,width,ui_px(65,scale)),theme,0);
    rf_ui_text(canvas,ui_rect((canvas->width-width)/2+layout.padding,y+layout.padding,
        width-layout.padding*2,ui_px(18,scale)),title,theme->text,scale,1);
    ui_label(view,hud->flag_carried||hud->flag_near?RF_ACTION_FLAG:RF_ACTION_INTERACT,key,sizeof(key));
    if (view->interaction_distance_rfu>0) snprintf(line,sizeof(line),"[%s] %s   %d m",key,
        action&&*action?action:"交互",(view->interaction_distance_rfu+256)/RASTERFALL_RFU_PER_METER);
    else snprintf(line,sizeof(line),"[%s] %s",key,action&&*action?action:"交互");
    rf_ui_text(canvas,ui_rect((canvas->width-width)/2+layout.padding,y+ui_px(38,scale),
        width-layout.padding*2,ui_px(18,scale)),line,theme->accent,scale,1);
}

struct ui_text_test_result { int count,right,bottom; };
static int ui_text_test_rectangle(void *context,int x,int y,int w,int h,
                                  unsigned color,int alpha)
{
    struct ui_text_test_result *result=context;
    ++result->count;
    if (x+w>result->right) result->right=x+w;
    if (y+h>result->bottom) result->bottom=y+h;
    (void)color;(void)alpha;
    return 0;
}

int rf_player_ui_logic_test(void)
{
    const char mixed[]="A中B";
    const char *invalid="\xf0\x80\x80\x80";
    struct ui_text_test_result bounds={0};
    struct rasterfall_canvas canvas={160,120,0,&bounds,ui_text_test_rectangle};
    if (rasterfall_canvas_text_width(mixed,1000)!=32 ||
        rasterfall_canvas_text_width(mixed,1500)!=48 ||
        rasterfall_canvas_text_width("AB\nA",1000)!=16) return -1;
    if (rasterfall_canvas_codepoint(&invalid)!='?' || (unsigned char)*invalid!=0x80) return -2;
    rasterfall_canvas_text_wrap(&canvas,3,5,68,3,"中英文 mixed words 正确换行与省略",0xffffff,1250);
    if (!bounds.count || bounds.right>71 || bounds.bottom>75 || canvas.failed) return -3;
    return 0;
}
