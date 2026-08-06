#ifndef RASTERFALL_NET_H
#define RASTERFALL_NET_H

#include "core.h"
#include "net.h"
#include "rasterfall_camera.h"
#include "rasterfall_session.h"

#define RASTERFALL_NET_DEFAULT_PORT 28460
#define RASTERFALL_NET_MAX_PACKET 1400
#define RASTERFALL_NET_PROTOCOL_VERSION 1
#define RASTERFALL_NET_PLAYER_MAX 2

enum rasterfall_net_mode {
    RASTERFALL_NET_OFF,
    RASTERFALL_NET_HOST,
    RASTERFALL_NET_CLIENT
};

enum rasterfall_net_packet_type {
    RASTERFALL_NET_HELLO = 1,
    RASTERFALL_NET_INPUT,
    RASTERFALL_NET_SNAPSHOT
};

struct rasterfall_net_player {
    int active;
    int id;
    struct camera camera;
    int hp;
    int weapon;
    int state;
};

struct rasterfall_net_enemy {
    int active, ai_state, hp;
    int x, z, speed;
    int bite_cooldown_ms, flash, hurt, dying_ms;
    int dir_x, dir_z;
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
    int peer_state_initialized;
    struct rasterfall_net_player players[RASTERFALL_NET_PLAYER_MAX];
    struct rasterfall_net_enemy enemies[TOY_GAME_MAX_ENEMIES];
    int enemy_count;
    int remote_event_count;
    unsigned char remote_events[TOY_GAME_MAX_EVENTS];
    int snapshot_world_wave;
    int snapshot_world_to_spawn;
    int snapshot_world_enemies_alive;
    int snapshot_world_phase;
    int snapshot_world_phase_timer_ms;
    int snapshot_world_alarm_timer_ms;
    int snapshot_ready;
    int connected;
    int rtt_ms;
    uint32_t last_command_sequence;
    long last_command_sent_ms;
    uint32_t last_snapshot_sequence;
    long last_snapshot_sent_ms;
    long last_receive_ms;
    long last_hello_ms;
};

void rasterfall_net_init(struct rasterfall_net *net);
int rasterfall_net_host(struct rasterfall_net *net, int port,
                        const struct camera *spawn);
int rasterfall_net_connect(struct rasterfall_net *net, const char *ip, int port);
void rasterfall_net_close(struct rasterfall_net *net);
void rasterfall_net_poll(struct rasterfall_net *net);
void rasterfall_net_update_connection(struct rasterfall_net *net);
int rasterfall_net_send_command(struct rasterfall_net *net,
                                const struct rasterfall_command *command,
                                const struct camera *predicted);
int rasterfall_net_send_snapshot(struct rasterfall_net *net,
                                 const struct camera *host_camera,
                                 const struct toy_game *game);
void rasterfall_net_apply_remote(struct rasterfall_net *net,
                                 struct rasterfall_session *session);
void rasterfall_net_reconcile_client(struct rasterfall_net *net,
                                     struct rasterfall_session *session,
                                     struct camera *camera);
int rasterfall_net_self_test(void);

#endif
