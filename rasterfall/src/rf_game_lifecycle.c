#include "tlibc_everything.h"
#include "rf_game_lifecycle.h"
#include "rasterfall_render_resources.h"
#include "rasterfall_feature_freeze.h"
#include "rf_ui_font.h"

static const char *world_path(enum rasterfall_world_id world)
{
    return rasterfall_world_map_path(world);
}

void rf_game_seed_world_groups(struct rf_game_runtime *runtime)
{
    struct rasterfall_session *s=runtime->session;
    if(s->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01)return;
    rf_rts_sync(&runtime->rts,&s->game_state,s->scene_local.world_generation);
    for(int group=0;group<3;++group) {
        rf_rts_clear(&runtime->rts);
        for(int n=0;n<5;++n) {
            if((group==0 && n>=2) || (group==1 && n<2))continue;
            rf_rts_select(&runtime->rts,&s->game_state,s->frontier_squad_indices[n],1);
        }
        rf_rts_group(&runtime->rts,&s->game_state,group,1);
    }
    rf_rts_clear(&runtime->rts);
}

static int rf_game_world_preflight(enum rasterfall_world_id world,const char *path)
{
    struct rasterfall_session *probe=tlibc_malloc(sizeof(*probe));
    int result;
    if(!probe)return -1;
    memset(probe,0,sizeof(*probe));
    result=rasterfall_session_load(probe,path);
    if(result==0 && probe->world_id!=world)result=-1;
    rasterfall_session_unload(probe);
    tlibc_free(probe);
    return result;
}

static int rf_game_request_world_path(struct rf_game_runtime *runtime,
    enum rasterfall_world_id world,const char *path)
{
    uint64_t seed;
    if (!runtime || !runtime->initialized || !runtime->session) return -1;
    if (world != RASTERFALL_WORLD_OUTPOST &&
        world != RASTERFALL_WORLD_CAMPAIGN_01 &&
        world != RASTERFALL_WORLD_RETURN_TO_WHU_V0 &&
        world != RASTERFALL_WORLD_TACTICAL_ARENA &&
        world != RASTERFALL_WORLD_TACTICAL_RANGE &&
        world != RASTERFALL_WORLD_FRONTIER_STATION_01 &&
        world != RASTERFALL_WORLD_PERF_EMPTY &&
        world != RASTERFALL_WORLD_PERF_COMPONENTS) return -1;
    if ((world == RASTERFALL_WORLD_FRONTIER_STATION_01 ||
         world == RASTERFALL_WORLD_TACTICAL_ARENA || world == RASTERFALL_WORLD_TACTICAL_RANGE) &&
        runtime->net.mode != RASTERFALL_NET_OFF) return -1;
    /* Load/parse/project into an independent owner before detaching the live
     * story or unloading its map. Session contains self-references, so never
     * memcpy or swap the probe into the live owner. */
    struct rasterfall_session *prepared=runtime->preloaded_session;
    if(prepared && (!prepared->map_ops.runtime_loaded || prepared->world_id!=world))prepared=NULL;
    if(!prepared && rf_game_world_preflight(world,path)<0)return -1;
    seed = runtime->session->seed;
    rf_story_detach(&runtime->story,runtime->session);
    if ((prepared?rasterfall_session_adopt_map(runtime->session,prepared):
                  rasterfall_session_load(runtime->session,path)) < 0)
        return -1;
    rasterfall_resources_invalidate(rasterfall_render_resources());
    runtime->session->world_id = world;
    rasterfall_session_reset(runtime->session, &runtime->camera,
                             seed ? seed : 1);
    rf_game_seed_world_groups(runtime);
    rasterfall_render_bake_lightmap();
    runtime->render_camera = runtime->camera;
    return 0;
}

int rf_game_request_world(struct rf_game_runtime *runtime,
                          enum rasterfall_world_id world)
{
    return rf_game_request_world_path(runtime,world,world_path(world));
}

int rf_game_world_request_logic_test(void)
{
    struct rf_game_runtime *runtime=tlibc_malloc(sizeof(*runtime));
    struct rasterfall_session *session=tlibc_malloc(sizeof(*session));
    struct rasterfall_session *before=tlibc_malloc(sizeof(*before));
    struct rf_story story_before;
    struct rasterfall_session *prepared=NULL;
    int result=-1,checks=0;
#define WORLD_CHECK(condition) do { if(!(condition)) { \
    __printf("WORLD-REQUEST failed line=%d\n",__LINE__);goto done; } checks++; } while(0)
    if(runtime)memset(runtime,0,sizeof(*runtime));
    if(session)memset(session,0,sizeof(*session));
    WORLD_CHECK(runtime && session && before);
    runtime->initialized=1;runtime->session=session;
    session->world_id=RASTERFALL_WORLD_OUTPOST;session->seed=17;
    session->scene_local.world_generation=23;
    toy_game_init(&session->game_state,17);
    session->game_state.actors[0].movement_hold_token=123;
    rf_story_init(&runtime->story);
    runtime->story.active_story=1;runtime->story.hold_token=123;
    runtime->story.session_revision=9;runtime->story.actor_index=0;
    story_before=runtime->story;memcpy(before,session,sizeof(*before));
    WORLD_CHECK(rf_game_request_world_path(runtime,RASTERFALL_WORLD_CAMPAIGN_01,
        "rasterfall/assets/maps/__rf_missing_world_request_test__.map")<0);
    WORLD_CHECK(!memcmp(before,session,sizeof(*before)) &&
        !memcmp(&story_before,&runtime->story,sizeof(story_before)));
    WORLD_CHECK(rf_game_request_world_path(runtime,RASTERFALL_WORLD_CAMPAIGN_01,
        "rasterfall/assets/manufacturing/blueprints.json")<0);
    WORLD_CHECK(!memcmp(before,session,sizeof(*before)) &&
        !memcmp(&story_before,&runtime->story,sizeof(story_before)));
    WORLD_CHECK(rf_game_request_world(runtime,(enum rasterfall_world_id)-1)<0);
    WORLD_CHECK(!memcmp(before,session,sizeof(*before)) &&
        !memcmp(&story_before,&runtime->story,sizeof(story_before)));
    WORLD_CHECK(rf_game_world_preflight(RASTERFALL_WORLD_OUTPOST,
        world_path(RASTERFALL_WORLD_OUTPOST))==0);
    WORLD_CHECK(!memcmp(before,session,sizeof(*before)) &&
        !memcmp(&story_before,&runtime->story,sizeof(story_before)));
    prepared=tlibc_malloc(sizeof(*prepared));WORLD_CHECK(prepared!=NULL);
    memset(prepared,0,sizeof(*prepared));
    const enum rasterfall_world_id maps[]={RASTERFALL_WORLD_OUTPOST,RASTERFALL_WORLD_CAMPAIGN_01,
        RASTERFALL_WORLD_RETURN_TO_WHU_V0,RASTERFALL_WORLD_FRONTIER_STATION_01,
        RASTERFALL_WORLD_TACTICAL_ARENA,RASTERFALL_WORLD_TACTICAL_RANGE};
    for(unsigned i=0;i<sizeof(maps)/sizeof(maps[0]);++i) {
        WORLD_CHECK(rasterfall_session_load(prepared,world_path(maps[i]))==0);
        runtime->preloaded_session=prepared;
        uint64_t generation=session->scene_local.world_generation;
        WORLD_CHECK(rf_game_request_world(runtime,maps[i])==0);
        WORLD_CHECK(!prepared->map_ops.runtime_loaded && !prepared->level.blob);
        WORLD_CHECK(session->world_id==maps[i] && session->map_ops.level==&session->level &&
            session->map_ops.spawn_count==&session->spawn_count && session->scene_local.world_generation>generation);
        rasterfall_session_unload(prepared);
        WORLD_CHECK(rasterfall_map_projection_counts_match(&session->map_ops));
    }
    result=0;
done:
    if(prepared){rasterfall_session_unload(prepared);tlibc_free(prepared);}
    if(session)rasterfall_session_unload(session);
    tlibc_free(before);tlibc_free(session);tlibc_free(runtime);
    __printf("WORLD-REQUEST %s checks=%d preflight/preserve-world-generation/story-hold\n",
        result?"FAIL":"PASS",checks);
#undef WORLD_CHECK
    return result;
}

int rf_game_init(struct rf_game_runtime *runtime,
                 struct rf_core *core,
                 struct rasterfall_session *session,
                 const char *map_path)
{
    if (!runtime || !core || !session || !map_path) return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->core = core;
    runtime->session = session;
    rf_player_ui_init(&runtime->player_ui);
    rf_player_controls_init(&runtime->player_controls);
    rf_device_service_init(&runtime->device_service);
    rf_story_init(&runtime->story);
    runtime->ui_settings_path="rasterfall-ui-v2.cfg";
    runtime->story_save_path="rasterfall-story-v1.save";
    if ((!strcmp(map_path, "rasterfall/assets/maps/rasterfall_legacy.map") ?
         rasterfall_session_load_legacy(session, map_path) :
         rasterfall_session_load(session, map_path)) < 0) {
        runtime->core = NULL;
        runtime->session = NULL;
        return -1;
    }
    rasterfall_effects_init(&runtime->effects);
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
    rf_application_query_init(&runtime->application_query, core, runtime);
    rf_gui_init(&runtime->gui);
    rf_app_manager_init(&runtime->app_manager, &runtime->gui);
    rf_gui_set_app_manager(&runtime->gui, &runtime->app_manager);
    if (rf_app_manager_register_defaults(&runtime->app_manager) < 0) {
        rasterfall_session_unload(session);
        runtime->core = NULL;
        runtime->session = NULL;
        return -1;
    }
    rf_app_manager_set_query_context(&runtime->app_manager,
                                     &runtime->application_query);
#endif
    rasterfall_net_init(&runtime->net);
    rasterfall_net_discovery_init(&runtime->discovery);
    runtime->lifecycle_paused = 0;
    runtime->lifecycle_running = 1;
    runtime->initialized = 1;
    runtime->render_context.session = session;
    runtime->render_context.effects = &runtime->effects;
    runtime->render_context.net = &runtime->net;
    rasterfall_render_bind(&runtime->render_context);
    rasterfall_resources_invalidate(rasterfall_render_resources());
    return 0;
}

int rf_game_runtime_get_status(const struct rf_game_runtime *runtime,
                               struct rf_game_runtime_status *status)
{
    const struct toy_game_actor *player;
    if (!status) return -1;
    memset(status, 0, sizeof(*status));
    if (!runtime) return -1;
    status->initialized = runtime->initialized;
    status->running = runtime->lifecycle_running;
    status->paused = runtime->lifecycle_paused;
    status->session_active = runtime->session != NULL;
    status->network_mode = runtime->net.mode;
    if (!runtime->session) return 0;
    player = rasterfall_session_local_player_const(runtime->session);
    if (!player) return 0;
    status->local_player_active = player->active;
    status->local_player_state = player->state;
    status->local_player_hp = player->hp;
    return 0;
}

void rf_game_shutdown(struct rf_game_runtime *runtime)
{
    if (!runtime || !runtime->initialized) return;
    rf_story_detach(&runtime->story,runtime->session);
    if(runtime->player_persistence) {
        if(runtime->story.dirty)rf_story_save(&runtime->story,runtime->story_save_path);
        if(runtime->player_controls.settings_dirty)rf_player_settings_save(runtime,runtime->ui_settings_path);
    }
    rasterfall_render_set_outpost_model_lab(0,0);
    rasterfall_net_discovery_close(&runtime->discovery);
    rasterfall_net_close(&runtime->net);
    rasterfall_resources_invalidate(rasterfall_render_resources());
    if (runtime->session)
        rasterfall_session_unload(runtime->session);
    rf_ui_font_release();
    memset(runtime, 0, sizeof(*runtime));
}
