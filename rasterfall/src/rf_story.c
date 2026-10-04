#include "tlibc_everything.h"
#include "core.h"
#include "rf_story.h"
#include "rasterfall_session.h"
#include "rasterfall_units.h"

/* Content is a small, immutable graph. Add nodes and event transitions here;
 * the controller, input, layout, and camera do not know dialogue branches. */
static const struct rf_story_node nodes[] = {
    { 1010, RF_STORY_OUTPOST, 10, "null", "NULL", "前哨站",
      "你到了。我是 NULL。这里是前哨站，先熟悉一下周围吧。", "null_comms", 0, 0, 0,
      {{0}}, 1012, RF_STORY_TASK_LABS },
    { 1011, RF_STORY_OUTPOST, 10, "null", "NULL", "前哨站",
      "你好。需要了解这里的时候，可以来找我。", "null_comms", 0, 0, 0, {{0}}, 0, 0 },
    { 1012, RF_STORY_OUTPOST, 10, "null", "NULL", "前哨站",
      "你可以先看看这里的设施。想试用设备的话，再去实验区域。", "null_comms", 0, 0, 0, {{0}}, 0, 0 },
    { 1013, RF_STORY_OUTPOST, 10, "null", "NULL", "前哨站",
      "好，保持联系。", "null_comms", 0, 0, 0, {{0}}, 0, 0 },
    { 1020, RF_STORY_LABS, 10, "null", "NULL", "前哨站 / 远程",
      "这里是实验区域。你可以在这里查看设备、试用武器，也可以看看网格编织机的制造过程。", "null_comms", 0, 0, 0,
      {{0}}, 1021, RF_STORY_TASK_WEAVER },
    { 1021, RF_STORY_LABS, 10, "null", "NULL", "前哨站 / 远程",
      "找到网格编织机，打开蓝图库。选好物品后，界面会显示制造时间和能源需求。", "null_comms", 0, 0, 0, {{0}}, 0, 0 },
    { 1022, RF_STORY_LABS, 10, "null", "NULL", "前哨站 / 远程",
      "好。需要操作设备时，靠近后查看交互提示。", "null_comms", 0, 0, 0, {{0}}, 0, 0 },
    { 3010, RF_STORY_FRONTIER_ARRIVAL, 30, "null", "NULL", "前哨站 / 字幕通讯",
      "这里曾由 RF 与另一组织共同使用。他们已经离开；按我们的回收判断，清除占领者并保住设施。", "", 0, 0, 0, {{0}}, 0, 0 },
    { 3020, RF_STORY_FRONTIER_INFECTED, 40, "null", "NULL", "前哨站 / 字幕通讯",
      "北侧和东侧发现感染者。它们也会攻击占领者，注意侧翼。", "", 0, 0, 0, {{0}}, 0, 0 },
    { 3030, RF_STORY_FRONTIER_PREPARE, 50, "null", "NULL", "前哨站 / 字幕通讯",
      "初始守军已肃清。还有新的部队在接近，45 秒整备；接管车间、仓库和能源设施。", "", 0, 0, 0, {{0}}, 0, 0 },
    { 3040, RF_STORY_FRONTIER_COUNTERATTACK, 60, "null", "NULL", "前哨站 / 字幕通讯",
      "感染者开始进场，西路也有枪手。他们会互相交火，保持小队协同并肃清全部增援。", "", 0, 0, 0, {{0}}, 0, 0 },
    { 3050, RF_STORY_FRONTIER_SECURED, 70, "null", "NULL", "前哨站 / 字幕通讯",
      "设施已接管，现场肃清。站点回收完成；可继续使用设备，或返回前哨站。", "", 0, 0, 0, {{0}}, 0, 0 },
    { 3060, RF_STORY_FRONTIER_FAILED, 80, "null", "NULL", "前哨站 / 字幕通讯",
      "小队已无法继续行动。重整后再来，或先返回前哨站。", "", 0, 0, 0, {{0}}, 0, 0 }
};

struct story_camera_profile {
    const char *id;
    int distances[3], offset_sy, offset_cy;
    int eye_offset_y, pitch_sy, pitch_cy, near_rfu, fov_degrees, shell_radius;
};
/* Framing is content configuration. A different camera ID can select a
 * different fixed setup without editing the conversation controller. */
static const struct story_camera_profile camera_profiles[] = {
    { "null_comms", { 720, 680, 640 }, 0, 1024,
      RASTERFALL_HUMAN_HEIGHT_RFU / 2 + RASTERFALL_RFU_FROM_CM(35) - RASTERFALL_HUMAN_EYE_HEIGHT_RFU,
      0, 1024, 64, 67, 100 }
};

static int story_index(int id)
{
    if(id>=RF_STORY_FRONTIER_ARRIVAL && id<=RF_STORY_FRONTIER_FAILED)
        return 2+id-RF_STORY_FRONTIER_ARRIVAL;
    return id == RF_STORY_OUTPOST ? 0 : id == RF_STORY_LABS ? 1 : -1;
}

int rf_story_trigger_policy(int id)
{
    /* Per-game latches are transient; saved completion is not a lifetime ban. */
    static const struct { int id, policy; } triggers[] = {
        { RF_STORY_OUTPOST, RF_STORY_TRIGGER_EACH_GAME },
        { RF_STORY_LABS, RF_STORY_TRIGGER_EACH_GAME }
    };
    for (unsigned i=0;i<sizeof(triggers)/sizeof(triggers[0]);++i)
        if (triggers[i].id==id) return triggers[i].policy;
    return RF_STORY_TRIGGER_ONCE;
}

const struct rf_story_node *rf_story_find_node(int id)
{
    unsigned i;
    for (i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i)
        if (nodes[i].id == id) return &nodes[i];
    return NULL;
}

const struct rf_story_node *rf_story_current_node(const struct rf_story *s)
{
    return s && s->active_story ? rf_story_find_node(s->node_id) : NULL;
}

int rf_story_line_duration_ms(const char *line)
{
    /* Temporary normal speech speed: about four CJK syllables/second.
     * Decode UTF-8 scalars so CJK bytes do not inflate speaking time. */
    const unsigned char *p = (const unsigned char *)line;
    int ms = 500;
    if (!p) return ms;
    while (*p) {
        unsigned cp = *p++;
        if (cp >= 0xC0) {
            int extra = cp < 0xE0 ? 1 : cp < 0xF0 ? 2 : 3;
            cp &= extra == 1 ? 31 : extra == 2 ? 15 : 7;
            while (extra-- && (*p & 0xC0) == 0x80) cp = (cp << 6) | (*p++ & 63);
        }
        if (cp == ',' || cp == 0xFF0C || cp == 0x3001) ms += 200;
        else if (cp == '.' || cp == '!' || cp == '?' || cp == 0x3002 ||
                 cp == 0xFF01 || cp == 0xFF1F) ms += 400;
        else if (cp > 32) ms += cp < 128 ? 100 : 250;
    }
    return ms;
}

static void task_set(struct rf_story *s, int id)
{
    memset(&s->task, 0, sizeof(s->task));
    s->task.id = id;
    s->task.state = id ? RF_STORY_TASK_RUNNING : RF_STORY_TASK_INACTIVE;
    switch (id) {
    case RF_STORY_TASK_EXPLORE:
        s->task.title = "熟悉周围的设施";
        s->task.hint = "自由探索，靠近设备查看交互提示";
        break;
    case RF_STORY_TASK_LABS:
        s->task.title = "前往实验区域";
        s->task.hint = "沿园区道路前往实验设施";
        strcpy(s->task.target_id, "character_lab_area");
        break;
    case RF_STORY_TASK_WEAVER:
        s->task.title = "打开网格编织机的蓝图库";
        s->task.hint = "靠近网格编织机并查看交互提示";
        strcpy(s->task.target_id, "mesh_weaver");
        break;
    case RF_STORY_TASK_BLUEPRINT:
        s->task.title = "查看一个可用蓝图";
        s->task.hint = "阅读制造时间和能源需求；选择不会开始制造";
        strcpy(s->task.target_id, "mesh_weaver");
        break;
    default: s->task.title = ""; s->task.hint = ""; break;
    }
    s->dirty = 1;
}

void rf_story_init(struct rf_story *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->enabled = 1;
    s->collapsed = 1;
    s->actor_index = -1;
    s->session_revision = s->node_revision = 1;
    task_set(s, RF_STORY_TASK_NONE);
    s->dirty = 0;
}

static struct toy_game_actor *bound_actor(struct rf_story *s,
    struct rasterfall_session *session)
{
    struct toy_game_actor *actor;
    if (!session || s->world_generation != session->scene_local.world_generation ||
        s->actor_index < 0 || s->actor_index >= TOY_GAME_MAX_ACTORS) return NULL;
    actor = &session->game_state.actors[s->actor_index];
    if (!actor->active || actor->actor_id != s->actor_id ||
        actor->combat_generation != s->actor_generation) return NULL;
    return actor;
}

static void release_hold(struct rf_story *s, struct rasterfall_session *session)
{
    struct toy_game_actor *actor = bound_actor(s, session);
    if (actor && s->hold_token && actor->movement_hold_token == s->hold_token)
        actor->movement_hold_token = 0;
    s->hold_token = 0;
}

void rf_story_detach(struct rf_story *s, struct rasterfall_session *session)
{
    if (!s) return;
    release_hold(s, session);
    s->actor_index = -1;
    s->camera.active = 0;
    if (!session || s->world_generation != session->scene_local.world_generation)
        s->region_presence = 0;
    if (session) s->world_generation = session->scene_local.world_generation;
    s->link = s->active_story ? RF_STORY_LINK_CONNECTING : RF_STORY_LINK_OFF;
    s->retry_ms = 0;
    if(s->frontier_mission_id) {
        if(story_index(s->active_story)>=2)s->active_story=s->node_id=0;
        int kept=0;
        for(int i=0;i<s->queue_count;++i)
            if(story_index(s->queue[i])<2)s->queue[kept++]=s->queue[i];
        s->queue_count=kept;s->frontier_mission_id=0;s->collapsed=1;s->link=RF_STORY_LINK_OFF;
        for(int i=2;i<8;++i)s->progress[i]=RF_STORY_UNSEEN;
    }
}

void rf_story_frontier_events(struct rf_story *s,struct rasterfall_session *session,unsigned events)
{
    if(!s || !session || !s->enabled ||
        session->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01)return;
    if(s->world_generation!=session->scene_local.world_generation)rf_story_detach(s,session);
    if(s->frontier_mission_id!=session->frontier.mission_id) {
        rf_story_close(s,session);
        s->queue_count=0;
        for(int i=2;i<8;++i)s->progress[i]=RF_STORY_UNSEEN;
        s->frontier_mission_id=session->frontier.mission_id;
        memset(&s->task,0,sizeof(s->task));
    }
    for(int bit=0;bit<6;++bit)if(events&(1u<<bit)) {
        int id=RF_STORY_FRONTIER_ARRIVAL+bit,index=story_index(id);
        if(s->progress[index]!=RF_STORY_UNSEEN || s->queue_count>=RF_STORY_QUEUE_CAP)continue;
        s->progress[index]=RF_STORY_QUEUED;s->queue[s->queue_count++]=id;
    }
}

static void queue_story(struct rf_story *s, int id)
{
    int i = story_index(id);
    if (i >= 0 && rf_story_trigger_policy(id) == RF_STORY_TRIGGER_EACH_GAME) {
        if ((s->triggered_this_game & (1u << i)) || s->queue_count >= RF_STORY_QUEUE_CAP) return;
        s->triggered_this_game |= 1u << i;
        s->progress[i] = RF_STORY_QUEUED;
        s->queue[s->queue_count++] = id;
        s->dirty = 1;
        return;
    }
    if (i < 0 || (s->progress[i] != RF_STORY_UNSEEN &&
        !(rf_story_trigger_policy(id)==RF_STORY_TRIGGER_EACH_ENTRY &&
          (s->progress[i]==RF_STORY_COMPLETED || s->progress[i]==RF_STORY_CANCELLED))) ||
        s->queue_count >= RF_STORY_QUEUE_CAP) return;
    s->progress[i] = RF_STORY_QUEUED;
    s->queue[s->queue_count++] = id;
    s->dirty = 1;
}

void rf_story_emit(struct rf_story *s, int event, const char *target)
{
    if (!s || !s->enabled) return;
    if (event == RF_STORY_EVENT_OUTPOST_ENTER) queue_story(s, RF_STORY_OUTPOST);
    if (event == RF_STORY_EVENT_LABS_ENTER) {
        queue_story(s, RF_STORY_LABS);
        if (s->task.id == RF_STORY_TASK_LABS && s->task.state == RF_STORY_TASK_RUNNING) {
            s->task.state = RF_STORY_TASK_DONE; s->dirty = 1;
        }
    }
    if (!target || strcmp(target, s->task.target_id)) return;
    if (event == RF_STORY_EVENT_WEAVER_OPEN && s->task.id == RF_STORY_TASK_WEAVER &&
        s->task.state == RF_STORY_TASK_RUNNING) task_set(s, RF_STORY_TASK_BLUEPRINT);
    if (event == RF_STORY_EVENT_BLUEPRINT_VIEW && s->task.id == RF_STORY_TASK_BLUEPRINT &&
        s->task.state == RF_STORY_TASK_RUNNING) {
        s->task.state = RF_STORY_TASK_DONE; s->task.hint = "蓝图已查看；可自由决定是否制造";
        s->dirty = 1;
    }
    if (event == RF_STORY_EVENT_DEVICE_LOST && s->task.state == RF_STORY_TASK_RUNNING) {
        s->task.state = RF_STORY_TASK_FAILED; s->task.target_valid = 0; s->dirty = 1;
    }
}

static void history_add(struct rf_story *s, int choice)
{
    struct rf_story_history_entry *h;
    if (s->history_count >= RF_STORY_HISTORY_CAP) {
        memmove(s->history, s->history + 1,
            (RF_STORY_HISTORY_CAP - 1) * sizeof(s->history[0]));
        s->history_count--;
    }
    h = &s->history[s->history_count++];
    h->story_id = s->active_story; h->node_id = s->node_id;
    h->choice = choice; h->revision = s->node_revision;
    s->history_revision++;
}

static int actor_available(const struct toy_game_actor *a)
{
    return a && a->active && a->state == TOY_GAME_ACTOR_ALIVE && a->hp > 0 &&
        !a->control_disabled && !a->special_control && !a->airborne_ms &&
        !a->airborne_y && !a->damage_flash_ms && a->combat_target.kind < 0;
}

int rf_story_actor_camera(const struct rasterfall_session *session,
    const struct toy_game_actor *a,struct camera *view)
{
    const struct story_camera_profile *p=&camera_profiles[0];
    int distance=0,eye=a->ground_y+a->airborne_y+
        RASTERFALL_HUMAN_EYE_HEIGHT_RFU+p->eye_offset_y;
    for(int i=0;i<3;++i) {
        int blocked=0,steps=(p->distances[i]+31)/32;
        for(int n=1;n<=steps;++n)
            if(toy_game_position_blocked_at_height(&session->game_state,
                a->x+p->offset_sy*p->distances[i]*n/steps/1024,
                a->z+p->offset_cy*p->distances[i]*n/steps/1024,32,eye)) {blocked=1;break;}
        if(!blocked){distance=p->distances[i];break;}
    }
    if(!distance)return 0;
    memset(view,0,sizeof(*view));
    view->x=a->x+p->offset_sy*distance/1024;view->z=a->z+p->offset_cy*distance/1024;
    view->y=RASTERFALL_WORLD_GROUND_Y+eye;
    view->sy=-p->offset_sy;view->cy=-p->offset_cy;
    view->pitch_sy=p->pitch_sy;view->pitch_cy=p->pitch_cy;
    return 1;
}

static int bind_camera(struct rf_story *s, struct rasterfall_session *session)
{
    struct toy_game_actor *a;
    struct rf_story_camera_entity *c = &s->camera;
    const struct rf_story_node *node = rf_story_current_node(s);
    const struct story_camera_profile *profile = NULL;
    const char *camera_id = node ? node->camera_id : "null_comms";
    int i, distance = 0, index = session->null_actor_index;
    unsigned candidate;
    for (candidate = 0; candidate < sizeof(camera_profiles) / sizeof(camera_profiles[0]); ++candidate)
        if (!strcmp(camera_profiles[candidate].id, camera_id)) profile = &camera_profiles[candidate];
    if (!profile) return 0;
    if (session->world_id != RASTERFALL_WORLD_OUTPOST ||
        index < 0 || index >= TOY_GAME_MAX_ACTORS) return 0;
    a = &session->game_state.actors[index];
    if (!actor_available(a)) return 0;
    /* Authored forward hemisphere. Validate the entire optical corridor,
     * not just the endpoint, so a repositioned resident cannot get a camera
     * through a wall. Try bounded distances once, then report unavailable. */
    for (i = 0; i < 3; ++i) {
        int n, blocked = 0, steps = (profile->distances[i] + 31) / 32;
        /* Sampling distance stays below the query diameter, including thin
         * authored walls between endpoints; this runs only at connection. */
        for (n = 1; n <= steps; ++n)
            if (toy_game_position_blocked_at_height(&session->game_state,
                    a->x + profile->offset_sy * profile->distances[i] * n / steps / 1024,
                    a->z + profile->offset_cy * profile->distances[i] * n / steps / 1024, 32,
                    a->ground_y + RASTERFALL_HUMAN_EYE_HEIGHT_RFU + profile->eye_offset_y)) {
                blocked = 1; break;
            }
        if (!blocked) { distance = profile->distances[i]; break; }
    }
    if (!distance) return 0;
    memset(c, 0, sizeof(*c));
    c->active = 1; c->stable_id = profile->id;
    c->generation = ++s->next_camera_generation;
    if (!c->generation) c->generation = ++s->next_camera_generation;
    c->actor_index = index; c->actor_id = a->actor_id;
    c->actor_generation = a->combat_generation;
    c->world_generation = session->scene_local.world_generation;
    c->anchor_x = a->x; c->anchor_z = a->z;
    c->view.x = a->x + profile->offset_sy * distance / 1024;
    c->view.z = a->z + profile->offset_cy * distance / 1024;
    c->view.y = RASTERFALL_WORLD_GROUND_Y + a->ground_y +
        RASTERFALL_HUMAN_EYE_HEIGHT_RFU + profile->eye_offset_y;
    c->view.sy = -profile->offset_sy; c->view.cy = -profile->offset_cy;
    c->view.pitch_sy = profile->pitch_sy; c->view.pitch_cy = profile->pitch_cy;
    c->near_rfu = profile->near_rfu; c->fov_degrees = profile->fov_degrees;
    c->shell_radius = profile->shell_radius;
    s->actor_index = index; s->actor_id = a->actor_id;
    s->actor_generation = a->combat_generation;
    s->world_generation = session->scene_local.world_generation;
    s->last_hp = a->hp;
    return 1;
}

const struct rf_story_camera_entity *rf_story_camera(const struct rf_story *s,
    const struct rasterfall_session *session)
{
    const struct toy_game_actor *a;
    const struct rf_story_camera_entity *c;
    if (!s || !session) return NULL;
    c = &s->camera;
    if (!c->active || c->world_generation != session->scene_local.world_generation ||
        c->actor_index < 0 || c->actor_index >= TOY_GAME_MAX_ACTORS) return NULL;
    a = &session->game_state.actors[c->actor_index];
    if (!a->active || a->actor_id != c->actor_id ||
        a->combat_generation != c->actor_generation || a->state != TOY_GAME_ACTOR_ALIVE)
        return NULL;
    return c;
}

static void task_target(struct rf_story *s, const struct rasterfall_session *session)
{
    const struct rf_map_runtime_region *region;
    const struct rf_map_runtime_object *object;
    s->task.target_valid = 0;
    if (s->task.state != RF_STORY_TASK_RUNNING || !s->task.target_id[0]) return;
    region = rf_map_runtime_find_region(&session->map_ops.runtime, s->task.target_id);
    if (region) {
        s->task.x = region->origin_x; s->task.z = region->origin_z;
        s->task.y = 0; s->task.target_valid = 1; return;
    }
    object = rf_map_runtime_find_object(&session->map_ops.runtime, s->task.target_id);
    if (object) {
        s->task.x = object->x; s->task.y = object->y; s->task.z = object->z;
        s->task.target_valid = 1;
    }
}

static void detect_regions(struct rf_story *s, const struct rasterfall_session *session)
{
    const struct toy_game_actor *p = toy_game_local_player_actor_const(&session->game_state);
    const struct rf_map_runtime_region *r;
    int i;
    unsigned presence=0;
    if (!p || p->state != TOY_GAME_ACTOR_ALIVE || session->world_id != RASTERFALL_WORLD_OUTPOST)
        return;
    r = rf_map_runtime_find_region(&session->map_ops.runtime, "outpost_safe");
    if (r && p->x >= r->bounds.min_x && p->x <= r->bounds.max_x &&
        p->z >= r->bounds.min_z && p->z <= r->bounds.max_z)
        presence |= 1;
    for (i = 0; i < rf_map_runtime_region_count(&session->map_ops.runtime); ++i) {
        r = rf_map_runtime_region_at(&session->map_ops.runtime, i);
        if (!r || strcmp(r->kind, "experiment")) continue;
        if (p->x >= r->bounds.min_x && p->x <= r->bounds.max_x &&
            p->z >= r->bounds.min_z && p->z <= r->bounds.max_z) {
            presence |= 2; break;
        }
    }
    if ((presence & 1) && !(s->region_presence & 1))
        rf_story_emit(s, RF_STORY_EVENT_OUTPOST_ENTER, NULL);
    if ((presence & 2) && !(s->region_presence & 2))
        rf_story_emit(s, RF_STORY_EVENT_LABS_ENTER, NULL);
    s->region_presence=presence;
}

static void interrupt_call(struct rf_story *s, struct rasterfall_session *session)
{
    release_hold(s, session);
    s->link = RF_STORY_LINK_INTERRUPTED;
    s->progress[story_index(s->active_story)] = RF_STORY_INTERRUPTED;
    s->dirty = 1;
}

static void face_camera(struct toy_game_actor *a, const struct camera *camera, int dt_ms)
{
    int dx = camera->x - a->x, dz = camera->z - a->z;
    int length = isqrt((long long)dx * dx + (long long)dz * dz);
    int step = dt_ms * 2, sy, cy;
    if (!length) return;
    if (step > 128) step = 128;
    sy = dx * 1024 / length; cy = dz * 1024 / length;
    /* Bounded body facing reuses the ordinary actor pose. A future head/neck
     * look-at layer can consume the same camera target without replacing it. */
    if ((long long)a->sy * sy + (long long)a->cy * cy < -900000) {
        a->sy += step; a->cy += step / 8;
    } else {
        a->sy += (sy - a->sy) * step / 1024;
        a->cy += (cy - a->cy) * step / 1024;
    }
    length = isqrt((long long)a->sy * a->sy + (long long)a->cy * a->cy);
    if (length) { a->sy = a->sy * 1024 / length; a->cy = a->cy * 1024 / length; }
}

int rf_story_subtitle_only(const struct rf_story *s)
{
    const struct rf_story_node *node=rf_story_current_node(s);
    return node && (!node->camera_id || !node->camera_id[0]);
}

void rf_story_update(struct rf_story *s, struct rasterfall_session *session,
    int dt_ms, int combat, int allow_start)
{
    struct toy_game_actor *a;
    if (!s || !session) return;
    if (!s->enabled) { rf_story_detach(s, session); return; }
    if (s->world_generation != session->scene_local.world_generation)
        rf_story_detach(s, session);
    s->combat = combat != 0;
    if(session->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01) {
        if(!s->active_story && allow_start && s->queue_count) {
            s->active_story=s->queue[0];
            memmove(s->queue,s->queue+1,sizeof(s->queue[0])*(--s->queue_count));
            s->node_id=s->active_story*10;s->session_revision++;s->node_revision++;
            s->progress[story_index(s->active_story)]=RF_STORY_ACTIVE;
            s->collapsed=0;s->link=RF_STORY_LINK_UNAVAILABLE;s->line_elapsed_ms=0;
            s->camera.active=0;s->actor_index=-1;history_add(s,-1);
        }
        const struct rf_story_node *radio=rf_story_current_node(s);
        if(radio && story_index(s->active_story)>=2 && !s->collapsed && dt_ms>0) {
            s->link=RF_STORY_LINK_UNAVAILABLE;
            s->line_elapsed_ms+=dt_ms;
            if(s->line_elapsed_ms>=rf_story_line_duration_ms(radio->line))
                rf_story_answer(s,session,s->session_revision,s->node_revision,-1);
        }
        return;
    }
    if(story_index(s->active_story)>=2) {
        rf_story_close(s,session);s->queue_count=0;s->frontier_mission_id=0;
    }
    detect_regions(s, session);
    task_target(s, session);
    if (!s->active_story && allow_start && !combat && s->queue_count &&
        session->world_id == RASTERFALL_WORLD_OUTPOST &&
        session->null_actor_index >= 0 && session->null_actor_index < TOY_GAME_MAX_ACTORS &&
        actor_available(&session->game_state.actors[session->null_actor_index])) {
        int i, best = 0;
        /* Priorities belong to content; FIFO breaks ties. Never replace an
         * unanswered lower-priority node without an explicit interrupt. */
        for (i = 1; i < s->queue_count; ++i)
            if (rf_story_find_node(s->queue[i] * 10)->priority >
                rf_story_find_node(s->queue[best] * 10)->priority) best = i;
        s->active_story = s->queue[best];
        for (i = best + 1; i < s->queue_count; ++i) s->queue[i - 1] = s->queue[i];
        s->queue_count--;
        s->node_id = s->active_story * 10;
        s->session_revision++; s->node_revision++;
        s->progress[story_index(s->active_story)] = RF_STORY_ACTIVE;
        s->collapsed = 0; s->link = RF_STORY_LINK_CONNECTING; s->retry_ms = 0;
        s->line_elapsed_ms = 0;
        s->dirty = 1; history_add(s, -1);
    }
    if (!s->active_story) {
        release_hold(s, session);
        if (!rf_story_camera(s, session) && session->world_id == RASTERFALL_WORLD_OUTPOST)
            bind_camera(s, session);
        return;
    }
    if (s->link == RF_STORY_LINK_CONNECTING) {
        if (bind_camera(s, session)) {
            s->link = RF_STORY_LINK_LIVE;
            s->progress[story_index(s->active_story)] = RF_STORY_ACTIVE;
        } else {
            s->retry_ms += dt_ms;
            if (s->retry_ms >= 3000) {
                s->link = RF_STORY_LINK_UNAVAILABLE;
                s->progress[story_index(s->active_story)] = RF_STORY_INTERRUPTED;
                s->dirty = 1;
            }
            return;
        }
    }
    if (s->link == RF_STORY_LINK_INTERRUPTED && !combat && allow_start &&
        session->null_actor_index >= 0 && session->null_actor_index < TOY_GAME_MAX_ACTORS &&
        actor_available(&session->game_state.actors[session->null_actor_index])) {
        s->link = RF_STORY_LINK_CONNECTING; s->retry_ms = 0;
        return;
    }
    if (s->link != RF_STORY_LINK_LIVE) return;
    a = bound_actor(s, session);
    if (!actor_available(a) || !rf_story_camera(s, session) || a->hp < s->last_hp ||
        abs(a->x - s->camera.anchor_x) > 128 || abs(a->z - s->camera.anchor_z) > 128) {
        interrupt_call(s, session); return;
    }
    if (!s->hold_token) {
        s->hold_token = 0x53540000u | (s->session_revision & 65535u);
        if (a->movement_hold_token && a->movement_hold_token != s->hold_token) {
            s->hold_token = 0; interrupt_call(s, session); return;
        }
        a->movement_hold_token = s->hold_token;
    }
    face_camera(a, &s->camera.view, dt_ms);
    s->last_hp = a->hp;
    if (!s->collapsed && dt_ms > 0) {
        const struct rf_story_node *node = rf_story_current_node(s);
        s->line_elapsed_ms += dt_ms;
        if (node && !node->choice_count && s->line_elapsed_ms >= rf_story_line_duration_ms(node->line))
            rf_story_answer(s, session, s->session_revision, s->node_revision, -1);
    }
}

int rf_story_answer(struct rf_story *s, struct rasterfall_session *session,
    unsigned session_revision, unsigned node_revision, int choice)
{
    const struct rf_story_node *n = rf_story_current_node(s);
    int radio=n && story_index(s->active_story)>=2;
    if (!n || !session || s->collapsed ||
        (radio?s->link!=RF_STORY_LINK_UNAVAILABLE:s->link!=RF_STORY_LINK_LIVE) ||
        session_revision != s->session_revision || node_revision != s->node_revision ||
        (!radio && !actor_available(bound_actor(s, session)))) return 0;
    if (n->choice_count) {
        const struct rf_story_choice *c;
        if (choice < 0 || choice >= n->choice_count) return 0;
        c = &n->choices[choice];
        if (!rf_story_find_node(c->next_node)) return 0;
        history_add(s, choice);
        if (c->task) { task_set(s, c->task); task_target(s, session); }
        if (c->event) rf_story_emit(s, c->event, NULL);
        s->node_id = c->next_node; s->node_revision++;
        history_add(s, -1);
    } else {
        if (choice != -1) return 0;
        if (n->auto_task) { task_set(s, n->auto_task); task_target(s, session); }
        if (n->auto_next_node) {
            if (!rf_story_find_node(n->auto_next_node)) return 0;
            s->node_id = n->auto_next_node; s->node_revision++;
            s->line_elapsed_ms = 0;
            history_add(s, -1); s->dirty = 1;
            return 1;
        }
        s->progress[story_index(s->active_story)] = RF_STORY_COMPLETED;
        release_hold(s, session);
        s->active_story = s->node_id = 0;
        s->collapsed = 1;
        s->link = RF_STORY_LINK_OFF; s->node_revision++;
    }
    s->dirty = 1;
    s->line_elapsed_ms = 0;
    return 1;
}

void rf_story_collapse(struct rf_story *s, int collapsed)
{
    if (!s || !s->active_story) return;
    s->collapsed = collapsed != 0;
    if (!s->collapsed && (s->link == RF_STORY_LINK_INTERRUPTED ||
        s->link == RF_STORY_LINK_UNAVAILABLE)) {
        s->link = RF_STORY_LINK_CONNECTING; s->retry_ms = 0;
    }
    s->dirty = 1;
}

void rf_story_close(struct rf_story *s, struct rasterfall_session *session)
{
    if (!s || !s->active_story) return;
    release_hold(s, session);
    s->progress[story_index(s->active_story)] = RF_STORY_CANCELLED;
    s->active_story = s->node_id = 0; s->collapsed = 0;
    s->link = RF_STORY_LINK_OFF; s->node_revision++; s->dirty = 1;
}

int rf_story_reset(struct rf_story *s, struct rasterfall_session *session, int id)
{
    int i, n = 0, index = story_index(id);
    if (!s || (id && index < 0)) return 0;
    if (!id || s->active_story == id) rf_story_close(s, session);
    for (i = 0; i < s->queue_count; ++i)
        if (id && s->queue[i] != id) s->queue[n++] = s->queue[i];
    s->queue_count = n;
    if (!id) memset(s->progress,0,sizeof(s->progress));
    else s->progress[index] = RF_STORY_UNSEEN;
    s->region_presence &= id ? ~(1u << index) : 0;
    s->triggered_this_game &= id ? ~(1u << index) : 0;
    task_set(s, RF_STORY_TASK_NONE);
    s->session_revision++; s->node_revision++; s->dirty = 1;
    return 1;
}

int rf_story_replay(struct rf_story *s, struct rasterfall_session *session, int id)
{
    if (story_index(id) < 0 || !rf_story_reset(s, session, id)) return 0;
    queue_story(s, id);
    return 1;
}

/* Explicit LE records avoid ABI/padding/endianness dependencies. Current
 * progress persistence is intentionally independent of world/actor saves. */
#define STORY_SAVE_WORDS 20
static void put_word(unsigned char *p, unsigned v)
{ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }
static unsigned get_word(const unsigned char *p)
{ return p[0] | (unsigned)p[1]<<8 | (unsigned)p[2]<<16 | (unsigned)p[3]<<24; }
static unsigned save_hash(const unsigned char *p, int n)
{
    unsigned hash = 2166136261u;
    int i;
    for (i = 0; i < n; ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}

int rf_story_save(struct rf_story *s, const char *path)
{
    unsigned char data[STORY_SAVE_WORDS * 4];
    unsigned values[STORY_SAVE_WORDS];
    char temp[512];
    int i, fd, written = 0;
    if (!s || !path || strlen(path) > sizeof(temp) - 6) return -1;
    memset(values, 0, sizeof(values));
    values[0]=0x54534652u; values[1]=1;
    values[2]=s->progress[0]; values[3]=s->progress[1];
    if(story_index(s->active_story)<2) { values[4]=s->active_story;values[5]=s->node_id; }
    values[6]=s->collapsed;
    values[7]=s->task.id; values[8]=s->task.state;
    for(i=0;i<s->queue_count && i<RF_STORY_QUEUE_CAP;++i)
        if(story_index(s->queue[i])<2)values[10+values[9]++]=s->queue[i];
    for(i=0;i<STORY_SAVE_WORDS-1;++i) put_word(data+i*4,values[i]);
    put_word(data+(STORY_SAVE_WORDS-1)*4,save_hash(data,(STORY_SAVE_WORDS-1)*4));
    snprintf(temp,sizeof(temp),"%s.tmp",path);
    fd=__openat(AT_FDCWD,temp,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0) goto failed;
    while(written<(int)sizeof(data)) {
        int n=(int)__write(fd,data+written,sizeof(data)-written);
        if(n<=0) { __close(fd); goto failed; }
        written+=n;
    }
    if(__close(fd)<0 || __rename(temp,path)<0) goto failed;
    s->dirty=0; s->persistence_error=0; return 0;
failed:
    s->persistence_error=1; return -1;
}

int rf_story_load(struct rf_story *s, const char *path)
{
    unsigned char data[STORY_SAVE_WORDS*4+1];
    unsigned v[STORY_SAVE_WORDS];
    struct rf_story loaded;
    int fd, count=0, i, task_state;
    if(!s || !path) return -1;
    /* Load is an init-time operation. A caller replacing live state must
     * detach first; refusing here prevents leaking an owned hold. */
    if(s->hold_token) return -1;
    fd=__openat(AT_FDCWD,path,O_RDONLY,0);
    if(fd<0) {
#ifdef TOYC_WINDOWS
        if(errno == ENOENT) return 1;
#else
        if(fd == -ENOENT) return 1;
#endif
        s->persistence_error=1;
        return -1;
    }
    while(count<(int)sizeof(data)) {
        int n=(int)__read(fd,data+count,sizeof(data)-count);
        if(n<0) { __close(fd); return -1; }
        if(!n) break;
        count+=n;
    }
    __close(fd);
    if(count!=STORY_SAVE_WORDS*4) return -1;
    for(i=0;i<STORY_SAVE_WORDS;++i) v[i]=get_word(data+i*4);
    if(v[0]!=0x54534652u || v[1]!=1 || v[19]!=save_hash(data,76) ||
        v[2]>RF_STORY_CANCELLED || v[3]>RF_STORY_CANCELLED || v[6]>1 ||
        v[9]>RF_STORY_QUEUE_CAP || v[8]>RF_STORY_TASK_CANCELLED ||
        (v[7] && (v[7]<RF_STORY_TASK_EXPLORE || v[7]>RF_STORY_TASK_BLUEPRINT))) return -1;
    if(v[4] && (story_index(v[4])<0 || !rf_story_find_node(v[5]) ||
        rf_story_find_node(v[5])->story_id!=(int)v[4])) return -1;
    if(!v[4] && v[5]) return -1;
    rf_story_init(&loaded);
    loaded.progress[0]=v[2]; loaded.progress[1]=v[3];
    loaded.active_story=v[4]; loaded.node_id=v[5]; loaded.collapsed=v[6];
    task_set(&loaded,v[7]); task_state=v[8]; loaded.task.state=task_state;
    for(i=0;i<(int)v[9];++i) {
        int j,index=story_index(v[10+i]);
        if(index<0 || loaded.progress[index]!=RF_STORY_QUEUED || v[10+i]==v[4]) return -1;
        for(j=0;j<i;++j) if(v[10+j]==v[10+i]) return -1;
        loaded.queue[loaded.queue_count++]=v[10+i];
        loaded.triggered_this_game |= 1u << index;
    }
    if(loaded.active_story) {
        int index=story_index(loaded.active_story);
        if(loaded.progress[index]!=RF_STORY_ACTIVE && loaded.progress[index]!=RF_STORY_INTERRUPTED) return -1;
        loaded.link=RF_STORY_LINK_CONNECTING; history_add(&loaded,-1);
        loaded.triggered_this_game |= 1u << index;
    }
    loaded.session_revision=s->session_revision+1;
    loaded.triggered_this_game |= s->triggered_this_game;
    loaded.node_revision=s->node_revision+1;
    loaded.dirty=0;
    *s=loaded;
    return 0;
}
