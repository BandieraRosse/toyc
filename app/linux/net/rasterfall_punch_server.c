/* Rasterfall 公网 UDP 打洞协调服务。
 * 负责房间匹配，并转发主机与最多三个客户端之间的游戏 UDP 数据。
 * 默认监听 UDP 28461；云服务器上运行：build/rasterfall_punch_server
 *
 * 管理命令（标准输入）：
 *   rooms              列出活动房间
 *   room <id>          查看一个房间
 *   reset <id>         清空一个房间
 *   reset all          清空全部房间
 *   help / quit        帮助 / 退出服务
 */
#include "tlibc_everything.h"
#include "net.h"
#include "errno.h"
#include "rasterfall_public_protocol.h"

#define PUNCH_MAGIC_0 'R'
#define PUNCH_MAGIC_1 'F'
#define PUNCH_MAGIC_2 'P'
#define PUNCH_MAGIC_3 '2'
#define PUNCH_MAX_ROOMS 128
#define PUNCH_PEER_TIMEOUT_MS 12000
#define PUNCH_MAX_PACKET 3000
#define PUNCH_GUEST_MAX 3

struct punch_peer {
    int active;
    struct sockaddr_in address;
    uint32_t nonce;
    long last_seen_ms;
    unsigned long registrations;
};
struct punch_room {
    int active;
    int room_id;
    uint32_t token;
    unsigned long generation;
    long created_ms;
    struct punch_peer host;
    struct punch_peer guests[PUNCH_GUEST_MAX];
};

static struct punch_room rooms[PUNCH_MAX_ROOMS];
static unsigned long next_generation = 1;

static unsigned int get_u16(const unsigned char *p)
{ return ((unsigned int)p[0] << 8) | p[1]; }
static uint32_t get_u32(const unsigned char *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put_u16(unsigned char *p, unsigned int v)
{ p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put_u32(unsigned char *p, uint32_t v)
{ p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

static long now_ms(void)
{
    struct timespec ts;
    if (__clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int same_address(const struct sockaddr_in *a,
                        const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

static void address_text(const struct sockaddr_in *address,
                         char *buffer, int size)
{
    const char *ip = inet_ntoa(address->sin_addr);
    if (!ip) ip = "?";
    snprintf(buffer, (size_t)size, "%s:%d", ip, ntohs(address->sin_port));
}

static void log_peer(const char *prefix, const struct punch_peer *peer)
{
    char address[64];
    address_text(&peer->address, address, sizeof(address));
    __printf("punch-server: %s %s nonce=%u registrations=%lu\n",
             prefix, address, (unsigned int)peer->nonce, peer->registrations);
}

static void clear_room(struct punch_room *room, const char *reason)
{
    if (!room || !room->active) return;
    __printf("punch-server: reset room %04d generation=%lu (%s)\n",
             room->room_id, room->generation, reason ? reason : "manual");
    memset(room, 0, sizeof(*room));
}

static uint32_t new_token(int room_id)
{
    uint32_t token;
    token = (uint32_t)now_ms() ^ (uint32_t)(room_id * 2654435761U) ^
            (uint32_t)(next_generation * 2246822519U);
    next_generation++;
    if (!token) token = 1;
    return token;
}

static struct punch_room *find_room(int id)
{
    int i;
    for (i = 0; i < PUNCH_MAX_ROOMS; i++) {
        if (rooms[i].active && rooms[i].room_id == id) return &rooms[i];
    }
    return NULL;
}

static struct punch_room *create_room(int id)
{
    int free_slot = -1, i;
    if (find_room(id)) return NULL;
    for (i = 0; i < PUNCH_MAX_ROOMS; i++)
        if (!rooms[i].active) { free_slot = i; break; }
    if (free_slot < 0) return NULL;
    memset(&rooms[free_slot], 0, sizeof(rooms[free_slot]));
    rooms[free_slot].active = 1;
    rooms[free_slot].room_id = id;
    rooms[free_slot].generation = next_generation++;
    rooms[free_slot].created_ms = now_ms();
    rooms[free_slot].token = new_token(id);
    __printf("punch-server: created room %04d generation=%lu token=%u\n",
             id, rooms[free_slot].generation,
             (unsigned int)rooms[free_slot].token);
    return &rooms[free_slot];
}

static void expire_rooms(long now)
{
    int i;
    for (i = 0; i < PUNCH_MAX_ROOMS; i++) {
        struct punch_room *room = &rooms[i];
        if (!room->active) continue;
        if (room->host.active && now - room->host.last_seen_ms > PUNCH_PEER_TIMEOUT_MS) {
            clear_room(room, "host timeout");
            continue;
        }
        for (int i = 0; i < PUNCH_GUEST_MAX; i++)
            if (room->guests[i].active &&
                now - room->guests[i].last_seen_ms > PUNCH_PEER_TIMEOUT_MS) {
                log_peer("guest timeout", &room->guests[i]);
                memset(&room->guests[i], 0, sizeof(room->guests[i]));
            }
    }
}

static void print_room(const struct punch_room *room)
{
    char host[64];
    int i;
    if (!room || !room->active) return;
    if (room->host.active) address_text(&room->host.address, host, sizeof(host));
    else strcpy(host, "-");
    __printf("room %04d gen=%lu token=%u host=%s\n",
             room->room_id, room->generation, (unsigned int)room->token,
             host);
    for (i = 0; i < PUNCH_GUEST_MAX; i++) {
        char extra[64];
        if (room->guests[i].active)
            address_text(&room->guests[i].address, extra, sizeof(extra));
        else strcpy(extra, "-");
        __printf("room %04d guest%d=%s\n", room->room_id, i + 1, extra);
    }
}

static void print_rooms(void)
{
    int i, count = 0;
    for (i = 0; i < PUNCH_MAX_ROOMS; i++) {
        if (!rooms[i].active) continue;
        print_room(&rooms[i]);
        count++;
    }
    __printf("punch-server: %d active room(s)\n", count);
}

static void handle_command(char *line, int *running)
{
    int id, i;
    while (*line == ' ' || *line == '\t') line++;
    if (!strncmp(line, "rooms", 5)) print_rooms();
    else if (!strncmp(line, "room ", 5)) {
        id = atoi(line + 5);
        for (i = 0; i < PUNCH_MAX_ROOMS; i++)
            if (rooms[i].active && rooms[i].room_id == id) break;
        if (i < PUNCH_MAX_ROOMS) print_room(&rooms[i]);
        else __printf("punch-server: room %04d not found\n", id);
    } else if (!strncmp(line, "reset all", 9)) {
        for (i = 0; i < PUNCH_MAX_ROOMS; i++) clear_room(&rooms[i], "manual all");
    } else if (!strncmp(line, "reset ", 6)) {
        id = atoi(line + 6);
        for (i = 0; i < PUNCH_MAX_ROOMS; i++)
            if (rooms[i].active && rooms[i].room_id == id) break;
        if (i < PUNCH_MAX_ROOMS) clear_room(&rooms[i], "manual");
        else __printf("punch-server: room %04d not found\n", id);
    } else if (!strncmp(line, "help", 4)) {
        __printf("commands: rooms | room <id> | reset <id> | reset all | help | quit\n");
    } else if (!strncmp(line, "quit", 4)) {
        *running = 0;
    } else if (line[0]) {
        __printf("punch-server: unknown command (try help)\n");
    }
}

static void send_match(int fd, const struct punch_peer *destination,
                       const struct punch_peer *peer, int room_id,
                       uint32_t token, int player_id)
{
    unsigned char packet[20];
    memcpy(packet, "RFP3", 4);
    packet[4] = RASTERFALL_PUBLIC_VERSION;
    packet[5] = RASTERFALL_PUBLIC_MATCH;
    put_u16(packet + 6, (unsigned int)room_id);
    put_u32(packet + 8, token);
    memcpy(packet + 12, &peer->address.sin_addr.s_addr, 4);
    put_u16(packet + 16, ntohs(peer->address.sin_port));
    packet[18] = (unsigned char)rasterfall_public_room_uses_relay(room_id);
    packet[19] = (unsigned char)player_id;
    sendto(fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)&destination->address, sizeof(destination->address));
}

static void send_error(int fd, const struct sockaddr_in *destination,
                       int room_id, int error)
{
    unsigned char packet[RASTERFALL_PUBLIC_ERROR_SIZE];
    memcpy(packet, "RFP3", 4);
    packet[4] = RASTERFALL_PUBLIC_VERSION;
    packet[5] = RASTERFALL_PUBLIC_ERROR;
    put_u16(packet + 6, (unsigned int)room_id);
    packet[8] = (unsigned char)error;
    packet[9] = 0;
    sendto(fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)destination, sizeof(*destination));
}

static void send_registered(int fd, const struct sockaddr_in *destination,
                            int room_id)
{
    unsigned char packet[RASTERFALL_PUBLIC_REGISTERED_SIZE];
    memcpy(packet, "RFP3", 4);
    packet[4] = RASTERFALL_PUBLIC_VERSION;
    packet[5] = RASTERFALL_PUBLIC_REGISTERED;
    put_u16(packet + 6, (unsigned int)room_id);
    packet[8] = 0;
    sendto(fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)destination, sizeof(*destination));
}

static void send_room_matches(int fd, const struct punch_room *room)
{
    int i;
    if (!room || !room->host.active) return;
    for (i = 0; i < PUNCH_GUEST_MAX; i++) {
        if (!room->guests[i].active) continue;
        send_match(fd, &room->host, &room->guests[i], room->room_id,
                   room->token, i + 1);
        send_match(fd, &room->guests[i], &room->host, room->room_id,
                   room->token, i + 1);
    }
}

static void relay_packet(int fd, const struct sockaddr_in *source,
                         const unsigned char *packet, long size)
{
    int i;
    unsigned char wrapped[PUNCH_MAX_PACKET + 5];
    for (i = 0; i < PUNCH_MAX_ROOMS; i++) {
        struct punch_room *room = &rooms[i];
        int j;
        if (!room->active) continue;
        int source_id = -1;
        if (!rasterfall_public_room_uses_relay(room->room_id)) continue;
        if (room->host.active && same_address(&room->host.address, source))
            source_id = 0;
        else for (j = 0; j < PUNCH_GUEST_MAX; j++)
            if (room->guests[j].active &&
                same_address(&room->guests[j].address, source))
                source_id = j + 1;
        if (source_id < 0) continue;
        if (size + 5 > (long)sizeof(wrapped)) return;
        wrapped[0] = 'R'; wrapped[1] = 'F'; wrapped[2] = 'R'; wrapped[3] = '4';
        wrapped[4] = (unsigned char)source_id;
        memcpy(wrapped + 5, packet, (size_t)size);
        if (room->host.active && source_id != 0)
            sendto(fd, wrapped, (size_t)size + 5, 0,
                   (const struct sockaddr *)&room->host.address,
                   sizeof(room->host.address));
        for (j = 0; j < PUNCH_GUEST_MAX; j++)
            if (room->guests[j].active && source_id != j + 1)
                sendto(fd, wrapped, (size_t)size + 5, 0,
                       (const struct sockaddr *)&room->guests[j].address,
                       sizeof(room->guests[j].address));
        return;
    }
}

int main(int argc, char **argv)
{
    int fd, port = RASTERFALL_PUBLIC_PORT, reuse = 1, running = 1;
    struct sockaddr_in local, source;
    unsigned char packet[PUNCH_MAX_PACKET];
    char command[256];
    int command_length = 0;
    if (argc > 1) port = atoi(argv[1]);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { __fprintf(2, "punch-server: socket failed\n"); return 1; }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((unsigned short)port);
    local.sin_addr.s_addr = 0;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        __fprintf(2, "punch-server: bind UDP %d failed\n", port);
        return 1;
    }
    __printf("rasterfall punch server listening on UDP %d\n", port);
    __printf("punch-server: type 'help' for commands\n");
    __fcntl(0, F_SETFL, (unsigned long)(__fcntl(0, F_GETFL, 0) | O_NONBLOCK));
    while (running) {
        struct pollfd fds[2];
        fds[0].fd = fd; fds[0].events = POLLIN; fds[0].revents = 0;
        fds[1].fd = 0; fds[1].events = POLLIN; fds[1].revents = 0;
        if (__poll(fds, 2, 500) < 0) continue;
        expire_rooms(now_ms());
        if (fds[1].revents & POLLIN) {
            char input[64];
            long n = __read(0, input, sizeof(input));
            int i;
            for (i = 0; i < n; i++) {
                if (input[i] == '\n' || input[i] == '\r') {
                    command[command_length] = 0;
                    handle_command(command, &running);
                    command_length = 0;
                } else if (command_length < (int)sizeof(command) - 1) {
                    command[command_length++] = input[i];
                }
            }
        }
        if (!running) break;
        if (!(fds[0].revents & POLLIN)) continue;
        socklen_t length = sizeof(source);
        long received = recvfrom(fd, packet, sizeof(packet), 0,
                                 (struct sockaddr *)&source, &length);
        if (received <= 0) continue;
        if (received < RASTERFALL_PUBLIC_REGISTER_SIZE ||
            memcmp(packet, "RFP3", 4) != 0 ||
            packet[4] != RASTERFALL_PUBLIC_VERSION ||
            packet[5] != RASTERFALL_PUBLIC_REGISTER) {
            relay_packet(fd, &source, packet, received);
            continue;
        }
        {
            int room_id = (int)get_u16(packet + 6);
            int role = packet[10];
            struct punch_room *room;
            struct punch_peer *slot;
            uint32_t nonce = get_u32(packet + 8);
            if (!rasterfall_public_room_valid(room_id) ||
                (role != RASTERFALL_PUBLIC_ROLE_HOST &&
                 role != RASTERFALL_PUBLIC_ROLE_GUEST)) continue;
            room = find_room(room_id);
            if (role == RASTERFALL_PUBLIC_ROLE_HOST) {
                if (!room) room = create_room(room_id);
                if (!room) {
                    send_error(fd, &source, room_id,
                               RASTERFALL_PUBLIC_ERROR_SERVER_FULL);
                    continue;
                }
                slot = &room->host;
                if (slot->active &&
                    !(same_address(&slot->address, &source) &&
                      slot->nonce == nonce)) {
                    send_error(fd, &source, room_id,
                               RASTERFALL_PUBLIC_ERROR_ROOM_EXISTS);
                    continue;
                }
            } else {
                if (!room || !room->host.active) {
                    send_error(fd, &source, room_id,
                               RASTERFALL_PUBLIC_ERROR_ROOM_NOT_FOUND);
                    continue;
                }
                slot = NULL;
                for (int i = 0; i < PUNCH_GUEST_MAX; i++)
                    if (room->guests[i].active &&
                        same_address(&room->guests[i].address, &source) &&
                        room->guests[i].nonce == nonce) {
                        slot = &room->guests[i]; break;
                    }
                if (!slot)
                    for (int i = 0; i < PUNCH_GUEST_MAX; i++)
                        if (!room->guests[i].active) {
                            slot = &room->guests[i]; break;
                        }
                if (!slot) {
                    send_error(fd, &source, room_id,
                               RASTERFALL_PUBLIC_ERROR_ROOM_FULL);
                    continue;
                }
            }
            {
                int same_session = slot->active &&
                    same_address(&slot->address, &source) &&
                    slot->nonce == nonce;
                slot->active = 1;
                memcpy(&slot->address, &source, sizeof(source));
                slot->nonce = nonce;
                slot->last_seen_ms = now_ms();
                slot->registrations++;
                if (!same_session || slot->registrations == 1 ||
                    (slot->registrations % 10) == 0)
                    log_peer(role == RASTERFALL_PUBLIC_ROLE_HOST ?
                             "host register" : "guest register", slot);
            }
            send_registered(fd, &source, room_id);
            if (room->host.active) {
                __printf("punch-server: match room %04d generation=%lu\n",
                         room_id, room->generation);
                send_room_matches(fd, room);
            }
        }
    }
    __close(fd);
    __printf("punch-server: stopped\n");
    return 0;
}
