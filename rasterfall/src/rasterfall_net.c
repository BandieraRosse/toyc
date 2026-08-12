#include "tlibc_everything.h"
#include "errno.h"
#include "rasterfall_net.h"

#ifdef TOYC_WINDOWS
#define net_windows_log toy_windows_log
#else
static void net_windows_log(const char *message) { (void)message; }
#endif

#define special_timer_ms ability.special_timer_ms
#define special_windup_ms ability.special_windup_ms
#define special_target_active ability.special_target_active
#define charge_active ability.charge_active
#define charge_dir_x ability.charge_dir_x
#define charge_dir_z ability.charge_dir_z
#define charge_elapsed_ms ability.charge_elapsed_ms
#define special_target_player ability.special_target_player
#define special_target_actor_index ability.special_target_actor_index
#define special_pull_timer_ms ability.special_pull_timer_ms

#define NET_HEADER_SIZE 16
#define NET_MAGIC_0 'R'
#define NET_MAGIC_1 'F'
#define NET_MAGIC_2 'N'
#define NET_MAGIC_3 '1'
#define NET_INPUT_SIZE 36
#define NET_PLAYER_BASE_SIZE 65
#define NET_PLAYER_RAY_SIZE 15
#define NET_PLAYER_SIZE (NET_PLAYER_BASE_SIZE + 4 + 1 + TOY_GAME_MAX_RAYS * NET_PLAYER_RAY_SIZE)
#define NET_ACTOR_SIZE 37
#define NET_ENEMY_SIZE 48
#define NET_EVENT_SIZE (4 + 1 + TOY_GAME_MAX_EVENTS)
#define NET_WORLD_BASE_SIZE 32
#define NET_WORLD_FLAG_SIZE 12
#define NET_WORLD_SIZE (NET_WORLD_BASE_SIZE + 4 + 4 + \
                        RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + \
                        TOY_GAME_MAX_ACTORS * 2)
#define NET_SNAPSHOT_BASE 8
#define NET_INPUT_HOLD_TICKS 15
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

static void net_ack_remote_events(struct rasterfall_net *net, uint32_t ack)
{
    int remove_count = 0;
    if (!net->remote_event_snapshot_sequence ||
        ack < net->remote_event_snapshot_sequence)
        return;
    while (remove_count < net->remote_event_queue_count &&
           net->remote_event_ids[remove_count] <= net->remote_event_snapshot_last_id)
        remove_count++;
    if (!remove_count) return;
    if (remove_count < net->remote_event_queue_count) {
        memmove(net->remote_event_queue,
                net->remote_event_queue + remove_count,
                (size_t)(net->remote_event_queue_count - remove_count));
        memmove(net->remote_event_ids,
                net->remote_event_ids + remove_count,
                (size_t)(net->remote_event_queue_count - remove_count) *
                    sizeof(net->remote_event_ids[0]));
    }
    net->remote_event_queue_count -= remove_count;
}

static void net_drop_reliable_events(struct rasterfall_net *net)
{
    uint32_t minimum = 0xffffffffU;
    int i, remove_count = 0;
    int consumers = 0;
    if (net->peer_known && net->connected) {
        minimum = net->peer_reliable_event_ack;
        consumers = 1;
    }
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
        struct rasterfall_net_remote *remote = &net->remotes[i];
        if (!remote->active || !remote->connected) continue;
        if (!consumers || remote->reliable_event_ack < minimum)
            minimum = remote->reliable_event_ack;
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
    if (net->peer_known && net->connected && net_send(net, packet, size) < 0)
        return -1;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
        if (net->remotes[i].active && net->remotes[i].connected &&
            !net->relay_mode)
            net_send_to(net, &net->remotes[i].address, packet, size);
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

static int same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family && a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

#define DISCOVERY_MAGIC_0 'R'
#define DISCOVERY_MAGIC_1 'F'
#define DISCOVERY_MAGIC_2 'D'
#define DISCOVERY_MAGIC_3 '1'
#define DISCOVERY_QUERY 1
#define DISCOVERY_RESPONSE 2
#define DISCOVERY_PACKET_SIZE 43

static int discovery_socket(int host)
{
    struct sockaddr_in address;
    int fd, reuse = 1;
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)(host ? RASTERFALL_NET_DISCOVERY_PORT : 0));
    address.sin_addr.s_addr = INADDR_ANY;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        __close(fd);
        return -1;
    }
    return fd;
}

void rasterfall_net_discovery_init(struct rasterfall_net_discovery *discovery)
{
    memset(discovery, 0, sizeof(*discovery));
    discovery->fd = -1;
}

int rasterfall_net_discovery_browser_start(struct rasterfall_net_discovery *discovery)
{
    int broadcast = 1;
    rasterfall_net_discovery_init(discovery);
    discovery->fd = discovery_socket(0);
    if (discovery->fd < 0) return -1;
    setsockopt(discovery->fd, SOL_SOCKET, SO_BROADCAST,
               &broadcast, sizeof(broadcast));
    discovery->mode = RASTERFALL_NET_DISCOVERY_BROWSER;
    return 0;
}

int rasterfall_net_discovery_host_start(struct rasterfall_net_discovery *discovery)
{
    rasterfall_net_discovery_init(discovery);
    discovery->fd = discovery_socket(1);
    if (discovery->fd < 0) return -1;
    discovery->mode = RASTERFALL_NET_DISCOVERY_HOST;
    return 0;
}

void rasterfall_net_discovery_close(struct rasterfall_net_discovery *discovery)
{
    if (!discovery) return;
    if (discovery->fd >= 0) __close(discovery->fd);
    rasterfall_net_discovery_init(discovery);
}

static void discovery_send_query_to(int fd, const struct sockaddr_in *destination)
{
    unsigned char packet[8];
    packet[0] = DISCOVERY_MAGIC_0; packet[1] = DISCOVERY_MAGIC_1;
    packet[2] = DISCOVERY_MAGIC_2; packet[3] = DISCOVERY_MAGIC_3;
    packet[4] = DISCOVERY_QUERY;
    packet[5] = RASTERFALL_NET_PROTOCOL_VERSION;
    put_u16(packet + 6, 0);
    sendto(fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)destination, sizeof(*destination));
}

static void discovery_send_query(struct rasterfall_net_discovery *discovery)
{
    struct sockaddr_in broadcast, loopback;
    memset(&broadcast, 0, sizeof(broadcast));
    broadcast.sin_family = AF_INET;
    broadcast.sin_port = htons(RASTERFALL_NET_DISCOVERY_PORT);
    broadcast.sin_addr.s_addr = 0xffffffffU;
    discovery_send_query_to(discovery->fd, &broadcast);
    /* 广播在部分 Linux/WSL 网络配置中不会回送到本机；本地回环查询
     * 保证同一台机器启动两个 Rasterfall 进程时也能自动发现房间。 */
    memset(&loopback, 0, sizeof(loopback));
    loopback.sin_family = AF_INET;
    loopback.sin_port = htons(RASTERFALL_NET_DISCOVERY_PORT);
    loopback.sin_addr.s_addr = inet_addr("127.0.0.1");
    discovery_send_query_to(discovery->fd, &loopback);
}

static void discovery_send_response(int fd, const struct sockaddr_in *destination,
                                    const char *room_name, int game_port,
                                    int players, int max_players, int state)
{
    unsigned char packet[DISCOVERY_PACKET_SIZE];
    int length = room_name ? (int)strlen(room_name) : 0;
    if (length > 31) length = 31;
    memset(packet, 0, sizeof(packet));
    packet[0] = DISCOVERY_MAGIC_0; packet[1] = DISCOVERY_MAGIC_1;
    packet[2] = DISCOVERY_MAGIC_2; packet[3] = DISCOVERY_MAGIC_3;
    packet[4] = DISCOVERY_RESPONSE;
    packet[5] = RASTERFALL_NET_PROTOCOL_VERSION;
    put_u16(packet + 6, (unsigned int)game_port);
    packet[8] = (unsigned char)(players < 0 ? 0 : players);
    packet[9] = (unsigned char)(max_players < 0 ? 0 : max_players);
    packet[10] = (unsigned char)(state < 0 ? 0 : state);
    if (length) memcpy(packet + 11, room_name, (size_t)length);
    sendto(fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)destination, sizeof(*destination));
}

static void discovery_store_room(struct rasterfall_net_discovery *discovery,
                                 const struct sockaddr_in *source,
                                 const unsigned char *packet)
{
    int i, slot = -1;
    long now = net_monotonic_ms();
    for (i = 0; i < RASTERFALL_NET_DISCOVERY_MAX_ROOMS; i++) {
        if (discovery->rooms[i].active &&
            same_peer(&discovery->rooms[i].address, source)) {
            slot = i;
            break;
        }
        /* 同一台主机可能同时从广播网卡和 127.0.0.1 回应；合并重复房间，
         * 并优先保留本机可直连的地址。 */
        if (discovery->rooms[i].active &&
            discovery->rooms[i].game_port == (int)get_u16(packet + 6) &&
            memcmp(discovery->rooms[i].name, packet + 11, 31) == 0) {
            slot = i;
            break;
        }
        if (slot < 0 && !discovery->rooms[i].active) slot = i;
    }
    if (slot < 0) return;
    if (!discovery->rooms[slot].active) discovery->room_count++;
    discovery->rooms[slot].active = 1;
    memcpy(&discovery->rooms[slot].address, source, sizeof(*source));
    memcpy(discovery->rooms[slot].name, packet + 11, 31);
    discovery->rooms[slot].name[31] = 0;
    discovery->rooms[slot].game_port = (int)get_u16(packet + 6);
    discovery->rooms[slot].players = packet[8];
    discovery->rooms[slot].max_players = packet[9];
    discovery->rooms[slot].state = packet[10];
    discovery->rooms[slot].last_seen_ms = now;
}

void rasterfall_net_discovery_poll(struct rasterfall_net_discovery *discovery,
                                   const char *room_name, int game_port,
                                   int players, int max_players, int state)
{
    unsigned char packet[DISCOVERY_PACKET_SIZE];
    struct sockaddr_in source;
    socklen_t source_len;
    long now;
    if (!discovery || discovery->fd < 0) return;
    now = net_monotonic_ms();
    if (discovery->mode == RASTERFALL_NET_DISCOVERY_BROWSER) {
        if (!discovery->last_query_ms || now - discovery->last_query_ms >= 1000) {
            discovery_send_query(discovery);
            discovery->last_query_ms = now;
        }
    }
    for (;;) {
        long received;
        source_len = sizeof(source);
        received = recvfrom(discovery->fd, packet, sizeof(packet), 0,
                            (struct sockaddr *)&source, &source_len);
        if (received < 0) break;
        if (received < 6 || packet[0] != DISCOVERY_MAGIC_0 ||
            packet[1] != DISCOVERY_MAGIC_1 || packet[2] != DISCOVERY_MAGIC_2 ||
            packet[3] != DISCOVERY_MAGIC_3 ||
            packet[5] != RASTERFALL_NET_PROTOCOL_VERSION) continue;
        if (discovery->mode == RASTERFALL_NET_DISCOVERY_HOST &&
            packet[4] == DISCOVERY_QUERY) {
            discovery_send_response(discovery->fd, &source, room_name,
                                     game_port, players, max_players, state);
        } else if (discovery->mode == RASTERFALL_NET_DISCOVERY_BROWSER &&
                   packet[4] == DISCOVERY_RESPONSE &&
                   received >= DISCOVERY_PACKET_SIZE) {
            discovery_store_room(discovery, &source, packet);
        }
    }
    if (discovery->mode == RASTERFALL_NET_DISCOVERY_BROWSER) {
        int i;
        for (i = 0; i < RASTERFALL_NET_DISCOVERY_MAX_ROOMS; i++) {
            if (discovery->rooms[i].active &&
                now - discovery->rooms[i].last_seen_ms > 3000) {
                discovery->rooms[i].active = 0;
                if (discovery->room_count > 0) discovery->room_count--;
            }
        }
    }
}

static int net_send(struct rasterfall_net *net, unsigned char *packet, int size)
{
    long sent;
    if (net->fd < 0 || !net->peer_known || size <= 0) return -1;
    sent = sendto(net->fd, packet, (size_t)size, 0,
                  (const struct sockaddr *)&net->peer, sizeof(net->peer));
    if (sent == size) {
        net->net_stats_tx_bytes += (unsigned long)size;
        net->net_stats_tx_packets++;
        net_stats_roll(net);
    }
    return sent == size ? 0 : -1;
}

static int net_send_to(struct rasterfall_net *net,
                       const struct sockaddr_in *address,
                       unsigned char *packet, int size)
{
    long sent;
    if (!net || !address || net->fd < 0 || size <= 0) return -1;
    sent = sendto(net->fd, packet, (size_t)size, 0,
                  (const struct sockaddr *)address, sizeof(*address));
    if (sent == size) {
        net->net_stats_tx_bytes += (unsigned long)size;
        net->net_stats_tx_packets++;
        net_stats_roll(net);
    }
    return sent == size ? 0 : -1;
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

static int net_remote_index(const struct rasterfall_net *net,
                            const struct sockaddr_in *source)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
        if (net->remotes[i].active &&
            same_peer(&net->remotes[i].address, source)) return i;
    return -1;
}

static void net_init_player_slots(struct toy_game_slot *slots,
                                  int *current_slot)
{
    if (!slots || !current_slot) return;
    memset(slots, 0, sizeof(struct toy_game_slot) * TOY_GAME_WEAPON_SLOTS);
    slots[0].weapon = -1;
    slots[1].weapon = TOY_GAME_WEAPON_PISTOL;
    slots[1].mag = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->mag_size;
    slots[1].reserve = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->reserve_max;
    *current_slot = 1;
}

static int net_remote_index_client_id(const struct rasterfall_net *net,
                                      int client_id)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
        if (net->remotes[i].active && net->remotes[i].client_id == client_id)
            return i;
    return -1;
}

static int net_alloc_remote(struct rasterfall_net *net,
                            const struct sockaddr_in *source)
{
    int i;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
        struct rasterfall_net_remote *remote = &net->remotes[i];
        if (remote->active) continue;
        memset(remote, 0, sizeof(*remote));
        remote->active = 1;
        remote->client_id = i + 2;
        remote->connected = 1;
        memcpy(&remote->address, source, sizeof(*source));
        memcpy(&remote->spawn, &net->peer_spawn, sizeof(remote->spawn));
        remote->spawn.x += remote->client_id * 350;
        memcpy(&remote->camera, &remote->spawn, sizeof(remote->camera));
        remote->hp = TOY_GAME_SECONDARY_PLAYER_HP;
        remote->state = TOY_GAME_PLAYING;
        net_init_player_slots(remote->slots, &remote->current_slot);
        return i;
    }
    return -1;
}

static int net_alloc_remote_client_id(struct rasterfall_net *net,
                                      const struct sockaddr_in *source,
                                      int client_id)
{
    int i = net_remote_index_client_id(net, client_id);
    if (i >= 0) return i;
    i = net_alloc_remote(net, source);
    if (i >= 0) net->remotes[i].client_id = client_id;
    return i;
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

static void net_reset_peer_state(struct rasterfall_net *net)
{
    memcpy(&net->peer_camera, &net->peer_spawn, sizeof(net->peer_camera));
    net_init_player_slots(net->peer_slots, &net->peer_current_slot);
    /* The first snapshot can be sent before the client has delivered its
     * first input.  Keep the remote player alive with the same initial state
     * as toy_game_init instead of exposing the zeroed transport state. */
    net->peer_hp = TOY_GAME_SECONDARY_PLAYER_HP;
    net->peer_state = TOY_GAME_PLAYING;
    net->peer_down = 0;
    net->peer_revive_progress_ms = 0;
    net->peer_reliable_event_ack = 0;
    net->peer_revive_active = 0;
    net->peer_host_revive_active = 0;
    net->peer_host_revive_progress_ms = 0;
    net->peer_revive_target_id = -1;
    net->local_revive_peer_active = 0;
    net->local_revive_peer_progress_ms = 0;
    net->peer_reloading = 0;
    net->peer_reload_timer_ms = 0;
    net->peer_fire_cooldown_ms = 0;
    net->peer_muzzle_flash_ms = 0;
    net->peer_damage_flash_ms = 0;
    net->peer_kills = 0;
    net->peer_special_kills = 0;
    net->peer_damage_dealt = 0;
    net->peer_fire_seq = 0;
    net->peer_ray_count = 0;
    net->peer_state_initialized = 0;
    net->peer_camera_initialized = 0;
    net->peer_reported_camera_ready = 0;
    net->peer_last_receive_ms = 0;
    net->remote_command_ready = 0;
    net->last_input_sequence = 0;
    net->remote_event_queue_count = 0;
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
    if (spawn) {
        memcpy(&net->peer_camera, spawn, sizeof(struct camera));
        memcpy(&net->peer_spawn, spawn, sizeof(struct camera));
    }
    return 0;
}

int rasterfall_net_connect(struct rasterfall_net *net, const char *ip, int port)
{
    unsigned char hello[NET_HEADER_SIZE];
    int size;
    rasterfall_net_init(net);
    net->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (net->fd < 0) return -1;
    memset(&net->peer, 0, sizeof(net->peer));
    net->peer.sin_family = AF_INET;
    net->peer.sin_port = htons((unsigned short)port);
    net->peer.sin_addr.s_addr = inet_addr(ip);
    if (net->peer.sin_addr.s_addr == 0xffffffffU) {
        __close(net->fd);
        net->fd = -1;
        return -1;
    }
    net->peer_known = 1;
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
    if (spawn) {
        memcpy(&net->peer_spawn, spawn, sizeof(*spawn));
        memcpy(&net->peer_camera, spawn, sizeof(*spawn));
    }
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
    if (net->fd >= 0) __close(net->fd);
    rasterfall_net_init(net);
}

static int encode_command(unsigned char *packet, uint32_t sequence,
                          uint32_t ack, uint32_t tick,
                          const struct rasterfall_command *command,
                          const struct camera *predicted)
{
    unsigned char *p = packet + NET_HEADER_SIZE;
    int size = packet_begin(packet, RASTERFALL_NET_INPUT, NET_INPUT_SIZE,
                            sequence, ack);
    if (size < 0) return size;
    put_u32(p, tick);
    p[4] = put_i8_value(command->move_forward);
    p[5] = put_i8_value(command->move_strafe);
    p[6] = (unsigned char)(command->fire_held != 0);
    p[7] = 0;
    put_i16(p + 8, command->turn);
    put_i16(p + 10, command->pitch);
    put_u16(p + 12, command->buttons);
    put_u16(p + 14, 0);
    put_u32(p + 16, (uint32_t)predicted->x);
    put_u32(p + 20, (uint32_t)predicted->z);
    put_i16(p + 24, predicted->sy);
    put_i16(p + 26, predicted->cy);
    put_i16(p + 28, predicted->pitch_sy);
    put_i16(p + 30, predicted->pitch_cy);
    put_u32(p + 32, 0);
    p[7] = (unsigned char)(command->shop_item < 0 ? 0 : command->shop_item);
    put_u16(p + 14, (unsigned int)command->shop_action);
    return size;
}

static int decode_command(const unsigned char *payload, int size,
                          struct rasterfall_command *command)
{
    if (size != NET_INPUT_SIZE) return -1;
    memset(command, 0, sizeof(struct rasterfall_command));
    command->move_forward = get_i8_value(payload[4]);
    command->move_strafe = get_i8_value(payload[5]);
    command->fire_held = payload[6] != 0;
    command->turn = get_i16(payload + 8);
    command->pitch = get_i16(payload + 10);
    command->buttons = get_u16(payload + 12);
    command->shop_item = payload[7];
    command->shop_action = get_u16(payload + 14);
    if (command->move_forward < -1 || command->move_forward > 1 ||
        command->move_strafe < -1 || command->move_strafe > 1) return -1;
    if (command->turn < -1024 || command->turn > 1024 ||
        command->pitch < -1024 || command->pitch > 1024) return -1;
    return 0;
}

static void decode_command_camera(const unsigned char *payload,
                                  struct camera *camera)
{
    camera->x = (int)get_u32(payload + 16);
    camera->z = (int)get_u32(payload + 20);
    camera->sy = get_i16(payload + 24);
    camera->cy = get_i16(payload + 26);
    camera->pitch_sy = get_i16(payload + 28);
    camera->pitch_cy = get_i16(payload + 30);
    camera->y = 0;
}

int rasterfall_net_send_command(struct rasterfall_net *net,
                                const struct rasterfall_command *command,
                                const struct camera *predicted)
{
    unsigned char packet[NET_HEADER_SIZE + NET_INPUT_SIZE];
    int size;
    if (net->mode != RASTERFALL_NET_CLIENT) return -1;
    net->tick++;
    size = encode_command(packet, ++net->send_sequence,
                          net->receive_sequence, net->tick, command, predicted);
    put_u32(packet + NET_HEADER_SIZE + 32, net->reliable_event_ack);
    net->last_command_sequence = net->send_sequence;
    if (command->buttons & RASTERFALL_CMD_JUMP)
        net->last_jump_command_sequence = net->send_sequence;
    net->last_command_sent_ms = net_monotonic_ms();
    return net_send(net, packet, size);
}

static void encode_player(unsigned char *p, int id, int active,
                          const struct camera *camera, int hp,
                          int weapon, int state, int downed,
                          int revive_progress_ms,
                          const struct toy_game_slot *slots,
                          int current_slot, int reloading, int reload_timer_ms,
                          int muzzle_flash_ms, int kills, int special_kills,
                          int damage_dealt,
                          unsigned int fire_seq, int ray_count,
                          const struct toy_game_ray *rays,
                          int airborne_ms, int airborne_y,
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
    put_u32(p + 52, input_ack);
    p[56] = animation && animation->id >= 0 &&
            animation->id < TOY_GAME_ANIM_COUNT ?
            (unsigned char)animation->id : TOY_GAME_ANIM_NONE;
    put_i16(p + 57, animation ? animation->time_ms : 0);
    put_i16(p + 20, hp);
    p[22] = (unsigned char)current_slot;
    p[23] = put_weapon_value(slots ? slots[0].weapon : -1);
    p[24] = put_weapon_value(slots ? slots[1].weapon : -1);
    put_i16(p + 25, slots ? slots[0].mag : 0);
    put_i16(p + 27, slots ? slots[0].reserve : 0);
    put_i16(p + 29, slots ? slots[1].mag : 0);
    put_i16(p + 31, slots ? slots[1].reserve : 0);
    p[33] = (unsigned char)(reloading != 0);
    put_i16(p + 34, reload_timer_ms);
    put_i16(p + 36, muzzle_flash_ms);
    put_i16(p + 38, kills);
    put_i16(p + 59, special_kills);
    put_u32(p + 61, (uint32_t)damage_dealt);
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
    player->airborne_ms = get_i16(p + 46);
    player->airborne_y = get_i16(p + 48);
    player->input_ack = get_u32(p + 52);
    player->animation.id = p[56] < TOY_GAME_ANIM_COUNT ? p[56] :
                           TOY_GAME_ANIM_NONE;
    player->animation.time_ms = get_i16(p + 57);
    player->hp = get_i16(p + 20);
    player->current_slot = p[22] < TOY_GAME_WEAPON_SLOTS ? p[22] : 0;
    player->slot_weapon[0] = get_weapon_value(p[23]);
    player->slot_weapon[1] = get_weapon_value(p[24]);
    player->mag[0] = get_i16(p + 25); player->reserve[0] = get_i16(p + 27);
    player->mag[1] = get_i16(p + 29); player->reserve[1] = get_i16(p + 31);
    player->reloading = p[33] != 0; player->reload_timer_ms = get_i16(p + 34);
    player->muzzle_flash_ms = get_i16(p + 36);
    player->kills = get_i16(p + 38);
    player->special_kills = get_i16(p + 59);
    player->damage_dealt = (int)get_u32(p + 61);
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
    p[27] = (unsigned char)(e->special_target_active ? 1 : 0);
    p[28] = (unsigned char)(e->charge_active ? 1 : 0);
    put_i16(p + 29, e->special_timer_ms);
    put_i16(p + 31, e->special_windup_ms);
    p[33] = put_i8_value(e->special_target_player);
    p[34] = put_i8_value(e->special_target_actor_index);
    put_i16(p + 35, e->special_pull_timer_ms);
    put_i16(p + 37, e->charge_dir_x);
    put_i16(p + 39, e->charge_dir_z);
    put_i16(p + 41, e->charge_elapsed_ms);
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
    e->special_target_active = p[27] & 1;
    e->charge_active = p[28] & 1;
    e->special_timer_ms = get_i16(p + 29);
    e->special_windup_ms = get_i16(p + 31);
    e->special_target_player = get_i8_value(p[33]);
    e->special_target_actor_index = get_i8_value(p[34]);
    e->special_pull_timer_ms = get_i16(p + 35);
    e->charge_dir_x = get_i16(p + 37);
    e->charge_dir_z = get_i16(p + 39);
    e->charge_elapsed_ms = get_i16(p + 41);
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
        if (net->peer_known && net->connected &&
            net_send(net, packet, size) < 0) return -1;
        for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
            if (net->remotes[i].active && net->remotes[i].connected &&
                !net->relay_mode &&
                net_send_to(net, &net->remotes[i].address,
                            packet, size) < 0) return -1;
        net->ai_fire_sent_seq[actor_index] = actor->fire_seq;
    }
    return 0;
}

int rasterfall_net_send_snapshot(struct rasterfall_net *net,
                                 const struct rasterfall_session *session,
                                 const struct camera *host_camera,
                                 const struct toy_game *game,
                                 int air_walls_enabled,
                                 int manual_alarm_enabled,
                                 int manual_alarm_timer_ms)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET];
    unsigned char snapshot[RASTERFALL_NET_MAX_SNAPSHOT];
    unsigned char *p = snapshot;
    int weapon = game->slots[game->current_slot].weapon;
    int peer_weapon = net->peer_slots[net->peer_current_slot].weapon;
    int event_count;
    unsigned char *event_data;
    unsigned char *world_data;
    int actor_count = 0;
    int actor_indices[RASTERFALL_NET_MAX_ACTORS];
    int enemy_count = 0;
    int enemy_indices[TOY_GAME_MAX_ENEMIES];
    int actor_i;
    for (actor_i = 0; actor_i < TOY_GAME_MAX_ACTORS &&
         actor_count < RASTERFALL_NET_MAX_ACTORS; actor_i++)
        if (game->actors[actor_i].active)
            actor_indices[actor_count++] = actor_i;
    for (int enemy_i = 0; enemy_i < TOY_GAME_MAX_ENEMIES; enemy_i++)
        if (game->enemies[enemy_i].active)
            enemy_indices[enemy_count++] = enemy_i;
    int payload_size = NET_SNAPSHOT_BASE +
                       RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE + 1 +
                       actor_count * NET_ACTOR_SIZE +
                       enemy_count * NET_ENEMY_SIZE + NET_EVENT_SIZE + NET_WORLD_SIZE;
    int size, offset, part_count, part_index;
    if (net->mode != RASTERFALL_NET_HOST ||
        (!net->peer_known && !net->remotes[0].active &&
         !net->remotes[1].active)) return -1;
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
    encode_player(p + NET_SNAPSHOT_BASE + NET_PLAYER_SIZE, 1,
                  net->peer_known, &net->peer_camera, net->peer_hp,
                  peer_weapon, net->peer_state, net->peer_down,
                  net->peer_revive_progress_ms, net->peer_slots,
                  net->peer_current_slot, net->peer_reloading,
                  net->peer_reload_timer_ms, net->peer_muzzle_flash_ms,
                  net->peer_kills, net->peer_special_kills,
                  net->peer_damage_dealt,
                  net->peer_fire_seq,
                  net->peer_ray_count, net->peer_rays,
                  net->peer_airborne_ms, net->peer_airborne_y,
                  net->last_input_sequence, &net->peer_animation);
    for (int remote_i = 0; remote_i < RASTERFALL_NET_REMOTE_MAX; remote_i++) {
        struct rasterfall_net_remote *remote = &net->remotes[remote_i];
        int player_id = remote_i + 2;
        int offset = NET_SNAPSHOT_BASE + player_id * NET_PLAYER_SIZE;
        encode_player(p + offset, player_id,
                      remote->active && remote->connected,
                      &remote->camera, remote->hp,
                      remote->current_slot >= 0 &&
                              remote->current_slot < TOY_GAME_WEAPON_SLOTS ?
                          remote->slots[remote->current_slot].weapon : -1,
                      remote->state, remote->down,
                      remote->revive_progress_ms, remote->slots,
                      remote->current_slot, remote->reloading,
                      remote->reload_timer_ms, remote->muzzle_flash_ms,
                      remote->kills, remote->special_kills,
                      remote->damage_dealt,
                      remote->fire_seq, remote->ray_count,
                      remote->rays, remote->airborne_ms,
                      remote->airborne_y, remote->last_input_sequence,
                      &remote->animation);
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
    event_data = p + NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                 actor_count * NET_ACTOR_SIZE + 1 + enemy_count * NET_ENEMY_SIZE;
    world_data = event_data + NET_EVENT_SIZE;
    /* Reliable events use their own packet and event-id ACK.  Keep the
     * legacy snapshot event area empty so a lost snapshot cannot duplicate
     * or falsely acknowledge an event. */
    event_count = 0;
    put_u32(event_data, 0);
    event_data[4] = 0;
    memset(event_data + 5, 0, TOY_GAME_MAX_EVENTS);
    if (event_count)
        memcpy(event_data + 5, net->remote_event_queue,
               (size_t)event_count);
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
    put_u32(world_data + NET_WORLD_BASE_SIZE, (uint32_t)game->money);
    put_i16(world_data + NET_WORLD_BASE_SIZE + 4,
            session ? session->flag_count : 0);
    for (int flag_i = 0; flag_i < RASTERFALL_MAX_FLAGS; flag_i++) {
        const struct rasterfall_flag *flag = session &&
            flag_i < session->flag_count ? &session->flags[flag_i] : 0;
        unsigned char *fp = world_data + NET_WORLD_BASE_SIZE + 8 +
                            flag_i * NET_WORLD_FLAG_SIZE;
        put_u32(fp, flag ? (uint32_t)flag->x : 0);
        put_u32(fp + 4, flag ? (uint32_t)flag->z : 0);
        fp[8] = flag && flag->active ? 1 : 0;
        fp[9] = flag && flag->carried ? 1 : 0;
        fp[10] = put_i8_value(flag ? flag->carrier_id : -1);
        fp[11] = 0;
    }
    for (int actor_i = 0; actor_i < TOY_GAME_MAX_ACTORS; actor_i++)
        put_i16(world_data + NET_WORLD_BASE_SIZE + 8 +
                RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + actor_i * 2,
                session && actor_i < TOY_GAME_MAX_ACTORS &&
                session->game_state.actors[actor_i].active ?
                    session->game_state.actors[actor_i].flag_index : -1);
    net->remote_event_snapshot_sequence = net->last_snapshot_sequence;
    if (event_count)
        net->remote_event_snapshot_last_id = net->remote_event_ids[event_count - 1];
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
        if (net->peer_known && net_send(net, packet, size) < 0) return -1;
        for (int remote_i = 0; remote_i < RASTERFALL_NET_REMOTE_MAX; remote_i++)
            if (net->remotes[remote_i].active &&
                net->remotes[remote_i].connected &&
                !net->relay_mode &&
                net_send_to(net, &net->remotes[remote_i].address,
                            packet, size) < 0) return -1;
    }
    net_send_reliable_events(net);
    return send_ai_fire_packets(net, game);
}

static int decode_snapshot(const unsigned char *payload, int size,
                           struct rasterfall_net *net)
{
    int count, i;
    const unsigned char *event_data;
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
                NET_EVENT_SIZE + NET_WORLD_SIZE) return -1;
    for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++)
        net->players[i].active = 0;
    for (i = 0; i < count; i++) {
        struct rasterfall_net_player player;
        if (decode_player(payload + NET_SNAPSHOT_BASE + i * NET_PLAYER_SIZE,
                          &player) < 0) return -1;
        memcpy(&net->players[player.id], &player,
               sizeof(struct rasterfall_net_player));
    }
    net->actor_count = payload[7];
    for (i = 0; i < net->actor_count; i++)
        decode_actor(payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                     i * NET_ACTOR_SIZE, &net->actors[i]);
    net->enemy_count = payload[NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                               net->actor_count * NET_ACTOR_SIZE];
    if (net->enemy_count > TOY_GAME_MAX_ENEMIES) return -1;
    event_data = payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                 net->actor_count * NET_ACTOR_SIZE + 1 +
                 net->enemy_count * NET_ENEMY_SIZE;
    world_data = event_data + NET_EVENT_SIZE;
    for (i = 0; i < net->enemy_count; i++)
        decode_enemy(payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                     net->actor_count * NET_ACTOR_SIZE + 1 + i * NET_ENEMY_SIZE,
                     &net->enemies[i]);
    /* The legacy snapshot event slot is intentionally ignored.  Reliable
     * events arrive through RASTERFALL_NET_RELIABLE_EVENT and are ACKed by
     * event ID, independently of snapshot delivery. */
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
    net->snapshot_money = (int)get_u32(world_data + NET_WORLD_BASE_SIZE);
    net->snapshot_flag_count = get_i16(world_data + NET_WORLD_BASE_SIZE + 4);
    if (net->snapshot_flag_count < 0 ||
        net->snapshot_flag_count > RASTERFALL_MAX_FLAGS) return -1;
    for (i = 0; i < RASTERFALL_MAX_FLAGS; i++) {
        const unsigned char *fp = world_data + NET_WORLD_BASE_SIZE + 8 +
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
            world_data + NET_WORLD_BASE_SIZE + 8 +
            RASTERFALL_MAX_FLAGS * NET_WORLD_FLAG_SIZE + i * 2);
    net->snapshot_player_control_disabled = world_data[30] & 1;
    net->snapshot_ready = 1;
    net->world_ready = 1;
    return 0;
}

static int decode_snapshot_part(const unsigned char *payload, int size,
                                uint32_t sequence, struct rasterfall_net *net)
{
    int total_size, offset, chunk, part_index, part_count;
    unsigned int expected_mask;
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
    if (sequence < net->receive_sequence ||
        (net->snapshot_part_sequence &&
         sequence < net->snapshot_part_sequence)) return 0;
    if (sequence != net->snapshot_part_sequence ||
        total_size != net->snapshot_part_total_size ||
        part_count != net->snapshot_part_count) {
        net->snapshot_part_sequence = sequence;
        net->snapshot_part_total_size = total_size;
        net->snapshot_part_count = part_count;
        net->snapshot_part_mask = 0;
    }
    memcpy(net->snapshot_part_buffer + offset, payload + NET_SNAPSHOT_PART_BASE,
           (size_t)chunk);
    net->snapshot_part_mask |= 1U << part_index;
    expected_mask = part_count == 32 ? 0xffffffffU : ((1U << part_count) - 1U);
    if (net->snapshot_part_mask != expected_mask) return 0;
    if (decode_snapshot(net->snapshot_part_buffer, total_size, net) < 0) {
        net->snapshot_part_mask = 0;
        return -1;
    }
    net->receive_sequence = sequence;
    net->snapshot_part_mask = 0;
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
            if (net->relay_mode) {
                /* In relay mode all game packets use the coordinator as the
                 * stable endpoint; the coordinator forwards them by room. */
                memcpy(&net->peer, &net->public_server, sizeof(net->peer));
            } else {
                struct sockaddr_in matched_peer;
                memset(&matched_peer, 0, sizeof(matched_peer));
                matched_peer.sin_family = AF_INET;
                memcpy(&matched_peer.sin_addr.s_addr, packet + 12, 4);
                matched_peer.sin_port = htons((unsigned short)get_u16(packet + 16));
                if (net->mode == RASTERFALL_NET_HOST && packet[19] >= 2 &&
                    net->peer_known && !same_peer(&net->peer, &matched_peer)) {
                    int extra = net_alloc_remote_client_id(
                        net, &matched_peer, packet[19]);
                    if (extra >= 0) {
                        memcpy(&net->remotes[extra].address, &matched_peer,
                               sizeof(matched_peer));
                        net->remotes[extra].connected = 1;
                    }
                } else {
                    memcpy(&net->peer, &matched_peer, sizeof(matched_peer));
                }
            }
            net->peer_known = 1;
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
            if (net->mode == RASTERFALL_NET_HOST && new_match) {
                /* A server-side room reset starts a new session generation.
                 * Drop the old remote inventory/state before the next input,
                 * otherwise the host can render a stale player model. */
                net_reset_peer_state(net);
            }
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
            memcpy(&net->peer, &source, sizeof(source));
            net->peer_known = 1;
            net->public_matched = 1;
            net->connected = 1;
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
        net_stats_note_sequence(net, sequence);
        net->last_receive_ms = net_monotonic_ms();
        if (net->mode == RASTERFALL_NET_HOST) {
            if (net->relay_mode && relay_client_id >= 2) {
                int remote_index = net_alloc_remote_client_id(
                    net, &source, relay_client_id);
                if (remote_index < 0) continue;
                {
                    struct rasterfall_net_remote *remote =
                        &net->remotes[remote_index];
                    remote->last_receive_ms = net->last_receive_ms;
                    if (type == RASTERFALL_NET_HELLO) {
                        /* Relay clients already received their player ID in
                         * MATCH; their first input is enough to initialize
                         * the spawn from the authoritative snapshot. */
                    } else if (type == RASTERFALL_NET_INPUT &&
                               sequence > remote->last_input_sequence &&
                               decode_command(packet + NET_HEADER_SIZE,
                                              payload_size,
                                              &remote->command) == 0) {
                        if (!remote->camera_initialized) {
                            decode_command_camera(packet + NET_HEADER_SIZE,
                                                  &remote->camera);
                            remote->camera_initialized = 1;
                        }
                        decode_command_camera(packet + NET_HEADER_SIZE,
                                              &remote->reported_camera);
                        remote->reported_camera_ready = 1;
                        remote->reliable_event_ack =
                            get_u32(packet + NET_HEADER_SIZE + 32);
                        remote->last_input_sequence = sequence;
                        remote->last_input_tick = net->tick;
                        remote->command_ready = 1;
                        if (ack == net->last_snapshot_sequence &&
                            net->last_snapshot_sent_ms) {
                            long elapsed = net_monotonic_ms() -
                                           net->last_snapshot_sent_ms;
                            if (elapsed >= 0 && elapsed < 60000)
                                remote->rtt_ms = (int)elapsed;
                        }
                    }
                }
                continue;
            }
            if (!net->peer_known) {
                if (type != RASTERFALL_NET_HELLO && type != RASTERFALL_NET_INPUT)
                    continue;
                memcpy(&net->peer, &source, sizeof(source));
                net->peer_known = 1;
                net->connected = 1;
                net->peer_last_receive_ms = net->last_receive_ms;
                memcpy(&net->peer_camera, &net->peer_spawn, sizeof(net->peer_camera));
                net->peer_camera_initialized = 0;
                net->peer_reported_camera_ready = 0;
            } else if (net->public_room &&
                       net_remote_index(net, &source) < 0) {
                /* 以已打洞成功的实际来源为准，适配 endpoint-dependent NAT。 */
                memcpy(&net->peer, &source, sizeof(source));
            }
            if (!net->relay_mode && net->peer_known &&
                !same_peer(&net->peer, &source)) {
                int remote_index = net_remote_index(net, &source);
                if (remote_index < 0 &&
                    (type == RASTERFALL_NET_HELLO ||
                     type == RASTERFALL_NET_INPUT))
                    remote_index = net_alloc_remote(net, &source);
                if (remote_index < 0) continue;
                {
                    struct rasterfall_net_remote *remote =
                        &net->remotes[remote_index];
                    remote->last_receive_ms = net->last_receive_ms;
                    if (type == RASTERFALL_NET_HELLO) {
                        net_send_join_accept(net, &source, remote->client_id,
                                             &remote->spawn);
                    } else if (type == RASTERFALL_NET_INPUT &&
                               sequence > remote->last_input_sequence &&
                               decode_command(packet + NET_HEADER_SIZE,
                                              payload_size,
                                              &remote->command) == 0) {
                        if (!remote->camera_initialized) {
                            decode_command_camera(packet + NET_HEADER_SIZE,
                                                  &remote->camera);
                            remote->camera_initialized = 1;
                        }
                        decode_command_camera(packet + NET_HEADER_SIZE,
                                              &remote->reported_camera);
                        remote->reported_camera_ready = 1;
                        remote->reliable_event_ack =
                            get_u32(packet + NET_HEADER_SIZE + 32);
                        remote->last_input_sequence = sequence;
                        remote->last_input_tick = net->tick;
                        remote->command_ready = 1;
                        if (ack == net->last_snapshot_sequence &&
                            net->last_snapshot_sent_ms) {
                            long elapsed = net_monotonic_ms() -
                                           net->last_snapshot_sent_ms;
                            if (elapsed >= 0 && elapsed < 60000)
                                remote->rtt_ms = (int)elapsed;
                        }
                    }
                }
                continue;
            }
            net->peer_last_receive_ms = net->last_receive_ms;
            if (type == RASTERFALL_NET_HELLO) {
                if (!net->relay_mode)
                    net_send_join_accept(net, &source, 1, &net->peer_spawn);
            } else if (type == RASTERFALL_NET_INPUT &&
                       sequence > net->last_input_sequence &&
                       decode_command(packet + NET_HEADER_SIZE, payload_size,
                                      &net->remote_command) == 0) {
                if (!net->peer_camera_initialized) {
                    decode_command_camera(packet + NET_HEADER_SIZE,
                                          &net->peer_camera);
                    net->peer_camera_initialized = 1;
                }
                decode_command_camera(packet + NET_HEADER_SIZE,
                                      &net->peer_reported_camera);
                net->peer_reported_camera_ready = 1;
                net->peer_reliable_event_ack =
                    get_u32(packet + NET_HEADER_SIZE + 32);
                if (ack == net->last_snapshot_sequence &&
                    net->last_snapshot_sent_ms) {
                    long elapsed = net_monotonic_ms() - net->last_snapshot_sent_ms;
                    net_stats_note_rtt(net, elapsed);
                }
                net->last_input_sequence = sequence;
                net->receive_sequence = sequence;
                net->remote_command_ready = 1;
                net_ack_remote_events(net, ack);
            }
        } else if (net->mode == RASTERFALL_NET_CLIENT) {
            if (net->public_room) {
                memcpy(&net->peer, &source, sizeof(source));
                net->peer_known = 1;
            } else if (!same_peer(&net->peer, &source)) continue;
            if (type == RASTERFALL_NET_HELLO && payload_size >= 20) {
                net->local_player_id = packet[NET_HEADER_SIZE];
                net->peer_spawn.x = (int)get_u32(packet + NET_HEADER_SIZE + 4);
                net->peer_spawn.z = (int)get_u32(packet + NET_HEADER_SIZE + 8);
                net->peer_spawn.sy = get_i16(packet + NET_HEADER_SIZE + 12);
                net->peer_spawn.cy = get_i16(packet + NET_HEADER_SIZE + 14);
                net->peer_spawn.pitch_sy =
                    get_i16(packet + NET_HEADER_SIZE + 16);
                net->peer_spawn.pitch_cy =
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
            } else if (type == RASTERFALL_NET_SNAPSHOT_PART &&
                       decode_snapshot_part(packet + NET_HEADER_SIZE,
                                             payload_size, sequence, net) == 0) {
                /* A snapshot becomes visible only after every application
                 * fragment has arrived. */
            } else if (type == RASTERFALL_NET_SNAPSHOT &&
                       sequence > net->receive_sequence &&
                       decode_snapshot(packet + NET_HEADER_SIZE, payload_size, net) == 0)
                net->receive_sequence = sequence;
            if (type == RASTERFALL_NET_SNAPSHOT ||
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
        for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
            struct rasterfall_net_remote *remote = &net->remotes[i];
            if (remote->active && remote->last_receive_ms &&
                now - remote->last_receive_ms > 3000)
                memset(remote, 0, sizeof(*remote));
        }
        if (net->peer_known && net->peer_last_receive_ms &&
            now - net->peer_last_receive_ms > 3000) {
            net_reset_peer_state(net);
            net->peer_known = 0;
            net->connected = 0;
            net->peer_last_receive_ms = 0;
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
        if (net->public_matched && net->peer_known &&
            (!net->last_public_punch_ms || now - net->last_public_punch_ms >= 100)) {
            if (punch_send_probe(net) == 0) net->last_public_punch_ms = now;
        }
    }
    if (net->mode == RASTERFALL_NET_CLIENT && net->last_receive_ms &&
        now - net->last_receive_ms > 3000) {
        net->connected = 0;
        net->last_input_sequence = 0;
        net->receive_sequence = 0;
        net->remote_command_ready = 0;
        net->snapshot_ready = 0;
        net->world_ready = 0;
        net->spawn_pending = 0;
        net->remote_event_queue_count = 0;
        net->remote_event_next_id = 0;
        net->remote_event_last_id = 0;
        net->reliable_event_ack = 0;
        net->remote_event_snapshot_last_id = 0;
        net->remote_event_snapshot_sequence = 0;
    }
    if (net->mode == RASTERFALL_NET_CLIENT && !net->connected &&
        ((!net->public_room) || net->public_matched) &&
        (!net->last_hello_ms || now - net->last_hello_ms >= 500)) {
        size = packet_begin(hello, RASTERFALL_NET_HELLO, 0,
                            ++net->send_sequence, net->receive_sequence);
        if (net_send(net, hello, size) == 0) net->last_hello_ms = now;
    }
}

static void net_apply_extra_remote(struct rasterfall_net *net,
                                   struct rasterfall_session *session,
                                   struct rasterfall_net_remote *remote)
{
    struct toy_game *g = &session->game_state;
    struct toy_game_slot host_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray host_rays[TOY_GAME_MAX_RAYS];
    struct toy_game_actor *actor;
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    int host_px, host_pz, host_hp, host_down, host_revive;
    int host_current, host_reload, host_reload_timer, host_cooldown;
    int host_muzzle, host_damage, host_kills, host_special_kills;
    int host_damage_dealt, host_ray_count;
    struct toy_game_animation_state host_animation;
    unsigned int host_fire_seq;
    int actor_airborne_ms, actor_airborne_y, actor_vertical_velocity;
    int actor_knockback_x, actor_knockback_z;
    int event_start, index;
    if (!remote->active || !remote->connected || !remote->command_ready)
        return;
    /* client_id 1 is the legacy peer at actor slot BASE; client_id 2/3
     * must therefore use BASE+1/BASE+2 rather than reusing slot BASE. */
    index = TOY_GAME_REMOTE_ACTOR_BASE + remote->client_id - 1;
    if (index < 0 || index >= TOY_GAME_MAX_ACTORS) return;
    actor = &g->actors[index];
    if (remote->reported_camera_ready) {
        remote->camera.sy = remote->reported_camera.sy;
        remote->camera.cy = remote->reported_camera.cy;
        remote->camera.pitch_sy = remote->reported_camera.pitch_sy;
        remote->camera.pitch_cy = remote->reported_camera.pitch_cy;
    }
    /* Smoker/Charger and the vertical simulation run before input application.
     * Start this player's command from the authoritative actor position so a
     * pull or launch cannot be overwritten by the stale client position. */
    remote->camera.x = actor->x;
    remote->camera.z = actor->z;
    remote->camera.y = actor->ground_y + actor->airborne_y;
    toy_game_update_actor_motion(g, index, 16);
    if ((remote->command.buttons & RASTERFALL_CMD_JUMP) &&
        actor->state == TOY_GAME_ACTOR_ALIVE)
        toy_game_jump_actor(g, index,
            (remote->camera.sy * remote->command.move_forward +
             remote->camera.cy * remote->command.move_strafe) *
                RASTERFALL_MOVE_STEP / 1024,
            (remote->camera.cy * remote->command.move_forward -
             remote->camera.sy * remote->command.move_strafe) *
                RASTERFALL_MOVE_STEP / 1024);
    actor_airborne_ms = actor->airborne_ms;
    actor_airborne_y = actor->airborne_y;
    actor_vertical_velocity = actor->vertical_velocity;
    actor_knockback_x = actor->knockback_x;
    actor_knockback_z = actor->knockback_z;
    memcpy(host_slots, g->slots, sizeof(host_slots));
    memcpy(host_rays, g->rays, sizeof(host_rays));
    host_px = g->px; host_pz = g->pz; host_hp = g->hp;
    host_down = g->player_down;
    host_revive = g->player_revive_progress_ms;
    host_current = g->current_slot;
    host_reload = g->reloading; host_reload_timer = g->reload_timer_ms;
    host_cooldown = g->fire_cooldown_ms; host_muzzle = g->muzzle_flash_ms;
    host_damage = g->damage_flash_ms; host_kills = g->kills;
    host_special_kills = g->special_kills;
    host_damage_dealt = g->damage_dealt;
    host_ray_count = g->ray_count; host_fire_seq = g->fire_seq;
    host_animation = g->animation;
    memcpy(g->slots, actor->slots, sizeof(g->slots));
    g->current_slot = actor->current_slot;
    g->hp = actor->hp; g->player_down = actor->state == TOY_GAME_ACTOR_DOWNED;
    g->player_revive_progress_ms = actor->revive_progress_ms;
    g->reloading = actor->reloading; g->reload_timer_ms = actor->reload_timer_ms;
    g->fire_cooldown_ms = actor->fire_cooldown_ms;
    g->muzzle_flash_ms = actor->muzzle_flash_ms;
    g->damage_flash_ms = 0; g->kills = remote->kills;
    g->special_kills = remote->special_kills;
    g->damage_dealt = remote->damage_dealt;
    if (remote->command.buttons & RASTERFALL_CMD_CLEAR_STATS) {
        g->kills = 0;
        g->special_kills = 0;
        g->damage_dealt = 0;
    }
    g->fire_seq = actor->fire_seq;
    g->px = remote->camera.x; g->pz = remote->camera.z;
    rasterfall_session_step_remote_player(session, &remote->camera,
                                          &remote->command,
                                          actor->state != TOY_GAME_ACTOR_ALIVE,
                                          actor->ground_y + actor->airborne_y);
    actor->x = remote->camera.x;
    actor->z = remote->camera.z;
    toy_game_update_actor_ground(g, index);
    if ((remote->command.buttons & RASTERFALL_CMD_INTERACT) &&
        !g->player_down)
        rasterfall_session_interact_remote(session, &remote->camera);
    if (remote->command.buttons & RASTERFALL_CMD_FLAG)
        rasterfall_session_toggle_flag_remote(session, &remote->camera,
                                               remote->client_id);
    rasterfall_session_update_flag_remote(session, &remote->camera,
                                          remote->client_id);
    if (remote->command.buttons & RASTERFALL_CMD_SHOP)
        rasterfall_session_shop_request(session, remote->command.shop_action,
                                        remote->command.shop_item);
    memset(keys, 0, sizeof(keys));
    if (remote->command.buttons & RASTERFALL_CMD_RELOAD)
        keys[TOY_GAME_KEY_RELOAD] = 1;
    if (remote->command.buttons & RASTERFALL_CMD_SLOT_1)
        keys[TOY_GAME_KEY_SLOT_1] = 1;
    if (remote->command.buttons & RASTERFALL_CMD_SLOT_2)
        keys[TOY_GAME_KEY_SLOT_2] = 1;
    event_start = g->event_count;
    toy_game_update_weapon_held(g, keys,
        (remote->command.buttons & RASTERFALL_CMD_FIRE) != 0,
        remote->command.fire_held, remote->camera.sy, remote->camera.cy, 16);
    actor->x = remote->camera.x; actor->z = remote->camera.z;
    /* The legacy weapon view above belongs to this player's inventory only;
     * never copy the host player's vertical state back into this actor. */
    actor->airborne_ms = actor_airborne_ms;
    actor->airborne_y = actor_airborne_y;
    actor->vertical_velocity = actor_vertical_velocity;
    actor->knockback_x = actor_knockback_x;
    actor->knockback_z = actor_knockback_z;
    remote->camera.y = actor->ground_y + actor->airborne_y;
    actor->sy = remote->camera.sy; actor->cy = remote->camera.cy;
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
    actor->fire_seq = g->fire_seq; actor->ray_count = g->ray_count;
    memcpy(actor->rays, g->rays, sizeof(actor->rays));
    actor->animation = g->animation;
    remote->animation = g->animation;
    remote->hp = actor->hp; remote->down = actor->state == TOY_GAME_ACTOR_DOWNED;
    remote->state = g->state;
    remote->kills = g->kills;
    remote->special_kills = g->special_kills;
    remote->damage_dealt = g->damage_dealt;
    remote->airborne_ms = actor->airborne_ms;
    remote->airborne_y = actor->airborne_y;
    remote->slots[0] = actor->slots[0]; remote->slots[1] = actor->slots[1];
    remote->current_slot = actor->current_slot;
    remote->reloading = actor->reloading;
    remote->reload_timer_ms = actor->reload_timer_ms;
    remote->muzzle_flash_ms = actor->muzzle_flash_ms;
    remote->fire_seq = actor->fire_seq; remote->ray_count = actor->ray_count;
    memcpy(remote->rays, actor->rays, sizeof(remote->rays));
    if (g->event_count > event_start) {
        int count = g->event_count - event_start;
        if (count > TOY_GAME_MAX_EVENTS) count = TOY_GAME_MAX_EVENTS;
        net_queue_remote_events(net, g->events + event_start, count,
                                remote->client_id, remote->camera.x,
                                remote->camera.z);
        g->event_count = event_start;
    }
    memcpy(g->slots, host_slots, sizeof(g->slots));
    memcpy(g->rays, host_rays, sizeof(g->rays));
    g->px = host_px; g->pz = host_pz; g->hp = host_hp;
    g->player_down = host_down; g->player_revive_progress_ms = host_revive;
    g->current_slot = host_current; g->reloading = host_reload;
    g->reload_timer_ms = host_reload_timer; g->fire_cooldown_ms = host_cooldown;
    g->muzzle_flash_ms = host_muzzle; g->damage_flash_ms = host_damage;
    g->kills = host_kills; g->special_kills = host_special_kills;
    g->damage_dealt = host_damage_dealt;
    g->ray_count = host_ray_count;
    g->fire_seq = host_fire_seq;
    g->animation = host_animation;
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
    if (rescuer_id != 1 && net->peer_known && net->peer_down) {
        long dx = (long)rescuer->x - net->peer_camera.x;
        long dz = (long)rescuer->z - net->peer_camera.z;
        long d2 = dx * dx + dz * dz;
        if (d2 <= (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE &&
            (target < 0 || d2 < best)) { target = 1; best = d2; }
    }
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
        struct rasterfall_net_remote *other = &net->remotes[i];
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
    if (target_id == 1) return &net->peer_camera;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
        if (net->remotes[i].client_id == target_id)
            return &net->remotes[i].camera;
    (void)session;
    return NULL;
}

static int net_target_is_down(const struct rasterfall_net *net,
                              const struct rasterfall_session *session,
                              int target_id)
{
    int i;
    if (target_id == 0) return session->game_state.player_down;
    if (target_id == 1) return net->peer_known && net->peer_down;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
        if (net->remotes[i].client_id == target_id)
            return net->remotes[i].active && net->remotes[i].connected &&
                   net->remotes[i].down;
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
    } else if (target_id == 1) {
        net->peer_down = 0;
        net->peer_hp = TOY_GAME_REVIVE_HP;
        net->peer_revive_progress_ms = 0;
        session->game_state.secondary_player_down = 0;
        session->game_state.secondary_player_hp = TOY_GAME_REVIVE_HP;
    } else for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
        struct rasterfall_net_remote *target = &net->remotes[i];
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
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
        struct rasterfall_net_remote *rescuer = &net->remotes[i];
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
            else if (target_id == 1) net->peer_revive_progress_ms =
                rescuer->local_revive_progress_ms;
        }
    }
}

void rasterfall_net_apply_remote(struct rasterfall_net *net,
                                 struct rasterfall_session *session,
                                 struct camera *host_camera)
{
    struct toy_game *g = &session->game_state;
    struct toy_game_slot host_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray host_rays[TOY_GAME_MAX_RAYS];
    int host_px, host_pz, host_hp, host_state, host_current;
    int host_down, host_revive_progress;
    int host_reloading, host_reload_timer, host_cooldown, host_muzzle;
    int host_damage, host_kills, host_special_kills, host_damage_dealt;
    int host_ray_count;
    unsigned int host_fire_seq;
    struct toy_game_animation_state host_animation;
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    int event_start;
    net->tick++;
    if (net->remote_command_ready) {
        net->last_input_tick = net->tick;
        net->remote_command_ready = 0;
    }
    if (net->remote_command.buttons & RASTERFALL_CMD_RESET) {
        /* Restart is deliberately host-only. A client may report the key,
         * but cannot change the authoritative round state. */
        net->remote_command.buttons = 0;
        net->remote_command.fire_held = 0;
        return;
    }
    if (net->peer_known && !net->peer_state_initialized) {
        net_init_player_slots(net->peer_slots, &net->peer_current_slot);
        /* peer_hp belongs to player 2.  Using g->hp here makes the first
         * snapshot inherit the host's health (and used to leave it at zero
         * after the transport state was reset). */
        net->peer_hp = g->secondary_player_hp;
        net->peer_state = g->state;
        net->peer_down = g->secondary_player_down;
        net->peer_state_initialized = 1;
    }
    /* Charger/Smoker 对远端玩家的影响在本地主机规则步中完成，随后写回
     * 远端玩家的权威状态，确保客户端看到同一套血量与击飞状态。 */
    if (net->peer_known) {
        net->peer_camera.x = g->secondary_px;
        net->peer_camera.z = g->secondary_pz;
        net->peer_camera.y = g->secondary_player_ground_y +
                             g->secondary_player_airborne_y;
        net->peer_hp = g->secondary_player_hp;
        net->peer_down = g->secondary_player_down;
        net->peer_airborne_ms = g->secondary_player_airborne_ms;
        net->peer_airborne_y = g->secondary_player_airborne_y;
    }
    if (net->peer_known && net->tick - net->last_input_tick <= NET_INPUT_HOLD_TICKS) {
        net->remote_event_count = 0;
        event_start = g->event_count;
        if ((net->remote_command.buttons & RASTERFALL_CMD_JUMP) &&
            !net->peer_down)
            toy_game_jump_secondary_player(g,
                (net->peer_camera.sy * net->remote_command.move_forward +
                 net->peer_camera.cy * net->remote_command.move_strafe) *
                    RASTERFALL_MOVE_STEP / 1024,
                (net->peer_camera.cy * net->remote_command.move_forward -
                 net->peer_camera.sy * net->remote_command.move_strafe) *
                    RASTERFALL_MOVE_STEP / 1024);
        if (!net->peer_down)
            rasterfall_session_step_remote_player(session, &net->peer_camera,
                                                  &net->remote_command,
                                                  net->peer_down,
                                                  g->secondary_player_ground_y +
                                                  g->secondary_player_airborne_y);
        g->secondary_px = net->peer_camera.x;
        g->secondary_pz = net->peer_camera.z;
        toy_game_update_secondary_player_ground(g);
        net->peer_camera.y = g->secondary_player_ground_y +
                             g->secondary_player_airborne_y;
        /* The command's turn delta is only a prediction hint.  Use the
         * complete camera reported by the client before authoritative firing
         * so quick 90-degree turns and accumulated mouse deltas cannot leave
         * the host's aim behind. */
        if (net->peer_reported_camera_ready) {
            net->peer_camera.sy = net->peer_reported_camera.sy;
            net->peer_camera.cy = net->peer_reported_camera.cy;
            net->peer_camera.pitch_sy = net->peer_reported_camera.pitch_sy;
            net->peer_camera.pitch_cy = net->peer_reported_camera.pitch_cy;
        }
        /* 远端玩家的受伤判定也在主机执行。敌人 AI 的主目标仍是主机玩家，
         * 这里补充远端近战范围判定，避免客户端自行决定血量。 */
        if (net->peer_state == TOY_GAME_PLAYING && !net->peer_down) {
            int bite;
            for (bite = 0; bite < TOY_GAME_MAX_ENEMIES; bite++) {
                struct toy_game_enemy *enemy = &g->enemies[bite];
                long dx, dz;
                if (enemy->active != 1 || enemy->bite_cooldown_ms > 0) continue;
                dx = (long)enemy->x - net->peer_camera.x;
                dz = (long)enemy->z - net->peer_camera.z;
                if (dx * dx + dz * dz >
                    (long)TOY_GAME_ATTACK_RANGE * TOY_GAME_ATTACK_RANGE) continue;
                net->peer_hp -= TOY_GAME_BITE_DAMAGE;
                enemy->bite_cooldown_ms = TOY_GAME_BITE_MS;
                net_push_event(g, TOY_GAME_EV_BITE);
                if (net->peer_hp <= 0) {
                    net->peer_hp = 0;
                    net->peer_down = 1;
                    net->peer_revive_progress_ms = 0;
                    net_push_event(g, TOY_GAME_EV_PLAYER_DEATH);
                }
            }
        }
        /* A single E starts a persistent rescue action, matching local AI
         * rescue. The host continues it while the remote player stays near
         * the downed AI. */
        if (net->peer_revive_active) {
            int revive = rasterfall_session_revive_remote(session,
                                                           &net->peer_camera, 16);
            if (revive < 0) net->peer_revive_active = 0;
            else if (revive > 0) net->peer_revive_active = 0;
        }
        if (!net->peer_down &&
            (net->remote_command.buttons & RASTERFALL_CMD_INTERACT) &&
            !net->peer_revive_active) {
            int revive = rasterfall_session_revive_remote(session,
                                                           &net->peer_camera, 16);
            if (revive >= 0) net->peer_revive_active = 1;
        }
        /* 玩家2也可以救援后来加入的玩家；目标和进度跨 INPUT 保留。 */
        if (net->peer_revive_target_id >= 2 &&
            (!net_target_is_down(net, session, net->peer_revive_target_id) ||
             !net_rescue_target_camera(net, session, net->peer_revive_target_id))) {
            net->peer_revive_target_id = -1;
            net->peer_host_revive_progress_ms = 0;
        }
        if (net->peer_revive_target_id < 0 && !net->peer_down &&
            (net->remote_command.buttons & RASTERFALL_CMD_INTERACT)) {
            int target_id = net_find_down_target(net, session,
                                                 &net->peer_camera, 1);
            if (target_id >= 2) {
                net->peer_revive_target_id = target_id;
                net->peer_host_revive_progress_ms = 0;
            }
        }
        if (net->peer_revive_target_id >= 2) {
            const struct camera *target = net_rescue_target_camera(
                net, session, net->peer_revive_target_id);
            int target_id = net->peer_revive_target_id;
            if (target) {
                int revive = rasterfall_session_revive_player(
                    session, &net->peer_camera, target,
                    &net->peer_host_revive_progress_ms, 16);
                if (revive < 0) {
                    net->peer_revive_target_id = -1;
                    net->peer_host_revive_progress_ms = 0;
                } else if (revive > 0) {
                    net_finish_rescue(net, session, target_id);
                    net->peer_revive_target_id = -1;
                    net->peer_host_revive_progress_ms = 0;
                } else {
                    net->remotes[target_id - 2].revive_progress_ms =
                        net->peer_host_revive_progress_ms;
                }
            }
        }
        /* The remote player can rescue the host player. */
        if (net->peer_host_revive_active) {
            int revive = rasterfall_session_revive_player(
                session, &net->peer_camera, host_camera,
                &net->peer_host_revive_progress_ms, 16);
            if (revive < 0) {
                net->peer_host_revive_active = 0;
                net->peer_host_revive_progress_ms = 0;
                g->player_revive_progress_ms = 0;
            }
            else if (revive > 0) {
                net->peer_host_revive_active = 0;
                g->player_down = 0;
                g->player_revive_progress_ms = 0;
                g->hp = TOY_GAME_REVIVE_HP;
                toy_game_animation_set(&g->animation, TOY_GAME_ANIM_REVIVE);
                net_push_event(g, TOY_GAME_EV_REVIVE);
                net_push_event(g, TOY_GAME_EV_ACTOR_REVIVE);
            }
            if (net->peer_host_revive_active)
                g->player_revive_progress_ms =
                    net->peer_host_revive_progress_ms;
        }
        if (g->player_down && !net->peer_down &&
            (net->remote_command.buttons & RASTERFALL_CMD_INTERACT) &&
            !net->peer_host_revive_active) {
            int revive = rasterfall_session_revive_player(
                session, &net->peer_camera, host_camera,
                &net->peer_host_revive_progress_ms, 16);
            if (revive >= 0) net->peer_host_revive_active = 1;
            if (net->peer_host_revive_active)
                g->player_revive_progress_ms =
                    net->peer_host_revive_progress_ms;
        }
        /* 暂时把第二名玩家装载到 toy_game 的兼容单玩家视图，只执行
         * 武器/换弹/命中逻辑；随后恢复主机玩家，敌人与波次不重复推进。 */
        memcpy(host_slots, g->slots, sizeof(host_slots));
        memcpy(host_rays, g->rays, sizeof(host_rays));
        host_px = g->px; host_pz = g->pz; host_hp = g->hp;
        host_down = g->player_down;
        host_revive_progress = g->player_revive_progress_ms;
        host_state = g->state; host_current = g->current_slot;
        host_reloading = g->reloading; host_reload_timer = g->reload_timer_ms;
        host_cooldown = g->fire_cooldown_ms; host_muzzle = g->muzzle_flash_ms;
        host_damage = g->damage_flash_ms; host_kills = g->kills;
        host_special_kills = g->special_kills;
        host_damage_dealt = g->damage_dealt;
        host_ray_count = g->ray_count; host_fire_seq = g->fire_seq;
        host_animation = g->animation;
        memcpy(g->slots, net->peer_slots, sizeof(g->slots));
        g->current_slot = net->peer_current_slot;
        g->hp = net->peer_hp; g->state = net->peer_state;
        g->player_down = net->peer_down;
        g->player_revive_progress_ms = net->peer_revive_progress_ms;
        g->reloading = net->peer_reloading;
        g->reload_timer_ms = net->peer_reload_timer_ms;
        g->fire_cooldown_ms = net->peer_fire_cooldown_ms;
        g->muzzle_flash_ms = net->peer_muzzle_flash_ms;
        g->damage_flash_ms = net->peer_damage_flash_ms;
        g->kills = net->peer_kills;
        g->special_kills = net->peer_special_kills;
        g->damage_dealt = net->peer_damage_dealt;
        if (net->remote_command.buttons & RASTERFALL_CMD_CLEAR_STATS) {
            g->kills = 0;
            g->special_kills = 0;
            g->damage_dealt = 0;
        }
        g->fire_seq = net->peer_fire_seq;
        g->px = net->peer_camera.x; g->pz = net->peer_camera.z;
        /* 交互必须在装载玩家2的武器槽之后执行；否则拾取武器/弹药
         * 会错误地写入主机玩家槽位。世界级交互仍然作用于同一权威世界。 */
        if ((net->remote_command.buttons & RASTERFALL_CMD_INTERACT) &&
            !net->peer_down) {
            rasterfall_session_interact_remote(session, &net->peer_camera);
        }
        if (net->remote_command.buttons & RASTERFALL_CMD_FLAG)
            rasterfall_session_toggle_flag_remote(session, &net->peer_camera, 1);
        if (net->remote_command.buttons & RASTERFALL_CMD_SHOP)
            rasterfall_session_shop_request(session, net->remote_command.shop_action,
                                            net->remote_command.shop_item);
        rasterfall_session_update_flag_remote(session, &net->peer_camera, 1);
        memset(keys, 0, sizeof(keys));
        if (net->remote_command.buttons & RASTERFALL_CMD_RELOAD)
            keys[TOY_GAME_KEY_RELOAD] = 1;
        if (net->remote_command.buttons & RASTERFALL_CMD_SLOT_1)
            keys[TOY_GAME_KEY_SLOT_1] = 1;
        if (net->remote_command.buttons & RASTERFALL_CMD_SLOT_2)
            keys[TOY_GAME_KEY_SLOT_2] = 1;
        toy_game_update_weapon_held(g, keys,
            (net->remote_command.buttons & RASTERFALL_CMD_FIRE) != 0,
            net->remote_command.fire_held, net->peer_camera.sy,
            net->peer_camera.cy, 16);
        if (g->event_count > event_start) {
            int event_count = g->event_count - event_start;
            if (event_count > TOY_GAME_MAX_EVENTS)
                event_count = TOY_GAME_MAX_EVENTS;
            net_queue_remote_events(net, g->events + event_start, event_count,
                                    1, net->peer_camera.x, net->peer_camera.z);
            g->event_count = event_start;
        }
        memcpy(net->peer_slots, g->slots, sizeof(net->peer_slots));
        net->peer_current_slot = g->current_slot; net->peer_hp = g->hp;
        net->peer_state = g->state; net->peer_reloading = g->reloading;
        net->peer_down = g->player_down;
        net->peer_revive_progress_ms = g->player_revive_progress_ms;
        net->peer_reload_timer_ms = g->reload_timer_ms;
        net->peer_fire_cooldown_ms = g->fire_cooldown_ms;
        net->peer_muzzle_flash_ms = g->muzzle_flash_ms;
        net->peer_damage_flash_ms = g->damage_flash_ms;
        net->peer_kills = g->kills;
        net->peer_special_kills = g->special_kills;
        net->peer_damage_dealt = g->damage_dealt;
        net->peer_fire_seq = g->fire_seq;
        net->peer_animation = g->animation;
        net->peer_ray_count = g->ray_count;
        if (net->peer_ray_count < 0) net->peer_ray_count = 0;
        if (net->peer_ray_count > TOY_GAME_MAX_RAYS)
            net->peer_ray_count = TOY_GAME_MAX_RAYS;
        memcpy(net->peer_rays, g->rays, sizeof(net->peer_rays));
        memcpy(g->slots, host_slots, sizeof(g->slots));
        g->px = host_px; g->pz = host_pz; g->hp = host_hp;
        g->player_down = host_down;
        g->player_revive_progress_ms = host_revive_progress;
        g->state = host_state; g->current_slot = host_current;
        g->reloading = host_reloading; g->reload_timer_ms = host_reload_timer;
        g->fire_cooldown_ms = host_cooldown; g->muzzle_flash_ms = host_muzzle;
        g->damage_flash_ms = host_damage; g->kills = host_kills;
        g->special_kills = host_special_kills;
        g->damage_dealt = host_damage_dealt;
        g->ray_count = host_ray_count; g->fire_seq = host_fire_seq;
        g->animation = host_animation;
        memcpy(g->rays, host_rays, sizeof(g->rays));
    }
    net_apply_extra_rescue_actions(net, session);
    for (int remote_i = 0; remote_i < RASTERFALL_NET_REMOTE_MAX; remote_i++) {
        net_apply_extra_remote(net, session, &net->remotes[remote_i]);
        net->remotes[remote_i].command_ready = 0;
    }
    net->remote_command.turn = 0;
    net->remote_command.pitch = 0;
    net->remote_command.buttons = 0;
}

void rasterfall_net_sync_remote_players(struct rasterfall_net *net,
                                        struct toy_game *game)
{
    int i, index;
    struct toy_game_actor *actor;
    if (!net || !game || net->mode != RASTERFALL_NET_HOST) return;
    if (net->peer_known && net->connected) {
        index = toy_game_set_remote_player(game, 1, 1,
                                           net->peer_camera.x,
                                           net->peer_camera.z, "PLAYER 2");
        if (index >= 0) {
            actor = &game->actors[index];
            actor->sy = net->peer_camera.sy;
            actor->cy = net->peer_camera.cy;
            actor->hp = net->peer_hp;
            actor->state = net->peer_down ? TOY_GAME_ACTOR_DOWNED :
                                            TOY_GAME_ACTOR_ALIVE;
            actor->airborne_ms = net->peer_airborne_ms;
            actor->airborne_y = net->peer_airborne_y;
            memcpy(actor->slots, net->peer_slots, sizeof(actor->slots));
            actor->current_slot = net->peer_current_slot;
        }
    } else {
        toy_game_set_remote_player(game, 1, 0, 0, 0, NULL);
    }
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
        struct rasterfall_net_remote *remote = &net->remotes[i];
        if (!remote->active || !remote->connected) {
            toy_game_set_remote_player(game, i + 2, 0, 0, 0, NULL);
            continue;
        }
        index = toy_game_set_remote_player(game, i + 2, 1,
                                           remote->camera.x,
                                           remote->camera.z, "PLAYER");
        if (index < 0) continue;
        actor = &game->actors[index];
        actor->sy = remote->camera.sy;
        actor->cy = remote->camera.cy;
        actor->hp = remote->hp;
        actor->state = remote->down ? TOY_GAME_ACTOR_DOWNED :
                                      TOY_GAME_ACTOR_ALIVE;
        actor->airborne_ms = remote->airborne_ms;
        actor->airborne_y = remote->airborne_y;
        memcpy(actor->slots, remote->slots, sizeof(actor->slots));
        actor->current_slot = remote->current_slot;
    }
}

void rasterfall_net_prepare_host_step(struct rasterfall_net *net,
                                      struct toy_game *game)
{
    int i;
    if (!net || !game || net->mode != RASTERFALL_NET_HOST) return;
    toy_game_set_secondary_player_state(game,
        net->peer_known && net->connected,
        net->peer_camera.x, net->peer_camera.z, net->peer_down);
    if (net->peer_known && net->connected)
        game->secondary_player_hp = net->peer_hp;
    game->network_rescuer_available =
        net->peer_known && net->connected && !net->peer_down;
    for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
        if (net->remotes[i].active && net->remotes[i].connected &&
            !net->remotes[i].down)
            game->network_rescuer_available = 1;
    rasterfall_net_sync_remote_players(net, game);
}

void rasterfall_net_apply_local_rescue(struct rasterfall_net *net,
                                       struct rasterfall_session *session,
                                       const struct camera *host_camera,
                                       int interact_pressed, int dt_ms)
{
    int revive;
    if (!net || !session || !host_camera || net->mode != RASTERFALL_NET_HOST)
        return;
    if ((!net->peer_known || !net->connected || !net->peer_down)) {
        int any_down = 0;
        for (int i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++)
            if (net->remotes[i].active && net->remotes[i].connected &&
                net->remotes[i].down) any_down = 1;
        if (!any_down) return;
    }
    if (net->local_revive_peer_active) {
        revive = rasterfall_session_revive_player(
            session, host_camera, &net->peer_camera,
            &net->local_revive_peer_progress_ms, dt_ms);
        if (revive < 0) {
            net->local_revive_peer_active = 0;
            net->local_revive_peer_progress_ms = 0;
            net->peer_revive_progress_ms = 0;
        }
        else if (revive > 0) {
            net->local_revive_peer_active = 0;
            net->peer_down = 0;
            net->peer_hp = TOY_GAME_REVIVE_HP;
            net->peer_revive_progress_ms = 0;
            /* apply_remote() mirrors the secondary player from the game
             * state at the start of its authoritative pass.  Update that
             * mirror too, otherwise the completed rescue is immediately
             * overwritten by the old downed/zero-health state. */
            session->game_state.secondary_player_down = 0;
            session->game_state.secondary_player_hp = TOY_GAME_REVIVE_HP;
        }
        if (net->local_revive_peer_active)
            net->peer_revive_progress_ms = net->local_revive_peer_progress_ms;
    }
    if (net->peer_down && interact_pressed &&
        !net->local_revive_peer_active) {
        revive = rasterfall_session_revive_player(
            session, host_camera, &net->peer_camera,
            &net->local_revive_peer_progress_ms, dt_ms);
        if (revive >= 0) net->local_revive_peer_active = 1;
        if (net->local_revive_peer_active)
            net->peer_revive_progress_ms = net->local_revive_peer_progress_ms;
    }
    for (int remote_i = 0; remote_i < RASTERFALL_NET_REMOTE_MAX; remote_i++) {
        struct rasterfall_net_remote *remote = &net->remotes[remote_i];
        int revive;
        if (!remote->active || !remote->connected || !remote->down) continue;
        if (remote->local_revive_active) {
            revive = rasterfall_session_revive_player(
                session, host_camera, &remote->camera,
                &remote->local_revive_progress_ms, dt_ms);
            if (revive < 0) {
                remote->local_revive_active = 0;
                remote->local_revive_progress_ms = 0;
            } else if (revive > 0) {
                struct toy_game_actor *actor;
                int actor_index;
                remote->local_revive_active = 0;
                remote->local_revive_progress_ms = 0;
                remote->down = 0;
                remote->hp = TOY_GAME_REVIVE_HP;
                remote->revive_progress_ms = 0;
                actor_index = toy_game_set_remote_player(&session->game_state,
                    remote->client_id, 1, remote->camera.x,
                    remote->camera.z, "PLAYER");
                if (actor_index >= 0) {
                    actor = &session->game_state.actors[actor_index];
                    actor->state = TOY_GAME_ACTOR_ALIVE;
                    actor->hp = TOY_GAME_REVIVE_HP;
                    actor->revive_progress_ms = 0;
                }
            }
        }
        if (remote->down && interact_pressed && !remote->local_revive_active) {
            revive = rasterfall_session_revive_player(
                session, host_camera, &remote->camera,
                &remote->local_revive_progress_ms, dt_ms);
            if (revive >= 0) remote->local_revive_active = 1;
        }
        if (remote->local_revive_active)
            remote->revive_progress_ms = remote->local_revive_progress_ms;
    }
}

void rasterfall_net_reset_host(struct rasterfall_net *net)
{
    if (!net || net->mode != RASTERFALL_NET_HOST) return;
    net_reset_peer_state(net);
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
        for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
            if ((session->game_state.actors[i].kind == TOY_GAME_ACTOR_AI ||
                 session->game_state.actors[i].kind == TOY_GAME_ACTOR_PLAYER) &&
                !seen[i])
                session->game_state.actors[i].active = 0;
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
        session->game_state.animation = own->animation;
        session->game_state.player_revive_progress_ms = own->revive_progress_ms;
        session->game_state.kills = own->kills;
        session->game_state.special_kills = own->special_kills;
        session->game_state.damage_dealt = own->damage_dealt;
        session->game_state.state = own->state;
        session->game_state.current_slot = own->current_slot;
        session->game_state.slots[0].weapon = own->slot_weapon[0];
        session->game_state.slots[1].weapon = own->slot_weapon[1];
        session->game_state.slots[0].mag = own->mag[0];
        session->game_state.slots[0].reserve = own->reserve[0];
        session->game_state.slots[1].mag = own->mag[1];
        session->game_state.slots[1].reserve = own->reserve[1];
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
        session->game_state.money = net->snapshot_money;
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
        if (!net->last_jump_command_sequence ||
            net->last_snapshot_input_ack >= net->last_jump_command_sequence) {
            session->game_state.player_airborne_ms = own->airborne_ms;
            session->game_state.player_airborne_y = own->airborne_y;
            camera->y = own->camera.y;
        }
        session->air_walls_enabled = net->snapshot_air_walls_enabled;
        session->manual_alarm_on = net->snapshot_manual_alarm_enabled;
        session->manual_alarm_timer = net->snapshot_world_manual_alarm_timer_ms;
        rasterfall_map_set_air_walls(&session->map_ops,
                                     session->air_walls_enabled);
    }
    if (session && net->enemy_count >= 0) {
        int i;
        int seen[TOY_GAME_MAX_ENEMIES];
        memset(seen, 0, sizeof(seen));
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
            dst->special_target_active = src->special_target_active;
            dst->charge_active = src->charge_active;
            dst->special_timer_ms = src->special_timer_ms;
            dst->special_windup_ms = src->special_windup_ms;
            dst->special_target_player = src->special_target_player;
            dst->special_target_actor_index = src->special_target_actor_index;
            dst->special_pull_timer_ms = src->special_pull_timer_ms;
            dst->charge_dir_x = src->charge_dir_x;
            dst->charge_dir_z = src->charge_dir_z;
            dst->charge_elapsed_ms = src->charge_elapsed_ms;
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
            seen[index] = 1;
        }
        for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++)
            if (!seen[i])
                memset(&session->game_state.enemies[i], 0,
                       sizeof(session->game_state.enemies[i]));
        net_smooth_client_enemies(net, session);
    }
    if (net->last_jump_command_sequence &&
        net->last_snapshot_input_ack < net->last_jump_command_sequence) {
        net->snapshot_ready = 0;
        return;
    }
    dx = own->camera.x - camera->x;
    dz = own->camera.z - camera->z;
    dist2 = dx * dx + dz * dz;
    if (dist2 > 400L * 400L) {
        camera->x = own->camera.x;
        camera->z = own->camera.z;
    } else {
        camera->x += (int)(dx / 4);
        camera->z += (int)(dz / 4);
    }
    net->snapshot_ready = 0;
}
