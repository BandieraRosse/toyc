#include "tlibc_everything.h"
#include "rf_tactical_lab.h"
#include "math.h"
#include "rasterfall_session.h"
#include "rasterfall_feature_freeze.h"
#include "rasterfall_units.h"
#include "rf_numeric.h"
#include "rasterfall_model.h"
#include "rasterfall_character.h"
#include "rf_experiment_session.inc"

#define INTERACT_AIM_CONE 784
#define HORDE_COUNT_MIN 15
#define HORDE_COUNT_MAX 20
#define HORDE_MIN_PLAYER_DIST 700
#define QUARTER_TURN 1611
#define SMOOTH_TURN_STEP 128
#define MANAGED_AI_TURN_DEG_PER_SEC 480
#define HURD_CONTROL_MIN_X (-5000)
#define HURD_CONTROL_MAX_X 5000
#define HURD_CONTROL_MIN_Z 25500
#define HURD_CONTROL_MAX_Z 31500
#define HURD_FLAG_X 0
#define HURD_FLAG_Z 28500
#define STANDARD_FLAG_X 0
#define STANDARD_FLAG_Z 7000
#define ASSAULT_FLAG_X 14000
#define ASSAULT_FLAG_Z 0
static const int roster_spawn_positions[RASTERFALL_SQUAD_COUNT]
                                  [RASTERFALL_SQUAD_SIZE][2] = {
    {
        { 420, 7420 }, { -420, 7420 }, { -420, 6580 }, { 420, 6580 }
    },
    {
        { 14420, 420 }, { 13580, 420 }, { 13580, -420 }, { 14420, -420 }
    }
};
static const int flag_colors[] = { 0x173A70, 0x9E302B, 0xC78A24, 0x2B765B,
                                   0x704A91, 0xB75A2C };
static const char *flag_names[] = { "TOYC", "GNU", "LLVM", "GCC", "NASA", "UNIX" };

static int session_is_developer_ai(const char *name)
{
    return name && (!strcmp(name, "DEV_GUNNER") ||
                    !strcmp(name, "PLATFORM_GUARD") ||
                    !strcmp(name, "HIT_TEST") || !strcmp(name, "ANIM_TEST") ||
                    !strcmp(name, "AK_TEST") || !strcmp(name, "AWP_TEST"));
}
static void session_interact(struct rasterfall_session *session,
                             struct rasterfall_interactable *it);
#include "rf_frontier_session.inc"
static int session_near_flag(const struct rasterfall_session *session,
                             const struct camera *camera);

static void session_down_ai(struct rasterfall_session *session, int index,
                            int x, int z)
{
    struct toy_game_actor *actor = &session->game_state.actors[index];
    toy_game_move_ai_actor(&session->game_state, index, x, z);
    actor->hp = 0;
    actor->state = TOY_GAME_ACTOR_DOWNED;
    toy_game_actor_set_animation(actor, TOY_GAME_ANIM_DEATH);
    actor->revive_progress_ms = 0;
}

static void session_init_flag(struct rasterfall_session *s, int fi, int x, int z)
{
    struct rasterfall_flag *f = &s->flags[fi];
    memset(f, 0, sizeof(*f)); f->active = 1; f->x = x; f->z = z; f->facing = 1;
    f->color = flag_colors[fi % (int)(sizeof(flag_colors)/sizeof(flag_colors[0]))];
    f->carrier_id = -1;
    strncpy(f->label, flag_names[fi % (int)(sizeof(flag_names)/sizeof(flag_names[0]))], 4);
    f->label[4] = 0;
    /* Four corners of a deliberately compact, adjustable square. */
    f->slot_offsets[0][0] = 420;  f->slot_offsets[0][1] = 420;
    f->slot_offsets[1][0] = -420; f->slot_offsets[1][1] = 420;
    f->slot_offsets[2][0] = -420; f->slot_offsets[2][1] = -420;
    f->slot_offsets[3][0] = 420;  f->slot_offsets[3][1] = -420;
}

void rasterfall_session_hurd_status(
    const struct rasterfall_session *s,
    struct rasterfall_hurd_status *status)
{
    const struct rasterfall_hurd_outpost *outpost;
    const struct rasterfall_flag *flag = NULL;
    int i;
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!s) return;
    outpost = &s->hurd_outpost;
    if (outpost->flag_index >= 0 && outpost->flag_index < s->flag_count)
        flag = &s->flags[outpost->flag_index];
    status->flag_deployed_in_region = flag && flag->active && !flag->carried &&
        flag->x >= outpost->minx && flag->x <= outpost->maxx &&
        flag->z >= outpost->minz && flag->z <= outpost->maxz;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &s->game_state.actors[i];
        int member=0;
        for(int n=0;n<RASTERFALL_HURD_SQUAD_SIZE;++n)
            if(outpost->squad_actor_indices[n]==i)member=1;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->base_core || actor->developer_only || actor->companion ||
            !member)
            continue;
        status->assigned_count++;
        if (actor->state == TOY_GAME_ACTOR_ALIVE && actor->hp > 0)
            status->capable_count++;
    }
    status->controlled = status->flag_deployed_in_region &&
                         status->capable_count > 0;
}

static int session_near_ai(const struct rasterfall_session *session,
                           const struct camera *camera, int *out_index)
{
    int i, best = -1;
    long long best_d2 = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &session->game_state.actors[i];
        long long dx, dz, d2;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->base_core || actor->faction != TOY_GAME_FACTION_ALLIED ||
            actor->state != TOY_GAME_ACTOR_DOWNED) continue;
        dx = (long long)camera->x - actor->x;
        dz = (long long)camera->z - actor->z;
        d2 = dx * dx + dz * dz;
        if (d2 > (long long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE)
            continue;
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    if (out_index) *out_index = best;
    return best >= 0;
}

int rasterfall_session_find_down_ai(const struct rasterfall_session *session,
                                    const struct camera *camera)
{
    int index = -1;
    if (!session || !camera || !session_near_ai(session, camera, &index))
        return -1;
    return index;
}

static void session_set_air_walls(struct rasterfall_session *session,
                                  int enabled)
{
    rasterfall_map_set_air_walls(&session->map_ops, enabled);
    toy_game_rebuild_navigation(&session->game_state);
}

static int session_content_actor_position(
    const struct rasterfall_session *session, const char *id, int *x, int *z)
{
    int i;
    for (i = 0; i < session->content.actor_count; i++)
        if (!strcmp(session->content.actors[i].id, id)) {
            *x = session->content.actors[i].x;
            *z = session->content.actors[i].z;
            return 1;
        }
    return 0;
}

static const struct rasterfall_content_actor *session_content_actor(
    const struct rasterfall_session *session, const char *id)
{
    int i;
    if (!session || !id) return NULL;
    for (i = 0; i < session->content.actor_count; i++)
        if (!strcmp(session->content.actors[i].id, id))
            return &session->content.actors[i];
    return NULL;
}

static const struct rasterfall_content_formation *session_content_formation(
    const struct rasterfall_session *session, const char *id)
{
    int i;
    if (!session || !id) return NULL;
    for (i = 0; i < session->content.formation_count; i++)
        if (!strcmp(session->content.formations[i].id, id))
            return &session->content.formations[i];
    return NULL;
}

static int session_content_character_id(const char *name)
{
    if (!name) return RASTERFALL_CHARACTER_NONE;
    if (!strcmp(name, "MAID")) return RASTERFALL_CHARACTER_MAID;
    if (!strcmp(name, "HURD_GUNSMITH")) return RASTERFALL_CHARACTER_HURD_GUNSMITH;
    if (!strcmp(name, "HURD_LOGISTICS")) return RASTERFALL_CHARACTER_HURD_LOGISTICS;
    if (!strcmp(name, "HURD_MEDIC")) return RASTERFALL_CHARACTER_HURD_MEDIC;
    if (!strcmp(name, "HURD_GUARD")) return RASTERFALL_CHARACTER_HURD_GUARD;
    if (!strcmp(name, "RF_RIFLEMAN")) return RASTERFALL_CHARACTER_RF_RIFLEMAN;
    if (!strcmp(name, "SQUAD_A_ENGINEER")) return RASTERFALL_CHARACTER_SQUAD_A_ENGINEER;
    if (!strcmp(name, "SQUAD_A_RECON")) return RASTERFALL_CHARACTER_SQUAD_A_RECON;
    if (!strcmp(name, "SQUAD_B_BREACHER")) return RASTERFALL_CHARACTER_SQUAD_B_BREACHER;
    return RASTERFALL_CHARACTER_NONE;
}

static int session_content_flag_position(
    const struct rasterfall_session *session, const char *id, int *x, int *z,
    int *facing)
{
    int i;
    for (i = 0; i < session->content.flag_definition_count; i++)
        if (!strcmp(session->content.flag_definitions[i].id, id)) {
            *x = session->content.flag_definitions[i].x;
            *z = session->content.flag_definitions[i].z;
            *facing = session->content.flag_definitions[i].facing;
            return 1;
        }
    return 0;
}

static int session_content_terminal_kind(const char *kind)
{
    if (!strcmp(kind, "station")) return TOY_MAP_PICKUP_STATION_TERMINAL;
    if (!strcmp(kind, "operations")) return TOY_MAP_PICKUP_OPERATIONS_TERMINAL;
    if (!strcmp(kind, "super")) return TOY_MAP_PICKUP_SUPER_TERMINAL;
    return -1;
}

static void session_add_content_terminals(struct rasterfall_session *session)
{
    int i, kind;
    for (i = 0; i < session->content.terminal_count; i++) {
        const struct rasterfall_content_terminal *def =
            &session->content.terminals[i];
        kind = session_content_terminal_kind(def->kind);
        if (kind < 0 || session->item_count >= TOY_MAP_MAX_PICKUPS) continue;
        session->items[session->item_count].kind = kind;
        session->items[session->item_count].weapon = -1;
        session->items[session->item_count].x = def->x;
        session->items[session->item_count].y = def->y;
        session->items[session->item_count].z = def->z;
        session->item_count++;
    }
}

/* The roster is content data; this adapter only turns its ordered entries
 * into ordinary AI actors. It does not add squad behavior or visual state to
 * toy_game_actor. All formal members, including Jesus, are instantiated from
 * campaign content rather than a map-authored/default actor. */
static void session_spawn_formal_rosters(struct rasterfall_session *session)
{
    static const char *content_ids[RASTERFALL_SQUAD_COUNT][RASTERFALL_SQUAD_SIZE] = {
        { "jesus", "standard_medic", "standard_engineer", "standard_recon" },
        { "assault_rifleman", "assault_breacher", "assault_heavy", "assault_medic" }
    };
    int squad, member;
    for (squad = 0; squad < RASTERFALL_SQUAD_COUNT; squad++) {
        const struct rasterfall_squad_roster *roster =
            rasterfall_squad_roster(squad);
        for (member = 0; member < RASTERFALL_SQUAD_SIZE; member++)
            session->squad_runtime[squad].actor_indices[member] = -1;
        for (member = 0; member < RASTERFALL_SQUAD_SIZE; member++) {
            const struct rasterfall_roster_member *entry =
                &roster->members[member];
            int actor_index = -1;
            if (actor_index < 0) {
                int x = roster_spawn_positions[squad][member][0];
                int z = roster_spawn_positions[squad][member][1];
                int actor_id;
                session_content_actor_position(session,
                    content_ids[squad][member], &x, &z);
                actor_id = toy_game_add_ai(
                    &session->game_state, TOY_GAME_AI_LEVEL_2,
                    x, z, entry->name);
                actor_index = actor_id > 0 ? actor_id - 1 : -1;
            }
            if (actor_index < 0) continue;
            session->game_state.actors[actor_index].character_id =
                entry->character_id;
            /* The V2 action station is authored around the AK. Keep every
             * formal V2 roster actor on that same asset so the battle
             * renderer uses the same PRIMARY_GRIP / FOREGRIP left-hand IK
             * path as the station. Legacy/procedural actors retain their
             * class-selected weapons. */
            if (rasterfall_character_visual_recipe_for_character(
                    entry->character_id))
                toy_game_set_ai_weapon(&session->game_state, actor_index,
                                       TOY_GAME_WEAPON_AK);
            session->squad_runtime[squad].actor_indices[member] = actor_index;
        }
    }
}

int rasterfall_session_load(struct rasterfall_session *session,
                            const char *map_path)
{
    if (!session) return -1;
    /* A session owns level.blob through map_ops.  Release it before resetting
     * the containing object so load -> load cannot orphan the old map. */
    rasterfall_session_unload(session);
    {
        struct rf_gpu_scene_local_source source = session->scene_local;
        struct toy_game_player_movement movement;
        struct toy_game_gameplay_config gameplay;
        memcpy(&movement,&session->player_movement,sizeof(movement));
        memcpy(&gameplay,&session->gameplay_config,sizeof(gameplay));
        memset(session, 0, sizeof(struct rasterfall_session));
        session->scene_local = source;
        memcpy(&session->player_movement,&movement,sizeof(movement));
        memcpy(&session->gameplay_config,&gameplay,sizeof(gameplay));
    }
    session->air_walls_enabled = 1;
    session->highlight_index = -1;
    session->weaver_item_index = -1;
    session->weaver_item_serial = 0;
    rasterfall_map_bind(&session->map_ops, &session->level,
                        session->safe_rooms, session->spawn_zones,
                        &session->spawn_count, &session->air_walls_enabled,
                        session->items, &session->item_count);
    if (rasterfall_map_load_runtime_overlay(&session->map_ops, map_path) < 0) {
        __fprintf(2,"Map load failed: %s line=%d %s\n",map_path,
            session->map_ops.runtime.error_line,session->map_ops.runtime.error);
        return -1;
    }
    {
        const char *identity = rf_map_runtime_world_info(&session->map_ops.runtime)->identity;
        session->world_id = RASTERFALL_WORLD_CAMPAIGN_01;
        if (!strcmp(identity, "outpost")) session->world_id = RASTERFALL_WORLD_OUTPOST;
        else if (!strcmp(identity, "tactical_arena")) session->world_id=RASTERFALL_WORLD_TACTICAL_ARENA;
        else if (!strcmp(identity, "tactical_range")) session->world_id=RASTERFALL_WORLD_TACTICAL_RANGE;
        else if (!strcmp(identity, "frontier_station_01"))
            session->world_id = RASTERFALL_WORLD_FRONTIER_STATION_01;
        else if (!strcmp(identity, "performance_empty"))
            session->world_id = RASTERFALL_WORLD_PERF_EMPTY;
        else if (!strcmp(identity, "performance_components"))
            session->world_id = RASTERFALL_WORLD_PERF_COMPONENTS;
        else if (*identity && strcmp(identity, "campaign_01")) return -1;
    }
    if (rasterfall_world_content_load(&session->content, session->world_id,
                                      rasterfall_world_content_path(session->world_id)) < 0) {
        __fprintf(2,"Content load failed: %s line=%d %s\n",
            rasterfall_world_content_path(session->world_id),session->content.error_line,session->content.error);
        return -1;
    }
    session->world_request = session->world_id;
    if (rasterfall_map_project_runtime(&session->map_ops) < 0) {
        __fprintf(2,"Map projection failed: %s\n",map_path);return -1;
    }
    if(!session_frontier_bind(session))return -1;
    __printf("Loading world source: %s\n", map_path);
    __printf("Map runtime loaded: regions=%d interactions=%d\n",
             rf_map_runtime_region_count(&session->map_ops.runtime),
             rf_map_runtime_interaction_count(&session->map_ops.runtime));
    return 0;
}

int rasterfall_session_adopt_map(struct rasterfall_session *s,struct rasterfall_session *p)
{
    if(!s || !p || s==p || !p->map_ops.runtime_loaded)return -1;
    rasterfall_session_unload(s);
    struct rf_gpu_scene_local_source source=s->scene_local;
    struct toy_game_player_movement movement=s->player_movement;
    struct toy_game_gameplay_config gameplay=s->gameplay_config;
    memset(s,0,sizeof(*s));s->scene_local=source;
    s->player_movement=movement;s->gameplay_config=gameplay;
    s->world_id=p->world_id;s->world_request=s->world_id;s->air_walls_enabled=1;
    s->highlight_index=s->weaver_item_index=-1;
    s->level=p->level;memset(&p->level,0,sizeof(p->level));
    s->content=p->content;memset(&p->content,0,sizeof(p->content));
    s->map_ops.runtime=p->map_ops.runtime;
    memset(&p->map_ops.runtime,0,sizeof(p->map_ops.runtime));
    s->map_ops.runtime_loaded=1;p->map_ops.runtime_loaded=0;
    rasterfall_map_bind(&s->map_ops,&s->level,s->safe_rooms,s->spawn_zones,
        &s->spawn_count,&s->air_walls_enabled,s->items,&s->item_count);
    if(rasterfall_map_project_runtime(&s->map_ops)<0 || !session_frontier_bind(s))return -1;
    return 0;
}

int rasterfall_session_load_legacy(struct rasterfall_session *session,
                                   const char *map_path)
{
    if (!session) return -1;
    rasterfall_session_unload(session);
    {
        struct rf_gpu_scene_local_source source = session->scene_local;
        struct toy_game_player_movement movement;
        struct toy_game_gameplay_config gameplay;
        memcpy(&movement,&session->player_movement,sizeof(movement));
        memcpy(&gameplay,&session->gameplay_config,sizeof(gameplay));
        memset(session, 0, sizeof(struct rasterfall_session));
        session->scene_local = source;
        memcpy(&session->player_movement,&movement,sizeof(movement));
        memcpy(&session->gameplay_config,&gameplay,sizeof(gameplay));
    }
    session->world_id = RASTERFALL_WORLD_CAMPAIGN_01;
    if (rasterfall_world_content_load(&session->content, session->world_id,
                                      rasterfall_world_content_path(session->world_id)) < 0) {
        __fprintf(2,"Content load failed: %s line=%d %s\n",
            rasterfall_world_content_path(session->world_id),session->content.error_line,session->content.error);
        return -1;
    }
    session->world_request = session->world_id;
    session->air_walls_enabled = 1;
    session->highlight_index = -1;
    rasterfall_map_bind(&session->map_ops, &session->level,
                        session->safe_rooms, session->spawn_zones,
                        &session->spawn_count, &session->air_walls_enabled,
                        session->items, &session->item_count);
    if (rasterfall_map_load(&session->map_ops, map_path) < 0) return -1;
    rasterfall_map_prepare(&session->map_ops);
    __printf("Loading legacy map source: %s\n", map_path);
    return 0;
}

const struct toy_game_actor *rasterfall_session_local_player_const(
    const struct rasterfall_session *session)
{
    if (!session) return NULL;
    return toy_game_local_player_actor_const(&session->game_state);
}

void rasterfall_session_unload(struct rasterfall_session *session)
{
    if (!session) return;
    rasterfall_experiment_dispose(session,0);
    if(session->tactical) {
#if RF_TACTICAL_INTERACTIVE
        rf_tac_match_destroy(&session->tactical->match);
        rf_range_destroy(&session->tactical->range);
#endif
        free(session->tactical);session->tactical=NULL;
    }
    rf_gpu_scene_local_world(&session->scene_local);
    memset(&session->frontier,0,sizeof(session->frontier));
    memset(&session->frontier_config,0,sizeof(session->frontier_config));
    rasterfall_world_content_clear(&session->content);
    rasterfall_map_unload(&session->map_ops);
}

int rasterfall_session_request_world(struct rasterfall_session *session,
                                     enum rasterfall_world_id world)
{
    if (!session || (world != RASTERFALL_WORLD_OUTPOST &&
                     world != RASTERFALL_WORLD_CAMPAIGN_01 &&
                     world != RASTERFALL_WORLD_TACTICAL_ARENA &&
                     world != RASTERFALL_WORLD_TACTICAL_RANGE &&
                     world != RASTERFALL_WORLD_FRONTIER_STATION_01 &&
                     world != RASTERFALL_WORLD_PERF_EMPTY &&
                     world != RASTERFALL_WORLD_PERF_COMPONENTS)) return -1;
    session->world_request = world;
    session->world_request_pending = 1;
    return 0;
}

int rasterfall_session_take_world_request(struct rasterfall_session *session,
                                          enum rasterfall_world_id *world)
{
    if (!session || !world || !session->world_request_pending) return 0;
    *world = session->world_request;
    session->world_request_pending = 0;
    return 1;
}

#include "rf_tactical_session.inc"

void rasterfall_session_reset(struct rasterfall_session *session,
                              struct camera *camera, uint64_t seed)
{
    int i;
    rasterfall_experiment_dispose(session,1);
    if(session->tactical) {
#if RF_TACTICAL_INTERACTIVE
        rf_tac_match_destroy(&session->tactical->match);
        rf_range_destroy(&session->tactical->range);
#endif
        free(session->tactical);session->tactical=NULL;
    }
    if (session->map_ops.runtime_loaded)
        rasterfall_map_project_runtime(&session->map_ops);
    camera->x = session->level.start_x;
    camera->z = session->level.start_z;
    camera->sy = session->level.start_sy;
    camera->cy = session->level.start_cy;
    camera->pitch_sy = 0;
    camera->pitch_cy = 1024;
    camera->y = RASTERFALL_STANDING_CAMERA_Y;
    session->seed = seed ? seed : 1;
    session->skeletal_demo_pose = RASTERFALL_MODEL_POSE_BIND;
    session->humanoid_debug_action = RASTERFALL_HUMANOID_DEBUG_IDLE;
    session->humanoid_debug_time_ms = 0;
    session->skeletal_demo_player.clip = NULL;
    /* Fixed developer displays start in the reusable reference pose.  Keeping
     * all five high-detail models in WALK forced full animation, skinning and
     * triangle generation every frame for the entire session.  WALK remains
     * available from its map button and from the Pose Editor animation page. */
    session->skeletal_demo_player.clip_id = -1;
    session->skeletal_demo_player.time_ms = 0;
    session->skeletal_demo_player.playing = 0;
    session->skeletal_demo_player.loop = 1;
    session->skeletal_demo_player.speed_milli = 1000;
    rasterfall_rifle_pose_default(&session->rifle_pose);
    __memset(&session->hit_pose,0,sizeof(session->hit_pose));
    session->hit_pose.rotation[0][0]=8;
    session->hit_pose.rotation[0][2]=-8;
    rasterfall_calibration_init(&session->pose_editor);
    toy_game_init(&session->game_state, session->seed);
    if(session->gameplay_config.base_hp>0)
        toy_game_apply_gameplay_config(&session->game_state,&session->gameplay_config);
    if(session->player_movement.move_step>0)
        memcpy(&session->game_state.player_movement,&session->player_movement,
            sizeof(session->player_movement));
    /* 环境变量不依赖 libc；HOSTNAME 是最稳定的本机身份来源，缺失时
     * toy_game_init 的 PLAYER 保底仍可用。名字只用于身份展示/未来快照。 */
    if (global_envp && get_env_var(global_envp, "HOSTNAME"))
        toy_game_set_actor_name(
            toy_game_local_player_actor(&session->game_state),
            get_env_var(global_envp, "HOSTNAME"));
    toy_game_set_primitives(&session->game_state, session->level.primitives,
                            session->level.primitive_count,
                            session->level.room_limit);
    if(session->world_id==RASTERFALL_WORLD_OUTPOST &&
       !toy_game_set_grid_enabled(&session->game_state,1))
        __printf("OUTPOST-GRID invalid map capacity\n");
    if(session->game_state.grid_enabled) {
        int building=0;
        for(int region=0;region<rf_map_runtime_region_count(&session->map_ops.runtime);++region) {
            const struct rf_map_runtime_region *r=rf_map_runtime_region_at(&session->map_ops.runtime,region);
            if(strcmp(r->kind,"building_module"))continue;
            if(!toy_game_grid_mark_building(&session->game_state,building++,
                r->bounds.min_x,r->bounds.min_z,r->bounds.max_x,r->bounds.max_z))
                __printf("OUTPOST-GRID invalid building footprint %s\n",r->id);
        }
    }
    /* The safe room is open to the player through its doorway, but its whole
     * footprint is an enemy-forbidden area.  Register it even in the endless
     * director mode; toy_game_set_campaign also rebuilds navigation. */
    toy_game_set_campaign(&session->game_state, session->safe_rooms,
                          session->level.safe_count, session->spawn_zones,
                          session->spawn_count);
    /* The game starts directly in the ordinary endless wave director.  There
     * are no safe rooms, capture stages, alarms, or objective transitions. */
    /* actor 0 is reserved for the local player; map-authored AI starts at 1. */
    session->null_actor_index = -1;
    for (i = 0; i < 5; ++i) {
        session->frontier_squad_indices[i] = -1;
        session->frontier_squad_actor_ids[i] = -1;
        session->frontier_squad_generations[i] = 0;
    }
    for (i = 0; i < session->level.ai_spawn_count && i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_map_ai_spawn *spawn = &session->level.ai_spawns[i];
        int actor_index;
        if (i == 0) {
            toy_game_set_ai_teammate_class(&session->game_state, 1,
                                           spawn->class_id, spawn->x, spawn->z,
                                           spawn->name);
            actor_index = 1;
        } else {
            int actor_id = toy_game_add_ai(&session->game_state, spawn->class_id,
                                           spawn->x, spawn->z, spawn->name);
            actor_index = actor_id > 0 ? actor_id - 1 : -1;
        }
        if (actor_index < 0) continue;
        session->game_state.actors[actor_index].developer_only =
            session_is_developer_ai(spawn->name);
        if (spawn->weapon >= 0 &&
            spawn->weapon < TOY_GAME_WEAPON_COUNT) {
            toy_game_set_ai_weapon(&session->game_state, actor_index,
                                   spawn->weapon);
        }
        if (!strcmp(spawn->name, "BASE")) {
            struct toy_game_actor *base =
                &session->game_state.actors[actor_index];
            const struct toy_game_weapon_info *pistol =
                toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
            base->base_core = 1;
            base->max_hp = session->game_state.gameplay_config.base_hp;
            base->hp = session->game_state.gameplay_config.base_hp;
            base->state = TOY_GAME_ACTOR_ALIVE;
            base->slots[0].weapon = TOY_GAME_WEAPON_PISTOL;
            base->slots[0].mag = pistol->mag_size;
            base->slots[0].reserve = TOY_GAME_AMMO_INFINITE;
            base->current_slot = 0;
            session->game_state.base_actor_index = actor_index;
            session->game_state.base_regen_timer_ms =
                session->game_state.gameplay_config.base_regen_ms;
        }
        if (!strcmp(spawn->name, "HIT_TEST")) {
            session->game_state.actors[actor_index].fire_enabled = 0;
            session->game_state.actors[actor_index].hit_test_dummy = 1;
        }
        if (!strcmp(spawn->name, "ANIM_TEST")) {
            session->game_state.actors[actor_index].fire_enabled = 0;
            session->game_state.actors[actor_index].animation_demo = 1;
            session->game_state.actors[actor_index].animation_demo_elapsed_ms = 0;
            toy_game_actor_set_animation(
                &session->game_state.actors[actor_index],
                TOY_GAME_ANIM_IDLE);
        }
        if (!strcmp(spawn->name, "Null") || !strcmp(spawn->name, "NULL")) {
            struct toy_game_actor *null_actor =
                &session->game_state.actors[actor_index];
            null_actor->fire_enabled = 0;
            null_actor->ai_stationary = 1;
            null_actor->companion = 1;
            strcpy(null_actor->name, "NULL");
            session->null_actor_index = actor_index;
        }
        if (!strcmp(spawn->name, "Jesus"))
            session->game_state.actors[actor_index].character_id =
                RASTERFALL_CHARACTER_RF_RIFLEMAN;
        if (spawn->downed) session_down_ai(session, actor_index, spawn->x, spawn->z);
    }
    if (session->content.spawn_null) {
        for (i = 0; i < session->content.actor_count; i++) {
            struct rasterfall_content_actor *def = &session->content.actors[i];
            if (strcmp(def->id, "null")) continue;
            {
                int actor_id = toy_game_add_ai(&session->game_state,
                    TOY_GAME_AI_LEVEL_2, def->x, def->z, def->name);
                if (actor_id > 0) {
                    struct toy_game_actor *a =
                        &session->game_state.actors[actor_id - 1];
                    a->fire_enabled = 0;
                    a->ai_stationary = 1;
                    a->companion = 1;
                    strcpy(a->name, "NULL");
                    session->null_actor_index = actor_id - 1;
                }
            }
            break;
        }
    }
    /* World-authored road squads use the normal actor lifecycle and AI. */
    if(session->world_id==RASTERFALL_WORLD_OUTPOST ||
       session->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01) {
        int frontier_member = 0;
        for(int f=0;f<session->content.formation_count;++f) {
            const struct rasterfall_content_formation *formation=&session->content.formations[f];
            if(strncmp(formation->id,session->world_id==RASTERFALL_WORLD_OUTPOST?
                "rts_":"frontier_",session->world_id==RASTERFALL_WORLD_OUTPOST?4:9))continue;
            for(int n=0;n<formation->member_count;++n) {
                const struct rasterfall_content_actor *def=session_content_actor(session,formation->member_ids[n]);
                if(!def)continue;
                int id=toy_game_add_ai(&session->game_state,TOY_GAME_AI_LEVEL_2,def->x,def->z,def->name);
                if(id<1)continue;
                struct toy_game_actor *a=&session->game_state.actors[id-1];
                a->character_id=session_content_character_id(def->character);
                int weapon=toy_game_weapon_from_name(def->weapon);
                if(weapon>=0)toy_game_set_ai_weapon(&session->game_state,id-1,weapon);
                double yaw=def->yaw*3.141592653589793/180.0;
                a->sy=(int)(sin(yaw)*1024);a->cy=(int)(cos(yaw)*1024);
                if(session->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01 && frontier_member<5) {
                    session->frontier_squad_indices[frontier_member]=id-1;
                    session->frontier_squad_actor_ids[frontier_member]=a->actor_id;
                    session->frontier_squad_generations[frontier_member++]=a->combat_generation;
                    a->companion=1;
                }
            }
        }
    }
    if (session->content.spawn_campaign_roster)
        session_spawn_formal_rosters(session);
    /* Anime companions are session actors, separate from map-authored
     * low-poly mercenaries and the fixed developer model lineup. */
    if (session->content.spawn_campaign_support) {
        for (i = 0; i < session->content.actor_count; i++) {
            struct rasterfall_content_actor *def = &session->content.actors[i];
            if (strcmp(def->id, "eula")) continue;
            toy_game_add_anime_actor(&session->game_state, 0,
                                     def->x, def->z, def->name);
            break;
        }
    }
    /* Registration is separate from simulation: the existing teammate
     * executor remains authoritative while policies are introduced. */
    rasterfall_ai_registry_init(&session->ai_registry);
    rasterfall_ai_registry_sync(&session->ai_registry,
                                &session->game_state);
    if (session->managed_ai_enabled)
        rasterfall_ai_registry_add(
            &session->ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX,
            RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER, 100,
            RASTERFALL_AI_POLICY_MANAGED_SIMPLE);
    session->flag_count = 0;
    session->carried_flag = -1;
    session->hurd_outpost.flag_index = -1;
    for (i = 0; i < RASTERFALL_HURD_SQUAD_SIZE; i++)
        session->hurd_outpost.squad_actor_indices[i] = -1;
    if (session->content.campaign_flags_enabled) {
    session->flag_count = 5;
    /* Keep the initial flag at the world origin while the player starts
     * 500 units closer to the Eula display. */
    session_init_flag(session, 0, 0, 0);
    /* Restore the original Maid guard post and its flag index. */
    { int fx = -12000, fz = 0, facing = 1;
      session_content_flag_position(session, "maid_alpha", &fx, &fz, &facing);
      session_init_flag(session, RASTERFALL_MAID_FLAG_INDEX, fx, fz);
      session->flags[RASTERFALL_MAID_FLAG_INDEX].facing=facing; }
    { int fx = HURD_FLAG_X, fz = HURD_FLAG_Z, facing = 1;
      session_content_flag_position(session, "hurd", &fx, &fz, &facing);
      session_init_flag(session, RASTERFALL_HURD_FLAG_INDEX, fx, fz);
      session->flags[RASTERFALL_HURD_FLAG_INDEX].facing=facing; }
    { int fx = STANDARD_FLAG_X, fz = STANDARD_FLAG_Z, facing = 1;
      session_content_flag_position(session, "standard_response", &fx, &fz, &facing);
      session_init_flag(session, RASTERFALL_STANDARD_FLAG_INDEX, fx, fz);
      session->flags[RASTERFALL_STANDARD_FLAG_INDEX].facing=facing; }
    session->flags[RASTERFALL_STANDARD_FLAG_INDEX].color = 0x2B765B;
    strncpy(session->flags[RASTERFALL_STANDARD_FLAG_INDEX].label, "RESP", 4);
    session->flags[RASTERFALL_STANDARD_FLAG_INDEX].label[4] = 0;
    { int fx = ASSAULT_FLAG_X, fz = ASSAULT_FLAG_Z, facing = 1;
      session_content_flag_position(session, "assault", &fx, &fz, &facing);
      session_init_flag(session, RASTERFALL_ASSAULT_FLAG_INDEX, fx, fz);
      session->flags[RASTERFALL_ASSAULT_FLAG_INDEX].facing=facing; }
    session->flags[RASTERFALL_ASSAULT_FLAG_INDEX].color = 0xB75A2C;
    strncpy(session->flags[RASTERFALL_ASSAULT_FLAG_INDEX].label, "ASLT", 4);
    session->flags[RASTERFALL_ASSAULT_FLAG_INDEX].label[4] = 0;
    session->flags[RASTERFALL_HURD_FLAG_INDEX].color = 0xD58A2D;
    strncpy(session->flags[RASTERFALL_HURD_FLAG_INDEX].label, "HURD", 4);
    session->flags[RASTERFALL_HURD_FLAG_INDEX].label[4] = 0;
    session->hurd_outpost.flag_index = RASTERFALL_HURD_FLAG_INDEX;
    session->hurd_outpost.minx = HURD_CONTROL_MIN_X;
    session->hurd_outpost.maxx = HURD_CONTROL_MAX_X;
    session->hurd_outpost.minz = HURD_CONTROL_MIN_Z;
    session->hurd_outpost.maxz = HURD_CONTROL_MAX_Z;
    if (session->content.spawn_maid_squad) {
        const struct rasterfall_content_formation *formation =
            session_content_formation(session, "maid_squad");
        for (i = 0; formation && i < formation->member_count; i++) {
            const struct rasterfall_content_actor *def =
                session_content_actor(session, formation->member_ids[i]);
            if (!def || session_content_character_id(def->character) !=
                RASTERFALL_CHARACTER_MAID) continue;
            int actor_id = toy_game_add_anime_flag_guard(
                &session->game_state, i + 1,
                session_content_character_id(def->character),
                def->x, def->z, def->name, RASTERFALL_MAID_FLAG_INDEX);
            if (actor_id > 0) {
                int weapon = toy_game_weapon_from_name(def->weapon);
                if (weapon >= 0)
                    toy_game_set_ai_weapon(&session->game_state, actor_id - 1,
                                           weapon);
                toy_game_assign_actor_deployment(
                    &session->game_state, actor_id - 1,
                    def->x, def->z,
                    RASTERFALL_MAID_FLAG_INDEX);
            }
        }
    }
    if (session->content.spawn_campaign_support) {
        const struct rasterfall_content_formation *formation =
            session_content_formation(session, "hurd_squad");
        for (i = 0; formation && i < formation->member_count; i++) {
            const struct rasterfall_content_actor *def =
                session_content_actor(session, formation->member_ids[i]);
            int character_id = def ?
                session_content_character_id(def->character) :
                RASTERFALL_CHARACTER_NONE;
            if (!def || character_id < RASTERFALL_CHARACTER_HURD_GUNSMITH ||
                character_id > RASTERFALL_CHARACTER_HURD_GUARD) continue;
            int actor_id = toy_game_add_character_flag_guard(
                &session->game_state, character_id,
                def->x, def->z, def->name, RASTERFALL_HURD_FLAG_INDEX);
            if (actor_id > 0) {
                int actor_index = actor_id - 1;
                session->hurd_outpost.squad_actor_indices[i] = actor_index;
                {
                    int weapon = toy_game_weapon_from_name(def->weapon);
                    if (weapon >= 0)
                        toy_game_set_ai_weapon(&session->game_state, actor_index,
                                               weapon);
                }
                toy_game_assign_actor_deployment(
                    &session->game_state, actor_index,
                    def->x, def->z,
                    RASTERFALL_HURD_FLAG_INDEX);
            }
        }
    }
    }
    toy_game_local_player_actor(&session->game_state)->x = camera->x;
    toy_game_local_player_actor(&session->game_state)->z = camera->z;
    toy_game_local_player_actor(&session->game_state)->sy = camera->sy;
    toy_game_local_player_actor(&session->game_state)->cy = camera->cy;
    toy_game_local_player_actor(&session->game_state)->pitch_sy =
        camera->pitch_sy;
    toy_game_local_player_actor(&session->game_state)->pitch_cy =
        camera->pitch_cy;
    toy_game_local_player_actor(&session->game_state)->view_y = camera->y;
    /* Map-authored and developer actors can spawn on ramps or platforms.
     * Their constructors run before/after the primitive binding and therefore
     * cannot reliably initialize ground_y themselves.  Resolve every actor's
     * initial support once the complete actor roster is present. */
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (session->game_state.actors[i].active) {
            /* Flags retain their object lifecycle, never an actor assignment. */
            session->game_state.actors[i].flag_index=-1;
            session->game_state.actors[i].flag_guard=0;
            toy_game_update_actor_ground(&session->game_state, i);
        }
    rasterfall_camera_set_body(camera,
                               session->game_state.actors[0].x,
                               session->game_state.actors[0].z);
    session->banner_ms = 0;
    session->banner_text = NULL;
    session->manual_alarm_on = 0;
    session->manual_alarm_timer = 1000;
    session->highlight_index = -1;
    session->weaver_item_index = -1;
    session->weaver_item_serial = 0;
    session->smooth_turn_remaining = 0;
    session->ai_revive_active = 0;
    session->ai_revive_actor_index = -1;
    session->managed_ai_route_phase = 0;
    session->rts_active = 0;
    session->rts_move_active = 0;
    session->managed_ai_weapon_master_target = 0;
    session->managed_ai_weapon_master_route = 0;
    session->managed_ai_ammo_rest_wave = -1;
    session->managed_ai_target_index = -1;
    session->managed_ai_retarget_ms = 0;
                session->managed_ai_escape_phase = -1;
    session_set_air_walls(session, 1);
    rasterfall_map_reset_interactables(&session->map_ops);
    session_add_content_terminals(session);
    /* Formal modular roster members are session-owned and are not removed by
     * the hired-AI path. Their lifetimes end at reset/unload. */
    rf_gpu_scene_local_world(&session->scene_local);
    session_frontier_reset(session);
    rasterfall_session_tactical_reset(session);
    for (i=1;i<TOY_GAME_REMOTE_ACTOR_BASE;++i) {
        const struct toy_game_actor *a=&session->game_state.actors[i];
        if (a->active && !a->hired &&
            rasterfall_character_visual_recipe_for_character(a->character_id)) {
            if (rf_gpu_scene_local_created(&session->scene_local,a)<0)
                session->scene_local.failed=1;
        }
    }
}

void rasterfall_camera_rotate(struct camera *camera, int turn, int pitch)
{
    int old_sy = camera->sy;
    int old_psy = camera->pitch_sy;
    long long length;
    camera->sy = (old_sy * 1024 + camera->cy * turn) / 1024;
    camera->cy = (camera->cy * 1024 - old_sy * turn) / 1024;
    length = isqrt((long long)camera->sy * camera->sy +
                   (long long)camera->cy * camera->cy);
    if (length > 0) {
        camera->sy = (int)((long long)camera->sy * 1024 / length);
        camera->cy = (int)((long long)camera->cy * 1024 / length);
    }
    camera->pitch_sy = (old_psy * 1024 + camera->pitch_cy * pitch) / 1024;
    camera->pitch_cy = (camera->pitch_cy * 1024 - old_psy * pitch) / 1024;
    length = isqrt((long long)camera->pitch_sy * camera->pitch_sy +
                   (long long)camera->pitch_cy * camera->pitch_cy);
    if (length > 0) {
        camera->pitch_sy = (int)((long long)camera->pitch_sy * 1024 / length);
        camera->pitch_cy = (int)((long long)camera->pitch_cy * 1024 / length);
    }
    if (camera->pitch_cy < RASTERFALL_PITCH_LIMIT_CY) {
        camera->pitch_sy = camera->pitch_sy < 0 ?
            -RASTERFALL_PITCH_LIMIT_SY : RASTERFALL_PITCH_LIMIT_SY;
        camera->pitch_cy = RASTERFALL_PITCH_LIMIT_CY;
    } else if (camera->pitch_sy > RASTERFALL_PITCH_LIMIT_SY) {
        camera->pitch_sy = RASTERFALL_PITCH_LIMIT_SY;
        camera->pitch_cy = RASTERFALL_PITCH_LIMIT_CY;
    } else if (camera->pitch_sy < -RASTERFALL_PITCH_LIMIT_SY) {
        camera->pitch_sy = -RASTERFALL_PITCH_LIMIT_SY;
        camera->pitch_cy = RASTERFALL_PITCH_LIMIT_CY;
    }
}

static void session_move_player(struct rasterfall_session *session,
                                struct camera *camera,
                                const struct rasterfall_command *command)
{
    struct toy_game_actor *actor = toy_game_local_player_actor(&session->game_state);
    int jump = (command->buttons & RASTERFALL_CMD_JUMP) != 0;
    int dx = camera->sy * command->move_forward + camera->cy * command->move_strafe;
    int dz = camera->cy * command->move_forward - camera->sy * command->move_strafe;
    if (!actor) return;
    /* The route proves a world-space segment, independently of combat aim.
     * Quantizing it to camera-relative keys can steer off a narrow platform. */
    if (session->rts_active) {
        dx=session->rts_move_input_x;dz=session->rts_move_input_z;
    }
    if (jump) {
        /* Capture the input direction for replay; launch speed comes from
         * actual ground acceleration, never a second airborne input step. */
        dx = command->jump_dx; dz = command->jump_dz;
    }
    int moved=session->rts_active ? toy_game_move_player_input_supported(&session->game_state,
        TOY_GAME_PLAYER_ACTOR_INDEX,dx,dz) : toy_game_move_player_input(&session->game_state,
        TOY_GAME_PLAYER_ACTOR_INDEX,dx,dz,jump);
    if (!moved &&
        session->rts_active && session->rts_move_active) {
        toy_game_actor_cancel_navigation(actor);
        if (session->game_state.update_profile)
            session->game_state.update_profile->actor_direct_blocked++;
    }
}

static void session_sync_special_motion(struct rasterfall_session *session,
                                        struct camera *camera)
{
    struct toy_game_actor *player;
    if (!session || !camera) return;
    /* The gameplay/prediction state owns the body position.  Camera keeps
     * orientation and presentation height, and is rebuilt from that state. */
    player = toy_game_local_player_actor(&session->game_state);
    if (!player) return;
    rasterfall_camera_set_body(camera, player->x, player->z);
    camera->y = RASTERFALL_STANDING_CAMERA_Y +
                player->ground_y + player->airborne_y;
}

static void session_update_smooth_turn(struct rasterfall_session *session,
                                       struct camera *camera)
{
    int step = session->smooth_turn_remaining;
    if (step > SMOOTH_TURN_STEP) step = SMOOTH_TURN_STEP;
    if (step < -SMOOTH_TURN_STEP) step = -SMOOTH_TURN_STEP;
    if (step == 0) return;
    rasterfall_camera_rotate(camera, step, 0);
    session->smooth_turn_remaining -= step;
}

void rasterfall_session_interact_remote(struct rasterfall_session *session,
                                        const struct camera *camera,
                                        int expected_kind)
{
    int index, i;
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    index = rasterfall_session_compute_highlight(session, camera);
    /* Client prediction and the host's delayed camera can disagree about two
     * nearby buttons.  Carry the highlighted pickup kind with the edge and
     * resolve that same aimed target on the authority instead of activating
     * whichever neighbour happens to win the host's distance tie. */
    if (expected_kind >= 0 &&
        (index < 0 || session->items[index].kind != expected_kind)) {
        int saved_count = session->item_count;
        for (i = 0; i < saved_count; i++) {
            struct rasterfall_interactable saved;
            if (session->items[i].kind != expected_kind) continue;
            saved = session->items[0];
            session->items[0] = session->items[i];
            session->item_count = 1;
            index = rasterfall_session_compute_highlight(session, camera);
            session->item_count = saved_count;
            session->items[0] = saved;
            if (index == 0) { index = i; break; }
            index = -1;
        }
    }
    if (index >= 0 && (!session->weaver_item_serial ||
        index != session->weaver_item_index))
        session_interact(session, &session->items[index]);
}

void rasterfall_session_toggle_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id)
{
    int i;
    if (!session || !camera || session->game_state.state != TOY_GAME_PLAYING)
        return;
    for (i = 0; i < session->flag_count; i++) {
        if (!session->flags[i].carried ||
            session->flags[i].carrier_id != player_id) continue;
        session->flags[i].carried = 0;
        session->flags[i].carrier_id = -1;
        session->flags[i].x = camera->x;
        session->flags[i].z = camera->z;
        return;
    }
    i = session_near_flag(session, camera);
    if (i < 0 || session->flags[i].carried) return;
    session->flags[i].carried = 1;
    session->flags[i].carrier_id = player_id;
}

void rasterfall_session_update_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id)
{
    int i;
    if (!session || !camera) return;
    for (i = 0; i < session->flag_count; i++)
        if (session->flags[i].carried &&
            session->flags[i].carrier_id == player_id) {
            session->flags[i].x = camera->x;
            session->flags[i].z = camera->z;
        }
}

int rasterfall_session_revive_remote(struct rasterfall_session *session,
                                     const struct camera *camera, int dt_ms)
{
    int actor_index = -1;
    const struct toy_game_actor *player;
    if (!session || !camera) return -1;
    player = toy_game_local_player_actor_const(&session->game_state);
    if (!player || player->state == TOY_GAME_ACTOR_DOWNED) return -1;
    if (!session_near_ai(session, camera, &actor_index)) return -1;
    return toy_game_revive_actor(&session->game_state, actor_index, dt_ms);
}

int rasterfall_session_revive_target(struct rasterfall_session *session,
                                     const struct camera *rescuer,
                                     const struct camera *target,
                                     int *progress_ms, int dt_ms)
{
    long long dx, dz;
    if (!session || !rescuer || !target || !progress_ms || dt_ms <= 0)
        return -1;
    dx = (long long)rescuer->x - target->x;
    dz = (long long)rescuer->z - target->z;
    if (dx * dx + dz * dz >
        (long long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE)
        return -1;
    *progress_ms += dt_ms;
    if (*progress_ms < TOY_GAME_REVIVE_MS) return 0;
    *progress_ms = 0;
    return 1;
}

void rasterfall_session_weaver_sync(struct rasterfall_session *session)
{
    struct toy_mesh_weaver *w;
    int index, ready;
    if (!session) return;
    w = &session->game_state.weaver;
    index = session->weaver_item_serial ? session->weaver_item_index : -1;
    ready = w->enabled && w->phase == TOY_WEAVER_READY;
    if (index >= 0 && index < session->item_count) {
        if (!ready) {
            for (int i = index + 1; i < session->item_count; ++i)
                session->items[i - 1] = session->items[i];
            --session->item_count;
            if (session->highlight_index == index) session->highlight_index = -1;
            else if (session->highlight_index > index) --session->highlight_index;
            session->weaver_item_index = -1;
            session->weaver_item_serial = 0;
            return;
        }
    } else {
        session->weaver_item_index = -1;
        session->weaver_item_serial = 0;
        if (!ready || session->item_count >= TOY_MAP_MAX_PICKUPS) return;
        index = session->item_count++;
        session->weaver_item_index = index;
        session->weaver_item_serial = w->job_serial;
    }
    session->items[index].kind = TOY_MAP_PICKUP_WEAPON;
    session->items[index].weapon = w->blueprint.weapon;
    session->items[index].x = w->output_x;
    session->items[index].z = w->output_z;
    session->items[index].y = w->output_y;
}

void rasterfall_session_weaver_configure(struct rasterfall_session *session,
    int enabled, int output_x, int output_z, int output_y)
{
    if (!session) return;
    session->game_state.weaver.enabled = !!enabled;
    session->game_state.weaver.output_x = output_x;
    session->game_state.weaver.output_z = output_z;
    session->game_state.weaver.output_y = output_y;
    rasterfall_session_weaver_sync(session);
}

int rasterfall_session_weaver_start(struct rasterfall_session *session,
    const struct toy_mesh_blueprint *blueprint)
{
    int reason;
    if (!session) return TOY_WEAVER_DISABLED;
    reason = toy_game_weaver_start(&session->game_state, blueprint);
    session->banner_ms = 1800;
    session->banner_success = reason == TOY_WEAVER_OK;
    session->banner_text = reason == TOY_WEAVER_OK ?
        "MESH WEAVER STARTED" : toy_mesh_weaver_reason_name(reason);
    return reason;
}

int rasterfall_session_weaver_collect(struct rasterfall_session *session)
{
    int collected;
    if (!session) return 0;
    collected = toy_game_weaver_collect(&session->game_state,
        toy_game_local_player_actor(&session->game_state));
    session->banner_ms = 1800;
    session->banner_success = collected;
    session->banner_text = collected ? "WEAPON COLLECTED - ONE MAGAZINE" :
        "NO FINISHED WEAPON";
    rasterfall_session_weaver_sync(session);
    return collected;
}

int rasterfall_session_compute_highlight(const struct rasterfall_session *session,
                                         const struct camera *camera)
{
    int i, best = -1;
    long long best_d2 = 0;
    for (i = 0; i < session->item_count; i++) {
        const struct rasterfall_interactable *it = &session->items[i];
        long long dx = (long long)it->x - camera->x;
        long long dz = (long long)it->z - camera->z;
        long long d2 = dx * dx + dz * dz;
        long long dist, dot;
        if (d2 > (long long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE ||
            d2 == 0) continue;
        dist = isqrt(d2);
        if (dist <= 0) continue;
        dot = dx * camera->sy + dz * camera->cy;
        if (dot < dist * INTERACT_AIM_CONE) continue;
        if (best < 0 || d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

static void session_client_interact_banner(struct rasterfall_session *session)
{
    const struct rasterfall_interactable *it;
    if (session->highlight_index < 0 ||
        session->highlight_index >= session->item_count) return;
    it = &session->items[session->highlight_index];
    session->banner_success = 1;
    session->banner_ms = 1800;
    if (it->kind == TOY_MAP_PICKUP_AIR_BUTTON)
        session->banner_text = session->air_walls_enabled ?
            "AIR WALLS DISABLED" : "AIR WALLS ENABLED";
    else if (it->kind == TOY_MAP_PICKUP_ALARM_BUTTON)
        session->banner_text = session->manual_alarm_on ?
            "ALARM DISABLED" : "ALARM ENABLED - 2-3 ENEMIES EACH SECOND";
    else if (it->kind == TOY_MAP_PICKUP_BUTTON)
        session->banner_text = "HORDE REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_WAVE_SKIP_BUTTON)
        session->banner_text = "NEXT WAVE REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON)
        session->banner_text = "BROWN BRUTE HORDE REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON)
        session->banner_text = "RED RUNNER HORDE REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON)
        session->banner_text = "SMOKER REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON)
        session->banner_text = "CHARGER REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_TANK_BUTTON)
        session->banner_text = "TANK REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON)
        session->banner_text = "ATTACK POINTS X2";
    else if (it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON)
        session->banner_text = "ATTACK POINTS X3";
    else if (it->kind == TOY_MAP_PICKUP_ATTACK_X4_BUTTON)
        session->banner_text = "ATTACK POINTS X4";
    else if (it->kind == TOY_MAP_PICKUP_POSE_RESET_BUTTON)
        session->banner_text = "POSE RESET";
    else if (it->kind == TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON)
        session->banner_text = "RIGHT ARM POSE";
    else if (it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON)
        session->banner_text = "ARMS POSE";
    else if (it->kind == TOY_MAP_PICKUP_POSE_BODY_BUTTON)
        session->banner_text = "BODY TURN POSE";
    else if (it->kind == TOY_MAP_PICKUP_ANIM_IDLE_BUTTON)
        session->banner_text = "EULA IDLE_LOOP";
    else if (it->kind == TOY_MAP_PICKUP_ANIM_WALK_BUTTON)
        session->banner_text = "EULA WALK";
    else if (it->kind == TOY_MAP_PICKUP_ANIM_JOG_BUTTON)
        session->banner_text = "EULA JOG_FWD";
    else if (it->kind == TOY_MAP_PICKUP_GLB_IDLE_BUTTON)
        session->banner_text = "GLB IDLE_LOOP";
    else if (it->kind == TOY_MAP_PICKUP_GLB_WALK_BUTTON)
        session->banner_text = "GLB WALK";
    else if (it->kind == TOY_MAP_PICKUP_GLB_JOG_BUTTON)
        session->banner_text = "GLB JOG_FWD";
    else if (it->kind == TOY_MAP_PICKUP_ANIMATION_COMPOSITION_BUTTON)
        session->banner_text = "WALK + RIFLE STANCE + FIRE/HIT OVERLAY";
    else if (it->kind == TOY_MAP_PICKUP_HUMANOID_POSE_DEBUG_BUTTON)
        session->banner_text = "EULA AK HUMANOID POSE DEBUGGER";
    else if (it->kind == TOY_MAP_PICKUP_WEST_CORRIDOR_BUTTON)
        session->banner_text = "WEST CORRIDOR: 16 RANDOM ENEMIES";
    else if (it->kind == TOY_MAP_PICKUP_WEST_CORRIDOR_NO_TANK_BUTTON)
        session->banner_text = "WEST CORRIDOR: 16 RANDOM (NO TANK)";
    else if (it->kind == TOY_MAP_PICKUP_ENEMY_DEATH_TEST_BUTTON)
        session->banner_text = "ENEMY DEATH TEST REQUEST SENT";
    else if (it->kind == TOY_MAP_PICKUP_AMMO)
        session->banner_text = "AMMO REFILLED";
    else if (it->kind == TOY_MAP_PICKUP_WEAPON ||
             it->kind == TOY_MAP_PICKUP_SMG ||
             it->kind == TOY_MAP_PICKUP_SHOTGUN ||
        it->kind == TOY_MAP_PICKUP_THROWABLE)
        session->banner_text = "WEAPON PICKED UP";
    else if (it->kind == TOY_MAP_PICKUP_PILL)
        session->banner_text = "PILL PICKED UP";
    else if (it->kind == TOY_MAP_PICKUP_STATION_TERMINAL) {
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
        session->banner_text = "STATION TERMINAL OPENING";
#else
        session->banner_success = 0;
        session->banner_text = RASTERFALL_DESKTOP_UNAVAILABLE_MESSAGE;
#endif
    }
    else if (it->kind == TOY_MAP_PICKUP_OPERATIONS_TERMINAL)
        session->banner_text = "OPERATIONS: CAMPAIGN 01 READY";
    else if (it->kind == TOY_MAP_PICKUP_SUPER_TERMINAL)
        session->banner_text = "SUPER TERMINAL ACCESS RESTRICTED";
    else if (it->kind == TOY_MAP_PICKUP_RETURN_OUTPOST)
        session->banner_text = "RETURNING TO OUTPOST";
    else
        session->banner_text = "INTERACTION SENT TO HOST";
}

static void session_interact(struct rasterfall_session *session,
                             struct rasterfall_interactable *it)
{
    if (session->weaver_item_serial && session->weaver_item_index >= 0 &&
        session->weaver_item_index < session->item_count &&
        it == &session->items[session->weaver_item_index]) {
        rasterfall_session_weaver_collect(session);
        return;
    }
    struct toy_game_actor *player =
        toy_game_local_player_actor(&session->game_state);
    toy_game_emit_event(&session->game_state, TOY_GAME_EV_BUTTON);
    session->banner_success = 1;
    if (it->kind == TOY_MAP_PICKUP_SMG ||
        it->kind == TOY_MAP_PICKUP_SHOTGUN ||
        it->kind == TOY_MAP_PICKUP_AMMO ||
        it->kind == TOY_MAP_PICKUP_WEAPON ||
        it->kind == TOY_MAP_PICKUP_THROWABLE)
        toy_game_emit_event(&session->game_state, TOY_GAME_EV_PICKUP);
    if (it->kind == TOY_MAP_PICKUP_STATION_TERMINAL) {
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
        session->station_gui_request = 1;
        session->banner_ms = 0;
        session->banner_text = NULL;
#else
        session->station_gui_request = 0;
        session->banner_ms = 2200;
        session->banner_success = 0;
        session->banner_text = RASTERFALL_DESKTOP_UNAVAILABLE_MESSAGE;
#endif
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_OPERATIONS_TERMINAL) {
        rasterfall_session_request_world(session,
                                         RASTERFALL_WORLD_CAMPAIGN_01);
        session->banner_ms = 800;
        session->banner_text = "DEPLOYING CAMPAIGN 01";
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_SUPER_TERMINAL) {
        session->banner_ms = 2200;
        session->banner_success = 0;
        session->banner_text = "SUPER TERMINAL: ACCESS RESTRICTED";
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_RETURN_OUTPOST) {
        rasterfall_session_request_world(session, RASTERFALL_WORLD_OUTPOST);
        session->banner_ms = 800;
        session->banner_text = "RETURNING TO OUTPOST";
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_BUTTON) {
        int n;
        session->banner_ms = 3500;
        session->banner_text = "HORDE SUMMONED - THEY WILL FIND YOU";
        n = toy_game_spawn_horde(&session->game_state, HORDE_COUNT_MIN,
                                 HORDE_COUNT_MAX, session->spawn_zones,
                                 session->spawn_count, HORDE_MIN_PLAYER_DIST);
        __printf("rasterfall: horde summoned %d tracking enemies\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_WAVE_SKIP_BUTTON) {
        session->banner_ms = 1200;
        session->banner_text = toy_game_skip_wave_rest(&session->game_state) ?
            "NEXT WAVE STARTING" : "WAVE ALREADY STARTED";
    } else if (it->kind == TOY_MAP_PICKUP_AIR_BUTTON) {
        session_set_air_walls(session, !session->air_walls_enabled);
        session->banner_ms = 1800;
        session->banner_text = session->air_walls_enabled ?
            "AIR WALLS ENABLED" : "AIR WALLS DISABLED";
    } else if (it->kind == TOY_MAP_PICKUP_ALARM_BUTTON) {
        session->manual_alarm_on = !session->manual_alarm_on;
        session->manual_alarm_timer = 1000;
        session->banner_ms = 1800;
        session->banner_text = session->manual_alarm_on ?
            "ALARM ENABLED - 2-3 ENEMIES EACH SECOND" : "ALARM DISABLED";
    } else if (it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON) {
        int n = toy_game_spawn_horde_type(&session->game_state,
            TOY_GAME_ENEMY_PURSUIT_HEAVY, 2, 3, session->spawn_zones,
            session->spawn_count, HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 3000;
        session->banner_text = "BROWN BRUTE HORDE SUMMONED";
        __printf("rasterfall: heavy pursuit enemies summoned %d\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON) {
        int n = toy_game_spawn_horde_type(&session->game_state,
            TOY_GAME_ENEMY_PURSUIT_FAST, 2, 3, session->spawn_zones,
            session->spawn_count, HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 3000;
        session->banner_text = "RED RUNNER HORDE SUMMONED";
        __printf("rasterfall: fast pursuit enemies summoned %d\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ||
               it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ||
               it->kind == TOY_MAP_PICKUP_TANK_BUTTON) {
        int type = it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ?
                   TOY_GAME_ENEMY_SMOKER :
                   it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ?
                   TOY_GAME_ENEMY_CHARGER : TOY_GAME_ENEMY_TANK;
        int n = toy_game_spawn_horde_type(&session->game_state, type, 1, 1,
                                          session->spawn_zones,
                                          session->spawn_count,
                                          HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 2500;
        session->banner_text = it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ?
            "SMOKER SUMMONED" :
            it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ?
            "CHARGER SUMMONED" : "TANK SUMMONED";
        __printf("rasterfall: special test enemy summoned type %d (%d)\n",
                  type, n);
    } else if (it->kind == TOY_MAP_PICKUP_WEST_CORRIDOR_BUTTON) {
        struct toy_game_box corridor_spawn = {
            -44400, -43400, -1650, 1650, 0, 0
        };
        int n = toy_game_spawn_random_horde(&session->game_state, 16,
                                            &corridor_spawn, 1,
                                            HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 3500;
        session->banner_text = "WEST CORRIDOR: RANDOM HORDE SUMMONED";
        __printf("rasterfall: west corridor random horde summoned %d/16 enemies\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_WEST_CORRIDOR_NO_TANK_BUTTON) {
        struct toy_game_box corridor_spawn = {
            -44400, -43400, -1650, 1650, 0, 0
        };
        int n = toy_game_spawn_random_horde_no_tank(
            &session->game_state, 16, &corridor_spawn, 1,
            HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 3500;
        session->banner_text = "WEST CORRIDOR: RANDOM HORDE (NO TANK)";
        __printf("rasterfall: west corridor random no-tank horde summoned %d/16 enemies\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_ENEMY_DEATH_TEST_BUTTON) {
        static const int types[6] = {
            TOY_GAME_ENEMY_PURSUIT_COMMON,
            TOY_GAME_ENEMY_PURSUIT_FAST,
            TOY_GAME_ENEMY_PURSUIT_HEAVY,
            TOY_GAME_ENEMY_PURSUIT_COMMON,
            TOY_GAME_ENEMY_PURSUIT_FAST,
            TOY_GAME_ENEMY_PURSUIT_HEAVY
        };
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        int spawned = 0, killed = 0, row;
        for (row = 0; row < 6; row++) {
            struct toy_game_box point = {
                11000 + row * 1200, 11000 + row * 1200,
                -13500, -13500, 0, 0
            };
            unsigned char occupied[TOY_GAME_MAX_ENEMIES];
            int enemy_index;
            for (enemy_index = 0; enemy_index < TOY_GAME_MAX_ENEMIES;
                 enemy_index++)
                occupied[enemy_index] =
                    session->game_state.enemies[enemy_index].active != 0;
            if (toy_game_spawn_horde_type(&session->game_state, types[row],
                                          1, 1, &point, 1, 0) != 1)
                continue;
            spawned++;
            for (enemy_index = 0; enemy_index < TOY_GAME_MAX_ENEMIES;
                 enemy_index++)
                if (!occupied[enemy_index] &&
                    session->game_state.enemies[enemy_index].active == 1)
                    break;
            if (enemy_index < TOY_GAME_MAX_ENEMIES &&
                toy_game_apply_reported_hit(&session->game_state, player,
                    enemy_index,
                    session->game_state.enemies[enemy_index].hp) == 2)
                killed++;
        }
        session->banner_ms = 2500;
        session->banner_text = killed == 6 ?
            "ENEMY DEATH TEST: 6 LETHAL HITS" :
            "ENEMY DEATH TEST: PARTIAL (ENEMY SLOTS BUSY)";
        __printf("rasterfall: enemy death test spawned=%d killed=%d\n",
                 spawned, killed);
    } else if (it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON ||
               it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON ||
               it->kind == TOY_MAP_PICKUP_ATTACK_X4_BUTTON) {
        int multiplier = it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON ? 2 :
                         it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON ? 3 : 4;
        toy_game_set_wave_attack_multiplier(&session->game_state, multiplier);
        session->banner_ms = 2000;
        session->banner_text = multiplier == 2 ? "ATTACK POINTS X2" :
            multiplier == 3 ? "ATTACK POINTS X3" : "ATTACK POINTS X4";
    } else if (it->kind == TOY_MAP_PICKUP_POSE_RESET_BUTTON ||
               it->kind == TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON ||
               it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON ||
               it->kind == TOY_MAP_PICKUP_POSE_BODY_BUTTON) {
        session->skeletal_demo_pose =
            it->kind == TOY_MAP_PICKUP_POSE_RESET_BUTTON ?
                RASTERFALL_MODEL_POSE_BIND :
            it->kind == TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON ?
                RASTERFALL_MODEL_POSE_RIGHT_ARM :
            it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON ?
                RASTERFALL_MODEL_POSE_ARMS : RASTERFALL_MODEL_POSE_BODY_TURN;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id =
            it->kind == TOY_MAP_PICKUP_POSE_RESET_BUTTON ? -1 :
            it->kind == TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON ? 0 :
            it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON ? 1 : 2;
        session->skeletal_demo_player.time_ms = 0;
        session->skeletal_demo_player.playing =
            session->skeletal_demo_player.clip_id >= 0;
        session->skeletal_demo_player.loop =
            session->skeletal_demo_player.clip_id == 1;
        session->banner_ms = 1800;
        session->banner_text =
            session->skeletal_demo_pose == RASTERFALL_MODEL_POSE_BIND ?
                "POSE RESET" :
            session->skeletal_demo_pose == RASTERFALL_MODEL_POSE_RIGHT_ARM ?
                "RIGHT ARM POSE" :
            session->skeletal_demo_pose == RASTERFALL_MODEL_POSE_ARMS ?
                "ARMS POSE" : "BODY TURN POSE";
    } else if (it->kind == TOY_MAP_PICKUP_ANIM_IDLE_BUTTON ||
               it->kind == TOY_MAP_PICKUP_ANIM_WALK_BUTTON ||
               it->kind == TOY_MAP_PICKUP_ANIM_JOG_BUTTON) {
        session->skeletal_demo_pose = RASTERFALL_MODEL_POSE_BIND;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id =
            it->kind == TOY_MAP_PICKUP_ANIM_IDLE_BUTTON ? 3 :
            it->kind == TOY_MAP_PICKUP_ANIM_WALK_BUTTON ? 4 : 5;
        session->skeletal_demo_player.time_ms = 0;
        session->skeletal_demo_player.playing = 1;
        session->skeletal_demo_player.loop = 1;
        session->banner_ms = 1800;
        session->banner_text = it->kind == TOY_MAP_PICKUP_ANIM_IDLE_BUTTON ?
            "EULA IDLE_LOOP" : it->kind == TOY_MAP_PICKUP_ANIM_WALK_BUTTON ?
            "EULA WALK" : "EULA JOG_FWD";
    } else if (it->kind == TOY_MAP_PICKUP_GLB_IDLE_BUTTON ||
               it->kind == TOY_MAP_PICKUP_GLB_WALK_BUTTON ||
               it->kind == TOY_MAP_PICKUP_GLB_JOG_BUTTON) {
        session->skeletal_demo_pose = RASTERFALL_MODEL_POSE_BIND;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id =
            it->kind == TOY_MAP_PICKUP_GLB_IDLE_BUTTON ? 6 :
            it->kind == TOY_MAP_PICKUP_GLB_WALK_BUTTON ? 7 : 8;
        session->skeletal_demo_player.time_ms = 0;
        session->skeletal_demo_player.playing = 1;
        session->skeletal_demo_player.loop = 1;
        session->banner_ms = 1800;
        session->banner_text = it->kind == TOY_MAP_PICKUP_GLB_IDLE_BUTTON ?
            "GLB IDLE_LOOP" : it->kind == TOY_MAP_PICKUP_GLB_WALK_BUTTON ?
            "GLB WALK" : "GLB JOG_FWD";
    } else if (it->kind == TOY_MAP_PICKUP_VMD_WALK_BUTTON) {
        session->skeletal_demo_pose = RASTERFALL_MODEL_POSE_BIND;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id = 9;
        session->skeletal_demo_player.time_ms = 0;
        session->skeletal_demo_player.playing = 1;
        session->skeletal_demo_player.loop = 1;
        session->banner_ms = 1800;
        session->banner_text = "VMD WALK (5 CHARACTER LINEUP)";
    } else if (it->kind == TOY_MAP_PICKUP_VMD_MANJUSAKA_BUTTON) {
        session->skeletal_demo_pose = RASTERFALL_MODEL_POSE_BIND;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id = 10;
        session->skeletal_demo_player.time_ms = 0;
        session->skeletal_demo_player.playing = 1;
        session->skeletal_demo_player.loop = 1;
        session->banner_ms = 1800;
        session->banner_text = "VMD MANJUSAKA (EULA DIRECT)";
    } else if (it->kind == TOY_MAP_PICKUP_ANIMATION_COMPOSITION_BUTTON) {
        session->skeletal_demo_pose = RASTERFALL_MODEL_POSE_BIND;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id = 11;
        session->skeletal_demo_player.time_ms = 0;
        session->skeletal_demo_player.playing = 1;
        session->skeletal_demo_player.loop = 1;
        session->banner_ms = 2400;
        session->banner_text = "COMPOSITION: WALK + RIFLE + FIRE/HIT";
    } else if (it->kind == TOY_MAP_PICKUP_HUMANOID_POSE_DEBUG_BUTTON) {
        session->pose_debug_active = 1;
        session->pose_debug_bone = 0;
        session->pose_debug_axis = 0;
        session->skeletal_demo_player.clip = NULL;
        session->skeletal_demo_player.clip_id = 11;
        session->skeletal_demo_player.playing = 1;
        session->skeletal_demo_player.loop = 1;
        session->banner_ms = 2200;
        session->banner_text = "EULA AK HUMANOID POSE DEBUGGER";
    } else if (it->kind == TOY_MAP_PICKUP_HUMANOID_ACTIONS_BUTTON) {
        session->humanoid_debug_action =
            (session->humanoid_debug_action + 1) %
            RASTERFALL_HUMANOID_DEBUG_ACTION_COUNT;
        session->humanoid_debug_time_ms = 0;
        session->banner_ms = 1800;
        session->banner_text =
            session->humanoid_debug_action == RASTERFALL_HUMANOID_DEBUG_IDLE ?
                "V2 ACTION: IDLE" :
            session->humanoid_debug_action == RASTERFALL_HUMANOID_DEBUG_WALK ?
                "V2 ACTION: WALK" :
            session->humanoid_debug_action == RASTERFALL_HUMANOID_DEBUG_AIM ?
                "V2 ACTION: RIFLE AIM" : "V2 ACTION: AIM + RECOIL";
    } else if (it->kind == TOY_MAP_PICKUP_AMMO) {
        int supplied=toy_game_actor_refill_ammo(&session->game_state, player)>0;
        if(session->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01) {
            for(int n=0;n<5;++n) {
                int index=session->frontier_squad_indices[n];
                if(index<0 || index>=TOY_GAME_MAX_ACTORS)continue;
                struct toy_game_actor *a=&session->game_state.actors[index];
                long long dx=(long long)a->x-it->x,dz=(long long)a->z-it->z;
                if(a->active && a->actor_id==session->frontier_squad_actor_ids[n] &&
                   a->combat_generation==session->frontier_squad_generations[n] &&
                   a->state==TOY_GAME_ACTOR_ALIVE &&
                   a->faction==TOY_GAME_FACTION_ALLIED &&
                   abs(a->ground_y-player->ground_y)<=512 && dx*dx+dz*dz<=1024LL*1024)
                    supplied+=toy_game_actor_refill_ammo(&session->game_state,a)>0;
            }
            snprintf(session->supply_message,sizeof(session->supply_message),
                "弹药补给：%d 人已补充；队友需靠近补给箱 2 米内",supplied);
            session->banner_text=session->supply_message;session->banner_ms=2400;
        }
    } else if (it->kind == TOY_MAP_PICKUP_WEAPON ||
        it->kind == TOY_MAP_PICKUP_THROWABLE ||
        it->kind == TOY_MAP_PICKUP_PILL) {
        if (it->kind == TOY_MAP_PICKUP_THROWABLE ||
            it->kind == TOY_MAP_PICKUP_PILL) {
            toy_game_actor_equip_weapon(&session->game_state, player, it->weapon);
            return;
        }
        toy_game_actor_equip_weapon(&session->game_state, player, it->weapon);
    } else {
        int weapon = it->kind == TOY_MAP_PICKUP_SMG ?
            TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN;
        toy_game_actor_equip_weapon(&session->game_state, player, weapon);
    }
}

static int session_near_flag(const struct rasterfall_session *s,
                             const struct camera *camera)
{
    int i, best = -1;
    long long best_d2 = 0;
    for (i = 0; i < s->flag_count; i++) {
        long long dx, dz, d2;
        if (!s->flags[i].active || s->flags[i].carried) continue;
        dx = (long long)camera->x - s->flags[i].x;
        dz = (long long)camera->z - s->flags[i].z;
        d2 = dx * dx + dz * dz;
        if (d2 <= (long long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE &&
            (best < 0 || d2 < best_d2)) { best = i; best_d2 = d2; }
    }
    return best;
}

static void session_toggle_flag(struct rasterfall_session *s,
                                struct camera *camera)
{
    int i = s->carried_flag;
    if (i >= 0) {
        s->flags[i].carried = 0;
        s->flags[i].carrier_id = -1;
        s->flags[i].x = camera->x;
        s->flags[i].z = camera->z;
        s->carried_flag = -1;
        s->banner_text = "FLAG PLANTED"; s->banner_ms = 1400;
        return;
    }
    i = session_near_flag(s, camera);
    if (i < 0) return;
    s->flags[i].carried = 1; s->flags[i].carrier_id = 0;
    s->carried_flag = i;
    s->banner_text = "FLAG CARRIED"; s->banner_ms = 1400;
}

static void session_update_carried_flag(struct rasterfall_session *s,
                                        const struct camera *camera)
{
    int i;
    if (!s || !camera) return;
    i = s->carried_flag;
    if (i < 0 || i >= s->flag_count || !s->flags[i].carried) return;
    s->flags[i].x = camera->x;
    s->flags[i].z = camera->z;
}

static void session_update_manual_alarm(struct rasterfall_session *session,
                                        int dt_ms)
{
    if (!session->manual_alarm_on ||
        session->game_state.state != TOY_GAME_PLAYING) return;
    session->manual_alarm_timer -= dt_ms;
    if (session->manual_alarm_timer > 0) return;
    session->manual_alarm_timer += 1000;
    toy_game_spawn_horde(&session->game_state, 2, 3, session->spawn_zones,
                         session->spawn_count, HORDE_MIN_PLAYER_DIST);
}

static int session_managed_ai_active(const struct rasterfall_session *session)
{
    int i;
    if (!session) return 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (session->ai_registry.agents[i].active &&
            session->ai_registry.agents[i].controller ==
                RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER &&
            session->ai_registry.agents[i].actor_index ==
                TOY_GAME_PLAYER_ACTOR_INDEX)
            return 1;
    return 0;
}

int rasterfall_session_set_managed_ai(struct rasterfall_session *session,
                                      int active)
{
    if (!session) return 0;
    session->managed_ai_enabled = active != 0;
    if (!active) {
        session->managed_ai_target_index = -1;
        session->managed_ai_retarget_ms = 0;
        session->managed_ai_escape_phase = -1;
        rasterfall_ai_registry_remove(
            &session->ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX);
        return 1;
    }
    return rasterfall_ai_registry_add(
        &session->ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX,
        RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER, 100,
        RASTERFALL_AI_POLICY_MANAGED_SIMPLE) >= 0;
}

static int session_managed_ai_face(struct camera *camera, int x, int z,
                                   int dt_ms);

void rasterfall_session_set_rts(struct rasterfall_session *session, int active)
{
    if (!session) return;
    if (session->rts_active != (active != 0))
        toy_game_actor_cancel_navigation(toy_game_local_player_actor(&session->game_state));
    session->rts_active = active != 0;
    /* A view switch does not cancel an already accepted order. FPS input
     * temporarily owns the local body; RTS resumes the same destination. */
}

void rasterfall_session_rts_move_player(struct rasterfall_session *session,
                                        int x, int z)
{
    if (!session || !session->rts_active) return;
    toy_game_actor_cancel_navigation(toy_game_local_player_actor(&session->game_state));
    session->rts_move_x = x;
    session->rts_move_z = z;
    struct toy_game_actor *actor=toy_game_local_player_actor(&session->game_state);
    session->rts_move_y=toy_game_query_ground(&session->game_state,x,z,
        RASTERFALL_PLAYER_RADIUS,actor->ground_y).support_y;
    session->rts_move_active = 1;
}

int rasterfall_session_rts_teleport_player(struct rasterfall_session *session,
                                          struct camera *camera,
                                          int x, int surface_y, int z)
{
    struct toy_game_actor *player;
    struct toy_game_ground_query ground;
    int height = surface_y + 900; /* Render floor origin -> gameplay feet. */
    if (!session || !camera || !session->rts_active ||
        session->game_state.state != TOY_GAME_PLAYING) return 0;
    player = toy_game_local_player_actor(&session->game_state);
    if (!player || player->state != TOY_GAME_ACTOR_ALIVE ||
        player->control_disabled) return 0;
    ground = toy_game_query_ground(&session->game_state, x, z,
                                   RASTERFALL_PLAYER_RADIUS, height);
    /* Visible paint, walls and the world backdrop are not support. */
    /* Ray intersection and integer ramp interpolation can differ by 1 RFU. */
    if (!ground.has_support || ground.support_y < height - 2 ||
        ground.support_y > height + 2 ||
        toy_game_position_blocked_at_height(&session->game_state, x, z,
                                            RASTERFALL_PLAYER_RADIUS, ground.support_y))
        return 0;
    player->x = x;
    player->z = z;
    player->ground_y = ground.support_y;
    player->airborne_ms = player->airborne_y = player->vertical_velocity = 0;
    player->air_x = player->air_z = 0;
    player->air_skip_horizontal_step = 0;
    player->air_velocity_remainder_x = player->air_velocity_remainder_z = 0;
    player->move_velocity_x = player->move_velocity_z = 0;
    player->move_remainder_x = player->move_remainder_z = 0;
    player->jump_coyote_steps = player->jump_buffer_steps = 0;
    player->knockback_x = player->knockback_z = 0;
    session->rts_move_active = 0;
    toy_game_actor_cancel_navigation(player);
    session_sync_special_motion(session, camera);
    return 1;
}

int rasterfall_session_rts_move_flag(struct rasterfall_session *session,
                                     int flag_index, int x, int z)
{
    struct rasterfall_flag *flag;
    if (!session || !session->rts_active || flag_index < 0 ||
        flag_index >= session->flag_count) return 0;
    flag = &session->flags[flag_index];
    if (!flag->active) return 0;
    flag->x = x;
    flag->z = z;
    flag->carried = 0;
    flag->carrier_id = -1;
    if (session->carried_flag == flag_index) session->carried_flag = -1;
    return 1;
}

int rasterfall_session_rts_order_actor_height(struct rasterfall_session *s,
    int index,int actor_id,unsigned generation,int x,int y,int z,int stop,int height_active)
{
    struct toy_game_actor *a;
    struct toy_game_ground_query ground;
    if(!s || !s->rts_active || s->game_state.state!=TOY_GAME_PLAYING ||
        index<0 || index>=TOY_GAME_MAX_ACTORS)return 0;
    a=&s->game_state.actors[index];
    if(!a->active || a->actor_id!=actor_id || a->combat_generation!=generation ||
        a->state!=TOY_GAME_ACTOR_ALIVE || a->faction!=TOY_GAME_FACTION_ALLIED ||
        a->developer_only || a->base_core || a->animation_demo || a->control_disabled ||
        a->movement_hold_token)return 0;
    if(a!=toy_game_local_player_actor(&s->game_state) && a->kind!=TOY_GAME_ACTOR_AI)return 0;
    if(stop){x=a->x;z=a->z;y=a->ground_y;}
    else {
        if(x<-2000000 || x>2000000 || z<-2000000 || z>2000000)return 0;
        if(height_active && (y<-1000000 || y>1000000))return 0;
        ground=toy_game_query_ground(&s->game_state,x,z,RASTERFALL_PLAYER_RADIUS,height_active?y:a->ground_y);
        if(!ground.has_support || (height_active && abs(ground.support_y-y)>2) ||
            toy_game_position_blocked_at_height(&s->game_state,
            x,z,RASTERFALL_PLAYER_RADIUS,ground.support_y))return 0;
        y=ground.support_y;
    }
    if(a==toy_game_local_player_actor(&s->game_state)) {
        toy_game_actor_cancel_navigation(a);
        if(stop) {
            s->rts_move_active=0;a->moving=0;
            a->move_velocity_x=a->move_velocity_z=0;
            a->move_remainder_x=a->move_remainder_z=0;
        }
        else {rasterfall_session_rts_move_player(s,x,z);s->rts_move_y=y;}
    } else {
        toy_game_actor_cancel_rescue(&s->game_state,index);
        toy_game_actor_cancel_navigation(a);
        a->command_destination_active=1;a->command_x=x;a->command_z=z;
        a->command_y=y;a->command_height_active=1;
        a->nav_active=0;
        if(stop) {
            a->moving=0;
            a->move_velocity_x=a->move_velocity_z=0;
            a->move_remainder_x=a->move_remainder_z=0;
        }
    }
    return 1;
}

int rasterfall_session_rts_order_actor(struct rasterfall_session *s,
    int index,int actor_id,unsigned generation,int x,int z,int stop)
{ return rasterfall_session_rts_order_actor_height(s,index,actor_id,generation,x,0,z,stop,0); }

static void session_build_rts_command(struct rasterfall_session *session,
                                      struct camera *camera,
                                      struct rasterfall_command *command,
                                      int dt_ms)
{
    struct toy_game_actor *player = toy_game_local_player_actor(&session->game_state);
    struct toy_game_combat_target target;
    int steer_x = session->rts_move_x, steer_z = session->rts_move_z;
    int step, planned = 0;
    memset(command, 0, sizeof(*command));
    session->rts_move_input_x=session->rts_move_input_z=0;
    if (!player || player->state != TOY_GAME_ACTOR_ALIVE) return;
    step = toy_game_player_move_step(&session->game_state,player);
    if (session->rts_move_active) {
        long long dx = (long long)session->rts_move_x - player->x;
        long long dz = (long long)session->rts_move_z - player->z;
        if (dx * dx + dz * dz <= 250LL * 250LL &&
            abs(player->ground_y-session->rts_move_y)<=2) {
            session->rts_move_active = 0;
            toy_game_actor_cancel_navigation(player);
            player->move_velocity_x = player->move_velocity_z = 0;
            player->move_remainder_x = player->move_remainder_z = 0;
        } else if (!player->control_disabled && !player->movement_hold_token &&
                   player->airborne_ms <= 0) {
            planned = toy_game_actor_navigation_target_height(&session->game_state,
                player, session->rts_move_x,session->rts_move_y,session->rts_move_z, step, dt_ms,
                &steer_x, &steer_z);
        } else toy_game_actor_cancel_navigation(player);
    }
    if (toy_game_find_combat_target(&session->game_state, player, &target)) {
        int dx = target.x - player->x, dz = target.z - player->z;
        int distance = isqrt((long long)dx * dx + (long long)dz * dz);
        if (distance > 0) {
            camera->pitch_sy = (target.y - player->ground_y - player->airborne_y -
                RASTERFALL_HUMAN_EYE_HEIGHT_RFU) * 1024 / distance;
            camera->pitch_cy = 1024;
        }
        if (session_managed_ai_face(camera, target.x, target.z, dt_ms)) {
            command->buttons |= RASTERFALL_CMD_FIRE;
            command->fire_held = 1;
        }
        if (player->slots[player->current_slot].mag == 0)
            command->buttons |= RASTERFALL_CMD_RELOAD;
    } else if (session->rts_move_active) {
        session_managed_ai_face(camera, steer_x, steer_z, dt_ms);
    }
    if (session->rts_move_active && planned) {
        long long dx = (long long)steer_x - player->x;
        long long dz = (long long)steer_z - player->z;
        rf_direction_q10(dx,dz,&session->rts_move_input_x,&session->rts_move_input_z);
        /* A temporary waypoint must not get stuck outside Game's
         * step+24 arrival radius in the larger final-goal input deadzone. */
        long long threshold = player->nav_active ? (long long)step * 1024 / 2 : 160000;
        long long forward = dx * camera->sy + dz * camera->cy;
        long long strafe = dx * camera->cy - dz * camera->sy;
        if (forward > threshold) command->move_forward = 1;
        else if (forward < -threshold) command->move_forward = -1;
        if (strafe > threshold) command->move_strafe = 1;
        else if (strafe < -threshold) command->move_strafe = -1;
    }
}

int rasterfall_session_rts_logic_test(void)
{
    {
        struct camera numeric_camera={0};
        int i;
        numeric_camera.cy=1024;
        for(i=0;i<100;i++)session_managed_ai_face(&numeric_camera,30,-19,16);
        if(!rf_direction_valid(numeric_camera.sy,numeric_camera.cy))return 90;
        int sy=numeric_camera.sy,cy=numeric_camera.cy;
        session_managed_ai_face(&numeric_camera,0,0,16);
        if(sy!=numeric_camera.sy || cy!=numeric_camera.cy)return 91;
    }
    static struct rasterfall_session test;
    struct camera camera;
    struct rasterfall_command command;
    memset(&test, 0, sizeof(test));
    memset(&camera, 0, sizeof(camera));
    toy_game_init(&test.game_state, 1);
    camera.cy = camera.pitch_cy = 1024;
    rasterfall_session_set_rts(&test, 1);
    rasterfall_session_rts_move_player(&test, 3000, 0);
    session_build_rts_command(&test, &camera, &command, 16);
    if (!test.rts_move_active || command.move_strafe != 1) return 1;
    test.game_state.enemies[0].active = 1;
    test.game_state.enemies[0].hp = 100;
    test.game_state.enemies[0].z = 1000;
    session_build_rts_command(&test, &camera, &command, 16);
    if (!(command.buttons & RASTERFALL_CMD_FIRE) || !command.fire_held ||
        command.move_strafe != 1) return 2;
    test.flag_count = 1;
    test.flags[0].active = 1;
    if (!rasterfall_session_rts_move_flag(&test, 0, 1200, 1300) ||
        test.flags[0].x != 1200 || test.flags[0].z != 1300) return 3;
    rasterfall_session_set_rts(&test, 0);
    if (!test.rts_move_active ||
        rasterfall_session_rts_move_flag(&test, 0, 0, 0)) return 4;
    {
        struct toy_map_primitive floors[3];
        struct toy_game_actor *player = toy_game_local_player_actor(&test.game_state);
        memset(floors, 0, sizeof(floors));
        floors[0].minx = floors[0].minz = -2000;
        floors[0].maxx = floors[0].maxz = 2000;
        floors[0].flags = TOY_MAP_PRIMITIVE_WALKABLE;
        floors[1] = floors[0];
        floors[1].minx = 3000; floors[1].maxx = 6000;
        floors[1].surface_y0 = floors[1].surface_y1 = 1200;
        floors[2] = floors[0];
        floors[2].shape = TOY_MAP_PRIMITIVE_BOX;
        floors[2].minx = floors[2].minz = -400;
        floors[2].maxx = floors[2].maxz = 400;
        floors[2].surface_y0 = floors[2].surface_y1 = 2000;
        floors[2].flags = TOY_MAP_PRIMITIVE_COLLISION;
        test.game_state.primitives = floors;
        test.game_state.primitive_count = 3;
        test.game_state.room_limit = 8000;
        if (rasterfall_session_rts_teleport_player(&test, &camera, 1000, -900, 0)) return 5;
        rasterfall_session_set_rts(&test, 1);
        player->airborne_ms = 200; player->airborne_y = 300;
        player->vertical_velocity = -40; player->air_x = 50;
        test.rts_move_active = 1;
        if (!rasterfall_session_rts_teleport_player(&test, &camera, 4000, 300, 0) ||
            player->x != 4000 || player->ground_y != 1200 ||
            camera.x != 4000 || camera.y != RASTERFALL_STANDING_CAMERA_Y + 1200 ||
            player->airborne_ms || player->airborne_y || player->vertical_velocity ||
            player->air_x || test.rts_move_active) return 6;
        if (rasterfall_session_rts_teleport_player(&test, &camera, 7000, -900, 0) ||
            rasterfall_session_rts_teleport_player(&test, &camera, 0, -900, 0) ||
            rasterfall_session_rts_teleport_player(&test, &camera, 4000, -900, 0) ||
            player->x != 4000 || player->ground_y != 1200) return 7;
        player->control_disabled = 1;
        if (rasterfall_session_rts_teleport_player(&test, &camera, 1000, -900, 0)) return 8;
        test.game_state.primitives = NULL;
    }
    /* Individual orders survive flag movement and FPS/RTS switches. Reused
     * identities, downed actors and invalid ground never receive an order. */
    memset(&test,0,sizeof(test));toy_game_init(&test.game_state,7);
    struct toy_map_primitive order_floor={0};
    order_floor.minx=order_floor.minz=-8000;order_floor.maxx=order_floor.maxz=8000;
    order_floor.flags=TOY_MAP_PRIMITIVE_WALKABLE;
    test.game_state.primitives=&order_floor;test.game_state.primitive_count=1;
    test.game_state.room_limit=8000;
    rasterfall_session_set_rts(&test,1);
    int id=toy_game_add_ai(&test.game_state,TOY_GAME_AI_LEVEL_2,1000,1000,"ORDER_TEST");
    if(id<1)return 9;
    struct toy_game_actor *a=&test.game_state.actors[id-1];
    if(!rasterfall_session_rts_order_actor(&test,id-1,a->actor_id,a->combat_generation,3000,1000,0))return 10;
    test.flag_count=1;test.flags[0].active=1;a->flag_index=0;
    rasterfall_session_rts_move_flag(&test,0,-5000,-5000);
    if(a->command_x!=3000 || a->deployment_x!=1000)return 11;
    rasterfall_session_set_rts(&test,0);
    for(int n=0;n<12;++n)toy_game_update_ai_teammates(&test.game_state,16);
    if(a->x<=1000 || a->command_x!=3000)return 12;
    rasterfall_session_set_rts(&test,1);
    if(!rasterfall_session_rts_order_actor(&test,id-1,a->actor_id,a->combat_generation,0,0,1) ||
        a->command_x!=a->x || a->command_z!=a->z)return 13;
    int stopped=a->x;
    for(int n=0;n<12;++n)toy_game_update_ai_teammates(&test.game_state,16);
    if(a->x!=stopped || rasterfall_session_rts_order_actor(&test,id-1,a->actor_id,
        a->combat_generation+1,3000,1000,0))return 14;
    a->state=TOY_GAME_ACTOR_DOWNED;
    if(rasterfall_session_rts_order_actor(&test,id-1,a->actor_id,a->combat_generation,3000,1000,0))return 15;
    test.game_state.primitives=NULL;
    /* Local RTS shares planning but Session advances the body exactly once.
     * A generic wall forces a route; temporary waypoints never finish the
     * accepted final order or fall into the old larger input deadzone. */
    memset(&test,0,sizeof(test));memset(&camera,0,sizeof(camera));
    toy_game_init(&test.game_state,31);test.game_state.external_director=1;
    struct toy_map_primitive route[2]={{0}};
    route[0].shape=TOY_MAP_PRIMITIVE_FLAT;
    route[0].minx=route[0].minz=-8000;route[0].maxx=route[0].maxz=8000;
    route[0].flags=TOY_MAP_PRIMITIVE_WALKABLE|TOY_MAP_PRIMITIVE_COLLISION;
    route[1].shape=TOY_MAP_PRIMITIVE_BOX;route[1].flags=TOY_MAP_PRIMITIVE_COLLISION;
    route[1].minx=-350;route[1].maxx=350;route[1].minz=-1200;route[1].maxz=1200;
    route[1].surface_y0=route[1].surface_y1=2000;
    toy_game_set_primitives(&test.game_state,route,2,8000);
    rasterfall_session_set_rts(&test,1);
    if(!rasterfall_session_rts_teleport_player(&test,&camera,-2000,-900,0))return 16;
    struct toy_game_actor *player=toy_game_local_player_actor(&test.game_state);
    if(!rasterfall_session_rts_order_actor(&test,0,player->actor_id,player->combat_generation,2000,0,0))return 17;
    player->nav_generation=test.game_state.navigation_generation;
    player->nav_active=1;player->nav_x=player->x+130;player->nav_z=player->z;
    camera.sy=1024;camera.cy=0;camera.pitch_cy=1024;
    session_build_rts_command(&test,&camera,&command,16);
    if(command.move_forward!=1 || player->x!=-2000 || !test.rts_move_active ||
        test.rts_move_x!=2000 || test.rts_move_z!=0)return 18;
    struct rasterfall_command manual={0};
    int max_step=toy_game_actor_move_step(player,RASTERFALL_MOVE_STEP);
    for(int tick=0;tick<12000/max_step+60 && test.rts_move_active;++tick) {
        int old_x=player->x,old_z=player->z,old_time=test.game_state.combat_time_ms;
        rasterfall_session_step(&test,&camera,&manual,16);
        long long move_x=(long long)player->x-old_x,move_z=(long long)player->z-old_z;
        if(move_x*move_x+move_z*move_z>2LL*max_step*max_step+4*max_step ||
            test.game_state.combat_time_ms!=old_time+16 ||
            toy_game_position_blocked_at_height(&test.game_state,player->x,player->z,TOY_GAME_PLAYER_RADIUS,0)) {
            __printf("RTS local step failed tick=%d delta=%d,%d limit=%d clock=%d/%d position=%d,%d\n",
                tick,player->x-old_x,player->z-old_z,max_step,test.game_state.combat_time_ms,
                old_time+16,player->x,player->z);return 19;
        }
    }
    if(test.rts_move_active || (long long)(player->x-2000)*(player->x-2000)+
        (long long)player->z*player->z>250LL*250LL) {
        __printf("RTS local arrival failed position=%d,%d active=%d nav=%d,%d velocity=%d,%d\n",
            player->x,player->z,test.rts_move_active,player->nav_x,player->nav_z,
            player->move_velocity_x,player->move_velocity_z);return 20;
    }
    if(!rasterfall_session_rts_order_actor(&test,0,player->actor_id,player->combat_generation,4000,0,0))return 21;
    session_build_rts_command(&test,&camera,&command,16);
    if(!rasterfall_session_rts_order_actor(&test,0,player->actor_id,player->combat_generation,0,0,1) ||
        test.rts_move_active || player->nav_active || player->nav_direct_valid)return 22;
    int stopped_x=player->x,stopped_z=player->z;
    rasterfall_session_step(&test,&camera,&manual,16);
    if(player->x!=stopped_x || player->z!=stopped_z)return 23;
    if(!rasterfall_session_rts_order_actor(&test,0,player->actor_id,player->combat_generation,-4000,0,0))return 24;
    session_build_rts_command(&test,&camera,&command,16);
    rasterfall_session_set_rts(&test,0);
    if(player->nav_active || player->nav_direct_valid || !test.rts_move_active)return 25;
    camera.sy=0;camera.cy=1024;
    manual.move_forward=1;manual.buttons=RASTERFALL_CMD_FIRE;manual.fire_held=1;
    unsigned int before_fire=player->fire_seq;
    rasterfall_session_step(&test,&camera,&manual,16);
    if(player->z<=stopped_z || player->z>=stopped_z+max_step || player->fire_seq!=before_fire+1 ||
        !test.rts_move_active || test.rts_move_x!=-4000 || test.rts_move_z!=0)return 26;
    rasterfall_session_set_rts(&test,1);
    session_build_rts_command(&test,&camera,&command,16);
    if(!test.rts_move_active || test.rts_move_x!=-4000 || test.rts_move_z!=0)return 27;
    player->control_disabled=1;
    session_build_rts_command(&test,&camera,&command,16);
    if(player->nav_active || player->nav_direct_valid || command.move_forward ||
        command.move_strafe || !test.rts_move_active || test.rts_move_x!=-4000)return 32;
    player->control_disabled=0;
    test.game_state.primitives=NULL;
    /* Empty magazines already reload through the formal Game weapon step,
     * including RTS with no visible target. Each timer advances once and
     * completion consumes exactly the available reserve, without loops. */
    for(int scenario=0;scenario<4;++scenario) {
        memset(&test,0,sizeof(test));memset(&camera,0,sizeof(camera));
        memset(&manual,0,sizeof(manual));toy_game_init(&test.game_state,42);
        test.game_state.external_director=1;
        toy_game_set_primitives(&test.game_state,route,1,8000);
        rasterfall_session_set_rts(&test,1);camera.cy=camera.pitch_cy=1024;
        player=toy_game_local_player_actor(&test.game_state);
        int weapon=scenario==1?TOY_GAME_WEAPON_PISTOL:TOY_GAME_WEAPON_AK;
        toy_game_actor_equip_weapon(&test.game_state,player,weapon);
        player->weapon_switch_timer_ms=0;
        struct toy_game_slot *slot=&player->slots[player->current_slot];
        slot->mag=0;slot->reserve=scenario==1?TOY_GAME_AMMO_INFINITE:scenario==2?0:17;
        if(scenario==3){player->reloading=1;player->reload_timer_ms=80;}
        int starts=0,done=0;
        for(int tick=0;tick<400;++tick) {
            int was_reloading=player->reloading,old_timer=player->reload_timer_ms;
            rasterfall_session_step(&test,&camera,&manual,16);
            if(was_reloading && old_timer>16 && player->reload_timer_ms!=old_timer-16)return 28;
            unsigned char events[TOY_GAME_MAX_EVENTS];
            int count=toy_game_drain_events(&test.game_state,events,sizeof(events));
            for(int e=0;e<count;++e) {
                starts+=events[e]==TOY_GAME_EV_RELOAD_START;
                done+=events[e]==TOY_GAME_EV_RELOAD_DONE;
            }
        }
        int expected_mag=scenario==1?toy_game_weapon_info(weapon)->mag_size:scenario==2?0:17;
        if(slot->mag!=expected_mag || slot->reserve!=(scenario==1?TOY_GAME_AMMO_INFINITE:0) ||
            player->reloading || starts!=(scenario<2?1:0) || done!=(scenario==2?0:1))return 29;
    }
    /* A partial magazine in FPS still waits for the player's manual request. */
    rasterfall_session_set_rts(&test,0);player->slots[player->current_slot].mag=1;
    player->slots[player->current_slot].reserve=7;
    for(int tick=0;tick<20;++tick)rasterfall_session_step(&test,&camera,&manual,16);
    if(player->reloading || player->slots[player->current_slot].mag!=1)return 30;
    manual.buttons=RASTERFALL_CMD_RELOAD;rasterfall_session_step(&test,&camera,&manual,16);
    manual.buttons=0;
    for(int tick=0;tick<400;++tick)rasterfall_session_step(&test,&camera,&manual,16);
    if(player->reloading || player->slots[player->current_slot].mag!=8 ||
        player->slots[player->current_slot].reserve!=0)return 31;
    /* An oblique camera must not turn a proved straight route into keyboard
     * diagonals that carry the player off a finite, one-metre platform. */
    memset(&test,0,sizeof(test));memset(&camera,0,sizeof(camera));
    memset(&manual,0,sizeof(manual));toy_game_init(&test.game_state,42);
    test.game_state.external_director=1;
    struct toy_map_primitive narrow={0};
    narrow.shape=TOY_MAP_PRIMITIVE_FLAT;
    narrow.flags=TOY_MAP_PRIMITIVE_WALKABLE;
    narrow.minx=-512;narrow.maxx=6512;narrow.minz=-256;narrow.maxz=256;
    toy_game_set_primitives(&test.game_state,&narrow,1,8000);
    rasterfall_session_set_rts(&test,1);
    if(!rasterfall_session_rts_teleport_player(&test,&camera,0,-900,60))return 33;
    camera.sy=307;camera.cy=977;camera.pitch_cy=1024;
    player=toy_game_local_player_actor(&test.game_state);
    if(!rasterfall_session_rts_order_actor(&test,0,player->actor_id,player->combat_generation,6000,60,0))return 34;
    for(int tick=0;tick<320 && test.rts_move_active;++tick) {
        rasterfall_session_step(&test,&camera,&manual,16);
        if(player->airborne_ms || abs(player->z)>76)return 35;
    }
    if(test.rts_move_active || abs(player->x-6000)>250)return 36;
    player->z=narrow.maxz+TOY_GAME_PLAYER_RADIUS-4;
    struct toy_game_actor grounded=*player;
    player->move_velocity_z=toy_game_player_move_step(&test.game_state,player)*1024;
    if(toy_game_move_player_input_supported(&test.game_state,0,0,1024) ||
        player->x!=grounded.x || player->z!=grounded.z || player->airborne_ms)return 38;
    player->move_velocity_z=toy_game_player_move_step(&test.game_state,player)*1024;
    toy_game_move_player_input(&test.game_state,0,0,1024,0);
    if(!player->airborne_ms)return 39;
    *player=grounded;
    int recovery_x=0,recovery_z=0;
    if(!toy_game_actor_navigation_target_height(&test.game_state,player,
        player->x-512,0,60,toy_game_player_move_step(&test.game_state,player),16,
        &recovery_x,&recovery_z) || player->airborne_ms || player->z!=grounded.z)return 40;
    struct toy_map_primitive separated[2]={narrow,narrow};
    separated[0].maxx=1512;separated[1].minx=2512;
    toy_game_set_primitives(&test.game_state,separated,2,8000);
    player->x=0;player->z=60;
    int route_x=0,route_z=60;
    if(toy_game_actor_navigation_target(&test.game_state,player,6000,60,
        toy_game_player_move_step(&test.game_state,player),16,&route_x,&route_z) ||
        player->nav_direct_valid)return 37;
    /* Authored-grid mode: negative origin, closed cell boundaries, overlapping
     * storeys, solid occupancy, and footprint retention after rebuilding. */
    struct toy_map_primitive grid_floor[3]={0};
    grid_floor[0].shape=TOY_MAP_PRIMITIVE_FLAT;
    grid_floor[0].flags=TOY_MAP_PRIMITIVE_WALKABLE;
    grid_floor[0].minx=-2048;grid_floor[0].maxx=4096;
    grid_floor[0].minz=-4096;grid_floor[0].maxz=1024;
    grid_floor[1]=grid_floor[0];grid_floor[1].surface_y0=4096;
    grid_floor[2].shape=TOY_MAP_PRIMITIVE_BOX;grid_floor[2].flags=TOY_MAP_PRIMITIVE_COLLISION;
    grid_floor[2].minx=512;grid_floor[2].maxx=1024;
    grid_floor[2].minz=-1024;grid_floor[2].maxz=-512;
    grid_floor[2].surface_y0=2048;
    toy_game_set_primitives(&test.game_state,grid_floor,3,100000);
    if(!toy_game_set_grid_enabled(&test.game_state,1) ||
       test.game_state.nav_cell_size!=512 || test.game_state.nav_origin!=-2048 ||
       test.game_state.nav_origin_z!=-4096)return 41;
    if(toy_game_grid_cell(&test.game_state,-2049,-4000)>=0 ||
       toy_game_grid_cell(&test.game_state,-2048,-4096)!=0)return 42;
    if(!toy_game_grid_walkable(&test.game_state,-256,0,-256) ||
       !toy_game_grid_walkable(&test.game_state,-256,4096,-256) ||
       toy_game_grid_walkable(&test.game_state,768,0,-768) ||
       toy_game_grid_walkable(&test.game_state,-256,2048,-256))return 43;
    if(!toy_game_grid_mark_building(&test.game_state,0,-1024,-2048,2048,0))return 44;
    toy_game_rebuild_navigation(&test.game_state);
    int occupied=toy_game_grid_cell(&test.game_state,-256,-256);
    if(test.game_state.grid_buildings[occupied]!=1 ||
       !toy_game_grid_walkable(&test.game_state,-256,0,-256))return 45;
    player->x=256;player->z=-768;player->ground_y=0;player->airborne_ms=0;
    if(toy_game_try_move_actor(&test.game_state,player,768,-768) || player->x!=256)return 46;
    uint64_t grid_actors,grid_enemies;
    int unit_cell=toy_game_grid_cell(&test.game_state,player->x,player->z);
    toy_game_grid_unit_occupants(&test.game_state,unit_cell,&grid_actors,&grid_enemies);
    if(!(grid_actors&1))return 48;
    player->x=-1792;player->z=-3840;
    toy_game_grid_unit_occupants(&test.game_state,unit_cell,&grid_actors,&grid_enemies);
    if(grid_actors&1)return 49;
    for(int node=1;node<=test.game_state.flow_count;++node)
        if((test.game_state.flow_nodes[node].x-256)%512 ||
           (test.game_state.flow_nodes[node].z-256)%512)return 47;
    player->x=256;player->z=-768;
    if(!toy_game_grid_walkable(&test.game_state,256,0,-768) ||
       !toy_game_grid_walkable(&test.game_state,768,0,-1280) ||
       toy_game_try_move_actor(&test.game_state,player,768,-1280))return 50;
    grid_floor[0].maxx=512;
    grid_floor[1]=grid_floor[0];grid_floor[1].minx=1536;grid_floor[1].maxx=4096;
    grid_floor[2].flags=0;
    toy_game_set_primitives(&test.game_state,grid_floor,3,100000);
    player->x=256;player->z=-768;
    if(toy_game_grid_walkable(&test.game_state,768,0,-768) ||
       toy_game_try_move_actor(&test.game_state,player,1792,-768))return 51;
    grid_floor[0].minx=-200000;grid_floor[0].maxx=200000;
    toy_game_set_primitives(&test.game_state,grid_floor,3,300000);
    if(test.game_state.grid_valid || test.game_state.nav_cell_size!=512 ||
       toy_game_try_move_actor(&test.game_state,player,300,-768))return 52;
    __printf("Game metre-grid bounds / storeys / building and live unit occupancy / corners / void / capacity passed\n");
    toy_game_set_grid_enabled(&test.game_state,0);
    test.game_state.primitives=NULL;
    __printf("RTS finite narrow platform / support gap / aim-independent route input passed\n");
    __printf("RTS local shared-planning/waypoint/final-goal/single-step/STOP/FPS-fire passed\n");
    __printf("RTS existing finite/infinite/dry/already-reloading/manual-FPS reload passed\n");
    return 0;
}

int rasterfall_session_recover_managed_actor(
    struct rasterfall_session *session, struct camera *camera)
{
    int x, z;
    if (!session || !camera || !session->managed_ai_enabled ||
        session->game_state.state != TOY_GAME_PLAYING)
        return 0;
    x = session->level.start_x;
    z = session->level.start_z;
    camera->x = x;
    camera->z = z;
    camera->sy = session->level.start_sy;
    camera->cy = session->level.start_cy;
    camera->pitch_sy = 0;
    camera->pitch_cy = 1024;
    camera->y = RASTERFALL_STANDING_CAMERA_Y;
    toy_game_local_player_actor(&session->game_state)->x = x;
    toy_game_local_player_actor(&session->game_state)->z = z;
    toy_game_local_player_actor(&session->game_state)->sy = camera->sy;
    toy_game_local_player_actor(&session->game_state)->cy = camera->cy;
    {
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        player->ground_y = toy_game_query_ground(
        &session->game_state, x, z, RASTERFALL_PLAYER_RADIUS, 0).support_y;
        player->airborne_ms = 0;
        player->airborne_y = 0;
        player->vertical_velocity = 0;
        player->air_x = 0;
        player->air_z = 0;
        player->air_skip_horizontal_step = 0;
        player->air_velocity_remainder_x = player->air_velocity_remainder_z = 0;
        player->move_velocity_x = player->move_velocity_z = 0;
        player->move_remainder_x = player->move_remainder_z = 0;
        player->jump_coyote_steps = player->jump_buffer_steps = 0;
        player->knockback_x = 0;
        player->knockback_z = 0;
    }
    toy_game_clear_actor_special_control(
        toy_game_local_player_actor(&session->game_state), 0);
    session->managed_ai_escape_phase = -1;
    session->managed_ai_route_phase = 5;
    session->managed_ai_target_index = -1;
    session->managed_ai_retarget_ms = 0;
    return 1;
}

static int session_managed_ai_face(struct camera *camera, int x, int z,
                                   int dt_ms)
{
    long long dx, dz;
    int turn, aligned;
    long long cross, dot, cross_abs;
    if (!camera) return 0;
    dx = (long long)x - camera->x;
    dz = (long long)z - camera->z;
    if (!dx && !dz) return 1;
    int target_sy=camera->sy, target_cy=camera->cy;
    struct rf_numeric_context numeric={0};
    numeric.kind=3;numeric.slot=numeric.id=numeric.generation=-1;
    numeric.x=camera->x;numeric.z=camera->z;
    rf_direction_check("session-face-input",camera->sy,camera->cy,&numeric);
    if(!rf_direction_valid(camera->sy,camera->cy)) {
        if(rf_numeric_strict())return 0;
        if(rf_direction_q10(camera->sy,camera->cy,&camera->sy,&camera->cy)!=1) {
            camera->sy=0;camera->cy=1024;
        }
    }
    if (rf_direction_set("session-face",dx,dz,&target_sy,&target_cy,&numeric)!=1) return 0;
    /* camera_rotate's turn argument is tan(angle) * 1024 for small angles.
     * Use a bounded per-tick delta so managed AI turns at 480 degrees/sec. */
    turn = MANAGED_AI_TURN_DEG_PER_SEC * dt_ms * 1024 / (1000 * 57);
    if (turn < 1) turn = 1;
    cross = (long long)camera->sy * dz - (long long)camera->cy * dx;
    dot = (long long)camera->sy * dx + (long long)camera->cy * dz;
    cross_abs = cross < 0 ? -cross : cross;
    aligned = dot > 0 && cross_abs <= dot * turn / 1024;
    if (aligned) {
        camera->sy = target_sy;
        camera->cy = target_cy;
        return 1;
    }
    /* Positive camera turn rotates toward +X.  The cross-product sign is
     * reversed for this coordinate convention. */
    rasterfall_camera_rotate(camera, cross < 0 ? turn : -turn, 0);
    return 0;
}

static int session_managed_ai_pistol_defense(
    struct rasterfall_session *session, struct camera *camera,
    struct rasterfall_command *command, int dt_ms)
{
    int enemy_index, dx, dz, distance;
    struct toy_game_enemy *enemy;
    const struct toy_game_actor *player;
    player = session ? toy_game_local_player_actor(&session->game_state) : NULL;
    if (!session || !camera || !command ||
        !player || player->current_slot != 1 ||
        player->special_control !=
            TOY_GAME_SPECIAL_CONTROL_SMOKER)
        return 0;
    enemy_index = player->special_source;
    if (enemy_index < 0 || enemy_index >= TOY_GAME_MAX_ENEMIES)
        return 0;
    enemy = &session->game_state.enemies[enemy_index];
    if (enemy->active != 1 || enemy->hp <= 0) return 0;
    dx = enemy->x - camera->x;
    dz = enemy->z - camera->z;
    distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (distance > toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->range)
        return 0;
    if (!session_managed_ai_face(camera, enemy->x, enemy->z, dt_ms))
        return 1;
    command->buttons |= RASTERFALL_CMD_FIRE;
    command->fire_held = 1;
    return 1;
}

static int session_managed_ai_weapon_master(
    struct rasterfall_session *session, struct camera *camera,
    struct rasterfall_command *command, int dt_ms)
{
    static const int weapons[] = {
        TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN,
        TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP
    };
    int target_weapon, pickup = -1, ammo = -1, i;
    int dx, dz, distance;
    const struct toy_game_weapon_info *info;
    struct toy_game_actor *player;

    int primary_empty = 0;
    player = session ? toy_game_local_player_actor(&session->game_state) : NULL;
    if (!session || !camera || !command || !player ||
        player->state == TOY_GAME_ACTOR_DOWNED ||
        session->game_state.state != TOY_GAME_PLAYING)
        return 0;
    if (player->slots[0].weapon >= TOY_GAME_WEAPON_SMG &&
        player->slots[0].weapon <= TOY_GAME_WEAPON_AWP) {
        const struct toy_game_weapon_info *primary = toy_game_weapon_info(
            player->slots[0].weapon);
        if (primary->reserve_max != TOY_GAME_AMMO_INFINITE)
            primary_empty = player->slots[0].reserve <= 0;
    }
    /* Horde completion starts a new rest period even though the wave number
     * has not advanced yet. */
    if (session->game_state.campaign_phase == TOY_GAME_PHASE_HORDE)
        session->managed_ai_ammo_rest_wave = -1;
    /* Empty reserve is an unconditional emergency, regardless of the route
     * state in which the last magazine ended. */
    if (primary_empty && session->game_state.campaign_phase == TOY_GAME_PHASE_HORDE &&
        session->managed_ai_weapon_master_route != 7 &&
        session->managed_ai_weapon_master_route != 8) {
        /* Pistol has infinite reserve ammo and remains usable while a
         * smoker disables ordinary movement.  The next normal weapon is
         * restored after the ammo-box trip. */
        if (player->current_slot != 1)
            toy_game_actor_switch_weapon(&session->game_state, player, 1);
        session->managed_ai_weapon_master_route = 7;
    }
    /* Every rest period starts with a mandatory ammo-box visit. */
    if (session->game_state.campaign_phase == TOY_GAME_PHASE_CALM &&
        session->managed_ai_ammo_rest_wave != session->game_state.wave &&
        (session->managed_ai_weapon_master_route <= 2 ||
         session->managed_ai_weapon_master_route == 6))
        session->managed_ai_weapon_master_route = 7;
    if (session->managed_ai_weapon_master_route == 7) {
        int route_x = 0, route_z = -3500;
        if (session_managed_ai_pistol_defense(session, camera, command,
                                              dt_ms))
            return 1;
        int route_distance = isqrt((long long)(route_x - camera->x) *
                                   (route_x - camera->x) +
                                   (long long)(route_z - camera->z) *
                                   (route_z - camera->z));
        if (!session_managed_ai_face(camera, route_x, route_z, dt_ms))
            return 1;
        if (route_distance > 350) {
            command->move_forward = 1;
            return 1;
        }
        session->managed_ai_weapon_master_route = 8;
    }
    if (session->managed_ai_weapon_master_route == 8) {
        int dx, dz, route_distance;
        int ammo_index = -1, i;
        if (session_managed_ai_pistol_defense(session, camera, command,
                                              dt_ms))
            return 1;
        for (i = 0; i < session->item_count; i++)
            if (session->items[i].kind == TOY_MAP_PICKUP_AMMO &&
                session->items[i].x > 0 && session->items[i].z > -2000)
                ammo_index = i;
        if (ammo_index < 0) return 0;
        dx = session->items[ammo_index].x - camera->x;
        dz = session->items[ammo_index].z - camera->z;
        route_distance = isqrt((long long)dx * dx + (long long)dz * dz);
        if (!session_managed_ai_face(camera, session->items[ammo_index].x,
                                     session->items[ammo_index].z, dt_ms))
            return 1;
        if (route_distance > 350) {
            command->move_forward = 1;
            return 1;
        }
        toy_game_actor_refill_ammo(&session->game_state, player);
        if (player->slots[0].weapon >= TOY_GAME_WEAPON_SMG &&
            player->slots[0].weapon <= TOY_GAME_WEAPON_AWP)
            toy_game_actor_switch_weapon(&session->game_state, player, 0);
        if (session->game_state.campaign_phase == TOY_GAME_PHASE_CALM)
            session->managed_ai_ammo_rest_wave = session->game_state.wave;
        session->managed_ai_weapon_master_route = 6;
        return 0;
    }
    if (session->game_state.campaign_phase != TOY_GAME_PHASE_CALM)
        return 0;
    /* The wave number advances when a wave starts.  Therefore the first
     * preparation is target 0, and the next calm phase advances to target 1. */
    if (session->managed_ai_weapon_master_target < session->game_state.wave) {
        session->managed_ai_weapon_master_target = session->game_state.wave;
        session->managed_ai_weapon_master_route = 0;
    }
    if (session->managed_ai_weapon_master_target > 4)
        return 0;
    target_weapon = session->managed_ai_weapon_master_target < 4 ?
        weapons[session->managed_ai_weapon_master_target] : TOY_GAME_WEAPON_AK;
    info = toy_game_weapon_info(target_weapon);

    for (i = 0; i < session->item_count; i++) {
        if (session->items[i].weapon == target_weapon &&
            (session->items[i].kind == TOY_MAP_PICKUP_WEAPON ||
             session->items[i].kind == TOY_MAP_PICKUP_SMG ||
             session->items[i].kind == TOY_MAP_PICKUP_SHOTGUN)) pickup = i;
        else if (session->items[i].kind == TOY_MAP_PICKUP_AMMO &&
                 /* This is the ammo box beside the central base. */
                 session->items[i].x > 0 && session->items[i].z > -2000)
            ammo = i;
    }
    if (pickup < 0 || ammo < 0 || !info) return 0;

    if (session->managed_ai_weapon_master_route <= 2) {
        int route_x = session->managed_ai_weapon_master_route == 0 ? 0 :
                      session->managed_ai_weapon_master_route == 1 ? 0 :
                      session->items[pickup].x;
        int route_z = session->managed_ai_weapon_master_route == 0 ? -3500 :
                      session->managed_ai_weapon_master_route == 1 ? -4500 :
                      session->items[pickup].z;
        dx = session->items[pickup].x - camera->x;
        dz = session->items[pickup].z - camera->z;
        distance = isqrt((long long)dx * dx + (long long)dz * dz);
        distance = isqrt((long long)(route_x - camera->x) *
                         (route_x - camera->x) +
                         (long long)(route_z - camera->z) *
                         (route_z - camera->z));
        if (distance <= 350) {
            if (session->managed_ai_weapon_master_route < 2) {
                session->managed_ai_weapon_master_route++;
            } else {
                toy_game_actor_equip_weapon(&session->game_state, player, target_weapon);
                session->managed_ai_weapon_master_route = 3;
            }
        }
        if (!session_managed_ai_face(camera, route_x, route_z, dt_ms))
            return 1;
        if (session->managed_ai_weapon_master_route <= 2) {
            command->move_forward = distance > 350 ? 1 : 0;
            return 1;
        }
    }
    if (session->managed_ai_weapon_master_route == 3) {
        int route_x = 0, route_z = -4500;
        distance = isqrt((long long)(route_x - camera->x) *
                         (route_x - camera->x) +
                         (long long)(route_z - camera->z) *
                         (route_z - camera->z));
        if (!session_managed_ai_face(camera, route_x, route_z, dt_ms))
            return 1;
        if (distance > 350) {
            command->move_forward = 1;
            return 1;
        }
        session->managed_ai_weapon_master_route = 4;
    }
    if (session->managed_ai_weapon_master_route == 4) {
        int route_x = 0, route_z = -3500;
        distance = isqrt((long long)(route_x - camera->x) *
                         (route_x - camera->x) +
                         (long long)(route_z - camera->z) *
                         (route_z - camera->z));
        if (!session_managed_ai_face(camera, route_x, route_z, dt_ms))
            return 1;
        if (distance > 350) {
            command->move_forward = 1;
            return 1;
        }
        session->managed_ai_weapon_master_route = 5;
    }
    if (session->managed_ai_weapon_master_route == 5) {
        dx = session->items[ammo].x - camera->x;
        dz = session->items[ammo].z - camera->z;
        distance = isqrt((long long)dx * dx + (long long)dz * dz);
        if (!session_managed_ai_face(camera, session->items[ammo].x,
                                     session->items[ammo].z, dt_ms))
            return 1;
        if (distance > 350) {
            command->move_forward = 1;
            return 1;
        }
        toy_game_actor_refill_ammo(&session->game_state, player);
        toy_game_actor_equip_weapon(&session->game_state, player, target_weapon);
        session->managed_ai_ammo_rest_wave = session->game_state.wave;
        session->managed_ai_weapon_master_route = 6;
    }
    return 0;
}

static void session_build_managed_ai_command(
    struct rasterfall_session *session, struct camera *camera,
    struct rasterfall_command *command, int dt_ms)
{
    struct toy_game_ai_observation observation;
    int target_x = camera->x, target_z = camera->z;
    int target_found = 0, target_is_wave_button = 0;
    int target_is_ai_revive = 0;
    int target_is_escape = 0;
    int target_stop_distance = 900;
    int target_enemy_index = -1;
    int weapon_range = TOY_GAME_MAX_RANGE;
    int attack_min_distance, attack_max_distance;
    int route_phase;
    const struct toy_game_box *safe_room = NULL;
    int dx, dz, distance, i;
    memset(command, 0, sizeof(*command));
    if (toy_game_local_player_actor_const(&session->game_state)->state ==
        TOY_GAME_ACTOR_DOWNED) {
        return;
    }
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (session->managed_ai_retarget_ms > 0)
        session->managed_ai_retarget_ms -= dt_ms;
    memset(&observation, 0, sizeof(observation));
    observation.nearest_enemy_index = -1;
    toy_game_ai_observe(&session->game_state, TOY_GAME_PLAYER_ACTOR_INDEX,
                        &observation);
    if (observation.current_weapon >= 0 &&
        observation.current_weapon < TOY_GAME_WEAPON_COUNT)
        weapon_range = toy_game_weapon_info(observation.current_weapon)->range;
    attack_min_distance = weapon_range * 60 / 100;
    attack_max_distance = weapon_range * 80 / 100;
    if (observation.current_weapon == TOY_GAME_WEAPON_SHOTGUN) {
        attack_min_distance = 2000;
        attack_max_distance = 3000;
    } else if (observation.current_weapon == TOY_GAME_WEAPON_SMG) {
        attack_min_distance = 2000;
        attack_max_distance = 4000;
    } else if (observation.current_weapon == TOY_GAME_WEAPON_AK) {
        attack_min_distance = 3000;
        attack_max_distance = 5000;
    } else if (observation.current_weapon == TOY_GAME_WEAPON_AWP) {
        /* Keep the sniper at long range instead of letting the generic
         * fallback steering pull it toward every visible target. */
        attack_min_distance = 6000;
        attack_max_distance = weapon_range;
    }
    if (session->managed_ai_retarget_ms <= 0 ||
        session->managed_ai_target_index < 0 ||
        session->managed_ai_target_index >= TOY_GAME_MAX_ENEMIES ||
        session->game_state.enemies[session->managed_ai_target_index].active != 1 ||
        session->game_state.enemies[session->managed_ai_target_index].hp <= 0) {
        session->managed_ai_target_index = observation.nearest_enemy_index;
        session->managed_ai_retarget_ms = 1000;
    }
    target_enemy_index = session->managed_ai_target_index;
    if (session->game_state.safe_room_count > 0)
        safe_room = &session->safe_rooms[0];

    if (session->managed_ai_escape_phase < 0 &&
        camera->z < -5700 &&
        !(safe_room && toy_game_point_in_box(camera->x, camera->z,
                                             safe_room)))
        session->managed_ai_escape_phase = 0;
    /* A safe-room visit during the deliberate wave-button route is expected.
     * Outside that route, treat it as an abnormal mobility state and recover
     * to the main base instead of allowing the player to remain trapped. */
    if (session->managed_ai_escape_phase < 0 && safe_room &&
        toy_game_point_in_box(camera->x, camera->z, safe_room) &&
        (session->managed_ai_route_phase < 0 ||
         session->managed_ai_route_phase >= 5))
        session->managed_ai_escape_phase = 4;

    if (session->managed_ai_escape_phase >= 0) {
        /* Fixed recovery path: developer side corridor -> air-gate opening
         * -> safe-room door -> main base.  It avoids the table/platform lanes
         * and never asks the generic combat steering to solve this escape. */
        target_is_escape = 1;
            if (toy_game_position_blocked_at_height(
                &session->game_state, camera->x, camera->z,
                RASTERFALL_PLAYER_RADIUS,
                toy_game_local_player_actor_const(&session->game_state)->ground_y +
                toy_game_local_player_actor_const(&session->game_state)->airborne_y)) {
            /* A Charger can leave us inside a prop.  This is the terminal's
             * same authoritative recovery operation: synchronize the local
             * actor position and clear its airborne impulse before the next
             * game tick. */
            if (rasterfall_session_recover_managed_actor(session, camera))
                return;
        }
        if (session->managed_ai_escape_phase == 0) {
            target_x = camera->x >= 0 ? 2000 : -2000;
            target_z = -6500;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350)
                session->managed_ai_escape_phase = 1;
        }
        if (session->managed_ai_escape_phase == 1) {
            target_x = 0;
            target_z = -6500;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350)
                session->managed_ai_escape_phase = 2;
        }
        if (session->managed_ai_escape_phase == 2) {
            target_x = 0;
            target_z = -3500;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350)
                session->managed_ai_escape_phase = 3;
        }
        if (session->managed_ai_escape_phase == 3) {
            target_x = 0;
            target_z = 0;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350) {
                session->managed_ai_escape_phase = -1;
                target_found = 0;
            }
        }
        if (session->managed_ai_escape_phase == 4) {
            /* Safe-room recovery first crosses its center, then uses the
             * same door/base segment as the normal exit route. */
            target_x = (safe_room->minx + safe_room->maxx) / 2;
            target_z = (safe_room->minz + safe_room->maxz) / 2;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350)
                session->managed_ai_escape_phase = 2;
        }
        if (session->managed_ai_escape_phase >= 0)
            target_found = 1;
    } else if (session->game_state.campaign_phase == TOY_GAME_PHASE_CALM &&
        safe_room) {
        /* Rest-phase priority: rescue every downed teammate before entering
         * the safe-room route and pressing the next-wave button. */
        for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
            const struct toy_game_actor *actor =
                &session->game_state.actors[i];
            if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
                actor->base_core || actor->faction != TOY_GAME_FACTION_ALLIED ||
                actor->state != TOY_GAME_ACTOR_DOWNED)
                continue;
            target_x = actor->x;
            target_z = actor->z;
            target_found = 1;
            target_is_ai_revive = 1;
            break;
        }
        if (target_is_ai_revive) {
            /* The normal interaction executor owns revive progress. */
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= RASTERFALL_INTERACT_RANGE)
                command->buttons |= RASTERFALL_CMD_INTERACT;
        }
        if (target_is_ai_revive)
            goto managed_ai_target_ready;
        if (session->game_state.spawn_timer_ms <= 0)
            return;
        if (session->managed_ai_route_phase >= 3)
            session->managed_ai_route_phase = 0;
        route_phase = session->managed_ai_route_phase;
        if (route_phase == 0) {
            target_x = (safe_room->minx + safe_room->maxx) / 2;
            target_z = safe_room->maxz + 500;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350)
                session->managed_ai_route_phase = route_phase = 1;
        }
        if (route_phase == 1) {
            target_x = (safe_room->minx + safe_room->maxx) / 2;
            target_z = (safe_room->minz + safe_room->maxz) / 2;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350)
                session->managed_ai_route_phase = route_phase = 2;
        }
        if (route_phase == 2) {
            for (i = 0; i < session->item_count; i++) {
                if (session->items[i].kind != TOY_MAP_PICKUP_WAVE_SKIP_BUTTON)
                    continue;
                target_x = session->items[i].x;
                target_z = session->items[i].z;
                target_found = 1;
                target_is_wave_button = 1;
                break;
            }
        } else {
            target_found = 1;
        }
    } else {
        if (session->managed_ai_route_phase >= 0 &&
            session->managed_ai_route_phase < 3)
            session->managed_ai_route_phase = 3;
        if (session->managed_ai_route_phase == 3 && safe_room) {
            /* Leave through the room center, the exact reverse of entry. */
            target_x = (safe_room->minx + safe_room->maxx) / 2;
            target_z = (safe_room->minz + safe_room->maxz) / 2;
            target_found = 1;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350) {
                session->managed_ai_route_phase = 4;
                target_found = 0;
            }
        }
        if (session->managed_ai_route_phase == 4 && safe_room) {
            target_x = (safe_room->minx + safe_room->maxx) / 2;
            target_z = safe_room->maxz + 500;
            target_found = 1;
            if (isqrt((long long)(target_x - camera->x) *
                      (target_x - camera->x) +
                      (long long)(target_z - camera->z) *
                      (target_z - camera->z)) <= 350) {
                session->managed_ai_route_phase = 5;
                target_found = 0;
            }
        }
    }
    if (!target_found && target_enemy_index >= 0) {
        target_x = session->game_state.enemies[target_enemy_index].x;
        target_z = session->game_state.enemies[target_enemy_index].z;
        target_found = 1;
    } else if (!target_found && session->game_state.base_actor_index >= 0 &&
               session->game_state.base_actor_index < TOY_GAME_MAX_ACTORS &&
               session->game_state.actors[
                   session->game_state.base_actor_index].active) {
        target_x = session->game_state.actors[
            session->game_state.base_actor_index].x;
        target_z = session->game_state.actors[
            session->game_state.base_actor_index].z;
        target_found = 1;
    }
    if (!target_found) return;
managed_ai_target_ready:
    if (!target_found) return;
    if (session->managed_ai_route_phase >= 0 &&
        session->managed_ai_route_phase < 5)
        target_stop_distance = 200;
    if (target_is_escape)
        target_stop_distance = 200;
    if (!session_managed_ai_face(camera, target_x, target_z, dt_ms)) {
        command->move_forward = 0;
        return;
    }
    dx = target_x - camera->x;
    dz = target_z - camera->z;
    distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (target_is_escape)
        command->move_forward = distance > target_stop_distance ? 1 : 0;
    else if (target_is_wave_button && distance <= RASTERFALL_INTERACT_RANGE)
        command->buttons |= RASTERFALL_CMD_INTERACT;
    else if (target_is_ai_revive && distance <= RASTERFALL_INTERACT_RANGE)
        command->buttons |= RASTERFALL_CMD_INTERACT;
    else if (!target_is_wave_button && !target_is_ai_revive &&
             target_enemy_index >= 0 &&
             distance > attack_max_distance)
        command->move_forward = 1;
    else if (!target_is_wave_button && !target_is_ai_revive &&
             target_enemy_index >= 0 &&
             distance < attack_min_distance)
        command->move_forward = -1;
    else if ((target_enemy_index < 0 ||
              distance > attack_max_distance ||
              distance < attack_min_distance) &&
             distance > (target_is_wave_button || target_is_ai_revive ?
                         RASTERFALL_INTERACT_RANGE / 2 :
                         target_stop_distance))
        command->move_forward = 1;
    /* Managed-player combat intentionally fires at full rate.  The distance
     * band is the limiter; there is no artificial trigger cooldown here. */
    {
        int enemy_distance = -1;
        if (target_enemy_index >= 0 &&
            session->game_state.enemies[target_enemy_index].active == 1) {
            int enemy_dx = session->game_state.enemies[target_enemy_index].x -
                           camera->x;
            int enemy_dz = session->game_state.enemies[target_enemy_index].z -
                           camera->z;
            enemy_distance = isqrt((long long)enemy_dx * enemy_dx +
                                   (long long)enemy_dz * enemy_dz);
        }
        /* During recovery, any target within the weapon range may be fired at
         * while the fixed route owns movement.  In normal combat, entering
         * range is enough to fire; 60%--80% only controls distance. */
        if (!target_is_wave_button && !target_is_ai_revive &&
            target_enemy_index >= 0 && enemy_distance >= 0 &&
            enemy_distance <= weapon_range &&
            (target_is_escape ||
             session->game_state.campaign_phase == TOY_GAME_PHASE_HORDE)) {
            /* Keep the primary weapon usable for the whole wave.  The
             * refill box replenishes reserve ammo during preparation; this
             * command performs the ordinary magazine reload while fighting. */
            if (observation.current_weapon >= TOY_GAME_WEAPON_SMG &&
                observation.current_weapon <= TOY_GAME_WEAPON_AWP &&
                observation.ammo_percent == 0) {
                command->buttons |= RASTERFALL_CMD_RELOAD;
            } else {
                command->buttons |= RASTERFALL_CMD_FIRE;
                command->fire_held = 1;
            }
        }
    }
}

void rasterfall_session_step(struct rasterfall_session *session,
                             struct camera *camera,
                             const struct rasterfall_command *command,
                             int dt_ms)
{
    struct rasterfall_command managed_command;
    int old_reloading;
    unsigned int old_fire_seq;
    if (command->buttons & RASTERFALL_CMD_RESET) {
        rasterfall_session_reset(session, camera, session->seed);
        return;
    }
    rasterfall_animation_player_update(&session->skeletal_demo_player, dt_ms);
    if (session->humanoid_debug_action == RASTERFALL_HUMANOID_DEBUG_IDLE)
        session->humanoid_debug_time_ms =
            (session->humanoid_debug_time_ms + dt_ms) % 2400;
    else if (session->humanoid_debug_action == RASTERFALL_HUMANOID_DEBUG_WALK)
        session->humanoid_debug_time_ms =
            (session->humanoid_debug_time_ms + dt_ms) % 800;
    else if (session->humanoid_debug_action == RASTERFALL_HUMANOID_DEBUG_AIM)
        session->humanoid_debug_time_ms =
            (session->humanoid_debug_time_ms + dt_ms) % 1000;
    else if (session->humanoid_debug_time_ms < 180)
        session->humanoid_debug_time_ms += dt_ms;
    else
        session->humanoid_debug_time_ms = 180;
    if (session->pose_debug_active && session->pose_editor.active) {
        /* Capture the advancing clock before applying this frame's editor
         * command.  A paused TIME edit below can then seek the player without
         * being overwritten on the same frame. */
        if (session->pose_editor.animation_base != 0 &&
            session->pose_editor.animation_playing)
            session->pose_editor.animation_time_ms =
                session->skeletal_demo_player.time_ms;
    }
    if (session->pose_debug_active && command->pose_editor_action) {
        int editor_action = command->pose_editor_action;
        int editor_result = rasterfall_calibration_editor_step(&session->pose_editor, editor_action);
        session->pose_debug_active = session->pose_editor.active;
        if (editor_action == RASTERFALL_POSE_EDITOR_EXPORT) {
            session->banner_text = editor_result ? "POSE EXPORTED" : "POSE EXPORT FAILED";
            session->banner_ms = 1800;
        } else if (editor_result && session->pose_editor.active &&
                   session->pose_editor.dirty) {
            /* Pose edits are sparse key events, so saving each accepted edit
             * is cheap and prevents a renderer/window failure from discarding
             * an authoring session. */
            rasterfall_calibration_export(&session->pose_editor);
        }
    }
    if (session->pose_debug_active && session->pose_editor.active) {
        if (session->pose_editor.animation_base == 0) {
            session->skeletal_demo_player.clip = NULL;
            session->skeletal_demo_player.clip_id = -1;
            session->skeletal_demo_player.playing = 0;
        } else {
            session->skeletal_demo_player.clip_id = 11;
            session->skeletal_demo_player.time_ms =
                session->pose_editor.animation_time_ms;
            session->skeletal_demo_player.playing =
                session->pose_editor.animation_playing;
        }
        /* The editor stores the same five channels consumed by composition. */
        memcpy(session->rifle_pose.rotation, session->pose_editor.pose.body_pose,
               sizeof(session->rifle_pose.rotation));
    }
    if (session->pose_debug_active && command->pose_debug_action) {
        int action=command->pose_debug_action;
        struct rasterfall_rifle_pose *edited=&session->rifle_pose;
        if(action==RASTERFALL_POSE_DEBUG_PREV_BONE)session->pose_debug_bone=(session->pose_debug_bone+RASTERFALL_RIFLE_POSE_BONE_COUNT-1)%RASTERFALL_RIFLE_POSE_BONE_COUNT;
        else if(action==RASTERFALL_POSE_DEBUG_NEXT_BONE)session->pose_debug_bone=(session->pose_debug_bone+1)%RASTERFALL_RIFLE_POSE_BONE_COUNT;
        else if(action>=RASTERFALL_POSE_DEBUG_AXIS_X&&action<=RASTERFALL_POSE_DEBUG_AXIS_Z)session->pose_debug_axis=action-RASTERFALL_POSE_DEBUG_AXIS_X;
        else if(action==RASTERFALL_POSE_DEBUG_DECREASE)edited->rotation[session->pose_debug_bone][session->pose_debug_axis]--;
        else if(action==RASTERFALL_POSE_DEBUG_INCREASE)edited->rotation[session->pose_debug_bone][session->pose_debug_axis]++;
        else if(action==RASTERFALL_POSE_DEBUG_EXPORT){char out[1400];int fd,n=0,i;n+=snprintf(out+n,sizeof(out)-n,"# Eula AK humanoid poses\n");for(i=0;i<RASTERFALL_RIFLE_POSE_BONE_COUNT;i++)n+=snprintf(out+n,sizeof(out)-n,"rifle %s %d %d %d\n",rasterfall_rifle_pose_bone_names[i],session->rifle_pose.rotation[i][0],session->rifle_pose.rotation[i][1],session->rifle_pose.rotation[i][2]);for(i=0;i<RASTERFALL_RIFLE_POSE_BONE_COUNT;i++)n+=snprintf(out+n,sizeof(out)-n,"hit %s %d %d %d\n",rasterfall_rifle_pose_bone_names[i],session->hit_pose.rotation[i][0],session->hit_pose.rotation[i][1],session->hit_pose.rotation[i][2]);fd=__openat(AT_FDCWD,"tmp/eula_ak_rifle_pose.txt",O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0){__write(fd,out,n);__close(fd);session->banner_text="POSES EXPORTED: tmp/eula_ak_rifle_pose.txt";}else session->banner_text="POSE EXPORT FAILED";session->banner_ms=2400;}
    }
    if (session->rts_active) {
        session_build_rts_command(session, camera, &managed_command, dt_ms);
        command = &managed_command;
    } else if (session_managed_ai_active(session)) {
        memset(&managed_command, 0, sizeof(managed_command));
        /* Weapon-master preparation owns the command while it is travelling
         * through the weapon pickup route.  The generic calm route must not retarget
         * or rotate the player in the middle of that fixed path. */
        if (!session_managed_ai_weapon_master(session, camera,
                                              &managed_command, dt_ms))
            session_build_managed_ai_command(session, camera, &managed_command,
                                             dt_ms);
        command = &managed_command;
    }
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (command->buttons & RASTERFALL_CMD_FLAG)
        session_toggle_flag(session, camera);
    if (toy_game_local_player_actor_const(&session->game_state)->state ==
        TOY_GAME_ACTOR_ALIVE)
        session_move_player(session, camera, command);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    session_update_smooth_turn(session, camera);
    session_update_carried_flag(session, camera);
    {
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        player->sy = camera->sy; player->cy = camera->cy;
        player->pitch_sy = camera->pitch_sy;
        player->pitch_cy = camera->pitch_cy;
        player->moving = player->move_velocity_x || player->move_velocity_z ||
                         player->air_x || player->air_z;
        toy_game_update_actor_ground(&session->game_state,
                                     TOY_GAME_PLAYER_ACTOR_INDEX);
    }
    /* Ground/platform resolution must precede weapon simulation: the visual
     * muzzle is derived from camera->y during this same tick. */
    session_sync_special_motion(session, camera);
    session->highlight_index = rasterfall_session_compute_highlight(session, camera);
    if (command->buttons & RASTERFALL_CMD_INTERACT)
        session_client_interact_banner(session);
    if (command->buttons & RASTERFALL_CMD_SHOVE)
        toy_game_actor_shove(&session->game_state,
            toy_game_local_player_actor(&session->game_state),
            camera->sy, camera->cy);
    if ((command->buttons & RASTERFALL_CMD_INTERACT) &&
        session_near_ai(session, camera, &session->ai_revive_actor_index))
        session->ai_revive_active = 1;
    if ((command->buttons & RASTERFALL_CMD_INTERACT) &&
        session->highlight_index >= 0 &&
        toy_game_local_player_actor_const(&session->game_state)->state !=
            TOY_GAME_ACTOR_DOWNED)
        session_interact(session, &session->items[session->highlight_index]);
    if (session->ai_revive_active) {
        if (!session_near_ai(session, camera, NULL) ||
            toy_game_local_player_actor_const(&session->game_state)->state ==
                TOY_GAME_ACTOR_DOWNED) {
            session->ai_revive_active = 0;
        } else if (toy_game_revive_actor(&session->game_state,
                                         session->ai_revive_actor_index,
                                         dt_ms)) {
            const struct toy_game_actor *revived =
                &session->game_state.actors[session->ai_revive_actor_index];
            session->ai_revive_active = 0;
            session->banner_ms = 1800;
            session->banner_text = revived->name;
        }
    }
    /* World simulation is separate from the local actor's weapon step. */
    rasterfall_grid_tick(session,dt_ms);
    toy_game_update_world(&session->game_state, dt_ms);
    rasterfall_session_weaver_sync(session);
    {
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        struct toy_game_actor_command actor_command;
        int fired = 0;
        old_reloading = player->reloading;
        old_fire_seq = player->fire_seq;
        memset(&actor_command, 0, sizeof(actor_command));
        actor_command.switch_slot = -1;
        actor_command.aim_active = 1;
        actor_command.aim_sy = player->sy;
        actor_command.aim_cy = player->cy;
        actor_command.reload = (command->buttons & RASTERFALL_CMD_RELOAD) != 0;
        actor_command.fire_pressed =
            (command->buttons & RASTERFALL_CMD_FIRE) != 0;
        actor_command.fire_held = command->fire_held;
        if (command->buttons & RASTERFALL_CMD_SLOT_1) actor_command.switch_slot = 0;
        else if (command->buttons & RASTERFALL_CMD_SLOT_2) actor_command.switch_slot = 1;
        else if (command->buttons & RASTERFALL_CMD_SLOT_3) actor_command.switch_slot = 2;
        else if (command->buttons & RASTERFALL_CMD_SLOT_4) actor_command.switch_slot = 3;
        toy_game_execute_actor_command(&session->game_state, player,
                                       &actor_command, dt_ms, 100);
        if ((command->buttons & RASTERFALL_CMD_FIRE) &&
            toy_game_actor_use_special(&session->game_state, player,
                                       player->sy, player->cy)) fired = 1;
        if ((command->buttons & RASTERFALL_CMD_FIRE) &&
            toy_game_actor_throwable(&session->game_state, player,
                                     player->sy, player->cy,
                                     camera->pitch_sy, camera->pitch_cy,
                                     camera->y)) fired = 1;
        (void)fired;
    }
    {
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        /* This used to be handled by toy_game_update_held().  The actor
         * migration calls the lower-level weapon step directly, so keep the
         * local action selection and clock here as well. */
        if (player->reloading && !old_reloading)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_RELOAD);
        else if (player->fire_seq != old_fire_seq)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_FIRE);
        toy_game_actor_update_animation(player, dt_ms);
        if (player->animation.id == TOY_GAME_ANIM_RELOAD &&
            !player->reloading)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_NONE);
        else if (player->animation.id == TOY_GAME_ANIM_FIRE &&
                 player->animation.time_ms >=
                 toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_NONE);
        else if (player->animation.id == TOY_GAME_ANIM_SHOVE &&
                 player->animation.time_ms >=
                 toy_game_animation_info(TOY_GAME_ANIM_SHOVE)->duration_ms)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_NONE);
        else if (player->animation.id == TOY_GAME_ANIM_MELEE &&
                 player->animation.time_ms >=
                 toy_game_animation_info(TOY_GAME_ANIM_MELEE)->duration_ms)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_NONE);
        else if (player->animation.id == TOY_GAME_ANIM_THROW &&
                 player->animation.time_ms >=
                 toy_game_animation_info(TOY_GAME_ANIM_THROW)->duration_ms)
            toy_game_actor_set_animation(player, TOY_GAME_ANIM_NONE);
    }
    {
        int i;
        for (i = 0; i < session->game_state.event_count; i++)
            if (session->game_state.events[i] == TOY_GAME_EV_WAVE_START) {
                session->banner_ms = session->game_state.gameplay_config.wave_announce_ms;
                session->banner_success = 1;
                session->banner_text = "WAVE STARTING";
                break;
            }
    }
    {
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        if (!player->reloading &&
            toy_game_animation_allows_locomotion(player->animation.id))
            toy_game_actor_set_animation(player,
                command->move_forward || command->move_strafe ?
                TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_NONE);
    }
    session_sync_special_motion(session, camera);
    session_update_manual_alarm(session, dt_ms);
    session_frontier_step(session,dt_ms);
    if (session->banner_ms > 0) {
        session->banner_ms -= dt_ms;
        if (session->banner_ms <= 0) {
    session->banner_ms = 0;
    session->banner_text = NULL;
    session->banner_success = 1;
        }
    }
}

int rasterfall_session_dev_killall(struct rasterfall_session *session)
{
    int i, killed = 0;
    if (!session) return 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *enemy = &session->game_state.enemies[i];
        if (enemy->active != 1) continue;
        enemy->hp = 0;
        enemy->active = 2;
        enemy->dying_ms = TOY_GAME_DYING_MS;
        enemy->flash = 120;
        killed++;
    }
    session->game_state.enemies_alive -= killed;
    if (session->game_state.enemies_alive < 0)
        session->game_state.enemies_alive = 0;
    return killed;
}

static void session_step_client_mode(struct rasterfall_session *session,
                                     struct camera *camera,
                                     const struct rasterfall_command *command,
                                     int dt_ms, int suppress_presentation)
{
    int i;
    int saved_throw_timer;
    int old_reloading;
    unsigned int old_fire_seq;
    struct toy_game_actor *local_player =
        toy_game_local_player_actor(&session->game_state);
    struct toy_game_animation_state saved_animation =
        local_player->animation;
    struct toy_game_ray saved_rays[TOY_GAME_MAX_RAYS];
    int saved_events = session->game_state.event_count;
    int saved_muzzle = local_player->muzzle_flash_ms;
    int saved_ray_count = local_player->ray_count;
    session->game_state.defer_actor_damage = 1;
    saved_throw_timer = local_player->throw_timer_ms;
    unsigned int saved_fire_seq = local_player->fire_seq;
    memcpy(saved_rays, local_player->rays, sizeof(saved_rays));
    if (command->buttons & RASTERFALL_CMD_RESET) {
        rasterfall_session_reset(session, camera, session->seed);
        return;
    }
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    session_move_player(session, camera, command);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    session_update_carried_flag(session, camera);
    {
        struct toy_game_actor *player =
            toy_game_local_player_actor(&session->game_state);
        player->sy = camera->sy; player->cy = camera->cy;
        player->pitch_sy = camera->pitch_sy;
        player->pitch_cy = camera->pitch_cy;
        player->moving = player->move_velocity_x || player->move_velocity_z ||
                         player->air_x || player->air_z;
        toy_game_update_actor_ground(&session->game_state,
                                     TOY_GAME_PLAYER_ACTOR_INDEX);
    }
    session_sync_special_motion(session, camera);
    session->highlight_index = rasterfall_session_compute_highlight(session, camera);
    if (command->buttons & RASTERFALL_CMD_INTERACT)
        session_client_interact_banner(session);
    /* Predict only the local first-person presentation.  The host remains
     * authoritative for the shove's enemy displacement and stun state. */
    if (command->buttons & RASTERFALL_CMD_SHOVE)
        toy_game_actor_set_animation(local_player,
                               TOY_GAME_ANIM_SHOVE);
    /* AI rescue remains host-authoritative, but keep the local action state so
     * the client can render the same progress bar while the host advances it.
     * The actor's authoritative progress arrives in the next snapshot. */
    if ((command->buttons & RASTERFALL_CMD_INTERACT) &&
        session_near_ai(session, camera, &session->ai_revive_actor_index))
        session->ai_revive_active = 1;
    if (session->ai_revive_active) {
        int index = session->ai_revive_actor_index;
        if (index < 0 || index >= TOY_GAME_MAX_ACTORS ||
            !session_near_ai(session, camera, NULL) ||
            toy_game_local_player_actor_const(&session->game_state)->state ==
                TOY_GAME_ACTOR_DOWNED ||
            session->game_state.actors[index].state != TOY_GAME_ACTOR_DOWNED) {
            session->ai_revive_active = 0;
        }
    }
    /* 交互由主机权威执行。客户端只发送 INTERACT 命令，等待主机快照
     * 回传拾取、救援、弹药、空气墙和刷怪结果，避免两端世界分叉。 */
    old_reloading = local_player->reloading;
    old_fire_seq = local_player->fire_seq;
    {
        struct toy_game_actor_command actor_command;
        int weapon;
        memset(&actor_command, 0, sizeof(actor_command));
        actor_command.switch_slot = -1;
        actor_command.aim_active = 1;
        actor_command.aim_sy = camera->sy;
        actor_command.aim_cy = camera->cy;
        actor_command.reload = (command->buttons & RASTERFALL_CMD_RELOAD) != 0;
        actor_command.fire_pressed =
            (command->buttons & RASTERFALL_CMD_FIRE) != 0;
        actor_command.fire_held = command->fire_held;
        if (command->buttons & RASTERFALL_CMD_SLOT_1) actor_command.switch_slot = 0;
        else if (command->buttons & RASTERFALL_CMD_SLOT_2) actor_command.switch_slot = 1;
        else if (command->buttons & RASTERFALL_CMD_SLOT_3) actor_command.switch_slot = 2;
        else if (command->buttons & RASTERFALL_CMD_SLOT_4) actor_command.switch_slot = 3;
        toy_game_execute_actor_command(&session->game_state, local_player,
                                       &actor_command, dt_ms, 100);
        weapon = toy_game_actor_current_weapon(local_player);
        /* Melee damage and projectile creation are host-authoritative, but
         * their first-person windup must start on the owning client.  The
         * client intentionally does not call toy_game_fire for these weapons
         * because that would also mutate the shared world/inventory. */
        if ((command->buttons & RASTERFALL_CMD_FIRE) &&
            !local_player->reloading &&
            local_player->weapon_switch_timer_ms <= 0) {
            if (weapon == TOY_GAME_WEAPON_AXE &&
                local_player->melee_timer_ms <= 0) {
                local_player->melee_timer_ms =
                    TOY_CONFIG_MELEE_SWING_MS;
                toy_game_actor_set_animation(local_player,
                                       TOY_GAME_ANIM_MELEE);
            } else if ((weapon == TOY_GAME_WEAPON_BOMB ||
                        weapon == TOY_GAME_WEAPON_MOLOTOV) &&
                       local_player->slots[local_player->current_slot].mag > 0 &&
                       local_player->throw_timer_ms <= 0) {
                local_player->throw_timer_ms =
                    TOY_CONFIG_THROW_COOLDOWN_MS;
                toy_game_actor_set_animation(local_player,
                                       TOY_GAME_ANIM_THROW);
            }
        }
    }
    /* The client predicts its own weapon state, so it must also predict the
     * presentation transition.  The host path uses toy_game_update_held,
     * which owns this transition; the client deliberately calls the lower
     * level weapon update to avoid simulating the shared world. */
    if (local_player->reloading && !old_reloading)
        toy_game_actor_set_animation(local_player,
                               TOY_GAME_ANIM_RELOAD);
    else if (local_player->fire_seq != old_fire_seq)
        toy_game_actor_set_animation(local_player,
                               TOY_GAME_ANIM_FIRE);
    if (local_player->fire_seq != old_fire_seq) {
        for (i = 0; i < local_player->ray_count; i++) {
            int enemy_index = local_player->rays[i].enemy_index;
            if (enemy_index >= 0 && enemy_index < TOY_GAME_MAX_ENEMIES &&
                session->game_state.enemies[enemy_index].active == 1)
                session->game_state.enemies[enemy_index].hurt = 80;
        }
    }
    if (local_player->animation.id == TOY_GAME_ANIM_RELOAD) {
        toy_game_animation_update(&local_player->animation, dt_ms);
        if (!local_player->reloading)
            toy_game_actor_set_animation(local_player,
                                   TOY_GAME_ANIM_NONE);
    } else if (local_player->animation.id == TOY_GAME_ANIM_FIRE) {
        toy_game_animation_update(&local_player->animation, dt_ms);
        if (local_player->animation.time_ms >=
            toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms)
            toy_game_actor_set_animation(local_player,
                                   TOY_GAME_ANIM_NONE);
    }
    /* There is no local gameplay update on the client for the shove, so
     * advance its presentation clock here until the authoritative snapshot
     * replaces it. */
    if (local_player->animation.id == TOY_GAME_ANIM_SHOVE) {
        toy_game_animation_update(&local_player->animation, dt_ms);
        if (local_player->animation.time_ms >=
            toy_game_animation_info(TOY_GAME_ANIM_SHOVE)->duration_ms)
            toy_game_actor_set_animation(local_player,
                                   TOY_GAME_ANIM_NONE);
    }
    if (local_player->animation.id == TOY_GAME_ANIM_MELEE ||
        local_player->animation.id == TOY_GAME_ANIM_THROW) {
        toy_game_animation_update(&local_player->animation, dt_ms);
        if (local_player->animation.time_ms >=
            toy_game_animation_info(local_player->animation.id)->duration_ms)
            toy_game_actor_set_animation(local_player,
                                   TOY_GAME_ANIM_NONE);
    }
    if (!local_player->reloading &&
        toy_game_animation_allows_locomotion(
            local_player->animation.id))
        toy_game_actor_set_animation(local_player,
                               command->move_forward || command->move_strafe ?
                               TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_NONE);
    toy_game_update_actor_motion(&session->game_state,
                                 TOY_GAME_PLAYER_ACTOR_INDEX, dt_ms);
    /* Motion changes airborne_y (and may move the player horizontally).  Keep
     * the predicted first-person camera in the same post-tick state as the
     * host camera instead of waiting for the next snapshot to move camera.y. */
    session_sync_special_motion(session, camera);
    /* Clients do not run the authoritative enemy simulation.  Still advance
     * the short death presentation locally so a lost final entity snapshot
     * cannot leave a flattened corpse rendered forever. */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *enemy = &session->game_state.enemies[i];
        if (enemy->active == 1) {
            if (enemy->hurt > 0) {
                enemy->hurt -= dt_ms;
                if (enemy->hurt < 0) enemy->hurt = 0;
            }
            if (enemy->flash > 0) {
                enemy->flash -= dt_ms;
                if (enemy->flash < 0) enemy->flash = 0;
            }
        } else if (enemy->active == 2) {
            enemy->dying_ms -= dt_ms;
            if (enemy->dying_ms <= 0) enemy->active = 0;
        }
    }
    local_player->throw_timer_ms = saved_throw_timer;
    if (session->banner_ms > 0) {
        session->banner_ms -= dt_ms;
        if (session->banner_ms <= 0) {
            session->banner_ms = 0;
            session->banner_text = NULL;
        }
    }
    /* AI simulation only runs on the host.  Clients still own the visual
     * idle/walk cross-fade for snapshot actors, so advance that small clock
     * without touching their authoritative animation id/time. */
    if (!suppress_presentation)
        for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
            struct toy_game_actor *actor = &session->game_state.actors[i];
            if (!actor->active || actor->locomotion_blend_ms >= 200) continue;
            actor->locomotion_blend_ms += dt_ms;
            if (actor->locomotion_blend_ms > 200)
                actor->locomotion_blend_ms = 200;
        }
    if (suppress_presentation) {
        local_player->animation = saved_animation;
        session->game_state.event_count = saved_events;
        local_player->muzzle_flash_ms = saved_muzzle;
        local_player->fire_seq = saved_fire_seq;
        local_player->ray_count = saved_ray_count;
        memcpy(local_player->rays, saved_rays, sizeof(saved_rays));
    }
}

void rasterfall_session_step_client(struct rasterfall_session *session,
                                    struct camera *camera,
                                    const struct rasterfall_command *command,
                                    int dt_ms)
{
    session_step_client_mode(session, camera, command, dt_ms, 0);
}

void rasterfall_session_replay_client(struct rasterfall_session *session,
                                      struct camera *camera,
                                      const struct rasterfall_command *command,
                                      int dt_ms)
{
    session_step_client_mode(session, camera, command, dt_ms, 1);
}
