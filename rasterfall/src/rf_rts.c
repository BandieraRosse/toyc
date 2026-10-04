#include "tlibc_everything.h"
#include "rf_rts.h"
#include "rasterfall_units.h"

int rf_rts_selectable(const struct toy_game *g,int i)
{
    if(!g || i<0 || i>=TOY_GAME_MAX_ACTORS)return 0;
    const struct toy_game_actor *a=&g->actors[i];
    return a->active && a->state!=TOY_GAME_ACTOR_DEAD &&
        a->faction==TOY_GAME_FACTION_ALLIED && !a->developer_only &&
        !a->base_core && !a->animation_demo &&
        (a->kind==TOY_GAME_ACTOR_AI || a==toy_game_local_player_actor_const(g));
}
int rf_rts_member_valid(const struct rf_rts_member *m,const struct toy_game *g,int i)
{
    return m && m->active && rf_rts_selectable(g,i) &&
        m->actor_id==g->actors[i].actor_id && m->generation==g->actors[i].combat_generation;
}
void rf_rts_clear(struct rf_rts_state *s)
{
    memset(s->selected,0,sizeof(s->selected));
    s->primary=s->hovered=s->active_group=-1;s->page=0;
}
void rf_rts_sync(struct rf_rts_state *s,const struct toy_game *g,uint64_t world)
{
    if(s->world_generation!=world) {
        memset(s,0,sizeof(*s));s->world_generation=world;rf_rts_clear(s);
    }
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        if(!rf_rts_member_valid(&s->selected[i],g,i))s->selected[i].active=0;
        for(int n=0;n<RF_RTS_GROUPS;++n)
            if(!rf_rts_member_valid(&s->groups[n][i],g,i))s->groups[n][i].active=0;
    }
    if(s->primary<0 || s->primary>=TOY_GAME_MAX_ACTORS || !s->selected[s->primary].active)
        s->primary=rf_rts_nth(s,g,0);
    if(s->hovered<0 || s->hovered>=TOY_GAME_MAX_ACTORS || !s->selected[s->hovered].active)s->hovered=-1;
}
void rf_rts_select(struct rf_rts_state *s,const struct toy_game *g,int i,int add)
{
    if(!add)rf_rts_clear(s);
    if(!rf_rts_selectable(g,i))return;
    s->selected[i].active=1;s->selected[i].actor_id=g->actors[i].actor_id;
    s->selected[i].generation=g->actors[i].combat_generation;
    if(s->primary<0)s->primary=i;
    s->active_group=-1;s->page=0;
}
int rf_rts_nth(const struct rf_rts_state *s,const struct toy_game *g,int n)
{
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)
        if(rf_rts_member_valid(&s->selected[i],g,i) && n--==0)return i;
    return -1;
}
int rf_rts_count(const struct rf_rts_state *s,const struct toy_game *g)
{
    int count=0;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)count+=rf_rts_member_valid(&s->selected[i],g,i);
    return count;
}
int rf_rts_group_count(const struct rf_rts_state *s,const struct toy_game *g,int n)
{
    int count=0;if(n<0 || n>=RF_RTS_GROUPS)return 0;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)count+=rf_rts_member_valid(&s->groups[n][i],g,i);
    return count;
}
void rf_rts_group(struct rf_rts_state *s,const struct toy_game *g,int n,int assign)
{
    if(n<0 || n>=RF_RTS_GROUPS)return;
    if(assign) {
        memset(s->groups[n],0,sizeof(s->groups[n]));
        for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)
            if(rf_rts_member_valid(&s->selected[i],g,i))s->groups[n][i]=s->selected[i];
    } else {
        rf_rts_clear(s);
        for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)
            if(rf_rts_member_valid(&s->groups[n][i],g,i))s->selected[i]=s->groups[n][i];
        s->primary=rf_rts_nth(s,g,0);s->active_group=n;
    }
}
int rf_rts_project(const struct camera *c,int w,int h,int x,int y,int z,int *sx,int *sy)
{
    long long dx=(long long)x-c->x,dz=(long long)z-c->z,dy=(long long)y-c->y;
    long long wx=(dx*c->cy-dz*c->sy)/1024,wz=(dx*c->sy+dz*c->cy)/1024;
    long long vy=(dy*c->pitch_cy-wz*c->pitch_sy)/1024;
    long long vz=(dy*c->pitch_sy+wz*c->pitch_cy)/1024;
    if(vz<64 || w<=0 || h<=0)return 0;
    long long px=w/2+wx*(w*3/4)/vz,py=h/2-vy*(w*3/4)/vz;
    if(px<-100000 || px>100000 || py<-100000 || py>100000)return 0;
    *sx=(int)px;*sy=(int)py;return 1;
}
static int actor_screen(const struct toy_game_actor *a,const struct camera *c,int w,int h,int *x,int *y)
{
    return rf_rts_project(c,w,h,a->x,RASTERFALL_WORLD_GROUND_Y+a->ground_y+
        a->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU/2,a->z,x,y);
}
int rf_rts_pick(const struct toy_game *g,const struct camera *c,int w,int h,int x,int y)
{
    int best=-1;long long distance=0;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        int sx,sy,tx,ty,radius=12;
        const struct toy_game_actor *a=&g->actors[i];
        if(!rf_rts_selectable(g,i) || !actor_screen(a,c,w,h,&sx,&sy))continue;
        if(rf_rts_project(c,w,h,a->x,RASTERFALL_WORLD_GROUND_Y+a->ground_y+
            a->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU,a->z,&tx,&ty)) {
            int d=abs(tx-sx)+abs(ty-sy);if(d>radius)radius=d;
        }
        long long d=(long long)(x-sx)*(x-sx)+(long long)(y-sy)*(y-sy);
        if(d<=(long long)radius*radius && (best<0 || d<distance)){best=i;distance=d;}
    }
    return best;
}
void rf_rts_box(struct rf_rts_state *s,const struct toy_game *g,const struct camera *c,
    int w,int h,int x0,int y0,int x1,int y1,int add)
{
    if(x0>x1){int t=x0;x0=x1;x1=t;}if(y0>y1){int t=y0;y0=y1;y1=t;}
    if(!add)rf_rts_clear(s);
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        int x,y;
        if(rf_rts_selectable(g,i) && actor_screen(&g->actors[i],c,w,h,&x,&y) &&
            x>=0 && x<w && y>=0 && y<h && x>=x0 && x<=x1 && y>=y0 && y<=y1)
            rf_rts_select(s,g,i,1);
    }
}
int rf_rts_logic_test(void)
{
    static struct toy_game g;struct rf_rts_state s={0};
    memset(&g,0,sizeof(g));rf_rts_sync(&s,&g,1);
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        struct toy_game_actor *a=&g.actors[i];
        a->active=1;a->actor_id=i+100;a->combat_generation=i+1;
        a->kind=TOY_GAME_ACTOR_AI;a->state=TOY_GAME_ACTOR_ALIVE;
        a->faction=TOY_GAME_FACTION_ALLIED;a->hp=a->max_hp=100;
        rf_rts_select(&s,&g,i,1);
    }
    rf_rts_group(&s,&g,0,1);rf_rts_clear(&s);rf_rts_group(&s,&g,0,0);
    if(rf_rts_count(&s,&g)!=TOY_GAME_MAX_ACTORS)return 1;
    rf_rts_select(&s,&g,0,0);rf_rts_group(&s,&g,1,1);
    rf_rts_select(&s,&g,1,0);rf_rts_group(&s,&g,1,1);rf_rts_group(&s,&g,1,1);
    rf_rts_group(&s,&g,1,0);
    if(rf_rts_group_count(&s,&g,1)!=1 || rf_rts_count(&s,&g)!=1 ||
        rf_rts_nth(&s,&g,0)!=1 || rf_rts_group_count(&s,&g,0)!=TOY_GAME_MAX_ACTORS)return 2;
    rf_rts_select(&s,&g,0,1);rf_rts_group(&s,&g,1,1);
    if(rf_rts_group_count(&s,&g,1)!=2)return 7;
    g.actors[0].combat_generation++;g.actors[1].state=TOY_GAME_ACTOR_DEAD;
    rf_rts_group(&s,&g,1,0);if(rf_rts_count(&s,&g))return 3;
    rf_rts_group(&s,&g,0,1);if(rf_rts_group_count(&s,&g,0))return 4;
    rf_rts_select(&s,&g,2,0);rf_rts_group(&s,&g,9,1);rf_rts_sync(&s,&g,2);
    if(rf_rts_count(&s,&g) || rf_rts_group_count(&s,&g,9))return 5;
    /* Reversed drag corners, hostile and developer exclusion. */
    struct camera c={0};c.y=10000;c.pitch_sy=-1024;c.cy=1024;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)g.actors[i].x=100000;
    g.actors[2].x=g.actors[3].x=g.actors[4].x=0;
    g.actors[3].faction=TOY_GAME_FACTION_HOSTILE;g.actors[4].developer_only=1;
    rf_rts_box(&s,&g,&c,1280,720,700,400,600,300,0);
    if(rf_rts_count(&s,&g)!=1 || rf_rts_nth(&s,&g,0)!=2 ||
        rf_rts_pick(&g,&c,1280,720,640,360)!=2)return 6;
    return 0;
}
