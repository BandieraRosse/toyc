#include "tlibc_everything.h"
#include "core.h"
#include "rf_story.h"
#include "rasterfall_session.h"
#include "rasterfall_rifle_pose.h"

static void story_fixture(struct rf_story *story, struct rasterfall_session *session)
{
    int id;
    memset(session, 0, sizeof(*session));
    toy_game_init(&session->game_state, 7);
    session->game_state.room_limit = 50000;
    session->world_id = RASTERFALL_WORLD_OUTPOST;
    session->scene_local.world_generation = 1;
    id = toy_game_add_ai(&session->game_state, TOY_GAME_AI_LEVEL_2, 1000, 1000, "NULL");
    session->null_actor_index = id - 1;
    session->game_state.actors[id - 1].ai_stationary = 1;
    session->game_state.actors[id - 1].companion = 1;
    session->game_state.actors[id - 1].fire_enabled = 0;
    rf_story_init(story);
}

int rf_story_logic_test(void)
{
    static struct rasterfall_session session;
    struct rf_story story, loaded;
    struct toy_game_actor *actor;
    unsigned sr, nr, hold;
    int i, x, z;
    const char *save = "rf_story_logic_test.bin";
    story_fixture(&story, &session);
    actor = &session.game_state.actors[session.null_actor_index];
    rf_story_emit(&story, RF_STORY_EVENT_OUTPOST_ENTER, NULL);
    rf_story_emit(&story, RF_STORY_EVENT_OUTPOST_ENTER, NULL);
    rf_story_emit(&story, RF_STORY_EVENT_LABS_ENTER, NULL);
    if (story.queue_count != 2) return 1;
    rf_story_update(&story, &session, 16, 1, 1);
    if (story.active_story || actor->movement_hold_token) return 2;
    rf_story_update(&story, &session, 16, 0, 1);
    if (story.active_story != RF_STORY_OUTPOST || story.link != RF_STORY_LINK_LIVE ||
        !actor->movement_hold_token || actor->control_disabled) {
        printf("RF-STORY-TEST connect story=%d link=%d hold=%u actor=%d state=%d hp=%d target=%d\n",
            story.active_story, story.link, actor->movement_hold_token, session.null_actor_index,
            actor->state, actor->hp, actor->combat_target.kind);
        return 3;
    }
    sr = story.session_revision; nr = story.node_revision;
    if (rf_story_answer(&story, &session, sr, nr, 3)) return 4;
    rf_story_collapse(&story, 1);
    if (rf_story_answer(&story, &session, sr, nr, -1) ||
        story.progress[0] != RF_STORY_ACTIVE) return 5;
    rf_story_collapse(&story, 0);
    if (!rf_story_answer(&story, &session, sr, nr, -1) ||
        rf_story_answer(&story, &session, sr, nr, -1)) return 6;
    if (story.node_id != 1012 || story.task.id != RF_STORY_TASK_LABS) return 7;
    if (!rf_story_answer(&story, &session, story.session_revision, story.node_revision, -1) ||
        actor->movement_hold_token || story.progress[0] != RF_STORY_COMPLETED) return 8;
    rf_story_update(&story, &session, 16, 0, 1);
    if (story.active_story != RF_STORY_LABS || story.node_id != 1020) return 9;
    if (!rf_story_answer(&story, &session, story.session_revision, story.node_revision, -1) ||
        story.task.id != RF_STORY_TASK_WEAVER) return 10;
    rf_story_close(&story, &session);
    if (story.progress[1] != RF_STORY_CANCELLED || actor->movement_hold_token ||
        story.task.state != RF_STORY_TASK_RUNNING) return 11;
    rf_story_emit(&story, RF_STORY_EVENT_WEAVER_OPEN, "wrong_device");
    if (story.task.id != RF_STORY_TASK_WEAVER) return 12;
    rf_story_emit(&story, RF_STORY_EVENT_WEAVER_OPEN, "mesh_weaver");
    if (story.task.id != RF_STORY_TASK_BLUEPRINT) return 13;
    rf_story_emit(&story, RF_STORY_EVENT_BLUEPRINT_VIEW, "mesh_weaver");
    if (story.task.state != RF_STORY_TASK_DONE) return 14;
    rf_story_emit(&story, RF_STORY_EVENT_LABS_ENTER, NULL);
    rf_story_emit(&story, RF_STORY_EVENT_LABS_ENTER, NULL);
    if (story.queue_count || story.progress[1]!=RF_STORY_CANCELLED) return 15;

    /* Replaying a node, saving it, then loading into a fresh owner restores
     * stable content only. Actor identity is resolved on the next update. */
    if (!rf_story_replay(&story, &session, RF_STORY_LABS)) return 16;
    rf_story_update(&story, &session, 16, 0, 1);
    if (rf_story_save(&story, save) != 0) return 17;
    rf_story_init(&loaded);
    if (rf_story_load(&loaded, save) != 0 || loaded.node_id != 1020 ||
        loaded.actor_index != -1 || loaded.hold_token || loaded.camera.active) return 18;
    rf_story_detach(&story, &session);
    rf_story_update(&loaded, &session, 16, 0, 1);
    if (loaded.link != RF_STORY_LINK_LIVE || !actor->movement_hold_token) return 19;
    rf_story_detach(&loaded, &session);

    /* A stale owner cannot release control from a reused actor slot. */
    rf_story_update(&story, &session, 16, 0, 1);
    hold = actor->movement_hold_token;
    actor->combat_generation++;
    actor->movement_hold_token = hold + 1;
    rf_story_detach(&story, &session);
    if (actor->movement_hold_token != hold + 1) return 20;
    actor->movement_hold_token = 0;
    rf_story_update(&story, &session, 16, 0, 1);
    actor->hp--;
    rf_story_update(&story, &session, 16, 0, 1);
    if (story.link != RF_STORY_LINK_INTERRUPTED || actor->movement_hold_token) return 21;
    rf_story_collapse(&story, 0);
    rf_story_update(&story, &session, 16, 0, 1);
    story.camera.active = 0;
    rf_story_update(&story, &session, 16, 0, 1);
    if (story.link != RF_STORY_LINK_INTERRUPTED || actor->movement_hold_token) return 22;
    rf_story_close(&story, &session);

    /* Resident and temporary hold are independent of the rifle presentation
     * gate. Existing random idle poses continue naturally under a call. */
    {
        struct rasterfall_rifle_history h = {0};
        struct rasterfall_rifle_pose_input pose;
        int first = -1, changed = 0;
        actor->movement_hold_token = 123;
        actor->animation.id = TOY_GAME_ANIM_IDLE;
        toy_game_set_ai_weapon(&session.game_state, session.null_actor_index, TOY_GAME_WEAPON_AK);
        for (i = 0; i < 24000; i += 100) {
            rasterfall_rifle_sample(actor, (unsigned)i, &h, &pose);
            if (!h.idle_active) return 23;
            if (first < 0) first = h.idle_pose;
            else if (first != h.idle_pose) changed = 1;
        }
        if (!changed) return 24;
    }
    x = actor->x; z = actor->z;
    toy_game_local_player_actor(&session.game_state)->x = x + 10000;
    session.game_state.ai_context_actor_index = session.null_actor_index;
    for (i = 0; i < 60; ++i) toy_game_update_ai_teammate(&session.game_state, 16);
    if (actor->x != x || actor->z != z || actor->animation.time_ms <= 0) return 25;
    actor->movement_hold_token = 0;
    actor->companion = actor->ai_stationary = 0;
    actor->deployment_x = x + 2000;
    for (i = 0; i < 20; ++i) toy_game_update_ai_teammate(&session.game_state, 16);
    if (actor->x == x && actor->z == z) return 26;
    /* Both linear performances advance by speech time, pause while hidden,
     * release actor control at the end, and never queue twice in one game. */
    if (rf_story_line_duration_ms("abcd") <= rf_story_line_duration_ms("ab") ||
        rf_story_line_duration_ms("ab.") <= rf_story_line_duration_ms("ab")) return 27;
    for (i = 0; i < 2; ++i) {
        int which = i ? RF_STORY_LABS : RF_STORY_OUTPOST;
        int duration;
        story_fixture(&story, &session);
        if (!story.collapsed || !rf_story_replay(&story, &session, which)) return 27;
        rf_story_update(&story, &session, 16, 0, 1);
        if (rf_story_current_node(&story)->choice_count) return 28;
        duration = rf_story_line_duration_ms(rf_story_current_node(&story)->line);
        rf_story_collapse(&story, 1);
        rf_story_update(&story, &session, duration, 0, 1);
        if (story.node_id != which * 10) return 28;
        rf_story_collapse(&story, 0);
        rf_story_update(&story, &session, duration - 17, 0, 1);
        if (story.node_id != which * 10) return 28;
        rf_story_update(&story, &session, 1, 0, 1);
        if (story.node_id != (i ? 1021 : 1012) || story.line_elapsed_ms) return 28;
        rf_story_update(&story, &session,
            rf_story_line_duration_ms(rf_story_current_node(&story)->line), 0, 1);
        if (story.active_story || !story.collapsed || story.progress[i] != RF_STORY_COMPLETED ||
            session.game_state.actors[session.null_actor_index].movement_hold_token) return 29;
        rf_story_emit(&story, i ? RF_STORY_EVENT_LABS_ENTER : RF_STORY_EVENT_OUTPOST_ENTER, NULL);
        if (story.queue_count) return 29;
    }
    if (rf_story_save(&story, save) || rf_story_save(&story, save)) return 30;
    {
        struct rf_story before;
        int fd = __openat(AT_FDCWD, save, O_WRONLY | O_TRUNC, 0);
        if (fd < 0) return 31;
        if (__write(fd, "bad", 3) != 3) { __close(fd); return 32; }
        __close(fd);
        before = story;
        if (rf_story_load(&story, save) != -1 || memcmp(&before, &story, sizeof(story))) return 33;
    }
    /* A new game may play completed saved content once; region re-entry and
     * world replacement within the same game do not replay it. */
    story_fixture(&story,&session);
    if(rf_map_runtime_load(&session.map_ops.runtime,"rasterfall/assets/maps/outpost.map")<0)return 34;
    {
        int failed=0;
        struct toy_game_actor *p=toy_game_local_player_actor(&session.game_state);
        const struct rf_map_runtime_region *region=rf_map_runtime_find_region(&session.map_ops.runtime,"outpost_safe");
        if(!region){rf_map_runtime_unload(&session.map_ops.runtime);return 35;}
        p->x=0;p->z=0;
        story.progress[0]=RF_STORY_COMPLETED;
        if(rf_story_save(&story,save) || rf_story_load(&story,save))failed=36;
        rf_story_update(&story,&session,16,0,1);
        if(story.active_story!=RF_STORY_OUTPOST)failed=37;
        rf_story_answer(&story,&session,story.session_revision,story.node_revision,-1);
        rf_story_answer(&story,&session,story.session_revision,story.node_revision,-1);
        for(i=0;i<100;++i)rf_story_update(&story,&session,16,0,1);
        if(story.active_story || story.queue_count)failed=38;
        p->x=region->bounds.max_x+1000;
        rf_story_update(&story,&session,16,0,1);
        p->x=0;
        rf_story_update(&story,&session,16,0,1);
        if(story.active_story || story.queue_count)failed=39;
        rf_story_close(&story,&session);
        rf_story_update(&story,&session,16,0,1);
        if(story.active_story || story.queue_count)failed=40;
        session.scene_local.world_generation++;
        rf_story_update(&story,&session,16,0,1);
        if(story.active_story || story.queue_count)failed=41;
        /* A full history still changes its revision when new text arrives. */
        for(i=0;i<RF_STORY_HISTORY_CAP;++i) {
            rf_story_replay(&story,&session,RF_STORY_OUTPOST);
            rf_story_update(&story,&session,16,0,1);
        }
        sr=story.history_revision;
        rf_story_answer(&story,&session,story.session_revision,story.node_revision,-1);
        if(story.history_count!=RF_STORY_HISTORY_CAP || story.history_revision!=sr+1)failed=42;
        rf_story_detach(&story,&session);
        rf_map_runtime_unload(&session.map_ops.runtime);
        if(failed)return failed;
    }
    return 0;
}
