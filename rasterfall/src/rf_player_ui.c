#include "tlibc_everything.h"
#include "rf_player_ui.h"
#include "rasterfall_hud.h"
#include "rf_input_bindings.h"
#include "rasterfall_units.h"
#include "rf_ui_font.h"
#include "rf_story.h"
#include "rasterfall_model.h"
#include "rasterfall_calibration.h"
#include "rasterfall_viewmodel.h"
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
        18,10,12,220,230,145,230,106,145,260,350,184
    };
    if (!state) return;
    memset(state,0,sizeof(*state));
    state->mode=RF_PLAYER_UI_PLAYER;state->scale_percent=100;
    state->theme=theme;state->layout=layout;
    rf_minimap_init(&state->minimap);
}

void rf_player_subtitles_update(struct rf_player_ui_state *state,
    const struct rf_story *story,int dt_ms)
{
    unsigned added=story->history_revision-state->subtitle_history_revision;
    int kept=0;
    for(int i=0;i<state->subtitle_count;++i) {
        struct rf_ui_subtitle entry=state->subtitles[i];
        int speaking=story->active_story && entry.choice<0 &&
            entry.node_id==story->node_id && entry.revision==story->node_revision;
        if(speaking)entry.remaining_ms=RF_UI_SUBTITLE_FADE_MS;
        else if(entry.speaking)entry.remaining_ms=RF_UI_SUBTITLE_FADE_MS;
        else entry.remaining_ms-=ui_max(0,dt_ms);
        entry.speaking=speaking;
        if(entry.remaining_ms>0)state->subtitles[kept++]=entry;
    }
    state->subtitle_count=kept;
    if(added>(unsigned)story->history_count)added=(unsigned)story->history_count;
    for(int i=story->history_count-(int)added;i<story->history_count;++i) {
        const struct rf_story_history_entry *source=&story->history[i];
        const struct rf_story_node *node=rf_story_find_node(source->node_id);
        struct rf_ui_subtitle *entry;
        if(!node)continue;
        if(state->subtitle_count==RF_UI_SUBTITLE_CAP) {
            memmove(state->subtitles,state->subtitles+1,
                sizeof(state->subtitles[0])*(RF_UI_SUBTITLE_CAP-1));
            --state->subtitle_count;
        }
        entry=&state->subtitles[state->subtitle_count++];
        entry->node_id=source->node_id;entry->choice=source->choice;entry->revision=source->revision;
        entry->speaking=story->active_story && source->choice<0 &&
            source->node_id==story->node_id && source->revision==story->node_revision;
        entry->remaining_ms=rf_story_line_duration_ms(source->choice>=0 && source->choice<node->choice_count?
            node->choices[source->choice].text:node->line)+RF_UI_SUBTITLE_FADE_MS;
    }
    state->subtitle_history_revision=story->history_revision;
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

/* UI-sized side views of the actual weapon meshes, prepared outside draw.
 * Like RTS portraits these retain only coverage, never a model or GPU view. */
#define UI_WEAPON_W 144
#define UI_WEAPON_H 48
static struct {
    int prepared;
    unsigned char mask[UI_WEAPON_W*UI_WEAPON_H];
} ui_weapon_meshes[TOY_GAME_WEAPON_COUNT];

static void ui_weapon_edge(unsigned char *mask,int x,int y,int endx,int endy)
{
    int dx=abs(endx-x),dy=-abs(endy-y),sx=x<endx?1:-1,sy=y<endy?1:-1,e=dx+dy;
    for(;;) {
        if(x>=0 && x<UI_WEAPON_W && y>=0 && y<UI_WEAPON_H)mask[y*UI_WEAPON_W+x]=220;
        if(x==endx && y==endy)break;
        int e2=e*2;if(e2>=dy){e+=dy;x+=sx;}if(e2<=dx){e+=dx;y+=sy;}
    }
}

static void ui_weapon_point(const struct rasterfall_weapon_model_adapter *adapter,
    const unsigned char *vertex,double *x,double *y)
{
    int v[3];memcpy(v,vertex,sizeof(v));
    *x=*y=0;
    for(int n=0;n<3;++n) {
        *x+=adapter->basis[6+n]*((double)v[n]-adapter->center[n]);
        *y+=adapter->basis[3+n]*((double)v[n]-adapter->center[n]);
    }
}

static void ui_weapon_mesh_prepare(int weapon)
{
    struct rasterfall_model_asset asset={0};
    struct rasterfall_weapon_model_adapter adapter;
    int minimum[3]={2147483647,2147483647,2147483647},maximum[3]={-2147483647,-2147483647,-2147483647};
    double minx=1e30,miny=1e30,maxx=-1e30,maxy=-1e30;
    if(weapon<0 || weapon>=TOY_GAME_WEAPON_COUNT || ui_weapon_meshes[weapon].prepared)return;
    ui_weapon_meshes[weapon].prepared=1;
    const char *path=rasterfall_weapon_model_path(weapon);
    if(!path || rasterfall_model_load(&asset,path)<0)return;
    unsigned char *mask=ui_weapon_meshes[weapon].mask;
    for(unsigned i=0;i<asset.vertex_count;++i) {
        int v[3];memcpy(v,asset.vertices+i*asset.vertex_bytes,sizeof(v));
        for(int n=0;n<3;++n){minimum[n]=ui_min(minimum[n],v[n]);maximum[n]=ui_max(maximum[n],v[n]);}
    }
    if(!asset.vertex_count || rasterfall_weapon_model_adapt(weapon,minimum,maximum,&adapter)<0) {
        rasterfall_model_unload(&asset);return;
    }
    for(unsigned i=0;i<asset.vertex_count;++i) {
        double x,y;ui_weapon_point(&adapter,asset.vertices+i*asset.vertex_bytes,&x,&y);
        if(x<minx)minx=x;
        if(x>maxx)maxx=x;
        if(y<miny)miny=y;
        if(y>maxy)maxy=y;
    }
    double scale=(UI_WEAPON_W-6)/(maxx-minx+1),ys=(UI_WEAPON_H-6)/(maxy-miny+1);
    if(ys<scale)scale=ys;
    for(unsigned i=0;i+2<asset.index_count;i+=3) {
        int px[3],py[3],valid=1;
        for(int n=0;n<3;++n) {
            unsigned index;double x,y;memcpy(&index,asset.indices+4*(i+n),4);
            if(index>=asset.vertex_count){valid=0;break;}
            ui_weapon_point(&adapter,asset.vertices+index*asset.vertex_bytes,&x,&y);
            px[n]=UI_WEAPON_W/2+(int)((x-(minx+maxx)*.5)*scale);
            py[n]=UI_WEAPON_H/2-(int)((y-(miny+maxy)*.5)*scale);
        }
        if(valid)for(int n=0;n<3;++n)ui_weapon_edge(mask,px[n],py[n],px[(n+1)%3],py[(n+1)%3]);
    }
    /* Dense triangles merge at icon size; retain contour and subdued interior
     * mesh, with the same scan treatment as the RTS body portrait. */
    unsigned char edges[UI_WEAPON_W*UI_WEAPON_H];memcpy(edges,mask,sizeof(edges));
    for(int y=1;y<UI_WEAPON_H-1;++y)for(int x=1;x<UI_WEAPON_W-1;++x) {
        int k=y*UI_WEAPON_W+x;
        if(edges[k] && edges[k-1] && edges[k+1] && edges[k-UI_WEAPON_W] && edges[k+UI_WEAPON_W])
            mask[k]=(y%6==0 || (x+y/3)%9==0)?140:35;
    }
    rasterfall_model_unload(&asset);
}

static void ui_grid_body(struct rf_player_ui_state *s,const struct toy_game *g,
    int index,int enemy,int x,int y,int z)
{
    int radius=enemy?TOY_GAME_CHARGER_RADIUS:TOY_GAME_PLAYER_RADIUS;
    int maxx=g->nav_origin+g->nav_width*512-1,maxz=g->nav_origin_z+g->nav_height*512-1;
    if(x-radius>maxx || x+radius<g->nav_origin || z-radius>maxz || z+radius<g->nav_origin_z)return;
    int first=toy_game_grid_cell(g,ui_clamp(x-radius,g->nav_origin,maxx),ui_clamp(z-radius,g->nav_origin_z,maxz));
    int last=toy_game_grid_cell(g,ui_clamp(x+radius,g->nav_origin,maxx),ui_clamp(z+radius,g->nav_origin_z,maxz));
    if(first<0 || last<0)return;
    for(int cz=first/g->nav_width;cz<=last/g->nav_width;++cz)
        for(int cx=first%g->nav_width;cx<=last%g->nav_width;++cx) {
            int cell=cz*g->nav_width+cx;
            uint64_t actors,enemies;
            if(abs(s->grid_y[cell]-y)>=RASTERFALL_HUMAN_HEIGHT_RFU)continue;
            if(!enemy)s->grid_cells[cell]=4;
            else {
                toy_game_grid_unit_occupants(g,cell,&actors,&enemies);
                if((enemies>>index)&1)s->grid_cells[cell]=6;
            }
        }
}

static void ui_grid_prepare(struct rf_player_ui_state *s,const struct toy_game *g,unsigned world)
{
    if(!g || !s->grid_view || !g->grid_enabled || !g->grid_valid)return;
    int count=g->nav_width*g->nav_height;
    if(!s->grid_cache_valid || s->grid_cache_generation!=g->navigation_generation ||
       s->grid_cache_world!=world || s->grid_cache_y!=s->grid_focus_y) {
        for(int cell=0;cell<count;++cell) {
            int x=g->nav_origin+(cell%g->nav_width)*512+256;
            int z=g->nav_origin_z+(cell/g->nav_width)*512+256;
            struct toy_game_ground_query q=toy_game_query_ground(g,x,z,TOY_GAME_PLAYER_RADIUS,s->grid_focus_y);
            s->grid_y[cell]=s->grid_focus_y;s->grid_static[cell]=0;
            if(q.has_support && abs(q.support_y-s->grid_focus_y)<=TOY_GAME_GRID_CELL_SIZE) {
                s->grid_y[cell]=q.support_y;
                s->grid_static[cell]=toy_game_grid_walkable(g,x,q.support_y,z) &&
                    !toy_game_position_blocked_at_height(g,x,z,TOY_GAME_PLAYER_RADIUS,q.support_y)?
                    (g->grid_buildings[cell]?2:1):3;
            }
        }
        s->grid_cache_generation=g->navigation_generation;s->grid_cache_world=world;
        s->grid_cache_y=s->grid_focus_y;s->grid_cache_valid=1;
    }
    memcpy(s->grid_cells,s->grid_static,(unsigned)count);
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *a=&g->actors[i];
        int cell=a->grid_reserved_cell;
        if(a->grid_reserved && a->active && a->hp>0 && a->state==TOY_GAME_ACTOR_ALIVE &&
           a->grid_reserved_navigation==g->navigation_generation &&
           a->grid_reserved_combat==a->combat_generation && cell>=0 && cell<count &&
           abs(s->grid_y[cell]-a->grid_reserved_y)<RASTERFALL_HUMAN_HEIGHT_RFU)s->grid_cells[cell]=5;
    }
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *a=&g->actors[i];
        if(a->active && a->hp>0 && a->state==TOY_GAME_ACTOR_ALIVE && !a->base_core)
            ui_grid_body(s,g,i,0,a->x,a->ground_y+a->airborne_y,a->z);
    }
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i) {
        const struct toy_game_enemy *e=&g->enemies[i];
        if(e->active==1 && e->hp>0)ui_grid_body(s,g,i,1,e->x,e->ground_y+e->airborne_y,e->z);
    }
}

void rf_player_ui_prepare(struct rf_player_ui_state *state,const struct toy_map *map,
                          const struct toy_game *game,unsigned int map_generation)
{
    if (!state) return;
    ui_grid_prepare(state,game,map_generation);
    rf_minimap_prepare(&state->minimap,map,map_generation);
    rf_minimap_collect_allies(&state->minimap,game);
    const struct toy_game_actor *player=toy_game_local_player_actor_const(game);
    if(player)ui_weapon_mesh_prepare(toy_game_actor_current_weapon(player));
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
    map_size=ui_px(config->map_size,scale);
    map_size=ui_min(map_size,ui_min(width-margin*2,height-margin*2-96));
    resource_width=ui_min(ui_px(config->resource_width,scale),width/3);
    phase_width=ui_min(ui_px(config->phase_width,scale),width-resource_width-margin*3-gap);
    out->phase=ui_rect((width-phase_width)/2,margin,phase_width,ui_px(38,scale));
    out->resources=ui_rect(width-margin-resource_width,margin,resource_width,ui_px(38,scale));
    if (out->phase.x+out->phase.w+gap>out->resources.x)
        out->phase.x=out->resources.x-gap-out->phase.w;
    map_size=ui_min(map_size,ui_min(width/3,height/3));
    out->map=ui_rect(margin,height-margin-map_size,map_size,map_size);
    out->objective=ui_rect(margin,out->map.y-ui_px(24,scale)-gap-ui_px(22,scale),
        ui_max(map_size,ui_min(ui_px(360,scale),width-margin*2)),ui_px(22,scale));
    vital_width=ui_min(ui_px(config->vital_width,scale),(width-margin*2-gap)/2);
    weapon_width=ui_min(ui_px(config->weapon_width,scale),(width-margin*2-gap)/2);
    out->vitals=ui_rect(width-margin-vital_width,height-margin-ui_px(config->vital_height,scale),
        vital_width,ui_px(config->vital_height,scale));
    out->weapon=ui_rect(width-margin-weapon_width,out->vitals.y-gap-ui_px(config->weapon_height,scale),
        weapon_width,ui_px(config->weapon_height,scale));
    out->hints=ui_rect(out->map.x+out->map.w+gap,height-margin-ui_px(30,scale),
        out->vitals.x-out->map.x-out->map.w-gap*2,ui_px(30,scale));
    {
        int dock_h=ui_min(ui_px(config->rts_height,scale),height/3);
        int dock_y=height-margin-dock_h;
        int map_width=map_size;
        int commands_width=ui_min(ui_px(230,scale),width/4);
        int selection_x=margin+map_width+gap;
        int portrait_width=ui_min(ui_px(100,scale),width/8);
        out->commands=ui_rect(width-margin-commands_width,dock_y,commands_width,dock_h);
        out->portrait=ui_rect(out->commands.x-gap-portrait_width,dock_y,portrait_width,dock_h);
        out->selection=ui_rect(selection_x,dock_y,
            out->portrait.x-selection_x-gap,dock_h);
        /* High UI scales reflow this dense dock as a unit, preserving space
         * for all ten group buttons and the selected-unit camera. */
        if(out->selection.w<ui_px(300,scale)) {
            int available=width-selection_x-margin-gap*3;
            commands_width=available*28/100;portrait_width=available*12/100;
            out->commands.x=width-margin-commands_width;out->commands.w=commands_width;
            out->portrait.x=out->commands.x-gap-portrait_width;out->portrait.w=portrait_width;
            out->selection.w=out->portrait.x-selection_x-gap;
        }
        out->groups=ui_rect(selection_x,dock_y-gap-ui_px(27,scale),out->selection.w,ui_px(27,scale));
        out->video=ui_rect(out->portrait.x+1,out->portrait.y+1,
            ui_max(0,out->portrait.w-2),ui_max(0,out->portrait.h-2));
        out->vitals=out->commands;
        out->weapon.x=out->vitals.x;out->weapon.w=out->vitals.w;
        out->weapon.y=out->vitals.y-gap-out->weapon.h;
        if (rts_active) {
            out->hints=ui_rect(0,0,0,0);
            out->dock_toggle=ui_rect(width-margin-ui_px(100,scale),
                (state->rts_collapsed?height-margin:dock_y-gap)-ui_px(26,scale),
                ui_px(100,scale),ui_px(26,scale));
            if (state->rts_collapsed)
                out->selection=out->commands=out->hints=out->groups=out->portrait=out->video=ui_rect(0,0,0,0);
        } else {
            /* Reuse the resolved RTS selection span, including narrow-window
             * reflow. The two compact FPS cards occupy its right-hand remainder. */
            int card_h=ui_px(88,scale),card_gap=ui_px(6,scale);
            int available=width-margin-out->portrait.x;
            int status_w=ui_px(148,scale),gun_w=available-card_gap-status_w;
            out->hints.x=out->selection.x;out->hints.w=out->selection.w;
            out->weapon=ui_rect(width-margin-gun_w,height-margin-card_h,gun_w,card_h);
            out->vitals=ui_rect(out->weapon.x-card_gap-status_w,out->weapon.y,status_w,card_h);
            if(gun_w<ui_px(180,scale)) {
                gun_w=available;
                out->weapon=ui_rect(width-margin-gun_w,height-margin-card_h,gun_w,card_h);
                out->vitals=ui_rect(out->weapon.x,out->weapon.y-card_gap-card_h,gun_w,card_h);
            }
            out->selection=out->commands=out->groups=out->portrait=out->video=out->dock_toggle=ui_rect(0,0,0,0);
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

struct rf_ui_rect rf_player_ui_command_rect(const struct rf_ui_layout *layout,int index)
{
    struct rf_ui_rect rect=layout->commands;
    if(rect.w<=0 || rect.h<=0)return ui_rect(0,0,0,0);
    int pad=layout->padding,gap=ui_max(2,ui_px(4,layout->scale_milli));
    int side=ui_min((rect.w-2*pad-2*gap)/3,(rect.h-2*pad-2*gap)/3);
    rect.x+=pad+(rect.w-2*pad-3*side-2*gap)/2+(side+gap)*(index%3);
    rect.y+=pad+(side+gap)*(index/3);rect.w=rect.h=side;
    return rect;
}

struct rf_ui_rect rf_player_ui_grid_legend_rect(const struct rf_ui_layout *layout)
{ return ui_rect(layout->margin,ui_px(110,layout->scale_milli),ui_px(170,layout->scale_milli),ui_px(164,layout->scale_milli)); }

int rf_player_ui_hit_test(const struct rf_player_ui_state *state,
                          int width,int height,int rts_active,int x,int y)
{
    struct rf_ui_layout layout;
    if (!state || !rts_active || state->mode>=RF_PLAYER_UI_EXPERIMENT) return RF_PLAYER_UI_HIT_NONE;
    rf_ui_layout_resolve(&layout,state,width,height,rts_active);
    if (rf_ui_rect_contains(layout.dock_toggle,x,y)) return RF_PLAYER_UI_HIT_DOCK;
    if (state->rts_collapsed) return RF_PLAYER_UI_HIT_NONE;
    for (int i=0;i<4;++i) if (rf_ui_rect_contains(rf_player_ui_command_rect(&layout,i),x,y))
        return i==3?RF_PLAYER_UI_HIT_GRID:RF_PLAYER_UI_HIT_STOP+i;
    return RF_PLAYER_UI_HIT_NONE;
}

static void ui_bar(struct rasterfall_canvas *canvas,int x,int y,int w,int h,
                    int value,int maximum,unsigned color)
{
    rasterfall_canvas_rect(canvas,x,y,w,h,0x233D4A,255);
    if (maximum>0) rasterfall_canvas_rect(canvas,x,y,
        (int)((long long)ui_clamp(value,0,maximum)*w/maximum),h,color,255);
}

/* Preserve the RTS palette and corner accent with a 24% opaque backing. */
static void ui_fps_card(struct rasterfall_canvas *canvas,struct rf_ui_rect r,
    const struct rf_ui_theme *theme)
{
    rasterfall_canvas_rect(canvas,r.x,r.y,r.w,r.h,theme->panel,theme->panel_alpha*72/255);
    rasterfall_canvas_rect(canvas,r.x,r.y,r.w,1,theme->border,95);
    rasterfall_canvas_rect(canvas,r.x,r.y+r.h-1,r.w,1,theme->border,95);
    rasterfall_canvas_rect(canvas,r.x,r.y,1,r.h,theme->border,75);
    rasterfall_canvas_rect(canvas,r.x+r.w-1,r.y,1,r.h,theme->border,75);
    rasterfall_canvas_rect(canvas,r.x,r.y,ui_min(22,r.w/3),2,theme->accent,210);
}

static void ui_fps_text(struct rasterfall_canvas *canvas,struct rf_ui_rect r,
    const char *text,unsigned color,int scale,int lines)
{
    /* Keep numeric status/ammo complete when the RTS remainder is narrow;
     * fit only the affected label rather than shrinking the whole HUD. */
    int measured=rf_ui_font_text_width(text,1000);
    if(measured>0)scale=ui_min(scale,ui_max(1,(r.w-2)*1000/measured));
    struct rf_ui_rect shadow=r;
    shadow.x+=ui_max(1,ui_px(1,scale));shadow.y+=ui_max(1,ui_px(1,scale));
    rf_ui_text(canvas,shadow,text,0x102332,scale,lines);
    rf_ui_text(canvas,r,text,color,scale,lines);
}

static void ui_vitals(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                      const struct rf_ui_layout *layout)
{
    const struct toy_game_actor *player=toy_game_local_player_actor_const(hud->game);
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct toy_game_capabilities caps;
    struct rf_ui_rect r=layout->vitals;
    int scale=layout->text_scale_milli,pad=ui_px(9,scale),x=r.x+pad,w=r.w-pad*2;
    char line[96];
    if(!player)return;
    toy_game_actor_capabilities(player,toy_game_actor_current_weapon(player),&caps);
    int hp=ui_max(0,player->hp),maximum=ui_max(1,player->max_hp);
    int capacity=ui_max(0,caps.evasion_capacity)*1000;
    int reserve=ui_clamp(player->evasion.reserve_milli,0,capacity);
    unsigned hp_color=hp<=maximum/4?theme->danger:hp<=maximum/2?theme->warning:theme->success;
    unsigned ev_color=capacity<=0?theme->muted:reserve<=0?theme->danger:reserve<capacity/4?theme->warning:theme->accent;
    ui_fps_card(canvas,r,theme);
    snprintf(line,sizeof(line),"生命 %d/%d",hp,maximum);
    ui_fps_text(canvas,ui_rect(x,r.y+ui_px(8,scale),w,ui_px(20,scale)),line,hp_color,scale,1);
    ui_bar(canvas,x,r.y+ui_px(30,scale),w,ui_px(5,scale),hp,maximum,hp_color);
    snprintf(line,sizeof(line),"回避 %d/%d",(reserve+999)/1000,caps.evasion_capacity);
    ui_fps_text(canvas,ui_rect(x,r.y+ui_px(39,scale),w,ui_px(20,scale)),line,ev_color,scale,1);
    ui_bar(canvas,x,r.y+ui_px(61,scale),w,ui_px(3,scale),reserve,capacity,ev_color);
    const char *status=capacity<=0?"不可用":reserve<=0?"耗尽":
        player->evasion.animation_ms>0?"回避中":player->evasion.pressure_ms>0?"恢复等待":"";
    int small=scale*3/4;
    ui_fps_text(canvas,ui_rect(x,r.y+ui_px(69,scale),w/2,ui_px(16,scale)),status,ev_color,small,1);
    snprintf(line,sizeof(line),"药品 %d",player->slots[3].weapon==TOY_GAME_WEAPON_PILL?player->slots[3].mag:0);
    ui_fps_text(canvas,ui_rect(x+w/2,r.y+ui_px(69,scale),w-w/2,ui_px(16,scale)),line,theme->muted,small,1);
}

static void ui_weapon_wire(struct rasterfall_canvas *canvas,struct rf_ui_rect r,int weapon,unsigned color)
{
    if(weapon<0 || weapon>=TOY_GAME_WEAPON_COUNT || r.w<1 || r.h<1)return;
    const unsigned char *mask=ui_weapon_meshes[weapon].mask;
    int w=ui_min(r.w,r.h*UI_WEAPON_W/UI_WEAPON_H),h=w*UI_WEAPON_H/UI_WEAPON_W;
    if(w<1 || h<1)return;
    r.x+=(r.w-w)/2;r.y+=(r.h-h)/2;
    for(int y=0;y<h;++y) {
        int run=0,opacity=0;
        for(int x=0;x<=w;++x) {
            int on=0;
            if(x<w) {
                int ax=x*UI_WEAPON_W/w,bx=ui_max(ax+1,(x+1)*UI_WEAPON_W/w);
                int ay=y*UI_WEAPON_H/h,by=ui_max(ay+1,(y+1)*UI_WEAPON_H/h);
                for(int yy=ay;yy<by;++yy)for(int xx=ax;xx<bx;++xx)
                    on=ui_max(on,mask[yy*UI_WEAPON_W+xx]);
            }
            if(on!=opacity) {
                if(opacity)rasterfall_canvas_rect(canvas,r.x+run,r.y+y,x-run,1,color,opacity);
                run=x;opacity=on;
            }
        }
    }
}

static void ui_weapon(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,
                       const struct rf_ui_layout *layout)
{
    const struct toy_game_actor *player=toy_game_local_player_actor_const(hud->game);
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct rf_ui_rect r=layout->weapon;
    int scale=layout->text_scale_milli,pad=ui_px(9,scale),x=r.x+pad,w=r.w-pad*2;
    char line[96];
    if(!player || player->current_slot<0 || player->current_slot>=TOY_GAME_WEAPON_SLOTS)return;
    const struct toy_game_slot *slot=&player->slots[player->current_slot];
    ui_fps_card(canvas,r,theme);
    snprintf(line,sizeof(line),"%s",player->reloading?"换弹中":slot->weapon>=0?toy_game_weapon_name(slot->weapon):"未装备");
    ui_fps_text(canvas,ui_rect(x,r.y+ui_px(7,scale),w,ui_px(18,scale)),line,theme->muted,scale*7/8,1);
    int ammo_w=ui_min(ui_px(60,scale),w*2/5),mesh_w=w-ammo_w-ui_px(6,scale);
    ui_weapon_wire(canvas,ui_rect(x,r.y+ui_px(27,scale),mesh_w,ui_px(45,scale)),slot->weapon,theme->accent);
    snprintf(line,sizeof(line),"%d",slot->mag);
    ui_fps_text(canvas,ui_rect(x+w-ammo_w,r.y+ui_px(26,scale),ammo_w,ui_px(30,scale)),line,
        slot->mag<=0?theme->danger:theme->text,scale*3/2,1);
    if(slot->reserve==TOY_GAME_AMMO_INFINITE)snprintf(line,sizeof(line),"/ INF");
    else snprintf(line,sizeof(line),"/ %d",slot->reserve);
    ui_fps_text(canvas,ui_rect(x+w-ammo_w,r.y+ui_px(55,scale),ammo_w,ui_px(20,scale)),line,theme->muted,scale*7/8,1);
    if(player->reloading) {
        int total=ui_max(1,toy_game_actor_reload_ms(player,toy_game_weapon_info_or_null(slot->weapon)));
        ui_bar(canvas,x,r.y+ui_px(80,scale),w,ui_px(3,scale),
            ui_clamp(total-player->reload_timer_ms,0,total),total,theme->warning);
    }
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
    if (view->rts_active) {
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
    char line[192];
    int scale=layout->text_scale_milli;
    rf_ui_panel(canvas,r,theme,0);
    rf_player_ui_map_view(&m,state,view,player,canvas->width,canvas->height);
    m.sight_count=view->map_sight_count;
    for(int i=0;i<m.sight_count;++i) {
        m.sight_x[i]=view->map_sight_x[i];m.sight_z[i]=view->map_sight_z[i];
    }
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
    r=layout->objective;
    if (view->objective_title && *view->objective_title)
        snprintf(line,sizeof(line),"%s",view->objective_title);
    else snprintf(line,sizeof(line),"自由探索");
    rf_ui_text(canvas,r,line,
        view->objective_active?theme->warning:theme->text,scale,1);
    if(view->mission_title[0] && view->objective_detail) {
        struct rf_ui_rect detail=r;detail.y-=ui_px(22,scale);
        rf_ui_text(canvas,detail,view->objective_detail,theme->muted,scale,1);
    }
}

#include "rf_rts_ui.inc"

static void ui_fps(struct rasterfall_canvas *canvas,const struct rf_ui_theme *theme,
                   const struct rf_ui_layout *layout,int fps)
{
    int scale=layout->text_scale_milli,pad=ui_px(8,scale);
    struct rf_ui_rect r=ui_rect(layout->margin,layout->margin,ui_px(88,scale),ui_px(28,scale));
    char value[16];
    int number_width,label_width=rf_ui_font_text_width("FPS",scale);
    if (fps>0) snprintf(value,sizeof(value),"%d",fps);
    else snprintf(value,sizeof(value),"--");
    number_width=rf_ui_font_text_width(value,scale);
    r.w=ui_max(r.w,pad*3+label_width+ui_max(number_width,rf_ui_font_text_width("888",scale)));
    rasterfall_canvas_rect(canvas,r.x,r.y,r.w,r.h,theme->panel,theme->panel_alpha);
    rasterfall_canvas_rect(canvas,r.x,r.y,ui_px(18,scale),1,theme->accent,230);
    rf_ui_text(canvas,ui_rect(r.x+pad,r.y+ui_px(5,scale),label_width,ui_px(20,scale)),
        "FPS",theme->muted,scale,1);
    rf_ui_text(canvas,ui_rect(r.x+r.w-pad-number_width,r.y+ui_px(5,scale),number_width,ui_px(20,scale)),
        value,theme->text,scale,1);
}

void rf_player_ui_layout(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *hud,int fps)
{
    const struct rf_player_ui_view *view=&hud->player_ui_view;
    const struct rf_ui_theme *theme=&hud->player_ui->theme;
    struct rf_ui_layout layout;
    char line[192],mode[24],terminal[24],map[24],menu[24];
    if (!canvas || !hud->game) return;
    rf_ui_layout_resolve(&layout,hud->player_ui,canvas->width,canvas->height,view->rts_active);
    ui_grid_overlay(canvas,hud,&layout);
    if(view->rts_active)rts_world_feedback(canvas,hud,&layout);
    ui_fps(canvas,theme,&layout,fps);
    if (view->rts_active) {
        rf_ui_button(canvas,layout.dock_toggle,theme,hud->player_ui->rts_collapsed?"展开底栏":"收起底栏",
            layout.text_scale_milli,0,1);
        ui_map(canvas,hud,&layout);
        if (!hud->player_ui->rts_collapsed) ui_rts(canvas,hud,&layout);
    } else { ui_map(canvas,hud,&layout);ui_vitals(canvas,hud,&layout);ui_weapon(canvas,hud,&layout); }
    ui_label(view,RF_ACTION_COMMAND_MODE,mode,sizeof(mode));
    ui_label(view,RF_ACTION_CONSOLE,terminal,sizeof(terminal));
    ui_label(view,RF_ACTION_COMMS_FOCUS,map,sizeof(map));
    ui_label(view,RF_ACTION_CANCEL,menu,sizeof(menu));
    snprintf(line,sizeof(line),"[%s] 菜单  [%s] %s  [%s] 终端  [%s] 信息栏",menu,mode,
        view->rts_active?"FPS":"战术视角",terminal,map);
    if (view->hints && layout.hints.w>ui_px(200,layout.text_scale_milli) &&
        !(view->rts_active && hud->player_ui->rts_collapsed)) {
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
        struct rf_ui_rect r=ui_rect(canvas->width-width-layout.margin,
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
    if (view->rts_active || view->modal) return;
    rf_ui_layout_resolve(&layout,hud->player_ui,canvas->width,canvas->height,0);
    scale=layout.text_scale_milli;
    if (!title || !*title) {
        if (hud->flag_carried || hud->flag_near) { title="队伍旗帜";action=hud->flag_carried?"放置旗帜":"携带旗帜"; }
        else if (hud->highlighted>=0 && hud->highlighted<hud->interactable_count) {
            int kind=hud->interactables[hud->highlighted].kind;
            title=kind==TOY_MAP_PICKUP_AMMO?"弹药补给":
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
    /* A new utterance must not restart another utterance's fade, and an
     * interrupted/current utterance remains visible until it actually ends. */
    {
        static struct rf_player_ui_state state;
        struct rf_story story={0};
        rf_player_ui_init(&state);
        story.active_story=RF_STORY_OUTPOST;story.node_id=1010;story.node_revision=1;
        story.history[0]=(struct rf_story_history_entry){RF_STORY_OUTPOST,1010,-1,1};
        story.history_count=1;story.history_revision=1;
        rf_player_subtitles_update(&state,&story,16);
        rf_player_subtitles_update(&state,&story,60000);
        if(state.subtitle_count!=1 || !state.subtitles[0].speaking) return -5;
        story.node_id=1012;story.node_revision=2;
        story.history[1]=(struct rf_story_history_entry){RF_STORY_OUTPOST,1012,-1,2};
        story.history_count=2;story.history_revision=2;
        rf_player_subtitles_update(&state,&story,16);
        if(state.subtitle_count!=2 || state.subtitles[0].speaking ||
            !state.subtitles[1].speaking) return -6;
        rf_player_subtitles_update(&state,&story,RF_UI_SUBTITLE_FADE_MS/2);
        if(state.subtitles[0].remaining_ms!=RF_UI_SUBTITLE_FADE_MS/2) return -7;
        rf_player_subtitles_update(&state,&story,RF_UI_SUBTITLE_FADE_MS);
        if(state.subtitle_count!=1 || state.subtitles[0].node_id!=1012) return -8;
        story.active_story=0;
        rf_player_subtitles_update(&state,&story,16);
        rf_player_subtitles_update(&state,&story,RF_UI_SUBTITLE_FADE_MS);
        rf_player_subtitles_update(&state,&story,16);
        if(state.subtitle_count) return -9;
    }
    const char mixed[]="A中B";
    const char *invalid="\xf0\x80\x80\x80";
    struct ui_text_test_result bounds={0};
    struct rasterfall_canvas canvas={160,120,0,&bounds,ui_text_test_rectangle,NULL};
    if (rasterfall_canvas_text_width(mixed,1000)!=32 ||
        rasterfall_canvas_text_width(mixed,1500)!=48 ||
        rasterfall_canvas_text_width("AB\nA",1000)!=16) return -1;
    if (rasterfall_canvas_codepoint(&invalid)!='?' || (unsigned char)*invalid!=0x80) return -2;
    rasterfall_canvas_text_wrap(&canvas,3,5,68,3,"中英文 mixed words 正确换行与省略",0xffffff,1250);
    if (!bounds.count || bounds.right>71 || bounds.bottom>75 || canvas.failed) return -3;
    /* Painting and hit testing must keep the same coordinate space after
     * native resolution changes and independent user scaling. */
    {
        static const int sizes[][2]={{1280,720},{1920,1080},{1600,900}};
        static const int scales[]={75,100,125,175};
        struct rf_player_ui_state state;
        struct rf_ui_layout layout;
        rf_player_ui_init(&state);
        for (int i=0;i<3;++i) for (int j=0;j<4;++j) for (int folded=0;folded<2;++folded) {
            state.scale_percent=scales[j];state.rts_collapsed=folded;
            rf_ui_layout_resolve(&layout,&state,sizes[i][0],sizes[i][1],1);
            struct rf_ui_rect r=layout.dock_toggle;
            if (r.w<=0 || r.h<=0 || r.x<0 || r.y<0 ||
                r.x+r.w>sizes[i][0] || r.y+r.h>sizes[i][1] ||
                rf_player_ui_hit_test(&state,sizes[i][0],sizes[i][1],1,
                    r.x+r.w/2,r.y+r.h/2)!=RF_PLAYER_UI_HIT_DOCK) return -4;
            if(!folded)for(int n=0;n<4;++n) {
                r=rf_player_ui_command_rect(&layout,n);
                if(r.w<=0 || r.w!=r.h || r.x<layout.commands.x || r.y<layout.commands.y ||
                   r.x+r.w>layout.commands.x+layout.commands.w || r.y+r.h>layout.commands.y+layout.commands.h ||
                   rf_player_ui_hit_test(&state,sizes[i][0],sizes[i][1],1,r.x+r.w/2,r.y+r.h/2)!=
                   (n==3?RF_PLAYER_UI_HIT_GRID:RF_PLAYER_UI_HIT_STOP+n))return -5;
            }
        }
    }
    return 0;
}
