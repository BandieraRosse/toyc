/* Rasterfall 公网 UDP 打洞协调服务。
 * 只交换两个客户端的公网 endpoint，不转发任何游戏数据。
 * 默认监听 UDP 28461；云服务器上运行：build/rasterfall_punch_server
 */
#include "tlibc_everything.h"
#include "net.h"
#include "errno.h"

#define PUNCH_MAGIC_0 'R'
#define PUNCH_MAGIC_1 'F'
#define PUNCH_MAGIC_2 'P'
#define PUNCH_MAGIC_3 '2'
#define PUNCH_VERSION 1
#define PUNCH_REGISTER 1
#define PUNCH_MATCH 2
#define PUNCH_MAX_ROOMS 128
#define PUNCH_PORT 28461

struct punch_peer { int active; struct sockaddr_in address; uint32_t nonce; };
struct punch_room {
    int active;
    int room_id;
    uint32_t token;
    struct punch_peer host;
    struct punch_peer guest;
};

static struct punch_room rooms[PUNCH_MAX_ROOMS];

static unsigned int get_u16(const unsigned char *p)
{ return ((unsigned int)p[0] << 8) | p[1]; }
static uint32_t get_u32(const unsigned char *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put_u16(unsigned char *p, unsigned int v)
{ p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put_u32(unsigned char *p, uint32_t v)
{ p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

static struct punch_room *find_room(int id)
{
    int i, free_slot = -1;
    for (i = 0; i < PUNCH_MAX_ROOMS; i++) {
        if (rooms[i].active && rooms[i].room_id == id) return &rooms[i];
        if (free_slot < 0 && !rooms[i].active) free_slot = i;
    }
    if (free_slot < 0) return NULL;
    memset(&rooms[free_slot], 0, sizeof(rooms[free_slot]));
    rooms[free_slot].active = 1;
    rooms[free_slot].room_id = id;
    rooms[free_slot].token = (uint32_t)__time(NULL) ^ (uint32_t)(id * 2654435761U);
    return &rooms[free_slot];
}

static void send_match(int fd, const struct punch_peer *destination,
                       const struct punch_peer *peer, int room_id, uint32_t token)
{
    unsigned char packet[20];
    memcpy(packet, "RFP2", 4);
    packet[4] = PUNCH_VERSION; packet[5] = PUNCH_MATCH;
    put_u16(packet + 6, (unsigned int)room_id);
    put_u32(packet + 8, token);
    memcpy(packet + 12, &peer->address.sin_addr.s_addr, 4);
    put_u16(packet + 16, ntohs(peer->address.sin_port));
    packet[18] = packet[19] = 0;
    sendto(fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)&destination->address, sizeof(destination->address));
}

int main(int argc, char **argv)
{
    int fd, port = PUNCH_PORT, reuse = 1;
    struct sockaddr_in local, source;
    unsigned char packet[64];
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
    for (;;) {
        socklen_t length = sizeof(source);
        long received = recvfrom(fd, packet, sizeof(packet), 0,
                                 (struct sockaddr *)&source, &length);
        if (received < 12 || memcmp(packet, "RFP2", 4) != 0 ||
            packet[4] != PUNCH_VERSION || packet[5] != PUNCH_REGISTER) continue;
        {
            int room_id = (int)get_u16(packet + 6);
            int role = packet[10];
            struct punch_room *room;
            struct punch_peer *slot;
            if (room_id < 0 || room_id > 9999 || (role != 1 && role != 2)) continue;
            room = find_room(room_id);
            if (!room) continue;
            slot = role == 1 ? &room->host : &room->guest;
            slot->active = 1;
            memcpy(&slot->address, &source, sizeof(source));
            slot->nonce = get_u32(packet + 8);
            if (room->host.active && room->guest.active) {
                send_match(fd, &room->host, &room->guest, room_id, room->token);
                send_match(fd, &room->guest, &room->host, room_id, room->token);
            }
        }
    }
}
