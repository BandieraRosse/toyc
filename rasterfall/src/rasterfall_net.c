#include "tlibc_everything.h"
#include "errno.h"
#include "rasterfall_net.h"

#define NET_HEADER_SIZE 16
#define NET_MAGIC_0 'R'
#define NET_MAGIC_1 'F'
#define NET_MAGIC_2 'N'
#define NET_MAGIC_3 '1'
#define NET_INPUT_SIZE 32
#define NET_PLAYER_BASE_SIZE 52
#define NET_PLAYER_RAY_SIZE 15
#define NET_PLAYER_SIZE (NET_PLAYER_BASE_SIZE + 4 + 1 + TOY_GAME_MAX_RAYS * NET_PLAYER_RAY_SIZE)
#define NET_ACTOR_SIZE 25
#define NET_ENEMY_SIZE 33
#define NET_EVENT_SIZE (4 + 1 + TOY_GAME_MAX_EVENTS)
#define NET_WORLD_SIZE 32
#define NET_SNAPSHOT_BASE 8
#define NET_INPUT_HOLD_TICKS 15
#define NET_AI_FIRE_BASE 6
#define NET_SNAPSHOT_PART_BASE 10
#define NET_SNAPSHOT_FRAGMENT_DATA 1000

static long net_monotonic_ms(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static void net_push_event(struct toy_game *game, unsigned char event)
{
    if (game->event_count < TOY_GAME_MAX_EVENTS)
        game->events[game->event_count++] = event;
}

static void net_queue_remote_events(struct rasterfall_net *net,
                                    const unsigned char *events, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        /* Fire audio is reconstructed from the reliable fire_seq in the
         * player snapshot; keeping SHOOT here would play it twice. */
        if (events[i] == TOY_GAME_EV_SHOOT) continue;
        if (net->remote_event_queue_count >= RASTERFALL_NET_EVENT_QUEUE_MAX)
            break;
        net->remote_event_queue[net->remote_event_queue_count] = events[i];
        net->remote_event_ids[net->remote_event_queue_count] =
            ++net->remote_event_next_id;
        net->remote_event_queue_count++;
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
    return (unsigned char)(weapon < 0 ? 0 : weapon + 1);
}

static int get_weapon_value(unsigned char value)
{
    int weapon = (int)value - 1;
    return weapon >= -1 && weapon < TOY_GAME_WEAPON_COUNT ? weapon : -1;
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
    return sent == size ? 0 : -1;
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
    memset(net->peer_slots, 0, sizeof(net->peer_slots));
    net->peer_current_slot = 0;
    net->peer_hp = 0;
    net->peer_state = TOY_GAME_PLAYING;
    net->peer_down = 0;
    net->peer_revive_progress_ms = 0;
    net->peer_revive_active = 0;
    net->peer_host_revive_active = 0;
    net->peer_host_revive_progress_ms = 0;
    net->local_revive_peer_active = 0;
    net->local_revive_peer_progress_ms = 0;
    net->peer_reloading = 0;
    net->peer_reload_timer_ms = 0;
    net->peer_fire_cooldown_ms = 0;
    net->peer_muzzle_flash_ms = 0;
    net->peer_damage_flash_ms = 0;
    net->peer_kills = 0;
    net->peer_fire_seq = 0;
    net->peer_ray_count = 0;
    net->peer_state_initialized = 0;
    net->peer_camera_initialized = 0;
    net->peer_reported_camera_ready = 0;
    net->remote_command_ready = 0;
    net->last_input_sequence = 0;
    net->remote_event_queue_count = 0;
}

int rasterfall_net_host(struct rasterfall_net *net, int port,
                        const struct camera *spawn)
{
    struct sockaddr_in address;
    int reuse = 1;
    rasterfall_net_init(net);
    net->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (net->fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = INADDR_ANY;
    setsockopt(net->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(net->fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        __close(net->fd);
        net->fd = -1;
        return -1;
    }
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
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
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
    net->last_command_sequence = net->send_sequence;
    net->last_command_sent_ms = net_monotonic_ms();
    return net_send(net, packet, size);
}

static void encode_player(unsigned char *p, int id, int active,
                          const struct camera *camera, int hp,
                          int weapon, int state, int downed,
                          int revive_progress_ms,
                          const struct toy_game_slot *slots,
                          int current_slot, int reloading, int reload_timer_ms,
                          int muzzle_flash_ms, int kills,
                          unsigned int fire_seq, int ray_count,
                          const struct toy_game_ray *rays,
                          int airborne_ms, int airborne_y)
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
    player->hp = get_i16(p + 20);
    player->current_slot = p[22] < TOY_GAME_WEAPON_SLOTS ? p[22] : 0;
    player->slot_weapon[0] = get_weapon_value(p[23]);
    player->slot_weapon[1] = get_weapon_value(p[24]);
    player->mag[0] = get_i16(p + 25); player->reserve[0] = get_i16(p + 27);
    player->mag[1] = get_i16(p + 29); player->reserve[1] = get_i16(p + 31);
    player->reloading = p[33] != 0; player->reload_timer_ms = get_i16(p + 34);
    player->muzzle_flash_ms = get_i16(p + 36);
    player->kills = get_i16(p + 38);
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

static void encode_enemy(unsigned char *p, const struct toy_game_enemy *e)
{
    p[0] = (unsigned char)e->active;
    p[1] = (unsigned char)e->type;
    p[2] = (unsigned char)e->ai_state;
    put_i16(p + 3, e->hp);
    put_u32(p + 5, (uint32_t)e->x); put_u32(p + 9, (uint32_t)e->z);
    put_i16(p + 13, e->speed); put_i16(p + 15, e->bite_cooldown_ms);
    put_i16(p + 17, e->flash); put_i16(p + 19, e->hurt);
    put_i16(p + 21, e->dying_ms);
    put_i16(p + 23, e->dir_x); put_i16(p + 25, e->dir_z);
    p[27] = (unsigned char)(e->special_target_active ? 1 : 0);
    p[28] = (unsigned char)(e->charge_active ? 1 : 0);
    put_i16(p + 29, e->airborne_ms);
    put_i16(p + 31, e->airborne_y);
}

static void decode_enemy(const unsigned char *p, struct rasterfall_net_enemy *e)
{
    memset(e, 0, sizeof(*e));
    e->active = p[0]; e->type = p[1]; e->ai_state = p[2];
    e->hp = get_i16(p + 3);
    e->x = (int)get_u32(p + 5); e->z = (int)get_u32(p + 9);
    e->speed = get_i16(p + 13); e->bite_cooldown_ms = get_i16(p + 15);
    e->flash = get_i16(p + 17); e->hurt = get_i16(p + 19);
    e->dying_ms = get_i16(p + 21);
    e->dir_x = get_i16(p + 23); e->dir_z = get_i16(p + 25);
    e->special_target_active = p[27] & 1;
    e->charge_active = p[28] & 1;
    e->airborne_ms = get_i16(p + 29);
    e->airborne_y = get_i16(p + 31);
}

static void encode_actor(unsigned char *p, const struct toy_game_actor *a,
                        int actor_index)
{
    p[0] = (unsigned char)((a->class_id & 3) << 2);
    p[0] |= (unsigned char)((a->state & 3) << 4);
    p[0] |= (unsigned char)((put_weapon_value(
        a->slots[a->current_slot].weapon) & 3) << 6);
    p[1] = (unsigned char)actor_index;
    put_u32(p + 2, (uint32_t)a->x); put_u32(p + 6, (uint32_t)a->z);
    put_i16(p + 10, a->sy); put_i16(p + 12, a->cy);
    put_i16(p + 14, a->hp);
    p[16] = (unsigned char)(a->state == TOY_GAME_ACTOR_DOWNED ?
                            a->revive_progress_ms / 12 :
                            (a->muzzle_flash_ms < 0 ? 0 :
                             a->muzzle_flash_ms > 255 ? 255 : a->muzzle_flash_ms));
    put_u32(p + 17, a->fire_seq);
    put_i16(p + 21, a->airborne_ms);
    put_i16(p + 23, a->airborne_y);
}

static void decode_actor(const unsigned char *p, struct rasterfall_net_actor *a)
{
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->actor_index = p[1];
    a->class_id = (p[0] >> 2) & 3;
    a->state = (p[0] >> 4) & 3;
    a->weapon = get_weapon_value((p[0] >> 6) & 3);
    a->x = (int)get_u32(p + 2); a->z = (int)get_u32(p + 6);
    a->sy = get_i16(p + 10); a->cy = get_i16(p + 12);
    a->hp = get_i16(p + 14);
    a->airborne_ms = get_i16(p + 21);
    a->airborne_y = get_i16(p + 23);
    if (a->state == TOY_GAME_ACTOR_DOWNED)
        a->revive_progress_ms = p[16] * 12;
    else
        a->muzzle_flash_ms = p[16];
    a->fire_seq = get_u32(p + 17);
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
        if (net_send(net, packet, size) < 0) return -1;
        net->ai_fire_sent_seq[actor_index] = actor->fire_seq;
    }
    return 0;
}

int rasterfall_net_send_snapshot(struct rasterfall_net *net,
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
    int actor_i;
    for (actor_i = 0; actor_i < TOY_GAME_MAX_ACTORS &&
         actor_count < RASTERFALL_NET_MAX_ACTORS; actor_i++)
        if (game->actors[actor_i].active)
            actor_indices[actor_count++] = actor_i;
    int payload_size = NET_SNAPSHOT_BASE +
                       RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE + 1 +
                       actor_count * NET_ACTOR_SIZE +
                       TOY_GAME_MAX_ENEMIES * NET_ENEMY_SIZE + NET_EVENT_SIZE + NET_WORLD_SIZE;
    int size, offset, part_count, part_index;
    if (net->mode != RASTERFALL_NET_HOST || !net->peer_known) return -1;
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
                  game->muzzle_flash_ms, game->kills, game->fire_seq,
                  game->ray_count, game->rays,
                  game->player_airborne_ms, game->player_airborne_y);
    encode_player(p + NET_SNAPSHOT_BASE + NET_PLAYER_SIZE, 1,
                  net->peer_known, &net->peer_camera, net->peer_hp,
                  peer_weapon, net->peer_state, net->peer_down,
                  net->peer_revive_progress_ms, net->peer_slots,
                  net->peer_current_slot, net->peer_reloading,
                  net->peer_reload_timer_ms, net->peer_muzzle_flash_ms,
                  net->peer_kills, net->peer_fire_seq,
                  net->peer_ray_count, net->peer_rays,
                  net->peer_airborne_ms, net->peer_airborne_y);
    for (actor_i = 0; actor_i < actor_count; actor_i++) {
        encode_actor(p + NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                     actor_i * NET_ACTOR_SIZE, &game->actors[actor_indices[actor_i]],
                     actor_indices[actor_i]);
    }
    p[NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
      actor_count * NET_ACTOR_SIZE] =
        TOY_GAME_MAX_ENEMIES;
    for (int i = 0; i < TOY_GAME_MAX_ENEMIES; i++)
        encode_enemy(p + NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                     actor_count * NET_ACTOR_SIZE + 1 + i * NET_ENEMY_SIZE,
                     &game->enemies[i]);
    event_data = p + NET_SNAPSHOT_BASE + RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE +
                 actor_count * NET_ACTOR_SIZE + 1 + TOY_GAME_MAX_ENEMIES * NET_ENEMY_SIZE;
    world_data = event_data + NET_EVENT_SIZE;
    event_count = net->remote_event_queue_count;
    if (event_count > TOY_GAME_MAX_EVENTS) event_count = TOY_GAME_MAX_EVENTS;
    put_u32(event_data, event_count ? net->remote_event_ids[0] : 0);
    event_data[4] = (unsigned char)event_count;
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
        if (net_send(net, packet, size) < 0) return -1;
    }
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
                TOY_GAME_MAX_ENEMIES * NET_ENEMY_SIZE + NET_EVENT_SIZE + NET_WORLD_SIZE) return -1;
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
    event_data = payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                 net->actor_count * NET_ACTOR_SIZE + 1 +
                 TOY_GAME_MAX_ENEMIES * NET_ENEMY_SIZE;
    world_data = event_data + NET_EVENT_SIZE;
    net->enemy_count = payload[NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                               net->actor_count * NET_ACTOR_SIZE];
    if (net->enemy_count > TOY_GAME_MAX_ENEMIES) return -1;
    for (i = 0; i < net->enemy_count; i++)
        decode_enemy(payload + NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE +
                     net->actor_count * NET_ACTOR_SIZE + 1 + i * NET_ENEMY_SIZE,
                     &net->enemies[i]);
    {
        uint32_t first_id = get_u32(event_data);
        int event_count = event_data[4];
        int accepted = 0;
        if (event_count > TOY_GAME_MAX_EVENTS) return -1;
        for (i = 0; i < event_count; i++) {
            uint32_t id = first_id + (uint32_t)i;
            if (id <= net->remote_event_last_id) continue;
            net->remote_events[accepted++] =
                event_data[5 + i];
            net->remote_event_last_id = id;
        }
        net->remote_event_count = accepted;
    }
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
    net->snapshot_player_control_disabled = world_data[30] & 1;
    net->snapshot_ready = 1;
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
        uint32_t sequence, ack;
        source_len = sizeof(source);
        received = recvfrom(net->fd, packet, sizeof(packet), 0,
                            (struct sockaddr *)&source, &source_len);
        if (received < 0) {
            if (received == -EAGAIN) return;
            return;
        }
        if (net->public_room && punch_packet(packet, (int)received, PUNCH_MATCH)) {
            uint32_t match_token;
            int new_match;
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
                memset(&net->peer, 0, sizeof(net->peer));
                net->peer.sin_family = AF_INET;
                memcpy(&net->peer.sin_addr.s_addr, packet + 12, 4);
                net->peer.sin_port = htons((unsigned short)get_u16(packet + 16));
            }
            net->peer_known = 1;
            net->public_matched = 1;
            if (net->relay_mode) net->connected = 1;
            if (net->mode == RASTERFALL_NET_HOST && new_match) {
                /* A server-side room reset starts a new session generation.
                 * Drop the old remote inventory/state before the next input,
                 * otherwise the host can render a stale player model. */
                net_reset_peer_state(net);
            }
            net->last_public_punch_ms = 0;
            if (!net->relay_mode) punch_send_probe(net);
            if (net->mode == RASTERFALL_NET_CLIENT) {
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
        if (packet_header(packet, (int)received, &type, &payload_size,
                          &sequence, &ack) < 0) continue;
        net->last_receive_ms = net_monotonic_ms();
        if (net->mode == RASTERFALL_NET_HOST) {
            if (!net->peer_known) {
                if (type != RASTERFALL_NET_HELLO && type != RASTERFALL_NET_INPUT)
                    continue;
                memcpy(&net->peer, &source, sizeof(source));
                net->peer_known = 1;
                net->connected = 1;
                memcpy(&net->peer_camera, &net->peer_spawn, sizeof(net->peer_camera));
                net->peer_camera_initialized = 0;
                net->peer_reported_camera_ready = 0;
            } else if (!net->public_room && !same_peer(&net->peer, &source)) {
                continue;
            } else if (net->public_room) {
                /* 以已打洞成功的实际来源为准，适配 endpoint-dependent NAT。 */
                memcpy(&net->peer, &source, sizeof(source));
            }
            if (type == RASTERFALL_NET_INPUT &&
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
                if (ack == net->last_snapshot_sequence &&
                    net->last_snapshot_sent_ms) {
                    long elapsed = net_monotonic_ms() - net->last_snapshot_sent_ms;
                    if (elapsed >= 0 && elapsed < 60000) net->rtt_ms = (int)elapsed;
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
            if (type == RASTERFALL_NET_AI_FIRE &&
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
                    if (elapsed >= 0 && elapsed < 60000) net->rtt_ms = (int)elapsed;
                }
            }
        }
    }
}

void rasterfall_net_update_connection(struct rasterfall_net *net)
{
    long now;
    unsigned char hello[NET_HEADER_SIZE];
    int size;
    if (!net || net->fd < 0 || net->mode == RASTERFALL_NET_OFF) return;
    now = net_monotonic_ms();
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
    if (net->last_receive_ms && now - net->last_receive_ms > 3000) {
        net->connected = 0;
        net->last_input_sequence = 0;
        net->receive_sequence = 0;
        net->remote_command_ready = 0;
        net->snapshot_ready = 0;
        net->remote_event_queue_count = 0;
        net->remote_event_next_id = 0;
        net->remote_event_last_id = 0;
        net->remote_event_snapshot_last_id = 0;
        net->remote_event_snapshot_sequence = 0;
        if (net->mode == RASTERFALL_NET_HOST) {
            /* 允许另一台主机重新接入，而不是永久锁死旧地址。 */
                net->peer_known = 0;
                net->peer_state_initialized = 0;
                net->peer_camera_initialized = 0;
                net->peer_reported_camera_ready = 0;
        }
    }
    if (net->mode == RASTERFALL_NET_CLIENT &&
        ((!net->connected && !net->public_room) ||
         (net->public_room && net->public_matched)) &&
        (!net->last_hello_ms || now - net->last_hello_ms >= 500)) {
        size = packet_begin(hello, RASTERFALL_NET_HELLO, 0,
                            ++net->send_sequence, net->receive_sequence);
        if (net_send(net, hello, size) == 0) net->last_hello_ms = now;
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
    int host_damage, host_kills, host_ray_count;
    unsigned int host_fire_seq;
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
        memcpy(net->peer_slots, g->slots, sizeof(net->peer_slots));
        net->peer_current_slot = g->current_slot;
        net->peer_hp = g->hp;
        net->peer_state = g->state;
        net->peer_down = 0;
        net->peer_state_initialized = 1;
    }
    /* Charger/Smoker 对远端玩家的影响在本地主机规则步中完成，随后写回
     * 远端玩家的权威状态，确保客户端看到同一套血量与击飞状态。 */
    if (net->peer_known) {
        net->peer_camera.x = g->secondary_px;
        net->peer_camera.z = g->secondary_pz;
        net->peer_hp = g->secondary_player_hp;
        net->peer_down = g->secondary_player_down;
        net->peer_airborne_ms = g->secondary_player_airborne_ms;
        net->peer_airborne_y = g->secondary_player_airborne_y;
    }
    if (net->peer_known && net->tick - net->last_input_tick <= NET_INPUT_HOLD_TICKS) {
        net->remote_event_count = 0;
        event_start = g->event_count;
        if (!net->peer_down)
            rasterfall_session_step_remote_player(session, &net->peer_camera,
                                                  &net->remote_command,
                                                  net->peer_down);
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
        host_ray_count = g->ray_count; host_fire_seq = g->fire_seq;
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
        g->fire_seq = net->peer_fire_seq;
        g->px = net->peer_camera.x; g->pz = net->peer_camera.z;
        /* 交互必须在装载玩家2的武器槽之后执行；否则拾取武器/弹药
         * 会错误地写入主机玩家槽位。世界级交互仍然作用于同一权威世界。 */
        if ((net->remote_command.buttons & RASTERFALL_CMD_INTERACT) &&
            !net->peer_down) {
            rasterfall_session_interact_remote(session, &net->peer_camera);
        }
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
            net_queue_remote_events(net, g->events + event_start, event_count);
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
        net->peer_kills = g->kills; net->peer_fire_seq = g->fire_seq;
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
        g->ray_count = host_ray_count; g->fire_seq = host_fire_seq;
        memcpy(g->rays, host_rays, sizeof(g->rays));
    }
    net->remote_command.turn = 0;
    net->remote_command.pitch = 0;
    net->remote_command.buttons = 0;
}

void rasterfall_net_apply_local_rescue(struct rasterfall_net *net,
                                       struct rasterfall_session *session,
                                       const struct camera *host_camera,
                                       int interact_pressed, int dt_ms)
{
    int revive;
    if (!net || !session || !host_camera || net->mode != RASTERFALL_NET_HOST ||
        !net->peer_known || !net->connected || !net->peer_down) return;
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
}

void rasterfall_net_reset_host(struct rasterfall_net *net)
{
    if (!net || net->mode != RASTERFALL_NET_HOST) return;
    net_reset_peer_state(net);
}

void rasterfall_net_reconcile_client(struct rasterfall_net *net,
                                     struct rasterfall_session *session,
                                     struct camera *camera)
{
    const struct rasterfall_net_player *own;
    long dx, dz, dist2;
    if (net->mode != RASTERFALL_NET_CLIENT || !net->snapshot_ready) return;
    own = &net->players[1];
    if (!own->active) return;
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
            dst->kind = TOY_GAME_ACTOR_AI;
            dst->class_id = src->class_id;
            dst->state = src->state;
            dst->x = src->x; dst->z = src->z;
            dst->sy = src->sy; dst->cy = src->cy;
            dst->hp = src->hp;
            dst->muzzle_flash_ms = src->muzzle_flash_ms;
            dst->fire_seq = src->fire_seq;
            dst->airborne_ms = src->airborne_ms;
            dst->airborne_y = src->airborne_y;
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
            if (session->game_state.actors[i].kind == TOY_GAME_ACTOR_AI &&
                !seen[i])
                session->game_state.actors[i].active = 0;
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
        session->game_state.player_revive_progress_ms = own->revive_progress_ms;
        session->game_state.kills = own->kills;
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
        session->game_state.player_control_disabled =
            net->snapshot_player_control_disabled;
        session->game_state.player_airborne_ms = own->airborne_ms;
        session->game_state.player_airborne_y = own->airborne_y;
        session->air_walls_enabled = net->snapshot_air_walls_enabled;
        session->manual_alarm_on = net->snapshot_manual_alarm_enabled;
        session->manual_alarm_timer = net->snapshot_world_manual_alarm_timer_ms;
        rasterfall_map_set_air_walls(&session->map_ops,
                                     session->air_walls_enabled);
    }
    if (session && net->enemy_count >= 0) {
        int i;
        for (i = 0; i < net->enemy_count; i++) {
            struct toy_game_enemy *dst = &session->game_state.enemies[i];
            const struct rasterfall_net_enemy *src = &net->enemies[i];
            int old_active = dst->active;
            dst->active = src->active; dst->type = src->type;
            dst->ai_state = src->ai_state;
            dst->hp = src->hp; dst->x = src->x; dst->z = src->z;
            dst->speed = src->speed; dst->bite_cooldown_ms = src->bite_cooldown_ms;
            dst->flash = src->flash; dst->hurt = src->hurt;
            dst->dying_ms = src->dying_ms;
            dst->special_target_active = src->special_target_active;
            dst->charge_active = src->charge_active;
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
        for (; i < TOY_GAME_MAX_ENEMIES; i++)
            memset(&session->game_state.enemies[i], 0,
                   sizeof(session->game_state.enemies[i]));
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

int rasterfall_net_self_test(void)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET];
    struct rasterfall_command input, output;
    struct camera camera;
    struct rasterfall_net net;
    struct rasterfall_net host, client;
    struct toy_game test_game;
    struct toy_game_slot test_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray test_ray;
    int size, type, payload_size;
    int port, i, transport_result = 0;
    uint32_t sequence;
    memset(&input, 0, sizeof(input));
    memset(&camera, 0, sizeof(camera));
    input.move_forward = 1;
    input.move_strafe = -1;
    input.turn = -321;
    input.pitch = 123;
    input.buttons = RASTERFALL_CMD_FIRE | RASTERFALL_CMD_INTERACT |
                    RASTERFALL_CMD_SHOVE;
    input.fire_held = 1;
    camera.x = -123456;
    camera.z = 654321;
    size = encode_command(packet, 0x10203040U, 7, 99, &input, &camera);
    if (size != NET_HEADER_SIZE + NET_INPUT_SIZE) return 1;
    if (packet_header(packet, size, &type, &payload_size, &sequence, NULL) < 0 ||
        type != RASTERFALL_NET_INPUT || sequence != 0x10203040U ||
        payload_size != NET_INPUT_SIZE) return 2;
    if (decode_command(packet + NET_HEADER_SIZE, payload_size, &output) < 0 ||
        output.move_forward != input.move_forward ||
        output.move_strafe != input.move_strafe || output.turn != input.turn ||
        output.pitch != input.pitch || output.buttons != input.buttons ||
        output.fire_held != input.fire_held) return 3;
    packet[4]++;
    if (packet_header(packet, size, &type, &payload_size, &sequence, NULL) == 0) return 4;
    packet[4]--;
    if (packet_header(packet, size - 1, &type, &payload_size, &sequence, NULL) == 0)
        return 5;
    rasterfall_net_init(&net);
    camera.sy = 0;
    camera.cy = 1024;
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;
    memset(test_slots, 0, sizeof(test_slots));
    test_slots[0].weapon = TOY_GAME_WEAPON_SMG;
    test_slots[1].weapon = TOY_GAME_WEAPON_SHOTGUN;
    test_slots[0].mag = 11; test_slots[0].reserve = 37;
    test_slots[1].mag = 6; test_slots[1].reserve = TOY_GAME_AMMO_INFINITE;
    memset(&test_ray, 0, sizeof(test_ray));
    test_ray.sy = 700; test_ray.cy = 746; test_ray.vy = -13;
    test_ray.ex = 1234; test_ray.ez = -5678; test_ray.hit_enemy = 1;
    encode_player(packet, 1, 1, &camera, 87, TOY_GAME_WEAPON_SMG,
                  TOY_GAME_PLAYING, 1, 123, test_slots, 0, 1, 240, 60, 4,
                  9, 1, &test_ray, 700, 88);
    if (decode_player(packet, &net.players[1]) < 0 ||
        net.players[1].camera.x != camera.x ||
        net.players[1].camera.z != camera.z || net.players[1].hp != 87 ||
        !net.players[1].downed || net.players[1].revive_progress_ms != 123 ||
        net.players[1].weapon != TOY_GAME_WEAPON_SMG ||
        net.players[1].slot_weapon[0] != TOY_GAME_WEAPON_SMG ||
        net.players[1].slot_weapon[1] != TOY_GAME_WEAPON_SHOTGUN ||
        net.players[1].mag[0] != 11 || net.players[1].reserve[0] != 37 ||
        !net.players[1].reloading || net.players[1].reload_timer_ms != 240 ||
        net.players[1].muzzle_flash_ms != 60 || net.players[1].kills != 4 ||
        net.players[1].fire_seq != 9 || net.players[1].ray_count != 1 ||
        net.players[1].rays[0].ex != 1234 || net.players[1].rays[0].ez != -5678 ||
        !net.players[1].rays[0].hit_enemy) return 6;
    toy_game_init(&test_game, 1);
    test_game.actors[0].class_id = TOY_GAME_AI_LEVEL_3;
    test_game.actors[0].state = TOY_GAME_ACTOR_DOWNED;
    test_game.actors[0].x = -4321; test_game.actors[0].z = 8765;
    test_game.actors[0].hp = 17;
    encode_actor(packet, &test_game.actors[0], 0);
    decode_actor(packet, &net.actors[0]);
    if (net.actors[0].class_id != TOY_GAME_AI_LEVEL_3 ||
        net.actors[0].state != TOY_GAME_ACTOR_DOWNED ||
        net.actors[0].x != -4321 || net.actors[0].z != 8765 ||
        net.actors[0].hp != 17) return 13;

    /* 真正经过 localhost UDP socket 的输入与快照回环，覆盖非阻塞
     * sendto/recvfrom、对端锁定和包头校验。逐个尝试测试端口，避免并行
     * 测试进程偶然占用某一个固定端口。 */
    rasterfall_net_init(&host);
    rasterfall_net_init(&client);
    port = 0;
    for (i = 0; i < 16; i++) {
        int candidate = 39000 + i;
        if (rasterfall_net_host(&host, candidate, &camera) == 0) {
            port = candidate;
            break;
        }
    }
    if (!port) return 7;
    if (rasterfall_net_connect(&client, "127.0.0.1", port) < 0) {
        rasterfall_net_close(&host);
        return 8;
    }
    if (rasterfall_net_send_command(&client, &input, &camera) < 0)
        transport_result = 9;
    for (i = 0; i < 1000 && !host.remote_command_ready; i++)
        rasterfall_net_poll(&host);
    if (!transport_result && (!host.peer_known || !host.remote_command_ready ||
        host.remote_command.move_forward != 1 ||
        host.remote_command.move_strafe != -1 ||
        host.remote_command.turn != -321)) transport_result = 10;
    if (toy_game_add_ai(&test_game, TOY_GAME_AI_LEVEL_2,
                        -2400, -1200, "SOLDIER") < 0 ||
        toy_game_add_ai(&test_game, TOY_GAME_AI_LEVEL_3,
                        2400, -1200, "ELITE") < 0)
        transport_result = 14;
    for (i = 0; i < 5; i++)
        if (toy_game_add_ai(&test_game, i % TOY_GAME_AI_CLASS_COUNT,
                            -1800 + i * 600, 1200, "BACKUP") < 0)
            transport_result = 14;
    test_game.actors[0].fire_seq = 4;
    test_game.actors[0].ray_count = 1;
    test_game.actors[0].rays[0].ex = 2222;
    test_game.actors[0].rays[0].ez = -3333;
    if (!transport_result &&
        rasterfall_net_send_snapshot(&host, &camera, &test_game, 1, 0, 0) < 0)
        transport_result = 11;
    for (i = 0; i < 1000 && !client.snapshot_ready; i++)
        rasterfall_net_poll(&client);
    if (!transport_result && (!client.snapshot_ready ||
        !client.players[0].active || client.players[0].camera.x != camera.x ||
        !client.players[1].active || client.actor_count != 8 ||
        client.actors[1].actor_index != 1 ||
        client.actors[2].actor_index != 2 ||
        client.actors[7].actor_index != 7 ||
        client.actors[0].ray_count != 1 ||
        client.actors[0].rays[0].ex != 2222 ||
        client.actors[0].rays[0].ez != -3333)) {
        transport_result = 12;
    }
    rasterfall_net_close(&client);
    rasterfall_net_close(&host);
    return transport_result;
}
