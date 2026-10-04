#include "rf_game_lifecycle.h"

#define FRONTIER_SESSION_CHECK(condition) do { if (!(condition)) { \
    failure = __LINE__; goto cleanup; } } while (0)

/* Arrange the player at an authored device, without fixing map coordinates.
 * These are logic fixtures; the ordinary-window route is checked separately. */
static int frontier_session_test_at(struct rasterfall_session *s,
    struct camera *view, int facility)
{
    const struct rf_map_runtime_object *o = rf_map_runtime_find_object(
        &s->map_ops.runtime, s->frontier.config.facilities[facility].point.id);
    struct toy_game_actor *p = toy_game_local_player_actor(&s->game_state);
    if (!o || !p) return 0;
    p->x = o->x; p->z = o->z; p->ground_y = o->y; p->airborne_y = 0;
    p->sy = view->sy = 0; p->cy = view->cy = 1024;
    rasterfall_camera_set_body(view, p->x, p->z);
    return 1;
}

static struct rf_player_request frontier_session_test_request(
    const struct rasterfall_session *s, int facility)
{
    struct rf_player_request a = {0};
    const struct rf_frontier_facility_config *f = &s->frontier.config.facilities[facility];
    a.operation = RF_PLAYER_FACILITY_CAPTURE; a.value = facility;
    a.world_generation = s->scene_local.world_generation;
    a.device_identity = f->identity; a.device_generation = f->generation;
    return a;
}

static int rf_frontier_session_logic_test(void)
{
    static struct rasterfall_session fixture;
    static struct rf_game_runtime test_runtime;
    struct toy_game *g = &fixture.game_state;
    struct toy_game_actor *player;
    struct camera view = {0};
    struct rasterfall_command idle = {0};
    struct rf_player_request request, stale;
    struct toy_mesh_weaver_supply supply;
    struct toy_mesh_weaver ready;
    int failure = 0, has_ak = 0, has_pistol = 0, elapsed, reused_index;
    double initial_energy;

    memset(&fixture, 0, sizeof(fixture));
    memset(&test_runtime, 0, sizeof(test_runtime));
    test_runtime.session = &fixture; test_runtime.net.mode = RASTERFALL_NET_OFF;
    rf_player_controls_init(&test_runtime.player_controls);
    FRONTIER_SESSION_CHECK(!rasterfall_session_load(&fixture,
        "rasterfall/assets/maps/frontier_station_01.map"));
    FRONTIER_SESSION_CHECK(fixture.world_id == RASTERFALL_WORLD_FRONTIER_STATION_01 &&
        fixture.frontier.phase == RF_FRONTIER_INACTIVE);
    rasterfall_session_reset(&fixture, &view, 7913);
    player = toy_game_local_player_actor(g);
    FRONTIER_SESSION_CHECK(g->state == TOY_GAME_PLAYING &&
        fixture.frontier.phase == RF_FRONTIER_ASSAULT && fixture.frontier.guards_alive == 12);
    FRONTIER_SESSION_CHECK(fixture.frontier_autonomous_squad.index>=0 &&
        g->squads[fixture.frontier_autonomous_squad.index].count==5 &&
        g->squads[fixture.frontier_autonomous_squad.index].mode==TOY_GAME_SQUAD_FOLLOW);
    for(int i=0;i<RF_FRONTIER_TRACKED;i++) {
        const struct rf_frontier_entity *e=&fixture.frontier.entities[i];
        if(e->active && e->actor && e->role==RF_FRONTIER_GUARD)
            FRONTIER_SESSION_CHECK(!g->squad_lookup[e->index] && g->actors[e->index].ai_guard_radius>0);
    }
    for (int i = 0; i < TOY_GAME_WEAPON_SLOTS; ++i) {
        has_ak |= player->slots[i].weapon == TOY_GAME_WEAPON_AK;
        has_pistol |= player->slots[i].weapon == TOY_GAME_WEAPON_PISTOL;
    }
    FRONTIER_SESSION_CHECK(has_ak && has_pistol && !g->weaver.enabled &&
        !g->weaver.supply.power_on && !g->weaver.supply.x1_on &&
        !g->weaver.supply.cpu_on && !g->weaver.supply.storage_bytes &&
        !g->weaver.produced_count && !g->weaver.collected_count);
    initial_energy = g->weaver.supply.energy_kj;
    for (int i = 0; i < 3; ++i) {
        supply = g->weaver.supply;
        if (i == 0) supply.cpu_on = 1;
        if (i == 1) supply.power_on = supply.x1_on = 1;
        if (i == 2) supply.storage_bytes = 64ULL * 1024 * 1024;
        FRONTIER_SESSION_CHECK(!rasterfall_session_weaver_set_supply(&fixture, &supply));
    }
    /* Capture each facility alone in a fresh action, so a grant cannot hide
     * an accidental dependency on the order used by the full route below. */
    for (int facility = 0; facility < 3; ++facility) {
        frontier_test_remove_role(&fixture.frontier, g, RF_FRONTIER_GUARD, 1);
        rasterfall_session_step(&fixture, &view, &idle, 16);
        FRONTIER_SESSION_CHECK(frontier_session_test_at(&fixture, &view, facility));
        request = frontier_session_test_request(&fixture, facility);
        FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &request,
            RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_OK);
        FRONTIER_SESSION_CHECK(g->weaver.enabled == (facility == 0) &&
            g->weaver.supply.cpu_on == (facility == 1) &&
            g->weaver.supply.storage_bytes == (facility == 1 ? 64ULL * 1024 * 1024 : 0) &&
            g->weaver.supply.power_on == (facility == 2) &&
            g->weaver.supply.x1_on == (facility == 2) &&
            g->weaver.supply.energy_kj == initial_energy && !g->weaver.produced_count);
        rasterfall_session_reset(&fixture, &view, 7920 + facility);
    }
    FRONTIER_SESSION_CHECK(frontier_session_test_at(&fixture, &view, 0));
    request = frontier_session_test_request(&fixture, 0);
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &request,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_UNAVAILABLE && !fixture.frontier.captured[0]);
    /* Existing defender identities are removed in the fixture; the live
     * session step must advance phase before accepting facility operations. */
    frontier_test_remove_role(&fixture.frontier, g, RF_FRONTIER_GUARD, 1);
    rasterfall_session_step(&fixture, &view, &idle, 16);
    FRONTIER_SESSION_CHECK(fixture.frontier.phase == RF_FRONTIER_PREPARE);
    FRONTIER_SESSION_CHECK(frontier_session_test_at(&fixture, &view, 0));
    request = frontier_session_test_request(&fixture, 0);
    stale = request; ++stale.world_generation;
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &stale,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_STALE);
    stale = request; ++stale.device_generation;
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &stale,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_STALE);
    stale = request; stale.device_identity = fixture.frontier.config.facilities[1].identity;
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &stale,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_STALE);
    FRONTIER_SESSION_CHECK(rf_frontier_mission_capture(&fixture.frontier, g,
        fixture.frontier.config.facilities[0].point.id, request.device_identity,
        request.device_generation, player->actor_id,
        g->actors[fixture.frontier_squad_indices[0]].combat_generation) == RF_FRONTIER_CAPTURE_INVALID);
    FRONTIER_SESSION_CHECK(!fixture.frontier.captured[0]);
    test_runtime.net.mode = RASTERFALL_NET_HOST;
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &request,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_PERMISSION);
    test_runtime.net.mode = RASTERFALL_NET_OFF;
    /* Each device grants only its own capability. Warehouse bytes are a
     * capacity ceiling, never an energy or manufactured-product grant. */
    for (int facility = 0; facility < 3; ++facility) {
        FRONTIER_SESSION_CHECK(frontier_session_test_at(&fixture, &view, facility));
        request = frontier_session_test_request(&fixture, facility);
        FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &request,
            RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_OK);
        FRONTIER_SESSION_CHECK(g->weaver.enabled &&
            g->weaver.supply.cpu_on == (facility >= 1) &&
            g->weaver.supply.storage_bytes == (facility >= 1 ? 64ULL * 1024 * 1024 : 0) &&
            g->weaver.supply.power_on == (facility >= 2) &&
            g->weaver.supply.x1_on == (facility >= 2) &&
            g->weaver.supply.energy_kj == initial_energy &&
            !g->weaver.produced_count && !g->weaver.collected_count);
    }
    FRONTIER_SESSION_CHECK(!rasterfall_session_weaver_start(&fixture,
        &rf_weaver_blueprints[0].geometry));
    for (elapsed = 0; elapsed < 120000 && g->weaver.phase != TOY_WEAVER_READY; elapsed += 137)
        toy_game_weaver_update(g, 137);
    FRONTIER_SESSION_CHECK(g->weaver.phase == TOY_WEAVER_READY &&
        g->weaver.produced_count == 1 && !g->weaver.collected_count &&
        g->weaver.supply.energy_kj < initial_energy);
    rasterfall_session_weaver_sync(&fixture);
    ready = g->weaver;
    for (int facility = 0; facility < 3; ++facility) {
        FRONTIER_SESSION_CHECK(frontier_session_test_at(&fixture, &view, facility));
        request = frontier_session_test_request(&fixture, facility);
        FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &request,
            RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_OK);
        supply = g->weaver.supply;
        supply.power_on = supply.cpu_on = supply.x1_on = 0;
        FRONTIER_SESSION_CHECK(rasterfall_session_weaver_set_supply(&fixture, &supply));
        supply.power_on = supply.cpu_on = supply.x1_on = 1;
        supply.energy_kj = initial_energy; /* even a stale supply cannot refill */
        FRONTIER_SESSION_CHECK(rasterfall_session_weaver_set_supply(&fixture, &supply));
        FRONTIER_SESSION_CHECK(g->weaver.phase == ready.phase &&
            g->weaver.job_serial == ready.job_serial && g->weaver.progress == ready.progress &&
            g->weaver.supply.energy_kj == ready.supply.energy_kj &&
            g->weaver.produced_count == ready.produced_count &&
            g->weaver.collected_count == ready.collected_count &&
            fixture.weaver_item_serial == ready.job_serial);
    }
    stale = frontier_session_test_request(&fixture, 0);
    rasterfall_session_reset(&fixture, &view, 7914);
    FRONTIER_SESSION_CHECK(frontier_session_test_at(&fixture, &view, 0));
    FRONTIER_SESSION_CHECK(fixture.scene_local.world_generation != stale.world_generation &&
        fixture.frontier.config.facilities[0].generation != stale.device_generation &&
        !g->weaver.produced_count && !fixture.frontier.captured[0]);
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &stale,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_STALE);
    stale.world_generation = fixture.scene_local.world_generation;
    FRONTIER_SESSION_CHECK(rf_player_execute(&test_runtime, &stale,
        RF_COMMAND_PERMISSION_USER).code == RF_PLAYER_STALE);
    /* Incapacitated player can still be rescued by an original party member. */
    player = toy_game_local_player_actor(g);
    player->state = TOY_GAME_ACTOR_DOWNED; player->hp = 0;
    rasterfall_session_step(&fixture, &view, &idle, 16);
    FRONTIER_SESSION_CHECK(fixture.frontier.phase != RF_FRONTIER_FAILED && g->state == TOY_GAME_PLAYING);
    for (int i = 0; i < 5; ++i) {
        struct toy_game_actor *a = &g->actors[fixture.frontier_squad_indices[i]];
        a->hp = 0; a->state = TOY_GAME_ACTOR_DOWNED;
    }
    reused_index = fixture.frontier_squad_indices[0];
    g->actors[reused_index].hp = g->actors[reused_index].max_hp;
    g->actors[reused_index].state = TOY_GAME_ACTOR_ALIVE;
    ++g->actors[reused_index].combat_generation;
    rasterfall_session_step(&fixture, &view, &idle, 16);
    FRONTIER_SESSION_CHECK(fixture.frontier.phase == RF_FRONTIER_FAILED && g->state == TOY_GAME_OVER);
    /* An ID mismatch alone also invalidates a reused slot, independently of
     * generation. This catches implementations which verify only one field. */
    rasterfall_session_reset(&fixture, &view, 7915);
    player = toy_game_local_player_actor(g);
    player->state = TOY_GAME_ACTOR_DOWNED; player->hp = 0;
    for (int i = 0; i < 5; ++i) {
        struct toy_game_actor *a = &g->actors[fixture.frontier_squad_indices[i]];
        a->hp = 0; a->state = TOY_GAME_ACTOR_DOWNED;
    }
    reused_index = fixture.frontier_squad_indices[0];
    g->actors[reused_index].hp = g->actors[reused_index].max_hp;
    g->actors[reused_index].state = TOY_GAME_ACTOR_ALIVE;
    g->actors[reused_index].actor_id = TOY_GAME_MAX_ACTORS + 17;
    rasterfall_session_step(&fixture, &view, &idle, 16);
    FRONTIER_SESSION_CHECK(fixture.frontier.phase == RF_FRONTIER_FAILED && g->state == TOY_GAME_OVER);
    /* Formal bindings preserve two three-person batches and all spawn quotas. */
    rasterfall_session_reset(&fixture,&view,7916);
    fixture.frontier.phase=RF_FRONTIER_COUNTERATTACK;
    fixture.frontier.phase_ms=0;fixture.frontier.first_final_infected_ms=-1;
    rasterfall_session_step(&fixture,&view,&idle,16);
    for(elapsed=0;elapsed<12500 && fixture.frontier.final_gunners_spawned<6;elapsed+=16)
        rasterfall_session_step(&fixture,&view,&idle,16);
    FRONTIER_SESSION_CHECK(fixture.frontier.final_gunners_spawned==6 &&
        fixture.frontier_gunner_bound_mask==63);
    for(int i=0;i<2;i++) {
        struct toy_game_squad_handle h=fixture.frontier_gunner_squads[i];
        FRONTIER_SESSION_CHECK(h.index>=0 && g->squads[h.index].count==3 &&
            g->squads[h.index].mode==TOY_GAME_SQUAD_ASSAULT &&
            g->squads[h.index].x==fixture.frontier_config.assault_x+(i?1080:-1080) &&
            g->squads[h.index].z==fixture.frontier_config.assault_z);
        for(int n=0;n<g->squads[h.index].count;n++) {
            int index=g->squads[h.index].members[n].identity.index;
            FRONTIER_SESSION_CHECK(g->actors[index].deployment_x==fixture.frontier_config.assault_x &&
                g->actors[index].deployment_z==fixture.frontier_config.assault_z);
        }
    }
cleanup:
    rasterfall_session_unload(&fixture);
    test_runtime.session = NULL;
    if (failure) __printf("FRONTIER session failure line=%d\n", failure);
    else __printf("FRONTIER session permissions, resources, stale requests and party lifetime: PASS\n");
    return failure;
}

#undef FRONTIER_SESSION_CHECK
