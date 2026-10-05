/* Protect config boundaries and live behavior through public gameplay APIs. */
static int gameplay_config_logic_test(void)
{
    static struct toy_game g, independent;
    static struct rasterfall_session session;
    struct toy_game_gameplay_config defaults, parsed, before;
    struct toy_game_actor *a;
    struct toy_game_enemy *e;
    struct camera camera = {0};
    struct toy_game_attack attack = {0};
    struct toy_game_damage_result result;
    struct toy_map_primitive floor = test_box_primitive(-20000,20000,-20000,20000,-154,0,
        TOY_MAP_PRIMITIVE_COLLISION|TOY_MAP_PRIMITIVE_WALKABLE);
    const char custom[] = "\xEF\xBB\xBF# config\r\nwave_first_delay_ms=32\r\n"
        "wave_announce_ms=16\nwave_pause_ms=48\nwave_max=2\nwave_scale_percent=50\n"
        "base_hp=600\nbase_regen_ms=32\nshove_range_rfu=1500\nshove_push_rfu=100\n"
        "shove_stun_ms=321\ncharger_shove_stun_ms=123\n"
        "melee_range_rfu=1800\nmelee_damage=5\nthrow_speed_rfu_per_second=5120\n"
        "throw_gravity_rfu_per_second2=3200\nbomb_damage=11\nbomb_radius_rfu=600\n"
        "bomb_fuse_ms=0\nmolotov_burn_ms=64\nmolotov_tick_ms=16\nmolotov_damage=3\n"
        "evasion_window_ms=200\nevasion_retrigger_ms=700\nevasion_recovery_delay_ms=100\n"
        "evasion_bullet_window_percent=25\nevasion_melee_window_percent=50\n"
        "evasion_melee_cost_percent=200 # last field beyond a 32-bit seen mask\n";
    const char *bad[] = {"base_hp=600\nunknown=1\n", "base_hp=600\nbase_hp=700\n",
        "base_regen_ms=0\n", "molotov_tick_ms=0\n", "wave_scale_percent=0\n",
        "throw_bounce_per_mille=1001\n", "base_hp=-1\n", "base_hp=1.5\n",
        "base_hp=999999999999999999999\n", "base_hp=\n", "base_hp 600\n",
        "evasion_melee_cost_percent=200\nevasion_melee_cost_percent=300\n",
        "throw_gravity_rfu_per_second2=1\n"};
    const char embedded[] = "base_hp=600\0\n";
    const char *error = NULL;
    int line = 0, base_id;
    toy_game_gameplay_defaults(&defaults);
    if (toy_game_gameplay_parse("",0,&parsed,&line,&error)<0 ||
        memcmp(&parsed,&defaults,sizeof(parsed))) return 1;
    if (toy_game_gameplay_parse(custom,sizeof(custom)-1,&parsed,&line,&error)<0) return 2;
    memcpy(&before,&parsed,sizeof(before));
    for (int i=0;i<(int)(sizeof(bad)/sizeof(bad[0]));++i) {
        if (toy_game_gameplay_parse(bad[i],strlen(bad[i]),&parsed,&line,&error)>=0 ||
            !error || line!=((i<2 || i==11)?2:1) ||
            memcmp(&parsed,&before,sizeof(parsed))) return 3;
    }
    if (toy_game_gameplay_parse(embedded,sizeof(embedded)-1,&parsed,&line,&error)>=0 ||
        line!=1 || memcmp(&parsed,&before,sizeof(parsed))) return 4;
    toy_game_init(&g,800); toy_game_apply_gameplay_config(&g,&parsed);
    toy_game_init(&independent,800);
    if (memcmp(&independent.gameplay_config,&defaults,sizeof(defaults)) ||
        g.spawn_timer_ms!=32 || g.base_regen_timer_ms!=32) return 5;
    toy_game_update_world(&g,16);
    if (g.wave || g.spawn_timer_ms!=16) return 6;
    toy_game_update_world(&g,16);
    if (g.wave!=1 || g.campaign_phase!=TOY_GAME_PHASE_BUILDUP || g.phase_timer_ms!=16) return 7;
    independent.spawn_timer_ms=0;
    toy_game_update_world(&independent,16);
    if (independent.wave_attack_points!=g.wave_attack_points*2) return 8;
    g.campaign_phase=TOY_GAME_PHASE_HORDE;g.enemies_alive=0;g.to_spawn=0;
    toy_game_update_world(&g,16);
    if (g.spawn_timer_ms!=48 || g.campaign_phase!=TOY_GAME_PHASE_CALM) return 9;
    g.campaign_phase=TOY_GAME_PHASE_HORDE;g.wave=2;
    toy_game_update_world(&g,16);
    if (g.state!=TOY_GAME_WON) return 10;
    toy_game_init(&g,801);toy_game_apply_gameplay_config(&g,&parsed);
    g.primitives=&floor;g.primitive_count=1;g.room_limit=20000;g.external_director=1;
    a=toy_game_local_player_actor(&g);e=&g.enemies[0];
    e->active=1;e->type=TOY_GAME_ENEMY_PURSUIT_COMMON;e->hp=100;e->z=1200;
    g.enemies_alive=1;
    if (toy_game_shove_from_position(&g,0,0,0,1024)!=1 ||
        e->z!=1300 || e->shove_stun_ms!=321) return 11;
    e->type=TOY_GAME_ENEMY_CHARGER;e->z=1200;
    if (toy_game_shove_from_position(&g,0,0,0,1024)!=1 || e->shove_stun_ms!=123) return 12;
    a->slots[1].weapon=TOY_GAME_WEAPON_AXE;a->current_slot=1;
    if (!toy_game_actor_use_special(&g,a,0,1024) || e->hp!=95) return 13;
    a->current_slot=2;a->slots[2].weapon=TOY_GAME_WEAPON_BOMB;a->slots[2].mag=1;
    if (!toy_game_actor_throwable(&g,a,0,1024,0,1024,0) ||
        g.projectiles[0].vz!=5120) return 14;
    /* A landed bomb uses configured range/damage and a zero fuse. */
    g.projectiles[0].x=0;g.projectiles[0].z=1000;g.projectiles[0].y=0;
    g.projectiles[0].vx=g.projectiles[0].vz=g.projectiles[0].vy=0;
    e->z=1200;e->hp=100;
    toy_game_update_world(&g,16);
    if (g.projectiles[0].active || e->hp!=89 || a->hp!=a->max_hp) return 15;
    g.projectiles[0].active=1;g.projectiles[0].landed=0;
    g.projectiles[0].kind=TOY_GAME_WEAPON_MOLOTOV;g.projectiles[0].y=0;
    g.projectiles[0].owner_actor_id=a->actor_id;e->hp=100;
    toy_game_update_world(&g,16);
    if (!g.burn_zones[0].active || g.burn_zones[0].remaining_ms!=48 || e->hp!=97) return 16;
    base_id=toy_game_add_ai(&g,TOY_GAME_AI_LEVEL_1,5000,5000,"BASE");
    if (base_id<1) return 17;
    g.base_actor_index=base_id-1;
    g.actors[base_id-1].base_core=1;g.actors[base_id-1].hp=580;
    g.actors[base_id-1].max_hp=600;g.base_regen_timer_ms=32;
    toy_game_update_world(&g,16);
    if (g.actors[base_id-1].hp!=580) return 18;
    toy_game_update_world(&g,16);
    if (g.actors[base_id-1].hp!=581) return 19;
    toy_game_actor_reset_combat(a,1);
    attack.kind=TOY_GAME_ATTACK_BULLET;
    attack.base_damage_milli=attack.health_damage_milli=10000;
    result=toy_game_damage_actor(&g,NULL,a,&attack);
    if (result.evasion_spent_milli!=10000 || a->evasion.window_ms!=200 ||
        a->evasion.retrigger_ms!=700 || a->evasion.pressure_ms!=100) return 20;
    result=toy_game_damage_actor(&g,NULL,a,&attack);
    if (result.evasion_spent_milli!=2500) return 21;
    attack.kind=TOY_GAME_ATTACK_MELEE;
    result=toy_game_damage_actor(&g,NULL,a,&attack);
    if (result.evasion_spent_milli!=10000) return 22;
    if (toy_game_actor_use_special(NULL,a,0,1024)) return 23;
    memset(&session,0,sizeof(session));
    memcpy(&session.gameplay_config,&parsed,sizeof(parsed));
    for (int i=0;i<2;++i) {
        if (rasterfall_session_load(&session,i==0 ?
            "rasterfall/assets/maps/performance_empty.map" :
            "rasterfall/assets/maps/rasterfall.map")<0) return 24;
        rasterfall_session_reset(&session,&camera,802);
        if (memcmp(&session.game_state.gameplay_config,&parsed,sizeof(parsed)) ||
            session.game_state.spawn_timer_ms!=32 || session.game_state.base_regen_timer_ms!=32) {
            rasterfall_session_unload(&session);return 25;
        }
        if (i==1 && (session.game_state.base_actor_index<0 ||
            session.game_state.actors[session.game_state.base_actor_index].hp!=600 ||
            session.game_state.actors[session.game_state.base_actor_index].max_hp!=600)) {
            rasterfall_session_unload(&session);return 26;
        }
    }
    rasterfall_session_unload(&session);
    return 0;
}
