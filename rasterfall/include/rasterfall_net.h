#ifndef RASTERFALL_NET_H
#define RASTERFALL_NET_H

#include "core.h"
#include "net.h"
#include "rasterfall_camera.h"
#include "rasterfall_session.h"

#define RASTERFALL_NET_DEFAULT_PORT 28460
#define RASTERFALL_NET_MAX_PACKET 1200
#define RASTERFALL_NET_MAX_SNAPSHOT 8192
/* v30 uses one uniform host-side client layout.  Keep protocol
 * changes explicit: clients with a different snapshot layout must fail during
 * discovery/handshake instead of decoding shifted world data. */
#define RASTERFALL_NET_PROTOCOL_VERSION 35
#define RASTERFALL_NET_MAX_ACTORS 32
#define RASTERFALL_NET_PLAYER_MAX 4
#define RASTERFALL_NET_CLIENT_MAX (RASTERFALL_NET_PLAYER_MAX - 1)
#define RASTERFALL_NET_EVENT_QUEUE_MAX 64
#define RASTERFALL_NET_RELIABLE_EVENT_MAX 64
#define RASTERFALL_NET_INPUT_HISTORY 128
#define RASTERFALL_NET_INPUT_REDUNDANCY 3
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
    RASTERFALL_NET_AI_FIRE,
    RASTERFALL_NET_SNAPSHOT_PART,
    RASTERFALL_NET_RELIABLE_EVENT,
    RASTERFALL_NET_PLAYER_SNAPSHOT,
    RASTERFALL_NET_ENTITY_SNAPSHOT,
    RASTERFALL_NET_WORLD_SNAPSHOT,
    RASTERFALL_NET_PLAYER_FIRE
};

struct rasterfall_net_input {
    uint32_t sequence;
    uint32_t tick;
    struct rasterfall_command command;
    int jump_dx, jump_dz;
    int valid;
};

struct rasterfall_net_event {
    uint32_t id;
    int type;
    int source_id;
    int target_id;
    int x, z;
    int value;
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

struct rasterfall_snapshot_assembly {
    uint32_t sequence;
    int total_size;
    int part_count;
    unsigned int mask;
    unsigned char buffer[RASTERFALL_NET_MAX_SNAPSHOT];
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
    int downed;
    int revive_progress_ms;
    int current_slot;
    /* The active weapon is not enough to restore the inventory after a
     * network pickup: the other slot may have changed while it was inactive. */
    int slot_weapon[TOY_GAME_WEAPON_SLOTS];
    int mag[TOY_GAME_WEAPON_SLOTS];
    int reserve[TOY_GAME_WEAPON_SLOTS];
    int reloading;
    int reload_timer_ms;
    int weapon_switch_timer_ms;
    int muzzle_flash_ms;
    int kills;
    int special_kills;
    int damage_dealt;
    int throwable_damage_dealt;
    unsigned int fire_seq;
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];
    int airborne_ms;
    int airborne_y;
    int airborne_velocity;
    int air_x, air_z;
    uint32_t input_ack;
    int special_motion;
    struct toy_game_animation_state animation;
};

struct rasterfall_net_enemy {
    int index;
    int active, type, ai_state, hp;
    int x, z, speed;
    int bite_cooldown_ms, flash, hurt, dying_ms;
    int dir_x, dir_z;
    struct toy_game_enemy_ability_state ability;
    int airborne_ms;
    int airborne_y;
};

/* Keep the predicted position associated with each input until the host
 * acknowledges it.  Reconciliation must compare positions from the same
 * input point instead of pulling the current prediction toward an older
 * snapshot. */
struct rasterfall_net_prediction {
    uint32_t sequence;
    int x, z;
};

struct rasterfall_net_remote_sample {
    int valid;
    long received_ms;
    struct camera camera;
    int airborne_y;
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
    int kills;
    int special_kills;
    int damage_dealt;
    int throwable_damage_dealt;
    int hired;
    char name[TOY_GAME_MAX_NAME];
    int muzzle_flash_ms;
    int revive_progress_ms;
    unsigned int fire_seq;
    int weapon_switch_timer_ms;
    int airborne_ms;
    int airborne_y;
    struct toy_game_animation_state animation;
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];
};

/* Host-authoritative state for one network client.  Every client uses the
 * same slot and simulation path; player id is slot index + 1. */
struct rasterfall_net_client {
    int active;
    int client_id;
    int connected;
    struct sockaddr_in address;
    struct camera camera;
    struct camera spawn;
    struct camera reported_camera;
    int camera_initialized;
    int reported_camera_ready;
    struct rasterfall_command command;
    int input_jump_dx, input_jump_dz;
    int command_ready;
    uint32_t last_input_sequence;
    uint32_t last_processed_input_sequence;
    struct rasterfall_net_input input_queue[RASTERFALL_NET_INPUT_HISTORY];
    int input_queue_depth;
    int input_queue_max_depth;
    int input_gap_ticks;
    uint32_t last_input_tick;
    struct toy_game_slot slots[TOY_GAME_WEAPON_SLOTS];
    int current_slot;
    int hp;
    int state;
    int down;
    int revive_progress_ms;
    int local_revive_active;
    int local_revive_progress_ms;
    int revive_target_id;
    int ai_revive_active;
    int ai_revive_actor_index;
    int reloading, reload_timer_ms, weapon_switch_timer_ms;
    int fire_cooldown_ms, muzzle_flash_ms, damage_flash_ms;
    int kills;
    int special_kills;
    int damage_dealt;
    int throwable_damage_dealt;
    int rtt_ms;
    uint32_t stats_last_rx_sequence;
    unsigned long stats_rx_packets;
    unsigned long stats_lost_packets;
    int loss_permille;
    unsigned int fire_seq;
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];
    int airborne_ms, airborne_y, airborne_velocity;
    int air_x, air_z;
    struct toy_game_animation_state animation;
    uint32_t reliable_event_ack;
    unsigned int shop_request_id;
    long last_receive_ms;
};

struct rasterfall_net {
    int mode;
    int fd;
    /* Client-side transport endpoint.  Host-side clients live exclusively in
     * clients[] and never share this address/state. */
    struct sockaddr_in server_address;
    int server_known;
    struct camera client_spawn_base;
    uint32_t send_sequence;
    uint32_t receive_sequence;
    uint32_t tick;
    struct rasterfall_net_player players[RASTERFALL_NET_PLAYER_MAX];
    struct rasterfall_net_client clients[RASTERFALL_NET_CLIENT_MAX];
    int host_revive_active;
    int host_revive_target_id;
    int host_revive_progress_ms;
    int local_player_id;
    int spawn_pending;
    int world_ready;
    struct rasterfall_net_actor actors[RASTERFALL_NET_MAX_ACTORS];
    int actor_count;
    struct rasterfall_net_enemy enemies[TOY_GAME_MAX_ENEMIES];
    int enemy_count;
    /* Active-only entity snapshots use the chunk bitmap to distinguish a
     * missing UDP chunk from an entity that disappeared in a complete
     * snapshot. */
    uint64_t entity_actor_seen;
    uint64_t entity_enemy_seen;
    unsigned int entity_actor_parts_seen;
    unsigned int entity_enemy_parts_seen;
    int entity_actor_parts_total;
    int entity_enemy_parts_total;
    int entity_actor_complete;
    int entity_enemy_complete;
    int remote_event_count;
    unsigned char remote_events[TOY_GAME_MAX_EVENTS];
    uint32_t remote_event_last_id;
    uint32_t reliable_event_ack;
    struct rasterfall_net_event reliable_events[RASTERFALL_NET_RELIABLE_EVENT_MAX];
    int reliable_event_count;
    uint32_t reliable_event_next_id;
    int local_event_scan_count;
    long reliable_event_last_send_ms;
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
    int snapshot_world_campaign_stage;
    int snapshot_world_wave_attack_points;
    int snapshot_world_wave_attack_multiplier;
    int snapshot_world_wave_waiting_common;
    int snapshot_world_wave_waiting_fast;
    int snapshot_world_wave_waiting_heavy;
    int snapshot_world_wave_waiting_special;
    int snapshot_world_wave_waiting_tank;
    int snapshot_money;
    unsigned int snapshot_unlocked_weapons;
    int snapshot_flag_count;
    struct rasterfall_flag snapshot_flags[RASTERFALL_MAX_FLAGS];
    int snapshot_actor_flag_index[TOY_GAME_MAX_ACTORS];
    struct toy_game_projectile snapshot_projectiles[TOY_GAME_MAX_PROJECTILES];
    struct toy_game_burn_zone snapshot_burn_zones[TOY_CONFIG_MAX_BURN_ZONES];
    int snapshot_player_control_disabled;
    int snapshot_air_walls_enabled;
    int snapshot_manual_alarm_enabled;
    int snapshot_ready;
    uint32_t player_snapshot_sequence;
    uint32_t entity_snapshot_sequence;
    uint32_t world_snapshot_sequence;
    /* 快照在应用层分片，避免依赖 IP 分片；保留当前和上一代未完成组，
     * 允许轻微乱序补齐上一代快照。 */
    struct rasterfall_snapshot_assembly snapshot_current;
    struct rasterfall_snapshot_assembly snapshot_previous;
    int connected;
    int rtt_ms;
    uint32_t last_command_sequence;
    uint32_t last_jump_command_sequence;
    uint32_t last_snapshot_input_ack;
    struct rasterfall_net_prediction prediction_history[64];
    struct rasterfall_net_input input_history[RASTERFALL_NET_INPUT_HISTORY];
    struct rasterfall_net_remote_sample remote_samples[RASTERFALL_NET_PLAYER_MAX][3];
    int remote_sample_count[RASTERFALL_NET_PLAYER_MAX];
    struct camera remote_render_camera[RASTERFALL_NET_PLAYER_MAX];
    int remote_render_airborne_y[RASTERFALL_NET_PLAYER_MAX];
    int correction_x, correction_z, correction_y;
    int correction_remaining_ms;
    long last_command_sent_ms;
    unsigned int shop_request_next_id;
    unsigned int pending_shop_request_id;
    int pending_shop_action;
    int pending_shop_item;
    int pending_shop_arg;
    unsigned int pending_action_buttons;
    long pending_shop_until_ms;
    uint32_t last_snapshot_sequence;
    unsigned int ai_fire_sent_seq[TOY_GAME_MAX_ACTORS];
    unsigned int player_fire_sent_seq[RASTERFALL_NET_CLIENT_MAX];
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
    /* Rolling transport diagnostics.  Loss is estimated from gaps in the
     * monotonic packet sequence received from the current server. */
    long net_stats_window_start_ms;
    unsigned long net_stats_tx_bytes;
    unsigned long net_stats_rx_bytes;
    unsigned long net_stats_tx_packets;
    unsigned long net_stats_rx_packets;
    unsigned long net_stats_lost_packets;
    uint32_t net_stats_last_rx_sequence;
    int net_stats_have_rx_sequence;
    int net_stats_tx_bps;
    int net_stats_rx_bps;
    int net_stats_loss_permille;
    int net_stats_avg_rtt_ms;
    long net_stats_rtt_sum_ms;
    int net_stats_rtt_samples;
    /* Snapshot-fragment statistics are a separate signal from packet
     * sequence gaps: all fragments of one snapshot intentionally share a
     * sequence number.  These are rolling one-second counters. */
    int snapshot_parts_received;
    int snapshot_parts_missing;
    int snapshot_parts_duplicate;
    int snapshot_completed;
    int snapshot_abandoned;
    unsigned long input_packets_sent, input_packets_received;
    unsigned long input_entries_received, input_duplicates;
    unsigned long input_out_of_order, input_recovered;
    unsigned long input_synthesized;
    unsigned long player_snapshots_received, entity_snapshots_received;
    unsigned long world_snapshots_received;
    unsigned long reconciliation_count, reconciliation_total;
    unsigned long reconciliation_max;
    unsigned long reconciliation_hard_snaps;
    unsigned long interpolation_underruns, extrapolation_count;
    int net_loss_percent;
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
int rasterfall_net_discovery_test(void);
void rasterfall_net_poll(struct rasterfall_net *net);
void rasterfall_net_update_connection(struct rasterfall_net *net);
/* Used by --logic-test to exercise the fixed-size snapshot assembler without
 * changing the wire format or opening a socket. */
int rasterfall_net_snapshot_fragment_test(void);
int rasterfall_net_client_slot_test(void);
int rasterfall_net_pipeline_test(void);
int rasterfall_net_send_command(struct rasterfall_net *net,
                                const struct rasterfall_command *command,
                                const struct camera *predicted,
                                int jump_dx, int jump_dz);
int rasterfall_net_send_snapshot(struct rasterfall_net *net,
                                 const struct rasterfall_session *session,
                                 const struct camera *host_camera,
                                 const struct toy_game *game,
                                 int air_walls_enabled,
                                 int manual_alarm_enabled,
                                 int manual_alarm_timer_ms);
void rasterfall_net_apply_clients(struct rasterfall_net *net,
                                  struct rasterfall_session *session,
                                  struct camera *host_camera);
void rasterfall_net_sync_clients(struct rasterfall_net *net,
                                 struct toy_game *game);
void rasterfall_net_prepare_host_step(struct rasterfall_net *net,
                                      struct toy_game *game);
void rasterfall_net_reconcile_client(struct rasterfall_net *net,
                                     struct rasterfall_session *session,
                                     struct camera *camera);
void rasterfall_net_update_presentation(struct rasterfall_net *net, int dt_ms);
const struct camera *rasterfall_net_remote_render_camera(
    const struct rasterfall_net *net, int player_id, int *airborne_y);
void rasterfall_net_set_loss(struct rasterfall_net *net, int percent);
void rasterfall_net_apply_local_rescue(struct rasterfall_net *net,
                                       struct rasterfall_session *session,
                                       const struct camera *host_camera,
                                       int interact_pressed, int dt_ms);
void rasterfall_net_reset_host(struct rasterfall_net *net);
void rasterfall_net_capture_events(struct rasterfall_net *net,
                                   const struct toy_game *game);

#endif
