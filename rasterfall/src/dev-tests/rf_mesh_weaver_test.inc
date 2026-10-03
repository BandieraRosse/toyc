#include "rf_weaver_blueprints_generated.h"

#define WEAVER_CHECK(condition) do { if (!(condition)) { \
    __printf("MESH-WEAVER failure line=%d\n", __LINE__); return 1; } } while (0)

static int weaver_test_close(double a, double b)
{
    double difference = a > b ? a - b : b - a;
    return difference <= 1.0e-7 * (1.0 + (a > 0.0 ? a : -a));
}

static int weaver_test_finish(struct toy_game *g, int step_ms)
{
    int elapsed = 0;
    while (g->weaver.phase != TOY_WEAVER_READY && elapsed < 120000) {
        toy_game_weaver_update(g, step_ms);
        elapsed += step_ms;
    }
    return elapsed;
}

static int rf_mesh_weaver_logic_test(void)
{
    static struct rasterfall_session fixture;
    struct toy_game *g = &fixture.game_state;
    struct toy_mesh_weaver saved, complete;
    struct toy_mesh_weaver_cost cost, changed;
    struct toy_mesh_blueprint bp = rf_weaver_blueprints[0].geometry, edit;
    struct toy_mesh_weaver_supply supply;
    struct toy_game_actor *player;
    struct camera view = {0};
    struct rasterfall_command command = {0};
    double initial_energy, expected, slow_progress;
    int elapsed, old_count;
    /* A zero-initialized session must never mistake static pickup zero for
     * its output slot. Static weapon stands remain reusable armory supplies. */
    memset(&fixture, 0, sizeof(fixture));
    toy_game_init(g, 7711);
    player = toy_game_local_player_actor(g);
    toy_game_set_primitives(g, NULL, 0, 20000);
    fixture.items[0].kind = TOY_MAP_PICKUP_WEAPON;
    fixture.items[0].weapon = TOY_GAME_WEAPON_AK;
    fixture.items[0].z = 600;
    fixture.items[0].y = -500;
    fixture.item_count = 1;
    rasterfall_session_weaver_sync(&fixture);
    WEAVER_CHECK(fixture.item_count == 1 && fixture.weaver_item_index == -1);
    memset(&view, 0, sizeof(view)); view.cy = view.pitch_cy = 1024;
    memset(&command, 0, sizeof(command)); command.buttons = RASTERFALL_CMD_INTERACT;
    rasterfall_session_step(&fixture, &view, &command, 16);
    WEAVER_CHECK(!toy_game_weapon_unlocked(g, TOY_GAME_WEAPON_AK) &&
        player->slots[0].weapon != TOY_GAME_WEAPON_AK && fixture.item_count == 1);
    g->unlocked_weapons |= 1u << TOY_GAME_WEAPON_AK;
    for (int repeat = 0; repeat < 2; ++repeat) {
        rasterfall_session_step(&fixture, &view, &command, 16);
        WEAVER_CHECK(player->slots[0].weapon == TOY_GAME_WEAPON_AK &&
            player->slots[0].mag == toy_game_weapon_info(TOY_GAME_WEAPON_AK)->mag_size &&
            player->slots[0].reserve == toy_game_weapon_info(TOY_GAME_WEAPON_AK)->reserve_max &&
            fixture.item_count == 1 && !g->weaver.collected_count);
        player->slots[0].mag = 1;
        player->slots[0].reserve = 0;
    }
    rasterfall_session_weaver_configure(&fixture, 1, 0, 600, -500);
    WEAVER_CHECK(!rasterfall_session_weaver_start(&fixture,
        &rf_weaver_blueprints[0].geometry));
    weaver_test_finish(g, 137);
    rasterfall_session_weaver_sync(&fixture);
    WEAVER_CHECK(fixture.item_count == 2 && fixture.weaver_item_index == 1 &&
        fixture.weaver_item_serial == g->weaver.job_serial);
    saved = g->weaver;
    rasterfall_session_weaver_configure(&fixture, 0, 0, 600, -500);
    WEAVER_CHECK(fixture.item_count == 1 && fixture.weaver_item_index == -1 &&
        g->weaver.phase == TOY_WEAVER_READY && g->weaver.job_serial == saved.job_serial &&
        g->weaver.supply.energy_kj == saved.supply.energy_kj);
    rasterfall_session_weaver_configure(&fixture, 1, 0, 600, -500);
    WEAVER_CHECK(fixture.item_count == 2 && fixture.weaver_item_serial == saved.job_serial);
    WEAVER_CHECK(rasterfall_session_weaver_collect(&fixture));
    WEAVER_CHECK(!rasterfall_session_weaver_start(&fixture,
        &rf_weaver_blueprints[1].geometry));
    WEAVER_CHECK(g->weaver.job_serial != saved.job_serial &&
        g->weaver.supply.energy_kj == saved.supply.energy_kj);
    weaver_test_finish(g, 137);
    rasterfall_session_weaver_sync(&fixture);
    WEAVER_CHECK(fixture.item_count == 2 && fixture.weaver_item_serial == g->weaver.job_serial &&
        g->weaver.produced_count == 2 && g->weaver.collected_count == 1);
    toy_game_init(g, 7711);
    rasterfall_session_weaver_sync(&fixture);
    WEAVER_CHECK(fixture.item_count == 1 && fixture.weaver_item_index == -1 &&
        fixture.weaver_item_serial == 0 && fixture.items[0].weapon == TOY_GAME_WEAPON_AK);
    __printf("MESH-WEAVER static pickup, output serial and reset isolation: PASS\n");
    memset(&fixture, 0, sizeof(fixture));
    fixture.weaver_item_index = fixture.highlight_index = -1;
    toy_game_init(g, 7711);
    player = toy_game_local_player_actor(g);
    WEAVER_CHECK(g->weaver.phase == TOY_WEAVER_IDLE && !g->weaver.enabled);
    WEAVER_CHECK(toy_game_weaver_start(g, &bp) == TOY_WEAVER_DISABLED);
    rasterfall_session_weaver_configure(&fixture, 1, 0, 600, -500);
    WEAVER_CHECK(toy_mesh_weaver_cost(&bp, &g->weaver.coefficients, &cost) == 0);
    edit = bp; ++edit.vertex_count;
    WEAVER_CHECK(!toy_mesh_weaver_cost(&edit, &g->weaver.coefficients, &changed));
    WEAVER_CHECK(changed.work > cost.work && changed.form_kj > cost.form_kj);
    edit = bp; ++edit.triangle_count;
    WEAVER_CHECK(!toy_mesh_weaver_cost(&edit, &g->weaver.coefficients, &changed));
    WEAVER_CHECK(changed.work > cost.work && changed.form_kj > cost.form_kj);
    edit = bp; edit.volume_mm3 += 1000000000ULL;
    WEAVER_CHECK(!toy_mesh_weaver_cost(&edit, &g->weaver.coefficients, &changed));
    WEAVER_CHECK(weaver_test_close(changed.form_kj - cost.form_kj,
                                  g->weaver.coefficients.kj_per_m3));
    edit = bp; edit.volume_mm3 *= 8;
    WEAVER_CHECK(!toy_mesh_weaver_cost(&edit, &g->weaver.coefficients, &changed));
    WEAVER_CHECK(weaver_test_close(changed.form_kj - cost.form_kj,
        7.0 * bp.volume_mm3 / 1000000000.0 * g->weaver.coefficients.kj_per_m3));
    edit = bp; edit.volume_mm3 = 0;
    WEAVER_CHECK(toy_game_weaver_start(g, &edit) == TOY_WEAVER_BAD_BLUEPRINT);
    edit = bp; edit.bounds_mm[2] = 2000;
    WEAVER_CHECK(toy_game_weaver_start(g, &edit) == TOY_WEAVER_TOO_LARGE);
    edit = bp; edit.bounds_mm[0] = 58; edit.bounds_mm[2] = 1004;
    WEAVER_CHECK(!toy_game_weaver_start(g, &edit)); /* fitted diagonal gun */
    toy_mesh_weaver_defaults(&g->weaver); g->weaver.enabled = 1;
    supply = g->weaver.supply; supply.storage_bytes = cost.storage_bytes - 1;
    toy_game_weaver_set_supply(g, &supply);
    WEAVER_CHECK(toy_game_weaver_start(g, &bp) == TOY_WEAVER_NO_STORAGE);
    supply.storage_bytes = 64ULL * 1024 * 1024;
    toy_game_weaver_set_supply(g, &supply);

    /* The same work split into 1, 16 and 137 ms simulation slices has the
     * same active duration and resource ledger, including phase boundaries. */
    expected = toy_mesh_weaver_estimate(&g->weaver, &cost) * 1000.0;
    WEAVER_CHECK(!toy_game_weaver_start(g, &bp));
    elapsed = weaver_test_finish(g, 1);
    WEAVER_CHECK(g->weaver.phase == TOY_WEAVER_READY);
    WEAVER_CHECK(elapsed >= expected && elapsed - expected < 1.001);
    complete = g->weaver;
    for (int variant = 0; variant < 2; ++variant) {
        int slice = variant ? 137 : 16;
        toy_mesh_weaver_defaults(&g->weaver); g->weaver.enabled = 1;
        WEAVER_CHECK(!toy_game_weaver_start(g, &bp));
        elapsed = weaver_test_finish(g, slice);
        WEAVER_CHECK(g->weaver.phase == TOY_WEAVER_READY);
        WEAVER_CHECK(elapsed >= expected && elapsed - expected < slice + 0.001);
        WEAVER_CHECK(weaver_test_close(g->weaver.elapsed_ms, complete.elapsed_ms));
        WEAVER_CHECK(weaver_test_close(g->weaver.used_compute, cost.work));
        WEAVER_CHECK(weaver_test_close(g->weaver.used_form_kj, cost.form_kj));
        WEAVER_CHECK(weaver_test_close(g->weaver.supply.energy_kj,
                                      complete.supply.energy_kj));
    }
    __printf("MESH-WEAVER formula and fixed-step: PASS ideal_ms=%.3f\n", expected);

    /* Both missing inputs pause every phase. Supply toggles preserve spent
     * resources; reduced compute changes the actual progress rate. */
    toy_mesh_weaver_defaults(&g->weaver); g->weaver.enabled = 1;
    initial_energy = g->weaver.supply.energy_kj;
    WEAVER_CHECK(!toy_game_weaver_start(g, &bp));
    toy_game_weaver_update(g, 1000);
    saved = g->weaver;
    supply = saved.supply; supply.power_on = 0;
    toy_game_weaver_set_supply(g, &supply);
    toy_game_weaver_update(g, 10000);
    WEAVER_CHECK(g->weaver.pause_reason == TOY_WEAVER_NO_POWER);
    WEAVER_CHECK(g->weaver.progress == saved.progress &&
        g->weaver.used_form_kj == saved.used_form_kj &&
        g->weaver.elapsed_ms == saved.elapsed_ms);
    supply.power_on = 1; supply.cpu_on = 0;
    toy_game_weaver_set_supply(g, &supply);
    toy_game_weaver_update(g, 10000);
    WEAVER_CHECK(g->weaver.pause_reason == TOY_WEAVER_NO_COMPUTE &&
                 g->weaver.progress == saved.progress);
    supply.cpu_on = 1; supply.x1_on = 0;
    toy_game_weaver_set_supply(g, &supply);
    toy_game_weaver_update(g, 1000);
    slow_progress = g->weaver.progress - saved.progress;
    WEAVER_CHECK(slow_progress > 0.0);
    g->weaver = saved;
    toy_game_weaver_update(g, 1000);
    WEAVER_CHECK(g->weaver.progress - saved.progress > slow_progress);
    supply = g->weaver.supply; supply.energy_kj = initial_energy;
    toy_game_weaver_set_supply(g, &supply);
    WEAVER_CHECK(g->weaver.supply.energy_kj < initial_energy);
    weaver_test_finish(g, 16);
    WEAVER_CHECK(weaver_test_close(initial_energy - g->weaver.supply.energy_kj,
        g->weaver.used_form_kj + g->weaver.used_base_kj));
    saved = g->weaver;
    supply = saved.supply; supply.power_on = 0;
    toy_game_weaver_set_supply(g, &supply);
    toy_game_weaver_update(g, 100000);
    WEAVER_CHECK(g->weaver.phase == TOY_WEAVER_READY && g->weaver.produced_count == 1);
    WEAVER_CHECK(g->weaver.supply.energy_kj == saved.supply.energy_kj);
    WEAVER_CHECK(toy_game_weaver_start(g, &bp) == TOY_WEAVER_OUTPUT_OCCUPIED);
    WEAVER_CHECK(toy_game_weaver_collect(g, player) == 1);
    WEAVER_CHECK(toy_game_weaver_collect(g, player) == 0);
    WEAVER_CHECK(g->weaver.collected_count == 1 && g->weaver.produced_count == 1);
    toy_mesh_weaver_defaults(&g->weaver); g->weaver.enabled = 1;
    g->weaver.supply.energy_kj = 0.2;
    WEAVER_CHECK(!toy_game_weaver_start(g, &bp));
    toy_game_weaver_update(g, 10000);
    WEAVER_CHECK(g->weaver.supply.energy_kj >= 0.0 && g->weaver.produced_count == 0);
    WEAVER_CHECK(g->weaver.pause_reason == TOY_WEAVER_NO_POWER);
    WEAVER_CHECK(weaver_test_close(g->weaver.used_form_kj +
                                  g->weaver.used_base_kj, 0.2));
    __printf("MESH-WEAVER pause, supply conservation and single output: PASS\n");

    /* Two real catalog weapons: manufacture, ordinary E highlight/claim,
     * real hitscan, slot switching, finite magazine, supply and reload. */
    for (int catalog = 0; catalog < 2; ++catalog) {
        const struct toy_game_weapon_info *weapon;
        struct toy_game_actor_command action = {0};
        int before, reserve, target_id;
        unsigned sequence;
        memset(&fixture, 0, sizeof(fixture));
        fixture.weaver_item_index = fixture.highlight_index = -1;
        toy_game_init(g, 7711);
        player = toy_game_local_player_actor(g);
        if (catalog == 1) toy_game_actor_equip_weapon(g, player, TOY_GAME_WEAPON_AK);
        toy_game_set_primitives(g, NULL, 0, 20000);
        fixture.items[0].kind = TOY_MAP_PICKUP_AMMO;
        fixture.items[0].x = 5000;
        fixture.item_count = 1;
        rasterfall_session_weaver_configure(&fixture, 1, 0, 600, -500);
        bp = rf_weaver_blueprints[catalog].geometry;
        weapon = toy_game_weapon_info(bp.weapon);
        WEAVER_CHECK(!rasterfall_session_weaver_start(&fixture, &bp));
        toy_game_weaver_update(g, 1000);
        saved = g->weaver;
        rasterfall_session_weaver_configure(&fixture, 1, 0, 600, -500);
        WEAVER_CHECK(g->weaver.progress == saved.progress &&
                     g->weaver.supply.energy_kj == saved.supply.energy_kj);
        weaver_test_finish(g, 16);
        rasterfall_session_weaver_sync(&fixture);
        old_count = fixture.item_count;
        WEAVER_CHECK(fixture.weaver_item_index == 1 && old_count == 2);
        rasterfall_session_weaver_sync(&fixture);
        WEAVER_CHECK(fixture.item_count == old_count);
        memset(&view, 0, sizeof(view)); view.cy = view.pitch_cy = 1024;
        WEAVER_CHECK(rasterfall_session_compute_highlight(&fixture, &view) == 1);
        memset(&command, 0, sizeof(command)); command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step(&fixture, &view, &command, 16);
        WEAVER_CHECK(fixture.weaver_item_index == -1 && fixture.item_count == 1);
        WEAVER_CHECK(fixture.items[0].kind == TOY_MAP_PICKUP_AMMO);
        WEAVER_CHECK(g->weaver.collected_count == 1 &&
            toy_game_actor_current_weapon(player) == bp.weapon &&
            player->slots[weapon->slot].mag == weapon->mag_size &&
            player->slots[weapon->slot].reserve == 0);
        WEAVER_CHECK(!rasterfall_session_weaver_collect(&fixture));
        if (bp.weapon == TOY_GAME_WEAPON_AK)
            WEAVER_CHECK(!toy_game_weapon_unlocked(g, bp.weapon));
        action.switch_slot = -1;
        toy_game_execute_actor_command(g, player, &action, 1000, 100);
        target_id = toy_game_add_gunner(g, 0, 0, 2000, "WEAVER TARGET");
        WEAVER_CHECK(target_id >= 0);
        toy_game_actor_by_id(g, target_id)->evasion.reserve_milli = 0;
        player->pitch_sy = -128; player->pitch_cy = 1024;
        before = player->slots[weapon->slot].mag; sequence = player->fire_seq;
        toy_game_actor_fire(g, player, 0, 1024, 100);
        WEAVER_CHECK(player->slots[weapon->slot].mag == before - 1 &&
            player->fire_seq == sequence + 1 && player->ray_count > 0);
        WEAVER_CHECK(player->rays[0].hit_enemy);
        action.reload = 1;
        toy_game_execute_actor_command(g, player, &action, 16, 100);
        WEAVER_CHECK(!player->reloading && player->slots[weapon->slot].mag == before - 1);
        action.reload = 0;
        for (int round = 1; round < weapon->mag_size; ++round) {
            toy_game_execute_actor_command(g, player, &action, 10000, 100);
            sequence = player->fire_seq;
            toy_game_actor_fire(g, player, 1024, 0, 100);
            WEAVER_CHECK(player->fire_seq == sequence + 1);
        }
        WEAVER_CHECK(player->slots[weapon->slot].mag == 0 && !player->reloading);
        toy_game_execute_actor_command(g, player, &action, 10000, 100);
        sequence = player->fire_seq;
        toy_game_actor_fire(g, player, 1024, 0, 100);
        WEAVER_CHECK(player->fire_seq == sequence && player->slots[weapon->slot].reserve == 0);
        WEAVER_CHECK(toy_game_actor_refill_ammo(g, player));
        reserve = player->slots[weapon->slot].reserve;
        WEAVER_CHECK(reserve > 0 && reserve != TOY_GAME_AMMO_INFINITE);
        toy_game_execute_actor_command(g, player, &action, 16, 100);
        WEAVER_CHECK(player->reloading);
        action.reload = 0;
        toy_game_execute_actor_command(g, player, &action, 10000, 100);
        WEAVER_CHECK(!player->reloading &&
            player->slots[weapon->slot].mag == weapon->mag_size &&
            player->slots[weapon->slot].reserve == reserve - weapon->mag_size);
        action.switch_slot = weapon->slot ? 0 : 1;
        toy_game_execute_actor_command(g, player, &action, 16, 100);
        WEAVER_CHECK(player->current_slot == action.switch_slot);
        action.switch_slot = weapon->slot;
        toy_game_execute_actor_command(g, player, &action, 16, 100);
        WEAVER_CHECK(player->current_slot == weapon->slot &&
            player->slots[weapon->slot].mag == weapon->mag_size);
        __printf("MESH-WEAVER real weapon: PASS weapon=%s active_ms=%.3f work=%.3f form_kj=%.6f\n",
            weapon->name, g->weaver.elapsed_ms, g->weaver.used_compute,
            g->weaver.used_form_kj);
    }
    toy_game_init(g, 7711);
    WEAVER_CHECK(!g->weaver.enabled && g->weaver.phase == TOY_WEAVER_IDLE &&
        g->weaver.produced_count == 0);
    player = toy_game_local_player_actor(g);
    toy_game_actor_refill_ammo(g, player);
    WEAVER_CHECK(player->slots[1].reserve == TOY_GAME_AMMO_INFINITE);
    return 0;
}

#undef WEAVER_CHECK
