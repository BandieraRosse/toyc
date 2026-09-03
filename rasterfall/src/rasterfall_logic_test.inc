#define special_windup_ms ability.special_windup_ms
#define special_target_active ability.special_target_active
#define special_target_kind ability.special_target_kind
#define special_pull_timer_ms ability.special_pull_timer_ms
#define charge_active ability.charge_active

static struct toy_map_primitive test_flat_primitive(
    int minx, int maxx, int minz, int maxz, int y)
{
    struct toy_map_primitive p;
    memset(&p, 0, sizeof(p));
    p.shape=TOY_MAP_PRIMITIVE_FLAT; p.minx=minx; p.maxx=maxx;
    p.minz=minz; p.maxz=maxz; p.surface_y0=y; p.surface_y1=y;
    p.flags=TOY_MAP_PRIMITIVE_COLLISION|TOY_MAP_PRIMITIVE_WALKABLE;
    return p;
}

static struct toy_map_primitive test_ramp_primitive(
    int shape, int minx, int maxx, int minz, int maxz, int y0, int y1)
{
    struct toy_map_primitive p=test_flat_primitive(minx,maxx,minz,maxz,y0);
    p.shape=shape; p.surface_y1=y1;
    return p;
}

static struct toy_map_primitive test_box_primitive(
    int minx, int maxx, int minz, int maxz, int miny, int maxy,
    unsigned int flags)
{
    struct toy_map_primitive p;
    memset(&p,0,sizeof(p)); p.shape=TOY_MAP_PRIMITIVE_BOX;
    p.minx=minx; p.maxx=maxx; p.minz=minz; p.maxz=maxz;
    p.base_y=miny; p.surface_y0=maxy; p.surface_y1=maxy; p.flags=flags;
    return p;
}

static int primitive_gameplay_logic_test(void)
{
    struct toy_game g;
    struct toy_game_ground_query ground;
    struct toy_map_primitive world[2];
    int left, right, table=0, ramp=0, air_gate=0;
    world[0]=test_flat_primitive(-2000,2000,-2000,2000,0);
    world[1]=test_flat_primitive(200,1000,-500,500,100);
    toy_game_init(&g,211); toy_game_set_primitives(&g,world,2,2000);
    ground=toy_game_query_ground(&g,400,0,TOY_GAME_PLAYER_RADIUS,0);
    if(!ground.has_support||!ground.has_landing||ground.support_y!=100)return 1;
    g.px=0;g.pz=0;g.player_ground_y=0;
    if(!toy_game_try_move_player(&g,400,0)||g.player_ground_y!=100)return 2;
    world[0]=test_ramp_primitive(TOY_MAP_PRIMITIVE_RAMP_X,
                                0,1200,-600,600,0,400);
    toy_game_set_primitives(&g,world,1,2000);
    ground=toy_game_query_ground(&g,600,0,0,0);
    if(!ground.has_support||ground.support_y!=200||!ground.support_is_ramp)return 3;
    g.px=200;g.pz=0;g.player_ground_y=200*400/1200;
    for(int x=300;x<=900;x+=100)if(!toy_game_try_move_player(&g,x,0)||g.player_ground_y!=x*400/1200)return 4;
    for(int x=800;x>=200;x-=100)if(!toy_game_try_move_player(&g,x,0)||g.player_ground_y!=x*400/1200)return 5;
    {
        struct toy_game nav_game;
        struct toy_map_primitive steep = test_ramp_primitive(
            TOY_MAP_PRIMITIVE_RAMP_X, -600, 600, -1200, 1200, 0, 600);
        int low = 3 * 8 + 3, high = 3 * 8 + 4;
        toy_game_init(&nav_game, 212);
        toy_game_set_primitives(&nav_game, &steep, 1, 1200);
        if (!nav_game.nav_walkable[low] || !nav_game.nav_walkable[high] ||
            nav_game.nav_component[low] != nav_game.nav_component[high] ||
            nav_game.nav_link_type[low * TOY_GAME_NAV_LINK_DIRECTIONS + 4] !=
                TOY_GAME_NAV_LINK_RAMP) {
            return 11;
        }
        {
            struct toy_map_primitive step[2] = {
                test_flat_primitive(-1200, 0, -1200, 1200, 0),
                test_flat_primitive(0, 1200, -1200, 1200, 600)
            };
            step[0].flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
            step[1].flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
            toy_game_set_primitives(&nav_game, step, 2, 1200);
            if (nav_game.nav_component[low] == nav_game.nav_component[high] ||
                nav_game.nav_link_type[low * TOY_GAME_NAV_LINK_DIRECTIONS + 4] !=
                    TOY_GAME_NAV_LINK_BLOCKED) return 12;
        }
    }
    {
        struct toy_game ramp_game;
        struct toy_map_primitive ramp_world[3] = {
            test_flat_primitive(-5000,5000,-5000,5000,0),
            test_ramp_primitive(TOY_MAP_PRIMITIVE_RAMP_X,
                                0,2400,-1000,1000,0,600),
            test_flat_primitive(2400,4200,-1000,1000,600)
        };
        int i, max_ground = 0;
        toy_game_init(&ramp_game, 213);
        ramp_game.to_spawn = 0;
        ramp_game.px = 3300;
        ramp_game.pz = 0;
        ramp_game.hp = 100000;
        ramp_game.safe_room_count = 0;
        toy_game_set_primitives(&ramp_game, ramp_world, 3, 5000);
        toy_game_place_enemy(&ramp_game, -500, 0);
        if (!ramp_game.enemies[0].active) return 13;
        ramp_game.enemies[0].ai_state = TOY_GAME_ENEMY_TRACKING;
        for (i = 0; i < 360; i++) {
            toy_game_update_held(&ramp_game, NULL, 0, 0, 0, 1024, 16);
            if (ramp_game.enemies[0].ground_y > max_ground)
                max_ground = ramp_game.enemies[0].ground_y;
        }
        if (max_ground < 450 || ramp_game.enemies[0].x < 1800) return 14;
    }
    world[0]=test_flat_primitive(-1200,1200,-1200,1200,0);
    world[1]=test_box_primitive(200,400,-1000,1000,0,1000,TOY_MAP_PRIMITIVE_COLLISION);
    toy_game_set_primitives(&g,world,2,1200);g.px=0;g.pz=0;g.player_ground_y=0;
    if(toy_game_try_move_player(&g,100,0))return 6;
    world[1].flags&=~TOY_MAP_PRIMITIVE_COLLISION;toy_game_rebuild_navigation(&g);
    if(!toy_game_try_move_player(&g,100,0))return 7;
    world[1]=test_box_primitive(-100,100,-1200,1200,0,1000,TOY_MAP_PRIMITIVE_COLLISION);
    toy_game_rebuild_navigation(&g);
    left=((150-g.nav_origin)/g.nav_cell_size)*g.nav_width+(-750-g.nav_origin)/g.nav_cell_size;
    right=((150-g.nav_origin)/g.nav_cell_size)*g.nav_width+(750-g.nav_origin)/g.nav_cell_size;
    if(!g.nav_walkable[left]||!g.nav_walkable[right]||!g.nav_component[left]||!g.nav_component[right]||g.nav_component[left]==g.nav_component[right]){
        __printf("primitive nav split: walk=%d/%d component=%d/%d\n",
                 g.nav_walkable[left],g.nav_walkable[right],
                 g.nav_component[left],g.nav_component[right]);return 8;}
    world[1].flags&=~TOY_MAP_PRIMITIVE_COLLISION;toy_game_rebuild_navigation(&g);
    if(!g.nav_walkable[left]||!g.nav_walkable[right]||g.nav_component[left]!=g.nav_component[right])return 9;
    for(int i=0;i<level_map.primitive_count;i++){
        const struct toy_map_primitive *p=&level_map.primitives[i];
        if(p->shape==TOY_MAP_PRIMITIVE_FLAT&&p->minx==-880&&p->maxx==880&&
           p->surface_y0==650&&(p->flags&TOY_MAP_PRIMITIVE_WALKABLE))table=1;
        if(p->shape==TOY_MAP_PRIMITIVE_RAMP_X&&p->minx==7000&&
           p->surface_y0==0&&p->surface_y1==900&&
           (p->flags&TOY_MAP_PRIMITIVE_WALKABLE))ramp=1;
        if(!strcmp(p->role,"air_gate_left")&&
           (p->flags&TOY_MAP_PRIMITIVE_COLLISION))air_gate=1;
    }
    if(!table||!ramp||!air_gate)return 10;
    {
        struct toy_game west_game;
        int i, max_x;
        toy_game_init(&west_game, 214);
        west_game.to_spawn = 0;
        west_game.px = -13000;
        west_game.pz = 0;
        west_game.hp = 100000;
        west_game.safe_room_count = 0;
        toy_game_set_primitives(&west_game, level_map.primitives,
                                level_map.primitive_count, 45000);
        toy_game_place_enemy(&west_game, -43400, 0);
        if (!west_game.enemies[0].active) return 15;
        west_game.enemies[0].ai_state = TOY_GAME_ENEMY_TRACKING;
        max_x = west_game.enemies[0].x;
        for (i = 0; i < 1000; i++) {
            toy_game_update_held(&west_game, NULL, 0, 0, 0, 1024, 16);
            if (west_game.enemies[0].x > max_x) max_x = west_game.enemies[0].x;
        }
        if (max_x < -35000) return 16;
    }
    return 0;
}

static int run_logic_test(void)
{
    struct camera camera;
    struct toy_input input;
    struct vec3 triangle[3], clipped[4];
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct rasterfall_command command;
    uint32_t *pixels;
    int count;
    long long direction_length;
    { int primitive_result=primitive_gameplay_logic_test();
      if(primitive_result){__printf("primitive gameplay failure: %d\n",primitive_result);return 211;} }
    __printf("rasterfall: primitive gameplay test passed\n");
    if (rasterfall_model_skinning_logic_test() != 0) return 157;
    if (rasterfall_humanoid_logic_test() != 0) return 162;
    if (rasterfall_humanoid_basis_logic_test() != 0) return 163;
    if (rasterfall_humanoid_retarget_logic_test() != 0) return 164;
    if (rasterfall_animation_logic_test() != 0) return 161;
    if (rasterfall_animation_composition_logic_test() != 0) return 168;
    if (rasterfall_character_logic_test() != 0) return 166;
    if (rasterfall_console_logic_test() != 0) return 169;
    toy_input_init(&input);
    rasterfall_session_reset(&session, &camera, 1);
    if (RASTERFALL_RFU_FROM_MM(1000) != RASTERFALL_RFU_PER_METER ||
        RASTERFALL_HUMAN_HEIGHT_RFU != 896 ||
        RASTERFALL_HUMAN_EYE_HEIGHT_RFU != 824 ||
        camera.y != RASTERFALL_STANDING_CAMERA_Y) return 167;
    {
        static const int kinds[13] = {
            TOY_MAP_PICKUP_POSE_RESET_BUTTON,
            TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON,
            TOY_MAP_PICKUP_POSE_ARMS_BUTTON,
            TOY_MAP_PICKUP_POSE_BODY_BUTTON,
            TOY_MAP_PICKUP_ANIM_IDLE_BUTTON,
            TOY_MAP_PICKUP_ANIM_WALK_BUTTON,
            TOY_MAP_PICKUP_ANIM_JOG_BUTTON,
            TOY_MAP_PICKUP_GLB_IDLE_BUTTON,
            TOY_MAP_PICKUP_GLB_WALK_BUTTON,
            TOY_MAP_PICKUP_GLB_JOG_BUTTON,
            TOY_MAP_PICKUP_VMD_WALK_BUTTON,
            TOY_MAP_PICKUP_ANIMATION_COMPOSITION_BUTTON,
            TOY_MAP_PICKUP_HUMANOID_POSE_DEBUG_BUTTON
        };
        static const int poses[13] = {
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_RIGHT_ARM,
            RASTERFALL_MODEL_POSE_ARMS,
            RASTERFALL_MODEL_POSE_BODY_TURN,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND,
            RASTERFALL_MODEL_POSE_BIND
        };
        int button, found = 0;
        for (button = 0; button < 13; button++) {
            int item;
            for (item = 0; item < session.item_count; item++)
                if (session.items[item].kind == kinds[button]) break;
            if (item >= session.item_count) return 158;
            camera.x = session.items[item].x;
            camera.z = session.items[item].z + 500;
            camera.sy = 0; camera.cy = -1024;
            rasterfall_session_interact_remote(&session, &camera, -1);
            if (session.skeletal_demo_pose != poses[button]) return 159;
            if (button == 0 && (session.skeletal_demo_player.playing ||
                                session.skeletal_demo_player.clip_id != -1))
                return 162;
            if (button == 1 && (!session.skeletal_demo_player.playing ||
                                session.skeletal_demo_player.clip_id != 0))
                return 163;
            if (button == 2 && (!session.skeletal_demo_player.playing ||
                                session.skeletal_demo_player.clip_id != 1 ||
                                !session.skeletal_demo_player.loop))
                return 164;
            if (button >= 4 && (!session.skeletal_demo_player.playing ||
                                session.skeletal_demo_player.clip_id !=
                                    (button >= 11 ? 11 : button - 1) ||
                                !session.skeletal_demo_player.loop))
                return 165;
            found++;
        }
        if (found != 13 || !session.pose_debug_active) return 158;
        rasterfall_session_reset(&session, &camera, 1);
    }
    if (session.level.safe_count > 0 &&
        session.game_state.safe_room_count != session.level.safe_count)
        return 126;
    {
        int anime=-1,i;
        for(i=0;i<TOY_GAME_MAX_ACTORS;i++)
            if(session.game_state.actors[i].active&&
               session.game_state.actors[i].anime_character_id){anime=i;break;}
        if(anime<0||session.game_state.actors[anime].max_hp!=190||
           session.game_state.actors[anime].slots[
             session.game_state.actors[anime].current_slot].weapon!=TOY_GAME_WEAPON_AK)
            return 169;
    }
    {
        struct toy_game anime_follow;
        int id,before;
        toy_game_init(&anime_follow,77);
        id=toy_game_add_anime_actor(&anime_follow,0,0,0,"EULA_TEST");
        if(id<0)return 170;
        anime_follow.room_limit=10000;
        anime_follow.px=3000;anime_follow.pz=0;
        before=anime_follow.actors[id-1].x;
        toy_game_update_ai_teammates(&anime_follow,100);
        if(anime_follow.actors[id-1].x<=before||
           anime_follow.actors[id-1].max_hp<=TOY_CONFIG_AI_LEVEL_3_HP)
            return 170;
    }
    /* 远端旗帜操作必须由主机状态承接，并随携带者同步位置。 */
    if (session.flag_count != 2) return 131;
    camera.x = session.flags[0].x;
    camera.z = session.flags[0].z;
    rasterfall_session_toggle_flag_remote(&session, &camera, 1);
    if (!session.flags[0].carried || session.flags[0].carrier_id != 1)
        return 132;
    camera.x += 240;
    camera.z -= 120;
    rasterfall_session_update_flag_remote(&session, &camera, 1);
    if (session.flags[0].x != 240 || session.flags[0].z != -120)
        return 133;
    rasterfall_session_toggle_flag_remote(&session, &camera, 1);
    if (session.flags[0].carried || session.flags[0].carrier_id != -1)
        return 134;
    if (session.flags[0].x != 240 || session.flags[0].z != -120)
        return 135;
    /* 单人商店 API：查询与执行必须使用同一套价格/合法性规则；AI
     * 条件查询只读游戏状态，不能偷偷改变经济或库存。 */
    {
        struct rasterfall_shop_request shop = {
            RASTERFALL_SHOP_BUY_WEAPON, TOY_GAME_WEAPON_SMG, -1, 0
        };
        int price = 0;
        const struct toy_game_actor *ai = &session.game_state.actors[0];
        if (!rasterfall_session_shop_can(&session, &shop, &price) ||
            price != TOY_GAME_PRICE_SMG ||
            toy_game_actor_health_percent(ai) <= 0 ||
            toy_game_actor_ammo_percent(ai) < 0 ||
            toy_game_count_active_enemies(&session.game_state) != 0 ||
            toy_game_count_downed_actors(&session.game_state) != 0 ||
            toy_game_nearest_enemy_distance(&session.game_state, ai) != -1)
            return 142;
        if (rasterfall_session_shop_execute(&session, &shop) != 2 ||
            session.game_state.money != 0 ||
            !toy_game_weapon_unlocked(&session.game_state,
                                      TOY_GAME_WEAPON_SMG)) return 143;
        shop.item = TOY_GAME_WEAPON_AWP;
        if (rasterfall_session_shop_can(&session, &shop, &price)) return 143;
        rasterfall_session_reset(&session, &camera, 1);
    }
    {
        int actor_index = rasterfall_session_shop_actor_at(&session, 0, 0);
        int was_assigned;
        if (actor_index < 0) return 135;
        was_assigned = session.game_state.actors[actor_index].flag_index == 0;
        if (rasterfall_session_shop_request(&session, 4, 0,
                                            actor_index) <= 0 ||
            session.game_state.actors[actor_index].flag_index !=
                (was_assigned ? -1 : 0))
            return 136;
    }
    /* 客户端交互由主机执行，但按下 E 后本地仍必须立即显示反馈。 */
    if (session.item_count > 0) {
        memset(&command, 0, sizeof(command));
        camera.x = session.items[0].x;
        camera.z = session.items[0].z - 100;
        camera.sy = 0; camera.cy = 1024;
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step_client(&session, &camera, &command, 16);
        if (!session.banner_text || session.banner_ms <= 0) return 100;
        rasterfall_session_reset(&session, &camera, 1);
    }
    /* 最小托管闭环：休息阶段走到波次按钮并跳波，死亡后自动付费复活。 */
    {
        int wave_item = -1;
        int i;
        rasterfall_session_reset(&session, &camera, 3);
        for (i = 0; i < session.item_count; i++)
            if (session.items[i].kind == TOY_MAP_PICKUP_WAVE_SKIP_BUTTON) {
                wave_item = i;
                break;
            }
        if (wave_item < 0 ||
            !rasterfall_session_set_managed_ai(&session, 1)) return 160;
        session.game_state.px = -200;
        session.game_state.pz = -7450;
        session.game_state.player_airborne_ms = 400;
        session.game_state.player_knockback_x = 900;
        session.game_state.player_knockback_z = -700;
        camera.x = session.game_state.px;
        camera.z = session.game_state.pz;
        if (!rasterfall_session_recover_managed_player(&session, &camera) ||
            camera.x != session.level.start_x ||
            camera.z != session.level.start_z ||
            session.game_state.px != camera.x ||
            session.game_state.pz != camera.z ||
            session.game_state.player_airborne_ms != 0 ||
            session.game_state.player_knockback_x != 0 ||
            session.game_state.player_knockback_z != 0)
            return 164;
        /* Charger recovery: a player dropped into the developer table area
         * must be returned to the base corridor by the fixed escape route. */
        session.game_state.campaign_phase = TOY_GAME_PHASE_HORDE;
        camera.x = 0;
        camera.z = -7450;
        session.game_state.px = camera.x;
        session.game_state.pz = camera.z;
        camera.sy = 0; camera.cy = 1024;
        for (i = 0; i < 300; i++) {
            rasterfall_session_step(&session, &camera, &command, 16);
            if (session.managed_ai_escape_phase < 0) break;
        }
        if (!((camera.x >= -500 && camera.x <= 500 &&
               camera.z >= -500 && camera.z <= 500) ||
              (camera.x == session.level.start_x &&
               camera.z == session.level.start_z)))
            return 163;
        rasterfall_session_reset(&session, &camera, 3);
        rasterfall_session_set_managed_ai(&session, 1);
        session.game_state.campaign_phase = TOY_GAME_PHASE_CALM;
        session.game_state.spawn_timer_ms = 100000;
        camera.x = 0;
        camera.z = 0;
        camera.sy = 0; camera.cy = 1024;
        for (i = 0; i < 300 &&
             session.game_state.campaign_phase == TOY_GAME_PHASE_CALM; i++)
            rasterfall_session_step(&session, &camera, &command, 16);
        if (session.game_state.campaign_phase == TOY_GAME_PHASE_CALM)
            return 161;
        session.game_state.player_down = 1;
        session.game_state.money = RASTERFALL_PAID_REVIVE_COST + 1;
        rasterfall_session_step(&session, &camera, &command, 16);
        if (session.game_state.player_down ||
            session.game_state.money != 1) return 162;
        rasterfall_session_set_managed_ai(&session, 0);
        rasterfall_session_reset(&session, &camera, 3);
    }
    {
        struct toy_game actor_test;
        struct toy_game ai_reload_test;
        struct toy_game throwable_test;
        int i;
        int ai_id;
        struct toy_game_actor *throwable_ai;
        const struct toy_game_actor *actor =
            toy_game_actor_by_id_const(&game, 1);
        const struct toy_game_enemy_info *common =
            toy_game_enemy_info(TOY_GAME_ENEMY_COMMON);
        const struct toy_game_enemy_info *pursuit_common =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_COMMON);
        const struct toy_game_enemy_info *heavy =
            toy_game_enemy_info(TOY_GAME_ENEMY_HEAVY);
        const struct toy_game_enemy_info *brute =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_HEAVY);
        const struct toy_game_enemy_info *runner =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_FAST);
        const struct toy_game_enemy_info *smoker =
            toy_game_enemy_info(TOY_GAME_ENEMY_SMOKER);
        const struct toy_game_enemy_info *charger =
            toy_game_enemy_info(TOY_GAME_ENEMY_CHARGER);
        const struct toy_game_enemy_info *tank =
            toy_game_enemy_info(TOY_GAME_ENEMY_TANK);
        struct toy_game_ai_observation ai_observation;
        struct toy_game_ai_decision ai_decision;
        struct toy_game ai_observe_test;
        struct rasterfall_ai_registry ai_registry;
        const struct toy_game_weapon_info *smg =
            toy_game_weapon_info(TOY_GAME_WEAPON_SMG);
        const struct toy_game_weapon_info *shotgun =
            toy_game_weapon_info(TOY_GAME_WEAPON_SHOTGUN);
        const struct toy_game_weapon_info *pistol =
            toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
        const struct toy_game_weapon_info *ak =
            toy_game_weapon_info(TOY_GAME_WEAPON_AK);
        const struct toy_game_weapon_info *awp =
            toy_game_weapon_info(TOY_GAME_WEAPON_AWP);
        toy_game_init(&ai_observe_test, 151);
        if (toy_game_add_ai(&ai_observe_test, TOY_GAME_AI_LEVEL_2,
                            0, 0, "OBSERVE") < 0 ||
            !toy_game_ai_observe(&ai_observe_test, 0, &ai_observation) ||
            ai_observation.actor_id != ai_observe_test.actors[0].actor_id ||
            ai_observation.health_percent <= 0 ||
            ai_observation.current_weapon < 0 ||
            ai_observation.nearest_enemy_index != -1 ||
            !ai_observation.at_deployment) return 144;
        toy_game_ai_decision_clear(&ai_decision);
        if (ai_decision.target_enemy != -1 ||
            ai_decision.switch_weapon != -1 ||
            ai_decision.shop_action != 0) return 145;
        rasterfall_ai_registry_init(&ai_registry);
        rasterfall_ai_registry_sync(&ai_registry, &ai_observe_test);
        if (ai_registry.count != 2 ||
            rasterfall_ai_registry_add(
                &ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX,
                RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER, 1,
                RASTERFALL_AI_POLICY_IDLE) < 0 ||
            ai_registry.count != 3) return 146;
        rasterfall_ai_registry_think(&ai_registry, &ai_observe_test, 1);
        if (ai_registry.agents[2].controller !=
                RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER ||
            ai_registry.agents[2].decision.target_enemy != -1) return 147;
        if (!actor || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->class_id != TOY_GAME_AI_LEVEL_2 ||
            pursuit_common->speed_min <= common->speed_max ||
            heavy->max_hp <= common->max_hp ||
            heavy->bite_damage <= common->bite_damage ||
            runner->speed_min <= pursuit_common->speed_max ||
            smg->damage != 25 || shotgun->damage != 25 ||
            common->max_hp != 50 || heavy->max_hp != 200 ||
            brute->max_hp != 200 || runner->max_hp != 60) return 49;
        if (!toy_game_enemy_type_is_valid(TOY_GAME_ENEMY_SMOKER) ||
            toy_game_enemy_type_is_valid(-1) ||
            toy_game_enemy_content_id(TOY_GAME_ENEMY_SMOKER) !=
                TOY_GAME_ENEMY_ID_SMOKER ||
            toy_game_enemy_from_content_id(TOY_GAME_ENEMY_ID_CHARGER) !=
                TOY_GAME_ENEMY_CHARGER ||
            toy_game_enemy_from_content_id(TOY_GAME_ENEMY_ID_TANK) !=
                TOY_GAME_ENEMY_TANK ||
            toy_game_enemy_from_content_id(99) >= 0 ||
            smoker->ability != TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE ||
            charger->ability != TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH ||
            tank->ability != TOY_GAME_ENEMY_ABILITY_TANK_SWEEP ||
            tank->max_hp != 4000 ||
            tank->speed_min != common->speed_min ||
            tank->speed_max != common->speed_max ||
            TOY_CONFIG_TANK_ATTACK_RANGE != 2850 ||
            TOY_CONFIG_TANK_ATTACK_START_DIVISOR != 2 ||
            TOY_CONFIG_TANK_ATTACK_START_RANGE != 1425 ||
            TOY_CONFIG_TANK_WINDUP_MS != 750 ||
            TOY_CONFIG_TANK_IMPACT_MS != 625) return 56;
        if (toy_game_weapon_from_name("pistol") != TOY_GAME_WEAPON_PISTOL ||
            toy_game_weapon_from_name("SMG") != TOY_GAME_WEAPON_SMG ||
            toy_game_weapon_from_name("shotgun") != TOY_GAME_WEAPON_SHOTGUN ||
            toy_game_weapon_from_name("missing") >= 0) return 53;
        if (!toy_game_weapon_is_valid(TOY_GAME_WEAPON_PISTOL) ||
            toy_game_weapon_is_valid(-1) ||
            toy_game_weapon_content_id(TOY_GAME_WEAPON_PISTOL) !=
                TOY_GAME_WEAPON_ID_PISTOL ||
            toy_game_weapon_content_id(TOY_GAME_WEAPON_SMG) !=
                TOY_GAME_WEAPON_ID_SMG ||
            toy_game_weapon_from_content_id(TOY_GAME_WEAPON_ID_SHOTGUN) !=
                TOY_GAME_WEAPON_SHOTGUN ||
            toy_game_weapon_from_content_id(99) >= 0 ||
            strcmp(smg->short_name, "SMG") != 0 ||
            shotgun->muzzle_profile != TOY_GAME_MUZZLE_SHOTGUN) return 55;
        if (awp->range != TOY_GAME_MAX_RANGE * 2 ||
            awp->alert_range != TOY_GAME_MAX_RANGE * 2 ||
            ak->range != TOY_GAME_MAX_RANGE * 3 / 2 ||
            ak->alert_range != TOY_GAME_MAX_RANGE ||
            smg->alert_range <= TOY_GAME_DETECT_RANGE ||
            pistol->alert_range <= TOY_GAME_DETECT_RANGE ||
            shotgun->alert_range != TOY_GAME_DETECT_RANGE) return 116;
        toy_game_actor_set_animation(&game.actors[1], TOY_GAME_ANIM_IDLE);
        toy_game_actor_update_animation(&game.actors[1], 160);
        if (game.actors[1].animation.id != TOY_GAME_ANIM_IDLE ||
            game.actors[1].animation.time_ms != 160 ||
            toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms != 180 ||
            toy_game_animation_info(TOY_GAME_ANIM_FIRE)->loop ||
            toy_game_animation_info(99)->duration_ms != 0) return 59;
        toy_game_animation_set(&game.animation, TOY_GAME_ANIM_RELOAD);
        toy_game_animation_update(&game.animation, 450);
        if (game.animation.id != TOY_GAME_ANIM_RELOAD ||
            game.animation.time_ms != 450) return 60;
        toy_game_animation_update(&game.animation, 600);
        if (game.animation.time_ms != 1050) return 61;
        toy_game_animation_set(&game.animation, TOY_GAME_ANIM_NONE);
        toy_game_init(&ai_reload_test, 9);
        ai_id = toy_game_add_ai(&ai_reload_test, TOY_GAME_AI_LEVEL_1,
                                0, 0, "RELOAD TEST");
        ai_reload_test.actors[ai_id - 1].slots[
            ai_reload_test.actors[ai_id - 1].current_slot].mag = 1;
        ai_reload_test.enemies[0].active = 1;
        ai_reload_test.enemies[0].type = TOY_GAME_ENEMY_HEAVY;
        ai_reload_test.enemies[0].x = 0;
        ai_reload_test.enemies[0].z = 1000;
        ai_reload_test.enemies[0].hp = 200;
        toy_game_update_ai_teammates(&ai_reload_test, 16);
        if (!ai_reload_test.actors[ai_id - 1].reloading ||
            ai_reload_test.actors[ai_id - 1].animation.id !=
                TOY_GAME_ANIM_RELOAD) return 62;
        toy_game_update_ai_teammates(&ai_reload_test,
                                     toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->reload_ms + 100);
        if (ai_reload_test.actors[ai_id - 1].reloading ||
            ai_reload_test.actors[ai_id - 1].slots[
                ai_reload_test.actors[ai_id - 1].current_slot].reserve !=
                TOY_GAME_AMMO_INFINITE ||
            ai_reload_test.actors[ai_id - 1].slots[
                ai_reload_test.actors[ai_id - 1].current_slot].mag <= 0) return 63;
        if (!game.actors[1].active ||
            game.actors[1].class_id != TOY_GAME_AI_LEVEL_1 ||
            game.actors[1].slots[game.actors[1].current_slot].weapon != TOY_GAME_WEAPON_PISTOL ||
            !game.actors[2].active ||
            game.actors[2].class_id != TOY_GAME_AI_LEVEL_3 ||
            (game.actors[2].slots[game.actors[2].current_slot].weapon != TOY_GAME_WEAPON_AK &&
             game.actors[2].slots[game.actors[2].current_slot].weapon != TOY_GAME_WEAPON_AWP) ||
            game.actors[0].hp != 120 || game.actors[0].max_hp != 120 ||
            game.actors[0].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[1].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[2].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[0].x != 1000 || game.actors[0].z != 0 ||
            game.actors[1].x != 0 || game.actors[1].z != -900 ||
            game.actors[2].x != -1000 || game.actors[2].z != 0 ||
            !game.actors[3].active || !game.actors[3].base_core ||
            game.actors[3].hp != TOY_CONFIG_BASE_HP ||
            game.actors[3].max_hp != TOY_CONFIG_BASE_HP ||
            game.actors[3].slots[0].weapon != TOY_GAME_WEAPON_PISTOL ||
            game.actors[3].state != TOY_GAME_ACTOR_ALIVE) return 51;
        toy_game_init(&actor_test, 3);
        if (toy_game_set_remote_player(&actor_test, 1, 1,
                                       2400, -3000, "REMOTE") !=
                TOY_GAME_REMOTE_ACTOR_BASE ||
            !actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].active ||
            actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].kind !=
                TOY_GAME_ACTOR_PLAYER ||
            actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].hp !=
                TOY_GAME_SECONDARY_PLAYER_HP) return 54;
        if (toy_game_set_remote_player(&actor_test, 2, 1,
                                       2750, -3000, "REMOTE2") !=
                TOY_GAME_REMOTE_ACTOR_BASE + 1 ||
            toy_game_set_remote_player(&actor_test, 3, 1,
                                       3100, -3000, "REMOTE3") !=
                TOY_GAME_REMOTE_ACTOR_BASE + 2 ||
            !actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 1].active ||
            !actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 2].active)
            return 55;
        toy_game_init(&throwable_test, 4);
        throwable_test.money = 200;
        if (toy_game_buy_weapon(&throwable_test, TOY_GAME_WEAPON_BOMB) <= 0 ||
            throwable_test.slots[2].weapon != TOY_GAME_WEAPON_BOMB ||
            toy_game_buy_weapon(&throwable_test, TOY_GAME_WEAPON_MOLOTOV) <= 0 ||
            throwable_test.slots[2].weapon != TOY_GAME_WEAPON_MOLOTOV)
            return 137;
        throwable_test.current_slot = 3;
        throwable_test.slots[3].weapon = TOY_GAME_WEAPON_PILL;
        throwable_test.slots[3].mag = 2;
        throwable_test.hp = 50;
        if (!toy_game_use_pill(&throwable_test) ||
            throwable_test.hp != TOY_GAME_PLAYER_HP ||
            throwable_test.slots[3].mag != 1)
            return 138;
        throwable_ai = &throwable_test.actors[0];
        throwable_ai->hp = throwable_ai->max_hp - 40;
        throwable_test.px = throwable_ai->x;
        throwable_test.pz = throwable_ai->z - 500;
        toy_game_shove(&throwable_test, 0, 1024);
        if (throwable_ai->hp != throwable_ai->max_hp ||
            throwable_test.slots[3].mag != 0)
            return 139;
        if (toy_game_set_remote_player(&throwable_test, 1, 1,
                                       throwable_test.px + 100,
                                       throwable_test.pz, "REMOTE") < 0)
            return 140;
        throwable_test.hp = TOY_GAME_PLAYER_HP;
        throwable_test.projectiles[0].active = 1;
        throwable_test.projectiles[0].kind = TOY_GAME_WEAPON_BOMB;
        throwable_test.projectiles[0].landed = 1;
        throwable_test.projectiles[0].fuse_ms = 1;
        throwable_test.projectiles[0].x = throwable_test.px;
        throwable_test.projectiles[0].z = throwable_test.pz;
        toy_game_update(&throwable_test, NULL, 0, 0, 1024, 16);
        if (throwable_test.hp >= TOY_GAME_PLAYER_HP ||
            throwable_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].hp >=
                TOY_GAME_SECONDARY_PLAYER_HP)
            return 141;
        actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].slots[1].mag = 7;
        actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 1].slots[1].mag = 11;
        if (actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 2].slots[1].mag == 7 ||
            actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 2].slots[1].mag == 11)
            return 56;
        if (toy_game_add_ai(&actor_test, TOY_GAME_AI_LEVEL_3,
                            1200, -3000, "ELITE") != 2 ||
            !toy_game_actor_by_id(&actor_test, 2) ||
            toy_game_actor_by_id(&actor_test, 2)->hp != 160 ||
            toy_game_actor_by_id(&actor_test, 2)->slots[0].reserve !=
                TOY_GAME_AMMO_INFINITE) return 50;
        actor_test.actors[0].state = TOY_GAME_ACTOR_DOWNED;
        actor_test.actors[0].hp = 0;
        for (i = 0; i < (TOY_GAME_REVIVE_MS + 15) / 16; i++)
            toy_game_revive_actor(&actor_test, 0, 16);
        if (actor_test.actors[0].state != TOY_GAME_ACTOR_ALIVE ||
            actor_test.actors[0].hp != TOY_GAME_REVIVE_HP) return 52;
    }
    memset(&command, 0, sizeof(command));
    command.move_forward = 1;
    for (int i = 0; i < 10; i++)
        rasterfall_session_step(&session, &camera, &command, 16);
    if (camera.x != session.level.start_x + session.level.start_sy * 760 / 1024 ||
        camera.z != session.level.start_z + session.level.start_cy * 760 / 1024)
        return 1;

    /* 死亡重开仍须恢复世界碰撞配置并能推进首波。 */
    game.state = TOY_GAME_OVER;
    rasterfall_session_reset(&session, &camera, 2);
    if (game.primitive_count != level_map.primitive_count ||
        game.room_limit != level_map.room_limit ||
        toy_game_position_blocked(&game, camera.x, camera.z,
                                  RASTERFALL_PLAYER_RADIUS)) {
        return 13;
    }
    if (!toy_game_skip_wave_rest(&game)) return 139;
    for (int i = 0; i < 170; i++)
        toy_game_update(&game, NULL, 0, camera.sy, camera.cy, 16);
    if (game.enemies_alive == 0) return 14;

    camera.x = -1200; camera.z = 0; camera.sy = 0; camera.cy = 1024;

    memset(&command, 0, sizeof(command));
    command.turn = 16;
    rasterfall_session_step(&session, &camera, &command, 16);
    /* 首次转向时 old_sy=0，定点旋转 cy 恰为 1024 不变，只断言转向生效 */
    if (camera.sy <= 0) return 3;
    memset(&command, 0, sizeof(command));
    command.pitch = 16;
    rasterfall_session_step(&session, &camera, &command, 16);
    if (camera.pitch_sy <= 0) return 4;
    camera.sy = 0;
    camera.cy = 1024;
    for (int i = 0; i < 10000; i++) rasterfall_camera_rotate(&camera, 37, 0);
    direction_length = isqrt((long long)camera.sy * camera.sy +
                             (long long)camera.cy * camera.cy);
    if (direction_length < 1022 || direction_length > 1026) return 16;
    camera.sy = 0;
    camera.cy = 1024;
    /* 俯仰持续旋转时保持归一化，并钳制在 ±75°，不能跨过 ±90°。 */
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;
    for (int i = 0; i < 10000; i++) rasterfall_camera_rotate(&camera, 0, 37);
    direction_length = isqrt((long long)camera.pitch_sy * camera.pitch_sy +
                             (long long)camera.pitch_cy * camera.pitch_cy);
    if (direction_length < 1022 || direction_length > 1026) return 41;
    if (camera.pitch_cy != RASTERFALL_PITCH_LIMIT_CY ||
        camera.pitch_sy != RASTERFALL_PITCH_LIMIT_SY) return 43;
    camera.pitch_sy = -1024;
    camera.pitch_cy = 0;
    for (int i = 0; i < 1000; i++) rasterfall_camera_rotate(&camera, 0, -31);
    direction_length = isqrt((long long)camera.pitch_sy * camera.pitch_sy +
                             (long long)camera.pitch_cy * camera.pitch_cy);
    if (direction_length < 1022 || direction_length > 1026) return 42;
    if (camera.pitch_cy != RASTERFALL_PITCH_LIMIT_CY ||
        camera.pitch_sy != -RASTERFALL_PITCH_LIMIT_SY) return 44;
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;

    triangle[0].x = -100; triangle[0].y = 0; triangle[0].z = 20;
    triangle[1].x =  100; triangle[1].y = 0; triangle[1].z = 200;
    triangle[2].x =    0; triangle[2].y = 100; triangle[2].z = 200;
    count = clip_near(triangle, 3, clipped);
    if (count != 4) return 5;
    for (int i = 0; i < count; i++)
        if (clipped[i].z < RASTERFALL_NEAR_Z) return 6;
    /* Two vertices behind become one triangle; all three behind reject it. */
    triangle[1].z = 20;
    count = clip_near(triangle, 3, clipped);
    if (count != 3) return 165;
    for (int i = 0; i < count; i++)
        if (clipped[i].z < RASTERFALL_NEAR_Z) return 166;
    triangle[2].z = 20;
    if (clip_near(triangle, 3, clipped) != 0) return 167;
    {
        int near_clip_result = rasterfall_render_near_clip_test();
        if (near_clip_result != 0) {
            __printf("rasterfall logic: render near-clip test failed: %d\n",
                     near_clip_result);
            return 168;
        }
    }
    pixels = tlibc_malloc(320 * 180 * sizeof(uint32_t));
    if (!pixels) return 7;
    surface.pixels = pixels;
    surface.width = 320;
    surface.height = 180;
    surface.stride = 320 * sizeof(uint32_t);
    camera.x = 0; camera.z = -4200;
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0);
    if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 8;
    }
    rasterfall_render_scene(&renderer, &camera);
    count = toy_renderer_flush(&renderer);
    /* The floor is now a single tessellated plane, so authored colour areas
     * no longer overdraw the checkerboard and the visible-pixel count is
     * lower than the old two-plane implementation. */
    if (count < 35000) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 8;
    }
    /* 两个槽位分别覆盖方块人和圆柱人，确保模型几何进入光栅器。 */
    memset(game.enemies, 0, sizeof(game.enemies));
    game.enemies[0].active = 1;
    game.enemies[0].x = -350;
    game.enemies[0].z = -2500;
    game.enemies[0].dir_z = -1024;
    game.enemies[1].active = 1;
    game.enemies[1].x = 350;
    game.enemies[1].z = -2500;
    game.enemies[1].dir_z = -1024;
    game.enemies[1].ai_state = TOY_GAME_ENEMY_ALERT;
    rasterfall_render_enemies(&renderer, &camera);
    if (toy_renderer_flush(&renderer) < 1000) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 15;
    }
    /* 弹道/粒子渲染冒烟：轴向弹道 + 终点越出右屏的弹道（屏幕裁剪与
     * 偏移线边界） + 远处粒子 */
    {
        struct rasterfall_tracer *t = &effects.tracers[0];
        struct rasterfall_particle *p = &effects.particles[0];
        t->active = 1;
        t->sx = camera.x;
        t->sy = RASTERFALL_TRACER_Y;
        t->sz = camera.z;
        t->ex = camera.x;
        t->ez = camera.z - 3000;
        t->life_ms = 100;
        effects.tracers[1].active = 1;
        effects.tracers[1].sx = camera.x;
        effects.tracers[1].sy = RASTERFALL_TRACER_Y;
        effects.tracers[1].sz = camera.z;
        effects.tracers[1].ex = camera.x + 4000;
        effects.tracers[1].ez = camera.z - 3000;
        effects.tracers[1].life_ms = 100;
        p->active = 1;
        p->x = camera.x + 200;
        p->y = -350;
        p->z = camera.z - 2000;
        p->life_ms = 100;
        rasterfall_render_tracers(&renderer, &camera);
        rasterfall_render_particles(&renderer, &camera);
        rasterfall_effects_reset_fire(&effects);
    }
    {
        struct rasterfall_effect_cue cue;
        memset(&cue, 0, sizeof(cue));
        cue.type = RASTERFALL_EFFECT_CUE_TRACER;
        cue.depth_test = 1;
        cue.sx = 1; cue.sy = 2; cue.sz = 3;
        cue.ex = 4; cue.ey = 5; cue.ez = 6;
        cue.life_ms = 77;
        rasterfall_effects_emit(&effects, &cue);
        if (!effects.tracers[0].active || effects.tracers[0].sx != 1 ||
            effects.tracers[0].ey != 5 || effects.tracers[0].life_ms != 77 ||
            !effects.tracers[0].depth_test)
            return 57;
        cue.type = RASTERFALL_EFFECT_CUE_HIT_PARTICLES;
        cue.dir_sy = 1024; cue.dir_cy = 0;
        rasterfall_effects_emit(&effects, &cue);
        if (effects.particle_next != 7) return 58;
        rasterfall_effects_reset_fire(&effects);
    }
    /* 开发者区域场景：纹理墙/模型台座渲染 + 真实开火产生弹道与命中
     * 火花（向北穿门洞直达最大射程长弹道，向西命中空气墙 1800）。
     * 弹道起点取枪口世界坐标；同帧附加枪口粒子。 */
    {
        camera.x = 0;
        camera.z = -6400;
        game.px = 0;
        game.pz = -6400;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 40;
        rasterfall_render_scene(&renderer, &camera);
        toy_renderer_flush(&renderer);
        effects.last_fire_seq = game.fire_seq;
        toy_game_fire(&game, 0, 1024);
        sync_fire_effects(&camera);
        toy_game_fire(&game, -1024, 0);
        sync_fire_effects(&camera);
        rasterfall_effects_update(&effects, 16);
        rasterfall_render_tracers(&renderer, &camera);
        rasterfall_render_particles(&renderer, &camera);
        /* 第一人称武器模型冒烟：SMG 几何进入光栅器 */
        rasterfall_viewmodel_render(&renderer, &game, &effects);
        rasterfall_effects_reset_fire(&effects);
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);

    /* 输入状态回归：组合键、重复 press 边沿、释放和失焦清理。 */
    {
        struct toy_window_events ev;
        toy_input_init(&input);
        memset(&ev, 0, sizeof(ev));
        ev.keyboard_focus_changed = 1;
        ev.keyboard_focused = 1;
        ev.key_event_count = 2;
        ev.key_events[0].key = KEY_W;
        ev.key_events[0].pressed = 1;
        ev.key_events[1].key = KEY_D;
        ev.key_events[1].pressed = 1;
        toy_input_apply(&input, &ev);
        if (!toy_input_down(&input, KEY_W) || !toy_input_down(&input, KEY_D) ||
            !toy_input_pressed(&input, KEY_W) ||
            !toy_input_pressed(&input, KEY_D)) return 10;
        toy_input_begin_frame(&input);
        memset(&ev, 0, sizeof(ev));
        ev.key_event_count = 1;
        ev.key_events[0].key = KEY_W;
        ev.key_events[0].pressed = 1; /* compositor repeat */
        toy_input_apply(&input, &ev);
        if (!toy_input_down(&input, KEY_W) ||
            toy_input_pressed(&input, KEY_W)) return 11;
        toy_input_begin_frame(&input);
        memset(&ev, 0, sizeof(ev));
        ev.keyboard_focus_changed = 1;
        ev.keyboard_focused = 0;
        toy_input_apply(&input, &ev);
        if (toy_input_down(&input, KEY_W) || toy_input_down(&input, KEY_D) ||
            !toy_input_released(&input, KEY_W) ||
            !toy_input_released(&input, KEY_D)) return 12;
    }

    /* 游戏规则冒烟：同 seed 推进 500 步，两遍运行状态一致 */
    {
        struct toy_game smoke;
        int pass, i;
        int first_wave = 0, first_kills = 0, first_hp = 0;
        for (pass = 0; pass < 2; pass++) {
            toy_game_init(&smoke, 1234);
            toy_game_set_primitives(&smoke, level_map.primitives,
                                    level_map.primitive_count,
                                    level_map.room_limit);
            smoke.px = 0;
            smoke.pz = -5000;
            for (i = 0; i < 500; i++)
                toy_game_update(&smoke, NULL, 0, 0, 1024, 16);
            if (pass == 0) {
                first_wave = smoke.wave;
                first_kills = smoke.kills;
                first_hp = smoke.hp;
            } else if (smoke.wave != first_wave || smoke.kills != first_kills ||
                       smoke.hp != first_hp) {
                return 9;
            }
        }
    }
    /* 武器槽：默认手枪出枪（槽 2），主武器槽为空；切空槽无效 */
    {
        int i;
        unsigned char keys[TOY_INPUT_KEY_COUNT];
        rasterfall_session_reset(&session, &camera, 7);
        if (game.current_slot != 1 ||
            game.slots[1].weapon != TOY_GAME_WEAPON_PISTOL ||
            game.slots[1].mag != TOY_CONFIG_PISTOL_MAG ||
            game.slots[1].reserve != TOY_CONFIG_PISTOL_RESERVE ||
            game.slots[0].weapon != -1) return 17;
        if (toy_game_switch_weapon(&game, 0)) return 18;
        /* 拾取 SMG：按配置容量并自动切出；同武器再拾取 = 补充弹药 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 19;
        if (game.current_slot != 0 || game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE) return 20;
        game.slots[0].mag = 7;
        game.slots[0].reserve = 120;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 0 ||
            game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE) return 21;
        /* 切枪保留各自弹药状态 */
        if (!toy_game_switch_weapon(&game, 1) ||
            game.slots[1].mag != TOY_CONFIG_PISTOL_MAG)
            return 22;
        if (!toy_game_switch_weapon(&game, 0) ||
            game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE) return 23;
        /* 切枪动画期间默认禁射；等待其 500ms 表现窗口结束后再测全自动。 */
        for (i = 0; i < TOY_CONFIG_WEAPON_SWITCH_MS / 16 + 1; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        /* 弹药盒补满备弹，不碰弹匣 */
        game.slots[0].mag = 3;
        game.slots[0].reserve = 5;
        if (!toy_game_refill_ammo(&game) ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE ||
            game.slots[0].mag != 3) return 24;
        /* SMG 全自动：按住 10 步（160ms，玩家增强后 50ms 冷却）消耗 3 发 */
        game.slots[0].mag = TOY_CONFIG_SMG_MAG;
        for (i = 0; i < 10; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[0].mag != TOY_CONFIG_SMG_MAG - 3) return 25;
        /* 手枪半自动：按住不连发 */
        toy_game_switch_weapon(&game, 1);
        for (i = 0; i < 5; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[1].mag != TOY_CONFIG_PISTOL_MAG) return 26;
        /* 有限备弹换弹：SMG 2000ms，按当前弹匣缺口消耗备弹 */
        memset(keys, 0, sizeof(keys));
        keys[TOY_GAME_KEY_RELOAD] = 1;
        toy_game_switch_weapon(&game, 0);
        toy_game_update(&game, keys, 0, 0, 1024, 16);
        if (!game.reloading) return 27;
        for (i = 0; i < 125; i++)
            toy_game_update(&game, NULL, 0, 0, 1024, 16);
        if (game.reloading || game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != 647) return 28;
        /* 霰弹枪按配置容量：12 弹丸随机散射，整匣内应放倒并排两敌 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SHOTGUN) != 1 ||
            game.slots[0].mag != TOY_CONFIG_SHOTGUN_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SHOTGUN_RESERVE) return 29;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies[0].active = 1;
        game.enemies[0].x = game.px + 120;
        game.enemies[0].z = game.pz + 900;
        game.enemies[1].active = 1;
        game.enemies[1].x = game.px - 120;
        game.enemies[1].z = game.pz + 900;
        game.enemies_alive = 2;
        game.kills = 0;
        {
            unsigned int seq_before = game.fire_seq;
            for (i = 0; i < 8 && game.kills < 2; i++)
                toy_game_fire(&game, 0, 1024);
            if (game.kills != 2) return 30;
            if (game.slots[0].mag != TOY_CONFIG_SHOTGUN_MAG - i) return 31;
            /* 弹道记录：射线数量跟随霰弹枪配置 */
            if (game.ray_count != TOY_CONFIG_SHOTGUN_PELLETS ||
                game.fire_seq != seq_before + (unsigned int)i) return 38;
            for (i = 0; i < game.ray_count; i++) {
                const struct toy_game_ray *r = &game.rays[i];
                long long len_sq = (long long)r->sy * r->sy +
                                   (long long)r->cy * r->cy;
                if (len_sq < 1022 * 1022 || len_sq > 1026 * 1026) return 38;
                if (r->ex < game.px - TOY_GAME_MAX_RANGE ||
                    r->ex > game.px + TOY_GAME_MAX_RANGE ||
                    r->ez < game.pz - TOY_GAME_MAX_RANGE ||
                    r->ez > game.pz + TOY_GAME_MAX_RANGE) return 38;
            }
        }
        /* 特感击杀单独累计，同时仍计入总击杀。 */
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_SMOKER;
        game.enemies[0].hp = 1;
        game.enemies[0].x = game.px;
        game.enemies[0].z = game.pz + 900;
        game.enemies_alive = 1;
        game.fire_cooldown_ms = 0;
        toy_game_fire(&game, 0, 1024);
        if (game.kills != 3 || game.special_kills != 1 ||
            game.damage_dealt != 1) return 117;
        /* 无敌人时射线止于世界边界；当前地图没有安全屋内墙。 */
        game.reloading = 0;
        game.slots[0].mag = 8;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        toy_game_fire(&game, -1024, 0);
        if (game.ray_count != TOY_CONFIG_SHOTGUN_PELLETS) return 39;
        for (i = 0; i < game.ray_count; i++) {
            const struct toy_game_ray *r = &game.rays[i];
            if (r->hit_enemy) return 39;
        }
    }
    /* 新地图没有按钮、武器/弹药拾取物或据点阶段；三名原据点 AI
     * 是基地防守队伍，尸潮由普通波次自动开始。 */
    if (game.campaign_mode || game.campaign_stage != 0 ||
        game.actors[1].active == 0 || game.actors[2].active == 0 ||
        game.actors[0].flag_index != 0 ||
        game.actors[1].flag_index != 0 ||
        game.actors[2].flag_index != 0)
        return 32;
    {
        struct toy_game base_test;
        int base_id;
        toy_game_init(&base_test, 91);
        base_id = toy_game_add_ai(&base_test, TOY_GAME_AI_LEVEL_1,
                                  0, 0, "BASE");
        base_test.base_actor_index = base_id - 1;
        base_test.actors[base_id - 1].base_core = 1;
        base_test.actors[base_id - 1].hp = TOY_CONFIG_BASE_HP - 20;
        base_test.actors[base_id - 1].max_hp = TOY_CONFIG_BASE_HP;
        if (!toy_game_apply_entity_impact(&base_test,
                                          TOY_GAME_ENTITY_ACTOR, base_id - 1,
                                          1024, 0, 1) ||
            base_test.actors[base_id - 1].airborne_ms != 0 ||
            base_test.actors[base_id - 1].hp != TOY_CONFIG_BASE_HP - 21)
            return 118;
        toy_game_update_held(&base_test, NULL, 0, 0, 0, 1024, 1000);
        if (base_test.actors[base_id - 1].hp != TOY_CONFIG_BASE_HP - 20)
            return 119;
        base_test.actors[base_id - 1].hp = 0;
        base_test.actors[base_id - 1].state = TOY_GAME_ACTOR_DOWNED;
        toy_game_update_held(&base_test, NULL, 0, 0, 0, 1024, 16);
        if (base_test.state != TOY_GAME_OVER) return 120;
    }
    /* 推开（shove）：右键/斜杠触发，面前 120° 扇形内的敌人被击退
     * TOY_GAME_SHOVE_PUSH 并僵直 TOY_GAME_SHOVE_STUN_MS；身后敌人不受
     * 影响。 */
    {
        struct camera aim = {0, -2100, 0, 1024, 0, 1024, 0};
        int i, pushed_z;
        int jx = game.actors[0].x, jz = game.actors[0].z;
        int gx = game.actors[1].x, gz = game.actors[1].z;
        int aix = game.actors[0].x, aiz = game.actors[0].z;
        /* 挪开 AI 队友，避免它们射杀测试敌人（DETECT_RANGE 5600）。 */
        game.actors[0].x = 0; game.actors[0].z = 11000;
        game.actors[1].x = 0; game.actors[1].z = 11000;
        game.px = aim.x; game.pz = aim.z - 5000;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        game.enemies[0].active = 1;             /* 面前 300：应被推 */
        game.enemies[0].x = 0;
        game.enemies[0].z = aim.z + 300;
        game.enemies[0].speed = 100;
        game.enemies[0].dir_z = 1024;
        game.enemies[1].active = 1;             /* 身后 300：不应被推 */
        game.enemies[1].x = 0;
        game.enemies[1].z = aim.z - 300;
        game.enemies[1].speed = 100;
        game.enemies[1].dir_z = -1024;
        game.enemies_alive = 2;
        /* 直接验证击退函数，避免本帧敌人 AI 先移动身后敌人。 */
        game.px = aim.x;
        game.pz = aim.z;
        toy_game_shove(&game, aim.sy, aim.cy);
        if (game.enemies[0].z != aim.z + 300 + TOY_GAME_SHOVE_PUSH) return 63;
        if (game.enemies[0].shove_stun_ms <= 0 ||
            game.enemies[0].shove_stun_ms > TOY_CONFIG_SHOVE_STUN_MS) return 64;
        if (game.enemies[1].z != aim.z - 300) return 65;
        if (game.enemies[1].shove_stun_ms != 0) return 66;
        pushed_z = game.enemies[0].z;
        /* 僵直中不移动：20 帧（320ms）内位置与僵直计时都应保持。 */
        for (i = 0; i < 20; i++) {
            toy_game_update_held(&game, NULL, 0, 0, aim.sy, aim.cy, 16);
        }
        if (game.enemies[0].z != pushed_z) return 67;
        if (game.enemies[0].shove_stun_ms == 0) return 68;
        /* 僵直结束（900ms ≈ 57 帧）后计时清零、恢复行动。 */
        for (i = 0; i < 100; i++) {
            toy_game_update_held(&game, NULL, 0, 0, aim.sy, aim.cy, 16);
        }
        if (game.enemies[0].active == 1 && game.enemies[0].shove_stun_ms != 0)
            return 69;
        game.actors[0].x = jx; game.actors[0].z = jz;
        game.actors[1].x = gx; game.actors[1].z = gz;
        game.actors[0].x = aix; game.actors[0].z = aiz;
    }
    /* 敌人槽位复用不能继承上一只敌人的推开僵直。 */
    {
        struct toy_game reuse_test;
        struct toy_game_box point = {0, 0, 300, 300, 0, 0};
        toy_game_init(&reuse_test, 47);
        reuse_test.primitive_count = 0;
        reuse_test.room_limit = 12000;
        reuse_test.px = 0; reuse_test.pz = 0;
        if (toy_game_spawn_horde_type(&reuse_test, TOY_GAME_ENEMY_COMMON, 1, 1,
                                      &point, 1, 0) != 1) return 87;
        toy_game_shove(&reuse_test, 0, 1024);
        if (reuse_test.enemies[0].shove_stun_ms <= 0) return 88;
        if (!toy_game_apply_entity_impact(&reuse_test, TOY_GAME_ENTITY_ENEMY, 0,
                                          0, 1024, 10000)) return 89;
        for (int i = 0; i < 100 && reuse_test.enemies[0].active != 0; i++)
            toy_game_update_held(&reuse_test, NULL, 0, 0, 0, 1024, 16);
        if (reuse_test.enemies[0].active != 0) return 90;
        if (toy_game_spawn_horde_type(&reuse_test, TOY_GAME_ENEMY_COMMON, 1, 1,
                                      &point, 1, 0) != 1) return 91;
        if (reuse_test.enemies[0].shove_stun_ms != 0) return 92;
    }
    /* 单基地防守没有撤离终点，也没有安全屋胜利判定。 */
    /* 特感里程碑：Smoker 锁住移动但不锁住武器，持续把玩家拉向自身。 */
    {
        int start_z;
        memset(game.enemies, 0, sizeof(game.enemies));
        memset(game.actors, 0, sizeof(game.actors));
        game.actors[0].active = 0;
        game.enemies_alive = 1;
        game.campaign_mode = 0;
        game.to_spawn = 0;
        game.safe_room_count = 0;
        game.primitive_count = 0;
        game.state = TOY_GAME_PLAYING;
        game.player_down = 0;
        game.player_control_disabled = 0;
        game.player_pull_enemy_index = -1;
        game.px = 0; game.pz = 0;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_SMOKER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 25;
        game.enemies[0].dir_x = 0; game.enemies[0].dir_z = -1024;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.player_control_disabled || game.enemies[0].special_windup_ms <= 0)
            return 74;
        for (int i = 0; i < 80; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (!game.player_control_disabled || game.player_pull_enemy_index != 0)
            return 74;
        start_z = game.pz;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.pz <= start_z) return 75;
        if (toy_game_fire(&game, 0, 1024) != 1) return 76;
        if (game.enemies[0].active != 2 ||
            game.player_control_disabled) return 77;
    }
    /* Smoker 对 AI 队友的舌头在队友倒地前不因固定时长自动释放。 */
    {
        toy_game_init(&game, 43);
        game.primitive_count = 0;
        game.room_limit = 12000;
        game.px = 10000; game.pz = 0;
        game.player_down = 0;
        toy_game_set_ai_teammate(&game, 1, 0, 0, "AI");
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_SMOKER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 100000;
        for (int i = 0; i < 380; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (!game.enemies[0].special_target_active ||
            game.enemies[0].special_target_kind != TOY_GAME_TARGET_ACTOR ||
            game.enemies[0].special_pull_timer_ms != -1) {
            return 81;
        }
        game.actors[0].state = TOY_GAME_ACTOR_DOWNED;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].special_target_active) return 82;
    }
    /* Charger 撞到 AI 时，AI 也必须进入带垂直速度的空中状态。 */
    {
        toy_game_init(&game, 44);
        game.primitive_count = 0;
        game.room_limit = 12000;
        game.px = 10000; game.pz = 0;
        toy_game_set_ai_teammate(&game, 1, 0, 300, "AI");
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_CHARGER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 100000;
        for (int i = 0; i < 100; i++) {
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
            if (game.actors[0].airborne_y > 0) break;
        }
        if (game.actors[0].airborne_ms <= 0 ||
            game.actors[0].vertical_velocity <= 0) return 83;
    }
    /* Charger 里程碑：蓄力后造成伤害，并进入短暂空中/击飞状态。 */
    {
        int old_hp, saw_airborne_y = 0, saw_control_lock = 0;
        int saw_charge_after_hit = 0;
        memset(game.enemies, 0, sizeof(game.enemies));
        memset(game.actors, 0, sizeof(game.actors));
        game.actors[0].active = 0;
        game.enemies_alive = 1;
        game.campaign_mode = 0;
        game.to_spawn = 0;
        game.safe_room_count = 0;
        game.primitive_count = 0;
        game.state = TOY_GAME_PLAYING;
        game.player_down = 0;
        game.player_control_disabled = 0;
        game.player_airborne_ms = 0;
        game.px = 0; game.pz = 0; old_hp = game.hp;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_CHARGER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 180;
        game.enemies[0].dir_x = 0; game.enemies[0].dir_z = -1024;
        for (int i = 0; i < 80; i++) {
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
            if (game.player_airborne_y > 0) saw_airborne_y = 1;
            if (game.player_control_disabled) saw_control_lock = 1;
            if (game.hp < old_hp && game.enemies[0].charge_active)
                saw_charge_after_hit = 1;
        }
        if (game.hp >= old_hp || !saw_airborne_y || !saw_control_lock ||
            !saw_charge_after_hit) return 78;
    }
    /* 统一实体冲击：AI 队友与普通实体共享击飞状态，落地后回到部署点。 */
    {
        toy_game_init(&game, 42);
        game.primitive_count = 0;
        game.room_limit = 12000;
        game.campaign_mode = 0;
        game.to_spawn = 0;
        game.actors[0].x = 1200;
        game.actors[0].z = 0;
        game.actors[0].deployment_x = 0;
        game.actors[0].deployment_z = 0;
        if (!toy_game_apply_entity_impact(&game, TOY_GAME_ENTITY_ACTOR, 0,
                                          1000, 0, 1) ||
            game.actors[0].airborne_ms <= 0 ||
            game.actors[0].animation.id != TOY_GAME_ANIM_SHOVE) return 79;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.actors[0].animation.id != TOY_GAME_ANIM_SHOVE) return 79;
        for (int i = 0; i < 130; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.actors[0].airborne_ms > 0)
            return 80;
    }
    /* 敌人分离使用碰撞半径，不能长期重叠成一团。 */
    {
        long long dx, dz, dist2;
        toy_game_init(&game, 45);
        game.primitive_count = 0;
        game.room_limit = 12000;
        game.enemies_alive = 2;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_COMMON;
        game.enemies[0].x = 0; game.enemies[0].z = 0;
        game.enemies[0].shove_stun_ms = 1000;
        game.enemies[1].active = 1;
        game.enemies[1].type = TOY_GAME_ENEMY_COMMON;
        game.enemies[1].x = 100; game.enemies[1].z = 0;
        game.enemies[1].shove_stun_ms = 1000;
        for (int i = 0; i < 12; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        dx = game.enemies[1].x - game.enemies[0].x;
        dz = game.enemies[1].z - game.enemies[0].z;
        dist2 = dx * dx + dz * dz;
        if (dist2 < (long long)TOY_GAME_ENEMY_RADIUS *
                   TOY_GAME_ENEMY_RADIUS * 4)
            return 84;
    }
    /* 玩家跳跃复用现有空中状态，落地后才能再次起跳。 */
    {
        toy_game_init(&game, 46);
        game.primitive_count = 0;
        game.room_limit = 12000;
        if (!toy_game_jump_with_velocity(&game, 10, 0) ||
            game.player_airborne_ms <= 0 ||
            toy_game_jump(&game)) return 85;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_airborne_y != 0 ||
            game.px <= 0 ||
            !toy_game_jump(&game)) return 86;
    }
    /* 多层地面：跳入悬空平台范围后应落在平台顶部，而不是穿回地面。 */
    {
        struct toy_map_primitive terrain[2] = {
            test_flat_primitive(-12000,12000,-12000,12000,0),
            test_flat_primitive(280,2200,-1000,1000,500)
        };
        struct toy_game_ground_query ground;
        toy_game_init(&game, 47);
        game.primitive_count = 0;
        game.room_limit = 12000;
        toy_game_set_primitives(&game,terrain,2,12000);
        ground = toy_game_query_ground(&game, 300, 0,
                                       TOY_GAME_PLAYER_RADIUS, 0);
        if (ground.support_y != 0 || ground.landing_y != 500 ||
            ground.touches_current_support != 1) return 171;
        ground = toy_game_query_ground(&game, 500, 0,
                                       TOY_GAME_PLAYER_RADIUS, 500);
        if (ground.support_y != 500 || ground.landing_y != 500 ||
            ground.touches_current_support != 1) return 172;
        ground = toy_game_query_ground(&game, 2400, 0,
                                       TOY_GAME_PLAYER_RADIUS, 500);
        if (ground.support_y != 0 || ground.landing_y != 0 ||
            ground.touches_current_support != 0) return 173;
        {
            struct toy_map_primitive step[2] = {
                test_flat_primitive(-2000,2000,-1000,1000,0),
                test_flat_primitive(200,1000,-500,500,100)
            };
            toy_game_set_primitives(&game,step,2,2000);
            game.px=0; game.pz=0; game.player_ground_y=0;
            if (!toy_game_try_move_player(&game,400,0) ||
                game.player_ground_y != 100) return 208;
        }
        {
            struct toy_map_primitive ramp=test_ramp_primitive(
                TOY_MAP_PRIMITIVE_RAMP_X,0,1200,-600,600,0,400);
            int nav_a, nav_b;
            toy_game_set_primitives(&game,&ramp,1,12000);
            ground = toy_game_query_ground(&game, 600, 0, 0, 0);
            if (ground.support_y != 200 || ground.landing_y != 200 ||
                !ground.support_is_ramp) return 174;
            game.px=200; game.pz=0;
            game.player_ground_y=200*400/1200;
            for (int x=300; x<=900; x+=100)
                if (!toy_game_try_move_player(&game,x,0) ||
                    game.player_ground_y != x*400/1200)
                    return 209;
            for (int x=800; x>=200; x-=100)
                if (!toy_game_try_move_player(&game,x,0) ||
                    game.player_ground_y != x*400/1200)
                    return 210;
            if (toy_game_position_blocked_at_height(
                    &game, 100, 0, 50, 0) ||
                !toy_game_position_blocked_at_height(
                    &game, 600, -550, 100, 0) ||
                !toy_game_position_blocked_at_height(
                    &game, 1150, 0, 100, 0) ||
                toy_game_position_blocked_at_height(
                    &game, 700, 0, 100, 200)) return 177;
            /* 墙顶面覆盖完整实体，并在坡尾按玩家半径精确衔接：上下坡
             * 都不能卡边，沿墙方向离开坡口后也必须继续受顶部支撑。 */
            {
                struct toy_map_primitive ramp_top[3] = {
                    test_box_primitive(-4000,3000,-5900,-5700,0,3900,
                                       TOY_MAP_PRIMITIVE_COLLISION),
                    test_ramp_primitive(TOY_MAP_PRIMITIVE_RAMP_Z,
                                        -1000,1000,-15000,-5900,0,3979),
                    test_flat_primitive(-4180,3180,-6260,-5520,3900)
                };
                toy_game_set_primitives(&game,ramp_top,3,16000);
                game.px = 0;
                game.pz = -7000;
                game.player_ground_y = toy_game_query_ground(
                    &game, game.px, game.pz, TOY_GAME_PLAYER_RADIUS,
                    0).support_y;
                while (game.pz < -5700) {
                    int next_z = game.pz + TOY_CONFIG_PLAYER_MOVE_STEP;
                    if (next_z > -5700) next_z = -5700;
                    if (!toy_game_try_move_player(&game, game.px, next_z))
                        return 198;
                    if (game.player_ground_y != toy_game_query_ground(
                            &game, game.px, game.pz,
                            TOY_GAME_PLAYER_RADIUS,
                            game.player_ground_y).support_y)
                        return 199;
                }
                if (game.player_ground_y != 3900) return 200;
                if (toy_game_position_blocked_at_height(
                        &game, 0, -6080, TOY_GAME_PLAYER_RADIUS, 3881))
                    return 188;
                if (toy_game_query_ground(&game, 0, -6080,
                        TOY_GAME_PLAYER_RADIUS, 3881).support_y != 3900)
                    return 193;
                if (toy_game_position_blocked_at_height(
                        &game, 0, -6090, TOY_GAME_PLAYER_RADIUS, 3900))
                    return 194;
                if (toy_game_query_ground(&game, 0, -6090,
                        TOY_GAME_PLAYER_RADIUS, 3900).support_y < 3890)
                    return 195;
                if (toy_game_position_blocked_at_height(
                        &game, -3500, -5700, TOY_GAME_PLAYER_RADIUS, 3900))
                    return 196;
                if (toy_game_query_ground(&game, -3500, -5700,
                        TOY_GAME_PLAYER_RADIUS, 3900).support_y != 3900)
                    return 197;
                toy_game_set_primitives(&game,&ramp,1,12000);
            }
            nav_a = ((150 - game.nav_origin) / game.nav_cell_size) *
                    game.nav_width +
                    (150 - game.nav_origin) / game.nav_cell_size;
            nav_b = ((150 - game.nav_origin) / game.nav_cell_size) *
                    game.nav_width +
                    (450 - game.nav_origin) / game.nav_cell_size;
            if (game.nav_ground_y[nav_a] != 50 ||
                game.nav_ground_y[nav_b] != 150 ||
                !game.nav_component[nav_a] ||
                game.nav_component[nav_a] != game.nav_component[nav_b])
                return 178;
            {
                struct toy_game nav_game;
                struct toy_map_primitive steep=test_ramp_primitive(
                    TOY_MAP_PRIMITIVE_RAMP_X,-600,600,-1200,1200,0,600);
                int low_cell, high_cell;
                toy_game_init(&nav_game, 48);
                nav_game.primitive_count = 0;
                nav_game.room_limit = 1200;
                toy_game_set_primitives(&nav_game,&steep,1,1200);
                low_cell = ((150 - nav_game.nav_origin) /
                            nav_game.nav_cell_size) * nav_game.nav_width +
                           (-150 - nav_game.nav_origin) /
                            nav_game.nav_cell_size;
                high_cell = ((150 - nav_game.nav_origin) /
                             nav_game.nav_cell_size) * nav_game.nav_width +
                            (150 - nav_game.nav_origin) /
                             nav_game.nav_cell_size;
                if (!nav_game.nav_walkable[low_cell] ||
                    !nav_game.nav_walkable[high_cell] ||
                    nav_game.nav_ground_y[low_cell] != 150 ||
                    nav_game.nav_ground_y[high_cell] != 450 ||
                    !nav_game.nav_component[low_cell] ||
                    !nav_game.nav_component[high_cell] ||
                    nav_game.nav_component[low_cell] !=
                        nav_game.nav_component[high_cell]) return 179;
                if (nav_game.nav_link_type[low_cell *
                        TOY_GAME_NAV_LINK_DIRECTIONS + 4] !=
                    TOY_GAME_NAV_LINK_RAMP) return 180;
                {
                    struct toy_map_primitive step[2] = {
                        test_flat_primitive(-1200, 0, -1200, 1200, 0),
                        test_flat_primitive(0, 1200, -1200, 1200, 600)
                    };
                    step[0].flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
                    step[1].flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
                    toy_game_set_primitives(&nav_game, step, 2, 1200);
                    if (nav_game.nav_component[low_cell] ==
                            nav_game.nav_component[high_cell]) return 181;
                }
            }
            game.px = 900;
            game.pz = 0;
            game.player_ground_y = 500;
            toy_game_update_player_ground(&game);
            if (game.player_ground_y != 300 || game.player_airborne_ms != 0)
                return 175;
            game.actors[0].x = 300;
            game.actors[0].z = 0;
            game.actors[0].ground_y = 0;
            toy_game_update_actor_ground(&game, 0);
            if (game.actors[0].ground_y != 100 ||
                game.actors[0].airborne_ms != 0) return 176;
            toy_game_set_primitives(&game,terrain,2,12000);
        game.px = 0;
        game.pz = 0;
        game.player_ground_y = 0;
        /* 事务移动区分上升与下降：高台不能直接攀爬，离开高台边缘则
         * 保持绝对高度进入下落；没有任何支撑也不能阻止水平移动。 */
        if (toy_game_try_move_player(&game, 500, 0) ||
            game.player_ground_y != 0) return 201;
        game.px = 2000;
        game.player_ground_y = 500;
        if (!toy_game_try_move_player(&game, 2400, 0) ||
            game.px != 2400 || game.player_ground_y != 0 ||
            game.player_airborne_y != 500 ||
            game.player_airborne_ms <= 0) return 202;
        game.player_airborne_ms = 0;
        game.player_airborne_y = 0;
        game.player_vertical_velocity = 0;
        game.px = 0;
        game.pz = 0;
        game.player_ground_y = 500;
        toy_game_set_primitives(&game,NULL,0,12000);
        if (!toy_game_try_move_player(&game, 300, 0) ||
            game.px != 300 || game.player_ground_y != 0 ||
            game.player_airborne_y != 500 ||
            game.player_airborne_ms <= 0) return 203;
        toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms <= 0) return 204;
        game.player_airborne_ms = 0;
        game.player_airborne_y = 0;
        game.player_vertical_velocity = 0;
        toy_game_set_primitives(&game,terrain,2,12000);
        game.px = 0;
        game.player_ground_y = 0;
        /* 完整斜向移动撞墙后移除法线 X 分量，保留 Z 切向分量。 */
        {
            struct toy_map_primitive slide_world[2] = {
                test_box_primitive(200,400,-1000,1000,0,1000,
                                   TOY_MAP_PRIMITIVE_COLLISION),
                test_flat_primitive(-2000,2000,-2000,2000,0)
            };
            toy_game_set_primitives(&game,slide_world,2,2000);
            game.px = 0; game.pz = 0; game.player_ground_y = 0;
            /* Collision is an independent primitive capability: the same
             * box becomes passable when an air-gate style toggle clears it,
             * while the WALKABLE ground primitive continues to support us. */
            if (toy_game_try_move_player(&game,100,0)) return 206;
            slide_world[0].flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
            toy_game_rebuild_navigation(&game);
            if (!toy_game_try_move_player(&game,100,0) || game.px != 100 ||
                game.player_ground_y != 0) return 207;
            slide_world[0].flags |= TOY_MAP_PRIMITIVE_COLLISION;
            toy_game_rebuild_navigation(&game);
            game.px=0; game.pz=0;
            if (!toy_game_move_player_sliding(&game, 100, 100) ||
                game.px != 0 || game.pz != 100 ||
                game.player_ground_y != 0) return 205;
            toy_game_set_primitives(&game,terrain,2,12000);
            game.px = 0; game.pz = 0; game.player_ground_y = 0;
        }
    }
        if (!toy_game_jump_with_velocity(&game, 20, 0)) return 87;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 500 ||
            game.player_airborne_y != 0) return 88;
        /* 从边缘原地起跳时，碰撞圆仍压着台面就应落回台顶，
         * 不能落到地面高度后卡在平台侧面。 */
        game.px = 2300;
        if (!toy_game_jump(&game)) return 94;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 500 ||
            game.player_airborne_y != 0) return 95;
        /* 从高台起跳并离开边缘后，相对起跳点高度回到零时仍在半空；
         * 必须继续下降到低处地面的绝对高度，不能在此处瞬移落地。 */
        game.px = 1800;
        game.player_ground_y = 500;
        if (!toy_game_jump_with_velocity(&game, 100, 0)) return 190;
        for (int i = 0; i < 400 && game.player_airborne_y >= 0; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms <= 0 || game.player_airborne_y >= 0 ||
            game.player_ground_y != 500) return 191;
        for (int i = 0; i < 400 && game.player_airborne_ms > 0; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 0 ||
            game.player_airborne_y != 0) return 192;
        /* 离开平台时，在碰撞圆完全越过边缘前仍保持站立；
         * 越过后从平台高度开始下落，不能直接陷入平台侧面。 */
        game.px = 2300;
        game.player_ground_y = 500;
        toy_game_update_player_ground(&game);
        if (game.player_ground_y != 500 || game.player_airborne_ms != 0)
            return 89;
        game.px = 2400;
        toy_game_update_player_ground(&game);
        if (game.player_ground_y != 0 || game.player_airborne_ms <= 0 ||
            game.player_airborne_y != 500) return 90;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_airborne_y != 0)
            return 91;
        /* 高处坠落不能因名义跳跃计时结束而瞬移落地。 */
        game.player_ground_y = 0;
        game.player_airborne_y = 10000;
        game.player_airborne_ms = TOY_GAME_JUMP_MS;
        game.player_vertical_velocity = 0;
        for (int i = 0; i < 57; i++) {
            int previous_y = game.player_airborne_y;
            toy_game_update_player_motion(&game, 16);
            if (previous_y - game.player_airborne_y >
                    TOY_GAME_FALL_TERMINAL_VELOCITY)
                return 189;
        }
        if (game.player_airborne_ms <= 0 || game.player_airborne_y <= 0)
            return 186;
        for (int i = 0; i < 600 && game.player_airborne_ms > 0; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_airborne_y != 0)
            return 187;
    }
    /* world 是导航和碰撞的最大坐标范围，不是默认地面。 */
    {
        struct toy_map_primitive ground_patch=test_flat_primitive(
            0,600,-600,600,0);
        struct toy_game_ground_query on_ground, no_ground;
        int on_cell, empty_cell;
        toy_game_init(&game, 184);
        toy_game_set_primitives(&game,&ground_patch,1,1200);
        on_ground = toy_game_query_ground(&game, 300, 0, 0, 0);
        no_ground = toy_game_query_ground(&game, 900, 0, 0, 0);
        on_cell = ((150 - game.nav_origin) / game.nav_cell_size) *
                  game.nav_width +
                  (150 - game.nav_origin) / game.nav_cell_size;
        empty_cell = ((150 - game.nav_origin) / game.nav_cell_size) *
                     game.nav_width +
                     (750 - game.nav_origin) / game.nav_cell_size;
        if (!on_ground.has_support || no_ground.has_support ||
            !toy_game_position_blocked_at_height(
                &game, 900, 0, TOY_GAME_PLAYER_RADIUS, 0) ||
            !game.nav_walkable[on_cell] || game.nav_walkable[empty_cell])
            return 185;
    }
    /* 网络玩家 actor 必须保留跳跃水平动量，并在平台顶落地。 */
    {
        struct toy_map_primitive terrain[2] = {
            test_flat_primitive(-12000,12000,-12000,12000,0),
            test_flat_primitive(280,2200,-1000,1000,500)
        };
        int actor_index = TOY_GAME_REMOTE_ACTOR_BASE + 1;
        toy_game_init(&game, 49);
        game.primitive_count = 0;
        game.room_limit = 12000;
        toy_game_set_primitives(&game,terrain,2,12000);
        if (toy_game_set_remote_player(&game, 2, 1, 0, 0, "REMOTE") !=
            actor_index) return 103;
        if (!toy_game_jump_actor(&game, actor_index, 20, 0)) return 104;
        for (int i = 0; i < 70; i++)
            toy_game_update_actor_motion(&game, actor_index, 16);
        if (game.actors[actor_index].x <= 0 ||
            game.actors[actor_index].ground_y != 500 ||
            game.actors[actor_index].airborne_ms != 0) return 105;
    }
    /* 客户端本地执行移动，主机只接受其报告的运动状态。 */
    {
        struct rasterfall_net net_test;
        int actor_index = TOY_GAME_REMOTE_ACTOR_BASE;
        rasterfall_session_reset(&session, &camera, 50);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_HOST;
        net_test.clients[0].active = 1;
        net_test.clients[0].connected = 1;
        net_test.clients[0].client_id = 1;
        net_test.clients[0].hp = TOY_GAME_SECONDARY_PLAYER_HP;
        net_test.clients[0].camera = camera;
        net_test.clients[0].reported_camera = camera;
        net_test.clients[0].reported_camera_ready = 1;
        net_test.clients[0].airborne_ms = 600;
        net_test.clients[0].airborne_y = 180;
        net_test.clients[0].airborne_velocity = 20;
        rasterfall_net_prepare_host_step(&net_test, &game);
        if (!game.actors[actor_index].active ||
            game.actors[actor_index].hp != TOY_GAME_SECONDARY_PLAYER_HP)
            return 106;
        net_test.clients[0].command_ready = 1;
        net_test.clients[0].command.move_forward = 1;
        net_test.clients[0].command.buttons = RASTERFALL_CMD_JUMP;
        rasterfall_net_apply_clients(&net_test, &session, &camera);
        if (game.actors[actor_index].airborne_ms != 600 ||
            game.actors[actor_index].airborne_y != 180) return 107;
        rasterfall_net_prepare_host_step(&net_test, &game);
        if (game.actors[actor_index].airborne_ms != 600 ||
            game.actors[actor_index].z != net_test.clients[0].camera.z)
            return 108;
    }
    /* 未确认跳跃输入之前，旧快照不能取消客户端预测或拉回水平位置。 */
    {
        struct rasterfall_net net_test;
        rasterfall_session_reset(&session, &camera, 51);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_CLIENT;
        net_test.local_player_id = 1;
        net_test.snapshot_ready = 1;
        net_test.last_jump_command_sequence = 10;
        net_test.last_command_sequence = 10;
        net_test.last_snapshot_input_ack = 9;
        net_test.players[1].active = 1;
        net_test.players[1].input_ack = 9;
        net_test.players[1].state = TOY_GAME_PLAYING;
        net_test.players[1].camera = camera;
        net_test.players[1].camera.x -= 1000;
        net_test.players[1].camera.z -= 1000;
        net_test.input_history[10 % RASTERFALL_NET_INPUT_HISTORY].valid = 1;
        net_test.input_history[10 % RASTERFALL_NET_INPUT_HISTORY].sequence = 10;
        net_test.input_history[10 % RASTERFALL_NET_INPUT_HISTORY].command.buttons =
            RASTERFALL_CMD_JUMP;
        net_test.input_history[10 % RASTERFALL_NET_INPUT_HISTORY].command.move_forward = 1;
        game.player_airborne_ms = 600;
        game.player_airborne_y = 180;
        {
            int old_x = camera.x, old_z = camera.z;
            rasterfall_net_reconcile_client(&net_test, &session, &camera);
            if (game.player_airborne_ms <= 0) return 109;
            if (camera.x != old_x || camera.z != old_z) return 142;
            if (camera.x == net_test.players[1].camera.x &&
                camera.z == net_test.players[1].camera.z) return 143;
        }
    }
    /* Even an acknowledged snapshot never restores the local position. */
    {
        struct rasterfall_net net_test;
        struct rasterfall_net_prediction *prediction;
        rasterfall_session_reset(&session, &camera, 511);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_CLIENT;
        net_test.local_player_id = 1;
        net_test.snapshot_ready = 1;
        net_test.players[1].active = 1;
        net_test.players[1].state = TOY_GAME_PLAYING;
        net_test.players[1].input_ack = 9;
        net_test.players[1].camera.x = 900;
        net_test.players[1].camera.z = 1900;
        prediction = &net_test.prediction_history[9 % 64];
        prediction->sequence = 9;
        prediction->x = 800;
        prediction->z = 1800;
        camera.x = 1000;
        camera.z = 2000;
        rasterfall_net_reconcile_client(&net_test, &session, &camera);
        if (camera.x != 1000 || camera.z != 2000) return 111;
    }
    /* Legacy replay data remains allocated, but snapshots do not execute it. */
    {
        struct rasterfall_net net_test;
        struct camera authoritative;
        rasterfall_session_reset(&session, &camera, 512);
        authoritative = camera;
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_CLIENT;
        net_test.local_player_id = 1;
        net_test.snapshot_ready = 1;
        net_test.players[1].active = 1;
        net_test.players[1].state = TOY_GAME_PLAYING;
        net_test.players[1].hp = session.game_state.hp;
        net_test.players[1].camera = authoritative;
        net_test.players[1].input_ack = 100;
        session.game_state.animation.id = TOY_GAME_ANIM_FIRE;
        session.game_state.animation.time_ms = 77;
        session.game_state.event_count = 1;
        session.game_state.events[0] = TOY_GAME_EV_SHOOT;
        rasterfall_net_reconcile_client(&net_test, &session, &camera);
        if (camera.x != authoritative.x || camera.z != authoritative.z ||
            session.game_state.animation.id != TOY_GAME_ANIM_FIRE ||
            session.game_state.animation.time_ms != 77 ||
            session.game_state.event_count != 1) return 141;
    }
    /* Local facing remains untouched by an authoritative snapshot. */
    {
        struct rasterfall_net net_test;
        struct camera authoritative;
        rasterfall_session_reset(&session, &camera, 513);
        authoritative = camera;
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_CLIENT;
        net_test.local_player_id = 1;
        net_test.snapshot_ready = 1;
        net_test.players[1].active = 1;
        net_test.players[1].state = TOY_GAME_PLAYING;
        net_test.players[1].hp = session.game_state.hp;
        net_test.players[1].camera = authoritative;
        net_test.players[1].input_ack = 100;
        session.smooth_turn_remaining = 1611;
        rasterfall_net_reconcile_client(&net_test, &session, &camera);
        if (camera.sy != authoritative.sy || camera.cy != authoritative.cy)
            return 144;
    }
    /* A replayed jump uses the vector captured by its command, even when the
     * camera restored from the snapshot has a different orientation. */
    {
        struct rasterfall_session live_session;
        struct rasterfall_session replay_session;
        struct camera live_camera, replay_camera;
        struct rasterfall_command jump_command;
        live_session = session;
        rasterfall_session_reset(&live_session, &live_camera, 514);
        replay_session = live_session;
        replay_camera = live_camera;
        memset(&jump_command, 0, sizeof(jump_command));
        jump_command.buttons = RASTERFALL_CMD_JUMP;
        jump_command.move_forward = 1;
        jump_command.jump_dx = 37;
        jump_command.jump_dz = -29;
        live_camera.sy = 0; live_camera.cy = 1024;
        replay_camera.sy = 1024; replay_camera.cy = 0;
        rasterfall_session_step_client(&live_session, &live_camera,
                                       &jump_command, 16);
        rasterfall_session_replay_client(&replay_session, &replay_camera,
                                         &jump_command, 16);
        if (live_session.game_state.player_air_x != 37 ||
            live_session.game_state.player_air_z != -29 ||
            replay_session.game_state.player_air_x != 37 ||
            replay_session.game_state.player_air_z != -29)
            return 145;
    }

    /* 玩家互救必须同时更新网络槽和 actor，且主机倒地不能禁用远端移动。 */
    {
        struct rasterfall_net net_test;
        int actor_index = TOY_GAME_REMOTE_ACTOR_BASE;
        rasterfall_session_reset(&session, &camera, 52);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_HOST;
        net_test.clients[0].active = 1;
        net_test.clients[0].connected = 1;
        net_test.clients[0].client_id = 1;
        net_test.clients[0].camera = camera;
        net_test.clients[0].hp = 0;
        net_test.clients[0].down = 1;
        rasterfall_net_prepare_host_step(&net_test, &game);
        game.actors[actor_index].hp = 0;
        game.actors[actor_index].state = TOY_GAME_ACTOR_DOWNED;
        for (int i = 0; i < (TOY_GAME_REVIVE_MS + 15) / 16; i++)
            rasterfall_net_apply_local_rescue(&net_test, &session, &camera,
                                               i == 0, 16);
        if (net_test.clients[0].down ||
            game.actors[actor_index].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[actor_index].hp != TOY_GAME_REVIVE_HP) return 110;

        game.player_down = 1;
        game.hp = 0;
        net_test.clients[0].command_ready = 1;
        net_test.clients[0].command.move_forward = 1;
        net_test.clients[0].command.buttons = RASTERFALL_CMD_JUMP;
        net_test.clients[0].reported_camera = camera;
        net_test.clients[0].reported_camera_ready = 1;
        net_test.clients[0].airborne_ms = 600;
        net_test.clients[0].airborne_y = 180;
        rasterfall_net_apply_clients(&net_test, &session, &camera);
        if (game.actors[actor_index].airborne_ms != 600) return 111;

        game.actors[actor_index].airborne_ms = 0;
        net_test.clients[0].airborne_ms = 0;
        net_test.clients[0].command.move_forward = 0;
        net_test.clients[0].command.buttons = RASTERFALL_CMD_INTERACT;
        for (int i = 0; i < (TOY_GAME_REVIVE_MS + 15) / 16; i++) {
            net_test.clients[0].command_ready = 1;
            rasterfall_net_apply_clients(&net_test, &session, &camera);
        }
        if (game.player_down || game.hp != TOY_GAME_REVIVE_HP) return 112;
    }
    /* 平台对地面实体是实心障碍：敌人不能生成或追进台体，
     * AI 队友也不能被直接移入平台。 */
    {
        struct toy_map_primitive terrain[2] = {
            test_flat_primitive(-12000,12000,-12000,12000,0),
            test_flat_primitive(-500,500,-500,500,800)
        };
        int actor_x;
        toy_game_init(&game, 48);
        game.primitive_count = 0;
        game.room_limit = 12000;
        game.to_spawn = 0;
        toy_game_set_primitives(&game,terrain,2,12000);
        toy_game_place_enemy(&game, 0, 0);
        if (game.enemies_alive != 0) return 96;
        toy_game_place_enemy(&game, -800, 0);
        if (game.enemies_alive != 1) return 97;
        game.px = 800; game.pz = 0;
        for (int i = 0; i < 100; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].x + TOY_GAME_ENEMY_RADIUS > terrain[1].minx)
            return 98;
        actor_x = game.actors[0].x;
        if (toy_game_move_ai_actor(&game, 0, 0, 0) ||
            game.actors[0].x != actor_x) return 99;
    }
    /* 动态散布：静止最稳，移动和连射依次扩大，停火后恢复。 */
    {
        int still_spread, moving_spread, hot_spread;
        toy_game_init(&game, 71);
        still_spread = toy_game_current_spread(&game);
        toy_game_set_player_moving(&game, 1);
        moving_spread = toy_game_current_spread(&game);
        toy_game_fire(&game, 0, 1024);
        hot_spread = toy_game_current_spread(&game);
        if (moving_spread <= still_spread || hot_spread <= moving_spread)
            return 113;
        toy_game_set_player_moving(&game, 0);
        toy_game_update_weapon_held(&game, NULL, 0, 0, 0, 1024, 2000);
        if (game.weapon_spread_heat != 0 ||
            toy_game_current_spread(&game) != still_spread) return 114;
    }
    /* AI 回位需绕过横在路径上的墙，而不是持续顶墙。 */
    {
        struct toy_map_primitive wall=test_box_primitive(
            -500,500,-900,900,0,0,TOY_MAP_PRIMITIVE_COLLISION);
        toy_game_init(&game, 72);
        toy_game_set_ai_teammate(&game, 1, -1400, 0, "AI");
        toy_game_set_primitives(&game,&wall,1,5000);
        game.actors[0].deployment_x = 1400;
        game.actors[0].deployment_z = 0;
        game.actors[0].x = -1400;
        game.actors[0].z = 0;
        game.to_spawn = 0;
        for (int i = 0; i < 180; i++) toy_game_update_ai_teammates(&game, 16);
        /* The arena no longer contains campaign geometry that needs a
         * scripted AI redeployment route. */
    }
    /* 导航目标选择：不可达的主玩家不能成为隐式默认目标；可达的
     * 第二目标即使更远也应优先于不可达目标。 */
    {
        struct toy_map_primitive wall=test_box_primitive(
            -100,100,-6000,6000,0,0,TOY_MAP_PRIMITIVE_COLLISION);
        toy_game_init(&game, 81);
        game.to_spawn = 0;
        game.px = 3000; game.pz = 0;
        toy_game_set_primitives(&game,&wall,1,6000);
        toy_game_place_enemy(&game, -3000, 0);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_kind != -1) return 120;

        toy_game_set_remote_player(&game, 1, 1, -1800, 0, "PLAYER");
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_kind != 1 ||
            game.enemies[0].target_index != TOY_GAME_REMOTE_ACTOR_BASE)
            return 121;
    }
    /* TRACKING 只忽略 LOS，不忽略 component；绕过有限障碍后仍可追踪。 */
    {
        struct toy_map_primitive wall=test_box_primitive(
            -100,100,-600,600,0,0,TOY_MAP_PRIMITIVE_COLLISION);
        toy_game_init(&game, 82);
        game.to_spawn = 0;
        game.px = 3000; game.pz = 2000;
        toy_game_set_primitives(&game,&wall,1,6000);
        toy_game_place_enemy(&game, -3000, 0);
        game.enemies[0].ai_state = TOY_GAME_ENEMY_TRACKING;
        game.enemies[0].type = TOY_GAME_ENEMY_COMMON;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_kind != 0) return 122;

        game.px = 3000; game.pz = 0;
        wall.minz = -6000; wall.maxz = 6000;
        toy_game_rebuild_navigation(&game);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_kind != -1) return 123;
    }
    /* 空气墙拓扑变化后，关闭墙体必须重新合并 component。 */
    {
        struct toy_map_primitive wall=test_box_primitive(
            -100,100,-6000,6000,0,0,TOY_MAP_PRIMITIVE_COLLISION);
        toy_game_init(&game, 83);
        game.to_spawn = 0; game.px = 3000; game.pz = 0;
        toy_game_set_primitives(&game,&wall,1,6000);
        toy_game_place_enemy(&game, -3000, 0);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_kind != -1) return 124;
        wall.flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
        toy_game_rebuild_navigation(&game);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_kind != 0) return 125;
    }
    /* Tank 在慢前摇末段同时命中前方单位；基地受 24 伤害但不击飞。 */
    {
        int i, player_hp, ai_hp, base_hp, behind_hp;
        toy_game_init(&game, 84);
        game.primitive_count = 0; game.room_limit = 12000; game.to_spawn = 0;
        game.px = 0; game.pz = 1200; /* 新起手距离内，旧 712 距离外 */
        memset(game.actors, 0, sizeof(game.actors));
        game.actors[0].active = 0;
        for (i = 0; i < 3; i++) {
            game.actors[i].active = 1;
            game.actors[i].kind = TOY_GAME_ACTOR_AI;
            game.actors[i].state = TOY_GAME_ACTOR_ALIVE;
            game.actors[i].hp = game.actors[i].max_hp = 100;
        }
        game.actors[0].x = 300; game.actors[0].z = 1200;
        game.actors[1].x = -300; game.actors[1].z = 1200;
        game.actors[1].base_core = 1;
        game.actors[2].x = 0; game.actors[2].z = -1200;
        for (i = 0; i < 3; i++) {
            game.actors[i].deployment_x = game.actors[i].x;
            game.actors[i].deployment_z = game.actors[i].z;
        }
        game.base_actor_index = 1;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_TANK;
        game.enemies[0].hp = 4000;
        game.enemies[0].speed = TOY_CONFIG_TANK_SPEED_MIN;
        game.enemies[0].dir_z = 1024;
        player_hp = game.hp; ai_hp = game.actors[0].hp;
        base_hp = game.actors[1].hp; behind_hp = game.actors[2].hp;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        for (i = 0; i < 39; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.hp != player_hp || game.actors[0].hp != ai_hp ||
            game.actors[1].hp != base_hp) return 126;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.hp != player_hp - 8 || game.player_airborne_ms <= 0 ||
            game.actors[0].hp != ai_hp - 8 ||
            game.actors[0].airborne_ms <= 0 ||
            game.actors[1].hp != base_hp - 24 ||
            game.actors[1].airborne_ms != 0 ||
            game.actors[2].hp != behind_hp) return 127;
        game.enemies[0].shove_stun_ms = 0;
        for (i = 0; i < 8; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].charge_active ||
            game.enemies[0].ability.special_timer_ms < 900) return 128;
    }
    /* Tank 的 Boss 质量使其完全免疫玩家和 AI 共用的推搜。 */
    {
        int old_z;
        toy_game_init(&game, 86);
        game.primitive_count = 0; game.room_limit = 12000; game.to_spawn = 0;
        game.px = 0; game.pz = 0;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_TANK;
        game.enemies[0].hp = 4000;
        game.enemies[0].x = 0; game.enemies[0].z = 500;
        old_z = game.enemies[0].z;
        if (toy_game_shove(&game, 0, 1024) != 0 ||
            game.enemies[0].z != old_z ||
            game.enemies[0].shove_stun_ms != 0) return 130;
    }
    /* Tank 击杀奖励独立于普通特感，固定为 50。 */
    {
        toy_game_init(&game, 85);
        game.primitive_count = 0; game.room_limit = 12000; game.to_spawn = 0;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_TANK;
        game.enemies[0].hp = 1;
        game.enemies[0].x = 0; game.enemies[0].z = 1000;
        game.money = 0;
        toy_game_fire(&game, 0, 1024);
        if (game.money != 50 || game.special_kills != 1) return 129;
    }
    if (rasterfall_net_snapshot_fragment_test() != 0) return 139;
    if (rasterfall_net_pipeline_test() != 0) return 140;
    if (rasterfall_net_client_slot_test() != 0) return 140;
    if (rasterfall_net_discovery_test() != 0) return 141;
    /* 已播完的预测死亡不能被致命射击确认前到达的旧存活快照重启。 */
    {
        struct rasterfall_net net_test;
        struct rasterfall_net_input *pending;
        rasterfall_session_reset(&session, &camera, 151);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_CLIENT;
        net_test.local_player_id = 1;
        net_test.snapshot_ready = 1;
        net_test.players[1].active = 1;
        net_test.players[1].state = TOY_GAME_PLAYING;
        net_test.players[1].hp = session.game_state.hp;
        net_test.players[1].fire_seq = 8;
        net_test.enemy_count = 1;
        net_test.enemies[0].index = 0;
        net_test.enemies[0].active = 1;
        net_test.enemies[0].hp = 10;
        session.game_state.enemies[0].active = 0;
        session.game_state.enemies[0].hp = 0;
        pending = &net_test.input_history[9 % RASTERFALL_NET_INPUT_HISTORY];
        pending->valid = 1;
        pending->sequence = 9;
        pending->fire_seq = 9;
        pending->ray_count = 1;
        pending->rays[0].enemy_index = 0;
        pending->rays[0].damage = 10;
        rasterfall_net_reconcile_client(&net_test, &session, &camera);
        if (session.game_state.enemies[0].active != 0) return 157;
        net_test.snapshot_ready = 1;
        net_test.players[1].fire_seq = 9;
        rasterfall_net_reconcile_client(&net_test, &session, &camera);
        if (session.game_state.enemies[0].active != 1) return 158;
    }
    /* 特感击飞通过一次冲量进入玩家本地运动状态，落地后恢复自主移动。 */
    {
        int actor_index;
        toy_game_init(&game, 152);
        game.primitive_count = 0;
        actor_index = toy_game_set_remote_player(&game, 1, 1, 0, 0,
                                                 "REMOTE");
        if (actor_index < 0 || !toy_game_apply_entity_impact(
                &game, TOY_GAME_ENTITY_ACTOR, actor_index, 0, 1024, 1) ||
            !game.actors[actor_index].control_disabled ||
            game.actors[actor_index].airborne_ms <= 0)
            return 152;
        while (game.actors[actor_index].airborne_ms > 0)
            toy_game_update_actor_motion(&game, actor_index, 16);
        if (game.actors[actor_index].control_disabled) return 153;
    }
    /* Smoker 控制使用 generation：旧 END 不能清掉更新后的控制。 */
    {
        toy_game_init(&game, 154);
        game.primitive_count = 0;
        game.room_limit = 12000;
        game.enemies[0].active = 1;
        game.enemies[0].x = 500;
        game.enemies[0].z = 0;
        game.px = 0;
        game.pz = 0;
        toy_game_set_player_special_control(
            &game, TOY_GAME_SPECIAL_CONTROL_SMOKER, 37, 0, 20);
        toy_game_clear_player_special_control(&game, 36);
        if (!game.player_control_disabled ||
            game.player_special_control_id != 37) return 154;
        toy_game_update_player_special_control(&game, 16);
        if (game.px <= 0) return 155;
        toy_game_clear_player_special_control(&game, 37);
        if (game.player_control_disabled ||
            game.player_special_control != TOY_GAME_SPECIAL_CONTROL_NONE)
            return 156;
    }
    /* 玩家武器增强只影响玩家操作者，AI 仍使用武器标准计时。 */
    {
        toy_game_init(&game, 143);
        game.slots[game.current_slot].mag = 1;
        toy_game_fire(&game, 0, 1024);
        if (game.fire_cooldown_ms != 200 * 100 /
                TOY_CONFIG_PLAYER_FIRE_RATE_PERCENT ||
            game.reload_timer_ms != 1500 * 60 / 100) return 150;
    }
    /* Combat Power 使用武器真实参数计算，并输出完整拆分，方便调平衡。 */
    {
        int weapon;
        static const int expected_power[TOY_GAME_WEAPON_BOMB] = {
            3, 7, 9, 12, 11, 5
        };
        /* Consumables do not contribute combat power; keep this legacy
         * balance assertion limited to combat weapons. */
        for (weapon = 0; weapon < TOY_GAME_WEAPON_BOMB; weapon++) {
            int dps = toy_game_weapon_combat_dps(weapon);
            int penalty = toy_game_weapon_spread_penalty(weapon);
            int power = toy_game_weapon_combat_power(weapon);
            __printf("rasterfall: CP %s dps=%d spread_penalty=%d bias=%d weapon=%d lv3_total=%d\n",
                     toy_game_weapon_name(weapon), dps, penalty,
                     toy_game_weapon_info(weapon)->power_bias, power,
                     TOY_CONFIG_COMBAT_AI_LEVEL_3_POINTS + power);
            if (power != expected_power[weapon]) return 137;
        }
        if (toy_game_ai_level_combat_power(TOY_GAME_AI_LEVEL_1) != 10 ||
            toy_game_ai_level_combat_power(TOY_GAME_AI_LEVEL_2) != 18 ||
            toy_game_ai_level_combat_power(TOY_GAME_AI_LEVEL_3) != 28 ||
            toy_game_ai_level_combat_power(-1) != 0) return 138;
    }
    /* Consumable shop items charge per purchase and stop at their inventory
     * caps; buying the other throwable replaces the current throwable. */
    {
        int i;
        toy_game_init(&game, 141);
        game.money = 1000;
        if (toy_game_buy_weapon(&game, TOY_GAME_WEAPON_BOMB) <= 0 ||
            game.slots[2].mag != 1) return 142;
        for (i = 0; i < TOY_GAME_THROWABLE_MAX - 1; i++)
            if (toy_game_buy_weapon(&game, TOY_GAME_WEAPON_BOMB) <= 0) return 143;
        if (game.slots[2].mag != TOY_GAME_THROWABLE_MAX ||
            toy_game_buy_weapon(&game, TOY_GAME_WEAPON_BOMB) != -1) return 144;
        if (toy_game_buy_weapon(&game, TOY_GAME_WEAPON_MOLOTOV) <= 0 ||
            game.slots[2].weapon != TOY_GAME_WEAPON_MOLOTOV ||
            game.slots[2].mag != 1) return 148;
        if (toy_game_buy_weapon(&game, TOY_GAME_WEAPON_MOLOTOV) <= 0 ||
            game.slots[2].mag != 2) return 149;
        toy_game_init(&game, 142);
        game.money = 1000;
        if (toy_game_buy_weapon(&game, TOY_GAME_WEAPON_PILL) <= 0) return 145;
        for (i = 1; i < TOY_GAME_PILL_MAX; i++)
            if (toy_game_buy_weapon(&game, TOY_GAME_WEAPON_PILL) <= 0) return 146;
        if (game.slots[3].mag != TOY_GAME_PILL_MAX ||
            toy_game_buy_weapon(&game, TOY_GAME_WEAPON_PILL) != -1) return 147;
    }
    /* Ground-aware traversal must be consumed by behavior, not only stored in
     * the navigation grid: enemies climb and descend, while a companion that
     * faces a ramp side routes around to its low entrance. */
    {
        struct toy_map_primitive terrain[3] = {
            test_flat_primitive(-5000,5000,-5000,5000,0),
            test_ramp_primitive(TOY_MAP_PRIMITIVE_RAMP_X,
                                0,2400,-1000,1000,0,600),
            test_flat_primitive(2400,4200,-1000,1000,600)
        };
        int i, max_ground = 0, min_ground, actor_id;
        struct toy_game_actor *ramp_actor;
        toy_game_init(&game, 180);
        game.to_spawn = 0;
        game.px = 3300; game.pz = 0;
        game.player_ground_y = 600;
        game.hp = 100000;
        game.safe_room_count = 0;
        toy_game_set_primitives(&game,terrain,3,5000);
        toy_game_place_enemy(&game, -500, 0);
        if (!game.enemies[0].active) return 180;
        game.enemies[0].ai_state = TOY_GAME_ENEMY_TRACKING;
        game.enemies[0].type = TOY_GAME_ENEMY_COMMON;
        for (i = 0; i < 360; i++) {
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
            if (game.enemies[0].ground_y > max_ground)
                max_ground = game.enemies[0].ground_y;
        }
        if (max_ground < 450) return 181;
        toy_game_init(&game, 182);
        game.to_spawn = 0;
        game.px = -800; game.pz = 0;
        game.hp = 100000;
        game.safe_room_count = 0;
        toy_game_set_primitives(&game,terrain,3,5000);
        memset(&game.enemies[0], 0, sizeof(game.enemies[0]));
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_COMMON;
        game.enemies[0].speed = toy_game_enemy_info(
            TOY_GAME_ENEMY_COMMON)->speed_max;
        game.enemies[0].hp = toy_game_enemy_info(
            TOY_GAME_ENEMY_COMMON)->max_hp;
        game.enemies[0].x = 3000;
        game.enemies[0].z = 0;
        game.enemies[0].ground_y = 600;
        game.enemies[0].target_kind = -1;
        game.enemies[0].retarget_timer_ms = 0;
        game.enemies[0].ai_state = TOY_GAME_ENEMY_TRACKING;
        game.enemies_alive = 1;
        min_ground = game.enemies[0].ground_y;
        for (i = 0; i < 360; i++) {
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
            if (game.enemies[0].ground_y < min_ground)
                min_ground = game.enemies[0].ground_y;
            memset(&game.enemies[1], 0,
                   sizeof(game.enemies) - sizeof(game.enemies[0]));
            game.enemies_alive = 1;
            game.to_spawn = 0;
            if (game.enemies[0].x <= 900) break;
        }
        if (game.enemies[0].x > 900 || min_ground > 225)
            return 182;

        toy_game_init(&game, 183);
        game.to_spawn = 0;
        game.px = 3300; game.pz = 0;
        game.player_ground_y = 600;
        game.safe_room_count = 0;
        toy_game_set_primitives(&game,terrain,3,5000);
        actor_id = toy_game_add_anime_actor(
            &game, 0, 1200, -1500, "RAMP_AI");
        ramp_actor = toy_game_actor_by_id(&game, actor_id);
        if (!ramp_actor) return 183;
        max_ground = 0;
        for (i = 0; i < 600; i++) {
            toy_game_update_ai_teammates(&game, 16);
            if (ramp_actor->ground_y > max_ground)
                max_ground = ramp_actor->ground_y;
        }
        if (max_ground < 300 || ramp_actor->x < 1200 ||
            ramp_actor->z < -1000) return 184;
    }
    __printf("rasterfall: logic test passed\n");
    return 0;
}
