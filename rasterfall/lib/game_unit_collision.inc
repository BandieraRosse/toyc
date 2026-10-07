/* Dynamic bodies belong to Game. Terrain/nav queries remain static, and only
 * real pool members participate in movement sweeps (never planner probes).
 * Scan the bounded 64+64 pools with a swept AABB before narrow phase. There
 * is no stale position cache, neighbour truncation, allocation or combat RNG. */
#define UNIT_SWEEP_SCALE 65536
#define UNIT_CONTACT_SKIN 2

struct unit_contact {
    int fraction, nx, nz;
};

static int unit_actor_solid(const struct toy_game_actor *a)
{
    return a->active && a->hp > 0 && a->state == TOY_GAME_ACTOR_ALIVE &&
        !a->base_core && !a->animation_demo;
}

static int unit_actor_moving(const struct toy_game *g,
                             const struct toy_game_actor *a)
{
    int i;
    if (!unit_actor_solid(a) || a->special_control ||
        a->knockback_x || a->knockback_z) return 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; ++i)
        if (a == &g->actors[i]) return 1;
    return 0;
}

static int unit_enemy_moving(const struct toy_game *g,
                             const struct toy_game_enemy *e)
{
    int i;
    if (e->active != 1 || e->hp <= 0 || e->ability.charge_active ||
        e->airborne_ms || e->knockback_x || e->knockback_z) return 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; ++i)
        if (e == &g->enemies[i]) return 1;
    return 0;
}

static long long unit_floor_div(long long n, int d)
{
    long long q = n / d;
    return q - (n % d < 0);
}

/* Restrict the horizontal sweep to the time interval in which the two body
 * height ranges overlap. In particular, a different storey is not a wall. */
static int unit_height_window(int y, int end_y, int other_y, int *lo, int *hi)
{
    int delta = end_y - y;
    int low = other_y - RASTERFALL_HUMAN_HEIGHT_RFU + 1;
    int high = other_y + RASTERFALL_HUMAN_HEIGHT_RFU - 1;
    long long from, to;
    *lo = 0; *hi = UNIT_SWEEP_SCALE;
    if (!delta) return y >= low && y <= high;
    if (delta > 0) {
        from = -unit_floor_div(-(long long)(low-y)*UNIT_SWEEP_SCALE, delta);
        to = unit_floor_div((long long)(high-y)*UNIT_SWEEP_SCALE, delta);
    } else {
        from = -unit_floor_div(-(long long)(y-high)*UNIT_SWEEP_SCALE, -delta);
        to = unit_floor_div((long long)(y-low)*UNIT_SWEEP_SCALE, -delta);
    }
    if (from > UNIT_SWEEP_SCALE || to < 0 || from > to) return 0;
    if (from > 0) *lo = (int)from;
    if (to < UNIT_SWEEP_SCALE) *hi = (int)to;
    return 1;
}

static long long unit_distance_at(int x, int z, int dx, int dz, int fraction)
{
    long long px = x + (long long)dx * fraction / UNIT_SWEEP_SCALE;
    long long pz = z + (long long)dz * fraction / UNIT_SWEEP_SCALE;
    return px*px + pz*pz;
}

/* Integer closest approach + bisection avoids a squared discriminant and
 * its overflow. The small skin covers RFU rounding at contact. */
static void unit_sweep_body(int x, int z, int y, int dx, int dz, int end_y,
                            int radius, int bx, int bz, int by, int br,
                            struct unit_contact *contact)
{
    int lo, hi, closest, safe, inside, middle;
    int r = radius + br + UNIT_CONTACT_SKIN + (abs(dx)+abs(dz))/UNIT_SWEEP_SCALE;
    long long px = (long long)x-bx, pz = (long long)z-bz;
    long long length2 = (long long)dx*dx + (long long)dz*dz;
    long long approach = px*dx + pz*dz, limit = (long long)r*r;
    int minx = x < x+dx ? x : x+dx, maxx = x > x+dx ? x : x+dx;
    int minz = z < z+dz ? z : z+dz, maxz = z > z+dz ? z : z+dz;
    if (bx < minx-r || bx > maxx+r || bz < minz-r || bz > maxz+r ||
        !unit_height_window(y, end_y, by, &lo, &hi)) return;
    if (lo == 0 && px*px+pz*pz < limit && approach >= 0 && length2 &&
        unit_distance_at((int)px,(int)pz,dx,dz,UNIT_SWEEP_SCALE) >= px*px+pz*pz)
        return; /* Existing overlap may escape, but never move deeper. */
    if (!length2) closest = lo;
    else {
        long long fraction = -approach*UNIT_SWEEP_SCALE/length2;
        closest = fraction < lo ? lo : fraction > hi ? hi : (int)fraction;
    }
    if (unit_distance_at((int)px, (int)pz, dx, dz, closest) >= limit) return;
    safe = lo; inside = closest;
    if (unit_distance_at((int)px, (int)pz, dx, dz, lo) < limit)
        safe = lo > 0 ? lo-1 : 0;
    else {
        while (inside-safe > 1) {
            middle = safe + (inside-safe)/2;
            if (unit_distance_at((int)px, (int)pz, dx, dz, middle) < limit)
                inside = middle;
            else safe = middle;
        }
    }
    if (safe >= contact->fraction) return;
    contact->fraction = safe;
    contact->nx = (int)px + (int)((long long)dx*safe/UNIT_SWEEP_SCALE);
    contact->nz = (int)pz + (int)((long long)dz*safe/UNIT_SWEEP_SCALE);
    if (!contact->nx && !contact->nz) {
        contact->nx = -dx; contact->nz = -dz;
    }
}

static struct unit_contact unit_sweep(const struct toy_game *g,
    const struct toy_game_actor *self_actor, const struct toy_game_enemy *self_enemy,
    int x, int z, int y, int dx, int dz, int end_y, int radius)
{
    struct unit_contact contact = {UNIT_SWEEP_SCALE, 0, 0};
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; ++i) {
        const struct toy_game_actor *a = &g->actors[i];
        if (a == self_actor || !unit_actor_solid(a)) continue;
        unit_sweep_body(x,z,y,dx,dz,end_y,radius,a->x,a->z,
            a->ground_y+a->airborne_y,TOY_GAME_PLAYER_RADIUS,&contact);
    }
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; ++i) {
        const struct toy_game_enemy *e = &g->enemies[i];
        if (e == self_enemy || e->active != 1 || e->hp <= 0) continue;
        unit_sweep_body(x,z,y,dx,dz,end_y,radius,e->x,e->z,
            e->ground_y+e->airborne_y,enemy_radius(e),&contact);
    }
    return contact;
}

int toy_game_try_move_actor(struct toy_game *g, struct toy_game_actor *a, int x, int z)
{
    struct toy_game_actor previous;
    struct unit_contact contact;
    int dynamic;
    if (!g || !a) return 0;
    dynamic = unit_actor_moving(g,a);
    if (!dynamic) return actor_try_move_terrain(g,a,x,z);
    memcpy(&previous,a,sizeof(previous));
    if (!actor_try_move_terrain(g,a,x,z)) return 0;
    contact = unit_sweep(g,a,NULL,previous.x,previous.z,
        previous.ground_y+previous.airborne_y,x-previous.x,z-previous.z,
        a->ground_y+a->airborne_y,TOY_GAME_PLAYER_RADIUS);
    if (contact.fraction == UNIT_SWEEP_SCALE) return 1;
    memcpy(a,&previous,sizeof(previous));
    return 0;
}

static void unit_slide_remainder(const struct unit_contact *contact, int *dx, int *dz)
{
    long long normal2 = (long long)contact->nx*contact->nx +
                         (long long)contact->nz*contact->nz;
    long long dot = (long long)*dx*contact->nx + (long long)*dz*contact->nz;
    if (normal2 && dot < 0) {
        *dx -= (int)(dot*contact->nx/normal2);
        *dz -= (int)(dot*contact->nz/normal2);
        /* Truncation must not leave an inward one-RFU component. */
        if ((long long)*dx*contact->nx + (long long)*dz*contact->nz < 0) {
            if (abs(contact->nx) >= abs(contact->nz)) *dx += contact->nx > 0 ? 1 : -1;
            else *dz += contact->nz > 0 ? 1 : -1;
        }
    }
}

int toy_game_move_actor_sliding(struct toy_game *g, struct toy_game_actor *a, int dx, int dz)
{
    int start_x, start_z, steps, step, max_delta;
    if (!g || !a) return 0;
    if (!unit_actor_moving(g,a)) return actor_move_sliding_base(g,a,dx,dz);
    start_x = a->x; start_z = a->z;
    max_delta = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
    steps = max_delta/(TOY_GAME_PLAYER_RADIUS/2)+1;
    for (step = 1; step <= steps && !a->airborne_ms; ++step) {
        int mx = (int)((long long)dx*step/steps)-(int)((long long)dx*(step-1)/steps);
        int mz = (int)((long long)dz*step/steps)-(int)((long long)dz*(step-1)/steps);
        int iteration;
        for (iteration = 0; iteration < 3 && (mx || mz); ++iteration) {
            struct toy_game_actor probe;
            struct unit_contact contact;
            int moved_x, moved_z, px, pz;
            memcpy(&probe,a,sizeof(probe));
            if (!actor_move_sliding_base(g,&probe,mx,mz)) break;
            mx = probe.x-a->x; mz = probe.z-a->z;
            contact = unit_sweep(g,a,NULL,a->x,a->z,a->ground_y+a->airborne_y,
                mx,mz,probe.ground_y+probe.airborne_y,TOY_GAME_PLAYER_RADIUS);
            if (contact.fraction == UNIT_SWEEP_SCALE) {
                actor_move_sliding_base(g,a,mx,mz); break;
            }
            px = (int)((long long)mx*contact.fraction/UNIT_SWEEP_SCALE);
            pz = (int)((long long)mz*contact.fraction/UNIT_SWEEP_SCALE);
            moved_x = a->x; moved_z = a->z;
            actor_move_sliding_base(g,a,px,pz);
            mx -= a->x-moved_x; mz -= a->z-moved_z;
            unit_slide_remainder(&contact,&mx,&mz);
        }
    }
    return a->x != start_x || a->z != start_z;
}

static int enemy_try_step(struct toy_game *g, struct toy_game_enemy *e, int x, int z, int radius)
{
    struct toy_game_enemy previous;
    struct unit_contact contact;
    if (!g || !e) return 0;
    if (!unit_enemy_moving(g,e)) return enemy_try_step_terrain(g,e,x,z,radius);
    memcpy(&previous,e,sizeof(previous));
    if (!enemy_try_step_terrain(g,e,x,z,radius)) return 0;
    contact = unit_sweep(g,NULL,e,previous.x,previous.z,previous.ground_y,
        x-previous.x,z-previous.z,e->ground_y,enemy_radius(e));
    if (contact.fraction == UNIT_SWEEP_SCALE) return 1;
    memcpy(e,&previous,sizeof(previous));
    e->nav_unit_blocked=1;
    return 0;
}

static int enemy_move_sliding(struct toy_game *g, struct toy_game_enemy *e, int dx, int dz, int radius)
{
    int start_x=e->x, start_z=e->z, iteration;
    struct unit_contact last_contact = {UNIT_SWEEP_SCALE,0,0};
    e->nav_unit_blocked=0;
    /* Keep static segment proofs valid at inclusive box boundaries. */
    if (unit_enemy_moving(g,e)) radius+=UNIT_CONTACT_SKIN;
    for (iteration=0; iteration<3 && (dx || dz); ++iteration) {
        struct toy_game_enemy probe;
        struct unit_contact contact;
        int px, pz, previous_x=e->x, previous_z=e->z;
        memcpy(&probe,e,sizeof(probe));
        if (!enemy_try_step_terrain(g,&probe,e->x+dx,e->z+dz,radius)) {
            enemy_try_step(g,e,e->x+dx,e->z,radius);
            enemy_try_step(g,e,e->x,e->z+dz,radius);
            break;
        }
        if (!unit_enemy_moving(g,e)) {
            enemy_try_step_terrain(g,e,probe.x,probe.z,radius); break;
        }
        contact=unit_sweep(g,NULL,e,e->x,e->z,e->ground_y,dx,dz,probe.ground_y,enemy_radius(e));
        last_contact=contact;
        if (contact.fraction!=UNIT_SWEEP_SCALE) e->nav_unit_blocked=1;
        if (contact.fraction==UNIT_SWEEP_SCALE) {
            enemy_try_step(g,e,probe.x,probe.z,radius); break;
        }
        px=(int)((long long)dx*contact.fraction/UNIT_SWEEP_SCALE);
        pz=(int)((long long)dz*contact.fraction/UNIT_SWEEP_SCALE);
        enemy_try_step(g,e,e->x+px,e->z+pz,radius);
        dx-=e->x-previous_x; dz-=e->z-previous_z;
        unit_slide_remainder(&contact,&dx,&dz);
    }
    if (e->x==start_x && e->z==start_z &&
        last_contact.fraction!=UNIT_SWEEP_SCALE) {
        int normal=isqrt((long long)last_contact.nx*last_contact.nx +
                         (long long)last_contact.nz*last_contact.nz);
        int step=e->speed/2;
        if (normal && step>0) {
            int sx=-(int)((long long)last_contact.nz*step/normal);
            int sz=(int)((long long)last_contact.nx*step/normal);
            if (!enemy_try_step(g,e,e->x+sx,e->z+sz,radius))
                enemy_try_step(g,e,e->x-sx,e->z-sz,radius);
        }
    }
    return e->x!=start_x || e->z!=start_z;
}

/* The old centre-distance bite range is smaller than two solid body radii.
 * Preserve its minimum, but keep melee reachable at physical contact. */
static int enemy_contact_attack_range(const struct toy_game_enemy *e)
{
    int contact = enemy_radius(e)+TOY_GAME_PLAYER_RADIUS+24;
    return contact > TOY_GAME_ATTACK_RANGE ? contact : TOY_GAME_ATTACK_RANGE;
}
