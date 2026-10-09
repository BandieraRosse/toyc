/* AI observes the same actor lifecycle and weapon rules as human actors. */
static struct toy_game_actor *combat_ai_fixture(struct toy_game *g)
{
    static struct toy_map_primitive floor;
    struct toy_game_actor *shooter;
    int id;
    toy_game_init(g, 9204);
    g->to_spawn = g->safe_room_count = 0;
    memset(&floor, 0, sizeof(floor));
    floor.shape = TOY_MAP_PRIMITIVE_FLAT;
    floor.flags = TOY_MAP_PRIMITIVE_COLLISION | TOY_MAP_PRIMITIVE_WALKABLE;
    floor.minx = floor.minz = -20000;
    floor.maxx = floor.maxz = 20000;
    toy_game_set_primitives(g, &floor, 1, 20000);
    g->actors[0].x = 0; g->actors[0].z = 3500;
    g->actors[0].hp = g->actors[0].max_hp = 5000;
    id = toy_game_add_gunner(g, 1, 0, 0, "AI CONTRACT");
    shooter = toy_game_actor_by_id(g, id);
    if (!shooter) return NULL;
    toy_game_actor_equip_weapon(g, shooter, TOY_GAME_WEAPON_AK);
    shooter->weapon_switch_timer_ms = 0;
    shooter->sy = 0; shooter->cy = 1024;
    return shooter;
}

static int combat_rescue_logic_test(void)
{
    static struct toy_game g;
    struct toy_game_actor *rescuer, *player, *peer;
    int i, id, index;
    unsigned int generation;
    rescuer = combat_ai_fixture(&g);
    if (!rescuer) return 1;
    rescuer->faction = TOY_GAME_FACTION_ALLIED;
    rescuer->fire_enabled = 0;
    index = (int)(rescuer - g.actors);
    player = &g.actors[0]; player->hp = 0; player->state = TOY_GAME_ACTOR_DOWNED;
    id = toy_game_add_ai(&g, TOY_GAME_AI_LEVEL_2, -500, 0, "RESCUE PEER");
    peer = toy_game_actor_by_id(&g, id);
    if (!peer) return 2;
    peer->fire_enabled = 0;
    for (i = 0; i < 700 && player->state == TOY_GAME_ACTOR_DOWNED; i++) {
        toy_game_update_ai_teammates(&g, 16);
        if (rescuer->ai_rescue_active && peer->ai_rescue_active &&
            rescuer->ai_rescue_index == peer->ai_rescue_index) return 3;
    }
    if (player->state != TOY_GAME_ACTOR_ALIVE || player->hp <= 0 ||
        player->revive_progress_ms) return 4;
    /* A new explicit move interrupts aid. A completed command may permit
     * nearby aid in place, without changing or leaving the commanded post. */
    player->hp = 0; player->state = TOY_GAME_ACTOR_DOWNED;
    peer->active = 0; rescuer->x = player->x; rescuer->z = player->z;
    toy_game_update_ai_teammates(&g, 16);
    if (!rescuer->ai_rescue_active || player->revive_progress_ms != 16) return 5;
    toy_game_actor_cancel_rescue(&g, index);
    rescuer->command_destination_active = 1;
    rescuer->command_x = 5000; rescuer->command_z = 3500;
    toy_game_update_ai_teammates(&g, 16);
    if (rescuer->ai_rescue_active || player->revive_progress_ms ||
        rescuer->x <= player->x) return 6;
    rescuer->x = 0; rescuer->z = 3500;
    rescuer->command_x = 0;
    for (i = 0; i < TOY_GAME_REVIVE_MS / 16 + 2; i++)
        toy_game_update_ai_teammates(&g, 16);
    if (player->state != TOY_GAME_ACTOR_ALIVE || rescuer->x ||
        rescuer->z != 3500 || rescuer->command_x ||
        !rescuer->command_destination_active) return 7;

    player->hp = 0; player->state = TOY_GAME_ACTOR_DOWNED;
    toy_game_update_ai_teammates(&g, 16);
    generation = player->combat_generation;
    player->combat_generation++;
    player->revive_progress_ms = 900;
    toy_game_update_ai_teammates(&g, 16);
    if (!rescuer->ai_rescue_active || rescuer->ai_rescue_generation == generation ||
        player->revive_progress_ms != 16) return 8;
    rescuer->damage_flash_ms = 100;
    toy_game_update_ai_teammates(&g, 16);
    if (rescuer->ai_rescue_active || player->revive_progress_ms) return 9;
    rescuer->damage_flash_ms = 0;
    id = toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_COMMON, 0, 3900);
    if (id < 0) return 10;
    toy_game_update_ai_teammates(&g, 16);
    if (rescuer->ai_rescue_active || player->revive_progress_ms) return 11;
    g.enemies[id].active = 0;
    rescuer->faction = TOY_GAME_FACTION_HOSTILE;
    for (i = 0; i < 30; i++) toy_game_update_ai_teammates(&g, 16);
    if (rescuer->ai_rescue_active || player->revive_progress_ms ||
        player->state != TOY_GAME_ACTOR_DOWNED) return 12;

    /* Both legacy held and normal world paths honor local task ownership. */
    for (i = 0; i < 2; i++) {
        toy_game_init(&g, 4921);
        g.external_director = 1;
        g.to_spawn = 12; g.spawn_timer_ms = 0;
        if (i) toy_game_update_world(&g, 16);
        else toy_game_update_held(&g, NULL, 0, 0, 0, 1024, 16);
        if (g.enemies_alive || g.to_spawn != 12 || g.wave != 0) return 13;
    }
    __printf("COMBAT rescue single-owner/player/command/identity/injury/threat and external-director passed\n");
    return 0;
}

static int actor_navigation_cache_logic_test(void)
{
    static struct toy_game g, reference;
    struct toy_game_actor *a, *b;
    struct toy_game_actor ca, cb;
    struct toy_game_actor_command command = {0};
    static struct toy_game_update_profile profile;
    struct toy_map_primitive world[2] = {{0}};
    unsigned int generation, queries;
    int i, id, index;
    memset(&profile,0,sizeof(profile));
    toy_game_init(&g, 9611);g.external_director=1;g.actors[0].active=0;
    world[0].shape=TOY_MAP_PRIMITIVE_FLAT;
    world[0].flags=TOY_MAP_PRIMITIVE_COLLISION|TOY_MAP_PRIMITIVE_WALKABLE;
    world[0].minx=world[0].minz=-10000;world[0].maxx=world[0].maxz=10000;
    toy_game_set_primitives(&g,world,2,10000);
    id=toy_game_add_ai(&g,TOY_GAME_AI_LEVEL_2,0,0,"CACHE CONTRACT");
    a=toy_game_actor_by_id(&g,id);if(!a)return 1;
    index=(int)(a-g.actors);
    command.switch_slot=-1;command.move_target_active=1;
    command.move_x=6000;command.move_z=0;command.move_speed=64;
    a->fire_cooldown_ms=77;
    memcpy(&ca,a,sizeof(ca));
    unsigned long long before_rng=g.rng;
    int before_events=g.event_count,before_time=g.combat_time_ms,steer_x=0,steer_z=0;
    if(!toy_game_actor_navigation_target(&g,a,6000,0,64,16,&steer_x,&steer_z) ||
        steer_x!=6000 || steer_z!=0)return 12;
    memcpy(&cb,a,sizeof(cb));
    ca.nav_x=cb.nav_x;ca.nav_z=cb.nav_z;ca.nav_active=cb.nav_active;
    ca.nav_direct_valid=cb.nav_direct_valid;ca.nav_direct_ms=cb.nav_direct_ms;
    ca.nav_goal_x=cb.nav_goal_x;ca.nav_goal_z=cb.nav_goal_z;
    ca.nav_direct_y=cb.nav_direct_y;ca.nav_generation=cb.nav_generation;
    ca.nav_layer_serial=cb.nav_layer_serial;
    if(memcmp(&ca,&cb,sizeof(ca)) || g.rng!=before_rng ||
        g.event_count!=before_events || g.combat_time_ms!=before_time)return 13;
    if(toy_game_actor_navigation_target(&g,a,2000001,0,64,16,&steer_x,&steer_z) ||
        toy_game_actor_navigation_target(&g,a,6000,0,64,-1,&steer_x,&steer_z))return 14;
    toy_game_actor_cancel_navigation(a);
    memcpy(&reference,&g,sizeof(g));b=&reference.actors[index];
    g.update_profile=&profile;
    /* Reference rescans each step. Cache affects planning work, never the
     * resulting actor motion, inventory, random stream or gameplay events. */
    for(i=0;i<17;i++) {
        b->nav_direct_valid=0;
        toy_game_execute_actor_command(&g,a,&command,16,100);
        toy_game_execute_actor_command(&reference,b,&command,16,100);
        memcpy(&ca,a,sizeof(ca));memcpy(&cb,b,sizeof(cb));
        ca.nav_direct_valid=cb.nav_direct_valid=0;
        ca.nav_direct_ms=cb.nav_direct_ms=0;
        if(memcmp(&ca,&cb,sizeof(ca)) || g.rng!=reference.rng ||
            g.event_count!=reference.event_count) return 2;
    }
    if(profile.actor_direct_queries!=2 || profile.actor_direct_hits!=15 ||
        !a->nav_direct_valid) return 3;
    queries=profile.actor_direct_queries;
    command.move_z=1000;
    toy_game_execute_actor_command(&g,a,&command,16,100);
    if(profile.actor_direct_queries!=queries+1)return 4;
    queries=profile.actor_direct_queries;a->ground_y=100;
    toy_game_execute_actor_command(&g,a,&command,16,100);
    if(profile.actor_direct_queries!=queries+1)return 5;
    generation=g.navigation_generation;
    toy_game_rebuild_navigation(&g);
    if(g.navigation_generation==generation || a->nav_active || a->nav_direct_valid)return 6;
    command.move_z=a->z;
    toy_game_execute_actor_command(&g,a,&command,16,100);
    if(!a->nav_direct_valid)return 7;
    /* An obstacle closes while the positive plan is cached. Ordinary body
     * collision still rejects the next step and invalidates that plan. */
    world[1].shape=TOY_MAP_PRIMITIVE_BOX;world[1].flags=TOY_MAP_PRIMITIVE_COLLISION;
    world[1].minx=a->x+TOY_GAME_PLAYER_RADIUS+20;world[1].maxx=world[1].minx+500;
    world[1].minz=-2500;world[1].maxz=2500;
    world[1].surface_y0=world[1].surface_y1=1500;
    toy_game_execute_actor_command(&g,a,&command,16,100);
    if(a->nav_direct_valid || !profile.actor_direct_blocked ||
        a->x>world[1].minx-TOY_GAME_PLAYER_RADIUS)return 8;
    a->nav_active=1;a->nav_direct_valid=1;a->nav_direct_ms=256;
    toy_game_actor_cancel_navigation(a);
    command.move_x=a->x-3000;command.move_z=a->z;
    i=a->x;
    toy_game_execute_actor_command(&g,a,&command,16,100);
    if(a->x>=i || a->nav_active || a->nav_goal_x!=command.move_x)return 9;
    /* Waiting at an RTS post consumes the same fixed-time lease; resuming
     * movement cannot retain a plan from before that pause. */
    a->command_destination_active=1;a->command_x=a->x;a->command_z=a->z;
    for(i=0;i<17;i++)toy_game_update_ai_teammates(&g,16);
    if(a->nav_direct_ms)return 10;
    queries=profile.actor_direct_queries;
    toy_game_execute_actor_command(&g,a,&command,16,100);
    if(profile.actor_direct_queries!=queries+1)return 11;
    g.update_profile=NULL;
    __printf("ACTOR-NAV cache fixed-time/reference-motion/goal/height/rebuild/blocked/new-order passed\n");
    return 0;
}

static int actor_endpoint_navigation_logic_test(void)
{
    static struct toy_game g;
    struct toy_map_primitive world[6] = {{0}};
    int indices[5], door, i, n;
    toy_game_init(&g, 7419);g.external_director=1;g.actors[0].active=0;
    world[0].shape=TOY_MAP_PRIMITIVE_FLAT;
    world[0].flags=TOY_MAP_PRIMITIVE_COLLISION|TOY_MAP_PRIMITIVE_WALKABLE;
    world[0].minx=world[0].minz=-50000;world[0].maxx=world[0].maxz=50000;
    toy_game_set_primitives(&g,world,6,50000);
    /* Generic opening aligned to a graph column. Crates leave actual bodies
     * room at both endpoints but block their conservative whole cells. */
    door=g.nav_origin+((-g.nav_origin)/g.nav_cell_size)*g.nav_cell_size+g.nav_cell_size/2;
    for(i=1;i<6;i++) {
        world[i].shape=TOY_MAP_PRIMITIVE_BOX;world[i].flags=TOY_MAP_PRIMITIVE_COLLISION;
        world[i].surface_y0=world[i].surface_y1=2000;
    }
    world[1].minx=-50000;world[1].maxx=door-1200;
    world[2].minx=door+1200;world[2].maxx=50000;
    world[1].minz=world[2].minz=-100;world[1].maxz=world[2].maxz=100;
    world[3].minx=door-3900;world[3].maxx=door-3300;
    world[3].minz=-6500;world[3].maxz=-3500;
    world[4].minx=door+1250;world[4].maxx=door+1850;
    world[4].minz=3500;world[4].maxz=6500;
    world[5].minx=door-1200;world[5].maxx=door+1200;
    world[5].minz=-100;world[5].maxz=100;world[5].flags=0;
    toy_game_rebuild_navigation(&g);
    for(n=0;n<5;n++) {
        /* Distinct body-sized slots keep the endpoint contract physically
         * possible now that actors cannot occupy one another's positions. */
        int id=toy_game_add_ai(&g,TOY_GAME_AI_LEVEL_2,door-4500+n*50,-5800+n*400,"ENDPOINT CONTRACT");
        struct toy_game_actor *a=toy_game_actor_by_id(&g,id);
        if(!a)return 1;
        indices[n]=(int)(a-g.actors);a->fire_enabled=0;
        a->command_destination_active=1;a->command_x=door+650+n*50;a->command_z=4200+n*400;
        i=(a->z-g.nav_origin)/g.nav_cell_size*g.nav_width+(a->x-g.nav_origin)/g.nav_cell_size;
        if(g.nav_walkable[i] || toy_game_position_blocked_at_height(&g,a->x,a->z,TOY_GAME_PLAYER_RADIUS,0))return 2;
        i=(a->command_z-g.nav_origin)/g.nav_cell_size*g.nav_width+(a->command_x-g.nav_origin)/g.nav_cell_size;
        if(g.nav_walkable[i] || toy_game_position_blocked_at_height(&g,a->command_x,a->command_z,TOY_GAME_PLAYER_RADIUS,0))return 3;
    }
    for(i=0;i<900;i++) {
        toy_game_update_world(&g,16);
        for(n=0;n<5;n++) {
            struct toy_game_actor *a=&g.actors[indices[n]];
            if(toy_game_position_blocked_at_height(&g,a->x,a->z,TOY_GAME_PLAYER_RADIUS,0))return 4;
            if(a->z>-280 && a->z<280 && (a->x<door-1020 || a->x>door+1020))return 5;
        }
    }
    for(n=0;n<5;n++) {
        struct toy_game_actor *a=&g.actors[indices[n]];
        long long dx=(long long)a->x-a->command_x,dz=(long long)a->z-a->command_z;
        if(dx*dx+dz*dz>200LL*200 || !a->command_destination_active)return 6;
        toy_game_actor_cancel_navigation(a);a->command_x=door-4500+n*50;a->command_z=-5800+n*400;
    }
    world[5].flags=TOY_MAP_PRIMITIVE_COLLISION;toy_game_rebuild_navigation(&g);
    for(i=0;i<300;i++) {
        toy_game_update_world(&g,16);
        for(n=0;n<5;n++)if(g.actors[indices[n]].z<280)return 7;
    }
    world[5].flags=0;toy_game_rebuild_navigation(&g);
    for(i=0;i<900;i++)toy_game_update_world(&g,16);
    for(n=0;n<5;n++) {
        struct toy_game_actor *a=&g.actors[indices[n]];
        long long dx=(long long)a->x-a->command_x,dz=(long long)a->z-a->command_z;
        if(dx*dx+dz*dz>200LL*200) {
            __fprintf(2,"ACTOR-NAV return member=%d pos=%d,%d goal=%d,%d waypoint=%d,%d active=%d\n",
                n,a->x,a->z,a->command_x,a->command_z,a->nav_x,a->nav_z,a->nav_active);
            return 8;
        }
    }
    __printf("ACTOR-NAV conservative-endpoints/five-actors/narrow-door/closed-wall/reopen passed\n");
    return 0;
}

static int combat_target_retention_logic_test(void)
{
    static struct toy_game g;
    struct toy_game_actor *shooter, *targets[2];
    struct toy_game_combat_target nearest;
    struct toy_map_primitive world[2] = {{0}};
    int i, id, near, switches = 0, previous, index;
    unsigned int sequence, generation;
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 1;
    g.actors[0].active = 0;
    shooter->faction = TOY_GAME_FACTION_ALLIED;
    shooter->skills.mastery[TOY_GAME_SKILL_PRECISION] = 0;
    shooter->command_destination_active = 1;
    shooter->command_x = shooter->command_z = 0;
    for (i = 0; i < 2; i++) {
        id = toy_game_add_gunner(&g, 0, i ? 100 : -100, 3000, "FOCUS CONTRACT");
        targets[i] = toy_game_actor_by_id(&g, id);
        if (!targets[i]) return 2;
        targets[i]->hit_test_dummy = 1; targets[i]->fire_enabled = 0;
        targets[i]->hp = targets[i]->max_hp = 5000;
    }
    previous = -1;
    /* Lv2 ordinary needs 300ms aim. Two continuously visible targets trade
     * nearest distance each 256ms scan, without leaving its aiming cone. */
    for (i = 0; i < 96; i++) {
        near = (i / 16) % 2;
        targets[near]->z = 2960; targets[1 - near]->z = 3040;
        if (!toy_game_find_combat_target(&g, shooter, &nearest) ||
            nearest.index != (int)(targets[near] - g.actors)) return 3;
        toy_game_update_ai_teammates(&g, 16);
        if (shooter->combat_target.index != previous) switches++;
        previous = shooter->combat_target.index;
    }
    if (switches != 1 || !shooter->fire_seq || shooter->combat_aim_ms < 300 ||
        shooter->x || shooter->z || !shooter->command_destination_active) return 4;
    /* A substantially closer threat overrides the retained target on the
     * same scan. Its fresh position is cached, rather than an old aim point. */
    near = targets[0] - g.actors == shooter->combat_target.index ? 0 : 1;
    targets[1 - near]->z = 1800; shooter->combat_scan_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.index != (int)(targets[1 - near] - g.actors) ||
        shooter->combat_target.z != 1800) return 5;
    /* A dead identity clears at once, even between sensory scans. */
    targets[1 - near]->hp = 0; targets[1 - near]->state = TOY_GAME_ACTOR_DEAD;
    shooter->combat_scan_ms = 200; sequence = shooter->fire_seq;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->combat_aim_ms ||
        shooter->fire_seq != sequence) return 6;
    index = (int)(targets[1 - near] - g.actors);
    generation = targets[1 - near]->combat_generation;
    targets[1 - near]->active = 0;
    id = toy_game_add_gunner(&g, 0, 100, 1800, "FOCUS REPLACEMENT");
    targets[1 - near] = toy_game_actor_by_id(&g, id);
    if (!targets[1 - near] || targets[1 - near] != &g.actors[index] ||
        targets[1 - near]->combat_generation == generation) return 7;
    targets[1 - near]->hit_test_dummy = 1; targets[1 - near]->fire_enabled = 0;
    shooter->combat_scan_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.index != index ||
        shooter->combat_target.generation != (int)targets[1 - near]->combat_generation ||
        shooter->combat_aim_ms >= 300) return 8;
    /* A biased target cannot extend physical alert range. */
    targets[1 - near]->x = 0;
    targets[1 - near]->z = toy_game_weapon_info(TOY_GAME_WEAPON_AK)->alert_range + 1;
    targets[near]->z = toy_game_weapon_info(TOY_GAME_WEAPON_AK)->alert_range - 1;
    shooter->combat_scan_ms = 0; toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.index != (int)(targets[near] - g.actors)) return 9;
    /* Closing LOS drops the retained target despite its better score. */
    world[0] = g.primitives[0];
    world[1].shape = TOY_MAP_PRIMITIVE_BOX;
    world[1].flags = TOY_MAP_PRIMITIVE_COLLISION;
    world[1].minx = -250; world[1].maxx = 250;
    world[1].minz = 1000; world[1].maxz = 1200;
    world[1].surface_y0 = world[1].surface_y1 = 2000;
    targets[near]->x = 0; targets[near]->z = 3000;
    targets[1 - near]->x = 1800; targets[1 - near]->z = 2600;
    toy_game_set_primitives(&g, world, 2, 20000);
    shooter->combat_scan_ms = 0; toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.index != (int)(targets[1 - near] - g.actors)) return 10;
    targets[1 - near]->faction = TOY_GAME_FACTION_ALLIED;
    shooter->combat_scan_ms = 200; toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->combat_aim_ms) return 18;
    /* Hostile guards apply the same commitment only inside their guard area;
     * infected lifetimes are checked just like human actor lifetimes. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 11;
    shooter->fire_enabled = 0; index = (int)(shooter - g.actors);
    g.actors[0].z = 3000;
    if (!toy_game_actor_set_guard(&g, index, 0, 0, 4000)) return 12;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != TOY_GAME_ENTITY_ACTOR) return 13;
    id = toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_COMMON, 0, 3100);
    if (id < 0) return 14;
    g.actors[0].z = 4500; shooter->combat_scan_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != TOY_GAME_ENTITY_ENEMY ||
        shooter->combat_target.index != id) return 15;
    generation = g.enemies[id].combat_generation;
    g.enemies[id].active = 0; g.enemies_alive--;
    if (toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_COMMON, 0, 3100) != id ||
        g.enemies[id].combat_generation == generation) return 16;
    shooter->combat_scan_ms = 200; toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->combat_aim_ms) return 17;
    __printf("COMBAT target-retention aim/fire/nearest/closer/death/lifetime/LOS/range/guard/faction/RTS passed\n");
    return 0;
}

static int gunner_hold_logic_test(void)
{
    static struct toy_game g;
    struct toy_game_actor *a;
    int weapon, tick, z, previous, settled, band;
    for (weapon = TOY_GAME_WEAPON_PISTOL; weapon <= TOY_GAME_WEAPON_AWP; weapon++) {
        a = combat_ai_fixture(&g);
        if (!a) return 1;
        g.external_director = 1; a->fire_enabled = 0;
        toy_game_actor_equip_weapon(&g, a, weapon);
        a->weapon_switch_timer_ms = 0;
        band = toy_game_weapon_info(weapon)->range / 3;
        if (band > 6000) band = 6000;
        if (band < 2400) band = 2400;
        g.actors[0].z = band + 850;
        if (!toy_game_actor_set_guard(&g, (int)(a - g.actors), 0, 0, 8500)) return 2;
        for (tick = 0; tick < 300; tick++) toy_game_update_world(&g, 16);
        settled = a->z;
        if (settled <= 0 || g.actors[0].z - settled > band ||
            a->combat_target.kind != TOY_GAME_ENTITY_ACTOR) return 3;
        /* Every firearm pursues into its own existing hold band, then stays
         * still instead of undoing pursuit toward its deployment post. */
        for (tick = 0; tick < 64; tick++) {
            toy_game_update_world(&g, 16);
            if (a->z != settled || a->x || a->moving) return 4;
        }
        previous = a->z; z = g.actors[0].z;
        for (tick = 0; tick < 192; tick++) {
            g.actors[0].z = z + ((tick / 32) % 2 ? 200 : -200);
            toy_game_update_world(&g, 16);
            if (a->z < previous) return 5;
            previous = a->z;
        }
        /* Near contact still retreats, then holds instead of walking back
         * into contact. No pursuit/retreat state or target lease is added. */
        g.actors[0].z = a->z + 1500; a->combat_scan_ms = 0;
        settled = a->z;
        for (tick = 0; tick < 96; tick++) toy_game_update_world(&g, 16);
        if (a->z >= settled || g.actors[0].z - a->z < 1800) return 6;
        settled = a->z;
        for (tick = 0; tick < 64; tick++) {
            toy_game_update_world(&g, 16);
            if (a->z != settled) return 7;
        }
        /* Identity loss retains only the bounded last-seen chase, then
         * returns normally to deployment while respecting the guard area. */
        g.actors[0].active = 0;
        for (tick = 0; tick < 700; tick++) {
            toy_game_update_world(&g, 16);
            if ((long long)a->x * a->x + (long long)a->z * a->z > 8500LL * 8500)
                return 8;
        }
        if (a->combat_target.kind >= 0 || a->combat_lost_ms ||
            (long long)a->x * a->x + (long long)a->z * a->z > 200LL * 200) return 9;
    }
    __printf("COMBAT gunner-hold weapon-bands/stable-pursuit/400RFU-motion/retreat/lost-target/guard passed\n");
    return 0;
}

static int combat_ai_logic_test(void)
{
    static struct toy_game g;
    struct toy_game_actor *shooter, *target, *peer, peer_before;
    struct toy_game_capabilities caps;
    struct toy_game_attack fatal = {0};
    struct toy_map_primitive wall = {0};
    struct toy_map_primitive obstruction[2] = {{0}};
    unsigned int sequence, generation;
    int i, elapsed, id, old_index;
    /* Exercise actual infected displacement, not just the math helper. */
    for(int speed_case=0;speed_case<3;++speed_case) {
        const int speeds[3]={83,137,450};
        shooter=combat_ai_fixture(&g);
        if(!shooter)return 63;
        shooter->active=0;
        id=toy_game_spawn_enemy(&g,TOY_GAME_ENEMY_PURSUIT_COMMON,300,-190);
        if(id<0)return 64;
        struct toy_game_enemy *enemy=&g.enemies[id];
        enemy->speed=speeds[speed_case];
        int old_x=enemy->x,old_z=enemy->z;
        for(i=0;i<100;++i) {
            toy_game_update_world(&g,16);
            long long norm=(long long)enemy->dir_x*enemy->dir_x+(long long)enemy->dir_z*enemy->dir_z;
            if(norm<1022LL*1022 || norm>1026LL*1026)return 65;
        }
        if(enemy->x==old_x && enemy->z==old_z)return 66;
    }
    /* Short per-tick movement deltas must still produce a unit facing vector.
     * A floored world-space distance used to stretch the actor and its gear. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 60;
    g.actors[0].active=0;
    shooter->ai_assault_active=1;
    shooter->deployment_x=6000;shooter->deployment_z=-3800;
    for(i=0;i<300;++i) {
        toy_game_update_ai_teammates(&g,16);
        long long length2=(long long)shooter->sy*shooter->sy+(long long)shooter->cy*shooter->cy;
        if(length2<1022LL*1022 || length2>1026LL*1026) {
            __printf("COMBAT facing non-unit tick=%d facing=(%d,%d)\n",i,shooter->sy,shooter->cy);
            return 61;
        }
    }
    if(shooter->x<1000 || shooter->z>-500)return 62;
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 1;
    target = &g.actors[0];
    toy_game_actor_capabilities(shooter, TOY_GAME_WEAPON_AK, &caps);
    /* Already facing a visible opponent still requires the full aim interval. */
    elapsed = 0;
    while (elapsed + 16 < caps.aim_ms) {
        toy_game_update_ai_teammates(&g, 16);
        elapsed += 16;
        if (shooter->fire_seq || shooter->combat_stats.shots) return 2;
    }
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != TOY_GAME_ENTITY_ACTOR ||
        shooter->combat_target.index != TOY_GAME_PLAYER_ACTOR_INDEX ||
        shooter->combat_target.generation != (int)target->combat_generation ||
        shooter->fire_seq != 1 || shooter->combat_stats.shots != 1 ||
        shooter->combat_aim_ms < caps.aim_ms) return 3;

    /* Visibility is checked even between budgeted scans. Closing a wall must
     * stop the very next shot; readiness survives only the short LOS grace. */
    sequence = shooter->fire_seq;
    wall.shape = TOY_MAP_PRIMITIVE_BOX;
    wall.flags = TOY_MAP_PRIMITIVE_COLLISION;
    wall.minx = -1500; wall.maxx = 1500;
    wall.minz = 1500; wall.maxz = 1800;
    wall.base_y = 0; wall.surface_y0 = wall.surface_y1 = 1600;
    obstruction[0].shape = TOY_MAP_PRIMITIVE_FLAT;
    obstruction[0].flags = TOY_MAP_PRIMITIVE_COLLISION | TOY_MAP_PRIMITIVE_WALKABLE;
    obstruction[0].minx = obstruction[0].minz = -20000;
    obstruction[0].maxx = obstruction[0].maxz = 20000;
    obstruction[1] = wall;
    toy_game_set_primitives(&g, obstruction, 2, 20000);
    shooter->fire_cooldown_ms = 0;
    shooter->combat_scan_ms = 200;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->fire_seq != sequence || !shooter->combat_aim_ms) return 4;
    for (i = 0; i < 13; i++) toy_game_update_ai_teammates(&g, 16);
    if (shooter->fire_seq != sequence || shooter->combat_aim_ms) return 4;
    for (i = 0; i < 40; i++) toy_game_update_ai_teammates(&g, 16);
    if (shooter->fire_seq != sequence || shooter->combat_target.kind != -1)
        return 5;
    /* A dying opponent becomes invalid before the next shot, even with a
     * ready weapon and a cached target whose next scan has not come due. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 6;
    target = &g.actors[0];
    toy_game_update_ai_teammates(&g, 16);
    sequence = shooter->fire_seq;
    shooter->combat_aim_ms = caps.aim_ms;
    shooter->combat_scan_ms = 200;
    shooter->fire_cooldown_ms = 0;
    fatal.kind = TOY_GAME_ATTACK_CONTROL;
    fatal.base_damage_milli = fatal.health_damage_milli = target->hp * 1000;
    toy_game_damage_actor(&g, shooter, target, &fatal);
    toy_game_update_ai_teammates(&g, 16);
    if (target->state != TOY_GAME_ACTOR_DOWNED ||
        shooter->combat_target.kind != -1 || shooter->combat_aim_ms ||
        shooter->fire_seq != sequence) return 7;

    /* Reusing a live target's slot with another generation invalidates the
     * cached reference, then requires a fresh perception/aim cycle. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 8;
    target = &g.actors[0];
    toy_game_update_ai_teammates(&g, 16);
    sequence = shooter->fire_seq;
    generation = target->combat_generation;
    if (shooter->combat_target.generation != (int)generation) return 8;
    target->combat_generation++;
    toy_game_actor_reset_combat(target, 1);
    shooter->combat_scan_ms = 200;
    shooter->combat_aim_ms = caps.aim_ms;
    shooter->fire_cooldown_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->combat_aim_ms ||
        shooter->fire_seq != sequence) return 9;
    shooter->combat_scan_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.generation != (int)target->combat_generation ||
        shooter->combat_aim_ms >= caps.aim_ms || shooter->fire_seq != sequence)
        return 10;
    target->active = 0;
    shooter->combat_scan_ms = 200;
    shooter->combat_aim_ms = caps.aim_ms;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->fire_seq != sequence)
        return 11;

    /* One gunner's death and later slot reuse must not reset its peer's
     * independent cooldown, aim, reserve, target or presentation sequence. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 12;
    id = toy_game_add_gunner(&g, 1, 600, 0, "PEER");
    peer = toy_game_actor_by_id(&g, id);
    if (!peer) return 13;
    for (i = 0; i < 40; i++) toy_game_update_ai_teammates(&g, 16);
    peer_before = *peer;
    old_index = (int)(shooter - g.actors);
    generation = shooter->combat_generation;
    shooter->evasion.window_ms = shooter->evasion.animation_ms = 200;
    fatal.base_damage_milli = fatal.health_damage_milli = shooter->hp * 1000;
    toy_game_damage_actor(&g, &g.actors[0], shooter, &fatal);
    if (shooter->state != TOY_GAME_ACTOR_DEAD || shooter->hp ||
        shooter->evasion.reserve_milli || shooter->evasion.animation_ms ||
        shooter->combat_target.kind != -1 || memcmp(peer, &peer_before, sizeof(*peer)))
        return 14;
    sequence = shooter->fire_seq;
    g.ai_context_actor_index = old_index;
    toy_game_update_ai_teammate(&g, 16);
    if (shooter->fire_seq != sequence || shooter->animation.id != TOY_GAME_ANIM_DEATH)
        return 15;
    shooter->active = 0;
    id = toy_game_add_gunner(&g, 1, 0, 0, "REPLACEMENT");
    shooter = toy_game_actor_by_id(&g, id);
    if (!shooter || shooter != &g.actors[old_index] ||
        shooter->combat_generation == generation ||
        shooter->state != TOY_GAME_ACTOR_ALIVE || shooter->fire_seq ||
        shooter->combat_stats.shots || shooter->combat_target.kind != -1 ||
        shooter->combat_aim_ms || shooter->evasion.sequence ||
        shooter->rays[0].actor_index != -1 ||
        memcmp(peer, &peer_before, sizeof(*peer))) return 16;
    /* Physical impulse outlives AI intent. Death during flight must finish
     * falling, retain DEATH, and never fire; surviving landings regain control. */
    for (int dies = 0; dies <= 1; dies++) {
        shooter = combat_ai_fixture(&g);
        if (!shooter) return 17;
        old_index = (int)(shooter - g.actors);
        shooter->fire_enabled = 0;
        if (!toy_game_apply_entity_impact(&g, TOY_GAME_ENTITY_ACTOR,
                                          old_index, 1024, 0, 1)) return 18;
        for (i = 0; i < 8; i++) toy_game_update_ai_teammates(&g, 16);
        if (shooter->airborne_y <= 0 || !shooter->control_disabled) return 19;
        if (dies) {
            fatal.base_damage_milli = fatal.health_damage_milli = shooter->hp * 1000;
            toy_game_damage_actor(&g, &g.actors[0], shooter, &fatal);
        }
        sequence = shooter->fire_seq;
        for (i = 0; i < 180 && shooter->airborne_ms > 0; i++)
            toy_game_update_ai_teammates(&g, 16);
        if (shooter->airborne_ms || shooter->airborne_y || shooter->vertical_velocity ||
            shooter->knockback_x || shooter->knockback_z || shooter->control_disabled ||
            shooter->fire_seq != sequence || (dies &&
                (shooter->state != TOY_GAME_ACTOR_DEAD ||
                 shooter->animation.id != TOY_GAME_ANIM_DEATH))) return 20;
    }
    /* Both AI and human rescue can complete before a lethal impulse lands.
     * Revive owns clearing that old impulse and any remaining control state. */
    for (int human = 0; human <= 1; human++) {
        shooter = combat_ai_fixture(&g);
        if (!shooter) return 21;
        if (human) shooter = &g.actors[0];
        old_index = (int)(shooter - g.actors);
        shooter->faction = TOY_GAME_FACTION_ALLIED;
        shooter->max_hp = human ? 160 : 80;
        shooter->hp = 5;
        if (!toy_game_apply_entity_impact(&g, TOY_GAME_ENTITY_ACTOR,
                                          old_index, 0, 1024, 5) ||
            shooter->state != TOY_GAME_ACTOR_DOWNED || !shooter->airborne_ms)
            return 22;
        shooter->airborne_y = 700;
        shooter->air_x = 17; shooter->air_z = 23;
        if (!toy_game_revive_actor(&g, old_index, TOY_GAME_REVIVE_MS) ||
            shooter->state != TOY_GAME_ACTOR_ALIVE ||
            shooter->hp != (human ? TOY_GAME_REVIVE_HP : 80) ||
            shooter->animation.id != TOY_GAME_ANIM_REVIVE ||
            shooter->control_disabled || shooter->special_control ||
            shooter->airborne_ms || shooter->airborne_y || shooter->vertical_velocity ||
            shooter->air_x || shooter->air_z || shooter->knockback_x ||
            shooter->knockback_z || shooter->knockback_cooldown_ms) return 23;
    }
    /* Ordinary AI receives the real weapon reserve and cannot reload after
     * consuming it. The formal pistol policy remains separate. */
    shooter = combat_ai_fixture(&g);
    if (!shooter || shooter->slots[shooter->current_slot].reserve !=
        toy_game_weapon_info(TOY_GAME_WEAPON_AK)->reserve_max) return 24;
    shooter->slots[shooter->current_slot].mag = 1;
    shooter->slots[shooter->current_slot].reserve = 2;
    if (!toy_game_actor_begin_fire(&g, shooter)) return 25;
    toy_game_update_actor_weapon_held(&g, shooter, NULL, 0, 0, 0, 1024,
                                     shooter->reload_timer_ms, 100);
    if (shooter->slots[shooter->current_slot].mag != 2 ||
        shooter->slots[shooter->current_slot].reserve || shooter->reloading) return 26;
    for (i = 0; i < 2; i++) {
        shooter->fire_cooldown_ms = 0;
        if (!toy_game_actor_begin_fire(&g, shooter)) return 27;
    }
    shooter->fire_cooldown_ms = 0;
    sequence = shooter->fire_seq;
    if (toy_game_actor_begin_fire(&g, shooter) || shooter->reloading ||
        shooter->fire_seq != sequence) return 28;

    /* A post observes only its assigned region, then returns to that post;
     * assault units advance without requiring an enemy in sight. */
    shooter = combat_ai_fixture(&g);
    old_index = (int)(shooter - g.actors);
    if (!toy_game_actor_set_guard(&g, old_index, 0, 0, 2000)) return 29;
    for (i = 0; i < 30; i++) toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->fire_seq ||
        shooter->x || shooter->z) return 30;
    g.actors[0].z = 1500;
    shooter->combat_scan_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != TOY_GAME_ENTITY_ACTOR) return 31;
    g.actors[0].active = 0;
    shooter->combat_lost_ms = 0;
    if (!toy_game_actor_set_assault(&g, old_index, 6000, 0)) return 32;
    for (i = 0; i < 30; i++) toy_game_update_ai_teammates(&g, 16);
    if (shooter->x <= 0 || shooter->ai_guard_radius) return 33;

    /* Infected are hostile to both actor factions. Slot reuse changes their
     * lifetime and cannot preserve a gunner's cached enemy reference. */
    shooter = combat_ai_fixture(&g);
    shooter->fire_enabled = 0;
    old_index = (int)(shooter - g.actors);
    id = toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_COMMON, 0, 1000);
    if (id < 0 || !g.enemies[id].combat_generation) return 34;
    generation = g.enemies[id].combat_generation;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != TOY_GAME_ENTITY_ENEMY ||
        shooter->combat_target.generation != (int)generation) return 35;
    toy_game_update_world(&g, 16);
    if (g.enemies[id].target_kind != TOY_GAME_TARGET_ACTOR ||
        g.enemies[id].target_index != old_index) return 36;
    g.actors[0].z = 700;
    g.enemies[id].retarget_timer_ms = 0;
    toy_game_update_world(&g, 16);
    if (g.enemies[id].target_kind != TOY_GAME_TARGET_HOST) return 37;
    g.enemies[id].active = 0;
    g.enemies_alive--;
    if (toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_FAST, 0, 1000) != id ||
        g.enemies[id].combat_generation == generation) return 38;
    shooter->combat_scan_ms = 200;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->combat_aim_ms) return 39;

    /* A command remains the allied movement authority while combat runs. */
    shooter = combat_ai_fixture(&g);
    shooter->faction = TOY_GAME_FACTION_ALLIED;
    shooter->companion = 1; shooter->fire_enabled = 0;
    shooter->command_destination_active = 1;
    shooter->command_x = -5000; shooter->command_z = 0;
    id = toy_game_add_gunner(&g, 0, 0, 5000, "COMMAND OPPONENT");
    if (id < 0) return 40;
    toy_game_actor_by_id(&g, id)->fire_enabled = 0;
    for (i = 0; i < 30; i++) toy_game_update_ai_teammates(&g, 16);
    if (shooter->x >= 0 || !shooter->command_destination_active ||
        shooter->combat_target.kind != TOY_GAME_ENTITY_ACTOR) return 41;
    i = combat_rescue_logic_test();
    if (i) { __printf("COMBAT rescue contract failed: %d\n", i); return 42; }
    i = actor_navigation_cache_logic_test();
    if (i) { __printf("ACTOR-NAV cache contract failed: %d\n", i); return 43; }
    i = actor_endpoint_navigation_logic_test();
    if (i) { __printf("ACTOR-NAV endpoints contract failed: %d\n", i); return 44; }
    i = combat_target_retention_logic_test();
    if (i) { __printf("COMBAT target-retention contract failed: %d\n", i); return 45; }
    i = gunner_hold_logic_test();
    if (i) { __printf("COMBAT gunner-hold contract failed: %d\n", i); return 46; }
    __printf("COMBAT AI aim/visibility/death/identity/finite-ammo/guard/assault/three-factions/RTS passed\n");
    return 0;
}
