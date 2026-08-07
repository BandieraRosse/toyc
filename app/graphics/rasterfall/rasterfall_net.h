#ifndef RASTERFALL_NET_H
#define RASTERFALL_NET_H

#include "core.h"
#include "net.h"
#include "rasterfall_camera.h"
#include "rasterfall_session.h"

#define RASTERFALL_NET_DEFAULT_PORT 28460
#define RASTERFALL_NET_MAX_PACKET 1400
#define RASTERFALL_NET_PROTOCOL_VERSION 7
#define RASTERFALL_NET_MAX_ACTORS 3
#define RASTERFALL_NET_PLAYER_MAX 2
#define RASTERFALL_NET_EVENT_QUEUE_MAX 64
#define RASTERFALL_NET_DISCOVERY_PORT 28459
#define RASTERFALL_NET_DISCOVERY_MAX_ROOMS 8
#define RASTERFALL_NET_PUNCH_SERVER "47.82.117.182"
#define RASTERFALL_NET_PUNCH_PORT 28461

enum rasterfall_net_mode {
    RASTERFALL_NET_OFF,
    RASTERFALL_NET_HOST,
    RASTERFALL_NET_CLIENT
};

enum rasterfall_net_packet_type {
    RASTERFALL_NET_HELLO = 1,
    RASTERFALL_NET_INPUT,
    RASTERFALL_NET_SNAPSHOT,
    RASTERFALL_NET_AI_FIRE
};

struct rasterfall_net_room {
    int active;
    struct sockaddr_in address;
    char name[32];
    int game_port;
    int players;
    int max_players;
    int state;
    long last_seen_ms;
};

struct rasterfall_net_discovery {
    int fd;
    int mode;
    long last_query_ms;
    int room_count;
    struct rasterfall_net_room rooms[RASTERFALL_NET_DISCOVERY_MAX_ROOMS];
};

enum rasterfall_net_discovery_mode {
    RASTERFALL_NET_DISCOVERY_OFF,
    RASTERFALL_NET_DISCOVERY_BROWSER,
    RASTERFALL_NET_DISCOVERY_HOST
};

struct rasterfall_net_player {
    int active;
    int id;
    struct camera camera;
    int hp;
    int weapon;
    int state;
    int current_slot;
    /* The active weapon is not enough to restore the inventory after a
     * remote pickup: the other slot may have changed while it was inactive. */
    int slot_weapon[TOY_GAME_WEAPON_SLOTS];
    int mag[TOY_GAME_WEAPON_SLOTS];
    int reserve[TOY_GAME_WEAPON_SLOTS];
    int reloading;
    int reload_timer_ms;
    int muzzle_flash_ms;
    int kills;
    unsigned int fire_seq;
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];
};

struct rasterfall_net_enemy {
    int active, type, ai_state, hp;
    int x, z, speed;
    int bite_cooldown_ms, flash, hurt, dying_ms;
    int dir_x, dir_z;
};

struct rasterfall_net_actor {
    int active;
    int actor_index;
    int class_id;
    int state;
    int x, z;
    int sy, cy;
    int hp;
    int weapon;
    int muzzle_flash_ms;
    int revive_progress_ms;
    unsigned int fire_seq;
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];
};

struct rasterfall_net {
    int mode;
    int fd;
    struct sockaddr_in peer;
    int peer_known;
    uint32_t send_sequence;
    uint32_t receive_sequence;
    uint32_t tick;
    uint32_t last_input_sequence;
    uint32_t last_input_tick;
    struct rasterfall_command remote_command;
    int remote_command_ready;
    struct camera peer_camera;
    struct camera peer_spawn;
    struct camera peer_reported_camera;
    int peer_camera_initialized;
    int peer_reported_camera_ready;
    /* 主机为第二名玩家保留独立的武器状态；敌人和地图仍由主机唯一推进。 */
    struct toy_game_slot peer_slots[TOY_GAME_WEAPON_SLOTS];
    int peer_current_slot;
    int peer_hp;
    int peer_state;
    int peer_reloading;
    int peer_reload_timer_ms;
    int peer_fire_cooldown_ms;
    int peer_muzzle_flash_ms;
    int peer_damage_flash_ms;
    int peer_kills;
    unsigned int peer_fire_seq;
    int peer_ray_count;
    struct toy_game_ray peer_rays[TOY_GAME_MAX_RAYS];
    int peer_state_initialized;
    struct rasterfall_net_player players[RASTERFALL_NET_PLAYER_MAX];
    struct rasterfall_net_actor actors[RASTERFALL_NET_MAX_ACTORS];
    int actor_count;
    struct rasterfall_net_enemy enemies[TOY_GAME_MAX_ENEMIES];
    int enemy_count;
    int remote_event_count;
    unsigned char remote_events[TOY_GAME_MAX_EVENTS];
    unsigned char remote_event_queue[RASTERFALL_NET_EVENT_QUEUE_MAX];
    uint32_t remote_event_ids[RASTERFALL_NET_EVENT_QUEUE_MAX];
    int remote_event_queue_count;
    uint32_t remote_event_next_id;
    uint32_t remote_event_last_id;
    uint32_t remote_event_snapshot_last_id;
    uint32_t remote_event_snapshot_sequence;
    int snapshot_world_wave;
    int snapshot_world_to_spawn;
    int snapshot_world_spawn_timer_ms;
    int snapshot_world_enemies_alive;
    int snapshot_world_phase;
    int snapshot_world_phase_timer_ms;
    int snapshot_world_alarm_timer_ms;
    int snapshot_world_spawn_budget;
    int snapshot_world_active_attackers;
    int snapshot_world_director_encounters;
    int snapshot_world_goal_hold_ms;
    int snapshot_world_manual_alarm_timer_ms;
    int snapshot_world_alarm_triggered;
    int snapshot_air_walls_enabled;
    int snapshot_manual_alarm_enabled;
    int snapshot_ready;
    int connected;
    int rtt_ms;
    uint32_t last_command_sequence;
    long last_command_sent_ms;
    uint32_t last_snapshot_sequence;
    unsigned int ai_fire_sent_seq[TOY_GAME_MAX_ACTORS];
    long last_snapshot_sent_ms;
    long last_receive_ms;
    long last_hello_ms;
    int public_room;
    int relay_mode;
    int public_room_id;
    uint32_t public_nonce;
    uint32_t public_token;
    int public_matched;
    struct sockaddr_in public_server;
    long last_public_register_ms;
    long last_public_punch_ms;
};

void rasterfall_net_init(struct rasterfall_net *net);
int rasterfall_net_host(struct rasterfall_net *net, int port,
                        const struct camera *spawn);
int rasterfall_net_connect(struct rasterfall_net *net, const char *ip, int port);
int rasterfall_net_public_host(struct rasterfall_net *net, int room_id,
                               const struct camera *spawn);
int rasterfall_net_public_connect(struct rasterfall_net *net, int room_id);
int rasterfall_net_local_address(char *buffer, int buffer_size);
void rasterfall_net_close(struct rasterfall_net *net);
void rasterfall_net_discovery_init(struct rasterfall_net_discovery *discovery);
int rasterfall_net_discovery_browser_start(struct rasterfall_net_discovery *discovery);
int rasterfall_net_discovery_host_start(struct rasterfall_net_discovery *discovery);
void rasterfall_net_discovery_close(struct rasterfall_net_discovery *discovery);
void rasterfall_net_discovery_poll(struct rasterfall_net_discovery *discovery,
                                   const char *room_name, int game_port,
                                   int players, int max_players, int state);
void rasterfall_net_poll(struct rasterfall_net *net);
void rasterfall_net_update_connection(struct rasterfall_net *net);
int rasterfall_net_send_command(struct rasterfall_net *net,
                                const struct rasterfall_command *command,
                                const struct camera *predicted);
int rasterfall_net_send_snapshot(struct rasterfall_net *net,
                                 const struct camera *host_camera,
                                 const struct toy_game *game,
                                 int air_walls_enabled,
                                 int manual_alarm_enabled,
                                 int manual_alarm_timer_ms);
void rasterfall_net_apply_remote(struct rasterfall_net *net,
                                 struct rasterfall_session *session,
                                 struct camera *host_camera);
void rasterfall_net_reconcile_client(struct rasterfall_net *net,
                                     struct rasterfall_session *session,
                                     struct camera *camera);
int rasterfall_net_self_test(void);

#endif
