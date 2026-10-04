#include "tlibc_everything.h"
#include "rf_minimap.h"
#include "string.h"

#define RF_MINIMAP_ACTOR_ID 0x80000000u

static int mini_clamp(int value,int low,int high)
{ return value<low?low:value>high?high:value; }

void rf_minimap_init(struct rf_minimap_state *state)
{ if (state) memset(state,0,sizeof(*state)); }

void rf_minimap_invalidate(struct rf_minimap_state *state)
{ if (state) state->ready=0; }

void rf_minimap_prepare(struct rf_minimap_state *state,
                        const struct toy_map *map,unsigned int generation)
{
    if (!state) return;
    if (!map) { rf_minimap_init(state);return; }
    if (state->ready && state->source==map && state->generation==generation) return;
    if (state->source!=map || state->generation!=generation) state->marker_count=0;
    state->source=map;state->generation=generation;state->terrain_count=0;
    state->minx=map->minx;state->maxx=map->maxx;
    state->minz=map->minz;state->maxz=map->maxz;
    if (state->maxx<=state->minx) state->maxx=state->minx+1;
    if (state->maxz<=state->minz) state->maxz=state->minz+1;
    /* Static footprints come from the authored presentation projection, never
     * from nav cells, a world render or hidden collision geometry. */
    for (int pass=0;pass<2;++pass) for (int i=0;i<map->draw_count && i<TOY_MAP_MAX_DRAW;++i) {
        const struct toy_map_draw *draw=&map->draw[i];
        struct rf_minimap_terrain *terrain;
        int floor=draw->type==TOY_MAP_DRAW_FLOOR || draw->type==TOY_MAP_DRAW_BORDER;
        int solid=draw->type==TOY_MAP_DRAW_WALL || draw->type==TOY_MAP_DRAW_BOX ||
            draw->type==TOY_MAP_DRAW_RAMP || draw->type==TOY_MAP_DRAW_PLATFORM ||
            (draw->type==TOY_MAP_DRAW_MODEL && !draw->style);
        if ((pass==0?!floor:!solid) || !strncmp(draw->text,"air_gate_",9)) continue;
        if (state->terrain_count>=RF_MINIMAP_MAX_TERRAIN) break;
        terrain=&state->terrain[state->terrain_count++];
        terrain->minx=draw->a<draw->b?draw->a:draw->b;
        terrain->maxx=draw->a>draw->b?draw->a:draw->b;
        terrain->minz=draw->c<draw->d?draw->c:draw->d;
        terrain->maxz=draw->c>draw->d?draw->c:draw->d;
        terrain->kind=floor?(draw->style==11?1:0):2;
    }
    state->ready=1;++state->revision;
}

int rf_minimap_marker_set(struct rf_minimap_state *state,
                          const struct rf_minimap_marker *marker)
{
    int index;
    if (!state || !marker || !marker->id || marker->kind<RF_MINIMAP_PLAYER ||
        marker->kind>RF_MINIMAP_DEVICE) return -1;
    for (index=0;index<state->marker_count;++index)
        if (state->markers[index].id==marker->id) break;
    if (index==state->marker_count) {
        if (index==RF_MINIMAP_MAX_MARKERS) return -1;
        ++state->marker_count;
    }
    state->markers[index]=*marker;
    state->markers[index].label[sizeof(marker->label)-1]=0;
    return index;
}

void rf_minimap_marker_remove(struct rf_minimap_state *state,unsigned int id)
{
    if (!state) return;
    for (int i=0;i<state->marker_count;++i) if (state->markers[i].id==id) {
        for (int j=i+1;j<state->marker_count;++j) state->markers[j-1]=state->markers[j];
        --state->marker_count;return;
    }
}

void rf_minimap_collect_allies(struct rf_minimap_state *state,const struct toy_game *game)
{
    const struct toy_game_actor *player;
    int keep=0;
    if (!state) return;
    for (int i=0;i<state->marker_count;++i)
        if (!(state->markers[i].id&RF_MINIMAP_ACTOR_ID)) state->markers[keep++]=state->markers[i];
    state->marker_count=keep;
    if (!game) return;
    player=toy_game_local_player_actor_const(game);
    if (!player) return;
    for (int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *actor=&game->actors[i];
        struct rf_minimap_marker marker;
        if (!actor->active || actor->hp<=0 || actor->developer_only ||
            toy_game_actors_hostile(player,actor)) continue;
        memset(&marker,0,sizeof(marker));
        marker.id=RF_MINIMAP_ACTOR_ID|(unsigned)(i+1);
        marker.kind=actor==player?RF_MINIMAP_PLAYER:RF_MINIMAP_ALLY;
        marker.x=actor->x;marker.y=actor->ground_y;marker.z=actor->z;
        marker.sy=actor->sy;marker.cy=actor->cy;
        snprintf(marker.label,sizeof(marker.label),"%s",actor->name);
        rf_minimap_marker_set(state,&marker);
    }
}

int rf_minimap_world_to_screen(const struct rf_minimap_view *view,
                               int wx,int wz,int *sx,int *sy)
{
    long long dx,dz,rx,rz;
    if (!view || !sx || !sy || view->span<=0 || view->width<=0 || view->height<=0) return 0;
    dx=(long long)wx-view->center_x;dz=(long long)wz-view->center_z;
    rx=(dx*view->rotation_cy-dz*view->rotation_sy)/1024;
    rz=(dx*view->rotation_sy+dz*view->rotation_cy)/1024;
    *sx=view->x+view->width/2+(int)(rx*view->width/view->span);
    *sy=view->y+view->height/2-(int)(rz*view->width/view->span);
    return *sx>=view->x && *sx<view->x+view->width &&
           *sy>=view->y && *sy<view->y+view->height;
}

int rf_minimap_screen_to_world(const struct rf_minimap_view *view,
                               int sx,int sy,int *wx,int *wz)
{
    long long rx,rz,norm;
    if (!view || !wx || !wz || view->span<=0 || view->width<=0 || view->height<=0 ||
        sx<view->x || sx>=view->x+view->width || sy<view->y || sy>=view->y+view->height) return 0;
    norm=(long long)view->rotation_cy*view->rotation_cy+
         (long long)view->rotation_sy*view->rotation_sy;
    if (!norm) return 0;
    rx=(long long)(sx-view->x-view->width/2)*view->span/view->width;
    rz=(long long)(view->y+view->height/2-sy)*view->span/view->width;
    *wx=view->center_x+(int)((rx*view->rotation_cy+rz*view->rotation_sy)*1024/norm);
    *wz=view->center_z+(int)((rz*view->rotation_cy-rx*view->rotation_sy)*1024/norm);
    return 1;
}

int rf_minimap_height_hint(const struct rf_minimap_view *view,int world_y)
{
    int threshold=view && view->layer_threshold>0?view->layer_threshold:1800;
    long long delta=view?(long long)world_y-view->reference_y:0;
    return delta>threshold?1:delta< -threshold?-1:0;
}

static void mini_rect(struct rasterfall_canvas *canvas,const struct rf_minimap_view *view,
                       int x,int y,int w,int h,unsigned color,int alpha)
{
    int right=x+w,bottom=y+h;
    x=mini_clamp(x,view->x,view->x+view->width);
    y=mini_clamp(y,view->y,view->y+view->height);
    right=mini_clamp(right,view->x,view->x+view->width);
    bottom=mini_clamp(bottom,view->y,view->y+view->height);
    rasterfall_canvas_rect(canvas,x,y,right-x,bottom-y,color,alpha);
}

static int mini_outcode(const struct rf_minimap_view *v,int x,int y)
{ return (x<v->x?1:x>=v->x+v->width?2:0)|(y<v->y?4:y>=v->y+v->height?8:0); }

static void mini_line(struct rasterfall_canvas *canvas,const struct rf_minimap_view *v,
                      int x0,int y0,int x1,int y1,unsigned color)
{
    int a=mini_outcode(v,x0,y0),b=mini_outcode(v,x1,y1);
    for (int iteration=0;(a||b) && iteration<8;++iteration) {
        int code=a?a:b,x,y;
        if (a&b) return;
        if (code&12) {
            y=(code&4)?v->y:v->y+v->height-1;
            if (y1==y0) return;
            x=x0+(int)((long long)(x1-x0)*(y-y0)/(y1-y0));
        } else {
            x=(code&1)?v->x:v->x+v->width-1;
            if (x1==x0) return;
            y=y0+(int)((long long)(y1-y0)*(x-x0)/(x1-x0));
        }
        if (code==a) { x0=x;y0=y;a=mini_outcode(v,x0,y0); }
        else { x1=x;y1=y;b=mini_outcode(v,x1,y1); }
    }
    if (a||b) return;
    {
        int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
        int dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,err=dx+dy;
        for (;;) {
            int twice=2*err;
            rasterfall_canvas_rect(canvas,x0,y0,1,1,color,255);
            if (x0==x1 && y0==y1) break;
            if (twice>=dy) { err+=dy;x0+=sx; }
            if (twice<=dx) { err+=dx;y0+=sy; }
        }
    }
}

void rf_minimap_layout(struct rasterfall_canvas *canvas,
                       const struct rf_minimap_state *state,const struct rf_minimap_view *v,
                       unsigned terrain_color,unsigned accent)
{
    int occupied_x[RF_MINIMAP_MAX_MARKERS],occupied_y[RF_MINIMAP_MAX_MARKERS],occupied=0;
    if (!canvas || !state || !v || !state->ready) return;
    mini_rect(canvas,v,v->x,v->y,v->width,v->height,0x101D29,235);
    for (int i=0;i<state->terrain_count;++i) {
        const struct rf_minimap_terrain *t=&state->terrain[i];
        int x[4],y[4];
        unsigned color=t->kind==2?terrain_color:t->kind==1?0x354C5A:0x263944;
        rf_minimap_world_to_screen(v,t->minx,t->minz,&x[0],&y[0]);
        rf_minimap_world_to_screen(v,t->maxx,t->minz,&x[1],&y[1]);
        rf_minimap_world_to_screen(v,t->maxx,t->maxz,&x[2],&y[2]);
        rf_minimap_world_to_screen(v,t->minx,t->maxz,&x[3],&y[3]);
        if (!v->rotation_sy && v->rotation_cy==1024)
            mini_rect(canvas,v,x[0],y[2],x[1]-x[0]+1,y[0]-y[2]+1,color,255);
        else for (int edge=0;edge<4;++edge)
            mini_line(canvas,v,x[edge],y[edge],x[(edge+1)%4],y[(edge+1)%4],color);
    }
    /* Clip the camera footprint to the map, before drawing friendly markers. */
    if(v->sight_count==3) {
        int x[3],y[3],top=v->y+v->height,bottom=v->y;
        for(int i=0;i<3;++i) {
            rf_minimap_world_to_screen(v,v->sight_x[i],v->sight_z[i],&x[i],&y[i]);
            if(y[i]<top)top=y[i];
            if(y[i]>bottom)bottom=y[i];
        }
        top=mini_clamp(top,v->y,v->y+v->height-1);
        bottom=mini_clamp(bottom,v->y,v->y+v->height-1);
        for(int row=top;row<=bottom;++row) {
            int left=0,right=0,hits=0;
            for(int i=0;i<3;++i) {
                int j=(i+1)%3;
                if((y[i]<=row && y[j]>row)||(y[j]<=row && y[i]>row)) {
                    int at=x[i]+(int)((long long)(x[j]-x[i])*(row-y[i])/(y[j]-y[i]));
                    if(!hits)left=right=at;
                    if(at<left)left=at;
                    if(at>right)right=at;
                    ++hits;
                }
            }
            if(hits>1)mini_rect(canvas,v,left,row,right-left+1,1,accent,45);
        }
    }
    for(int i=0;i<v->sight_count && i<4;++i) {
        int j=(i+1)%v->sight_count,x0,y0,x1,y1;
        rf_minimap_world_to_screen(v,v->sight_x[i],v->sight_z[i],&x0,&y0);
        rf_minimap_world_to_screen(v,v->sight_x[j],v->sight_z[j],&x1,&y1);
        mini_line(canvas,v,x0,y0,x1,y1,accent);
    }
    for (int pass=0;pass<2;++pass) for (int i=0;i<state->marker_count;++i) {
        const struct rf_minimap_marker *m=&state->markers[i];
        int x,y,inside,layer,duplicate=0;
        unsigned color=m->kind==RF_MINIMAP_OBJECTIVE?0xF5C66B:
            m->kind==RF_MINIMAP_DEVICE?0xB8A3EC:accent;
        if ((m->kind==RF_MINIMAP_PLAYER)!=(pass==1)) continue;
        inside=rf_minimap_world_to_screen(v,m->x,m->z,&x,&y);
        if (!inside && m->kind!=RF_MINIMAP_OBJECTIVE) continue;
        x=mini_clamp(x,v->x+5,v->x+v->width-6);
        y=mini_clamp(y,v->y+5,v->y+v->height-6);
        if (m->kind==RF_MINIMAP_ALLY || m->kind==RF_MINIMAP_DEVICE) {
            for (int j=0;j<occupied;++j)
                if (occupied_x[j]==x/7 && occupied_y[j]==y/7) { duplicate=1;break; }
            if (duplicate) continue;
            occupied_x[occupied]=x/7;occupied_y[occupied++]=y/7;
        }
        if (m->kind==RF_MINIMAP_PLAYER) {
            int dx=(m->sy*v->rotation_cy-m->cy*v->rotation_sy)/1024;
            int dz=(m->sy*v->rotation_sy+m->cy*v->rotation_cy)/1024;
            int tx=x+dx*7/1024,ty=y-dz*7/1024;
            mini_line(canvas,v,tx,ty,x-dx*3/1024+dz*4/1024,
                y+dz*3/1024+dx*4/1024,0xF1FCFF);
            mini_line(canvas,v,tx,ty,x-dx*3/1024-dz*4/1024,
                y+dz*3/1024-dx*4/1024,0xF1FCFF);
            mini_rect(canvas,v,x-1,y-1,3,3,accent,255);
        } else if (m->kind==RF_MINIMAP_ALLY) {
            mini_rect(canvas,v,x-2,y-2,4,4,color,255);
        } else {
            mini_line(canvas,v,x,y-4,x+4,y,color);
            mini_line(canvas,v,x+4,y,x,y+4,color);
            mini_line(canvas,v,x,y+4,x-4,y,color);
            mini_line(canvas,v,x-4,y,x,y-4,color);
        }
        layer=rf_minimap_height_hint(v,m->y);
        if (layer) {
            mini_line(canvas,v,x+4,y-layer*3,x+6,y-layer*5,color);
            mini_line(canvas,v,x+6,y-layer*5,x+8,y-layer*3,color);
        }
    }
    rasterfall_canvas_text(canvas,v->x+5,v->y+3,"N",0xABC4D1);
}

int rf_minimap_logic_test(void)
{
    struct rf_minimap_view view={10,20,200,100,250,-500,10000,0,1024,200,1000,0,{0},{0}};
    int x,y,wx,wz;
    if (!rf_minimap_world_to_screen(&view,250,-500,&x,&y) || x!=110 || y!=70) return -1;
    if (!rf_minimap_world_to_screen(&view,2250,500,&x,&y) || x!=150 || y!=50) return -2;
    if (!rf_minimap_screen_to_world(&view,x,y,&wx,&wz) || wx!=2250 || wz!=500) return -3;
    view.rotation_sy=1024;view.rotation_cy=0;
    if (!rf_minimap_world_to_screen(&view,2250,500,&x,&y) || x!=90 || y!=30) return -4;
    if (!rf_minimap_screen_to_world(&view,x,y,&wx,&wz) || wx!=2250 || wz!=500) return -5;
    if (rf_minimap_screen_to_world(&view,9,30,&wx,&wz)) return -6;
    view.span=20000;
    if (!rf_minimap_world_to_screen(&view,2250,500,&x,&y) || x!=100 || y!=50) return -7;
    if (rf_minimap_height_hint(&view,1201)!=1 || rf_minimap_height_hint(&view,-801)!=-1 ||
        rf_minimap_height_hint(&view,1200)!=0) return -8;
    return 0;
}
