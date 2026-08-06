#include "tlibc_everything.h"
#include "errno.h"
#include "rasterfall_net.h"

#define NET_HEADER_SIZE 16
#define NET_MAGIC_0 'R'
#define NET_MAGIC_1 'F'
#define NET_MAGIC_2 'N'
#define NET_MAGIC_3 '1'
#define NET_INPUT_SIZE 24
#define NET_PLAYER_SIZE 22
#define NET_SNAPSHOT_BASE 8
#define NET_INPUT_HOLD_TICKS 15

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
                         int *payload_size, uint32_t *sequence)
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
    return 0;
}

static int same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family && a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static int net_send(struct rasterfall_net *net, unsigned char *packet, int size)
{
    long sent;
    if (net->fd < 0 || !net->peer_known || size <= 0) return -1;
    sent = sendto(net->fd, packet, (size_t)size, 0,
                  (const struct sockaddr *)&net->peer, sizeof(net->peer));
    return sent == size ? 0 : -1;
}

void rasterfall_net_init(struct rasterfall_net *net)
{
    memset(net, 0, sizeof(struct rasterfall_net));
    net->fd = -1;
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
    if (spawn) memcpy(&net->peer_camera, spawn, sizeof(struct camera));
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
    size = packet_begin(hello, RASTERFALL_NET_HELLO, 0,
                        ++net->send_sequence, 0);
    net_send(net, hello, size);
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
    return net_send(net, packet, size);
}

static void encode_player(unsigned char *p, int id, int active,
                          const struct camera *camera, int hp,
                          int weapon, int state)
{
    p[0] = (unsigned char)(active != 0);
    p[1] = (unsigned char)id;
    p[2] = (unsigned char)weapon;
    p[3] = (unsigned char)state;
    put_u32(p + 4, (uint32_t)camera->x);
    put_u32(p + 8, (uint32_t)camera->z);
    put_i16(p + 12, camera->sy);
    put_i16(p + 14, camera->cy);
    put_i16(p + 16, camera->pitch_sy);
    put_i16(p + 18, camera->pitch_cy);
    put_i16(p + 20, hp);
}

static int decode_player(const unsigned char *p,
                         struct rasterfall_net_player *player)
{
    player->active = p[0] != 0;
    player->id = p[1];
    player->weapon = p[2];
    player->state = p[3];
    player->camera.x = (int)get_u32(p + 4);
    player->camera.z = (int)get_u32(p + 8);
    player->camera.sy = get_i16(p + 12);
    player->camera.cy = get_i16(p + 14);
    player->camera.pitch_sy = get_i16(p + 16);
    player->camera.pitch_cy = get_i16(p + 18);
    player->hp = get_i16(p + 20);
    return player->id >= 0 && player->id < RASTERFALL_NET_PLAYER_MAX ? 0 : -1;
}

int rasterfall_net_send_snapshot(struct rasterfall_net *net,
                                 const struct camera *host_camera,
                                 const struct toy_game *game)
{
    unsigned char packet[NET_HEADER_SIZE + NET_SNAPSHOT_BASE +
                         RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE];
    unsigned char *p = packet + NET_HEADER_SIZE;
    int weapon = game->slots[game->current_slot].weapon;
    int peer_weapon = net->peer_slots[net->peer_current_slot].weapon;
    int payload_size = NET_SNAPSHOT_BASE +
                       RASTERFALL_NET_PLAYER_MAX * NET_PLAYER_SIZE;
    int size;
    if (net->mode != RASTERFALL_NET_HOST || !net->peer_known) return -1;
    size = packet_begin(packet, RASTERFALL_NET_SNAPSHOT, payload_size,
                        ++net->send_sequence, net->receive_sequence);
    put_u32(p, net->tick);
    p[4] = RASTERFALL_NET_PLAYER_MAX;
    p[5] = p[6] = p[7] = 0;
    encode_player(p + NET_SNAPSHOT_BASE, 0, 1, host_camera, game->hp,
                  weapon, game->state);
    encode_player(p + NET_SNAPSHOT_BASE + NET_PLAYER_SIZE, 1,
                  net->peer_known, &net->peer_camera, net->peer_hp,
                  peer_weapon, net->peer_state);
    return net_send(net, packet, size);
}

static int decode_snapshot(const unsigned char *payload, int size,
                           struct rasterfall_net *net)
{
    int count, i;
    if (size < NET_SNAPSHOT_BASE) return -1;
    count = payload[4];
    if (count < 0 || count > RASTERFALL_NET_PLAYER_MAX ||
        size != NET_SNAPSHOT_BASE + count * NET_PLAYER_SIZE) return -1;
    for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++)
        net->players[i].active = 0;
    for (i = 0; i < count; i++) {
        struct rasterfall_net_player player;
        if (decode_player(payload + NET_SNAPSHOT_BASE + i * NET_PLAYER_SIZE,
                          &player) < 0) return -1;
        memcpy(&net->players[player.id], &player,
               sizeof(struct rasterfall_net_player));
    }
    net->snapshot_ready = 1;
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
        uint32_t sequence;
        source_len = sizeof(source);
        received = recvfrom(net->fd, packet, sizeof(packet), 0,
                            (struct sockaddr *)&source, &source_len);
        if (received < 0) {
            if (received == -EAGAIN) return;
            return;
        }
        if (packet_header(packet, (int)received, &type, &payload_size,
                          &sequence) < 0) continue;
        if (net->mode == RASTERFALL_NET_HOST) {
            if (!net->peer_known) {
                if (type != RASTERFALL_NET_HELLO && type != RASTERFALL_NET_INPUT)
                    continue;
                memcpy(&net->peer, &source, sizeof(source));
                net->peer_known = 1;
            } else if (!same_peer(&net->peer, &source)) {
                continue;
            }
            if (type == RASTERFALL_NET_INPUT &&
                sequence > net->last_input_sequence &&
                decode_command(packet + NET_HEADER_SIZE, payload_size,
                               &net->remote_command) == 0) {
                net->last_input_sequence = sequence;
                net->receive_sequence = sequence;
                net->remote_command_ready = 1;
            }
        } else if (net->mode == RASTERFALL_NET_CLIENT) {
            if (!same_peer(&net->peer, &source)) continue;
            if (type == RASTERFALL_NET_SNAPSHOT &&
                sequence > net->receive_sequence &&
                decode_snapshot(packet + NET_HEADER_SIZE, payload_size, net) == 0)
                net->receive_sequence = sequence;
        }
    }
}

void rasterfall_net_apply_remote(struct rasterfall_net *net,
                                 struct rasterfall_session *session)
{
    struct toy_game *g = &session->game_state;
    struct toy_game_slot host_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray host_rays[TOY_GAME_MAX_RAYS];
    int host_px, host_pz, host_hp, host_state, host_current;
    int host_reloading, host_reload_timer, host_cooldown, host_muzzle;
    int host_damage, host_kills, host_ray_count;
    unsigned int host_fire_seq;
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    net->tick++;
    if (net->remote_command_ready) {
        net->last_input_tick = net->tick;
        net->remote_command_ready = 0;
    }
    if (net->peer_known && !net->peer_state_initialized) {
        memcpy(net->peer_slots, g->slots, sizeof(net->peer_slots));
        net->peer_current_slot = g->current_slot;
        net->peer_hp = g->hp;
        net->peer_state = g->state;
        net->peer_state_initialized = 1;
    }
    if (net->peer_known && net->tick - net->last_input_tick <= NET_INPUT_HOLD_TICKS) {
        rasterfall_session_step_remote_player(session, &net->peer_camera,
                                              &net->remote_command);
        if (net->remote_command.buttons & RASTERFALL_CMD_INTERACT)
            rasterfall_session_interact_remote(session, &net->peer_camera);
        /* 暂时把第二名玩家装载到 toy_game 的兼容单玩家视图，只执行
         * 武器/换弹/命中逻辑；随后恢复主机玩家，敌人与波次不重复推进。 */
        memcpy(host_slots, g->slots, sizeof(host_slots));
        memcpy(host_rays, g->rays, sizeof(host_rays));
        host_px = g->px; host_pz = g->pz; host_hp = g->hp;
        host_state = g->state; host_current = g->current_slot;
        host_reloading = g->reloading; host_reload_timer = g->reload_timer_ms;
        host_cooldown = g->fire_cooldown_ms; host_muzzle = g->muzzle_flash_ms;
        host_damage = g->damage_flash_ms; host_kills = g->kills;
        host_ray_count = g->ray_count; host_fire_seq = g->fire_seq;
        memcpy(g->slots, net->peer_slots, sizeof(g->slots));
        g->current_slot = net->peer_current_slot;
        g->hp = net->peer_hp; g->state = net->peer_state;
        g->reloading = net->peer_reloading;
        g->reload_timer_ms = net->peer_reload_timer_ms;
        g->fire_cooldown_ms = net->peer_fire_cooldown_ms;
        g->muzzle_flash_ms = net->peer_muzzle_flash_ms;
        g->damage_flash_ms = net->peer_damage_flash_ms;
        g->kills = net->peer_kills;
        g->fire_seq = net->peer_fire_seq;
        g->px = net->peer_camera.x; g->pz = net->peer_camera.z;
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
        memcpy(net->peer_slots, g->slots, sizeof(net->peer_slots));
        net->peer_current_slot = g->current_slot; net->peer_hp = g->hp;
        net->peer_state = g->state; net->peer_reloading = g->reloading;
        net->peer_reload_timer_ms = g->reload_timer_ms;
        net->peer_fire_cooldown_ms = g->fire_cooldown_ms;
        net->peer_muzzle_flash_ms = g->muzzle_flash_ms;
        net->peer_damage_flash_ms = g->damage_flash_ms;
        net->peer_kills = g->kills; net->peer_fire_seq = g->fire_seq;
        memcpy(g->slots, host_slots, sizeof(g->slots));
        g->px = host_px; g->pz = host_pz; g->hp = host_hp;
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

void rasterfall_net_reconcile_client(struct rasterfall_net *net,
                                     struct camera *camera)
{
    const struct rasterfall_net_player *own;
    long dx, dz, dist2;
    if (net->mode != RASTERFALL_NET_CLIENT || !net->snapshot_ready) return;
    own = &net->players[1];
    if (!own->active) return;
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
    int size, type, payload_size;
    int port, i, transport_result = 0;
    uint32_t sequence;
    memset(&input, 0, sizeof(input));
    memset(&camera, 0, sizeof(camera));
    input.move_forward = 1;
    input.move_strafe = -1;
    input.turn = -321;
    input.pitch = 123;
    input.buttons = RASTERFALL_CMD_FIRE | RASTERFALL_CMD_INTERACT;
    input.fire_held = 1;
    camera.x = -123456;
    camera.z = 654321;
    size = encode_command(packet, 0x10203040U, 7, 99, &input, &camera);
    if (size != NET_HEADER_SIZE + NET_INPUT_SIZE) return 1;
    if (packet_header(packet, size, &type, &payload_size, &sequence) < 0 ||
        type != RASTERFALL_NET_INPUT || sequence != 0x10203040U ||
        payload_size != NET_INPUT_SIZE) return 2;
    if (decode_command(packet + NET_HEADER_SIZE, payload_size, &output) < 0 ||
        output.move_forward != input.move_forward ||
        output.move_strafe != input.move_strafe || output.turn != input.turn ||
        output.pitch != input.pitch || output.buttons != input.buttons ||
        output.fire_held != input.fire_held) return 3;
    packet[4]++;
    if (packet_header(packet, size, &type, &payload_size, &sequence) == 0) return 4;
    packet[4]--;
    if (packet_header(packet, size - 1, &type, &payload_size, &sequence) == 0)
        return 5;
    rasterfall_net_init(&net);
    camera.sy = 0;
    camera.cy = 1024;
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;
    encode_player(packet, 1, 1, &camera, 87, TOY_GAME_WEAPON_SMG,
                  TOY_GAME_PLAYING);
    if (decode_player(packet, &net.players[1]) < 0 ||
        net.players[1].camera.x != camera.x ||
        net.players[1].camera.z != camera.z || net.players[1].hp != 87 ||
        net.players[1].weapon != TOY_GAME_WEAPON_SMG) return 6;

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
    toy_game_init(&test_game, 1);
    if (!transport_result &&
        rasterfall_net_send_snapshot(&host, &camera, &test_game) < 0)
        transport_result = 11;
    for (i = 0; i < 1000 && !client.snapshot_ready; i++)
        rasterfall_net_poll(&client);
    if (!transport_result && (!client.snapshot_ready ||
        !client.players[0].active || client.players[0].camera.x != camera.x ||
        !client.players[1].active)) transport_result = 12;
    rasterfall_net_close(&client);
    rasterfall_net_close(&host);
    return transport_result;
}
