#include "rf_tactical.h"
#include "rf_tactical_weapon.h"
#include "rf_tactical_prediction.h"
#include "rf_ai_host.h"
#include "rasterfall_character.h"
#include "rasterfall_units.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* All geometry is in metres. The static map is immutable after baking. The
 * mutable world owns execution, weapon clocks, resource recovery and RNG. */
#define TAC_BODY_RADIUS 0.28f
#define TAC_NAV_SPEED RF_TW_BASE_MOVE_SPEED_MPS
#define TAC_CAPTURE_MS 2000

static unsigned int tac_hash_word(unsigned int h,unsigned int value);
static unsigned int tac_float_word(float f);
static unsigned tac_geometry_hash(const struct toy_game *g);

static float tac_min(float a, float b) { return a < b ? a : b; }
static float tac_max(float a, float b) { return a > b ? a : b; }
static float tac_abs(float a) { return a < 0.0f ? -a : a; }
static float tac_distance(struct rf_tac_vec a, struct rf_tac_vec b)
{ float x = a.x - b.x, y = a.y - b.y; return sqrtf(x*x + y*y); }
static int tac_valid_pos(struct rf_tac_vec p)
{ return p.x >= 0.35f && p.x <= RF_TAC_WIDTH_M-0.35f && p.y >= 0.35f && p.y <= RF_TAC_HEIGHT_M-0.35f; }
static unsigned int tac_generation(const struct rf_tac_world *w,int team)
{
    unsigned h=w->map->generation ^ (w->seed*0x9e3779b9u) ^
        (w->order_revision[team]*0x7f4a7c15u) ^ ((unsigned)team*0x27d4eb2du);
    for(int t=0;t<2;++t)for(int i=0;i<w->squad_size;++i){
        const struct rf_tac_unit *u=&w->units[t*RF_TAC_MAX_SQUAD+i];
        h=(h^(unsigned)u->actor_id)*16777619u;h=(h^u->actor_generation)*16777619u;
    }
    return h;
}

static int tac_x(float x) { return (int)lroundf((x-RF_TAC_WIDTH_M/2)*512); }
static int tac_z(float z) { return (int)lroundf((z-RF_TAC_HEIGHT_M/2)*512); }
static int tac_move_clear(const struct rf_tac_map *m, struct rf_tac_vec a, struct rf_tac_vec b)
{
    if (!m->geometry || !tac_valid_pos(a) || !tac_valid_pos(b)) return 0;
    return toy_game_short_connection(m->geometry,tac_x(a.x),tac_z(a.y),
        tac_x(b.x),tac_z(b.y),TOY_GAME_PLAYER_RADIUS,0);
}
static int tac_ray_clear(const struct rf_tac_map *m, struct rf_tac_vec a,
                         float az, struct rf_tac_vec b, float bz)
{
    struct toy_game_actor actor={0};
    actor.x=tac_x(a.x);actor.z=tac_z(a.y);
    actor.ground_y=(int)(az*512)-RASTERFALL_HUMAN_EYE_HEIGHT_RFU;
    return m->geometry && toy_game_combat_visible(m->geometry,&actor,
        tac_x(b.x),(int)(bz*512),tac_z(b.y));
}

/* Nearest rectangle face defines protection direction, never a scalar score.
 * The normal points out of the obstacle towards the protected soldier. */
static int tac_cover_at(const struct rf_tac_map *m, struct rf_tac_vec p,
                         struct rf_tac_vec *normal)
{
    int i, best = -1; float best_dist = 0.91f;
    normal->x = normal->y = 0.0f;
    for (i = 0; i < m->cover_count; ++i) {
        const struct rf_tac_cover *c = &m->covers[i];
        float x = tac_max(c->x0,tac_min(c->x1,p.x));
        float y = tac_max(c->y0,tac_min(c->y1,p.y));
        float dx = p.x-x, dy = p.y-y, d = sqrtf(dx*dx+dy*dy);
        if (d < 0.001f || d >= best_dist) continue;
        best = i; best_dist = d; normal->x = dx/d; normal->y = dy/d;
    }
    return best;
}

/* Exposure is descriptive only. No imaginary lean or displaced shot origin. */
static int tac_precise_exposure(const struct rf_tac_map *m,
                                struct rf_tac_vec from, struct rf_tac_vec to)
{
    int count=0;
    const float h=RASTERFALL_HUMAN_HEIGHT_RFU/512.0f;
    for(int i=0;i<3;++i)
        count+=tac_ray_clear(m,from,RASTERFALL_HUMAN_EYE_HEIGHT_RFU/512.0f,to,h*(0.3f+0.3f*i));
    return count;
}

float rf_tac_exposure(const struct rf_tac_map *map, struct rf_tac_vec from,
                      struct rf_tac_vec to)
{
    if (!map || !tac_valid_pos(from) || !tac_valid_pos(to)) return 0.0f;
    return (float)tac_precise_exposure(map,from,to) / 3.0f;
}

static void tac_add_cover(struct rf_tac_map *m, float x0, float y0,
                          float x1, float y1, int height)
{
    struct rf_tac_cover *c;
    if (m->cover_count >= RF_TAC_MAX_COVERS) return;
    c = &m->covers[m->cover_count++];
    c->x0=x0; c->y0=y0; c->x1=x1; c->y1=y1; c->height=height;
}

static int tac_add_node(struct rf_tac_map *m, struct rf_tac_vec p)
{
    struct rf_tac_node *n; const struct rf_tac_cover *c; int ci, i;
    if (m->node_count >= RF_TAC_MAX_NODES || !tac_move_clear(m,p,p)) return -1;
    for (i = 0; i < m->node_count; ++i)
        if (tac_distance(m->nodes[i].pos,p) < 0.18f) return i;
    n = &m->nodes[m->node_count]; n->pos=p;
    ci = tac_cover_at(m,p,&n->cover_normal); n->cover_index=ci;
    if (ci >= 0) {
        c = &m->covers[ci]; n->height=c->height;
        n->fire_over = c->height == RF_TAC_LOW;
        if (c->height == RF_TAC_HIGH) {
            if (tac_abs(n->cover_normal.x) > tac_abs(n->cover_normal.y)) {
                n->peek_left = tac_abs(p.y-c->y0) <= 0.8f;
                n->peek_right = tac_abs(p.y-c->y1) <= 0.8f;
            } else {
                n->peek_left = tac_abs(p.x-c->x0) <= 0.8f;
                n->peek_right = tac_abs(p.x-c->x1) <= 0.8f;
            }
        }
    }
    return m->node_count++;
}

static void tac_link(struct rf_tac_map *m, int a, int b)
{
    int i;
    if (a < 0 || b < 0 || a == b) return;
    for (i=0;i<m->neighbor_count[a];++i) if (m->neighbors[a][i] == b) return;
    if (m->neighbor_count[a] >= RF_TAC_NEIGHBORS || m->neighbor_count[b] >= RF_TAC_NEIGHBORS) return;
    m->neighbors[a][m->neighbor_count[a]++] = (short)b;
    m->neighbors[b][m->neighbor_count[b]++] = (short)a;
}

int rf_tac_nearest_node(const struct rf_tac_map *map, struct rf_tac_vec pos)
{
    int i, best=-1; float distance=1.0e20f;
    if (!map || !tac_valid_pos(pos)) return -1;
    for (i=0;i<map->node_count;++i) {
        float d = tac_distance(pos,map->nodes[i].pos);
        if (d < distance && tac_move_clear(map,pos,map->nodes[i].pos)) { best=i; distance=d; }
    }
    return best;
}

static int tac_map_geometry(struct rf_tac_map *map);
static void tac_map_bake(struct rf_tac_map *map)
{
    int x,y,i,j,grid_count;
    map->node_count=0;
    memset(map->neighbor_count,0,sizeof(map->neighbor_count));
    for (i=0;i<RF_TAC_GRID_W*RF_TAC_GRID_H;++i) map->grid_node[i]=-1;
    for (y=0;y<RF_TAC_GRID_H;++y) for (x=0;x<RF_TAC_GRID_W;++x) {
        struct rf_tac_vec p; p.x=1.0f+x*RF_TAC_GRID_M; p.y=1.0f+y*RF_TAC_GRID_M;
        map->grid_node[y*RF_TAC_GRID_W+x]=tac_add_node(map,p);
    }
    grid_count=map->node_count;
    for (i=0;i<map->cover_count;++i) {
        const struct rf_tac_cover *c=&map->covers[i]; struct rf_tac_vec p;
        for (j=0;j<2;++j) {
            p.x=c->x0-0.43f; p.y=j?c->y1-0.20f:c->y0+0.20f; tac_add_node(map,p);
            p.x=c->x1+0.43f; tac_add_node(map,p);
            p.x=j?c->x1-0.20f:c->x0+0.20f; p.y=c->y0-0.43f; tac_add_node(map,p);
            p.y=c->y1+0.43f; tac_add_node(map,p);
        }
    }
    for (y=0;y<RF_TAC_GRID_H;++y) for (x=0;x<RF_TAC_GRID_W;++x) {
        int a=map->grid_node[y*RF_TAC_GRID_W+x],dx,dy;
        if (a<0) continue;
        for (dy=-1;dy<=1;++dy) for (dx=-1;dx<=1;++dx) {
            int xx=x+dx,yy=y+dy,b;
            if (xx<0 || xx>=RF_TAC_GRID_W || yy<0 || yy>=RF_TAC_GRID_H) continue;
            b=map->grid_node[yy*RF_TAC_GRID_W+xx];
            if (b>a && tac_move_clear(map,map->nodes[a].pos,map->nodes[b].pos)) tac_link(map,a,b);
        }
    }
    for (i=grid_count;i<map->node_count;++i) {
        int linked=0;
        for (j=0;j<grid_count && linked<4;++j)
            if (tac_distance(map->nodes[i].pos,map->nodes[j].pos)<=3.0f &&
                tac_move_clear(map,map->nodes[i].pos,map->nodes[j].pos)) { tac_link(map,i,j); ++linked; }
    }
    for(i=0;i<map->node_count;++i){
        map->ai_nodes[i].position.x=map->nodes[i].pos.x;
        map->ai_nodes[i].position.z=map->nodes[i].pos.y;
        map->ai_nodes[i].neighbor_count=map->neighbor_count[i];
        memcpy(map->ai_nodes[i].neighbors,map->neighbors[i],sizeof(map->ai_nodes[i].neighbors));
    }
}

int rf_tac_map_generate(struct rf_tac_map *map, unsigned int seed)
{
    unsigned int rng = seed ? seed : 0xa341316cu;
    int i,j;
    if (!map) return 0;
    memset(map,0,sizeof(*map)); map->seed=seed;
    map->generation=(seed*0x85ebca6bu)^0x52465431u;
    map->objective.x=82.0f; map->objective.y=36.0f; map->objective_radius=6.0f;
    map->spawn[0].x=8.0f; map->spawn[0].y=36.0f;
    map->spawn[1]=map->objective;
    /* Half-open strongpoint: rear/side protection, broken frontal cover and
     * a final 7m approach without stepping-stone cover. Flanks remain open. */
    tac_add_cover(map,87.0f,29.0f,87.8f,35.0f,RF_TAC_HIGH);
    tac_add_cover(map,87.0f,37.0f,87.8f,43.0f,RF_TAC_HIGH);
    tac_add_cover(map,80.0f,28.0f,86.0f,28.7f,RF_TAC_HIGH);
    tac_add_cover(map,80.0f,43.3f,86.0f,44.0f,RF_TAC_HIGH);
    tac_add_cover(map,78.0f,30.0f,78.7f,33.0f,RF_TAC_LOW);
    tac_add_cover(map,78.0f,39.0f,78.7f,42.0f,RF_TAC_LOW);
    /* Full-height screens block every starting squad ray, with two exits. */
    tac_add_cover(map,20.0f,24.0f,20.8f,48.0f,RF_TAC_HIGH);
    /* Every band varies cover location, height and orientation. Three broad
     * advance lanes remain disconnected by gaps rather than enclosed walls. */
    for (i=0;i<4;++i) for (j=0;j<3;++j) {
        float cx=30.0f+i*10.0f+(float)(rf_tw_rng_next(&rng)%401)/100.0f;
        float cy=12.0f+j*22.0f+(float)(rf_tw_rng_next(&rng)%601)/100.0f-3.0f;
        int height=(rf_tw_rng_next(&rng)%3)!=0 ? RF_TAC_HIGH : RF_TAC_LOW;
        if (rf_tw_rng_next(&rng)&1u) tac_add_cover(map,cx,cy,cx+0.7f,cy+5.0f,height);
        else tac_add_cover(map,cx,cy,cx+5.0f,cy+0.7f,height);
    }
    if(!tac_map_geometry(map)) return 0;
    tac_map_bake(map);
    map->content_hash=tac_hash_word(2166136261u,map->generation);
    for(i=0;i<map->cover_count;++i) {
        const struct rf_tac_cover *c=&map->covers[i];
        map->content_hash=tac_hash_word(map->content_hash,tac_float_word(c->x0));
        map->content_hash=tac_hash_word(map->content_hash,tac_float_word(c->y0));
        map->content_hash=tac_hash_word(map->content_hash,tac_float_word(c->x1));
        map->content_hash=tac_hash_word(map->content_hash,tac_float_word(c->y1));
        map->content_hash=tac_hash_word(map->content_hash,c->height);
    }
    for(i=0;i<map->node_count;++i) {
        map->content_hash=tac_hash_word(map->content_hash,tac_float_word(map->nodes[i].pos.x));
        map->content_hash=tac_hash_word(map->content_hash,tac_float_word(map->nodes[i].pos.y));
        for(j=0;j<map->neighbor_count[i];++j)
            map->content_hash=tac_hash_word(map->content_hash,map->neighbors[i][j]);
    }
    map->content_hash=tac_geometry_hash(map->geometry);
    return rf_tac_nearest_node(map,map->spawn[0])>=0 && rf_tac_nearest_node(map,map->objective)>=0;
}

#include "rf_tactical_game.inc"

int rf_tac_command(struct rf_tac_world *world, int team, int kind,
                   struct rf_tac_vec target, float radius)
{
    int i;
    if (!world || !world->map || world->finished || team<0 || team>1 ||
        (kind!=RF_TAC_DEFEND && kind!=RF_TAC_ATTACK) || !tac_valid_pos(target) ||
        !(radius>=1.0f && radius<=16.0f) || rf_tac_nearest_node(world->map,target)<0) return 0;
    world->orders[team].kind=kind; world->orders[team].target=target;
    world->orders[team].radius=radius; world->orders[team].captured=0; world->orders[team].clear_ms=0;
    ++world->order_revision[team];
    for (i=0;i<world->squad_size;++i) {
        struct rf_tac_unit *u=&world->units[team*RF_TAC_MAX_SQUAD+i];
        u->action.kind=RF_TAC_HOLD; u->action.target=-1; u->destination=u->pos; u->nav_count=u->nav_cursor=0; u->nav_kind=RF_TAC_CURRENT;
    }
    return 1;
}

/* A binary heap keeps one source-to-all navigation query cheap. The shortest
 * geometric path is engine fact; accumulated threat is reported, not used as
 * a hidden strategy score or navigation preference. */
struct tac_paths {
    float distance[RF_TAC_MAX_NODES];
    short previous[RF_TAC_MAX_NODES], heap[RF_TAC_MAX_NODES], position[RF_TAC_MAX_NODES];
    int count, source;
};
static void tac_heap_up(struct tac_paths *p,int slot)
{
    while (slot>0) { int parent=(slot-1)/2,a=p->heap[slot],b=p->heap[parent];
        if (p->distance[a]>p->distance[b] || (p->distance[a]==p->distance[b] && a>=b)) break;
        p->heap[slot]=(short)b; p->position[b]=(short)slot; p->heap[parent]=(short)a; p->position[a]=(short)parent; slot=parent;
    }
}
static int tac_heap_pop(struct tac_paths *p)
{
    int result=p->heap[0],slot=0,last=p->heap[--p->count]; p->position[result]=-2;
    if (!p->count) return result;
    p->heap[0]=(short)last; p->position[last]=0;
    for (;;) { int child=slot*2+1,a,b;
        if (child>=p->count) break;
        if (child+1<p->count && p->distance[p->heap[child+1]]<p->distance[p->heap[child]]) ++child;
        a=p->heap[slot]; b=p->heap[child];
        if (p->distance[a]<=p->distance[b]) break;
        p->heap[slot]=(short)b; p->position[b]=(short)slot; p->heap[child]=(short)a; p->position[a]=(short)child; slot=child;
    }
    return result;
}
static void tac_paths_build(const struct rf_tac_map *m,struct rf_tac_vec start,struct tac_paths *p)
{
    int i;
    memset(p,0,sizeof(*p)); p->source=rf_tac_nearest_node(m,start);
    for (i=0;i<m->node_count;++i) { p->distance[i]=1.0e20f; p->previous[i]=-1; p->position[i]=-1; }
    if (p->source<0) return;
    p->distance[p->source]=tac_distance(start,m->nodes[p->source].pos);
    p->heap[0]=(short)p->source; p->position[p->source]=0; p->count=1;
    while (p->count) {
        int a=tac_heap_pop(p),k;
        for (k=0;k<m->neighbor_count[a];++k) {
            int b=m->neighbors[a][k]; float d;
            if (p->position[b]==-2) continue;
            d=p->distance[a]+tac_distance(m->nodes[a].pos,m->nodes[b].pos);
            if (d>=p->distance[b]) continue;
            p->distance[b]=d; p->previous[b]=(short)a;
            if (p->position[b]<0) { p->position[b]=(short)p->count; p->heap[p->count++]=(short)b; }
            tac_heap_up(p,p->position[b]);
        }
    }
}
static int tac_path_nodes(const struct tac_paths *p,int node,short *out)
{
    short backwards[RF_TAC_MAX_PATH]; int count=0,i;
    if (node<0 || p->distance[node]>=1.0e19f) return 0;
    while (node>=0 && count<RF_TAC_MAX_PATH) { backwards[count++]=(short)node; node=p->previous[node]; }
    if (node>=0) return 0;
    for (i=0;i<count;++i) out[i]=backwards[count-1-i];
    return count;
}

static int tac_route_nodes(const struct rf_tac_map *m,struct rf_tac_vec start,
                           const struct tac_paths *paths,int node,short *out)
{
    int count=tac_path_nodes(paths,node,out),skip=0,i;
    /* Replanning must not pull a continuously moving soldier backwards to its
     * nearest grid centre. The same swept connection is used in summaries. */
    if(count>1 && tac_move_clear(m,start,m->nodes[out[1]].pos)) skip=1;
    for(i=skip;i<count;++i) out[i-skip]=out[i];
    return count-skip;
}

static void tac_relation(const struct rf_tac_world *w,const struct rf_tac_unit *from,
    struct rf_tac_vec pos,const struct rf_tac_unit *to,int baked_from,int baked_to,
    struct rf_tac_relation *out)
{
    const struct toy_game_actor *source=toy_game_actor_by_id_const(w->game,from->actor_id);
    const struct toy_game_actor *target=toy_game_actor_by_id_const(w->game,to->actor_id);
    struct toy_game_actor hypothetical;
    const struct toy_game_weapon_info *weapon;
    (void)baked_from;(void)baked_to;
    memset(out,0,sizeof(*out));out->distance=tac_distance(pos,to->pos);
    if(!source || !target || !from->alive || !to->alive) return;
    hypothetical=*source;hypothetical.x=tac_x(pos.x);hypothetical.z=tac_z(pos.y);
    weapon=toy_game_weapon_info(toy_game_actor_current_weapon(source));
    out->visible=out->distance*512<=weapon->range && toy_game_combat_visible(w->game,
        &hypothetical,tac_x(to->pos.x),target->ground_y+target->airborne_y+
        RASTERFALL_HUMAN_HEIGHT_RFU*64/100,tac_z(to->pos.y));
    out->exposure=out->visible?rf_tac_exposure(w->map,pos,to->pos):0;
    /* Conservative potential, not an alternate hit/damage simulator. Actual
     * finite-ammo, geometry, skills and recoil are resolved by Game rollout. */
    if(out->visible) {
        float spread=toy_game_actor_current_spread(source)/1024.0f*out->distance*512;
        out->hit_rate=fminf(1.0f,TOY_GAME_PLAYER_RADIUS/fmaxf(1,spread))*out->exposure;
        float cycle=(weapon->mag_size-1)*toy_game_actor_fire_cooldown_ms(source,weapon)+
            toy_game_actor_reload_ms(source,weapon);
        out->expected_dps=weapon->damage*weapon->pellets*weapon->mag_size*1000.0f/cycle*out->hit_rate;
        if(from->reload_ms || !from->ammo) out->expected_dps=0;
    }
}

static void tac_view(const struct rf_tac_unit *u,struct rf_tac_unit_view *out)
{
    out->id=u->id; out->alive=u->alive; out->weapon=u->weapon; out->ammo=u->ammo;
    out->reload_ms=u->reload_ms; out->cooldown_ms=u->cooldown_ms; out->pos=u->pos;
    out->effective_health=u->alive?u->hp+u->evasion:0.0f;
    out->max_effective_health=RF_TW_BASE_HP+RF_TW_BASE_RISK;
}

static int tac_pick_node(const struct rf_tac_map *m,const struct tac_paths *paths,
                         struct rf_tac_vec desired,int need_cover,int side,
                         struct rf_tac_vec origin,struct rf_tac_vec direction)
{
    int i,best=-1; float best_distance=1.0e20f;
    for (i=0;i<m->node_count;++i) {
        float d; const struct rf_tac_node *node=&m->nodes[i];
        if (paths->distance[i]>=1.0e19f || paths->distance[i]>22.0f) continue;
        if (need_cover && node->cover_index<0) continue;
        if (side) {
            float cross=direction.x*(node->pos.y-origin.y)-direction.y*(node->pos.x-origin.x);
            if ((side<0 && cross>-0.5f) || (side>0 && cross<0.5f)) continue;
        }
        d=tac_distance(node->pos,desired);
        if (d<best_distance) { best_distance=d; best=i; }
    }
    return best;
}

static int tac_candidates(const struct rf_tac_world *world,int team,
                           const struct rf_tac_unit *u,struct tac_paths *paths,int *nodes,int *kinds)
{
    struct rf_tac_vec direction,desired[RF_TAC_CANDIDATES];
    const struct rf_tac_order *order=&world->orders[team]; float length; int k,count=0;
    short goal_route[RF_TAC_MAX_PATH]; int goal,route_count,advance=-1;
    tac_paths_build(world->map,u->pos,paths);
    goal=rf_tac_nearest_node(world->map,order->target);
    route_count=tac_route_nodes(world->map,u->pos,paths,goal,goal_route);
    if(route_count) {
        struct rf_tac_vec previous=u->pos; float travelled=0.0f; int r;
        for(r=0;r<route_count;++r) {
            travelled+=tac_distance(previous,world->map->nodes[goal_route[r]].pos);
            advance=goal_route[r]; previous=world->map->nodes[advance].pos;
            if(travelled>=9.0f) break;
        }
    }
    /* A route is a persistent execution intention. Refreshing the nearest
     * node at every tactical tick can alternate across an edge/cover anchor
     * and pull the body backwards. Continue a proven advance until its node
     * is reached; solvers can still interrupt it with FIRE or another role. */
    if(u->action.kind==RF_TAC_MOVE && u->nav_kind==RF_TAC_ADVANCE && u->nav_cursor<u->nav_count)
        advance=u->nav_nodes[u->nav_count-1];
    direction.x=order->target.x-u->pos.x; direction.y=order->target.y-u->pos.y;
    length=sqrtf(direction.x*direction.x+direction.y*direction.y);
    if(length<0.2f) { direction.x=team==0?1.0f:-1.0f; direction.y=0.0f; }
    else { direction.x/=length; direction.y/=length; }
    for(k=0;k<RF_TAC_CANDIDATES;++k) desired[k]=u->pos;
    desired[1].x+=direction.x*tac_min(9.0f,length); desired[1].y+=direction.y*tac_min(9.0f,length);
    desired[2].x+=direction.x*6.0f-direction.y*7.0f; desired[2].y+=direction.y*6.0f+direction.x*7.0f;
    desired[3].x+=direction.x*6.0f+direction.y*7.0f; desired[3].y+=direction.y*6.0f-direction.x*7.0f;
    desired[4]=desired[1]; desired[4].x-=direction.y*4.0f; desired[4].y+=direction.x*4.0f;
    desired[5]=desired[1]; desired[5].x+=direction.y*4.0f; desired[5].y-=direction.x*4.0f;
    desired[6].x-=direction.x*6.0f; desired[6].y-=direction.y*6.0f;
    desired[7]=u->action.kind==RF_TAC_MOVE?u->destination:order->target;
    for(k=0;k<RF_TAC_CANDIDATES;++k) {
        int n=k==0?paths->source:k==1?advance:tac_pick_node(world->map,paths,desired[k],k==4||k==5,k==4?1:k==5?-1:0,u->pos,direction);
        int previous,duplicate=0;
        if(n<0) continue;
        for(previous=0;previous<count;++previous)
            if(nodes[previous]==n && (previous>0 || tac_distance(u->pos,world->map->nodes[n].pos)<0.0001f)) duplicate=1;
        if(!duplicate) { nodes[count]=n; if(kinds) kinds[count]=k; ++count; }
    }
    return count;
}

static void tac_path_summary(const struct rf_tac_world *w,int team,
                             const struct rf_tac_unit *unit,const struct tac_paths *paths,
                             int node,struct rf_tac_path_summary *out)
{
    short route[RF_TAC_MAX_PATH]; int count,i,e;
    struct rf_tac_vec start=unit->pos;
    memset(out,0,sizeof(*out));
    if(unit->action.kind==RF_TAC_MOVE && unit->nav_cursor<unit->nav_count &&
       node==unit->nav_nodes[unit->nav_count-1]) {
        count=unit->nav_count-unit->nav_cursor;
        memcpy(route,&unit->nav_nodes[unit->nav_cursor],count*sizeof(*route));
    } else count=tac_route_nodes(w->map,unit->pos,paths,node,route);
    for (i=0;i<count;++i) {
        struct rf_tac_vec end=w->map->nodes[route[i]].pos;
        float length=tac_distance(start,end),segment_seconds=length/TAC_NAV_SPEED;
        int sample,samples=(int)ceilf(length/0.75f); float incoming=0.0f,exposed=0.0f;
        if(samples<1) samples=1;
        /* Samples are at most 0.75m apart on the actual route. Project weapon
         * readiness to arrival time; a reload ending in 20ms cannot make a
         * five-second route permanently safe. Enemy positions remain fixed
         * for this inexpensive estimate, rather than pretending to roll out
         * an opponent. DPS is a reload-amortized approximation once ready. */
        for (sample=0;sample<samples;++sample) {
            struct rf_tac_vec point; float t=(sample+0.5f)/samples,sample_dps=0.0f; int threatened=0;
            int arrival_ms=(int)((out->travel_time+segment_seconds*t)*1000.0f);
            point.x=start.x+(end.x-start.x)*t; point.y=start.y+(end.y-start.y)*t;
            for (e=0;e<w->squad_size;++e) {
                const struct rf_tac_unit *enemy=&w->units[(1-team)*RF_TAC_MAX_SQUAD+e];
                struct rf_tac_relation relation;
                struct rf_tac_unit projected=*enemy, moving=*unit;
                if(!enemy->alive) continue;
                moving.pos=point;
                if(projected.reload_ms<=arrival_ms) projected.reload_ms=0;
                tac_relation(w,&projected,projected.pos,&moving,-1,-1,&relation);
                threatened|=relation.visible;
                sample_dps+=relation.expected_dps;
            }
            incoming+=sample_dps/samples; exposed+=(float)threatened/samples;
        }
        out->move_distance+=length; out->travel_time+=segment_seconds;
        out->incoming_damage+=incoming*segment_seconds; out->exposed_time+=exposed*segment_seconds;
        start=end;
    }
}

void rf_tac_observe(const struct rf_tac_world *world,int team,struct rf_tac_observation *out)
{
    struct tac_paths objective_paths; int i,e,goal;
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!world || !world->map || team<0 || team>1) return;
    out->generation=tac_generation(world,team); out->tick=world->tick; out->time_ms=world->time_ms;
    out->team=team; out->count=world->squad_size; out->order=world->orders[team];
    tac_paths_build(world->map,out->order.target,&objective_paths);
    goal=objective_paths.source;
    for (i=0;i<world->squad_size;++i) {
        tac_view(&world->units[team*RF_TAC_MAX_SQUAD+i],&out->friendly[i]);
        tac_view(&world->units[(1-team)*RF_TAC_MAX_SQUAD+i],&out->enemy[i]);
    }
    for (i=0;i<world->squad_size;++i) {
        const struct rf_tac_unit *u=&world->units[team*RF_TAC_MAX_SQUAD+i];
        struct tac_paths paths;
        int nodes[RF_TAC_CANDIDATES],kinds[RF_TAC_CANDIDATES],k,count;
        for (e=0;e<world->squad_size;++e)
            tac_relation(world,u,u->pos,&world->units[(1-team)*RF_TAC_MAX_SQUAD+e],-1,-1,&out->relations[i][e]);
        if (!u->alive) continue;
        count=tac_candidates(world,team,u,&paths,nodes,kinds);
        for (k=0;k<count;++k) {
            struct rf_tac_candidate *c; const struct rf_tac_node *node; int n=nodes[k];
            c=&out->candidates[i][out->candidate_count[i]++]; node=&world->map->nodes[n];
            c->node=n; c->kind=kinds[k]; c->pos=k==0?u->pos:node->pos; c->cover_normal=node->cover_normal;
            c->height=node->height; c->peek_left=node->peek_left; c->peek_right=node->peek_right; c->fire_over=node->fire_over;
            if(k==0) {
                int ci=tac_cover_at(world->map,u->pos,&c->cover_normal);
                c->height=ci<0?0:world->map->covers[ci].height;
                c->fire_over=c->height==RF_TAC_LOW;
                c->peek_left=c->peek_right=0;
                if(ci>=0 && c->height==RF_TAC_HIGH) {
                    const struct rf_tac_cover *cover=&world->map->covers[ci];
                    if(tac_abs(c->cover_normal.x)>tac_abs(c->cover_normal.y)) {
                        c->peek_left=tac_abs(u->pos.y-cover->y0)<=0.8f;
                        c->peek_right=tac_abs(u->pos.y-cover->y1)<=0.8f;
                    } else {
                        c->peek_left=tac_abs(u->pos.x-cover->x0)<=0.8f;
                        c->peek_right=tac_abs(u->pos.x-cover->x1)<=0.8f;
                    }
                }
            }
            c->objective_distance=tac_distance(c->pos,out->order.target);
            c->objective_path_distance=k==0 && goal>=0?paths.distance[goal]+tac_distance(world->map->nodes[goal].pos,out->order.target):objective_paths.distance[n];
            for (e=0;e<world->squad_size;++e) {
                const struct rf_tac_unit *enemy=&world->units[(1-team)*RF_TAC_MAX_SQUAD+e];
                struct rf_tac_relation incoming; int enode=rf_tac_nearest_node(world->map,enemy->pos);
                tac_relation(world,u,c->pos,enemy,k==0?-1:n,enode,&c->relations[e]);
                /* Incoming target must be this candidate rather than u.pos. */
                memset(&incoming,0,sizeof(incoming));
                if (enemy->alive) {
                    struct rf_tac_unit target=*u; target.pos=c->pos; target.action.kind=RF_TAC_HOLD;
                    tac_relation(world,enemy,enemy->pos,&target,enode,k==0?-1:n,&incoming);
                }
                if (c->relations[e].visible) c->visible_enemy_mask|=1u<<e;
                if (incoming.visible) c->exposed_to_enemy_mask|=1u<<e;
                c->outgoing_dps=tac_max(c->outgoing_dps,c->relations[e].expected_dps);
                c->incoming_dps+=incoming.expected_dps;
                if (enemy->alive) c->cover_quality+=1.0f-incoming.exposure;
            }
            { int alive=0; for(e=0;e<world->squad_size;++e) alive+=out->enemy[e].alive;
              if (alive) c->cover_quality/=alive; }
            if (k!=0) tac_path_summary(world,team,u,&paths,n,&c->path);
        }
    }
}

void rf_tac_plan_hold(const struct rf_tac_observation *obs,struct rf_tac_plan *out)
{
    int i; if (!out) return; memset(out,0,sizeof(*out)); out->version=RF_TAC_VERSION;
    if (!obs) return;
    out->generation=obs->generation; out->tick=obs->tick; out->time_ms=obs->time_ms;
    out->team=obs->team; out->count=obs->count;
    for(i=0;i<obs->count && i<RF_TAC_MAX_SQUAD;++i) {
        out->actions[i].kind=RF_TAC_HOLD; out->actions[i].target=-1; out->destinations[i]=obs->friendly[i].pos;
    }
}

static void tac_unit_hold(struct rf_tac_unit *u)
{ u->action.kind=RF_TAC_HOLD; u->action.target=-1; u->destination=u->pos; u->nav_count=u->nav_cursor=0; u->nav_kind=RF_TAC_CURRENT; }

int rf_tac_apply(struct rf_tac_world *world,const struct rf_tac_plan *plan)
{
    int i,valid=1;
    if (!world || !world->map || !plan || world->finished) return 0;
    if (plan->team<0 || plan->team>1) { ++world->invalid_actions; return 0; }
    if ((plan->explicit_actions!=0 && plan->explicit_actions!=1) || plan->version!=RF_TAC_VERSION || plan->generation!=tac_generation(world,plan->team) ||
        plan->tick!=world->tick || plan->time_ms!=world->time_ms || plan->count!=world->squad_size) {
        for(i=0;i<world->squad_size;++i) tac_unit_hold(&world->units[plan->team*RF_TAC_MAX_SQUAD+i]);
        ++world->invalid_actions; return 0;
    }
    for(i=0;i<world->squad_size;++i) {
        struct rf_tac_unit *u=&world->units[plan->team*RF_TAC_MAX_SQUAD+i];
        struct rf_tac_action action=plan->actions[i]; int legal=1;
        u->explicit_actions=plan->explicit_actions;
        if (action.kind<RF_TAC_HOLD || action.kind>RF_TAC_RELOAD) legal=0;
        if (!u->alive) { tac_unit_hold(u); continue; }
        if (legal && action.kind==RF_TAC_FIRE)
            legal=action.target>=0 && action.target<world->squad_size &&
                  world->units[(1-plan->team)*RF_TAC_MAX_SQUAD+action.target].alive &&
                  (plan->explicit_actions || tac_precise_exposure(world->map,u->pos,world->units[(1-plan->team)*RF_TAC_MAX_SQUAD+action.target].pos));
        if (legal && action.kind==RF_TAC_RELOAD) legal=u->reload_ms || u->ammo<toy_game_weapon_info(u->weapon==RF_TW_RIFLE?TOY_GAME_WEAPON_AK:TOY_GAME_WEAPON_SMG)->mag_size;
        if (legal && action.kind==RF_TAC_MOVE && plan->explicit_actions) {
            struct rf_tac_vec destination=plan->destinations[i];
            int node=rf_tac_nearest_node(world->map,destination);
            legal=tac_valid_pos(destination) && node>=0 &&
                tac_move_clear(world->map,world->map->nodes[node].pos,destination);
            if(legal){u->destination=destination;u->nav_count=u->nav_cursor=0;u->nav_kind=RF_TAC_CONTINUE;}
        }
        if (legal && action.kind==RF_TAC_MOVE && !plan->explicit_actions) {
            struct tac_paths paths; int nodes[RF_TAC_CANDIDATES],kinds[RF_TAC_CANDIDATES];
            int k=action.candidate,count=tac_candidates(world,plan->team,u,&paths,nodes,kinds);
            legal=k>0 && k<count && tac_valid_pos(plan->destinations[i]) &&
                  tac_distance(plan->destinations[i],world->map->nodes[nodes[k]].pos)<0.02f;
            if (legal) {
                struct rf_tac_vec destination=world->map->nodes[nodes[k]].pos;
                if (u->action.kind!=RF_TAC_MOVE || tac_distance(u->destination,destination)>0.02f || !u->nav_count) {
                    u->nav_count=tac_route_nodes(world->map,u->pos,&paths,nodes[k],u->nav_nodes); u->nav_cursor=0;
                    legal=u->nav_count>0;
                }
                if (legal) {
                    u->destination=destination;
                    if(kinds[k]!=RF_TAC_CONTINUE || u->action.kind!=RF_TAC_MOVE) u->nav_kind=kinds[k];
                }
            }
        }
        if (!legal) { ++world->invalid_actions; valid=0; tac_unit_hold(u); }
        else { u->action=action; if(action.kind!=RF_TAC_MOVE) { u->nav_count=u->nav_cursor=0; u->nav_kind=RF_TAC_CURRENT; u->destination=u->pos; } }
    }
    /* A current API plan is accepted with per-member HOLD corrections. The
     * legacy interface keeps its old strict return value. Stale headers above
     * still reject the whole plan. One bad action must not abort an AI match. */
    return plan->explicit_actions?1:valid;
}

static int tac_target(const struct rf_tac_world *w,const struct rf_tac_unit *u)
{
    int e,best=-1; float best_distance=1.0e20f;
    if(u->explicit_actions){
        e=u->action.target;
        return u->action.kind==RF_TAC_FIRE && e>=0 && e<w->squad_size &&
            w->units[(1-u->team)*RF_TAC_MAX_SQUAD+e].alive?e:-1;
    }
    if (u->action.kind==RF_TAC_FIRE) {
        e=u->action.target;
        if (e>=0 && e<w->squad_size && w->units[(1-u->team)*RF_TAC_MAX_SQUAD+e].alive &&
            tac_precise_exposure(w->map,u->pos,w->units[(1-u->team)*RF_TAC_MAX_SQUAD+e].pos)) return e;
        return -1;
    }
    for(e=0;e<w->squad_size;++e) {
        const struct rf_tac_unit *enemy=&w->units[(1-u->team)*RF_TAC_MAX_SQUAD+e]; float d;
        if(!enemy->alive) continue;
        d=tac_distance(u->pos,enemy->pos);
        if(d<best_distance && tac_precise_exposure(w->map,u->pos,enemy->pos)) { best=e; best_distance=d; }
    }
    return best;
}

void rf_tac_prepare(struct rf_tac_world *world,int paused)
{
    if(!world || !world->game) return;
    for(int i=0;i<RF_TAC_MAX_UNITS;++i) {
        struct rf_tac_unit *u=&world->units[i];
        struct toy_game_actor *a=toy_game_actor_by_id(world->game,u->actor_id);
        if(!a || a->combat_generation!=u->actor_generation) continue;
        a->simulation_paused=paused;
        a->intent_exclusive=u->explicit_actions;
        int target=tac_target(world,u);
        const struct rf_tac_unit *v=target<0?NULL:&world->units[(1-u->team)*RF_TAC_MAX_SQUAD+target];
        if(u->action.kind==RF_TAC_MOVE && tac_distance(u->pos,u->destination)<0.35f) tac_unit_hold(u);
        toy_game_actor_set_intent(a,u->action.kind==RF_TAC_MOVE,tac_x(u->destination.x),0,
            tac_z(u->destination.y),v?v->actor_id:-1,v?v->actor_generation:0,
            (!u->explicit_actions && u->action.kind==RF_TAC_HOLD) || u->action.kind==RF_TAC_FIRE,u->action.kind==RF_TAC_RELOAD);
        u->last_shot_target=target<0?-1:(1-u->team)*RF_TAC_MAX_SQUAD+target;
    }
}

void rf_tac_finish(struct rf_tac_world *world,int dt_ms)
{
    int i,team,alive[2]={0};
    if(!world || world->finished) return;
    rf_tac_sync(world);
    for(i=0;i<RF_TAC_MAX_UNITS;++i) if(world->units[i].alive) ++alive[world->units[i].team];
    world->time_ms+=dt_ms;world->tick=world->time_ms/RF_TAC_THINK_MS;
    { int capture_mask=0;
    for(team=0;team<2;++team) if(world->orders[team].kind==RF_TAC_ATTACK && !world->orders[team].captured && alive[team]) {
        struct rf_tac_order *order=&world->orders[team]; int clear=1,occupied=0;
        for(i=0;i<world->squad_size;++i) {
            struct rf_tac_unit *enemy=&world->units[(1-team)*RF_TAC_MAX_SQUAD+i];
            struct rf_tac_unit *friendly=&world->units[team*RF_TAC_MAX_SQUAD+i];
            if(enemy->alive && tac_distance(enemy->pos,order->target)<=order->radius) clear=0;
            if(friendly->alive && tac_distance(friendly->pos,order->target)<=order->radius) occupied=1;
        }
        if(clear && occupied) order->clear_ms+=dt_ms; else order->clear_ms=0;
        if(order->clear_ms>=TAC_CAPTURE_MS) {
            order->captured=1; ++world->captures;
            for(i=0;i<world->squad_size;++i) tac_unit_hold(&world->units[team*RF_TAC_MAX_SQUAD+i]);
            capture_mask|=1<<team;
        }
    }
    if(capture_mask && !world->continuous_commands) { world->finished=1; world->winner=capture_mask==3?-1:capture_mask==1?0:1; }
    }
    if(world->continuous_commands)return;
    if(!world->finished && !alive[0] && !alive[1]) { world->finished=1; world->winner=-1; }
    /* Role symmetry: clearing defenders is insufficient for either attacker.
     * A surviving defending side wins immediately if its attackers die. */
    for(team=0;team<2;++team)
        if(!world->finished && alive[team] && !alive[1-team] && world->orders[team].kind==RF_TAC_DEFEND)
            { world->finished=1; world->winner=team; }
    if(!world->finished && world->time_ms>=world->max_time_ms) {
        int a=world->orders[0].kind==RF_TAC_ATTACK,b=world->orders[1].kind==RF_TAC_ATTACK;
        world->finished=1; world->winner=a!=b?(a?1:0):-1;
    }
}

void rf_tac_step(struct rf_tac_world *world)
{
    if(!world || !world->game || world->finished) return;
    rf_tac_prepare(world,0);
    world->game->event_count=0;
    toy_game_update_world(world->game,RF_TAC_DT_MS);
    rf_tac_finish(world,RF_TAC_DT_MS);
}

static unsigned int tac_hash_word(unsigned int h,unsigned int value)
{ int i; for(i=0;i<4;++i) { h^=value&255u; h*=16777619u; value>>=8; } return h; }
static unsigned int tac_float_word(float f)
{ unsigned int v; memcpy(&v,&f,sizeof(v)); return v; }
unsigned int rf_tac_hash(const struct rf_tac_world *world)
{
    unsigned int h=2166136261u; int i,j;
    if(!world || !world->map) return 0;
    h=tac_hash_word(h,RF_TAC_VERSION); h=tac_hash_word(h,world->map->generation);
    h=tac_hash_word(h,world->map->content_hash); h=tac_hash_word(h,world->map->cover_count);
    for(i=0;i<world->map->cover_count;++i) {
        const struct rf_tac_cover *c=&world->map->covers[i];
        h=tac_hash_word(h,tac_float_word(c->x0)); h=tac_hash_word(h,tac_float_word(c->y0));
        h=tac_hash_word(h,tac_float_word(c->x1)); h=tac_hash_word(h,tac_float_word(c->y1)); h=tac_hash_word(h,c->height);
    }
    h=tac_hash_word(h,world->seed); h=tac_hash_word(h,world->rng);
    if(world->game)h=tac_hash_word(h,(unsigned)(world->game->rng>>32));
    if(world->game)for(i=0;i<TOY_GAME_MAX_BULLETS;++i){
        const struct toy_game_bullet *b=&world->game->bullets[i];
        if(!b->active)continue;
        const int values[]={i,b->source_id,(int)b->source_generation,(int)b->fire_sequence,
            b->faction,b->weapon,b->weakpoint_percent,b->sx,b->sy,b->sz,b->x,b->y,b->z,
            b->dx,b->dy,b->dz,b->distance,b->remainder,b->age_ms};
        for(unsigned k=0;k<sizeof(values)/sizeof(values[0]);++k)h=tac_hash_word(h,(unsigned)values[k]);
    }
    h=tac_hash_word(h,world->time_ms); h=tac_hash_word(h,world->tick);
    h=tac_hash_word(h,world->squad_size); h=tac_hash_word(h,world->winner);
    h=tac_hash_word(h,world->finished); h=tac_hash_word(h,world->max_time_ms);
    h=tac_hash_word(h,world->captures); h=tac_hash_word(h,world->invalid_actions);
    h=tac_hash_word(h,world->continuous_commands);
    for(i=0;i<2;++i) {
        h=tac_hash_word(h,world->order_revision[i]);
        const struct rf_tac_order *o=&world->orders[i]; h=tac_hash_word(h,o->kind);
        h=tac_hash_word(h,tac_float_word(o->target.x)); h=tac_hash_word(h,tac_float_word(o->target.y));
        h=tac_hash_word(h,tac_float_word(o->radius)); h=tac_hash_word(h,o->captured); h=tac_hash_word(h,o->clear_ms);
    }
    for(i=0;i<RF_TAC_MAX_UNITS;++i) {
        const struct rf_tac_unit *u=&world->units[i];
        h=tac_hash_word(h,u->explicit_actions);
        const struct toy_game_actor *a=world->game?toy_game_actor_by_id_const(world->game,u->actor_id):NULL;
        if(a && a->combat_generation==u->actor_generation) {
            const int values[]={a->sy,a->cy,a->pitch_sy,a->pitch_cy,a->ground_y,a->airborne_y,
                a->airborne_ms,a->vertical_velocity,a->damage_remainder_milli,
                a->combat_aim_ms,a->ai_turn_remainder,a->weapon_switch_timer_ms,
                a->reloading,a->evasion.window_ms,a->evasion.retrigger_ms,
                a->evasion.pressure_ms,a->nav_active,a->nav_x,a->nav_z,
                a->nav_direct_valid,a->nav_direct_ms,a->nav_layer_count,a->nav_layer_cursor,
                a->command_destination_active,a->command_x,a->command_z,
                a->intent_move,a->intent_x,a->intent_z,a->intent_fire,a->intent_reload};
            for(unsigned n=0;n<sizeof(values)/sizeof(values[0]);++n)h=tac_hash_word(h,(unsigned)values[n]);
            for(int n=0;n<TOY_GAME_WEAPON_SLOTS;++n) {
                h=tac_hash_word(h,a->slots[n].weapon);h=tac_hash_word(h,a->slots[n].mag);
                h=tac_hash_word(h,a->slots[n].reserve);
            }
        }
        h=tac_hash_word(h,u->id); h=tac_hash_word(h,u->team); h=tac_hash_word(h,u->alive); h=tac_hash_word(h,u->weapon);
        h=tac_hash_word(h,tac_float_word(u->pos.x)); h=tac_hash_word(h,tac_float_word(u->pos.y));
        h=tac_hash_word(h,tac_float_word(u->hp)); h=tac_hash_word(h,tac_float_word(u->evasion));
        h=tac_hash_word(h,u->recovery_ms); h=tac_hash_word(h,u->ammo); h=tac_hash_word(h,u->cooldown_ms); h=tac_hash_word(h,u->reload_ms);
        h=tac_hash_word(h,u->recoil_milli_mrad); h=tac_hash_word(h,u->recoil_recovery_remainder);
        h=tac_hash_word(h,u->action.kind); h=tac_hash_word(h,u->action.candidate); h=tac_hash_word(h,u->action.target);
        h=tac_hash_word(h,tac_float_word(u->destination.x)); h=tac_hash_word(h,tac_float_word(u->destination.y));
        h=tac_hash_word(h,u->shots); h=tac_hash_word(h,u->hits); h=tac_hash_word(h,u->last_shot_target);
        h=tac_hash_word(h,tac_float_word(u->damage)); h=tac_hash_word(h,tac_float_word(u->absorbed));
        h=tac_hash_word(h,u->nav_count); h=tac_hash_word(h,u->nav_cursor);
        h=tac_hash_word(h,u->nav_kind);
        for(j=0;j<u->nav_count;++j) h=tac_hash_word(h,u->nav_nodes[j]);
    }
    return h;
}

struct tac_prediction_candidate {
    struct rf_tac_vec pos;
    int kind,count;
    short route[RF_TAC_MAX_PATH];
};

struct rf_tac_prediction {
    struct rf_tac_world root;
    int team,remaining_steps,valid;
    int candidate_count[2][RF_TAC_MAX_SQUAD];
    unsigned int fire_mask[2][RF_TAC_MAX_SQUAD];
    struct tac_prediction_candidate candidates[2][RF_TAC_MAX_SQUAD][RF_TAC_CANDIDATES];
};

/* Prepare only execution facts. Full eight-position path-risk observations
 * are unnecessary here, and navigation is not rerun for every beam leaf. */
static int tac_prediction_prepare(struct rf_tac_prediction *p)
{
    int team,i,e,k;
    for(team=0;team<2;++team) for(i=0;i<p->root.squad_size;++i) {
        const struct rf_tac_unit *u=&p->root.units[team*RF_TAC_MAX_SQUAD+i];
        struct tac_paths paths; int nodes[RF_TAC_CANDIDATES],kinds[RF_TAC_CANDIDATES],count;
        if(!u->alive) continue;
        if((u->weapon<0 || u->weapon>=RF_TW_KIND_COUNT) || !tac_valid_pos(u->pos) || !isfinite(u->hp) ||
           !isfinite(u->evasion) || u->hp<=0.0f || u->evasion<0.0f) return 0;
        count=tac_candidates(&p->root,team,u,&paths,nodes,kinds);
        if(!count || kinds[0]!=RF_TAC_CURRENT) return 0;
        p->candidate_count[team][i]=count;
        for(k=0;k<count;++k) {
            struct tac_prediction_candidate *c=&p->candidates[team][i][k];
            c->pos=k==0?u->pos:p->root.map->nodes[nodes[k]].pos; c->kind=kinds[k];
            if(k) {
                if(u->action.kind==RF_TAC_MOVE && u->nav_count>0 && tac_distance(u->destination,c->pos)<0.02f) {
                    c->count=u->nav_count; memcpy(c->route,u->nav_nodes,c->count*sizeof(*c->route));
                } else c->count=tac_route_nodes(p->root.map,u->pos,&paths,nodes[k],c->route);
            }
        }
        for(e=0;e<p->root.squad_size;++e) {
            const struct rf_tac_unit *enemy=&p->root.units[(1-team)*RF_TAC_MAX_SQUAD+e];
            if(enemy->alive && tac_precise_exposure(p->root.map,u->pos,enemy->pos))
                p->fire_mask[team][i]|=1u<<e;
        }
    }
    return 1;
}

int rf_tac_prediction_reset(struct rf_tac_prediction *prediction,
                            const struct rf_tac_world *source,int team,int step_budget)
{
    if(!prediction) return 0;
    prediction->valid=0;
    if(!source || !source->map || source->finished || team<0 || team>1 || step_budget<0 ||
       step_budget>1000000 || source->squad_size<1 || source->squad_size>RF_TAC_MAX_SQUAD ||
       source->map->node_count<1 || source->map->node_count>RF_TAC_MAX_NODES ||
       source->map->cover_count<0 || source->map->cover_count>RF_TAC_MAX_COVERS) return 0;
    rf_tac_world_destroy(&prediction->root);
    memset(prediction,0,sizeof(*prediction));
    if(!rf_tac_world_clone(&prediction->root,source)) return 0;
    prediction->root.game->rng=0x726f6c6c6f7574ULL ^ source->seed ^ (unsigned)source->time_ms;
    prediction->team=team; prediction->remaining_steps=step_budget;
    if(!tac_prediction_prepare(prediction)) return 0;
    prediction->valid=1; return 1;
}

struct rf_tac_prediction *rf_tac_prediction_create(const struct rf_tac_world *source,
                                                   int team,int step_budget)
{
    struct rf_tac_prediction *p=(struct rf_tac_prediction *)calloc(1,sizeof(*p));
    if(!p) return NULL;
    if(!rf_tac_prediction_reset(p,source,team,step_budget)) { rf_tac_world_destroy(&p->root); free(p); return NULL; }
    return p;
}

void rf_tac_prediction_destroy(struct rf_tac_prediction *prediction)
{ if(prediction) rf_tac_world_destroy(&prediction->root); free(prediction); }

static void tac_prediction_hold(const struct rf_tac_world *w,int team,struct rf_tac_plan *plan)
{
    int i; memset(plan,0,sizeof(*plan)); plan->version=RF_TAC_VERSION;
    plan->generation=tac_generation(w,team); plan->tick=w->tick; plan->time_ms=w->time_ms;
    plan->team=team; plan->count=w->squad_size;
    for(i=0;i<w->squad_size;++i) { plan->actions[i].kind=RF_TAC_HOLD; plan->actions[i].target=-1;
        plan->destinations[i]=w->units[team*RF_TAC_MAX_SQUAD+i].pos; }
}

/* The same root candidates and routes used by live apply are prepared once.
 * Keeping an existing destination keeps the original route/cursor, exactly
 * as live execution does; alternative destinations use its swept route. */
static int tac_prediction_apply(const struct rf_tac_prediction *p,struct rf_tac_world *w,
                                const struct rf_tac_plan *plan,int team)
{
    int i;
    if(!plan || plan->team!=team || plan->version!=RF_TAC_VERSION ||
       plan->generation!=tac_generation(&p->root,team) || plan->tick!=p->root.tick ||
       plan->time_ms!=p->root.time_ms || plan->count!=p->root.squad_size) return 0;
    for(i=0;i<w->squad_size;++i) {
        struct rf_tac_unit *u=&w->units[team*RF_TAC_MAX_SQUAD+i];
        struct rf_tac_action action=plan->actions[i];
        if(!u->alive) { tac_unit_hold(u); continue; }
        if(action.kind<RF_TAC_HOLD || action.kind>RF_TAC_RELOAD) return 0;
        if(action.kind==RF_TAC_FIRE && (action.target<0 || action.target>=w->squad_size ||
           !(p->fire_mask[team][i]&(1u<<action.target)))) return 0;
        if(action.kind==RF_TAC_RELOAD && !u->reload_ms && u->ammo>=toy_game_weapon_info(u->weapon==RF_TW_RIFLE?TOY_GAME_WEAPON_AK:TOY_GAME_WEAPON_SMG)->mag_size) return 0;
        if(action.kind==RF_TAC_MOVE) {
            int k=action.candidate; const struct tac_prediction_candidate *c;
            if(k<=0 || k>=p->candidate_count[team][i] || !tac_valid_pos(plan->destinations[i])) return 0;
            c=&p->candidates[team][i][k];
            if(tac_distance(plan->destinations[i],c->pos)>=0.02f || !c->count) return 0;
            if(u->action.kind!=RF_TAC_MOVE || tac_distance(u->destination,c->pos)>0.02f || !u->nav_count) {
                memcpy(u->nav_nodes,c->route,c->count*sizeof(*c->route));
                u->nav_count=c->count; u->nav_cursor=0;
            }
            u->destination=c->pos;
            if(c->kind!=RF_TAC_CONTINUE || u->action.kind!=RF_TAC_MOVE) u->nav_kind=c->kind;
        } else { u->nav_count=u->nav_cursor=0; u->nav_kind=RF_TAC_CURRENT; u->destination=u->pos; }
        u->action=action;
    }
    return 1;
}

static float tac_remaining_move_distance(const struct rf_tac_world *w,const struct rf_tac_unit *u)
{
    struct rf_tac_vec pos=u->pos; float distance=0.0f; int k;
    if(u->action.kind!=RF_TAC_MOVE) return 0.0f;
    for(k=u->nav_cursor;k<u->nav_count;++k) {
        struct rf_tac_vec next=w->map->nodes[u->nav_nodes[k]].pos;
        distance+=tac_distance(pos,next); pos=next;
    }
    return distance;
}

static void tac_forecast_destination(const struct rf_tac_world *w,const struct rf_tac_unit *u,
                                     struct rf_tac_forecast_unit *f)
{
    struct rf_tac_unit hypothetical=*u;
    hypothetical.pos=f->destination;
    f->destination_fire_ready_time_s=fmaxf(u->reload_ms,u->cooldown_ms)/1000.0f;
    for(int e=0;e<w->squad_size;++e) {
        const struct rf_tac_unit *enemy=&w->units[(1-u->team)*RF_TAC_MAX_SQUAD+e];
        struct rf_tac_relation out,in;
        tac_relation(w,&hypothetical,hypothetical.pos,enemy,-1,-1,&out);
        tac_relation(w,enemy,enemy->pos,&hypothetical,-1,-1,&in);
        f->destination_relations[e]=out;f->destination_reverse_relations[e]=in;
        f->destination_incoming_ready_time_s[e]=fmaxf(enemy->reload_ms,enemy->cooldown_ms)/1000.0f;
        f->destination_outgoing_dps=fmaxf(f->destination_outgoing_dps,out.expected_dps);
        f->destination_incoming_dps+=in.expected_dps;
    }
}

static void tac_forecast_project(const struct rf_tac_prediction *p,const struct rf_tac_world *w,
                                 struct rf_tac_forecast *out)
{
    int relative,i,e;
    out->team=p->team; out->count=w->squad_size; out->elapsed_ms=w->time_ms-p->root.time_ms;
    out->finished=w->finished; out->winner=w->winner;
    for(relative=0;relative<2;++relative) {
        int team=relative==0?p->team:1-p->team; struct tac_paths goal_paths;
        out->orders[relative]=w->orders[team]; tac_paths_build(w->map,w->orders[team].target,&goal_paths);
        for(i=0;i<w->squad_size;++i) {
            const struct rf_tac_unit *u=&w->units[team*RF_TAC_MAX_SQUAD+i];
            const struct rf_tac_unit *before=&p->root.units[team*RF_TAC_MAX_SQUAD+i];
            struct rf_tac_forecast_unit *f=relative==0?&out->friendly[i]:&out->enemy[i];
            tac_view(before,&f->before); tac_view(u,&f->after);
            out->alive_before[relative]+=before->alive; out->alive_after[relative]+=u->alive;
            out->health_before[relative]+=f->before.effective_health;
            out->health_after[relative]+=f->after.effective_health;
            f->shots_fired=u->shots-before->shots; f->destination=u->action.kind==RF_TAC_MOVE?u->destination:u->pos;
            f->objective_distance=tac_distance(u->pos,w->orders[team].target);
            f->inside_objective=u->alive && f->objective_distance<=w->orders[team].radius;
            if(u->alive && !f->inside_objective) {
                int node=rf_tac_nearest_node(w->map,u->pos);
                f->objective_path_distance=node<0?1.0e20f:goal_paths.distance[node]+tac_distance(u->pos,w->map->nodes[node].pos);
            }
            if(!u->alive) continue;
            f->moving=u->action.kind==RF_TAC_MOVE;
            if(f->moving) {
                struct rf_tac_path_summary remaining;
                int last=u->nav_nodes[u->nav_count-1];
                f->remaining_move_time_s=tac_remaining_move_distance(w,u)/TAC_NAV_SPEED;
                tac_path_summary(w,team,u,NULL,last,&remaining);
                f->remaining_path_incoming_damage=remaining.incoming_damage;
            }
            tac_forecast_destination(w,u,f);
        }
    }
    for(i=0;i<w->squad_size;++i) for(e=0;e<w->squad_size;++e) {
        const struct rf_tac_unit *f=&w->units[p->team*RF_TAC_MAX_SQUAD+i];
        const struct rf_tac_unit *enemy=&w->units[(1-p->team)*RF_TAC_MAX_SQUAD+e];
        tac_relation(w,f,f->pos,enemy,-1,-1,&out->relations[i][e]);
        tac_relation(w,enemy,enemy->pos,f,-1,-1,&out->reverse_relations[e][i]);
        out->friendly[i].outgoing_dps=tac_max(out->friendly[i].outgoing_dps,out->relations[i][e].expected_dps);
        out->friendly[i].incoming_dps+=out->reverse_relations[e][i].expected_dps;
        out->enemy[e].outgoing_dps=tac_max(out->enemy[e].outgoing_dps,out->reverse_relations[e][i].expected_dps);
        out->enemy[e].incoming_dps+=out->relations[i][e].expected_dps;
    }
}

static int tac_prediction_evaluate(void *opaque,const struct rf_tac_plan *friendly,
                                    const struct rf_tac_plan *enemy,int duration_ms,
                                    struct rf_tac_forecast *out)
{
    struct rf_tac_prediction *p=(struct rf_tac_prediction *)opaque;
    struct rf_tac_world world; struct rf_tac_plan hold; int steps=0,required;
    if(!out) return RF_TAC_PRED_INVALID;
    memset(out,0,sizeof(*out));
    if(!p || !p->valid || !friendly || duration_ms<=0 || duration_ms>RF_TAC_PREDICTION_MAX_MS ||
       duration_ms%RF_TAC_THINK_MS) return RF_TAC_PRED_INVALID;
    required=(duration_ms+RF_TAC_DT_MS-1)/RF_TAC_DT_MS;
    if(required>p->remaining_steps) return RF_TAC_PRED_BUDGET;
    if(!rf_tac_world_clone(&world,&p->root)) return RF_TAC_PRED_INVALID;
    if(!enemy) { tac_prediction_hold(&p->root,1-p->team,&hold); enemy=&hold; }
    if(!tac_prediction_apply(p,&world,friendly,p->team) ||
       !tac_prediction_apply(p,&world,enemy,1-p->team)) { rf_tac_world_destroy(&world); return RF_TAC_PRED_INVALID; }
    while(steps<required && !world.finished) { rf_tac_step(&world); ++steps; }
    out->uncertain_shots=1; /* A sampled branch never proves a certain win. */
    p->remaining_steps-=steps;
    out->steps=steps; out->requested_ms=duration_ms;
    tac_forecast_project(p,&world,out);
    rf_tac_world_destroy(&world);
    return RF_TAC_PRED_OK;
}

static int tac_prediction_remaining(const void *opaque)
{
    const struct rf_tac_prediction *p=(const struct rf_tac_prediction *)opaque;
    return p && p->valid?p->remaining_steps:0;
}

void rf_tac_prediction_provider(struct rf_tac_prediction *prediction,struct rf_tac_predictor *out)
{
    if(!out) return;
    memset(out,0,sizeof(*out));
    if(!prediction || !prediction->valid) return;
    out->opaque=prediction; out->evaluate=tac_prediction_evaluate;
    out->remaining_steps=tac_prediction_remaining;
}

#include "rf_ai_tactical.inc"
