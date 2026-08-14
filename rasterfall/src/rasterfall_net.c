#include "tlibc_everything.h"
#include "errno.h"
#include "rasterfall_net.h"

#ifdef TOYC_WINDOWS
#define net_windows_log toy_windows_log
#else
static void net_windows_log(const char *message) { (void)message; }
#endif


#define NET_HEADER_SIZE 16
#define NET_MAGIC_0 'R'
#define NET_MAGIC_1 'F'
#define NET_MAGIC_2 'N'
#define NET_MAGIC_3 '1'
#define NET_INPUT_ENTRY_SIZE 28
#define NET_INPUT_META_SIZE 24
#define NET_INPUT_SIZE (NET_INPUT_META_SIZE + \
                        RASTERFALL_NET_INPUT_REDUNDANCY * NET_INPUT_ENTRY_SIZE)
#define NET_PLAYER_BASE_SIZE 85
#define NET_PLAYER_RAY_SIZE 15
#define NET_PLAYER_SIZE (NET_PLAYER_BASE_SIZE + 4 + 1 + TOY_GAME_MAX_RAYS * NET_PLAYER_RAY_SIZE)
#define NET_ACTOR_SIZE (42 + TOY_GAME_MAX_NAME)
#define NET_ENEMY_SIZE 48
#define NET_WORLD_BASE_SIZE 44
#define NET_WORLD_FLAG_SIZE 12
#define NET_WORLD_ENTITY_BASE (NET_WORLD_BASE_SIZE + 4 + 4 + \
                        RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + \
                        TOY_GAME_MAX_ACTORS * 2)
#define NET_WORLD_PROJECTILE_SIZE 32
#define NET_WORLD_BURN_ZONE_SIZE 20
#define NET_WORLD_SIZE (NET_WORLD_ENTITY_BASE + \
                        TOY_GAME_MAX_PROJECTILES * NET_WORLD_PROJECTILE_SIZE + \
                        TOY_CONFIG_MAX_BURN_ZONES * NET_WORLD_BURN_ZONE_SIZE)
#define NET_SNAPSHOT_BASE 8
#define NET_INPUT_HOLD_TICKS 15
#define NET_INTERPOLATION_DELAY_MS 100
#define NET_EXTRAPOLATION_LIMIT_MS 75
#define NET_AI_FIRE_BASE 6
#define NET_SNAPSHOT_PART_BASE 10
#define NET_SNAPSHOT_FRAGMENT_DATA 1000
#define NET_RELIABLE_EVENT_BASE 5
#define NET_RELIABLE_EVENT_SIZE 13

static int packet_begin(unsigned char *packet, int type, int payload_size,
                        uint32_t sequence, uint32_t ack);
static void put_u32(unsigned char *p, uint32_t value);
static void put_i16(unsigned char *p, int value);
static unsigned char put_i8_value(int value);
static uint32_t get_u32(const unsigned char *p);
static int net_send(struct rasterfall_net *net, unsigned char *packet, int size);
static int net_send_to(struct rasterfall_net *net,
                       const struct sockaddr_in *address,
                       unsigned char *packet, int size);
static int net_send_clients(struct rasterfall_net *net,
                            unsigned char *packet, int size);

static long net_monotonic_ms(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static void net_stats_roll(struct rasterfall_net *net)
{
    long now, elapsed;
    if (!net) return;
    now = net_monotonic_ms();
    if (!net->net_stats_window_start_ms)
        net->net_stats_window_start_ms = now;
    elapsed = now - net->net_stats_window_start_ms;
    if (elapsed < 1000) return;
    net->net_stats_tx_bps = (int)(net->net_stats_tx_bytes * 1000 / elapsed);
    net->net_stats_rx_bps = (int)(net->net_stats_rx_bytes * 1000 / elapsed);
    if (net->net_stats_rx_packets + net->net_stats_lost_packets > 0)
        net->net_stats_loss_permille = (int)
            (net->net_stats_lost_packets * 1000 /
             (net->net_stats_rx_packets + net->net_stats_lost_packets));
    if (net->net_stats_rtt_samples > 0)
        net->net_stats_avg_rtt_ms = (int)(net->net_stats_rtt_sum_ms /
                                          net->net_stats_rtt_samples);
    net->net_stats_window_start_ms = now;
    net->net_stats_tx_bytes = 0;
    net->net_stats_rx_bytes = 0;
    net->net_stats_tx_packets = 0;
    net->net_stats_rx_packets = 0;
    net->net_stats_lost_packets = 0;
    net->net_stats_rtt_sum_ms = 0;
    net->net_stats_rtt_samples = 0;
    net->snapshot_parts_received = 0;
    net->snapshot_parts_missing = 0;
    net->snapshot_parts_duplicate = 0;
    net->snapshot_completed = 0;
    net->snapshot_abandoned = 0;
}

static void net_stats_note_rtt(struct rasterfall_net *net, long elapsed)
{
    if (!net || elapsed < 0 || elapsed >= 60000) return;
    net->rtt_ms = (int)elapsed;
    net->net_stats_rtt_sum_ms += elapsed;
    net->net_stats_rtt_samples++;
    net_stats_roll(net);
}

static void net_stats_note_sequence(struct rasterfall_net *net,
                                     uint32_t sequence)
{
    if (!net || !sequence) return;
    if (net->net_stats_have_rx_sequence &&
        sequence > net->net_stats_last_rx_sequence + 1)
        net->net_stats_lost_packets +=
            sequence - net->net_stats_last_rx_sequence - 1;
    if (!net->net_stats_have_rx_sequence ||
        sequence > net->net_stats_last_rx_sequence) {
        net->net_stats_last_rx_sequence = sequence;
        net->net_stats_have_rx_sequence = 1;
    }
}

static void net_client_note_sequence(struct rasterfall_net_client *client,
                                     uint32_t sequence)
{
    if (!client || !sequence) return;
    if (client->stats_last_rx_sequence &&
        sequence > client->stats_last_rx_sequence + 1)
        client->stats_lost_packets +=
            sequence - client->stats_last_rx_sequence - 1;
    if (!client->stats_last_rx_sequence ||
        sequence > client->stats_last_rx_sequence) {
        client->stats_last_rx_sequence = sequence;
        client->stats_rx_packets++;
    }
    if (client->stats_rx_packets + client->stats_lost_packets)
        client->loss_permille = (int)(client->stats_lost_packets * 1000 /
            (client->stats_rx_packets + client->stats_lost_packets));
}

static void net_push_event(struct toy_game *game, unsigned char event)
{
    if (game->event_count < TOY_GAME_MAX_EVENTS)
        game->events[game->event_count++] = event;
}

static void net_queue_remote_events(struct rasterfall_net *net,
                                    const unsigned char *events, int count,
                                    int source_id, int x, int z)
{
    int i;
    for (i = 0; i < count; i++) {
        if (net->reliable_event_count < RASTERFALL_NET_RELIABLE_EVENT_MAX) {
            struct rasterfall_net_event *event =
                &net->reliable_events[net->reliable_event_count++];
            event->id = ++net->reliable_event_next_id;
            event->type = events[i];
            event->source_id = source_id;
            event->target_id = -1;
            event->x = x;
            event->z = z;
            event->value = 0;
        }
    }
}

static void net_drop_reliable_events(struct rasterfall_net *net)
{
    uint32_t minimum = 0xffffffffU;
    int i, remove_count = 0;
    int consumers = 0;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *client = &net->clients[i];
        if (!client->active || !client->connected) continue;
        if (!consumers || client->reliable_event_ack < minimum)
            minimum = client->reliable_event_ack;
        consumers = 1;
    }
    if (!consumers) {
        net->reliable_event_count = 0;
        return;
    }
    while (remove_count < net->reliable_event_count &&
           net->reliable_events[remove_count].id <= minimum)
        remove_count++;
    if (!remove_count) return;
    if (remove_count < net->reliable_event_count)
        memmove(net->reliable_events,
                net->reliable_events + remove_count,
                (size_t)(net->reliable_event_count - remove_count) *
                    sizeof(net->reliable_events[0]));
    net->reliable_event_count -= remove_count;
}

static int net_send_reliable_events(struct rasterfall_net *net)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET];
    int count, size, i;
    long now;
    if (!net || net->mode != RASTERFALL_NET_HOST ||
        net->reliable_event_count <= 0) return 0;
    now = net_monotonic_ms();
    if (net->reliable_event_last_send_ms &&
        now - net->reliable_event_last_send_ms < 100) return 0;
    count = net->reliable_event_count;
    if (count > TOY_GAME_MAX_EVENTS) count = TOY_GAME_MAX_EVENTS;
    size = packet_begin(packet, RASTERFALL_NET_RELIABLE_EVENT,
                        NET_RELIABLE_EVENT_BASE + count * NET_RELIABLE_EVENT_SIZE,
                        ++net->send_sequence, net->receive_sequence);
    if (size < 0) return -1;
    put_u32(packet + NET_HEADER_SIZE, net->reliable_events[0].id);
    packet[NET_HEADER_SIZE + 4] = (unsigned char)count;
    for (i = 0; i < count; i++) {
        const struct rasterfall_net_event *event = &net->reliable_events[i];
        unsigned char *p = packet + NET_HEADER_SIZE + NET_RELIABLE_EVENT_BASE +
                           i * NET_RELIABLE_EVENT_SIZE;
        p[0] = (unsigned char)event->type;
        p[1] = put_i8_value(event->source_id);
        p[2] = put_i8_value(event->target_id);
        p[3] = 0;
        put_i16(p + 4, event->x);
        put_i16(p + 6, event->z);
        put_i16(p + 8, event->value);
        put_u32(p + 9, event->id);
    }
    if (net_send_clients(net, packet, size) < 0) return -1;
    net->reliable_event_last_send_ms = now;
    return 0;
}

static int decode_reliable_events(const unsigned char *payload, int size,
                                  struct rasterfall_net *net)
{
    int count, i;
    uint32_t first_id;
    if (size < NET_RELIABLE_EVENT_BASE) return -1;
    first_id = get_u32(payload);
    count = payload[4];
    if (count < 0 || count > TOY_GAME_MAX_EVENTS ||
        size != NET_RELIABLE_EVENT_BASE + count * NET_RELIABLE_EVENT_SIZE)
        return -1;
    net->remote_event_count = 0;
    for (i = 0; i < count; i++) {
        const unsigned char *p = payload + NET_RELIABLE_EVENT_BASE +
                                 i * NET_RELIABLE_EVENT_SIZE;
        uint32_t id = get_u32(p + 9);
        if (id != first_id + (uint32_t)i) return -1;
        if (id <= net->remote_event_last_id) continue;
        if (id != net->remote_event_last_id + 1) break;
        if (net->remote_event_count < TOY_GAME_MAX_EVENTS)
            net->remote_events[net->remote_event_count++] = p[0];
        net->remote_event_last_id = id;
    }
    net->reliable_event_ack = net->remote_event_last_id;
    return 0;
}

void rasterfall_net_capture_events(struct rasterfall_net *net,
                                   const struct toy_game *game)
{
    int i;
    if (!net || !game || net->mode != RASTERFALL_NET_HOST) return;
    if (game->event_count < net->local_event_scan_count)
        net->local_event_scan_count = 0;
    for (i = net->local_event_scan_count; i < game->event_count; i++) {
        if (net->reliable_event_count >= RASTERFALL_NET_RELIABLE_EVENT_MAX)
            break;
        net->reliable_events[net->reliable_event_count].id =
            ++net->reliable_event_next_id;
        net->reliable_events[net->reliable_event_count].type = game->events[i];
        net->reliable_events[net->reliable_event_count].source_id = 0;
        net->reliable_events[net->reliable_event_count].target_id = -1;
        net->reliable_events[net->reliable_event_count].x = game->px;
        net->reliable_events[net->reliable_event_count].z = game->pz;
        net->reliable_events[net->reliable_event_count].value =
            game->current_slot;
        net->reliable_event_count++;
        net->local_event_scan_count = i + 1;
    }
}

static void put_u16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

static void put_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static unsigned int get_u16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

static uint32_t get_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int snapshot_popcount(unsigned int mask)
{
    int count = 0;
    while (mask) {
        mask &= mask - 1;
        count++;
    }
    return count;
}

static void put_i16(unsigned char *p, int value)
{
    put_u16(p, (unsigned int)(unsigned short)value);
}

static int get_i16(const unsigned char *p)
{
    unsigned int value = get_u16(p);
    return value & 0x8000 ? (int)value - 65536 : (int)value;
}

static unsigned char put_i8_value(int value)
{
    return (unsigned char)(value < 0 ? value + 256 : value);
}

static int get_i8_value(unsigned char value)
{
    return value >= 128 ? (int)value - 256 : (int)value;
}

static int sequence_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static int sequence_before_or_equal(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

static uint32_t net_loss_random = 0x72f31a5dU;

static int net_simulated_drop(struct rasterfall_net *net, int type)
{
    if (!net || net->net_loss_percent <= 0 ||
        type <= 0 || type == RASTERFALL_NET_HELLO) return 0;
    net_loss_random = net_loss_random * 1664525U + 1013904223U;
    return (int)((net_loss_random >> 16) % 100U) < net->net_loss_percent;
}

static int packet_type_of(const unsigned char *packet, int size)
{
    return packet && size >= NET_HEADER_SIZE ? packet[5] : 0;
}

/* Weapon slot -1 (empty) must survive the byte-packed snapshot. */
static unsigned char put_weapon_value(int weapon)
{
    int content_id = toy_game_weapon_content_id(weapon);
    return (unsigned char)(content_id < 0 ? 0 : content_id);
}

static int get_weapon_value(unsigned char value)
{
    if (value == 0) return -1;
    return toy_game_weapon_from_content_id((int)value);
}

static int packet_begin(unsigned char *packet, int type, int payload_size,
                        uint32_t sequence, uint32_t ack)
{
    if (payload_size < 0 || payload_size > RASTERFALL_NET_MAX_PACKET - NET_HEADER_SIZE)
        return -1;
    packet[0] = NET_MAGIC_0;
    packet[1] = NET_MAGIC_1;
    packet[2] = NET_MAGIC_2;
    packet[3] = NET_MAGIC_3;
    packet[4] = RASTERFALL_NET_PROTOCOL_VERSION;
    packet[5] = (unsigned char)type;
    put_u16(packet + 6, (unsigned int)payload_size);
    put_u32(packet + 8, sequence);
    put_u32(packet + 12, ack);
    return NET_HEADER_SIZE + payload_size;
}

static int packet_header(const unsigned char *packet, int size, int *type,
                         int *payload_size, uint32_t *sequence,
                         uint32_t *ack)
{
    int payload;
    if (size < NET_HEADER_SIZE || packet[0] != NET_MAGIC_0 ||
        packet[1] != NET_MAGIC_1 || packet[2] != NET_MAGIC_2 ||
        packet[3] != NET_MAGIC_3 ||
        packet[4] != RASTERFALL_NET_PROTOCOL_VERSION) return -1;
    payload = (int)get_u16(packet + 6);
    if (payload != size - NET_HEADER_SIZE) return -1;
    *type = packet[5];
    *payload_size = payload;
    *sequence = get_u32(packet + 8);
    if (ack) *ack = get_u32(packet + 12);
    return 0;
}

static int same_address(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family && a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static int net_send(struct rasterfall_net *net, unsigned char *packet, int size)
{
    long sent;
    if (net->fd < 0 || !net->server_known || size <= 0 ||
        size > RASTERFALL_NET_MAX_PACKET) return -1;
    if (net_simulated_drop(net, packet_type_of(packet, size))) return 0;
    sent = sendto(net->fd, packet, (size_t)size, 0,
                  (const struct sockaddr *)&net->server_address, sizeof(net->server_address));
    if (sent == size) {
        net->net_stats_tx_bytes += (unsigned long)size;
        net->net_stats_tx_packets++;
        net_stats_roll(net);
    }
    return sent == size ? 0 : -1;
}

static int net_send_clients(struct rasterfall_net *net,
                            unsigned char *packet, int size)
{
    int i;
    if (net->relay_mode) return net_send(net, packet, size);
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
        if (net->clients[i].active && net->clients[i].connected &&
            net_send_to(net, &net->clients[i].address, packet, size) < 0)
            return -1;
    return 0;
}

static int net_send_to(struct rasterfall_net *net,
                       const struct sockaddr_in *address,
                       unsigned char *packet, int size)
{
    long sent;
    if (!net || !address || net->fd < 0 || size <= 0 ||
        size > RASTERFALL_NET_MAX_PACKET) return -1;
    if (net_simulated_drop(net, packet_type_of(packet, size))) return 0;
    sent = sendto(net->fd, packet, (size_t)size, 0,
                  (const struct sockaddr *)address, sizeof(*address));
    if (sent == size) {
        net->net_stats_tx_bytes += (unsigned long)size;
        net->net_stats_tx_packets++;
        net_stats_roll(net);
    }
    return sent == size ? 0 : -1;
}

/* A paid revive is a player action, not a rescue interaction.  The client
 * predicts it for responsiveness, but the shared money balance and the
 * remote player's downed state must be changed here on the host. */
static int net_paid_revive_client(struct rasterfall_net *net,
                                  struct rasterfall_session *session,
                                  struct rasterfall_net_client *client)
{
    struct toy_game *game;
    struct toy_game_actor *actor;
    int actor_index;
    if (!net || !session || !client || !client->active || !client->connected ||
        !client->down || session->game_state.state != TOY_GAME_PLAYING)
        return 0;
    game = &session->game_state;
    if (game->money < RASTERFALL_PAID_REVIVE_COST) return 0;
    actor_index = TOY_GAME_REMOTE_ACTOR_BASE + client->client_id - 1;
    if (actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS) return 0;
    actor = &game->actors[actor_index];
    game->money -= RASTERFALL_PAID_REVIVE_COST;
    client->camera = client->spawn;
    client->reported_camera = client->spawn;
    client->down = 0;
    client->hp = TOY_GAME_REVIVE_HP;
    client->revive_progress_ms = 0;
    actor->x = client->camera.x;
    actor->z = client->camera.z;
    actor->hp = TOY_GAME_REVIVE_HP;
    actor->state = TOY_GAME_ACTOR_ALIVE;
    actor->revive_progress_ms = 0;
    actor->airborne_ms = 0;
    actor->airborne_y = 0;
    actor->vertical_velocity = 0;
    actor->air_x = actor->x;
    actor->air_z = actor->z;
    toy_game_animation_set(&actor->animation, TOY_GAME_ANIM_REVIVE);
    client->animation = actor->animation;
    toy_game_emit_event(game, TOY_GAME_EV_REVIVE);
    toy_game_emit_event(game, TOY_GAME_EV_ACTOR_REVIVE);
    return 1;
}

static int net_send_join_accept(struct rasterfall_net *net,
                                const struct sockaddr_in *address,
                                int client_id, const struct camera *spawn)
{
    unsigned char packet[NET_HEADER_SIZE + 20];
    int size;
    size = packet_begin(packet, RASTERFALL_NET_HELLO, 20,
                        ++net->send_sequence, net->receive_sequence);
    if (size < 0) return -1;
    packet[NET_HEADER_SIZE] = (unsigned char)client_id;
    packet[NET_HEADER_SIZE + 1] = (unsigned char)RASTERFALL_NET_PLAYER_MAX;
    put_u32(packet + NET_HEADER_SIZE + 4, (uint32_t)spawn->x);
    put_u32(packet + NET_HEADER_SIZE + 8, (uint32_t)spawn->z);
    put_i16(packet + NET_HEADER_SIZE + 12, spawn->sy);
    put_i16(packet + NET_HEADER_SIZE + 14, spawn->cy);
    put_i16(packet + NET_HEADER_SIZE + 16, spawn->pitch_sy);
    put_i16(packet + NET_HEADER_SIZE + 18, spawn->pitch_cy);
    return net_send_to(net, address, packet, size);
}

static int net_client_index(const struct rasterfall_net *net,
                            const struct sockaddr_in *source)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
        if (net->clients[i].active &&
            same_address(&net->clients[i].address, source)) return i;
    return -1;
}

static void net_init_player_slots(struct toy_game_slot *slots,
                                  int *current_slot)
{
    if (!slots || !current_slot) return;
    memset(slots, 0, sizeof(struct toy_game_slot) * TOY_GAME_WEAPON_SLOTS);
    slots[0].weapon = -1;
    slots[2].weapon = -1;
    slots[3].weapon = -1;
    slots[1].weapon = TOY_GAME_WEAPON_PISTOL;
    slots[1].mag = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->mag_size;
    slots[1].reserve = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->reserve_max;
    *current_slot = 1;
}

static int net_client_index_client_id(const struct rasterfall_net *net,
                                      int client_id)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
        if (net->clients[i].active && net->clients[i].client_id == client_id)
            return i;
    return -1;
}

static void net_reset_client(struct rasterfall_net *net,
                             struct rasterfall_net_client *client, int id,
                             const struct sockaddr_in *source)
{
    memset(client, 0, sizeof(*client));
    client->active = 1;
    client->connected = 1;
    client->client_id = id;
    client->revive_target_id = -1;
    client->ai_revive_actor_index = -1;
    if (source) memcpy(&client->address, source, sizeof(*source));
    memcpy(&client->spawn, &net->client_spawn_base, sizeof(client->spawn));
    client->spawn.x += (id - 1) * 350;
    memcpy(&client->camera, &client->spawn, sizeof(client->camera));
    client->hp = TOY_GAME_SECONDARY_PLAYER_HP;
    client->state = TOY_GAME_PLAYING;
    net_init_player_slots(client->slots, &client->current_slot);
}

static int net_alloc_client(struct rasterfall_net *net,
                            const struct sockaddr_in *source)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *client = &net->clients[i];
        if (client->active) continue;
        net_reset_client(net, client, i + 1, source);
        return i;
    }
    return -1;
}

static int net_alloc_client_id(struct rasterfall_net *net,
                               const struct sockaddr_in *source,
                               int client_id)
{
    int i = net_client_index_client_id(net, client_id);
    if (i >= 0) return i;
    if (client_id <= 0 || client_id > RASTERFALL_NET_CLIENT_MAX) return -1;
    i = client_id - 1;
    if (net->clients[i].active) return -1;
    net_reset_client(net, &net->clients[i], client_id, source);
    return client_id - 1;
}

int rasterfall_net_client_slot_test(void)
{
    struct rasterfall_net net;
    struct sockaddr_in address;
    int i;
    rasterfall_net_init(&net);
    net.client_spawn_base.x = 1000;
    net.client_spawn_base.z = 2000;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        address.sin_port = htons((unsigned short)(3000 + i));
        if (net_alloc_client(&net, &address) != i) return 1;
        if (net.clients[i].client_id != i + 1 ||
            net.clients[i].camera.x != 1000 + i * 350 ||
            net_client_index(&net, &address) != i) return 2;
    }
    address.sin_port = htons(4000);
    if (net_alloc_client(&net, &address) >= 0) return 3;
    net.clients[1].kills = 99;
    memset(&net.clients[1], 0, sizeof(net.clients[1]));
    if (net_alloc_client_id(&net, &address, 2) != 1 ||
        net.clients[1].kills != 0 || net.clients[1].client_id != 2)
        return 4;
    return 0;
}

#define PUNCH_VERSION 2
#define PUNCH_REGISTER 1
#define PUNCH_MATCH 2
#define PUNCH_PROBE 3

static int punch_packet(const unsigned char *p, int size, int type)
{
    return size >= 6 && p[0] == 'R' && p[1] == 'F' && p[2] == 'P' &&
           p[3] == '2' && p[4] == PUNCH_VERSION && p[5] == type;
}

static int punch_send_register(struct rasterfall_net *net)
{
    unsigned char packet[12];
    uint32_t nonce = net->public_nonce;
    packet[0] = 'R'; packet[1] = 'F'; packet[2] = 'P'; packet[3] = '2';
    packet[4] = PUNCH_VERSION; packet[5] = PUNCH_REGISTER;
    put_u16(packet + 6, (unsigned int)net->public_room_id);
    put_u32(packet + 8, nonce);
    packet[10] = (unsigned char)(net->mode == RASTERFALL_NET_HOST ? 1 : 2);
    packet[11] = 0;
    return sendto(net->fd, packet, sizeof(packet), 0,
                  (const struct sockaddr *)&net->public_server,
                  sizeof(net->public_server)) == (long)sizeof(packet) ? 0 : -1;
}

static int punch_send_probe(struct rasterfall_net *net)
{
    unsigned char packet[12];
    packet[0] = 'R'; packet[1] = 'F'; packet[2] = 'P'; packet[3] = '2';
    packet[4] = PUNCH_VERSION; packet[5] = PUNCH_PROBE;
    put_u32(packet + 6, net->public_token); packet[10] = packet[11] = 0;
    return net_send(net, packet, sizeof(packet));
}

static int public_socket(struct rasterfall_net *net)
{
    struct sockaddr_in address;
    net->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (net->fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = 0;
    address.sin_port = 0;
    if (bind(net->fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        __close(net->fd); net->fd = -1; return -1;
    }
    return 0;
}

void rasterfall_net_init(struct rasterfall_net *net)
{
    memset(net, 0, sizeof(struct rasterfall_net));
    net->fd = -1;
}

static void net_reset_clients(struct rasterfall_net *net)
{
    memset(net->clients, 0, sizeof(net->clients));
}

int rasterfall_net_host(struct rasterfall_net *net, int port,
                        const struct camera *spawn)
{
    struct sockaddr_in address;
    int reuse = 1;
    net_windows_log("net host: init");
    rasterfall_net_init(net);
    net_windows_log("net host: socket");
    net->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (net->fd < 0) return -1;
    net_windows_log("net host: socket ready");
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = INADDR_ANY;
    net_windows_log("net host: setsockopt");
    setsockopt(net->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    net_windows_log("net host: bind");
    if (bind(net->fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        __close(net->fd);
        net->fd = -1;
        return -1;
    }
    net_windows_log("net host: bind ready");
    net->mode = RASTERFALL_NET_HOST;
    if (spawn) memcpy(&net->client_spawn_base, spawn, sizeof(*spawn));
    return 0;
}

int rasterfall_net_connect(struct rasterfall_net *net, const char *ip, int port)
{
    unsigned char hello[NET_HEADER_SIZE];
    int size;
    rasterfall_net_init(net);
    net->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (net->fd < 0) return -1;
    memset(&net->server_address, 0, sizeof(net->server_address));
    net->server_address.sin_family = AF_INET;
    net->server_address.sin_port = htons((unsigned short)port);
    net->server_address.sin_addr.s_addr = inet_addr(ip);
    if (net->server_address.sin_addr.s_addr == 0xffffffffU) {
        __close(net->fd);
        net->fd = -1;
        return -1;
    }
    net->server_known = 1;
    net->local_player_id = 1;
    net->mode = RASTERFALL_NET_CLIENT;
    net->last_hello_ms = net_monotonic_ms();
    size = packet_begin(hello, RASTERFALL_NET_HELLO, 0,
                        ++net->send_sequence, 0);
    net_send(net, hello, size);
    return 0;
}

int rasterfall_net_public_host(struct rasterfall_net *net, int room_id,
                               const struct camera *spawn)
{
    rasterfall_net_init(net);
    if (public_socket(net) < 0) return -1;
    memset(&net->public_server, 0, sizeof(net->public_server));
    net->public_server.sin_family = AF_INET;
    net->public_server.sin_port = htons(RASTERFALL_NET_PUNCH_PORT);
    net->public_server.sin_addr.s_addr = inet_addr(RASTERFALL_NET_PUNCH_SERVER);
    net->public_room = 1; net->public_room_id = room_id;
    net->public_nonce = (uint32_t)net_monotonic_ms();
    if (!net->public_nonce) net->public_nonce = 1;
    net->public_token = net->public_nonce;
    net->mode = RASTERFALL_NET_HOST;
    if (spawn) memcpy(&net->client_spawn_base, spawn, sizeof(*spawn));
    net->last_public_register_ms = 0;
    return punch_send_register(net);
}

int rasterfall_net_public_connect(struct rasterfall_net *net, int room_id)
{
    rasterfall_net_init(net);
    if (public_socket(net) < 0) return -1;
    memset(&net->public_server, 0, sizeof(net->public_server));
    net->public_server.sin_family = AF_INET;
    net->public_server.sin_port = htons(RASTERFALL_NET_PUNCH_PORT);
    net->public_server.sin_addr.s_addr = inet_addr(RASTERFALL_NET_PUNCH_SERVER);
    net->public_room = 1; net->public_room_id = room_id;
    net->public_nonce = (uint32_t)net_monotonic_ms();
    if (!net->public_nonce) net->public_nonce = 1;
    net->public_token = net->public_nonce;
    net->mode = RASTERFALL_NET_CLIENT;
    net->last_public_register_ms = 0;
    return punch_send_register(net);
}

int rasterfall_net_local_address(char *buffer, int buffer_size)
{
    struct sockaddr_in target, local;
    socklen_t length = sizeof(local);
    int fd;
    char *address;
    if (!buffer || buffer_size < 16) return -1;
    strcpy(buffer, "127.0.0.1");
    net_windows_log("net address: socket");
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    net_windows_log("net address: connect");
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    target.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (connect(fd, (struct sockaddr *)&target, sizeof(target)) == 0 &&
        getsockname(fd, (struct sockaddr *)&local, &length) == 0) {
        address = inet_ntoa(local.sin_addr);
        if (address && address[0]) {
            strcpy(buffer, address);
        }
    }
    net_windows_log("net address: close");
    __close(fd);
    return 0;
}

void rasterfall_net_close(struct rasterfall_net *net)
{
    if (net && net->mode != RASTERFALL_NET_OFF)
        __printf("rasterfall net: rtt=%dms input tx/rx=%lu/%lu "
                 "entries=%lu dup=%lu reorder=%lu recovered=%lu synth=%lu "
                 "player/entity/world=%lu/%lu/%lu reconcile=%lu "
                 "avg/max=%lu/%lu snap=%lu interp-underrun=%lu extra=%lu\n",
                 net->net_stats_avg_rtt_ms > 0 ? net->net_stats_avg_rtt_ms :
                                                net->rtt_ms,
                 net->input_packets_sent, net->input_packets_received,
                 net->input_entries_received, net->input_duplicates,
                 net->input_out_of_order, net->input_recovered,
                 net->input_synthesized,
                 net->player_snapshots_received,
                 net->entity_snapshots_received,
                 net->world_snapshots_received,
                 net->reconciliation_count,
                 net->reconciliation_count ? net->reconciliation_total /
                     net->reconciliation_count : 0,
                 net->reconciliation_max, net->reconciliation_hard_snaps,
                 net->interpolation_underruns, net->extrapolation_count);
    if (net->fd >= 0) __close(net->fd);
    rasterfall_net_init(net);
}

static void encode_input_entry(unsigned char *p,
                               const struct rasterfall_net_input *input)
{
    const struct rasterfall_command *c = &input->command;
    put_u32(p, input->sequence); put_u32(p + 4, input->tick);
    p[8] = put_i8_value(c->move_forward);
    p[9] = put_i8_value(c->move_strafe);
    p[10] = (unsigned char)(c->fire_held != 0);
    p[11] = (unsigned char)(c->shop_item < 0 ? 0 : c->shop_item);
    put_i16(p + 12, c->turn); put_i16(p + 14, c->pitch);
    put_u16(p + 16, c->buttons); put_u16(p + 18, c->shop_action);
    put_i16(p + 20, c->shop_arg); put_u32(p + 24, c->shop_request_id);
}

static int decode_input_entry(const unsigned char *p,
                              struct rasterfall_net_input *input)
{
    struct rasterfall_command *c;
    memset(input, 0, sizeof(*input)); c = &input->command;
    input->sequence = get_u32(p); input->tick = get_u32(p + 4);
    c->move_forward = get_i8_value(p[8]); c->move_strafe = get_i8_value(p[9]);
    c->fire_held = p[10] != 0; c->shop_item = p[11];
    c->turn = get_i16(p + 12); c->pitch = get_i16(p + 14);
    c->buttons = get_u16(p + 16); c->shop_action = get_u16(p + 18);
    c->shop_arg = get_i16(p + 20); c->shop_request_id = get_u32(p + 24);
    if (!input->sequence || c->move_forward < -1 || c->move_forward > 1 ||
        c->move_strafe < -1 || c->move_strafe > 1 || c->turn < -1024 ||
        c->turn > 1024 || c->pitch < -1024 || c->pitch > 1024) return -1;
    input->valid = 1;
    return 0;
}

static void decode_command_camera(const unsigned char *payload,
                                  struct camera *camera)
{
    camera->x = (int)get_u32(payload + 4);
    camera->z = (int)get_u32(payload + 8);
    camera->sy = get_i16(payload + 12);
    camera->cy = get_i16(payload + 14);
    camera->pitch_sy = get_i16(payload + 16);
    camera->pitch_cy = get_i16(payload + 18);
    camera->y = 0;
}

static void net_record_prediction(struct rasterfall_net *net,
                                  uint32_t sequence,
                                  const struct camera *predicted)
{
    struct rasterfall_net_prediction *entry;
    if (!net || !predicted) return;
    entry = &net->prediction_history[sequence % 64U];
    entry->sequence = sequence;
    entry->x = predicted->x;
    entry->z = predicted->z;
}

int rasterfall_net_send_command(struct rasterfall_net *net,
                                const struct rasterfall_command *command,
                                const struct camera *predicted)
{
    unsigned char packet[NET_HEADER_SIZE + NET_INPUT_SIZE];
    unsigned char *p = packet + NET_HEADER_SIZE;
    int size, result, count = 0, i;
    uint32_t sequence;
    struct rasterfall_command wire;
    if (net->mode != RASTERFALL_NET_CLIENT) return -1;
    memset(packet, 0, sizeof(packet));
    wire = *command;
    if (wire.buttons & (RASTERFALL_CMD_SHOP |
                        RASTERFALL_CMD_FLAG |
                        RASTERFALL_CMD_INTERACT)) {
        net->pending_shop_request_id = ++net->shop_request_next_id;
        if (!net->pending_shop_request_id)
            net->pending_shop_request_id = ++net->shop_request_next_id;
        net->pending_shop_action = wire.shop_action;
        net->pending_shop_item = wire.shop_item;
        net->pending_shop_arg = wire.shop_arg;
        net->pending_action_buttons = wire.buttons &
            (RASTERFALL_CMD_SHOP | RASTERFALL_CMD_FLAG |
             RASTERFALL_CMD_INTERACT);
        net->pending_shop_until_ms = net_monotonic_ms() + 2000;
    }
    if (net->pending_shop_request_id &&
        net_monotonic_ms() < net->pending_shop_until_ms) {
        wire.buttons |= net->pending_action_buttons;
        wire.shop_action = net->pending_shop_action;
        wire.shop_item = net->pending_shop_item;
        wire.shop_arg = net->pending_shop_arg;
        wire.shop_request_id = net->pending_shop_request_id;
    } else if (net->pending_shop_request_id) {
        net->pending_shop_request_id = 0;
    }
    net->tick++;
    sequence = ++net->send_sequence;
    {
        struct rasterfall_net_input *entry =
            &net->input_history[sequence % RASTERFALL_NET_INPUT_HISTORY];
        memset(entry, 0, sizeof(*entry)); entry->valid = 1;
        entry->sequence = sequence; entry->tick = net->tick;
        entry->command = wire;
    }
    size = packet_begin(packet, RASTERFALL_NET_INPUT, NET_INPUT_SIZE,
                        sequence, net->receive_sequence);
    if (size < 0) return size;
    p[0] = 0; p[1] = p[2] = p[3] = 0;
    put_u32(p + 4, (uint32_t)predicted->x); put_u32(p + 8, (uint32_t)predicted->z);
    put_i16(p + 12, predicted->sy); put_i16(p + 14, predicted->cy);
    put_i16(p + 16, predicted->pitch_sy); put_i16(p + 18, predicted->pitch_cy);
    put_u32(p + 20, net->reliable_event_ack);
    for (i = 0; i < RASTERFALL_NET_INPUT_REDUNDANCY; i++) {
        uint32_t s = sequence - (uint32_t)i;
        struct rasterfall_net_input *entry =
            &net->input_history[s % RASTERFALL_NET_INPUT_HISTORY];
        if (!entry->valid || entry->sequence != s) break;
        encode_input_entry(p + NET_INPUT_META_SIZE + count * NET_INPUT_ENTRY_SIZE,
                           entry);
        count++;
    }
    p[0] = (unsigned char)count;
    net->last_command_sequence = net->send_sequence;
    if (command->buttons & RASTERFALL_CMD_JUMP)
        net->last_jump_command_sequence = sequence;
    net->last_command_sent_ms = net_monotonic_ms();
    result = net_send(net, packet, size);
    net->input_packets_sent++;
    if (result == 0) {
        net_record_prediction(net, sequence, predicted);
        /* The current packet plus its redundancy copies are the reliable
         * delivery window for an edge action.  Keeping the request alive for
         * two seconds used to put the same shop command in every subsequent
         * input packet and delayed the ordinary navigation inputs behind it. */
        if (wire.shop_request_id == net->pending_shop_request_id)
            net->pending_shop_request_id = 0;
    }
    return result;
}

static void encode_player(unsigned char *p, int id, int active,
                          const struct camera *camera, int hp,
                          int weapon, int state, int downed,
                          int revive_progress_ms,
                          const struct toy_game_slot *slots,
                          int current_slot, int reloading, int reload_timer_ms,
                          int muzzle_flash_ms, int kills, int special_kills,
                          int damage_dealt, int throwable_damage_dealt,
                          unsigned int fire_seq, int ray_count,
                          const struct toy_game_ray *rays,
                          int airborne_ms, int airborne_y,
                          int airborne_velocity,
                          int air_x, int air_z,
                          uint32_t input_ack,
                          const struct toy_game_animation_state *animation)
{
    p[0] = (unsigned char)(active != 0);
    p[1] = (unsigned char)id;
    p[2] = put_weapon_value(weapon);
    p[3] = (unsigned char)state;
    p[40] = (unsigned char)(downed != 0);
    put_u16(p + 41, (unsigned int)(revive_progress_ms < 0 ? 0 :
                                    revive_progress_ms));
    put_u32(p + 4, (uint32_t)camera->x);
    put_u32(p + 8, (uint32_t)camera->z);
    put_i16(p + 12, camera->sy);
    put_i16(p + 14, camera->cy);
    put_i16(p + 16, camera->pitch_sy);
    put_i16(p + 18, camera->pitch_cy);
    put_i16(p + 43, camera->y);
    put_i16(p + 46, airborne_ms);
    put_i16(p + 48, airborne_y);
    put_i16(p + 50, airborne_velocity);
    put_i16(p + 65, air_x);
    put_i16(p + 67, air_z);
    put_u32(p + 52, input_ack);
    p[56] = animation && animation->id >= 0 &&
            animation->id < TOY_GAME_ANIM_COUNT ?
            (unsigned char)animation->id : TOY_GAME_ANIM_NONE;
    put_i16(p + 57, animation ? animation->time_ms : 0);
    put_i16(p + 20, hp);
    p[22] = (unsigned char)current_slot;
    p[23] = put_weapon_value(slots ? slots[0].weapon : -1);
    p[24] = put_weapon_value(slots ? slots[1].weapon : -1);
    p[68] = put_weapon_value(slots ? slots[2].weapon : -1);
    p[77] = put_weapon_value(slots ? slots[3].weapon : -1);
    put_i16(p + 25, slots ? slots[0].mag : 0);
    put_i16(p + 27, slots ? slots[0].reserve : 0);
    put_i16(p + 29, slots ? slots[1].mag : 0);
    put_i16(p + 31, slots ? slots[1].reserve : 0);
    put_i16(p + 69, slots ? slots[2].mag : 0);
    put_i16(p + 71, slots ? slots[2].reserve : 0);
    put_i16(p + 79, slots ? slots[3].mag : 0);
    put_i16(p + 81, slots ? slots[3].reserve : 0);
    p[33] = (unsigned char)(reloading != 0);
    put_i16(p + 34, reload_timer_ms);
    put_i16(p + 36, muzzle_flash_ms);
    put_i16(p + 38, kills);
    put_i16(p + 59, special_kills);
    put_u32(p + 61, (uint32_t)damage_dealt);
    put_u32(p + 73, (uint32_t)throwable_damage_dealt);
    put_u32(p + NET_PLAYER_BASE_SIZE, fire_seq);
    if (ray_count < 0) ray_count = 0;
    if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
    p[NET_PLAYER_BASE_SIZE + 4] = (unsigned char)ray_count;
    memset(p + NET_PLAYER_BASE_SIZE + 5, 0,
           TOY_GAME_MAX_RAYS * NET_PLAYER_RAY_SIZE);
    for (int i = 0; i < ray_count; i++) {
        unsigned char *q = p + NET_PLAYER_BASE_SIZE + 5 + i * NET_PLAYER_RAY_SIZE;
        put_i16(q, rays[i].sy); put_i16(q + 2, rays[i].cy);
        put_i16(q + 4, rays[i].vy); put_u32(q + 6, (uint32_t)rays[i].ex);
        put_u32(q + 10, (uint32_t)rays[i].ez);
        q[14] = (unsigned char)((rays[i].hit_enemy ? 1 : 0) |
                                (rays[i].hit_world ? 2 : 0));
    }
}

static int decode_player(const unsigned char *p,
                         struct rasterfall_net_player *player)
{
    player->active = p[0] != 0;
    player->id = p[1];
    player->weapon = get_weapon_value(p[2]);
    player->state = p[3];
    player->downed = p[40] != 0;
    player->revive_progress_ms = (int)get_u16(p + 41);
    player->camera.x = (int)get_u32(p + 4);
    player->camera.z = (int)get_u32(p + 8);
    player->camera.sy = get_i16(p + 12);
    player->camera.cy = get_i16(p + 14);
    player->camera.pitch_sy = get_i16(p + 16);
    player->camera.pitch_cy = get_i16(p + 18);
    player->camera.y = get_i16(p + 43);
    player->weapon_switch_timer_ms = get_i16(p + 44);
    player->airborne_ms = get_i16(p + 46);
    player->airborne_y = get_i16(p + 48);
    player->airborne_velocity = get_i16(p + 50);
    player->air_x = get_i16(p + 65);
    player->air_z = get_i16(p + 67);
    player->input_ack = get_u32(p + 52);
    player->animation.id = p[56] < TOY_GAME_ANIM_COUNT ? p[56] :
                           TOY_GAME_ANIM_NONE;
    player->animation.time_ms = get_i16(p + 57);
    player->hp = get_i16(p + 20);
    player->current_slot = p[22] < TOY_GAME_WEAPON_SLOTS ? p[22] : 0;
    player->slot_weapon[0] = get_weapon_value(p[23]);
    player->slot_weapon[1] = get_weapon_value(p[24]);
    player->slot_weapon[2] = get_weapon_value(p[68]);
    player->slot_weapon[3] = get_weapon_value(p[77]);
    player->mag[0] = get_i16(p + 25); player->reserve[0] = get_i16(p + 27);
    player->mag[1] = get_i16(p + 29); player->reserve[1] = get_i16(p + 31);
    player->mag[2] = get_i16(p + 69); player->reserve[2] = get_i16(p + 71);
    player->mag[3] = get_i16(p + 79); player->reserve[3] = get_i16(p + 81);
    player->reloading = p[33] != 0; player->reload_timer_ms = get_i16(p + 34);
    player->muzzle_flash_ms = get_i16(p + 36);
    player->kills = get_i16(p + 38);
    player->special_kills = get_i16(p + 59);
    player->damage_dealt = (int)get_u32(p + 61);
    player->throwable_damage_dealt = (int)get_u32(p + 73);
    player->fire_seq = get_u32(p + NET_PLAYER_BASE_SIZE);
    player->ray_count = p[NET_PLAYER_BASE_SIZE + 4];
    if (player->ray_count > TOY_GAME_MAX_RAYS) return -1;
    for (int i = 0; i < player->ray_count; i++) {
        const unsigned char *q = p + NET_PLAYER_BASE_SIZE + 5 + i * NET_PLAYER_RAY_SIZE;
        player->rays[i].sy = get_i16(q); player->rays[i].cy = get_i16(q + 2);
        player->rays[i].vy = get_i16(q + 4);
        player->rays[i].ex = (int)get_u32(q + 6);
        player->rays[i].ez = (int)get_u32(q + 10);
        player->rays[i].hit_enemy = q[14] & 1;
        player->rays[i].hit_world = (q[14] & 2) != 0;
    }
    return player->id >= 0 && player->id < RASTERFALL_NET_PLAYER_MAX ? 0 : -1;
}

static void net_push_remote_sample(struct rasterfall_net *net, int id,
                                   const struct rasterfall_net_player *player)
{
    int count;
    struct rasterfall_net_remote_sample *samples;
    if (!net || !player || id < 0 || id >= RASTERFALL_NET_PLAYER_MAX ||
        id == net->local_player_id) return;
    samples = net->remote_samples[id];
    count = net->remote_sample_count[id];
    if (count >= 3) {
        samples[0] = samples[1]; samples[1] = samples[2]; count = 2;
    }
    samples[count].valid = 1;
    samples[count].received_ms = net_monotonic_ms();
    samples[count].camera = player->camera;
    samples[count].airborne_y = player->airborne_y;
    net->remote_sample_count[id] = count + 1;
    if (count == 0) {
        net->remote_render_camera[id] = player->camera;
        net->remote_render_airborne_y[id] = player->airborne_y;
    }
}

static void encode_enemy(unsigned char *p, const struct toy_game_enemy *e,
                         int index)
{
    p[0] = (unsigned char)e->active;
    p[1] = (unsigned char)(toy_game_enemy_content_id(e->type) < 0 ? 0 :
                           toy_game_enemy_content_id(e->type));
    p[2] = (unsigned char)e->ai_state;
    put_i16(p + 3, e->hp);
    put_u32(p + 5, (uint32_t)e->x); put_u32(p + 9, (uint32_t)e->z);
    put_i16(p + 13, e->speed); put_i16(p + 15, e->bite_cooldown_ms);
    put_i16(p + 17, e->flash); put_i16(p + 19, e->hurt);
    put_i16(p + 21, e->dying_ms);
    put_i16(p + 23, e->dir_x); put_i16(p + 25, e->dir_z);
    p[27] = (unsigned char)(e->ability.special_target_active ? 1 : 0);
    p[28] = (unsigned char)(e->ability.charge_active ? 1 : 0);
    put_i16(p + 29, e->ability.special_timer_ms);
    put_i16(p + 31, e->ability.special_windup_ms);
    p[33] = put_i8_value(e->ability.special_target_kind);
    p[34] = put_i8_value(e->ability.special_target_index);
    put_i16(p + 35, e->ability.special_pull_timer_ms);
    put_i16(p + 37, e->ability.charge_dir_x);
    put_i16(p + 39, e->ability.charge_dir_z);
    put_i16(p + 41, e->ability.charge_elapsed_ms);
    put_i16(p + 43, e->airborne_ms);
    put_i16(p + 45, e->airborne_y);
    p[47] = (unsigned char)index;
}

static void decode_enemy(const unsigned char *p, struct rasterfall_net_enemy *e)
{
    memset(e, 0, sizeof(*e));
    e->index = p[47];
    e->active = p[0];
    e->type = toy_game_enemy_from_content_id((int)p[1]);
    if (e->type < 0) e->type = TOY_GAME_ENEMY_COMMON;
    e->ai_state = p[2];
    e->hp = get_i16(p + 3);
    e->x = (int)get_u32(p + 5); e->z = (int)get_u32(p + 9);
    e->speed = get_i16(p + 13); e->bite_cooldown_ms = get_i16(p + 15);
    e->flash = get_i16(p + 17); e->hurt = get_i16(p + 19);
    e->dying_ms = get_i16(p + 21);
    e->dir_x = get_i16(p + 23); e->dir_z = get_i16(p + 25);
    e->ability.special_target_active = p[27] & 1;
    e->ability.charge_active = p[28] & 1;
    e->ability.special_timer_ms = get_i16(p + 29);
    e->ability.special_windup_ms = get_i16(p + 31);
    e->ability.special_target_kind = get_i8_value(p[33]);
    e->ability.special_target_index = get_i8_value(p[34]);
    e->ability.special_pull_timer_ms = get_i16(p + 35);
    e->ability.charge_dir_x = get_i16(p + 37);
    e->ability.charge_dir_z = get_i16(p + 39);
    e->ability.charge_elapsed_ms = get_i16(p + 41);
    e->airborne_ms = get_i16(p + 43);
    e->airborne_y = get_i16(p + 45);
}

static void encode_actor(unsigned char *p, const struct toy_game_actor *a,
                        int actor_index)
{
    p[0] = (unsigned char)((a->class_id & 3) << 2);
    p[0] |= (unsigned char)((a->state & 3) << 4);
    p[1] = (unsigned char)actor_index;
    p[2] = put_weapon_value(a->slots[a->current_slot].weapon);
    put_u32(p + 3, (uint32_t)a->x); put_u32(p + 7, (uint32_t)a->z);
    put_i16(p + 11, a->sy); put_i16(p + 13, a->cy);
    put_i16(p + 15, a->hp);
    p[17] = (unsigned char)(a->state == TOY_GAME_ACTOR_DOWNED ?
                            a->revive_progress_ms / 12 :
                            (a->muzzle_flash_ms < 0 ? 0 :
                             a->muzzle_flash_ms > 255 ? 255 : a->muzzle_flash_ms));
    put_u32(p + 18, a->fire_seq);
    put_i16(p + 22, a->airborne_ms);
    put_i16(p + 24, a->airborne_y);
    p[26] = (unsigned char)a->animation.id;
    put_i16(p + 27, a->animation.time_ms);
    put_i16(p + 29, a->kills);
    put_i16(p + 31, a->special_kills);
    put_u32(p + 33, (uint32_t)a->damage_dealt);
    put_u32(p + 37, (uint32_t)a->throwable_damage_dealt);
    p[41] = (unsigned char)(a->hired != 0);
    memcpy(p + 42, a->name, TOY_GAME_MAX_NAME);
}

static void decode_actor(const unsigned char *p, struct rasterfall_net_actor *a)
{
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->actor_index = p[1];
    a->class_id = (p[0] >> 2) & 3;
    a->state = (p[0] >> 4) & 3;
    a->weapon = get_weapon_value(p[2]);
    a->x = (int)get_u32(p + 3); a->z = (int)get_u32(p + 7);
    a->sy = get_i16(p + 11); a->cy = get_i16(p + 13);
    a->hp = get_i16(p + 15);
    a->airborne_ms = get_i16(p + 22);
    a->airborne_y = get_i16(p + 24);
    a->animation.id = p[26] < TOY_GAME_ANIM_COUNT ? p[26] :
                      TOY_GAME_ANIM_NONE;
    a->animation.time_ms = get_i16(p + 27);
    a->kills = get_i16(p + 29);
    a->special_kills = get_i16(p + 31);
    a->damage_dealt = (int)get_u32(p + 33);
    a->throwable_damage_dealt = (int)get_u32(p + 37);
    a->hired = p[41] != 0;
    memcpy(a->name, p + 42, TOY_GAME_MAX_NAME);
    a->name[TOY_GAME_MAX_NAME - 1] = 0;
    if (a->state == TOY_GAME_ACTOR_DOWNED)
        a->revive_progress_ms = p[17] * 12;
    else
        a->muzzle_flash_ms = p[17];
    a->fire_seq = get_u32(p + 18);
}

static int send_ai_fire_packets(struct rasterfall_net *net,
                                const struct toy_game *game)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET];
    int actor_index;
    for (actor_index = 0; actor_index < TOY_GAME_MAX_ACTORS; actor_index++) {
        const struct toy_game_actor *actor = &game->actors[actor_index];
        unsigned char *p;
        int ray_count, i, size;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        if (!actor->fire_seq || actor->fire_seq == net->ai_fire_sent_seq[actor_index])
            continue;
        ray_count = actor->ray_count;
        if (ray_count < 0) ray_count = 0;
        if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
        size = packet_begin(packet, RASTERFALL_NET_AI_FIRE,
                            NET_AI_FIRE_BASE + ray_count * NET_PLAYER_RAY_SIZE,
                            ++net->send_sequence, net->receive_sequence);
        if (size < 0) return -1;
        p = packet + NET_HEADER_SIZE;
        p[0] = (unsigned char)actor_index;
        p[1] = (unsigned char)ray_count;
        put_u32(p + 2, actor->fire_seq);
        for (i = 0; i < ray_count; i++) {
            unsigned char *q = p + NET_AI_FIRE_BASE + i * NET_PLAYER_RAY_SIZE;
            put_i16(q, actor->rays[i].sy); put_i16(q + 2, actor->rays[i].cy);
            put_i16(q + 4, actor->rays[i].vy);
            put_u32(q + 6, (uint32_t)actor->rays[i].ex);
            put_u32(q + 10, (uint32_t)actor->rays[i].ez);
            q[14] = (unsigned char)((actor->rays[i].hit_enemy ? 1 : 0) |
                                    (actor->rays[i].hit_world ? 2 : 0));
        }
        /* Use the same sequence for every recipient.  Previously AI fire
         * packets only went to client 1: later clients missed the effect and
         * interpreted the resulting sequence hole as packet loss. */
        if (net_send_clients(net, packet, size) < 0) return -1;
        net->ai_fire_sent_seq[actor_index] = actor->fire_seq;
    }
    return 0;
}

#define NET_PLAYER_COMPACT_SIZE (NET_PLAYER_SIZE + 1)
#define NET_PLAYER_SNAPSHOT_BASE 8
#define NET_ENTITY_CHUNK_BASE 8

static void encode_player_compact(unsigned char *p, int id, int active,
                                  const struct camera *camera, int hp,
                                  int weapon, int state, int downed,
                                  int revive_progress_ms,
                                  const struct toy_game_slot *slots,
                                  int current_slot, int reloading,
                                  int reload_timer_ms, int weapon_switch_timer_ms,
                                  int muzzle_flash_ms,
                                  int kills, int special_kills,
                                  int damage_dealt, int throwable_damage_dealt,
                                  unsigned int fire_seq,
                                  int ray_count,
                                  const struct toy_game_ray *rays,
                                  int airborne_ms, int airborne_y,
                                  int airborne_velocity,
                                  int air_x, int air_z,
                                  uint32_t input_ack,
                                  int special_motion,
                                  const struct toy_game_animation_state *animation)
{
    unsigned char full[NET_PLAYER_SIZE];
    memset(full, 0, sizeof(full));
    encode_player(full, id, active, camera, hp, weapon, state, downed,
                  revive_progress_ms, slots, current_slot, reloading,
                  reload_timer_ms, muzzle_flash_ms, kills, special_kills,
                  damage_dealt, throwable_damage_dealt, fire_seq, ray_count, rays,
                  airborne_ms, airborne_y,
                  airborne_velocity,
                  air_x, air_z,
                  input_ack, animation);
    put_i16(full + 44, weapon_switch_timer_ms);
    memcpy(p, full, NET_PLAYER_SIZE);
    p[NET_PLAYER_SIZE] = (unsigned char)(special_motion != 0);
}

static int decode_player_compact(const unsigned char *p,
                                 struct rasterfall_net_player *player)
{
    unsigned char full[NET_PLAYER_SIZE];
    memset(full, 0, sizeof(full));
    memcpy(full, p, NET_PLAYER_SIZE);
    if (decode_player(full, player) < 0) return -1;
    player->special_motion = p[NET_PLAYER_SIZE] != 0;
    return 0;
}

static int net_send_player_snapshot(struct rasterfall_net *net,
                                    const struct camera *host_camera,
                                    const struct toy_game *game,
                                    uint32_t snapshot_sequence)
{
    unsigned char packet[NET_HEADER_SIZE + NET_PLAYER_SNAPSHOT_BASE +
                         RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_COMPACT_SIZE];
    unsigned char *p = packet + NET_HEADER_SIZE;
    int size, i;
    size = packet_begin(packet, RASTERFALL_NET_PLAYER_SNAPSHOT,
                        NET_PLAYER_SNAPSHOT_BASE +
                        RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_COMPACT_SIZE,
                        ++net->send_sequence, net->receive_sequence);
    if (size < 0) return -1;
    put_u32(p, snapshot_sequence); p[4] = RASTERFALL_NET_PLAYER_MAX;
    p[5] = (unsigned char)(game->player_control_disabled != 0);
    p[6] = p[7] = 0;
    encode_player_compact(p + NET_PLAYER_SNAPSHOT_BASE, 0, 1, host_camera,
        game->hp, game->slots[game->current_slot].weapon, game->state,
        game->player_down, game->player_revive_progress_ms, game->slots,
        game->current_slot, game->reloading, game->reload_timer_ms,
        game->weapon_switch_timer_ms,
        game->muzzle_flash_ms, game->kills, game->special_kills,
        game->damage_dealt, game->throwable_damage_dealt, game->fire_seq,
        game->ray_count, game->rays,
        game->player_airborne_ms, game->player_airborne_y,
        game->player_vertical_velocity, 0,
        game->player_air_x, game->player_air_z,
        game->player_control_disabled || game->player_airborne_ms > 0,
        &game->animation);
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *c = &net->clients[i];
        int id = i + 1;
        encode_player_compact(p + NET_PLAYER_SNAPSHOT_BASE +
            id * NET_PLAYER_COMPACT_SIZE, id, c->active && c->connected,
            &c->camera, c->hp,
            c->current_slot >= 0 && c->current_slot < TOY_GAME_WEAPON_SLOTS ?
                c->slots[c->current_slot].weapon : -1,
            c->state, c->down, c->revive_progress_ms, c->slots,
            c->current_slot, c->reloading, c->reload_timer_ms,
            c->weapon_switch_timer_ms,
            c->muzzle_flash_ms, c->kills, c->special_kills, c->damage_dealt,
            c->throwable_damage_dealt,
            c->fire_seq, c->ray_count, c->rays,
            c->airborne_ms, c->airborne_y,
            c->airborne_velocity,
            c->air_x, c->air_z,
            c->last_processed_input_sequence, c->airborne_ms > 0,
            &c->animation);
    }
    return net_send_clients(net, packet, size);
}

static int net_send_entity_chunks(struct rasterfall_net *net,
                                  const struct toy_game *game,
                                  uint32_t snapshot_sequence)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET];
    int kind;
    for (kind = 0; kind < 2; kind++) {
        int indices[TOY_GAME_MAX_ENEMIES];
        int count = 0, cursor = 0, chunk_index = 0;
        int entry_size = kind == 0 ? NET_ACTOR_SIZE : NET_ENEMY_SIZE;
        int max_entries = (RASTERFALL_NET_MAX_PACKET - NET_HEADER_SIZE -
                           NET_ENTITY_CHUNK_BASE) / entry_size;
        int limit = kind == 0 ? TOY_GAME_MAX_ACTORS : TOY_GAME_MAX_ENEMIES;
        /* Inactive slots carry no useful state.  Omitting them is important:
         * at 15 Hz the old fixed-size snapshot made clients decode several
         * chunks of tombstones on every update.  The receiver clears omitted
         * slots only after all chunks of this kind arrived, so a lost UDP
         * chunk still leaves the previous state usable. */
        for (int i = 0; i < limit; i++) {
            int active = kind == 0 ? game->actors[i].active :
                                     game->enemies[i].active;
            if (active) indices[count++] = i;
        }
        /* Always send one empty chunk: it is the completion marker for a
         * snapshot in which this entity kind has no active entries. */
        do {
            int n = count - cursor, size;
            unsigned char *p = packet + NET_HEADER_SIZE;
            if (n > max_entries) n = max_entries;
            size = packet_begin(packet, RASTERFALL_NET_ENTITY_SNAPSHOT,
                                NET_ENTITY_CHUNK_BASE + n * entry_size,
                                ++net->send_sequence, net->receive_sequence);
            if (size < 0) return -1;
            put_u32(p, snapshot_sequence); p[4] = (unsigned char)kind;
            p[5] = (unsigned char)chunk_index++; p[6] = (unsigned char)n;
            p[7] = (unsigned char)((count + max_entries - 1) / max_entries);
            for (int i = 0; i < n; i++)
                if (kind == 0)
                    encode_actor(p + NET_ENTITY_CHUNK_BASE + i * entry_size,
                                 &game->actors[indices[cursor + i]],
                                 indices[cursor + i]);
                else
                    encode_enemy(p + NET_ENTITY_CHUNK_BASE + i * entry_size,
                                 &game->enemies[indices[cursor + i]],
                                 indices[cursor + i]);
            if (net_send_clients(net, packet, size) < 0) return -1;
            cursor += n;
        } while (cursor < count);
    }
    return 0;
}

static int net_send_world_snapshot(struct rasterfall_net *net,
                                   const struct rasterfall_session *session,
                                   const struct toy_game *game,
                                   int air_walls_enabled,
                                   int manual_alarm_enabled,
                                   int manual_alarm_timer_ms,
                                   uint32_t snapshot_sequence)
{
    unsigned char packet[NET_HEADER_SIZE + 4 + NET_WORLD_SIZE];
    unsigned char *w = packet + NET_HEADER_SIZE + 4;
    int size, i;
    size = packet_begin(packet, RASTERFALL_NET_WORLD_SNAPSHOT,
                        4 + NET_WORLD_SIZE, ++net->send_sequence,
                        net->receive_sequence);
    if (size < 0) return -1;
    put_u32(packet + NET_HEADER_SIZE, snapshot_sequence);
    memset(w, 0, NET_WORLD_SIZE);
    put_i16(w, game->wave); put_i16(w + 2, game->to_spawn);
    put_i16(w + 4, game->spawn_timer_ms); put_i16(w + 6, game->enemies_alive);
    put_i16(w + 8, game->campaign_phase); put_i16(w + 10, game->phase_timer_ms);
    w[12] = (unsigned char)((air_walls_enabled ? 1 : 0) |
                            (manual_alarm_enabled ? 2 : 0));
    put_i16(w + 14, game->alarm_timer_ms); put_i16(w + 16, game->spawn_budget);
    put_i16(w + 18, game->active_attackers);
    put_i16(w + 20, game->director_encounters); put_i16(w + 22, game->goal_hold_ms);
    put_i16(w + 24, manual_alarm_timer_ms); put_i16(w + 26, game->alarm_triggered);
    put_i16(w + 28, game->campaign_stage);
    w[30] = (unsigned char)(game->player_control_disabled != 0);
    w[31] = (unsigned char)game->wave_attack_multiplier;
    put_i16(w + 32, game->wave_attack_points);
    put_i16(w + 34, game->wave_waiting_common);
    put_i16(w + 36, game->wave_waiting_fast);
    put_i16(w + 38, game->wave_waiting_heavy);
    put_i16(w + 40, game->wave_waiting_special);
    put_i16(w + 42, game->wave_waiting_tank);
    put_u32(w + NET_WORLD_BASE_SIZE, (uint32_t)game->money);
    put_u32(w + NET_WORLD_BASE_SIZE + 4, game->unlocked_weapons);
    put_i16(w + NET_WORLD_BASE_SIZE + 8, session ? session->flag_count : 0);
    for (i = 0; i < RASTERFALL_MAX_FLAGS; i++) {
        const struct rasterfall_flag *f = session && i < session->flag_count ?
                                          &session->flags[i] : NULL;
        unsigned char *fp = w + NET_WORLD_BASE_SIZE + 12 +
                            i * NET_WORLD_FLAG_SIZE;
        put_u32(fp, f ? (uint32_t)f->x : 0); put_u32(fp + 4, f ? (uint32_t)f->z : 0);
        fp[8] = f && f->active; fp[9] = f && f->carried;
        fp[10] = put_i8_value(f ? f->carrier_id : -1);
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        put_i16(w + NET_WORLD_BASE_SIZE + 12 +
                RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + i * 2,
                session && session->game_state.actors[i].active ?
                    session->game_state.actors[i].flag_index : -1);
    for (i = 0; i < TOY_GAME_MAX_PROJECTILES; i++) {
        const struct toy_game_projectile *p = game->projectiles + i;
        unsigned char *pp = w + NET_WORLD_ENTITY_BASE +
                            i * NET_WORLD_PROJECTILE_SIZE;
        memset(pp, 0, NET_WORLD_PROJECTILE_SIZE);
        pp[0] = (unsigned char)(p->active != 0);
        pp[1] = (unsigned char)p->kind;
        put_u32(pp + 2, (uint32_t)p->x); put_u32(pp + 6, (uint32_t)p->z);
        put_i16(pp + 10, p->vx); put_i16(pp + 12, p->vz);
        put_i16(pp + 14, p->vy); put_i16(pp + 16, p->fuse_ms);
        put_i16(pp + 18, p->blink_timer_ms); put_i16(pp + 20, p->flash_ms);
        put_i16(pp + 22, p->age_ms); put_i16(pp + 24, p->y);
        put_i16(pp + 26, p->bounces); pp[28] = (unsigned char)(p->landed != 0);
    }
    for (i = 0; i < TOY_CONFIG_MAX_BURN_ZONES; i++) {
        const struct toy_game_burn_zone *zone = game->burn_zones + i;
        unsigned char *bp = w + NET_WORLD_ENTITY_BASE +
                            TOY_GAME_MAX_PROJECTILES * NET_WORLD_PROJECTILE_SIZE +
                            i * NET_WORLD_BURN_ZONE_SIZE;
        memset(bp, 0, NET_WORLD_BURN_ZONE_SIZE);
        bp[0] = (unsigned char)(zone->active != 0);
        put_u32(bp + 2, (uint32_t)zone->x); put_u32(bp + 6, (uint32_t)zone->z);
        put_i16(bp + 10, zone->remaining_ms); put_i16(bp + 12, zone->tick_ms);
        put_i16(bp + 14, zone->elapsed_ms);
    }
    return net_send_clients(net, packet, size);
}

int rasterfall_net_send_snapshot(struct rasterfall_net *net,
                                 const struct rasterfall_session *session,
                                 const struct camera *host_camera,
                                 const struct toy_game *game,
                                 int air_walls_enabled,
                                 int manual_alarm_enabled,
                                 int manual_alarm_timer_ms)
{
    int actor_i;
    if (net->mode != RASTERFALL_NET_HOST) return -1;
    for (actor_i = 0; actor_i < RASTERFALL_NET_CLIENT_MAX; actor_i++)
        if (net->clients[actor_i].active && net->clients[actor_i].connected)
            break;
    if (actor_i == RASTERFALL_NET_CLIENT_MAX) return -1;
    {
        uint32_t snapshot_sequence = ++net->last_snapshot_sequence;
        net->last_snapshot_sent_ms = net_monotonic_ms();
        if (net_send_player_snapshot(net, host_camera, game,
                                     snapshot_sequence) < 0) return -1;
        if (net_send_entity_chunks(net, game, snapshot_sequence) < 0) return -1;
        if (net_send_world_snapshot(net, session, game, air_walls_enabled,
                                    manual_alarm_enabled,
                                    manual_alarm_timer_ms,
                                    snapshot_sequence) < 0) return -1;
        net_send_reliable_events(net);
        return send_ai_fire_packets(net, game);
    }
#if 0 /* Protocol v30 monolithic snapshot sender; kept beside its decoder only
       * until the next compatibility cleanup.  Protocol v31 never builds or
       * executes this all-or-nothing path. */
    if (payload_size > RASTERFALL_NET_MAX_SNAPSHOT) return -1;
    net->last_snapshot_sequence = ++net->send_sequence;
    net->last_snapshot_sent_ms = net_monotonic_ms();
    put_u32(p, net->tick);
    p[4] = RASTERFALL_NET_PLAYER_MAX;
    p[5] = p[6] = 0;
    p[7] = (unsigned char)actor_count;
    encode_player(p + NET_SNAPSHOT_BASE, 0, 1, host_camera, game->hp,
                  weapon, game->state, game->player_down,
                  game->player_revive_progress_ms, game->slots, game->current_slot,
                  game->reloading, game->reload_timer_ms,
                  game->muzzle_flash_ms, game->kills, game->special_kills,
                  game->damage_dealt,
                  game->fire_seq,
                  game->ray_count, game->rays,
                  game->player_airborne_ms, game->player_airborne_y, 0,
                  &game->animation);
    for (int client_i = 0; client_i < RASTERFALL_NET_CLIENT_MAX; client_i++) {
        struct rasterfall_net_client *client = &net->clients[client_i];
        int player_id = client_i + 1;
        int offset = NET_SNAPSHOT_BASE + player_id * NET_PLAYER_SIZE;
        encode_player(p + offset, player_id,
                      client->active && client->connected,
                      &client->camera, client->hp,
                      client->current_slot >= 0 &&
                              client->current_slot < TOY_GAME_WEAPON_SLOTS ?
                          client->slots[client->current_slot].weapon : -1,
                      client->state, client->down,
                      client->revive_progress_ms, client->slots,
                      client->current_slot, client->reloading,
                      client->reload_timer_ms, client->muzzle_flash_ms,
                      client->kills, client->special_kills,
                      client->damage_dealt,
                      client->fire_seq, client->ray_count,
                      client->rays, client->airborne_ms,
                      client->airborne_y,
                      client->last_processed_input_sequence,
                      &client->animation);
    }
    for (actor_i = 0; actor_i < actor_count; actor_i++) {
        encode_actor(p + NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                     actor_i * NET_ACTOR_SIZE, &game->actors[actor_indices[actor_i]],
                     actor_indices[actor_i]);
    }
    p[NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
      actor_count * NET_ACTOR_SIZE] = (unsigned char)enemy_count;
    for (int i = 0; i < enemy_count; i++)
        encode_enemy(p + NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                     actor_count * NET_ACTOR_SIZE + 1 + i * NET_ENEMY_SIZE,
                     &game->enemies[enemy_indices[i]], enemy_indices[i]);
    world_data = p + NET_SNAPSHOT_BASE +
                 RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                 actor_count * NET_ACTOR_SIZE + 1 +
                 enemy_count * NET_ENEMY_SIZE;
    put_i16(world_data, game->wave);
    put_i16(world_data + 2, game->to_spawn);
    put_i16(world_data + 4, game->spawn_timer_ms);
    put_i16(world_data + 6, game->enemies_alive);
    put_i16(world_data + 8, game->campaign_phase);
    put_i16(world_data + 10, game->phase_timer_ms);
    world_data[12] = (unsigned char)((air_walls_enabled ? 1 : 0) |
                                     (manual_alarm_enabled ? 2 : 0));
    put_i16(world_data + 14, game->alarm_timer_ms);
    put_i16(world_data + 16, game->spawn_budget);
    put_i16(world_data + 18, game->active_attackers);
    put_i16(world_data + 20, game->director_encounters);
    put_i16(world_data + 22, game->goal_hold_ms);
    put_i16(world_data + 24, manual_alarm_timer_ms);
    put_i16(world_data + 26, game->alarm_triggered);
    put_i16(world_data + 28, game->campaign_stage);
    world_data[30] = (unsigned char)(game->player_control_disabled ? 1 : 0);
    world_data[31] = (unsigned char)game->wave_attack_multiplier;
    put_i16(world_data + 32, game->wave_attack_points);
    put_i16(world_data + 34, game->wave_waiting_common);
    put_i16(world_data + 36, game->wave_waiting_fast);
    put_i16(world_data + 38, game->wave_waiting_heavy);
    put_i16(world_data + 40, game->wave_waiting_special);
    put_i16(world_data + 42, game->wave_waiting_tank);
    put_u32(world_data + NET_WORLD_BASE_SIZE, (uint32_t)game->money);
    put_u32(world_data + NET_WORLD_BASE_SIZE + 4,
            (uint32_t)game->unlocked_weapons);
    put_i16(world_data + NET_WORLD_BASE_SIZE + 8,
            session ? session->flag_count : 0);
    for (int flag_i = 0; flag_i < RASTERFALL_MAX_FLAGS; flag_i++) {
        const struct rasterfall_flag *flag = session &&
            flag_i < session->flag_count ? &session->flags[flag_i] : 0;
        unsigned char *fp = world_data + NET_WORLD_BASE_SIZE + 12 +
                            flag_i * NET_WORLD_FLAG_SIZE;
        put_u32(fp, flag ? (uint32_t)flag->x : 0);
        put_u32(fp + 4, flag ? (uint32_t)flag->z : 0);
        fp[8] = flag && flag->active ? 1 : 0;
        fp[9] = flag && flag->carried ? 1 : 0;
        fp[10] = put_i8_value(flag ? flag->carrier_id : -1);
        fp[11] = 0;
    }
    for (int actor_i = 0; actor_i < TOY_GAME_MAX_ACTORS; actor_i++)
        put_i16(world_data + NET_WORLD_BASE_SIZE + 12 +
                RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + actor_i * 2,
                session && actor_i < TOY_GAME_MAX_ACTORS &&
                session->game_state.actors[actor_i].active ?
                    session->game_state.actors[actor_i].flag_index : -1);
    part_count = (payload_size + NET_SNAPSHOT_FRAGMENT_DATA - 1) /
                 NET_SNAPSHOT_FRAGMENT_DATA;
    if (part_count <= 0 || part_count > 32) return -1;
    for (part_index = 0, offset = 0; part_index < part_count;
         part_index++, offset += NET_SNAPSHOT_FRAGMENT_DATA) {
        int chunk = payload_size - offset;
        unsigned char *part;
        if (chunk > NET_SNAPSHOT_FRAGMENT_DATA) chunk = NET_SNAPSHOT_FRAGMENT_DATA;
        size = packet_begin(packet, RASTERFALL_NET_SNAPSHOT_PART,
                            NET_SNAPSHOT_PART_BASE + chunk,
                            net->last_snapshot_sequence,
                            net->receive_sequence);
        if (size < 0) return -1;
        part = packet + NET_HEADER_SIZE;
        put_u16(part, (unsigned int)payload_size);
        put_u16(part + 2, (unsigned int)offset);
        put_u16(part + 4, (unsigned int)chunk);
        put_u16(part + 6, (unsigned int)part_index);
        put_u16(part + 8, (unsigned int)part_count);
        memcpy(part + NET_SNAPSHOT_PART_BASE, snapshot + offset, (size_t)chunk);
        if (net_send_clients(net, packet, size) < 0) return -1;
    }
    net_send_reliable_events(net);
    return send_ai_fire_packets(net, game);
#endif
}

static int decode_player_snapshot(const unsigned char *payload, int size,
                                  struct rasterfall_net *net)
{
    uint32_t snapshot_sequence;
    int count, i;
    if (size != NET_PLAYER_SNAPSHOT_BASE +
                RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_COMPACT_SIZE) return -1;
    snapshot_sequence = get_u32(payload); count = payload[4];
    if (count != RASTERFALL_NET_PLAYER_MAX ||
        (net->player_snapshot_sequence &&
         !sequence_after(snapshot_sequence, net->player_snapshot_sequence)))
        return 0;
    for (i = 0; i < count; i++) {
        struct rasterfall_net_player player;
        if (decode_player_compact(payload + NET_PLAYER_SNAPSHOT_BASE +
                                  i * NET_PLAYER_COMPACT_SIZE, &player) < 0)
            return -1;
        net->players[player.id] = player;
        net_push_remote_sample(net, player.id, &player);
    }
    net->snapshot_player_control_disabled = payload[5] & 1;
    net->player_snapshot_sequence = snapshot_sequence;
    net->snapshot_ready = 1; net->world_ready = 1;
    net->player_snapshots_received++;
    return 0;
}

static int decode_entity_snapshot(const unsigned char *payload, int size,
                                  struct rasterfall_net *net)
{
    uint32_t snapshot_sequence;
    int kind, count, entry_size, i, chunk_index, parts_total;
    if (size < NET_ENTITY_CHUNK_BASE) return -1;
    snapshot_sequence = get_u32(payload); kind = payload[4];
    chunk_index = payload[5]; count = payload[6]; parts_total = payload[7];
    if (kind > 1) return -1;
    if (parts_total <= 0 || chunk_index < 0 || chunk_index >= parts_total ||
        parts_total > 32) return -1;
    entry_size = kind == 0 ? NET_ACTOR_SIZE : NET_ENEMY_SIZE;
    if (size != NET_ENTITY_CHUNK_BASE + count * entry_size) return -1;
    if (net->entity_snapshot_sequence != snapshot_sequence) {
        if (net->entity_snapshot_sequence &&
            !sequence_after(snapshot_sequence, net->entity_snapshot_sequence))
            return 0;
        net->entity_snapshot_sequence = snapshot_sequence;
        net->actor_count = net->enemy_count = 0;
        net->entity_actor_seen = net->entity_enemy_seen = 0;
        net->entity_actor_parts_seen = net->entity_enemy_parts_seen = 0;
        net->entity_actor_complete = net->entity_enemy_complete = 0;
    }
    if (kind == 0) {
        if (net->entity_actor_parts_total != parts_total) {
            net->entity_actor_parts_total = parts_total;
            net->entity_actor_parts_seen = 0;
        }
        if (net->entity_actor_parts_seen & (1U << chunk_index)) return 0;
        net->entity_actor_parts_seen |= 1U << chunk_index;
    } else {
        if (net->entity_enemy_parts_total != parts_total) {
            net->entity_enemy_parts_total = parts_total;
            net->entity_enemy_parts_seen = 0;
        }
        if (net->entity_enemy_parts_seen & (1U << chunk_index)) return 0;
        net->entity_enemy_parts_seen |= 1U << chunk_index;
    }
    for (i = 0; i < count; i++) {
        if (kind == 0 && net->actor_count < RASTERFALL_NET_MAX_ACTORS) {
            decode_actor(payload + NET_ENTITY_CHUNK_BASE + i * entry_size,
                         &net->actors[net->actor_count++]);
            net->entity_actor_seen |= 1ULL << net->actors[net->actor_count - 1].actor_index;
        } else if (kind == 1 && net->enemy_count < TOY_GAME_MAX_ENEMIES) {
            decode_enemy(payload + NET_ENTITY_CHUNK_BASE + i * entry_size,
                         &net->enemies[net->enemy_count++]);
            net->entity_enemy_seen |= 1ULL << net->enemies[net->enemy_count - 1].index;
        }
    }
    if (kind == 0 && net->entity_actor_parts_seen ==
        ((1U << net->entity_actor_parts_total) - 1U))
        net->entity_actor_complete = 1;
    if (kind == 1 && net->entity_enemy_parts_seen ==
        ((1U << net->entity_enemy_parts_total) - 1U))
        net->entity_enemy_complete = 1;
    net->entity_snapshots_received++;
    return 0;
}

static int decode_world_snapshot(const unsigned char *payload, int size,
                                 struct rasterfall_net *net)
{
    const unsigned char *w;
    uint32_t sequence;
    int i;
    if (size != 4 + NET_WORLD_SIZE) return -1;
    sequence = get_u32(payload);
    if (net->world_snapshot_sequence &&
        !sequence_after(sequence, net->world_snapshot_sequence)) return 0;
    w = payload + 4;
    net->snapshot_world_wave = get_i16(w);
    net->snapshot_world_to_spawn = get_i16(w + 2);
    net->snapshot_world_spawn_timer_ms = get_i16(w + 4);
    net->snapshot_world_enemies_alive = get_i16(w + 6);
    net->snapshot_world_phase = get_i16(w + 8);
    net->snapshot_world_phase_timer_ms = get_i16(w + 10);
    net->snapshot_air_walls_enabled = w[12] & 1;
    net->snapshot_manual_alarm_enabled = (w[12] & 2) != 0;
    net->snapshot_world_alarm_timer_ms = get_i16(w + 14);
    net->snapshot_world_spawn_budget = get_i16(w + 16);
    net->snapshot_world_active_attackers = get_i16(w + 18);
    net->snapshot_world_director_encounters = get_i16(w + 20);
    net->snapshot_world_goal_hold_ms = get_i16(w + 22);
    net->snapshot_world_manual_alarm_timer_ms = get_i16(w + 24);
    net->snapshot_world_alarm_triggered = get_i16(w + 26);
    net->snapshot_world_campaign_stage = get_i16(w + 28);
    net->snapshot_player_control_disabled = w[30] & 1;
    net->snapshot_world_wave_attack_multiplier = w[31] ? w[31] : 1;
    net->snapshot_world_wave_attack_points = get_i16(w + 32);
    net->snapshot_world_wave_waiting_common = get_i16(w + 34);
    net->snapshot_world_wave_waiting_fast = get_i16(w + 36);
    net->snapshot_world_wave_waiting_heavy = get_i16(w + 38);
    net->snapshot_world_wave_waiting_special = get_i16(w + 40);
    net->snapshot_world_wave_waiting_tank = get_i16(w + 42);
    net->snapshot_money = (int)get_u32(w + NET_WORLD_BASE_SIZE);
    net->snapshot_unlocked_weapons = get_u32(w + NET_WORLD_BASE_SIZE + 4);
    net->snapshot_flag_count = get_i16(w + NET_WORLD_BASE_SIZE + 8);
    if (net->snapshot_flag_count < 0 ||
        net->snapshot_flag_count > RASTERFALL_MAX_FLAGS) return -1;
    for (i = 0; i < RASTERFALL_MAX_FLAGS; i++) {
        const unsigned char *fp = w + NET_WORLD_BASE_SIZE + 12 +
                                  i * NET_WORLD_FLAG_SIZE;
        struct rasterfall_flag *f = &net->snapshot_flags[i];
        f->x = (int)get_u32(fp); f->z = (int)get_u32(fp + 4);
        f->active = fp[8] != 0; f->carried = fp[9] != 0;
        f->carrier_id = get_i8_value(fp[10]);
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        net->snapshot_actor_flag_index[i] = get_i16(w + NET_WORLD_BASE_SIZE +
            12 + RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + i * 2);
    for (i = 0; i < TOY_GAME_MAX_PROJECTILES; i++) {
        const unsigned char *pp = w + NET_WORLD_ENTITY_BASE +
                                  i * NET_WORLD_PROJECTILE_SIZE;
        struct toy_game_projectile *p = &net->snapshot_projectiles[i];
        memset(p, 0, sizeof(*p));
        p->active = pp[0] != 0; p->kind = pp[1];
        p->x = (int)get_u32(pp + 2); p->z = (int)get_u32(pp + 6);
        p->vx = get_i16(pp + 10); p->vz = get_i16(pp + 12);
        p->vy = get_i16(pp + 14); p->fuse_ms = get_i16(pp + 16);
        p->blink_timer_ms = get_i16(pp + 18); p->flash_ms = get_i16(pp + 20);
        p->age_ms = get_i16(pp + 22); p->y = get_i16(pp + 24);
        p->bounces = get_i16(pp + 26); p->landed = pp[28] != 0;
    }
    for (i = 0; i < TOY_CONFIG_MAX_BURN_ZONES; i++) {
        const unsigned char *bp = w + NET_WORLD_ENTITY_BASE +
                                  TOY_GAME_MAX_PROJECTILES * NET_WORLD_PROJECTILE_SIZE +
                                  i * NET_WORLD_BURN_ZONE_SIZE;
        struct toy_game_burn_zone *zone = &net->snapshot_burn_zones[i];
        memset(zone, 0, sizeof(*zone));
        zone->active = bp[0] != 0;
        zone->x = (int)get_u32(bp + 2); zone->z = (int)get_u32(bp + 6);
        zone->remaining_ms = get_i16(bp + 10);
        zone->tick_ms = get_i16(bp + 12);
        zone->elapsed_ms = get_i16(bp + 14);
    }
    net->world_snapshot_sequence = sequence;
    net->world_snapshots_received++;
    return 0;
}

static int decode_snapshot(const unsigned char *payload, int size,
                           struct rasterfall_net *net)
{
    int count, i;
    const unsigned char *world_data;
    if (size < NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE + 1) return -1;
    count = payload[4];
    if (count < 0 || count > RASTERFALL_NET_PLAYER_MAX ||
        count != RASTERFALL_NET_PLAYER_MAX ||
        payload[7] > RASTERFALL_NET_MAX_ACTORS ||
        size != NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                payload[7] * NET_ACTOR_SIZE + 1 +
                payload[NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                        payload[7] * NET_ACTOR_SIZE] * NET_ENEMY_SIZE +
                NET_WORLD_SIZE) return -1;
    for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++)
        net->players[i].active = 0;
    for (i = 0; i < count; i++) {
        struct rasterfall_net_player player;
        if (decode_player(payload + NET_SNAPSHOT_BASE + i * NET_PLAYER_SIZE,
                          &player) < 0) return -1;
        memcpy(&net->players[player.id], &player,
               sizeof(struct rasterfall_net_player));
        net_push_remote_sample(net, player.id, &player);
    }
    net->actor_count = payload[7];
    for (i = 0; i < net->actor_count; i++)
        decode_actor(payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                     i * NET_ACTOR_SIZE, &net->actors[i]);
    net->enemy_count = payload[NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                               net->actor_count * NET_ACTOR_SIZE];
    if (net->enemy_count > TOY_GAME_MAX_ENEMIES) return -1;
    world_data = payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                 net->actor_count * NET_ACTOR_SIZE + 1 +
                 net->enemy_count * NET_ENEMY_SIZE;
    for (i = 0; i < net->enemy_count; i++)
        decode_enemy(payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                     net->actor_count * NET_ACTOR_SIZE + 1 + i * NET_ENEMY_SIZE,
                     &net->enemies[i]);
    net->snapshot_world_wave = get_i16(world_data);
    net->snapshot_world_to_spawn = get_i16(world_data + 2);
    net->snapshot_world_spawn_timer_ms = get_i16(world_data + 4);
    net->snapshot_world_enemies_alive = get_i16(world_data + 6);
    net->snapshot_world_phase = get_i16(world_data + 8);
    net->snapshot_world_phase_timer_ms = get_i16(world_data + 10);
    net->snapshot_air_walls_enabled = world_data[12] & 1;
    net->snapshot_manual_alarm_enabled = (world_data[12] & 2) != 0;
    net->snapshot_world_alarm_timer_ms = get_i16(world_data + 14);
    net->snapshot_world_spawn_budget = get_i16(world_data + 16);
    net->snapshot_world_active_attackers = get_i16(world_data + 18);
    net->snapshot_world_director_encounters = get_i16(world_data + 20);
    net->snapshot_world_goal_hold_ms = get_i16(world_data + 22);
    net->snapshot_world_manual_alarm_timer_ms = get_i16(world_data + 24);
    net->snapshot_world_alarm_triggered = get_i16(world_data + 26);
    net->snapshot_world_campaign_stage = get_i16(world_data + 28);
    net->snapshot_world_wave_attack_points = get_i16(world_data + 32);
    net->snapshot_world_wave_attack_multiplier = world_data[31] ? world_data[31] : 1;
    net->snapshot_world_wave_waiting_common = get_i16(world_data + 34);
    net->snapshot_world_wave_waiting_fast = get_i16(world_data + 36);
    net->snapshot_world_wave_waiting_heavy = get_i16(world_data + 38);
    net->snapshot_world_wave_waiting_special = get_i16(world_data + 40);
    net->snapshot_world_wave_waiting_tank = get_i16(world_data + 42);
    net->snapshot_money = (int)get_u32(world_data + NET_WORLD_BASE_SIZE);
    net->snapshot_unlocked_weapons = get_u32(world_data + NET_WORLD_BASE_SIZE + 4);
    net->snapshot_flag_count = get_i16(world_data + NET_WORLD_BASE_SIZE + 8);
    if (net->snapshot_flag_count < 0 ||
        net->snapshot_flag_count > RASTERFALL_MAX_FLAGS) return -1;
    for (i = 0; i < RASTERFALL_MAX_FLAGS; i++) {
        const unsigned char *fp = world_data + NET_WORLD_BASE_SIZE + 12 +
                                  i * NET_WORLD_FLAG_SIZE;
        struct rasterfall_flag *flag = &net->snapshot_flags[i];
        memset(flag, 0, sizeof(*flag));
        flag->x = (int)get_u32(fp);
        flag->z = (int)get_u32(fp + 4);
        flag->active = fp[8] != 0;
        flag->carried = fp[9] != 0;
        flag->carrier_id = get_i8_value(fp[10]);
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        net->snapshot_actor_flag_index[i] = get_i16(
            world_data + NET_WORLD_BASE_SIZE + 12 +
            RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + i * 2);
    net->snapshot_player_control_disabled = world_data[30] & 1;
    net->snapshot_ready = 1;
    net->world_ready = 1;
    return 0;
}

static unsigned int snapshot_expected_mask(int part_count)
{
    return part_count == 32 ? 0xffffffffU : ((1U << part_count) - 1U);
}

static void snapshot_clear_assembly(struct rasterfall_snapshot_assembly *assembly)
{
    if (!assembly) return;
    assembly->sequence = 0;
    assembly->total_size = 0;
    assembly->part_count = 0;
    assembly->mask = 0;
}

static int snapshot_assembly_complete(
    const struct rasterfall_snapshot_assembly *assembly)
{
    return assembly && assembly->part_count > 0 &&
           assembly->mask == snapshot_expected_mask(assembly->part_count);
}

static void snapshot_abandon(struct rasterfall_net *net,
                             struct rasterfall_snapshot_assembly *assembly)
{
    int missing;
    if (!net || !assembly || assembly->part_count <= 0 ||
        snapshot_assembly_complete(assembly)) return;
    missing = assembly->part_count - snapshot_popcount(assembly->mask);
    if (missing > 0) net->snapshot_parts_missing += missing;
    net->snapshot_abandoned++;
    snapshot_clear_assembly(assembly);
}

static int snapshot_try_decode(struct rasterfall_net *net,
                               struct rasterfall_snapshot_assembly *assembly)
{
    if (!net || !assembly || !snapshot_assembly_complete(assembly)) return 0;
    if (assembly->sequence <= net->receive_sequence) {
        snapshot_clear_assembly(assembly);
        return 0;
    }
    if (decode_snapshot(assembly->buffer, assembly->total_size, net) < 0) {
        snapshot_clear_assembly(assembly);
        return -1;
    }
    net->receive_sequence = assembly->sequence;
    net->snapshot_completed++;
    snapshot_clear_assembly(assembly);
    return 1;
}

static int decode_snapshot_part(const unsigned char *payload, int size,
                                uint32_t sequence, struct rasterfall_net *net)
{
    int total_size, offset, chunk, part_index, part_count;
    struct rasterfall_snapshot_assembly *assembly = NULL;
    if (size < NET_SNAPSHOT_PART_BASE) return -1;
    total_size = (int)get_u16(payload);
    offset = (int)get_u16(payload + 2);
    chunk = (int)get_u16(payload + 4);
    part_index = (int)get_u16(payload + 6);
    part_count = (int)get_u16(payload + 8);
    if (total_size <= 0 || total_size > RASTERFALL_NET_MAX_SNAPSHOT ||
        chunk <= 0 || chunk > NET_SNAPSHOT_FRAGMENT_DATA ||
        size != NET_SNAPSHOT_PART_BASE + chunk || part_count <= 0 ||
        part_count > 32 || part_index < 0 || part_index >= part_count ||
        offset != part_index * NET_SNAPSHOT_FRAGMENT_DATA ||
        offset + chunk > total_size) return -1;
    if (sequence <= net->receive_sequence) return 0;

    if (net->snapshot_current.part_count > 0 &&
        sequence == net->snapshot_current.sequence) {
        assembly = &net->snapshot_current;
    } else if (net->snapshot_previous.part_count > 0 &&
               sequence == net->snapshot_previous.sequence) {
        assembly = &net->snapshot_previous;
    } else if (net->snapshot_current.part_count <= 0) {
        assembly = &net->snapshot_current;
        snapshot_clear_assembly(assembly);
        assembly->sequence = sequence;
        assembly->total_size = total_size;
        assembly->part_count = part_count;
    } else if (sequence > net->snapshot_current.sequence) {
        /* Keep one incomplete generation alive for late fragments.  If both
         * slots are occupied, the older one is the first one we can abandon. */
        if (net->snapshot_previous.part_count > 0)
            snapshot_abandon(net, &net->snapshot_previous);
        net->snapshot_previous = net->snapshot_current;
        snapshot_clear_assembly(&net->snapshot_current);
        net->snapshot_current.sequence = sequence;
        net->snapshot_current.total_size = total_size;
        net->snapshot_current.part_count = part_count;
        assembly = &net->snapshot_current;
    } else {
        /* Older than current and not the retained previous generation. */
        return 0;
    }

    /* A sequence has one fixed layout.  Do not let a malformed variant
     * overwrite an existing assembly slot. */
    if (assembly->total_size != total_size || assembly->part_count != part_count)
        return -1;
    net->snapshot_parts_received++;
    if (assembly->mask & (1U << part_index)) {
        net->snapshot_parts_duplicate++;
        return 0;
    }
    memcpy(assembly->buffer + offset, payload + NET_SNAPSHOT_PART_BASE,
           (size_t)chunk);
    assembly->mask |= 1U << part_index;

    if (snapshot_try_decode(net, assembly) < 0) return -1;
    if (net->snapshot_previous.part_count > 0 &&
        net->snapshot_previous.sequence <= net->receive_sequence)
        snapshot_clear_assembly(&net->snapshot_previous);
    return 0;
}

static int snapshot_test_feed(struct rasterfall_net *net,
                              const unsigned char *snapshot, int total_size,
                              uint32_t sequence, int part_index, int part_count)
{
    unsigned char payload[NET_SNAPSHOT_PART_BASE + NET_SNAPSHOT_FRAGMENT_DATA];
    int offset = part_index * NET_SNAPSHOT_FRAGMENT_DATA;
    int chunk = total_size - offset;
    if (chunk > NET_SNAPSHOT_FRAGMENT_DATA) chunk = NET_SNAPSHOT_FRAGMENT_DATA;
    if (offset < 0 || chunk <= 0 || part_index < 0 ||
        part_index >= part_count || chunk > NET_SNAPSHOT_FRAGMENT_DATA)
        return -1;
    put_u16(payload, (unsigned int)total_size);
    put_u16(payload + 2, (unsigned int)offset);
    put_u16(payload + 4, (unsigned int)chunk);
    put_u16(payload + 6, (unsigned int)part_index);
    put_u16(payload + 8, (unsigned int)part_count);
    memcpy(payload + NET_SNAPSHOT_PART_BASE, snapshot + offset, (size_t)chunk);
    return decode_snapshot_part(payload, NET_SNAPSHOT_PART_BASE + chunk,
                                sequence, net);
}

int rasterfall_net_snapshot_fragment_test(void)
{
    unsigned char snapshot[RASTERFALL_NET_MAX_SNAPSHOT];
    struct rasterfall_net net;
    int total_size;
    int actor_count = 4;
    int enemy_count = 32;
    int actor_base = NET_SNAPSHOT_BASE +
                     RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE;
    int enemy_count_offset = actor_base + actor_count * NET_ACTOR_SIZE;
    int i;

    total_size = enemy_count_offset + 1 + enemy_count * NET_ENEMY_SIZE +
                 NET_WORLD_SIZE;
    if (total_size > RASTERFALL_NET_MAX_SNAPSHOT) return -1;
    memset(snapshot, 0, sizeof(snapshot));
    snapshot[4] = RASTERFALL_NET_PLAYER_MAX;
    snapshot[7] = (unsigned char)actor_count;
    for (i = 0; i < actor_count; i++)
        snapshot[actor_base + i * NET_ACTOR_SIZE + 1] = (unsigned char)i;
    snapshot[enemy_count_offset] = (unsigned char)enemy_count;
    for (i = 0; i < enemy_count; i++)
        snapshot[enemy_count_offset + 1 + i * NET_ENEMY_SIZE + 47] =
            (unsigned char)i;

    /* Complete in order. */
    rasterfall_net_init(&net);
    for (i = 0; i < 4; i++)
        if (snapshot_test_feed(&net, snapshot, total_size, 100, i, 4) < 0)
            return -1;
    if (net.snapshot_completed != 1 || net.receive_sequence != 100)
        return -2;

    /* Complete with arbitrary fragment order. */
    rasterfall_net_init(&net);
    for (i = 0; i < 4; i++) {
        static const int order[4] = {2, 0, 3, 1};
        if (snapshot_test_feed(&net, snapshot, total_size, 100,
                               order[i], 4) < 0) return -3;
    }
    if (net.snapshot_completed != 1 || net.receive_sequence != 100)
        return -4;

    /* A late fragment can complete the previous generation. */
    rasterfall_net_init(&net);
    if (snapshot_test_feed(&net, snapshot, total_size, 100, 0, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 100, 1, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 100, 3, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 101, 0, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 100, 2, 4) < 0)
        return -5;
    if (net.snapshot_completed != 1 || net.receive_sequence != 100 ||
        net.snapshot_previous.part_count != 0)
        return -6;

    /* A third incomplete generation abandons only the oldest one. */
    rasterfall_net_init(&net);
    if (snapshot_test_feed(&net, snapshot, total_size, 100, 0, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 101, 0, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 102, 0, 4) < 0)
        return -7;
    if (net.snapshot_abandoned != 1 || net.snapshot_parts_missing != 3)
        return -8;

    /* Duplicate fragments do not alter the assembly mask or receive count. */
    rasterfall_net_init(&net);
    if (snapshot_test_feed(&net, snapshot, total_size, 100, 0, 4) < 0 ||
        snapshot_test_feed(&net, snapshot, total_size, 100, 0, 4) < 0)
        return -9;
    if (net.snapshot_parts_received != 2 ||
        net.snapshot_parts_duplicate != 1)
        return -10;

    /* An older complete snapshot cannot roll back an applied newer one. */
    rasterfall_net_init(&net);
    for (i = 0; i < 4; i++)
        if (snapshot_test_feed(&net, snapshot, total_size, 101, i, 4) < 0)
            return -11;
    for (i = 0; i < 4; i++)
        if (snapshot_test_feed(&net, snapshot, total_size, 100, i, 4) < 0)
            return -12;
    if (net.snapshot_completed != 1 || net.receive_sequence != 101)
        return -13;

    /* Complete assembly state is cleared after decode. */
    if (net.snapshot_current.part_count != 0 &&
        net.snapshot_previous.part_count != 0)
        return -14;
    return 0;
}

static int decode_ai_fire(const unsigned char *payload, int size,
                          struct rasterfall_net *net)
{
    struct rasterfall_net_actor *actor;
    int actor_index, ray_count, i;
    if (size < NET_AI_FIRE_BASE) return -1;
    actor_index = payload[0];
    ray_count = payload[1];
    if (actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS ||
        ray_count < 0 || ray_count > TOY_GAME_MAX_RAYS ||
        size != NET_AI_FIRE_BASE + ray_count * NET_PLAYER_RAY_SIZE) return -1;
    actor = NULL;
    for (i = 0; i < net->actor_count; i++)
        if (net->actors[i].actor_index == actor_index) {
            actor = &net->actors[i];
            break;
        }
    if (!actor) {
        if (net->actor_count >= RASTERFALL_NET_MAX_ACTORS) return -1;
        actor = &net->actors[net->actor_count++];
        memset(actor, 0, sizeof(*actor));
        actor->active = 1;
        actor->actor_index = actor_index;
    }
    actor->fire_seq = get_u32(payload + 2);
    actor->ray_count = ray_count;
    for (i = 0; i < ray_count; i++) {
        const unsigned char *q = payload + NET_AI_FIRE_BASE +
                                 i * NET_PLAYER_RAY_SIZE;
        actor->rays[i].sy = get_i16(q); actor->rays[i].cy = get_i16(q + 2);
        actor->rays[i].vy = get_i16(q + 4);
        actor->rays[i].ex = (int)get_u32(q + 6);
        actor->rays[i].ez = (int)get_u32(q + 10);
        actor->rays[i].hit_enemy = q[14] & 1;
        actor->rays[i].hit_world = (q[14] & 2) != 0;
    }
    return 0;
}

static void net_receive_client_packet(struct rasterfall_net *net,
                                      struct rasterfall_net_client *client,
                                      const unsigned char *packet, int type,
                                      int payload_size, uint32_t sequence,
                                      uint32_t ack)
{
    const unsigned char *payload = packet + NET_HEADER_SIZE;
    int count, i;
    if (!client) return;
    net_client_note_sequence(client, sequence);
    client->last_receive_ms = net->last_receive_ms;
    if (type == RASTERFALL_NET_HELLO) {
        if (!client->last_processed_input_sequence)
            client->last_processed_input_sequence = sequence;
        if (!net->relay_mode)
            net_send_join_accept(net, &client->address, client->client_id,
                                 &client->spawn);
        return;
    }
    if (type != RASTERFALL_NET_INPUT || payload_size != NET_INPUT_SIZE) return;
    count = payload[0];
    if (count <= 0 || count > RASTERFALL_NET_INPUT_REDUNDANCY) return;
    net->input_packets_received++;
    if (!client->camera_initialized) {
        decode_command_camera(payload, &client->camera);
        client->camera_initialized = 1;
    }
    decode_command_camera(payload, &client->reported_camera);
    client->reported_camera_ready = 1;
    client->reliable_event_ack = get_u32(payload + 20);
    for (i = 0; i < count; i++) {
        struct rasterfall_net_input input;
        struct rasterfall_net_input *slot;
        if (decode_input_entry(payload + NET_INPUT_META_SIZE +
                               i * NET_INPUT_ENTRY_SIZE, &input) < 0)
            continue;
        net->input_entries_received++;
        if (client->last_processed_input_sequence &&
            sequence_before_or_equal(input.sequence,
                                     client->last_processed_input_sequence)) {
            net->input_duplicates++;
            continue;
        }
        slot = &client->input_queue[
            input.sequence % RASTERFALL_NET_INPUT_HISTORY];
        if (slot->valid && slot->sequence == input.sequence) {
            net->input_duplicates++;
            continue;
        }
        if (client->last_input_sequence &&
            !sequence_after(input.sequence, client->last_input_sequence))
            net->input_out_of_order++;
        if (i > 0) net->input_recovered++;
        *slot = input;
        client->input_queue_depth++;
        if (client->input_queue_depth > client->input_queue_max_depth)
            client->input_queue_max_depth = client->input_queue_depth;
        if (!client->last_input_sequence ||
            sequence_after(input.sequence, client->last_input_sequence))
            client->last_input_sequence = input.sequence;
    }
    client->last_input_tick = net->tick;
    if (ack && net->last_snapshot_sent_ms) {
        long elapsed = net_monotonic_ms() - net->last_snapshot_sent_ms;
        if (elapsed >= 0 && elapsed < 60000) client->rtt_ms = (int)elapsed;
    }
}

void rasterfall_net_poll(struct rasterfall_net *net)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET];
    struct sockaddr_in source;
    socklen_t source_len;
    long received;
    if (net->fd < 0) return;
    for (;;) {
        int type, payload_size;
        int relay_client_id = -1;
        uint32_t sequence, ack;
        source_len = sizeof(source);
        received = recvfrom(net->fd, packet, sizeof(packet), 0,
                            (struct sockaddr *)&source, &source_len);
        if (received < 0) {
            if (received == -EAGAIN) return;
            return;
        }
        net->net_stats_rx_bytes += (unsigned long)received;
        net->net_stats_rx_packets++;
        net_stats_roll(net);
        if (net->public_room && punch_packet(packet, (int)received, PUNCH_MATCH)) {
            uint32_t match_token;
            int new_match;
            int was_connected = net->connected;
            if (received < 18 || get_u16(packet + 6) != (unsigned int)net->public_room_id)
                continue;
            match_token = get_u32(packet + 8);
            new_match = !net->public_matched || net->public_token != match_token;
            net->public_token = match_token;
            net->relay_mode = packet[18] != 0;
            if (net->mode == RASTERFALL_NET_HOST && new_match)
                net_reset_clients(net);
            if (net->relay_mode) {
                /* In relay mode all game packets use the coordinator as the
                 * stable endpoint; the coordinator forwards them by room. */
                memcpy(&net->server_address, &net->public_server, sizeof(net->server_address));
            } else {
                struct sockaddr_in matched_address;
                memset(&matched_address, 0, sizeof(matched_address));
                matched_address.sin_family = AF_INET;
                memcpy(&matched_address.sin_addr.s_addr, packet + 12, 4);
                matched_address.sin_port = htons((unsigned short)get_u16(packet + 16));
                if (net->mode == RASTERFALL_NET_HOST) {
                    int index = net_alloc_client_id(
                        net, &matched_address, packet[19]);
                    if (index >= 0) {
                        memcpy(&net->clients[index].address, &matched_address,
                               sizeof(matched_address));
                        net->clients[index].connected = 1;
                    }
                } else {
                    memcpy(&net->server_address, &matched_address, sizeof(matched_address));
                }
            }
            net->server_known = net->relay_mode ||
                                net->mode == RASTERFALL_NET_CLIENT;
            net->public_matched = 1;
            if (net->mode == RASTERFALL_NET_CLIENT) {
                if (packet[19] <= 0 || packet[19] >= RASTERFALL_NET_PLAYER_MAX) {
                    /* 旧版公网协调器不会分配玩家 ID。继续运行会把
                     * 第二个客户端静默当成客户端1，表现为“看到别人的
                     * 画面且自己的输入失效”，必须等待新版服务端。 */
                    __printf("rasterfall: public server lacks player-id support\n");
                    net->connected = 0;
                    net->world_ready = 0;
                    continue;
                }
                net->local_player_id = packet[19];
            }
            if (net->relay_mode) net->connected = 1;
            net->last_public_punch_ms = 0;
            if (!net->relay_mode) punch_send_probe(net);
            if (net->mode == RASTERFALL_NET_CLIENT && !was_connected) {
                unsigned char hello[NET_HEADER_SIZE];
                int hello_size = packet_begin(hello, RASTERFALL_NET_HELLO, 0,
                                              ++net->send_sequence, 0);
                net_send(net, hello, hello_size);
                net->last_hello_ms = net_monotonic_ms();
            }
            continue;
        }
        if (net->public_room && punch_packet(packet, (int)received, PUNCH_PROBE)) {
            if (received < 10 || get_u32(packet + 6) != net->public_token) continue;
            /* NAT 可能在从协调服务器切换到对端后重新分配源端口；
             * 探测包已经带有房间令牌，因此可安全地刷新真实 endpoint。 */
            if (net->mode == RASTERFALL_NET_CLIENT) {
                memcpy(&net->server_address, &source, sizeof(source));
                net->server_known = 1;
            } else {
                int index = net_client_index(net, &source);
                if (index >= 0) net->clients[index].connected = 1;
            }
            net->public_matched = 1;
            if (net->mode == RASTERFALL_NET_CLIENT) net->connected = 1;
            net->last_receive_ms = net_monotonic_ms();
            continue;
        }
        if (net->relay_mode && received >= 5 + NET_HEADER_SIZE &&
            packet[0] == 'R' && packet[1] == 'F' &&
            packet[2] == 'R' && packet[3] == '4') {
            relay_client_id = packet[4];
            memmove(packet, packet + 5, (size_t)received - 5);
            received -= 5;
        }
        if (packet_header(packet, (int)received, &type, &payload_size,
                          &sequence, &ack) < 0) continue;
        net->last_receive_ms = net_monotonic_ms();
        if (net->mode == RASTERFALL_NET_HOST) {
            int client_index;
            struct rasterfall_net_client *client;
            if (net->relay_mode) {
                if (relay_client_id <= 0 ||
                    relay_client_id > RASTERFALL_NET_CLIENT_MAX)
                    continue;
                client_index = net_alloc_client_id(net, &source,
                                                   relay_client_id);
            } else {
                client_index = net_client_index(net, &source);
                if (client_index < 0 &&
                    (type == RASTERFALL_NET_HELLO ||
                     type == RASTERFALL_NET_INPUT))
                    client_index = net_alloc_client(net, &source);
            }
            if (client_index < 0) continue;
            client = &net->clients[client_index];
            if (!net->relay_mode)
                memcpy(&client->address, &source, sizeof(source));
            net_receive_client_packet(net, client, packet, type, payload_size,
                                      sequence, ack);
        } else if (net->mode == RASTERFALL_NET_CLIENT) {
            net_stats_note_sequence(net, sequence);
            if (net->public_room) {
                memcpy(&net->server_address, &source, sizeof(source));
                net->server_known = 1;
            } else if (!same_address(&net->server_address, &source)) continue;
            if (type == RASTERFALL_NET_HELLO && payload_size >= 20) {
                net->local_player_id = packet[NET_HEADER_SIZE];
                net->client_spawn_base.x = (int)get_u32(packet + NET_HEADER_SIZE + 4);
                net->client_spawn_base.z = (int)get_u32(packet + NET_HEADER_SIZE + 8);
                net->client_spawn_base.sy = get_i16(packet + NET_HEADER_SIZE + 12);
                net->client_spawn_base.cy = get_i16(packet + NET_HEADER_SIZE + 14);
                net->client_spawn_base.pitch_sy =
                    get_i16(packet + NET_HEADER_SIZE + 16);
                net->client_spawn_base.pitch_cy =
                    get_i16(packet + NET_HEADER_SIZE + 18);
                net->spawn_pending = 1;
                net->connected = 1;
            } else if (type == RASTERFALL_NET_RELIABLE_EVENT &&
                       decode_reliable_events(packet + NET_HEADER_SIZE,
                                              payload_size, net) == 0) {
                net->connected = 1;
            } else if (type == RASTERFALL_NET_AI_FIRE &&
                decode_ai_fire(packet + NET_HEADER_SIZE, payload_size, net) == 0) {
                /* AI fire packets are visual companions to snapshots and do
                 * not participate in snapshot ordering. */
            } else if (type == RASTERFALL_NET_PLAYER_SNAPSHOT &&
                       decode_player_snapshot(packet + NET_HEADER_SIZE,
                                              payload_size, net) == 0) {
                net->connected = 1;
            } else if (type == RASTERFALL_NET_ENTITY_SNAPSHOT &&
                       decode_entity_snapshot(packet + NET_HEADER_SIZE,
                                              payload_size, net) == 0) {
            } else if (type == RASTERFALL_NET_WORLD_SNAPSHOT &&
                       decode_world_snapshot(packet + NET_HEADER_SIZE,
                                             payload_size, net) == 0) {
            } else if (type == RASTERFALL_NET_SNAPSHOT_PART &&
                       decode_snapshot_part(packet + NET_HEADER_SIZE,
                                             payload_size, sequence, net) == 0) {
                /* A snapshot becomes visible only after every application
                 * fragment has arrived. */
            } else if (type == RASTERFALL_NET_SNAPSHOT &&
                       sequence > net->receive_sequence &&
                       decode_snapshot(packet + NET_HEADER_SIZE, payload_size, net) == 0)
                net->receive_sequence = sequence;
            if (!net->receive_sequence ||
                sequence_after(sequence, net->receive_sequence))
                net->receive_sequence = sequence;
            if (type == RASTERFALL_NET_SNAPSHOT ||
                type == RASTERFALL_NET_PLAYER_SNAPSHOT ||
                type == RASTERFALL_NET_SNAPSHOT_PART) {
                net->connected = 1;
                if (ack == net->last_command_sequence && net->last_command_sent_ms) {
                    long elapsed = net_monotonic_ms() - net->last_command_sent_ms;
                    net_stats_note_rtt(net, elapsed);
                } else if (net->last_command_sent_ms) {
                    long elapsed = net_monotonic_ms() - net->last_command_sent_ms;
                    net_stats_note_rtt(net, elapsed);
                }
            }
        }
    }
}

void rasterfall_net_update_connection(struct rasterfall_net *net)
{
    long now;
    unsigned char hello[NET_HEADER_SIZE];
    int size, i;
    if (!net || net->fd < 0 || net->mode == RASTERFALL_NET_OFF) return;
    now = net_monotonic_ms();
    net_stats_roll(net);
    if (net->mode == RASTERFALL_NET_HOST) {
        net_drop_reliable_events(net);
        net_send_reliable_events(net);
        for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
            struct rasterfall_net_client *client = &net->clients[i];
            if (client->active && client->last_receive_ms &&
                now - client->last_receive_ms > 3000)
                memset(client, 0, sizeof(*client));
        }
    }
    if (net->public_room) {
        /* REGISTER is also the room lease heartbeat.  Keep sending it after
         * MATCH; otherwise the coordinator quite correctly expires a host
         * that has already entered the game. */
        if ((!net->last_public_register_ms ||
             now - net->last_public_register_ms >= 5000)) {
            if (punch_send_register(net) == 0) net->last_public_register_ms = now;
        }
        if (net->public_matched && net->server_known &&
            (!net->last_public_punch_ms || now - net->last_public_punch_ms >= 100)) {
            if (punch_send_probe(net) == 0) net->last_public_punch_ms = now;
        }
    }
    if (net->mode == RASTERFALL_NET_CLIENT && net->last_receive_ms &&
        now - net->last_receive_ms > 3000) {
        net->connected = 0;
        net->receive_sequence = 0;
        net->snapshot_ready = 0;
        net->world_ready = 0;
        net->spawn_pending = 0;
        net->remote_event_last_id = 0;
        net->reliable_event_ack = 0;
        net->player_snapshot_sequence = 0;
        net->entity_snapshot_sequence = 0;
        net->world_snapshot_sequence = 0;
        memset(net->input_history, 0, sizeof(net->input_history));
        memset(net->remote_samples, 0, sizeof(net->remote_samples));
        memset(net->remote_sample_count, 0, sizeof(net->remote_sample_count));
        memset(net->remote_render_camera, 0,
               sizeof(net->remote_render_camera));
        net->correction_x = net->correction_z = net->correction_y = 0;
        net->correction_remaining_ms = 0;
    }
    if (net->mode == RASTERFALL_NET_CLIENT && !net->connected &&
        ((!net->public_room) || net->public_matched) &&
        (!net->last_hello_ms || now - net->last_hello_ms >= 500)) {
        size = packet_begin(hello, RASTERFALL_NET_HELLO, 0,
                            ++net->send_sequence, net->receive_sequence);
        if (net_send(net, hello, size) == 0) net->last_hello_ms = now;
    }
}

static void net_apply_client(struct rasterfall_net *net,
                                   struct rasterfall_session *session,
                                   struct rasterfall_net_client *client)
{
    struct toy_game *g = &session->game_state;
    struct toy_game_slot host_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray host_rays[TOY_GAME_MAX_RAYS];
    struct toy_game_actor *actor;
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    int host_px, host_pz, host_hp, host_down, host_revive;
    int host_current, host_reload, host_reload_timer, host_weapon_switch;
    int host_cooldown;
    int host_muzzle, host_damage, host_kills, host_special_kills;
    int host_damage_dealt, host_throwable_damage_dealt, host_ray_count;
    int old_reloading;
    unsigned int old_fire_seq;
    struct toy_game_animation_state host_animation;
    unsigned int host_fire_seq;
    int event_start, index;
    if (!client->active || !client->connected || !client->command_ready)
        return;
    /* Client ids map directly and uniformly onto the reserved actor slots. */
    index = TOY_GAME_REMOTE_ACTOR_BASE + client->client_id - 1;
    if (index < 0 || index >= TOY_GAME_MAX_ACTORS) return;
    actor = &g->actors[index];
    /* Start from the host's last camera state.  The reported camera is already
     * predicted by the client, so using it here and then applying command.turn
     * or command.pitch would rotate the same input twice. */
    /* Smoker/Charger and the vertical simulation are applied after this
     * command, matching the local client's jump/motion tick order. */
    client->camera.x = actor->x;
    client->camera.z = actor->z;
    client->camera.y = actor->ground_y + actor->airborne_y;
    if (client->command.buttons & RASTERFALL_CMD_REVIVE)
        net_paid_revive_client(net, session, client);
    if ((client->command.buttons & RASTERFALL_CMD_JUMP) &&
        actor->state == TOY_GAME_ACTOR_ALIVE)
        toy_game_jump_actor(g, index,
            (client->camera.sy * client->command.move_forward +
             client->camera.cy * client->command.move_strafe) *
                RASTERFALL_MOVE_STEP / 1024,
            (client->camera.cy * client->command.move_forward -
             client->camera.sy * client->command.move_strafe) *
                RASTERFALL_MOVE_STEP / 1024);
    memcpy(host_slots, g->slots, sizeof(host_slots));
    memcpy(host_rays, g->rays, sizeof(host_rays));
    host_px = g->px; host_pz = g->pz; host_hp = g->hp;
    host_down = g->player_down;
    host_revive = g->player_revive_progress_ms;
    host_current = g->current_slot;
    host_reload = g->reloading; host_reload_timer = g->reload_timer_ms;
    host_weapon_switch = g->weapon_switch_timer_ms;
    host_cooldown = g->fire_cooldown_ms; host_muzzle = g->muzzle_flash_ms;
    host_damage = g->damage_flash_ms; host_kills = g->kills;
    host_special_kills = g->special_kills;
    host_damage_dealt = g->damage_dealt;
    host_throwable_damage_dealt = g->throwable_damage_dealt;
    host_ray_count = g->ray_count; host_fire_seq = g->fire_seq;
    host_animation = g->animation;
    memcpy(g->slots, actor->slots, sizeof(g->slots));
    g->current_slot = actor->current_slot;
    g->hp = actor->hp; g->player_down = actor->state == TOY_GAME_ACTOR_DOWNED;
    g->player_revive_progress_ms = actor->revive_progress_ms;
    g->reloading = actor->reloading; g->reload_timer_ms = actor->reload_timer_ms;
    g->weapon_switch_timer_ms = client->weapon_switch_timer_ms;
    g->fire_cooldown_ms = actor->fire_cooldown_ms;
    g->muzzle_flash_ms = actor->muzzle_flash_ms;
    g->damage_flash_ms = 0; g->kills = client->kills;
    g->special_kills = client->special_kills;
    g->damage_dealt = client->damage_dealt;
    g->throwable_damage_dealt = client->throwable_damage_dealt;
    if (client->command.buttons & RASTERFALL_CMD_CLEAR_STATS) {
        g->kills = 0;
        g->special_kills = 0;
        g->damage_dealt = 0;
        g->throwable_damage_dealt = 0;
    }
    g->fire_seq = actor->fire_seq;
    /* Remote weapon simulation must start from this actor's animation.  The
     * game object is temporarily reused for weapon logic, but its animation
     * must not inherit the host player or a previously processed client. */
    g->animation = actor->animation;
    g->px = client->camera.x; g->pz = client->camera.z;
    rasterfall_session_step_remote_player(session, &client->camera,
                                          &client->command,
                                          actor->state != TOY_GAME_ACTOR_ALIVE,
                                          actor->ground_y + actor->airborne_y);
    actor->x = client->camera.x;
    actor->z = client->camera.z;
    toy_game_update_actor_ground(g, index);
    toy_game_update_actor_motion(g, index, 16);
    client->camera.x = actor->x;
    client->camera.z = actor->z;
    g->px = client->camera.x;
    g->pz = client->camera.z;
    if ((client->command.buttons & RASTERFALL_CMD_INTERACT) &&
        client->command.shop_request_id != client->shop_request_id &&
        !g->player_down)
        rasterfall_session_interact_remote(session, &client->camera);
    if ((client->command.buttons & RASTERFALL_CMD_FLAG) &&
        client->command.shop_request_id != client->shop_request_id)
        rasterfall_session_toggle_flag_remote(session, &client->camera,
                                               client->client_id);
    rasterfall_session_update_flag_remote(session, &client->camera,
                                          client->client_id);
    if ((client->command.buttons & RASTERFALL_CMD_SHOP) &&
        client->command.shop_request_id != client->shop_request_id) {
        rasterfall_session_shop_request(session, client->command.shop_action,
                                        client->command.shop_item,
                                        client->command.shop_arg);
        client->shop_request_id = client->command.shop_request_id;
    }
    if (client->command.buttons & (RASTERFALL_CMD_FLAG |
                                   RASTERFALL_CMD_INTERACT))
        client->shop_request_id = client->command.shop_request_id;
    memset(keys, 0, sizeof(keys));
    if (client->command.buttons & RASTERFALL_CMD_RELOAD)
        keys[TOY_GAME_KEY_RELOAD] = 1;
    if (client->command.buttons & RASTERFALL_CMD_SLOT_1)
        keys[TOY_GAME_KEY_SLOT_1] = 1;
    if (client->command.buttons & RASTERFALL_CMD_SLOT_2)
        keys[TOY_GAME_KEY_SLOT_2] = 1;
    if (client->command.buttons & RASTERFALL_CMD_SLOT_4)
        keys[TOY_GAME_KEY_SLOT_4] = 1;
    event_start = g->event_count;
    old_reloading = g->reloading;
    old_fire_seq = g->fire_seq;
    toy_game_update_weapon_held(g, keys,
        (client->command.buttons & RASTERFALL_CMD_FIRE) != 0,
        client->command.fire_held, client->camera.sy, client->camera.cy, 16);
    if (g->reloading && !old_reloading)
        toy_game_animation_set(&g->animation, TOY_GAME_ANIM_RELOAD);
    else if (g->fire_seq != old_fire_seq)
        toy_game_animation_set(&g->animation, TOY_GAME_ANIM_FIRE);
    toy_game_animation_update(&g->animation, 16);
    actor->x = client->camera.x; actor->z = client->camera.z;
    /* The temporary weapon view above belongs to this player's inventory only;
     * never copy the host player's vertical state back into this actor. */
    client->camera.y = actor->ground_y + actor->airborne_y;
    actor->sy = client->camera.sy; actor->cy = client->camera.cy;
    actor->hp = g->hp;
    actor->state = g->player_down ? TOY_GAME_ACTOR_DOWNED :
                                     TOY_GAME_ACTOR_ALIVE;
    actor->revive_progress_ms = g->player_revive_progress_ms;
    memcpy(actor->slots, g->slots, sizeof(actor->slots));
    actor->current_slot = g->current_slot;
    actor->reloading = g->reloading; actor->reload_timer_ms = g->reload_timer_ms;
    actor->fire_cooldown_ms = g->fire_cooldown_ms;
    actor->muzzle_flash_ms = g->muzzle_flash_ms;
    actor->kills = g->kills;
    actor->special_kills = g->special_kills;
    actor->damage_dealt = g->damage_dealt;
    actor->throwable_damage_dealt = g->throwable_damage_dealt;
    actor->fire_seq = g->fire_seq; actor->ray_count = g->ray_count;
    memcpy(actor->rays, g->rays, sizeof(actor->rays));
    actor->animation = g->animation;
    client->animation = g->animation;
    /* From this point on the shared legacy player state belongs to the host
     * again.  Keep the remote animation only in its actor/client records. */
    g->animation = host_animation;
    client->hp = actor->hp; client->down = actor->state == TOY_GAME_ACTOR_DOWNED;
    client->state = g->state;
    client->kills = g->kills;
    client->special_kills = g->special_kills;
    client->damage_dealt = g->damage_dealt;
    client->throwable_damage_dealt = g->throwable_damage_dealt;
    client->airborne_ms = actor->airborne_ms;
    client->airborne_y = actor->airborne_y;
    client->airborne_velocity = actor->vertical_velocity;
    client->air_x = actor->air_x;
    client->air_z = actor->air_z;
    client->slots[0] = actor->slots[0]; client->slots[1] = actor->slots[1];
    client->current_slot = actor->current_slot;
    client->reloading = actor->reloading;
    client->reload_timer_ms = actor->reload_timer_ms;
    client->weapon_switch_timer_ms = g->weapon_switch_timer_ms;
    client->muzzle_flash_ms = actor->muzzle_flash_ms;
    client->fire_seq = actor->fire_seq; client->ray_count = actor->ray_count;
    memcpy(client->rays, actor->rays, sizeof(client->rays));
    if (g->event_count > event_start) {
        int count = g->event_count - event_start;
        if (count > TOY_GAME_MAX_EVENTS) count = TOY_GAME_MAX_EVENTS;
        net_queue_remote_events(net, g->events + event_start, count,
                                client->client_id, client->camera.x,
                                client->camera.z);
        g->event_count = event_start;
    }
    memcpy(g->slots, host_slots, sizeof(g->slots));
    memcpy(g->rays, host_rays, sizeof(g->rays));
    g->px = host_px; g->pz = host_pz; g->hp = host_hp;
    g->player_down = host_down; g->player_revive_progress_ms = host_revive;
    g->current_slot = host_current; g->reloading = host_reload;
    g->reload_timer_ms = host_reload_timer; g->fire_cooldown_ms = host_cooldown;
    g->weapon_switch_timer_ms = host_weapon_switch;
    g->muzzle_flash_ms = host_muzzle; g->damage_flash_ms = host_damage;
    g->kills = host_kills; g->special_kills = host_special_kills;
    g->damage_dealt = host_damage_dealt;
    g->throwable_damage_dealt = host_throwable_damage_dealt;
    g->ray_count = host_ray_count;
    g->fire_seq = host_fire_seq;
}

static int net_find_down_target(struct rasterfall_net *net,
                                struct rasterfall_session *session,
                                const struct camera *rescuer,
                                int rescuer_id)
{
    int target = -1;
    long best = 0;
    int i;
    if (rescuer_id != 0 && session->game_state.player_down) {
        long dx = (long)rescuer->x - session->game_state.px;
        long dz = (long)rescuer->z - session->game_state.pz;
        long d2 = dx * dx + dz * dz;
        if (d2 <= (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE) {
            target = 0; best = d2;
        }
    }
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *other = &net->clients[i];
        long dx, dz, d2;
        if (other->client_id == rescuer_id || !other->active ||
            !other->connected || !other->down) continue;
        dx = (long)rescuer->x - other->camera.x;
        dz = (long)rescuer->z - other->camera.z;
        d2 = dx * dx + dz * dz;
        if (d2 <= (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE &&
            (target < 0 || d2 < best)) { target = other->client_id; best = d2; }
    }
    return target;
}

static const struct camera *net_rescue_target_camera(
    const struct rasterfall_net *net, const struct rasterfall_session *session,
    int target_id)
{
    int i;
    if (target_id == 0) return NULL;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
        if (net->clients[i].client_id == target_id)
            return &net->clients[i].camera;
    (void)session;
    return NULL;
}

static int net_target_is_down(const struct rasterfall_net *net,
                              const struct rasterfall_session *session,
                              int target_id)
{
    int i;
    if (target_id == 0) return session->game_state.player_down;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
        if (net->clients[i].client_id == target_id)
            return net->clients[i].active && net->clients[i].connected &&
                   net->clients[i].down;
    return 0;
}

static void net_finish_rescue(struct rasterfall_net *net,
                              struct rasterfall_session *session,
                              int target_id)
{
    int i;
    if (target_id == 0) {
        session->game_state.player_down = 0;
        session->game_state.hp = TOY_GAME_REVIVE_HP;
        session->game_state.player_revive_progress_ms = 0;
        toy_game_animation_set(&session->game_state.animation,
                               TOY_GAME_ANIM_REVIVE);
    } else for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *target = &net->clients[i];
        struct toy_game_actor *actor;
        int actor_index;
        if (target->client_id != target_id) continue;
        target->down = 0;
        target->hp = TOY_GAME_REVIVE_HP;
        target->revive_progress_ms = 0;
        actor_index = toy_game_set_remote_player(&session->game_state,
            target_id, 1, target->camera.x, target->camera.z, "PLAYER");
        if (actor_index >= 0) {
            actor = &session->game_state.actors[actor_index];
            actor->state = TOY_GAME_ACTOR_ALIVE;
            actor->hp = TOY_GAME_REVIVE_HP;
            actor->revive_progress_ms = 0;
        }
    }
    net_push_event(&session->game_state, TOY_GAME_EV_REVIVE);
    net_push_event(&session->game_state, TOY_GAME_EV_ACTOR_REVIVE);
}

static void net_apply_extra_rescue_actions(struct rasterfall_net *net,
                                           struct rasterfall_session *session)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *rescuer = &net->clients[i];
        struct camera host_target_camera;
        int target_id;
        const struct camera *target_camera;
        int revive;
        if (!rescuer->active || !rescuer->connected || rescuer->down ||
            !rescuer->command_ready) continue;
        if (rescuer->ai_revive_active) {
            int ai_index = rasterfall_session_find_down_ai(
                session, &rescuer->camera);
            if (ai_index < 0 || ai_index != rescuer->ai_revive_actor_index) {
                rescuer->ai_revive_active = 0;
                rescuer->ai_revive_actor_index = -1;
            } else {
                revive = rasterfall_session_revive_remote(
                    session, &rescuer->camera, 16);
                if (revive < 0 || revive > 0) {
                    rescuer->ai_revive_active = 0;
                    rescuer->ai_revive_actor_index = -1;
                }
            }
        }
        if (!rescuer->ai_revive_active &&
            (rescuer->command.buttons & RASTERFALL_CMD_INTERACT)) {
            int ai_index = rasterfall_session_find_down_ai(
                session, &rescuer->camera);
            if (ai_index >= 0) {
                rescuer->ai_revive_active = 1;
                rescuer->ai_revive_actor_index = ai_index;
                if (rasterfall_session_revive_remote(session,
                                                     &rescuer->camera, 16)) {
                    rescuer->ai_revive_active = 0;
                    rescuer->ai_revive_actor_index = -1;
                }
            }
        }
        target_id = rescuer->revive_target_id;
        if (rescuer->local_revive_active &&
            (!net_target_is_down(net, session, target_id) ||
             (target_id != 0 &&
              !(target_camera = net_rescue_target_camera(net, session,
                                                          target_id))))) {
            rescuer->local_revive_active = 0;
            rescuer->local_revive_progress_ms = 0;
            rescuer->revive_target_id = -1;
        }
        if (!rescuer->local_revive_active &&
            (rescuer->command.buttons & RASTERFALL_CMD_INTERACT)) {
            target_id = net_find_down_target(net, session, &rescuer->camera,
                                             rescuer->client_id);
            target_camera = net_rescue_target_camera(net, session, target_id);
            if (target_id >= 0 && (target_id == 0 || target_camera)) {
                rescuer->revive_target_id = target_id;
                rescuer->local_revive_progress_ms = 0;
                rescuer->local_revive_active = 1;
            }
        }
        if (!rescuer->local_revive_active) continue;
        target_id = rescuer->revive_target_id;
        target_camera = net_rescue_target_camera(net, session, target_id);
        if (target_id == 0) {
            memset(&host_target_camera, 0, sizeof(host_target_camera));
            host_target_camera.x = session->game_state.px;
            host_target_camera.z = session->game_state.pz;
            host_target_camera.cy = 1024;
            host_target_camera.pitch_cy = 1024;
            target_camera = &host_target_camera;
        }
        if (!target_camera) continue;
        revive = rasterfall_session_revive_player(
            session, &rescuer->camera, target_camera,
            &rescuer->local_revive_progress_ms, 16);
        if (revive < 0) {
            rescuer->local_revive_active = 0;
            rescuer->local_revive_progress_ms = 0;
            rescuer->revive_target_id = -1;
        } else if (revive > 0) {
            net_finish_rescue(net, session, target_id);
            rescuer->local_revive_active = 0;
            rescuer->local_revive_progress_ms = 0;
            rescuer->revive_target_id = -1;
        }
        if (rescuer->local_revive_active) {
            rescuer->revive_progress_ms = rescuer->local_revive_progress_ms;
            if (target_id == 0) session->game_state.player_revive_progress_ms =
                rescuer->local_revive_progress_ms;
            else if (target_id > 0 &&
                     target_id <= RASTERFALL_NET_CLIENT_MAX)
                net->clients[target_id - 1].revive_progress_ms =
                    rescuer->local_revive_progress_ms;
        }
    }
}

void rasterfall_net_apply_clients(struct rasterfall_net *net,
                                  struct rasterfall_session *session,
                                  struct camera *host_camera)
{
    int i;
    if (!net || !session || net->mode != RASTERFALL_NET_HOST) return;
    (void)host_camera;
    net->tick++;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *client = &net->clients[i];
        struct rasterfall_net_input *next;
        uint32_t next_sequence;
        if (!client->active || !client->connected) continue;
        next_sequence = client->last_processed_input_sequence + 1U;
        next = &client->input_queue[
            next_sequence % RASTERFALL_NET_INPUT_HISTORY];
        if (next->valid && next->sequence == next_sequence) {
            client->command = next->command;
            client->command_ready = 1;
            next->valid = 0;
            if (client->input_queue_depth > 0) client->input_queue_depth--;
            client->last_input_tick = net->tick;
            client->input_gap_ticks = 0;
        } else if (client->input_queue_depth > 0 &&
                   ++client->input_gap_ticks >= 8) {
            /* A burst longer than the redundancy window must not stall this
             * client forever.  Simulate exactly one missing tick from held
             * state, stripping every edge action, then continue in order. */
            client->command.turn = 0;
            client->command.pitch = 0;
            client->command.move_forward = 0;
            client->command.move_strafe = 0;
            client->command.fire_held = 0;
            client->command.buttons = 0;
            client->command.shop_action = 0;
            client->command.shop_request_id = 0;
            client->command_ready = 1;
            client->input_gap_ticks = 0;
            net->input_synthesized++;
        }
        if (client->command.buttons & RASTERFALL_CMD_RESET) {
            client->command.buttons = 0;
            client->command.fire_held = 0;
            client->command_ready = 0;
            continue;
        }
        if (net->tick - client->last_input_tick <= NET_INPUT_HOLD_TICKS)
            net_apply_client(net, session, client);
        else {
            /* A disconnected/stalled client must not keep walking using the
             * last command while still receiving authoritative damage. */
            client->command.move_forward = 0;
            client->command.move_strafe = 0;
            client->command.fire_held = 0;
        }
        if (client->command_ready)
            client->last_processed_input_sequence = next_sequence;
    }
    net_apply_extra_rescue_actions(net, session);
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        net->clients[i].command_ready = 0;
        net->clients[i].command.turn = 0;
        net->clients[i].command.pitch = 0;
        net->clients[i].command.buttons = 0;
    }
}

int rasterfall_net_pipeline_test(void)
{
    struct rasterfall_net net;
    struct rasterfall_net_client *client;
    unsigned char packet[NET_HEADER_SIZE + NET_INPUT_SIZE];
    unsigned char *p = packet + NET_HEADER_SIZE;
    struct rasterfall_net_input inputs[3];
    uint32_t expected;
    int i;
    rasterfall_net_init(&net); net.mode = RASTERFALL_NET_HOST;
    client = &net.clients[0]; client->active = client->connected = 1;
    client->client_id = 1; client->last_processed_input_sequence = 99;
    memset(inputs, 0, sizeof(inputs));
    for (i = 0; i < 3; i++) {
        inputs[i].valid = 1; inputs[i].sequence = 102U - (uint32_t)i;
        inputs[i].tick = inputs[i].sequence;
        inputs[i].command.move_forward = 1;
        if (inputs[i].sequence == 101) inputs[i].command.buttons =
            RASTERFALL_CMD_FIRE | RASTERFALL_CMD_JUMP;
    }
    memset(packet, 0, sizeof(packet));
    if (packet_begin(packet, RASTERFALL_NET_INPUT, NET_INPUT_SIZE, 102, 0) < 0)
        return 1;
    p[0] = 3;
    for (i = 0; i < 3; i++)
        encode_input_entry(p + NET_INPUT_META_SIZE + i * NET_INPUT_ENTRY_SIZE,
                           &inputs[i]);
    net_receive_client_packet(&net, client, packet, RASTERFALL_NET_INPUT,
                              NET_INPUT_SIZE, 102, 0);
    net_receive_client_packet(&net, client, packet, RASTERFALL_NET_INPUT,
                              NET_INPUT_SIZE, 102, 0);
    if (client->input_queue_depth != 3 || net.input_duplicates != 3 ||
        net.input_recovered < 2) return 2;
    for (expected = 100; expected <= 102; expected++) {
        struct rasterfall_net_input *entry = &client->input_queue[
            expected % RASTERFALL_NET_INPUT_HISTORY];
        if (!entry->valid || entry->sequence != expected) return 3;
        if (expected == 101 && entry->command.buttons !=
            (RASTERFALL_CMD_FIRE | RASTERFALL_CMD_JUMP)) return 4;
        entry->valid = 0; client->input_queue_depth--;
        client->last_processed_input_sequence = expected;
    }
    if (client->last_processed_input_sequence != 102 ||
        client->input_queue_depth != 0) return 5;
    /* Wrap-safe ordering is part of both duplicate rejection and ack. */
    if (!sequence_after(1U, 0xffffffffU) ||
        !sequence_before_or_equal(0xffffffffU, 1U)) return 6;
    /* Player movement remains usable when an unrelated entity chunk is lost. */
    {
        unsigned char player_packet[NET_PLAYER_SNAPSHOT_BASE +
            RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_COMPACT_SIZE];
        struct camera camera;
        struct toy_game_slot slots[TOY_GAME_WEAPON_SLOTS];
        struct toy_game_animation_state animation;
        struct toy_game_ray ray;
        struct rasterfall_net_player decoded;
        memset(player_packet, 0, sizeof(player_packet));
        memset(&camera, 0, sizeof(camera)); memset(slots, 0, sizeof(slots));
        memset(&animation, 0, sizeof(animation));
        slots[3].weapon = TOY_GAME_WEAPON_PILL;
        slots[3].mag = 7; slots[3].reserve = 0;
        camera.x = 400; put_u32(player_packet, 7); player_packet[4] = 4;
        for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++)
            encode_player_compact(player_packet + NET_PLAYER_SNAPSHOT_BASE +
                i * NET_PLAYER_COMPACT_SIZE, i, 1, &camera, 100, -1,
                TOY_GAME_PLAYING, 0, 0, slots,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, NULL, 0, 0, 0, 0, 0,
                i == 1 ? 102 : 0, 0,
                &animation);
        memset(&ray, 0, sizeof(ray));
        ray.sy = 12; ray.cy = 1012; ray.ex = 1234; ray.ez = -5678;
        encode_player_compact(player_packet + NET_PLAYER_SNAPSHOT_BASE,
                              0, 1, &camera, 100, -1, TOY_GAME_PLAYING, 0,
                              0, slots, 0, 0, 0, 0, 0, 0, 0, 0, 0, 77, 1, &ray,
                              0, 0, 0, 0, 0, 0, 0, &animation);
        if (decode_player_compact(player_packet + NET_PLAYER_SNAPSHOT_BASE,
                                  &decoded) < 0 ||
            decoded.ray_count != 1 ||
            decoded.rays[0].ex != 1234 || decoded.rays[0].ez != -5678 ||
            decoded.slot_weapon[3] != TOY_GAME_WEAPON_PILL ||
            decoded.mag[3] != 7)
            return 7;
        net.local_player_id = 1; net.snapshot_ready = 0;
        if (decode_player_snapshot(player_packet, sizeof(player_packet),
                                   &net) < 0 || !net.snapshot_ready ||
            net.players[1].input_ack != 102) return 8;
    }
    /* Fixed-delay interpolation is render-only and does not alter the latest
     * authoritative player camera. */
    {
        long now = net_monotonic_ms();
        net.local_player_id = 1; net.remote_sample_count[2] = 2;
        net.remote_samples[2][0].valid = net.remote_samples[2][1].valid = 1;
        net.remote_samples[2][0].received_ms = now - 150;
        net.remote_samples[2][1].received_ms = now - 50;
        net.remote_samples[2][0].camera.x = 0;
        net.remote_samples[2][1].camera.x = 100;
        net.players[2].camera.x = 100;
        rasterfall_net_update_presentation(&net, 0);
        if (net.remote_render_camera[2].x < 40 ||
            net.remote_render_camera[2].x > 60 ||
            net.players[2].camera.x != 100) return 9;
    }
    return 0;
}

void rasterfall_net_sync_clients(struct rasterfall_net *net,
                                 struct toy_game *game)
{
    int i, index;
    if (!net || !game || net->mode != RASTERFALL_NET_HOST) return;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        struct rasterfall_net_client *client = &net->clients[i];
        struct toy_game_actor *actor;
        if (!client->active || !client->connected) {
            toy_game_set_remote_player(game, i + 1, 0, 0, 0, NULL);
            continue;
        }
        index = toy_game_set_remote_player(game, client->client_id, 1,
                                           client->camera.x, client->camera.z,
                                           "PLAYER");
        if (index < 0) continue;
        actor = &game->actors[index];
        actor->sy = client->camera.sy;
        actor->cy = client->camera.cy;
        actor->hp = client->hp;
        actor->state = client->down ? TOY_GAME_ACTOR_DOWNED :
                                      TOY_GAME_ACTOR_ALIVE;
        actor->airborne_ms = client->airborne_ms;
        actor->airborne_y = client->airborne_y;
        actor->vertical_velocity = client->airborne_velocity;
        actor->air_x = client->air_x;
        actor->air_z = client->air_z;
        memcpy(actor->slots, client->slots, sizeof(actor->slots));
        actor->current_slot = client->current_slot;
    }
}

void rasterfall_net_prepare_host_step(struct rasterfall_net *net,
                                      struct toy_game *game)
{
    int i;
    if (!net || !game || net->mode != RASTERFALL_NET_HOST) return;
    game->network_rescuer_available = 0;
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
        if (net->clients[i].active && net->clients[i].connected &&
            !net->clients[i].down)
            game->network_rescuer_available = 1;
    rasterfall_net_sync_clients(net, game);
}

void rasterfall_net_apply_local_rescue(struct rasterfall_net *net,
                                       struct rasterfall_session *session,
                                       const struct camera *host_camera,
                                       int interact_pressed, int dt_ms)
{
    struct rasterfall_net_client *target;
    const struct camera *target_camera;
    int revive;
    if (!net || !session || !host_camera || net->mode != RASTERFALL_NET_HOST)
        return;
    if (net->host_revive_active &&
        !net_target_is_down(net, session, net->host_revive_target_id)) {
        net->host_revive_active = 0;
        net->host_revive_target_id = -1;
        net->host_revive_progress_ms = 0;
    }
    if (!net->host_revive_active && interact_pressed) {
        int id = net_find_down_target(net, session, host_camera, 0);
        if (id > 0) {
            net->host_revive_active = 1;
            net->host_revive_target_id = id;
            net->host_revive_progress_ms = 0;
        }
    }
    if (!net->host_revive_active) return;
    target = &net->clients[net->host_revive_target_id - 1];
    target_camera = &target->camera;
    revive = rasterfall_session_revive_player(
        session, host_camera, target_camera, &net->host_revive_progress_ms,
        dt_ms);
    if (revive < 0) {
        target->revive_progress_ms = 0;
        net->host_revive_active = 0;
        net->host_revive_target_id = -1;
        net->host_revive_progress_ms = 0;
    } else if (revive > 0) {
        int id = net->host_revive_target_id;
        net_finish_rescue(net, session, id);
        net->host_revive_active = 0;
        net->host_revive_target_id = -1;
        net->host_revive_progress_ms = 0;
    } else {
        target->revive_progress_ms = net->host_revive_progress_ms;
    }
}

void rasterfall_net_reset_host(struct rasterfall_net *net)
{
    if (!net || net->mode != RASTERFALL_NET_HOST) return;
    net_reset_clients(net);
    net->reliable_event_count = 0;
    net->reliable_event_next_id = 0;
    net->local_event_scan_count = 0;
    net->reliable_event_last_send_ms = 0;
}

static void net_smooth_client_enemies(struct rasterfall_net *net,
                                      struct rasterfall_session *session)
{
    int i;
    if (!net || !session) return;
    for (i = 0; i < net->enemy_count; i++) {
        const struct rasterfall_net_enemy *src = &net->enemies[i];
        struct toy_game_enemy *dst;
        int dx, dz;
        long dist2;
        if (src->index < 0 || src->index >= TOY_GAME_MAX_ENEMIES) continue;
        dst = &session->game_state.enemies[src->index];
        if (!src->active || !dst->active) continue;
        dx = src->x - dst->x;
        dz = src->z - dst->z;
        dist2 = (long)dx * dx + (long)dz * dz;
        /* Large corrections are teleports/charges and must remain immediate.
         * Ordinary movement is spread across render frames, hiding both the
         * 15 Hz snapshot cadence and an occasional missing snapshot. */
        if (dist2 > 1200L * 1200L) {
            dst->x = src->x;
            dst->z = src->z;
        } else {
            int step_x = dx / 3;
            int step_z = dz / 3;
            if (!step_x && dx) step_x = dx > 0 ? 1 : -1;
            if (!step_z && dz) step_z = dz > 0 ? 1 : -1;
            dst->x += step_x;
            dst->z += step_z;
        }
    }
}

void rasterfall_net_reconcile_client(struct rasterfall_net *net,
                                     struct rasterfall_session *session,
                                     struct camera *camera)
{
    const struct rasterfall_net_player *own;
    long dx, dz, dist2;
    int old_x, old_z, old_y, old_down, old_airborne;
    struct toy_game_animation_state presentation_animation;
    int presentation_muzzle;
    if (net->mode != RASTERFALL_NET_CLIENT) return;
    if (!net->snapshot_ready) {
        net_smooth_client_enemies(net, session);
        return;
    }
    if (net->local_player_id < 0 ||
        net->local_player_id >= RASTERFALL_NET_PLAYER_MAX)
        return;
    own = &net->players[net->local_player_id];
    if (!own->active) return;
    old_x = camera->x; old_z = camera->z; old_y = camera->y;
    old_down = session ? session->game_state.player_down : 0;
    old_airborne = session ? session->game_state.player_airborne_ms : 0;
    presentation_animation = session ? session->game_state.animation : own->animation;
    presentation_muzzle = session ? session->game_state.muzzle_flash_ms : 0;
    net->last_snapshot_input_ack = own->input_ack;
    if (session) {
        int seen[TOY_GAME_MAX_ACTORS];
        int i;
        memset(seen, 0, sizeof(seen));
        for (i = 0; i < net->actor_count; i++) {
            const struct rasterfall_net_actor *src = &net->actors[i];
            struct toy_game_actor *dst;
            int index = src->actor_index;
            if (index < 0 || index >= TOY_GAME_MAX_ACTORS) continue;
            dst = &session->game_state.actors[index];
            dst->actor_id = index + 1;
            dst->active = src->active;
            dst->kind = index >= TOY_GAME_REMOTE_ACTOR_BASE ?
                        TOY_GAME_ACTOR_PLAYER : TOY_GAME_ACTOR_AI;
            dst->class_id = src->class_id;
            dst->state = src->state;
            dst->x = src->x; dst->z = src->z;
            dst->sy = src->sy; dst->cy = src->cy;
            dst->hp = src->hp;
            dst->kills = src->kills;
            dst->special_kills = src->special_kills;
    dst->damage_dealt = src->damage_dealt;
            dst->throwable_damage_dealt = src->throwable_damage_dealt;
            dst->hired = src->hired;
            memcpy(dst->name, src->name, TOY_GAME_MAX_NAME);
            dst->muzzle_flash_ms = src->muzzle_flash_ms;
            dst->fire_seq = src->fire_seq;
            dst->airborne_ms = src->airborne_ms;
            dst->airborne_y = src->airborne_y;
            dst->animation.id = src->animation.id;
            dst->animation.time_ms = src->animation.time_ms;
            dst->revive_progress_ms = src->revive_progress_ms;
            dst->ray_count = src->ray_count;
            memcpy(dst->rays, src->rays, sizeof(dst->rays));
            if (dst->current_slot < 0 ||
                dst->current_slot >= TOY_GAME_WEAPON_SLOTS)
                dst->current_slot = 0;
            dst->slots[dst->current_slot].weapon = src->weapon;
            seen[index] = 1;
        }
        if (net->entity_actor_complete)
            for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
                if (!seen[i]) session->game_state.actors[i].active = 0;
        /* Missing independent entity chunks retain their last usable values;
         * explicit inactive tombstones in a later chunk clear them. */
        if (session->game_state.base_actor_index >= 0 &&
            session->game_state.base_actor_index < TOY_GAME_MAX_ACTORS) {
            const struct toy_game_actor *base =
                &session->game_state.actors[session->game_state.base_actor_index];
            if (base->active &&
                (base->state != TOY_GAME_ACTOR_ALIVE || base->hp <= 0))
                session->game_state.state = TOY_GAME_OVER;
        }
        if (session->ai_revive_active) {
            int index = session->ai_revive_actor_index;
            if (index < 0 || index >= TOY_GAME_MAX_ACTORS ||
                !session->game_state.actors[index].active ||
                session->game_state.actors[index].state !=
                    TOY_GAME_ACTOR_DOWNED)
                session->ai_revive_active = 0;
        }
        if (seen[0]) {
            const struct toy_game_actor *src = &session->game_state.actors[0];
            /* Compatibility mirror for the current HUD and renderer. */
            session->game_state.ai_active = src->active;
            session->game_state.ai_x = src->x;
            session->game_state.ai_z = src->z;
            session->game_state.ai_sy = src->sy;
            session->game_state.ai_cy = src->cy;
            session->game_state.ai_hp = src->hp;
            session->game_state.ai_down = src->state == TOY_GAME_ACTOR_DOWNED;
            session->game_state.ai_muzzle_flash_ms = src->muzzle_flash_ms;
            session->game_state.ai_fire_seq = src->fire_seq;
        }
        session->game_state.hp = own->hp;
        session->game_state.player_down = own->downed;
        /* Local viewmodel presentation is immediate and is not rewound by a
         * routine authoritative snapshot. */
        session->game_state.player_revive_progress_ms = own->revive_progress_ms;
        session->game_state.kills = own->kills;
        session->game_state.special_kills = own->special_kills;
        session->game_state.damage_dealt = own->damage_dealt;
        session->game_state.throwable_damage_dealt = own->throwable_damage_dealt;
        session->game_state.state = own->state;
        session->game_state.current_slot = own->current_slot;
        session->game_state.slots[0].weapon = own->slot_weapon[0];
        session->game_state.slots[1].weapon = own->slot_weapon[1];
        session->game_state.slots[2].weapon = own->slot_weapon[2];
        session->game_state.slots[3].weapon = own->slot_weapon[3];
        session->game_state.slots[0].mag = own->mag[0];
        session->game_state.slots[0].reserve = own->reserve[0];
        session->game_state.slots[1].mag = own->mag[1];
        session->game_state.slots[1].reserve = own->reserve[1];
        session->game_state.slots[2].mag = own->mag[2];
        session->game_state.slots[2].reserve = own->reserve[2];
        session->game_state.slots[3].mag = own->mag[3];
        session->game_state.slots[3].reserve = own->reserve[3];
        session->game_state.reloading = own->reloading;
        session->game_state.reload_timer_ms = own->reload_timer_ms;
        session->game_state.wave = net->snapshot_world_wave;
        session->game_state.to_spawn = net->snapshot_world_to_spawn;
        session->game_state.spawn_timer_ms = net->snapshot_world_spawn_timer_ms;
        session->game_state.enemies_alive = net->snapshot_world_enemies_alive;
        session->game_state.campaign_phase = net->snapshot_world_phase;
        session->game_state.phase_timer_ms = net->snapshot_world_phase_timer_ms;
        session->game_state.alarm_timer_ms = net->snapshot_world_alarm_timer_ms;
        session->game_state.spawn_budget = net->snapshot_world_spawn_budget;
        session->game_state.active_attackers = net->snapshot_world_active_attackers;
        session->game_state.director_encounters = net->snapshot_world_director_encounters;
        session->game_state.goal_hold_ms = net->snapshot_world_goal_hold_ms;
        session->game_state.alarm_triggered = net->snapshot_world_alarm_triggered;
        session->game_state.campaign_stage = net->snapshot_world_campaign_stage;
        session->game_state.wave_attack_points = net->snapshot_world_wave_attack_points;
        session->game_state.wave_attack_multiplier =
            net->snapshot_world_wave_attack_multiplier;
        session->game_state.wave_waiting_common = net->snapshot_world_wave_waiting_common;
        session->game_state.wave_waiting_fast = net->snapshot_world_wave_waiting_fast;
        session->game_state.wave_waiting_heavy = net->snapshot_world_wave_waiting_heavy;
        session->game_state.wave_waiting_special = net->snapshot_world_wave_waiting_special;
        session->game_state.wave_waiting_tank = net->snapshot_world_wave_waiting_tank;
        session->game_state.money = net->snapshot_money;
        session->game_state.unlocked_weapons = net->snapshot_unlocked_weapons;
        memcpy(session->game_state.projectiles, net->snapshot_projectiles,
               sizeof(session->game_state.projectiles));
        memcpy(session->game_state.burn_zones, net->snapshot_burn_zones,
               sizeof(session->game_state.burn_zones));
        if (net->snapshot_flag_count <= RASTERFALL_MAX_FLAGS) {
            session->flag_count = net->snapshot_flag_count;
            for (i = 0; i < RASTERFALL_MAX_FLAGS; i++) {
                struct rasterfall_flag *dst = &session->flags[i];
                const struct rasterfall_flag *src = &net->snapshot_flags[i];
                int color = dst->color;
                char label[5];
                memcpy(label, dst->label, sizeof(label));
                dst->active = src->active;
                dst->x = src->x;
                dst->z = src->z;
                dst->carried = src->carried;
                dst->carrier_id = src->carrier_id;
                dst->color = color;
                memcpy(dst->label, label, sizeof(dst->label));
            }
            session->carried_flag = -1;
            for (i = 0; i < session->flag_count; i++)
                if (session->flags[i].carried &&
                    session->flags[i].carrier_id == net->local_player_id)
                    session->carried_flag = i;
            for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
                session->game_state.actors[i].flag_index =
                    net->snapshot_actor_flag_index[i];
        }
        /* This world flag belongs to the host player's Smoker state.  It is
         * not a property of every client; copying it here made a client
         * unable to jump whenever the host was being dragged. */
        session->game_state.player_control_disabled = 0;
        session->game_state.player_airborne_ms = own->airborne_ms;
        session->game_state.player_airborne_y = own->airborne_y;
        session->game_state.player_ground_y = own->camera.y -
                                               own->airborne_y;
        session->game_state.player_vertical_velocity =
            own->airborne_velocity;
        session->game_state.player_air_x = own->air_x;
        session->game_state.player_air_z = own->air_z;
        camera->y = own->camera.y;
        session->air_walls_enabled = net->snapshot_air_walls_enabled;
        session->manual_alarm_on = net->snapshot_manual_alarm_enabled;
        session->manual_alarm_timer = net->snapshot_world_manual_alarm_timer_ms;
        rasterfall_map_set_air_walls(&session->map_ops,
                                     session->air_walls_enabled);
    }
    if (session && net->enemy_count >= 0) {
        int i;
        uint64_t seen = net->entity_enemy_seen;
        for (i = 0; i < net->enemy_count; i++) {
            const struct rasterfall_net_enemy *src = &net->enemies[i];
            struct toy_game_enemy *dst;
            int index = src->index;
            if (index < 0 || index >= TOY_GAME_MAX_ENEMIES) continue;
            dst = &session->game_state.enemies[index];
            int old_active = dst->active;
            dst->active = src->active; dst->type = src->type;
            dst->ai_state = src->ai_state;
            dst->hp = src->hp;
            if (old_active != 1 || src->active != 1) {
                dst->x = src->x;
                dst->z = src->z;
            }
            dst->speed = src->speed; dst->bite_cooldown_ms = src->bite_cooldown_ms;
            dst->flash = src->flash; dst->hurt = src->hurt;
            dst->dying_ms = src->dying_ms;
            dst->ability.special_target_active = src->ability.special_target_active;
            dst->ability.charge_active = src->ability.charge_active;
            dst->ability.special_timer_ms = src->ability.special_timer_ms;
            dst->ability.special_windup_ms = src->ability.special_windup_ms;
            dst->ability.special_target_kind = src->ability.special_target_kind;
            dst->ability.special_target_index = src->ability.special_target_index;
            dst->ability.special_pull_timer_ms = src->ability.special_pull_timer_ms;
            dst->ability.charge_dir_x = src->ability.charge_dir_x;
            dst->ability.charge_dir_z = src->ability.charge_dir_z;
            dst->ability.charge_elapsed_ms = src->ability.charge_elapsed_ms;
            dst->airborne_ms = src->airborne_ms;
            dst->airborne_y = src->airborne_y;
            if (old_active != 1 || src->active != 1 ||
                (dst->dir_x == 0 && dst->dir_z == 0)) {
                dst->dir_x = src->dir_x;
                dst->dir_z = src->dir_z;
            } else {
                /* 快照间隔通常为三逻辑 tick；插值方向，避免量化/丢包
                 * 造成面部在相邻方向之间来回跳。 */
                dst->dir_x += (src->dir_x - dst->dir_x) / 3;
                dst->dir_z += (src->dir_z - dst->dir_z) / 3;
            }
        }
        if (net->entity_enemy_complete)
            for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++)
                if (!(seen & (1ULL << i)))
                    session->game_state.enemies[i].active = 0;
        /* Do not clear unseen ranges: an entity chunk may have been lost. */
        net_smooth_client_enemies(net, session);
    }
    /* Restore the authoritative local state at input_ack, then replay only
     * this player's still-unacknowledged commands in sequence order. */
    camera->x = own->camera.x; camera->z = own->camera.z;
    camera->y = own->camera.y; camera->sy = own->camera.sy;
    camera->cy = own->camera.cy; camera->pitch_sy = own->camera.pitch_sy;
    camera->pitch_cy = own->camera.pitch_cy;
    if (session) {
        /* smooth_turn_remaining is local prediction state, not part of the
         * authoritative camera.  Replaying with the pre-reconcile remainder
         * applies each 90-degree edge a second time. */
        session->smooth_turn_remaining = 0;
        uint32_t s = own->input_ack + 1U;
        int replayed = 0;
        session->game_state.px = camera->x;
        session->game_state.pz = camera->z;
        while (sequence_before_or_equal(s, net->last_command_sequence) &&
               replayed < RASTERFALL_NET_INPUT_HISTORY) {
            struct rasterfall_net_input *entry =
                &net->input_history[s % RASTERFALL_NET_INPUT_HISTORY];
            if (!entry->valid || entry->sequence != s) break;
            rasterfall_session_replay_client(session, camera,
                                              &entry->command, 16);
            replayed++; s++;
        }
        session->game_state.animation = presentation_animation;
        session->game_state.muzzle_flash_ms = presentation_muzzle;
        for (int i = 0; i < RASTERFALL_NET_INPUT_HISTORY; i++)
            if (net->input_history[i].valid &&
                sequence_before_or_equal(net->input_history[i].sequence,
                                         own->input_ack))
                net->input_history[i].valid = 0;
    }
    dx = (long)old_x - camera->x; dz = (long)old_z - camera->z;
    dist2 = dx * dx + dz * dz;
    net->reconciliation_count++;
    {
        unsigned long magnitude = (unsigned long)(dx < 0 ? -dx : dx) +
                                  (unsigned long)(dz < 0 ? -dz : dz);
        net->reconciliation_total += magnitude;
        if (magnitude > net->reconciliation_max)
            net->reconciliation_max = magnitude;
    }
    if (dist2 > 900L * 900L || old_down != own->downed ||
        (old_airborne == 0) != (own->airborne_ms == 0) ||
        own->special_motion) {
        net->correction_x = net->correction_z = net->correction_y = 0;
        net->correction_remaining_ms = 0;
        net->reconciliation_hard_snaps++;
    } else if (dist2 > 8L * 8L) {
        net->correction_x += (int)dx;
        net->correction_z += (int)dz;
        net->correction_y += old_y - camera->y;
        net->correction_remaining_ms = 120;
    }
    net->snapshot_ready = 0;
}

static int net_lerp(int a, int b, long numerator, long denominator)
{
    if (denominator <= 0) return b;
    if (numerator < 0) numerator = 0;
    if (numerator > denominator) numerator = denominator;
    return a + (int)(((long)(b - a) * numerator) / denominator);
}

void rasterfall_net_update_presentation(struct rasterfall_net *net, int dt_ms)
{
    long target = net_monotonic_ms() - NET_INTERPOLATION_DELAY_MS;
    int id;
    if (!net) return;
    if (net->correction_remaining_ms > 0) {
        int consume = dt_ms > net->correction_remaining_ms ?
                      net->correction_remaining_ms : dt_ms;
        net->correction_x -= net->correction_x * consume /
                             net->correction_remaining_ms;
        net->correction_z -= net->correction_z * consume /
                             net->correction_remaining_ms;
        net->correction_y -= net->correction_y * consume /
                             net->correction_remaining_ms;
        net->correction_remaining_ms -= consume;
        if (!net->correction_remaining_ms)
            net->correction_x = net->correction_z = net->correction_y = 0;
    }
    for (id = 0; id < RASTERFALL_NET_PLAYER_MAX; id++) {
        struct rasterfall_net_remote_sample *s = net->remote_samples[id];
        int count = net->remote_sample_count[id], a = -1, b = -1, i;
        struct camera result;
        int airborne;
        if (id == net->local_player_id || count <= 0) continue;
        for (i = 0; i < count - 1; i++)
            if (target >= s[i].received_ms && target <= s[i + 1].received_ms) {
                a = i; b = i + 1; break;
            }
        if (a >= 0) {
            long n = target - s[a].received_ms;
            long d = s[b].received_ms - s[a].received_ms;
            result = s[a].camera;
            result.x = net_lerp(s[a].camera.x, s[b].camera.x, n, d);
            result.z = net_lerp(s[a].camera.z, s[b].camera.z, n, d);
            result.y = net_lerp(s[a].camera.y, s[b].camera.y, n, d);
            result.sy = net_lerp(s[a].camera.sy, s[b].camera.sy, n, d);
            result.cy = net_lerp(s[a].camera.cy, s[b].camera.cy, n, d);
            result.pitch_sy = net_lerp(s[a].camera.pitch_sy,
                                       s[b].camera.pitch_sy, n, d);
            result.pitch_cy = net_lerp(s[a].camera.pitch_cy,
                                       s[b].camera.pitch_cy, n, d);
            airborne = net_lerp(s[a].airborne_y, s[b].airborne_y, n, d);
        } else if (target > s[count - 1].received_ms && count >= 2) {
            long extra = target - s[count - 1].received_ms;
            long d = s[count - 1].received_ms - s[count - 2].received_ms;
            result = s[count - 1].camera;
            airborne = s[count - 1].airborne_y;
            if (extra > NET_EXTRAPOLATION_LIMIT_MS)
                extra = NET_EXTRAPOLATION_LIMIT_MS;
            if (d > 0) {
                result.x += (int)(((long)(s[count - 1].camera.x -
                    s[count - 2].camera.x) * extra) / d);
                result.z += (int)(((long)(s[count - 1].camera.z -
                    s[count - 2].camera.z) * extra) / d);
                airborne += (int)(((long)(s[count - 1].airborne_y -
                    s[count - 2].airborne_y) * extra) / d);
                net->extrapolation_count++;
            }
        } else {
            result = s[0].camera; airborne = s[0].airborne_y;
            net->interpolation_underruns++;
        }
        net->remote_render_camera[id] = result;
        net->remote_render_airborne_y[id] = airborne;
    }
}

const struct camera *rasterfall_net_remote_render_camera(
    const struct rasterfall_net *net, int player_id, int *airborne_y)
{
    if (!net || player_id < 0 || player_id >= RASTERFALL_NET_PLAYER_MAX)
        return NULL;
    if (airborne_y) *airborne_y = net->remote_render_airborne_y[player_id];
    return &net->remote_render_camera[player_id];
}

void rasterfall_net_set_loss(struct rasterfall_net *net, int percent)
{
    if (!net) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    net->net_loss_percent = percent;
}
