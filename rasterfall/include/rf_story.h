#ifndef RF_STORY_H
#define RF_STORY_H

#include "tlibc_types.h"
#include "rasterfall_camera.h"

struct rasterfall_session;

/* These IDs are persisted. Never renumber existing content. */
enum rf_story_id { RF_STORY_NONE, RF_STORY_OUTPOST = 101, RF_STORY_LABS = 102 };
enum rf_story_progress { RF_STORY_UNSEEN, RF_STORY_QUEUED, RF_STORY_ACTIVE,
    RF_STORY_INTERRUPTED, RF_STORY_COMPLETED, RF_STORY_CANCELLED };
enum rf_story_trigger_policy { RF_STORY_TRIGGER_EACH_ENTRY, RF_STORY_TRIGGER_ONCE,
    RF_STORY_TRIGGER_EACH_GAME };
int rf_story_trigger_policy(int story_id);
enum rf_story_task_id { RF_STORY_TASK_NONE, RF_STORY_TASK_EXPLORE = 201,
    RF_STORY_TASK_LABS = 202, RF_STORY_TASK_WEAVER = 203,
    RF_STORY_TASK_BLUEPRINT = 204 };
enum rf_story_task_state { RF_STORY_TASK_INACTIVE, RF_STORY_TASK_RUNNING,
    RF_STORY_TASK_DONE, RF_STORY_TASK_FAILED, RF_STORY_TASK_CANCELLED };
enum rf_story_event { RF_STORY_EVENT_OUTPOST_ENTER = 1, RF_STORY_EVENT_LABS_ENTER,
    RF_STORY_EVENT_WEAVER_OPEN, RF_STORY_EVENT_BLUEPRINT_VIEW,
    RF_STORY_EVENT_DEVICE_LOST };
enum rf_story_link { RF_STORY_LINK_OFF, RF_STORY_LINK_CONNECTING,
    RF_STORY_LINK_LIVE, RF_STORY_LINK_UNAVAILABLE, RF_STORY_LINK_INTERRUPTED };

#define RF_STORY_CHOICES 3
#define RF_STORY_HISTORY_CAP 32
#define RF_STORY_QUEUE_CAP 8

struct rf_story_choice {
    const char *id, *text;
    int next_node, event, task;
};
struct rf_story_node {
    int id, story_id, priority;
    const char *speaker_id, *speaker, *location, *line, *camera_id;
    int action, condition, choice_count;
    struct rf_story_choice choices[RF_STORY_CHOICES];
    int auto_next_node, auto_task;
};
struct rf_story_history_entry { int story_id, node_id, choice; unsigned revision; };
struct rf_story_task {
    int id, state;
    const char *title, *hint;
    char target_id[64];
    int target_valid, x, y, z;
};

/* A real session entity: the visible shell and video both consume view.
 * This transform stays fixed until the entity is explicitly recreated. */
struct rf_story_camera_entity {
    int active;
    const char *stable_id;
    unsigned generation, actor_generation;
    uint64_t world_generation;
    int actor_index, actor_id;
    struct camera view;
    int near_rfu, fov_degrees, shell_radius;
    int anchor_x, anchor_z;
};
struct rf_story {
    int progress[2], queue[RF_STORY_QUEUE_CAP], queue_count;
    int active_story, node_id, collapsed, link, enabled, combat;
    unsigned session_revision, node_revision, next_camera_generation;
    uint64_t world_generation;
    int actor_index, actor_id, last_hp, retry_ms;
    unsigned actor_generation, hold_token;
    struct rf_story_camera_entity camera;
    struct rf_story_task task;
    struct rf_story_history_entry history[RF_STORY_HISTORY_CAP];
    int history_count, dirty, persistence_error;
    unsigned history_revision, region_presence;
    unsigned triggered_this_game;
    int line_elapsed_ms;
};

void rf_story_init(struct rf_story *story);
/* Call before unloading/resetting the session and during shutdown. Keeps
 * story progress; releases only the exact actor generation owned by us. */
void rf_story_detach(struct rf_story *story, struct rasterfall_session *session);
/* Single-player authoritative update; allow_start=0 preserves active dialogue
 * while delaying new calls during menus, terminal typing, or combat. */
void rf_story_update(struct rf_story *story, struct rasterfall_session *session,
    int dt_ms, int combat, int allow_start);
void rf_story_emit(struct rf_story *story, int event, const char *target_id);
const struct rf_story_node *rf_story_current_node(const struct rf_story *story);
const struct rf_story_node *rf_story_find_node(int node_id);
int rf_story_line_duration_ms(const char *line);
/* choice=-1 confirms a terminal line. Both revisions must match the snapshot
 * that produced the input; a stale/double answer never advances a new node. */
int rf_story_answer(struct rf_story *story, struct rasterfall_session *session,
    unsigned session_revision, unsigned node_revision, int choice);
void rf_story_collapse(struct rf_story *story, int collapsed);
void rf_story_close(struct rf_story *story, struct rasterfall_session *session);
int rf_story_replay(struct rf_story *story, struct rasterfall_session *session,
    int story_id);
int rf_story_reset(struct rf_story *story, struct rasterfall_session *session,
    int story_id);
const struct rf_story_camera_entity *rf_story_camera(const struct rf_story *story,
    const struct rasterfall_session *session);
/* Versioned small progress save: stable IDs only, never actor slots/pointers.
 * On load active nodes reconnect through the normal entity resolver. */
int rf_story_save(struct rf_story *story, const char *path);
int rf_story_load(struct rf_story *story, const char *path);
int rf_story_logic_test(void);
/* Shared cinematic framing, without acquiring a story movement/facing hold. */
struct toy_game_actor;
int rf_story_actor_camera(const struct rasterfall_session *session,
    const struct toy_game_actor *actor,struct camera *camera);

#endif
