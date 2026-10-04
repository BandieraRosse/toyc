/* Shared geometry queries and attack-target aggregation. Direction X/Z is
 * normalized to 1024; Y is a slope relative to horizontal distance. */
static int combat_ray_slab(int origin, int direction, int lo, int hi,
                            long long *near_t, long long *far_t)
{
    long long a, b, swap;
    if (!direction) return origin >= lo && origin <= hi;
    a = (long long)(lo - origin) * 1024 * (1 << 20) / direction;
    b = (long long)(hi - origin) * 1024 * (1 << 20) / direction;
    if (a > b) { swap = a; a = b; b = swap; }
    if (a > *near_t) *near_t = a;
    if (b < *far_t) *far_t = b;
    return *near_t <= *far_t;
}

static int combat_world_distance(const struct toy_game *g, int x, int y, int z,
                                 int sy, int vy, int cy, int range)
{
    long long best = (long long)range * (1 << 20);
    int i;
    for (i = 0; i < g->primitive_count; i++) {
        const struct toy_map_primitive *p = &g->primitives[i];
        long long lo = 0, hi = best;
        int top = p->surface_y0 > p->surface_y1 ? p->surface_y0 : p->surface_y1;
        if (!(p->flags & TOY_MAP_PRIMITIVE_COLLISION)) continue;
        if (top < p->base_y) top = p->base_y;
        if (!combat_ray_slab(x, sy, p->minx, p->maxx, &lo, &hi) ||
            !combat_ray_slab(z, cy, p->minz, p->maxz, &lo, &hi) ||
            !combat_ray_slab(y, vy, p->base_y, top, &lo, &hi)) continue;
        if (p->shape == TOY_MAP_PRIMITIVE_RAMP_X ||
            p->shape == TOY_MAP_PRIMITIVE_RAMP_Z) {
            int along = p->shape == TOY_MAP_PRIMITIVE_RAMP_X ? x : z;
            int direction = p->shape == TOY_MAP_PRIMITIVE_RAMP_X ? sy : cy;
            int start = p->shape == TOY_MAP_PRIMITIVE_RAMP_X ? p->minx : p->minz;
            int span = p->shape == TOY_MAP_PRIMITIVE_RAMP_X ?
                p->maxx - p->minx : p->maxz - p->minz;
            int rise = p->surface_y1 - p->surface_y0;
            int plane_y, plane_v;
            if (span <= 0) continue;
            plane_y = p->surface_y0 + (int)((long long)(along - start) * rise / span);
            plane_v = (int)((long long)direction * rise / span);
            if (!combat_ray_slab(y - plane_y, vy - plane_v, -1000000, 0, &lo, &hi))
                continue;
        }
        if (hi < 0 || lo > best) continue;
        if (lo < 0) lo = 0;
        best = lo;
    }
    return (int)(best / (1 << 20));
}

static int combat_visible(const struct toy_game *g, const struct toy_game_actor *a,
                           int x, int y, int z)
{
    int dx = x - a->x, dz = z - a->z;
    int distance = isqrt((long long)dx * dx + (long long)dz * dz);
    int origin_y = a->ground_y + a->airborne_y + RASTERFALL_HUMAN_EYE_HEIGHT_RFU;
    if (!distance) return 1;
    return combat_world_distance(g, a->x, origin_y, a->z,
        (int)((long long)dx * 1024 / distance),
        (int)((long long)(y - origin_y) * 1024 / distance),
        (int)((long long)dz * 1024 / distance), distance + 1) >= distance;
}

int toy_game_find_combat_target(struct toy_game *g,
    const struct toy_game_actor *actor, struct toy_game_combat_target *out)
{
    const struct toy_game_weapon_info *w;
    long long best;
    int i;
    if (!g || !actor || !out) return 0;
    memset(out, 0, sizeof(*out)); out->kind = -1; out->index = -1;
    w = toy_game_weapon_info(toy_game_actor_current_weapon(actor));
    best = (long long)w->alert_range * w->alert_range + 1;
    for (i = 0; i < TOY_GAME_MAX_ACTORS + TOY_GAME_MAX_ENEMIES; i++) {
        int x, y, z, kind, index;
        unsigned int generation = 0;
        long long dx, dz, distance2;
        if (i < TOY_GAME_MAX_ACTORS) {
            const struct toy_game_actor *a = &g->actors[i];
            if (!a->active || a->state != TOY_GAME_ACTOR_ALIVE || a->hp <= 0 ||
                a->developer_only || !toy_game_actors_hostile(actor, a)) continue;
            x = a->x; z = a->z;
            y = a->ground_y + a->airborne_y + RASTERFALL_HUMAN_HEIGHT_RFU * 64 / 100;
            kind = TOY_GAME_ENTITY_ACTOR; index = i; generation = a->combat_generation;
        } else {
            const struct toy_game_enemy *e = &g->enemies[i - TOY_GAME_MAX_ACTORS];
            if (e->active != 1 || e->hp <= 0) continue;
            x = e->x; z = e->z;
            y = e->ground_y + e->airborne_y + RASTERFALL_HUMAN_HEIGHT_RFU * 64 / 100;
            kind = TOY_GAME_ENTITY_ENEMY; index = i - TOY_GAME_MAX_ACTORS;
            generation = e->combat_generation;
        }
        dx = (long long)x - actor->x; dz = (long long)z - actor->z;
        distance2 = dx * dx + dz * dz;
        if (actor->faction == TOY_GAME_FACTION_HOSTILE && actor->ai_guard_radius > 0) {
            long long gx = (long long)x - actor->deployment_x;
            long long gz = (long long)z - actor->deployment_z;
            if (gx * gx + gz * gz >
                (long long)actor->ai_guard_radius * actor->ai_guard_radius) continue;
        }
        if (distance2 >= best || !enemy_positions_share_target_region(g,
            actor->x, actor->z, x, z) || !combat_visible(g, actor, x, y, z)) continue;
        best = distance2;
        out->kind = kind; out->index = index; out->generation = generation;
        out->x = x; out->y = y; out->z = z;
    }
    return out->kind >= 0;
}

static void fire_ray(struct toy_game *g, struct toy_game_actor *actor,
                      struct toy_game_ray *ray, int damage, int range)
{
    int i, best, slope, ray_y, max_range;
    ray->enemy_index = -1; ray->actor_index = -1;
    ray->actor_generation = 0;
    ray->damage = ray->health_damage = ray->evasion_spent_milli = 0;
    ray->hit_enemy = ray->weakpoint = 0;
    ray_y = actor->ground_y + actor->airborne_y + RASTERFALL_HUMAN_EYE_HEIGHT_RFU;
    slope = ray->vy;
    if (actor->pitch_cy > 0)
        slope += (int)((long long)actor->pitch_sy * 1024 / actor->pitch_cy);
    max_range = (int)((long long)range * 1024 /
        isqrt(1024LL * 1024 + (long long)slope * slope));
    best = combat_world_distance(g, actor->x, ray_y, actor->z,
                                 ray->sy, slope, ray->cy, max_range);
    ray->hit_world = best < max_range;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES + TOY_GAME_MAX_ACTORS; i++) {
        int x, z, ground, height, lx, lz, t, hit_y, radius = TOY_GAME_HIT_RADIUS;
        long long proj, cross;
        if (i < TOY_GAME_MAX_ENEMIES) {
            const struct toy_game_enemy *e = &g->enemies[i];
            if (e->active != 1 || e->hp <= 0) continue;
            x = e->x; z = e->z; ground = e->ground_y + e->airborne_y;
            height = e->type == TOY_GAME_ENEMY_TANK ? 1600 :
                e->type == TOY_GAME_ENEMY_SMOKER ? 1250 : 1000;
        } else {
            const struct toy_game_actor *a = &g->actors[i - TOY_GAME_MAX_ENEMIES];
            if (!a->active || a->state != TOY_GAME_ACTOR_ALIVE || a->hp <= 0 ||
                a->developer_only || !toy_game_actors_hostile(actor, a)) continue;
            x = a->x; z = a->z; ground = a->ground_y + a->airborne_y;
            height = RASTERFALL_HUMAN_HEIGHT_RFU;
        }
        lx = x - actor->x; lz = z - actor->z;
        proj = (long long)lx * ray->sy + (long long)lz * ray->cy;
        cross = (long long)lx * ray->cy - (long long)lz * ray->sy;
        if (proj <= 0 || cross > radius * 1024 || cross < -radius * 1024) continue;
        t = (int)(proj / 1024);
        if (t >= best) continue;
        hit_y = ray_y + (int)((long long)t * slope / 1024);
        if (hit_y < ground || hit_y > ground + height) continue;
        best = t; ray->hit_world = 0;
        ray->enemy_index = i < TOY_GAME_MAX_ENEMIES ? i : -1;
        ray->actor_index = i >= TOY_GAME_MAX_ENEMIES ? i - TOY_GAME_MAX_ENEMIES : -1;
        ray->actor_generation = ray->actor_index >= 0 ?
            g->actors[ray->actor_index].combat_generation : 0;
        /* Infected retain their existing whole-body damage contract. Human
         * head hits are geometry-derived; skill only changes their payoff. */
        ray->weakpoint = ray->actor_index >= 0 && hit_y >= ground + height * 84 / 100;
        ray->damage = damage;
        ray->hit_enemy = 1;
    }
    ray->ex = actor->x + (int)((long long)ray->sy * best / 1024);
    ray->ez = actor->z + (int)((long long)ray->cy * best / 1024);
    ray->hit_y = ray_y + (int)((long long)slope * best / 1024);
}

int toy_game_actor_resolve_shot(struct toy_game *g, struct toy_game_actor *actor)
{
    int i, j, hit = 0;
    struct toy_game_capabilities caps;
    if (!g || !actor) return 0;
    toy_game_actor_capabilities(actor, toy_game_actor_current_weapon(actor), &caps);
    for (i = 0; i < actor->ray_count && i < TOY_GAME_MAX_RAYS; i++) {
        struct toy_game_ray *r = &actor->rays[i];
        r->health_damage = r->evasion_spent_milli = 0;
        if (r->damage <= 0) continue;
        if (r->enemy_index >= 0 && r->enemy_index < TOY_GAME_MAX_ENEMIES) {
            struct toy_game_enemy *e = &g->enemies[r->enemy_index];
            int before = e->hp;
            actor->combat_stats.hits++;
            r->hit_enemy = 1;
            if (toy_game_apply_reported_hit(g, actor, r->enemy_index, r->damage)) {
                r->health_damage = before - e->hp;
                actor->combat_stats.health_damage += r->health_damage;
                hit = 1;
            }
            actor->combat_stats.overkill += r->damage - r->health_damage;
        }
    }
    /* At most MAX_RAYS targets: no per-shot scan of the actor roster. */
    for (i = 0; i < actor->ray_count && i < TOY_GAME_MAX_RAYS; i++) {
        struct toy_game_ray *r = &actor->rays[i];
        struct toy_game_actor *target;
        struct toy_game_attack attack = {0};
        struct toy_game_damage_result result;
        int assigned_health = 0, assigned_evasion = 0, weight_sum = 0;
        if (r->damage <= 0 || r->actor_index < 0 || r->actor_index >= TOY_GAME_MAX_ACTORS)
            continue;
        for (j = 0; j < i; j++)
            if (actor->rays[j].actor_index == r->actor_index) break;
        if (j < i) continue;
        target = &g->actors[r->actor_index];
        if (!toy_game_actors_hostile(actor, target) || !target->active ||
            target->combat_generation != r->actor_generation) continue;
        attack.kind = TOY_GAME_ATTACK_BULLET;
        attack.source_x = actor->x; attack.source_z = actor->z;
        for (j = i; j < actor->ray_count; j++) {
            struct toy_game_ray *pellet = &actor->rays[j];
            if (pellet->actor_index != r->actor_index || pellet->damage <= 0) continue;
            attack.base_damage_milli += pellet->damage * 1000;
            attack.health_damage_milli += pellet->damage *
                (pellet->weakpoint ? caps.weakpoint_percent : 100) * 10;
            actor->combat_stats.hits++;
            pellet->hit_enemy = 1;
        }
        hit = 1;
        if (g->defer_actor_damage) continue;
        result = toy_game_damage_actor(g, actor, target, &attack);
        for (j = i; j < actor->ray_count; j++) {
            struct toy_game_ray *pellet = &actor->rays[j];
            int health, evasion;
            if (pellet->actor_index != r->actor_index || pellet->damage <= 0) continue;
            weight_sum += pellet->damage * 1000;
            health = (int)((long long)result.health_damage * weight_sum / attack.base_damage_milli);
            evasion = (int)((long long)result.evasion_spent_milli * weight_sum / attack.base_damage_milli);
            pellet->health_damage = health - assigned_health;
            pellet->evasion_spent_milli = evasion - assigned_evasion;
            assigned_health = health; assigned_evasion = evasion;
        }
    }
    return hit;
}
