/* Game-owned shared destination fields. Included after movement/navigation
 * primitives so planning and execution use the same support/collision rules.
 * No clock, allocation, combat RNG, leader, or synchronous fallback search. */
#define FLOW_INF 0xffffffffu
#define FLOW_SETTLED 65535

static int flow_cell(const struct toy_game *g, int x, int z)
{
    if (g->nav_cell_size <= 0 || x < g->nav_origin || z < g->nav_origin ||
        x >= g->nav_origin + g->nav_width * g->nav_cell_size ||
        z >= g->nav_origin + g->nav_height * g->nav_cell_size) return -1;
    return nav_cell_index(g, x, z);
}

static int flow_add_node(struct toy_game *g, int x, int z, int y)
{
    int cell = flow_cell(g, x, z), n;
    struct toy_game_ground_query ground;
    struct toy_game_enemy probe = {0};
    if (cell < 0) return 0;
    for (n = g->flow_cells[cell]; n; n = g->flow_nodes[n].sibling)
        if (g->flow_nodes[n].x == x && g->flow_nodes[n].z == z &&
            g->flow_nodes[n].y == y) return n;
    ground = toy_game_query_ground(g, x, z, 0, y);
    if (g->primitives && (!ground.has_support || ground.support_y != y))
        return 0;
    if (enemy_step_blocked(g, &probe, x, z, TOY_GAME_ENEMY_RADIUS, y,
                           !point_on_walkable_ramp(g, x, z,
                                                   TOY_GAME_ENEMY_RADIUS)))
        return 0;
    if (g->flow_count >= TOY_GAME_FLOW_MAX_NODES) {
        if (g->update_profile) g->update_profile->flow_overflow++;
        return 0;
    }
    n = ++g->flow_count;
    g->flow_nodes[n].x = x; g->flow_nodes[n].z = z;
    g->flow_nodes[n].y = y; g->flow_nodes[n].cell = (unsigned short)cell;
    g->flow_nodes[n].sibling = g->flow_cells[cell];
    g->flow_cells[cell] = (unsigned short)n;
    return n;
}

static void flow_add_supports(struct toy_game *g, int x, int z)
{
    int i;
    struct toy_game_ground_query ground = toy_game_query_ground(g, x, z, 0, 0);
    if (!g->primitives || ground.has_support)
        flow_add_node(g, x, z, ground.support_y);
    for (i = 0; i < g->primitive_count; ++i) {
        const struct toy_map_primitive *p = &g->primitives[i];
        int y;
        if (!(p->flags & TOY_MAP_PRIMITIVE_WALKABLE) ||
            x < p->minx || x > p->maxx || z < p->minz || z > p->maxz)
            continue;
        y = primitive_surface_height(p, x, z);
        if (y != ground.support_y) flow_add_node(g, x, z, y);
    }
}

static void enemy_flow_rebuild(struct toy_game *g)
{
    int i, x, z, pad = TOY_GAME_ENEMY_RADIUS + 16;
    g->flow_count = 0;
    g->flow_generation++;
    g->flow_repair_cell = -1;
    memset(g->flow_cells, 0, sizeof(g->flow_cells));
    memset(g->flow_edges, 0, sizeof(g->flow_edges));
    memset(g->flow_refined, 0, sizeof(g->flow_refined));
    for (i = 0; i < TOY_GAME_FLOW_FIELDS; ++i) g->flow_fields[i].active = 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; ++i) {
        g->enemies[i].nav_field = -1;
        g->enemies[i].nav_field_generation = 0;
        g->enemies[i].nav_flow_node = 0;
        g->enemies[i].nav_flow_retry = 0;
        g->enemies[i].nav_active = 0;
        g->enemies[i].nav_explore_ticks = 0;
    }
    if (!g->nav_flow_enabled) return;
    /* Loading is separate from tick budgets. Unlike the legacy component
     * map, this samples the actual radius and retains overlapping floors. */
    for (z = 0; z < g->nav_height; ++z)
        for (x = 0; x < g->nav_width; ++x)
            flow_add_supports(g, g->nav_origin + x * g->nav_cell_size +
                               g->nav_cell_size / 2,
                               g->nav_origin + z * g->nav_cell_size +
                               g->nav_cell_size / 2);
    /* Off-grid doorway corners and ramp/landing centers connect narrow
     * features whose free space does not contain any coarse cell center. */
    for (i = 0; i < g->primitive_count; ++i) {
        const struct toy_map_primitive *p = &g->primitives[i];
        int cx = p->minx + (p->maxx - p->minx) / 2;
        int cz = p->minz + (p->maxz - p->minz) / 2;
        if (p->shape == TOY_MAP_PRIMITIVE_BOX &&
            (p->flags & TOY_MAP_PRIMITIVE_COLLISION)) {
            flow_add_supports(g, p->minx - pad, p->minz - pad);
            flow_add_supports(g, p->minx - pad, p->maxz + pad);
            flow_add_supports(g, p->maxx + pad, p->minz - pad);
            flow_add_supports(g, p->maxx + pad, p->maxz + pad);
        }
        if (!(p->flags & TOY_MAP_PRIMITIVE_WALKABLE)) continue;
        flow_add_supports(g, cx, cz);
        flow_add_supports(g, p->minx + pad, cz);
        flow_add_supports(g, p->maxx - pad, cz);
        flow_add_supports(g, cx, p->minz + pad);
        flow_add_supports(g, cx, p->maxz - pad);
        if (p->shape == TOY_MAP_PRIMITIVE_RAMP_X) {
            flow_add_supports(g, p->minx, cz);
            flow_add_supports(g, p->maxx, cz);
            flow_add_supports(g, p->minx - pad, cz);
            flow_add_supports(g, p->maxx + pad, cz);
        } else if (p->shape == TOY_MAP_PRIMITIVE_RAMP_Z) {
            flow_add_supports(g, cx, p->minz);
            flow_add_supports(g, cx, p->maxz);
            flow_add_supports(g, cx, p->minz - pad);
            flow_add_supports(g, cx, p->maxz + pad);
        }
    }
}

/* Prove an entire flat corridor in one primitive pass. This fast path is
 * deliberately stronger than sampling: one floor covers the complete swept
 * footprint and no relevant solid overlaps it. Ambiguous ramps, steps and
 * overlapping platforms always use the ordinary movement sweep below. */
static int flow_flat_corridor(const struct toy_game *g, int x, int z, int y,
                              int tx, int tz, int ty)
{
    int radius = TOY_GAME_ENEMY_RADIUS, i, supported = !g->primitives;
    int minx = (x < tx ? x : tx) - radius, maxx = (x > tx ? x : tx) + radius;
    int minz = (z < tz ? z : tz) - radius, maxz = (z > tz ? z : tz) + radius;
    if (y != ty || minx < -g->room_limit || minz < -g->room_limit ||
        maxx > g->room_limit || maxz > g->room_limit) return 0;
    for (i = 0; i < g->safe_room_count; ++i) {
        const struct toy_game_box *b = &g->safe_rooms[i];
        if (maxx > b->minx && minx < b->maxx &&
            maxz > b->minz && minz < b->maxz) return 0;
    }
    for (i = 0; i < g->primitive_count; ++i) {
        const struct toy_map_primitive *p = &g->primitives[i];
        if (maxx < p->minx || minx > p->maxx ||
            maxz < p->minz || minz > p->maxz) continue;
        if ((p->flags & TOY_MAP_PRIMITIVE_WALKABLE) &&
            p->shape == TOY_MAP_PRIMITIVE_FLAT && p->surface_y0 == y &&
            minx >= p->minx && maxx <= p->maxx &&
            minz >= p->minz && maxz <= p->maxz) supported = 1;
        /* Even a non-solid upper floor can change the selected support. */
        if ((p->flags & TOY_MAP_PRIMITIVE_WALKABLE) &&
            p->shape != TOY_MAP_PRIMITIVE_BOX &&
            (p->surface_y0 != y || p->surface_y1 != y)) return 0;
        if (!(p->flags & TOY_MAP_PRIMITIVE_COLLISION)) continue;
        if (p->shape == TOY_MAP_PRIMITIVE_BOX) {
            if (!(p->flags & TOY_MAP_PRIMITIVE_BLOCKS_AIRBORNE) &&
                (p->base_y >= y + RASTERFALL_HUMAN_HEIGHT_RFU ||
                 (p->surface_y0 > p->base_y && p->surface_y0 <= y))) continue;
            return 0;
        }
        if (p->shape != TOY_MAP_PRIMITIVE_FLAT || p->surface_y0 > y) return 0;
    }
    return supported;
}

/* -1 is deferred, never interpreted as blocked/unreachable. Reserve samples
 * before entering a bounded sweep; local connectors have a separate reserve
 * so a cold field cannot starve already moving enemies. */
static int flow_connection(struct toy_game *g, int x, int z, int y,
                            int tx, int tz, int ty, int local)
{
    int dx = tx - x, dz = tz - z;
    int length = isqrt((long long)dx * dx + (long long)dz * dz);
    int steps = length / (TOY_GAME_ENEMY_RADIUS / 2) + 1;
    int cost = steps * 2 + 1, i, result = 0;
    int *budget = local ? &g->flow_local_budget : &g->flow_sample_budget;
    struct toy_game_enemy probe = {0};
    if (length > TOY_GAME_SHORT_CONNECTION_RANGE) return 0;
    if (*budget < cost) return -1;
    *budget -= cost;
    if (g->update_profile) g->update_profile->flow_samples += cost;
    if (!enemy_positions_share_target_region(g, x, z, tx, tz)) return 0;
    if ((!g->update_profile || !g->update_profile->flow_reference_probe) &&
        flow_flat_corridor(g, x, z, y, tx, tz, ty)) return 1;
    /* All support/body probes stay inside this expanded rectangle, including
     * X-then-Z substeps. The list includes both solids and support surfaces,
     * regardless of height/flags, so ramps and overlapping floors retain
     * their exact narrow-phase semantics. It is never retained across calls. */
    if (!g->update_profile || !g->update_profile->flow_reference_probe) {
        int radius = TOY_GAME_ENEMY_RADIUS;
        int minx = (x < tx ? x : tx) - radius;
        int maxx = (x > tx ? x : tx) + radius;
        int minz = (z < tz ? z : tz) - radius;
        int maxz = (z > tz ? z : tz) + radius;
        g->flow_probe_count = 0;
        for (i = 0; i < g->primitive_count; ++i) {
            const struct toy_map_primitive *p = &g->primitives[i];
            if (p->maxx < minx || p->minx > maxx ||
                p->maxz < minz || p->minz > maxz) continue;
            g->flow_probe_indices[g->flow_probe_count++] = (unsigned short)i;
        }
        g->flow_probe_active = 1;
    }
    probe.x = x; probe.z = z; probe.ground_y = y;
    if (!enemy_try_step(g, &probe, x, z, TOY_GAME_ENEMY_RADIUS)) goto finished;
    for (i = 1; i <= steps; ++i) {
        int nx = x + (int)((long long)dx * i / steps);
        int nz = z + (int)((long long)dz * i / steps);
        if ((nx != probe.x && !enemy_try_step(g, &probe, nx, probe.z,
                                             TOY_GAME_ENEMY_RADIUS)) ||
            (nz != probe.z && !enemy_try_step(g, &probe, nx, nz,
                                             TOY_GAME_ENEMY_RADIUS))) goto finished;
    }
    result = probe.ground_y == ty;
finished:
    g->flow_probe_active = 0;
    memset(g->flow_probe_indices, 0,
           g->flow_probe_count * sizeof(g->flow_probe_indices[0]));
    g->flow_probe_count = 0;
    return result;
}

static int flow_edge(struct toy_game *g, int from, int to)
{
    struct toy_game_flow_edge *cache = &g->flow_edges[
        ((unsigned int)from * 73856093u ^ (unsigned int)to * 19349663u) %
        TOY_GAME_FLOW_EDGE_CACHE];
    const struct toy_game_flow_node *a = &g->flow_nodes[from];
    const struct toy_game_flow_node *b = &g->flow_nodes[to];
    int result;
    if (cache->from == from && cache->to == to && cache->result) {
        if (g->update_profile) g->update_profile->flow_hits++;
        return cache->result == 2;
    }
    result = flow_connection(g, a->x, a->z, a->y, b->x, b->z, b->y, 0);
    if (result < 0) return result;
    cache->from = (unsigned short)from; cache->to = (unsigned short)to;
    cache->result = result ? 2 : 1;
    return result;
}

static int enemy_flow_direct(struct toy_game *g, struct toy_game_enemy *e,
                              int tx, int tz)
{
    int i = e->nav_target_actor, result, end_y = e->ground_y;
    if (i >= 0 && i < TOY_GAME_MAX_ACTORS) {
        const struct toy_game_actor *a = &g->actors[i];
        if (!a->active || a->x != tx || a->z != tz) return 0;
        struct toy_game_ground_query ground = toy_game_query_ground(g, tx, tz, 0,
                                                                    a->ground_y);
        g->flow_local_budget -= 80;
        result = short_connection_height(g, e->x, e->z, tx, tz,
                                           TOY_GAME_ENEMY_RADIUS, e->ground_y, &end_y);
        return result && end_y == ground.support_y;
    }
    return 0;
}

static unsigned int flow_priority(const struct toy_game *g,
                                   const struct toy_game_flow_field *f, int n)
{
    int dx = g->flow_nodes[n].x - f->guide_x;
    int dz = g->flow_nodes[n].z - f->guide_z;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return f->distance[n] + (unsigned int)(dx + dz) * 2u;
}

static int flow_less(const struct toy_game *g,
                      const struct toy_game_flow_field *f, int a, int b)
{
    unsigned int pa = flow_priority(g, f, a), pb = flow_priority(g, f, b);
    return pa < pb || (pa == pb && a < b);
}

static void flow_push(const struct toy_game *g, struct toy_game_flow_field *f, int node)
{
    int pos = f->position[node];
    if (!pos) pos = ++f->heap_count;
    while (pos > 1 && flow_less(g, f, node, f->heap[pos / 2])) {
        f->heap[pos] = f->heap[pos / 2];
        f->position[f->heap[pos]] = (unsigned short)pos;
        pos /= 2;
    }
    f->heap[pos] = (unsigned short)node;
    f->position[node] = (unsigned short)pos;
}

static int flow_pop(const struct toy_game *g, struct toy_game_flow_field *f)
{
    int result = f->heap[1], last = f->heap[f->heap_count--], pos = 1;
    while (pos * 2 <= f->heap_count) {
        int child = pos * 2;
        if (child < f->heap_count && flow_less(g, f, f->heap[child + 1],
                                               f->heap[child])) child++;
        if (!flow_less(g, f, f->heap[child], last)) break;
        f->heap[pos] = f->heap[child];
        f->position[f->heap[pos]] = (unsigned short)pos;
        pos = child;
    }
    if (f->heap_count) {
        f->heap[pos] = (unsigned short)last;
        f->position[last] = (unsigned short)pos;
    }
    f->position[result] = FLOW_SETTLED;
    return result;
}

static void flow_start(struct toy_game *g, struct toy_game_flow_field *f,
                        int actor, int x, int z, int y)
{
    uint64_t lease = f->active && f->actor == actor &&
                     f->actor_id == g->actors[actor].actor_id ?
                     f->born_tick : g->nav_tick;
    f->active = 1; f->generation = ++g->flow_generation;
    f->actor = actor; f->actor_id = g->actors[actor].actor_id;
    f->target_x = x; f->target_z = z;
    f->target_source_y = g->actors[actor].ground_y;
    f->target_y = toy_game_query_ground(g, x, z, 0, y).support_y;
    f->seed = 0; f->clearing = 1; f->heap_count = 0;
    f->current = 0; f->complete = 0; f->users = 0;
    f->refresh_tick = 0; f->used_tick = g->nav_tick; f->born_tick = lease;
    memset(f->requested, 0, sizeof(f->requested));
    memset(f->hints, 0, sizeof(f->hints));
    if (g->update_profile) g->update_profile->flow_builds++;
}

/* Enumerate at most nine cells. Remember failed candidates while a seed is
 * pending so the next tick cannot keep re-testing the same blocked point. */
static int flow_neighbor(const struct toy_game *g, int cell, int slot)
{
    int x = cell % g->nav_width + slot % 3 - 1;
    int z = cell / g->nav_width + slot / 3 - 1;
    if (x < 0 || z < 0 || x >= g->nav_width || z >= g->nav_height) return -1;
    return z * g->nav_width + x;
}

static void flow_work(struct toy_game *g, struct toy_game_flow_field *f,
                       int quota)
{
    while (quota-- > 0 && g->flow_edge_budget > 0) {
        const struct toy_game_flow_node *a, *b;
        int from, allowed, cell;
        unsigned int cost;
        g->flow_edge_budget--;
        if (f->clearing) {
            int end = f->clearing + 128, i;
            if (end > g->flow_count + 1) end = g->flow_count + 1;
            for (i = f->clearing; i < end; ++i) {
                f->distance[i] = FLOW_INF; f->position[i] = 0; f->next[i] = 0;
            }
            f->clearing = end <= g->flow_count ? end : 0;
            if (!f->clearing) { f->neighbor_cell = 0; f->neighbor = -1; }
            continue;
        }
        if (!f->seed) {
            cell = flow_cell(g, f->target_x, f->target_z);
            if (cell < 0 || f->neighbor_cell >= 9) { f->complete = 1; return; }
            if (f->neighbor < 0) {
                int c = flow_neighbor(g, cell, f->neighbor_cell);
                f->neighbor = c < 0 ? 0 : g->flow_cells[c];
            }
            if (!f->neighbor) { f->neighbor_cell++; f->neighbor = -1; continue; }
            from = f->neighbor; a = &g->flow_nodes[from];
            allowed = flow_connection(g, a->x, a->z, a->y,
                f->target_x, f->target_z, f->target_y, 0);
            if (allowed < 0) return;
            f->neighbor = a->sibling;
            if (!allowed) continue;
            f->seed = from;
            f->distance[from] = 0; f->next[from] = (unsigned short)from;
            flow_push(g, f, from);
        }
        if (!f->current) {
            if (!f->heap_count) { f->complete = 1; return; }
            f->current = flow_pop(g, f);
            f->neighbor_cell = 0; f->neighbor = -1;
        }
        if (f->neighbor_cell >= 9) { f->current = 0; continue; }
        b = &g->flow_nodes[f->current];
        if (f->neighbor < 0) {
            int c = flow_neighbor(g, b->cell, f->neighbor_cell);
            f->neighbor = c < 0 ? 0 : g->flow_cells[c];
        }
        if (!f->neighbor) { f->neighbor_cell++; f->neighbor = -1; continue; }
        from = f->neighbor; a = &g->flow_nodes[from];
        if (f->position[from] == FLOW_SETTLED) { f->neighbor = a->sibling; continue; }
        {
            int dx = a->x - b->x, dz = a->z - b->z, dy = a->y - b->y;
            cost = (unsigned int)isqrt((long long)dx * dx + (long long)dz * dz +
                                       (long long)dy * dy) + 1u;
        }
        cost += f->distance[f->current];
        if (cost >= f->distance[from]) { f->neighbor = a->sibling; continue; }
        allowed = flow_edge(g, from, f->current); /* incoming, not outgoing */
        if (allowed < 0) return;
        if (g->update_profile) g->update_profile->flow_edges++;
        f->neighbor = a->sibling;
        if (!allowed) continue;
        f->distance[from] = cost;
        f->next[from] = (unsigned short)f->current;
        flow_push(g, f, from);
    }
}

static void flow_repair_request(struct toy_game *g, int cell, int y)
{
    if (cell < 0 || g->flow_refined[cell] || g->flow_repair_cell >= 0) return;
    g->flow_repair_cell = cell; g->flow_repair_cursor = 0;
    g->flow_repair_y = y;
}

static int flow_demand_pending(const struct toy_game_flow_field *f)
{
    int i;
    if (f->clearing || !f->seed) return 1;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; ++i)
        if (f->requested[i] && f->position[f->requested[i]] != FLOW_SETTLED) return 1;
    return 0;
}

static void enemy_flow_update(struct toy_game *g)
{
    int i;
    int64_t started = g->update_profile && g->update_profile->clock_us ?
                      g->update_profile->clock_us() : 0;
    if (!g->nav_flow_enabled) return;
    g->flow_edge_budget = TOY_GAME_FLOW_EDGES_PER_TICK;
    g->flow_sample_budget = TOY_GAME_FLOW_SAMPLES_PER_TICK;
    g->flow_local_budget = TOY_GAME_FLOW_LOCAL_PER_TICK;
    /* One coalesced cell refinement, one point per tick. Existing node IDs
     * and edge results remain valid; fields restart only after the batch. */
    if (g->flow_repair_cell >= 0) {
        int cell = g->flow_repair_cell, k = g->flow_repair_cursor++;
        int x = g->nav_origin + (cell % g->nav_width) * g->nav_cell_size;
        int z = g->nav_origin + (cell / g->nav_width) * g->nav_cell_size;
        struct toy_game_ground_query q;
        x += (2 * (k % 4) + 1) * g->nav_cell_size / 8;
        z += (2 * (k / 4) + 1) * g->nav_cell_size / 8;
        q = toy_game_query_ground(g, x, z, 0, g->flow_repair_y);
        if (!g->primitives || q.has_support) flow_add_node(g, x, z, q.support_y);
        if (g->flow_repair_cursor == 16) {
            g->flow_refined[cell] = 1; g->flow_repair_cell = -1;
            if (g->update_profile) g->update_profile->flow_repairs++;
            for (i = 0; i < TOY_GAME_FLOW_FIELDS; ++i) {
                struct toy_game_flow_field *f = &g->flow_fields[i];
                if (f->active) flow_start(g, f, f->actor, f->target_x,
                                         f->target_z, f->target_y);
            }
        }
    }
    for (i = 0; i < TOY_GAME_FLOW_FIELDS; ++i) {
        struct toy_game_flow_field *f = &g->flow_fields[i];
        f->users = 0;
        if (f->active && (!g->actors[f->actor].active ||
            g->actors[f->actor].actor_id != f->actor_id ||
            g->actors[f->actor].hp <= 0)) f->active = 0;
    }
    for (i = 0; i < TOY_GAME_FLOW_FIELDS * 4; ++i) {
        int slot = (g->flow_cursor + i) % TOY_GAME_FLOW_FIELDS;
        struct toy_game_flow_field *f = &g->flow_fields[slot];
        if (f->active && !f->complete && flow_demand_pending(f)) flow_work(g, f, 128);
    }
    g->flow_cursor = (g->flow_cursor + 1) % TOY_GAME_FLOW_FIELDS;
    if (started) g->update_profile->flow_us +=
        g->update_profile->clock_us() - started;
}

static int enemy_flow_waypoint_impl(struct toy_game *g, struct toy_game_enemy *e,
                               int tx, int tz, int *out_x, int *out_z)
{
    int i, slot = -1, actor = -1, cell, node = 0;
    int ty = 0, best_slot = -1;
    unsigned int best = FLOW_INF;
    struct toy_game_flow_field *f;
    actor = e->nav_target_actor;
    if (actor >= 0 && actor < TOY_GAME_MAX_ACTORS && g->actors[actor].active)
        ty = g->actors[actor].ground_y;
    else actor = -1;
    if (actor < 0) return 0;
    for (i = 0; i < TOY_GAME_FLOW_FIELDS; ++i) {
        f = &g->flow_fields[i];
        if (f->active && f->actor == actor && f->actor_id == g->actors[actor].actor_id)
            { slot = i; break; }
        if (!f->active) best_slot = i;
    }
    if (slot < 0) {
        if (best_slot < 0) {
            /* Lease gives a busy field time to finish. Expired least-recent
             * use admits new targets without starvation or synchronous work. */
            for (i = 0; i < TOY_GAME_FLOW_FIELDS; ++i) {
                f = &g->flow_fields[i];
                if (g->nav_tick - f->born_tick < 256) continue;
                /* Preserve unfinished static searches. A moving target may
                 * restart work, but cannot renew its lease indefinitely. */
                if (flow_demand_pending(f) && !f->complete && !f->refresh_tick &&
                    g->nav_tick - f->used_tick < 64) continue;
                if (best_slot < 0 || f->used_tick < g->flow_fields[best_slot].used_tick)
                    best_slot = i;
            }
            if (best_slot < 0) return 0;
            if (g->update_profile) g->update_profile->flow_evictions++;
        }
        slot = best_slot;
        flow_start(g, &g->flow_fields[slot], actor, tx, tz, ty);
        g->flow_fields[slot].guide_x = e->x;
        g->flow_fields[slot].guide_z = e->z;
    }
    f = &g->flow_fields[slot];
    f->used_tick = g->nav_tick; f->users++;
    if (tx == f->target_x && tz == f->target_z && ty == f->target_source_y)
        ty = f->target_y;
    else if (g->nav_tick >= (uint64_t)f->refresh_tick)
        ty = toy_game_query_ground(g, tx, tz, 0, ty).support_y;
    else ty = f->target_y;
    if (g->nav_tick >= (uint64_t)f->refresh_tick && f->seed &&
        (tx != f->target_x || tz != f->target_z || ty != f->target_y)) {
        const struct toy_game_flow_node *s = &g->flow_nodes[f->seed];
        int allowed = flow_connection(g, s->x, s->z, s->y, tx, tz, ty, 1);
        if (allowed == 0) flow_start(g, f, actor, tx, tz, ty);
        if (allowed > 0) {
            f->target_x = tx; f->target_z = tz; f->target_y = ty;
            f->target_source_y = g->actors[actor].ground_y;
        }
        if (allowed >= 0) f->refresh_tick = (int)g->nav_tick + 64;
    }
    if (e->nav_field != slot || e->nav_field_generation != f->generation) {
        /* A field refresh invalidates links, not the physical destination
         * already being approached. Finish that short leg under collision. */
        if (e->nav_field != slot) e->nav_active = 0;
        e->nav_field = slot; e->nav_field_generation = f->generation;
        e->nav_flow_node = 0; e->nav_stuck_ms = 0;
        e->nav_flow_reject_count = 0;
    }
    cell = flow_cell(g, e->x, e->z);
    /* Request a local node even while the field is cold. The reverse queue
     * is biased toward its first requester, but its retained frontier can
     * serve any other start. Stop expanding when all requested nodes settle;
     * positive edge costs and links to settled parents prevent route cycles. */
    if (cell >= 0 && !e->nav_active) {
        int closest = 0;
        long long nearest = 0;
        for (i = 0; i < 9; ++i) {
            int c = flow_neighbor(g, cell, i), n;
            if (c < 0) continue;
            for (n = g->flow_cells[c]; n; n = g->flow_nodes[n].sibling) {
                const struct toy_game_flow_node *p = &g->flow_nodes[n];
                long long dx = (long long)p->x - e->x, dz = (long long)p->z - e->z;
                long long dy = (long long)p->y - e->ground_y;
                long long d = dx * dx + dz * dz + dy * dy;
                int j, rejected = 0;
                for (j = 0; j < e->nav_flow_reject_count; ++j)
                    if (e->nav_flow_rejected[j] == n) { rejected = 1; break; }
                if (rejected) continue;
                if (!closest || d < nearest) { closest = n; nearest = d; }
            }
        }
        f->requested[e - g->enemies] = (unsigned short)closest;
        if (!closest) flow_repair_request(g, cell, e->ground_y);
    }
    if (e->nav_active) {
        long long dx = (long long)e->nav_x - e->x;
        long long dz = (long long)e->nav_z - e->z;
        long long d = dx * dx + dz * dz;
        if (d + 16 < e->nav_best_distance2) {
            e->nav_best_distance2 = d; e->nav_stuck_ms = 0;
        } else e->nav_stuck_ms += 16;
        if (d > 4 && e->nav_stuck_ms < 800) {
            *out_x = e->nav_x; *out_z = e->nav_z; return 1;
        }
        e->nav_active = 0;
        if (d > 4) {
            /* A failed individual approach does not invalidate everybody's
             * route. Keep a local rejection and try another shared exit. */
            if (e->nav_flow_node && e->nav_flow_reject_count < 32)
                e->nav_flow_rejected[e->nav_flow_reject_count++] =
                    (unsigned short)e->nav_flow_node;
            e->nav_flow_node = 0;
        } else if (e->nav_flow_node && !f->clearing) {
            /* Only actual arrival publishes a breadcrumb. It is advice for
             * this layer, not a proof that every point in the cell connects. */
            if (e->nav_depart_cell >= 0) {
                struct toy_game_flow_hint *h =
                    &f->hints[e->nav_depart_cell % TOY_GAME_FLOW_HINTS];
                h->cell = e->nav_depart_cell; h->y = e->nav_depart_y;
                h->node = (unsigned short)e->nav_flow_node;
            }
            node = f->next[e->nav_flow_node];
            if (node == e->nav_flow_node) {
                /* At the destination anchor, release route ownership so the
                 * final approach can resume. Do not reselect this same node. */
                e->nav_flow_node = 0;
                f->requested[e - g->enemies] = 0;
                return 0;
            }
        }
    }
    if (f->clearing || cell < 0) return 0;
    if (!node) {
        const struct toy_game_flow_hint *h = &f->hints[cell % TOY_GAME_FLOW_HINTS];
        if (h->node && h->cell == cell && h->y == e->ground_y) {
            int rejected = 0;
            for (i = 0; i < e->nav_flow_reject_count; ++i)
                if (e->nav_flow_rejected[i] == h->node) rejected = 1;
            if (!rejected) {
                const struct toy_game_flow_node *p = &g->flow_nodes[h->node];
                long long dx = (long long)p->x - e->x, dz = (long long)p->z - e->z;
                if (dx * dx + dz * dz > 4) node = h->node;
                else if (f->next[h->node] != h->node) node = f->next[h->node];
            }
        }
    }
    if (!node) {
        /* Read shared directions without per-enemy sweep or admission wait.
         * Tentative nodes already point to a settled successor and are usable. */
        for (i = 0; i < 9; ++i) {
            int c = flow_neighbor(g, cell, i), n;
            if (c < 0) continue;
            for (n = g->flow_cells[c]; n; n = g->flow_nodes[n].sibling) {
                const struct toy_game_flow_node *p = &g->flow_nodes[n];
                int dx = p->x - e->x, dz = p->z - e->z, dy = p->y - e->ground_y;
                unsigned int score;
                int rejected = 0, j;
                if (!f->next[n] || f->distance[n] == FLOW_INF) continue;
                if (dy > TOY_CONFIG_GROUND_STEP_HEIGHT ||
                    dy < -TOY_CONFIG_GROUND_STEP_HEIGHT) continue;
                for (j = 0; j < e->nav_flow_reject_count; ++j)
                    if (e->nav_flow_rejected[j] == n) { rejected = 1; break; }
                if (rejected) continue;
                score = (unsigned int)isqrt((long long)dx * dx + (long long)dz * dz +
                                            (long long)dy * dy) + f->distance[n];
                if (score < best) { best = score; node = n; }
            }
        }
        if (!node) {
            if (f->complete) flow_repair_request(g, cell, e->ground_y);
            e->nav_flow_retry = 0;
            e->nav_flow_reject_count = 0;
            return 0;
        }
    }
    e->nav_flow_retry = 0;
    e->nav_depart_cell = cell; e->nav_depart_y = e->ground_y;
    e->nav_flow_node = node;
    e->nav_x = g->flow_nodes[node].x; e->nav_z = g->flow_nodes[node].z;
    e->nav_active = 1; e->nav_stuck_ms = 0;
    {
        long long dx = (long long)e->nav_x - e->x, dz = (long long)e->nav_z - e->z;
        e->nav_best_distance2 = dx * dx + dz * dz;
    }
    *out_x = e->nav_x; *out_z = e->nav_z;
    return 1;
}

static int enemy_flow_waypoint(struct toy_game *g, struct toy_game_enemy *e,
                               int tx, int tz, int *out_x, int *out_z)
{
    struct toy_game_update_profile *p = g->update_profile;
    int64_t start = p && p->clock_us ? p->clock_us() : 0;
    int result = enemy_flow_waypoint_impl(g, e, tx, tz, out_x, out_z);
    if (start) p->nav_intent_us += p->clock_us() - start;
    return result;
}
