#include "rf_tactical.h"
#include "rf_tactical_weapon.h"
#include <math.h>
#include <string.h>

/* All geometry is in metres. The static map is immutable after baking. The
 * mutable world owns execution, weapon clocks, resource recovery and RNG. */
#define TAC_BODY_RADIUS 0.28f
#define TAC_NAV_SPEED RF_TW_BASE_MOVE_SPEED_MPS
#define TAC_CAPTURE_MS 2000

static unsigned int tac_hash_word(unsigned int h,unsigned int value);
static unsigned int tac_float_word(float f);

static float tac_min(float a, float b) { return a < b ? a : b; }
static float tac_max(float a, float b) { return a > b ? a : b; }
static float tac_abs(float a) { return a < 0.0f ? -a : a; }
static float tac_distance(struct rf_tac_vec a, struct rf_tac_vec b)
{ float x = a.x - b.x, y = a.y - b.y; return sqrtf(x*x + y*y); }
static int tac_valid_pos(struct rf_tac_vec p)
{ return p.x >= 0.35f && p.x <= 63.65f && p.y >= 0.35f && p.y <= 47.65f; }
static unsigned int tac_generation(const struct rf_tac_world *w,int team)
{ return w->map->generation ^ (w->seed * 0x9e3779b9u) ^
         (w->order_revision[team]*0x7f4a7c15u) ^ ((unsigned int)team*0x27d4eb2du); }

static int tac_clip(float p, float q, float *first, float *last)
{
    float t;
    if (tac_abs(p) < 0.00001f) return q >= 0.0f;
    t = q / p;
    if (p < 0.0f) { if (t > *last) return 0; if (t > *first) *first = t; }
    else { if (t < *first) return 0; if (t < *last) *last = t; }
    return 1;
}

static int tac_rect_segment(struct rf_tac_vec a, struct rf_tac_vec b,
                            const struct rf_tac_cover *c, float margin,
                            float *first, float *last)
{
    float dx = b.x-a.x, dy = b.y-a.y;
    *first = 0.0f; *last = 1.0f;
    return tac_clip(-dx, a.x-c->x0+margin, first, last) &&
           tac_clip(dx, c->x1+margin-a.x, first, last) &&
           tac_clip(-dy, a.y-c->y0+margin, first, last) &&
           tac_clip(dy, c->y1+margin-a.y, first, last);
}

static int tac_move_clear(const struct rf_tac_map *m, struct rf_tac_vec a,
                           struct rf_tac_vec b)
{
    int i; float t0, t1;
    if (!tac_valid_pos(a) || !tac_valid_pos(b)) return 0;
    for (i = 0; i < m->cover_count; ++i)
        if (tac_rect_segment(a,b,&m->covers[i],TAC_BODY_RADIUS,&t0,&t1)) return 0;
    return 1;
}

static int tac_ray_clear(const struct rf_tac_map *m, struct rf_tac_vec a,
                         float az, struct rf_tac_vec b, float bz)
{
    int i; float t0, t1, z0, z1, top;
    for (i = 0; i < m->cover_count; ++i) {
        if (!tac_rect_segment(a,b,&m->covers[i],0.0f,&t0,&t1)) continue;
        if (t1 <= 0.0001f || t0 >= 0.9999f) continue;
        z0 = az + (bz-az)*t0; z1 = az + (bz-az)*t1;
        top = m->covers[i].height == RF_TAC_LOW ? 1.10f : 2.20f;
        if (tac_min(z0,z1) <= top) return 0;
    }
    return 1;
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

/* A high-cover lean is limited to 0.8m from an edge. It changes ray origins,
 * not authoritative body position or movement; middle wall anchors cannot
 * magically shoot through the wall. Choose the shortest legal corner. */
static struct rf_tac_vec tac_peek(const struct rf_tac_map *m,
                                 struct rf_tac_vec p, struct rf_tac_vec toward)
{
    struct rf_tac_vec normal, q = p; int ci = tac_cover_at(m,p,&normal);
    const struct rf_tac_cover *c;
    if (ci < 0 || m->covers[ci].height != RF_TAC_HIGH) return p;
    c = &m->covers[ci];
    if (normal.x*(toward.x-p.x)+normal.y*(toward.y-p.y) >= 0.0f) return p;
    if (tac_abs(normal.x) > tac_abs(normal.y)) {
        float low = tac_abs(p.y-c->y0), high = tac_abs(p.y-c->y1);
        if (tac_min(low,high) <= 0.8f) q.y = low <= high ? c->y0-0.16f : c->y1+0.16f;
    } else {
        float low = tac_abs(p.x-c->x0), high = tac_abs(p.x-c->x1);
        if (tac_min(low,high) <= 0.8f) q.x = low <= high ? c->x0-0.16f : c->x1+0.16f;
    }
    return q;
}

static int tac_precise_exposure(const struct rf_tac_map *m,
                                struct rf_tac_vec from, struct rf_tac_vec to)
{
    struct rf_tac_vec normal, source, target[3]; int source_cover, target_cover, i, count = 0;
    float source_z = 1.55f, heights[3] = {1.55f,0.95f,0.50f};
    source_cover = tac_cover_at(m,from,&normal);
    if (source_cover >= 0 && m->covers[source_cover].height == RF_TAC_LOW) source_z = 1.30f;
    source = tac_peek(m,from,to);
    target_cover = tac_cover_at(m,to,&normal);
    target[0] = target[1] = target[2] = to;
    if (target_cover >= 0) {
        if (m->covers[target_cover].height == RF_TAC_LOW) {
            heights[0] = 1.30f; heights[1] = 0.75f; heights[2] = 0.35f;
        } else target[0] = tac_peek(m,to,from);
    }
    for (i = 0; i < 3; ++i)
        if (tac_ray_clear(m,source,source_z,target[i],heights[i])) ++count;
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

int rf_tac_map_generate(struct rf_tac_map *map, unsigned int seed)
{
    unsigned int rng = seed ? seed : 0xa341316cu;
    int x,y,i,j,grid_count;
    if (!map) return 0;
    memset(map,0,sizeof(*map)); map->seed=seed;
    map->generation=(seed*0x85ebca6bu)^0x52465431u;
    map->objective.x=53.0f; map->objective.y=24.0f; map->objective_radius=6.0f;
    map->spawn[0].x=5.0f; map->spawn[0].y=24.0f;
    map->spawn[1]=map->objective;
    /* Half-open strongpoint: rear/side protection, broken frontal cover and
     * a final 7m approach without stepping-stone cover. Flanks remain open. */
    tac_add_cover(map,58.0f,17.0f,58.8f,23.0f,RF_TAC_HIGH);
    tac_add_cover(map,58.0f,25.0f,58.8f,31.0f,RF_TAC_HIGH);
    tac_add_cover(map,51.0f,16.0f,57.0f,16.7f,RF_TAC_HIGH);
    tac_add_cover(map,51.0f,31.3f,57.0f,32.0f,RF_TAC_HIGH);
    tac_add_cover(map,49.0f,18.0f,49.7f,21.0f,RF_TAC_LOW);
    tac_add_cover(map,49.0f,27.0f,49.7f,30.0f,RF_TAC_LOW);
    /* Every band varies cover location, height and orientation. Three broad
     * advance lanes remain disconnected by gaps rather than enclosed walls. */
    for (i=0;i<4;++i) for (j=0;j<3;++j) {
        float cx=13.0f+i*8.0f+(float)(rf_tw_rng_next(&rng)%401)/100.0f;
        float cy=9.0f+j*14.0f+(float)(rf_tw_rng_next(&rng)%601)/100.0f-3.0f;
        int height=(rf_tw_rng_next(&rng)%3)==0 ? RF_TAC_HIGH : RF_TAC_LOW;
        if (rf_tw_rng_next(&rng)&1u) tac_add_cover(map,cx,cy,cx+0.7f,cy+2.4f,height);
        else tac_add_cover(map,cx,cy,cx+2.4f,cy+0.7f,height);
    }
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
    for (i=0;i<map->node_count;++i) for (j=0;j<map->node_count;++j) {
        int exposure=tac_precise_exposure(map,map->nodes[i].pos,map->nodes[j].pos);
        map->exposure[i][j]=(unsigned char)exposure;
        if (exposure) map->visible[i][j>>5]|=1u<<(j&31);
    }
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
    return rf_tac_nearest_node(map,map->spawn[0])>=0 && rf_tac_nearest_node(map,map->objective)>=0;
}

static rf_tw_state tac_weapon_state(const struct rf_tac_unit *u)
{
    rf_tw_state s; s.ammo=u->ammo; s.cooldown_ms=u->cooldown_ms;
    s.reload_remaining_ms=u->reload_ms; s.recoil_milli_mrad=u->recoil_milli_mrad;
    s.recoil_recovery_remainder=u->recoil_recovery_remainder; return s;
}
static void tac_weapon_store(struct rf_tac_unit *u, const rf_tw_state *s)
{
    u->ammo=s->ammo; u->cooldown_ms=s->cooldown_ms; u->reload_ms=s->reload_remaining_ms;
    u->recoil_milli_mrad=s->recoil_milli_mrad; u->recoil_recovery_remainder=s->recoil_recovery_remainder;
}

int rf_tac_world_init(struct rf_tac_world *world, const struct rf_tac_map *map,
                      unsigned int seed, int squad_size, int weapon, int max_time_ms)
{
    int team,i;
    if (!world || !map || map->node_count<=0 || squad_size<1 || squad_size>RF_TAC_MAX_SQUAD ||
        !rf_tw_profile_get(weapon) || max_time_ms<RF_TAC_DT_MS) return 0;
    memset(world,0,sizeof(*world)); world->map=map; world->seed=seed;
    world->rng=seed?seed:0x1234567u; world->squad_size=squad_size;
    world->winner=-1; world->max_time_ms=max_time_ms;
    for (team=0;team<2;++team) {
        world->orders[team].kind=RF_TAC_DEFEND;
        world->orders[team].target=map->spawn[team]; world->orders[team].radius=map->objective_radius;
        for (i=0;i<squad_size;++i) {
            struct rf_tac_unit *u=&world->units[team*RF_TAC_MAX_SQUAD+i]; rf_tw_state ws;
            struct rf_tac_vec p=map->spawn[team]; int node;
            p.x += (i%2)*1.2f; p.y+=(i-(squad_size-1)*0.5f)*1.6f;
            node=rf_tac_nearest_node(map,p); if (node<0) return 0;
            u->id=team*RF_TAC_MAX_SQUAD+i; u->team=team; u->alive=1; u->weapon=weapon;
            u->pos=p; u->hp=RF_TW_BASE_HP; u->evasion=RF_TW_BASE_RISK;
            u->destination=p; u->action.kind=RF_TAC_HOLD; u->action.target=-1;
            u->last_shot_target=-1;
            rf_tw_state_reset(rf_tw_profile_get(weapon),&ws); tac_weapon_store(u,&ws);
        }
    }
    return 1;
}

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

static float tac_lateral_speed(struct rf_tac_vec shooter,struct rf_tac_vec target,
                               struct rf_tac_vec destination)
{
    float rx=target.x-shooter.x,ry=target.y-shooter.y;
    float vx=destination.x-target.x,vy=destination.y-target.y;
    float denominator=sqrtf((rx*rx+ry*ry)*(vx*vx+vy*vy));
    if(denominator<0.0001f) return 0.0f;
    return TAC_NAV_SPEED*tac_abs(rx*vy-ry*vx)/denominator;
}
static float tac_unit_lateral_speed(const struct rf_tac_world *w,struct rf_tac_vec shooter,
                                    const struct rf_tac_unit *target)
{
    if(target->action.kind!=RF_TAC_MOVE || target->nav_cursor>=target->nav_count) return 0.0f;
    return tac_lateral_speed(shooter,target->pos,w->map->nodes[target->nav_nodes[target->nav_cursor]].pos);
}

static void tac_relation(const struct rf_tac_world *w,const struct rf_tac_unit *from,
                          struct rf_tac_vec pos,const struct rf_tac_unit *to,int baked_from,
                          int baked_to,struct rf_tac_relation *out)
{
    rf_tw_context context; rf_tw_metrics metrics; int exposure;
    memset(out,0,sizeof(*out)); out->distance=tac_distance(pos,to->pos);
    if (!from->alive || !to->alive) return;
    exposure=baked_from>=0 && baked_to>=0 &&
             tac_distance(w->map->nodes[baked_from].pos,pos)<=0.10f &&
             tac_distance(w->map->nodes[baked_to].pos,to->pos)<=0.10f ? w->map->exposure[baked_from][baked_to] :
             tac_precise_exposure(w->map,pos,to->pos);
    out->visible=exposure!=0; out->exposure=exposure/3.0f;
    if (!exposure || from->reload_ms || !from->ammo) return;
    rf_tw_context_reset(&context); context.distance_m=out->distance; context.exposure=exposure;
    context.recoil_milli_mrad=from->recoil_milli_mrad;
    context.target_lateral_speed_mps=tac_unit_lateral_speed(w,pos,to);
    rf_tw_query(rf_tw_profile_get(from->weapon),&context,&metrics);
    out->hit_rate=metrics.hit_probability; out->expected_dps=metrics.reload_cycle_dps;
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
                rf_tw_context context; rf_tw_metrics metrics; rf_tw_state state;
                const rf_tw_profile *profile=rf_tw_profile_get(enemy->weapon); int exposure;
                if (!enemy->alive) continue;
                exposure=tac_precise_exposure(w->map,enemy->pos,point); if (!exposure) continue;
                threatened=1;
                state=tac_weapon_state(enemy);
                if(!state.ammo && !state.reload_remaining_ms) rf_tw_state_begin_reload(profile,&state);
                rf_tw_state_advance(profile,&state,arrival_ms);
                if(state.reload_remaining_ms || state.cooldown_ms || !state.ammo) continue;
                rf_tw_context_reset(&context); context.distance_m=tac_distance(enemy->pos,point);
                context.exposure=exposure; context.target_lateral_speed_mps=tac_lateral_speed(enemy->pos,point,end);
                context.recoil_milli_mrad=state.recoil_milli_mrad;
                rf_tw_query(profile,&context,&metrics);
                sample_dps+=metrics.reload_cycle_dps;
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
    if (plan->version!=RF_TAC_VERSION || plan->generation!=tac_generation(world,plan->team) ||
        plan->tick!=world->tick || plan->time_ms!=world->time_ms || plan->count!=world->squad_size) {
        for(i=0;i<world->squad_size;++i) tac_unit_hold(&world->units[plan->team*RF_TAC_MAX_SQUAD+i]);
        ++world->invalid_actions; return 0;
    }
    for(i=0;i<world->squad_size;++i) {
        struct rf_tac_unit *u=&world->units[plan->team*RF_TAC_MAX_SQUAD+i];
        struct rf_tac_action action=plan->actions[i]; int legal=1;
        if (action.kind<RF_TAC_HOLD || action.kind>RF_TAC_RELOAD) legal=0;
        if (!u->alive) { tac_unit_hold(u); continue; }
        if (legal && action.kind==RF_TAC_FIRE)
            legal=action.target>=0 && action.target<world->squad_size &&
                  world->units[(1-plan->team)*RF_TAC_MAX_SQUAD+action.target].alive &&
                  tac_precise_exposure(world->map,u->pos,world->units[(1-plan->team)*RF_TAC_MAX_SQUAD+action.target].pos);
        if (legal && action.kind==RF_TAC_RELOAD) legal=u->reload_ms || u->ammo<rf_tw_profile_get(u->weapon)->magazine;
        if (legal && action.kind==RF_TAC_MOVE) {
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
    return valid;
}

static int tac_target(const struct rf_tac_world *w,const struct rf_tac_unit *u)
{
    int e,best=-1; float best_distance=1.0e20f;
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

void rf_tac_step(struct rf_tac_world *world)
{
    float damage[RF_TAC_MAX_UNITS]={0},risk[RF_TAC_MAX_UNITS]={0}; int i,team,alive[2]={0};
    if(!world || !world->map || world->finished) return;
    /* Clocks and all movement resolve before either side fires. Damage is
     * buffered so a soldier alive at the fire phase gets its simultaneous shot. */
    for(i=0;i<RF_TAC_MAX_UNITS;++i) {
        struct rf_tac_unit *u=&world->units[i]; rf_tw_state state;
        if(!u->alive) continue;
        state=tac_weapon_state(u); rf_tw_state_advance(rf_tw_profile_get(u->weapon),&state,RF_TAC_DT_MS); tac_weapon_store(u,&state);
        if(u->action.kind==RF_TAC_RELOAD && !u->reload_ms && u->ammo==rf_tw_profile_get(u->weapon)->magazine) tac_unit_hold(u);
        rf_tw_recover(&u->evasion,&u->recovery_ms,RF_TAC_DT_MS);
        if(u->action.kind==RF_TAC_MOVE) {
            float budget=TAC_NAV_SPEED*RF_TAC_DT_MS/1000.0f;
            while(budget>0.0001f && u->nav_cursor<u->nav_count) {
                struct rf_tac_vec p=world->map->nodes[u->nav_nodes[u->nav_cursor]].pos,next;
                float d=tac_distance(u->pos,p),move=tac_min(d,budget);
                if(d<0.001f) { ++u->nav_cursor; continue; }
                next.x=u->pos.x+(p.x-u->pos.x)*move/d; next.y=u->pos.y+(p.y-u->pos.y)*move/d;
                if(!tac_move_clear(world->map,u->pos,next)) { tac_unit_hold(u); break; }
                u->pos=next; budget-=move;
                if(move>=d-0.0001f) ++u->nav_cursor;
            }
            if(u->nav_cursor>=u->nav_count) tac_unit_hold(u);
        }
    }
    for(i=0;i<RF_TAC_MAX_UNITS;++i) {
        struct rf_tac_unit *u=&world->units[i]; const rf_tw_profile *profile; rf_tw_state state; int target;
        if(!u->alive || u->action.kind==RF_TAC_MOVE) continue;
        profile=rf_tw_profile_get(u->weapon); state=tac_weapon_state(u);
        if(u->action.kind==RF_TAC_RELOAD || !u->ammo) {
            rf_tw_state_begin_reload(profile,&state); tac_weapon_store(u,&state); continue;
        }
        if(u->cooldown_ms || u->reload_ms) continue;
        target=tac_target(world,u);
        if(target>=0) {
            int id=(1-u->team)*RF_TAC_MAX_SQUAD+target; struct rf_tac_unit *enemy=&world->units[id];
            rf_tw_context context; rf_tw_shot shot; int exposure=tac_precise_exposure(world->map,u->pos,enemy->pos);
            rf_tw_context_reset(&context); context.distance_m=tac_distance(u->pos,enemy->pos); context.exposure=exposure;
            context.target_lateral_speed_mps=tac_unit_lateral_speed(world,u->pos,enemy);
            context.recoil_milli_mrad=u->recoil_milli_mrad;
            rf_tw_sample_shot(profile,&context,&world->rng,&shot);
            if(rf_tw_state_begin_shot(profile,&state)) {
                tac_weapon_store(u,&state); ++u->shots; u->last_shot_target=target;
                if(shot.hit) { ++u->hits; damage[id]+=shot.damage_milli/1000.0f; risk[id]+=shot.risk_cost_milli/1000.0f;
                    u->damage+=shot.damage_milli/1000.0f; }
            }
        }
    }
    for(i=0;i<RF_TAC_MAX_UNITS;++i) {
        struct rf_tac_unit *u=&world->units[i];
        if(!u->alive) continue;
        if(damage[i]>0.0f) {
            float absorbed=rf_tw_apply_damage(&u->hp,&u->evasion,damage[i],risk[i]);
            u->absorbed+=absorbed;
            u->recovery_ms=RF_TW_BASE_RECOVERY_DELAY_MS;
            if(u->hp<=0.0f) { u->hp=0.0f; u->alive=0; tac_unit_hold(u); }
        }
        if(u->alive) ++alive[u->team];
    }
    world->time_ms+=RF_TAC_DT_MS; world->tick=world->time_ms/RF_TAC_THINK_MS;
    { int capture_mask=0;
    for(team=0;team<2;++team) if(world->orders[team].kind==RF_TAC_ATTACK && alive[team]) {
        struct rf_tac_order *order=&world->orders[team]; int clear=1,occupied=1;
        for(i=0;i<world->squad_size;++i) {
            struct rf_tac_unit *enemy=&world->units[(1-team)*RF_TAC_MAX_SQUAD+i];
            struct rf_tac_unit *friendly=&world->units[team*RF_TAC_MAX_SQUAD+i];
            if(enemy->alive && tac_distance(enemy->pos,order->target)<=order->radius) clear=0;
            if(friendly->alive && tac_distance(friendly->pos,order->target)>order->radius) occupied=0;
        }
        if(clear && occupied) order->clear_ms+=RF_TAC_DT_MS; else order->clear_ms=0;
        if(order->clear_ms>=TAC_CAPTURE_MS) {
            order->captured=1; order->kind=RF_TAC_DEFEND; ++world->captures;
            for(i=0;i<world->squad_size;++i) tac_unit_hold(&world->units[team*RF_TAC_MAX_SQUAD+i]);
            capture_mask|=1<<team;
        }
    }
    if(capture_mask) { world->finished=1; world->winner=capture_mask==3?-1:capture_mask==1?0:1; }
    }
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
    h=tac_hash_word(h,world->time_ms); h=tac_hash_word(h,world->tick);
    h=tac_hash_word(h,world->squad_size); h=tac_hash_word(h,world->winner);
    h=tac_hash_word(h,world->finished); h=tac_hash_word(h,world->max_time_ms);
    h=tac_hash_word(h,world->captures); h=tac_hash_word(h,world->invalid_actions);
    for(i=0;i<2;++i) {
        h=tac_hash_word(h,world->order_revision[i]);
        const struct rf_tac_order *o=&world->orders[i]; h=tac_hash_word(h,o->kind);
        h=tac_hash_word(h,tac_float_word(o->target.x)); h=tac_hash_word(h,tac_float_word(o->target.y));
        h=tac_hash_word(h,tac_float_word(o->radius)); h=tac_hash_word(h,o->captured); h=tac_hash_word(h,o->clear_ms);
    }
    for(i=0;i<RF_TAC_MAX_UNITS;++i) {
        const struct rf_tac_unit *u=&world->units[i];
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
