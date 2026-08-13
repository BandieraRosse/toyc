#include "tlibc_everything.h"
#include "errno.h"
#include "rasterfall_net.h"

static long discovery_monotonic_ms(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static void discovery_put_u16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

static unsigned int discovery_get_u16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

static int discovery_same_address(const struct sockaddr_in *a,
                                  const struct sockaddr_in *b)
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
    if (!discovery) return -1;
    if (discovery->fd >= 0) rasterfall_net_discovery_close(discovery);
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
    if (!discovery) return -1;
    if (discovery->fd >= 0) rasterfall_net_discovery_close(discovery);
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
    discovery_put_u16(packet + 6, 0);
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
    discovery_put_u16(packet + 6, (unsigned int)game_port);
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
    long now = discovery_monotonic_ms();
    for (i = 0; i < RASTERFALL_NET_DISCOVERY_MAX_ROOMS; i++) {
        if (discovery->rooms[i].active &&
            discovery_same_address(&discovery->rooms[i].address, source)) {
            slot = i;
            break;
        }
        /* 同一台主机可能同时从广播网卡和 127.0.0.1 回应；合并重复房间，
         * 并优先保留本机可直连的地址。 */
        if (discovery->rooms[i].active &&
            discovery->rooms[i].game_port == (int)discovery_get_u16(packet + 6) &&
            memcmp(discovery->rooms[i].name, packet + 11, 31) == 0) {
            slot = i;
            break;
        }
        if (slot < 0 && !discovery->rooms[i].active) slot = i;
    }
    if (slot < 0) return;
    if (!discovery->rooms[slot].active) discovery->room_count++;
    /* A local response is a better endpoint than the broadcast source for
     * same-machine games.  Do not let a later broadcast response replace it. */
    if (discovery->rooms[slot].active &&
        discovery->rooms[slot].address.sin_addr.s_addr == inet_addr("127.0.0.1") &&
        source->sin_addr.s_addr != inet_addr("127.0.0.1")) {
        /* Keep the existing loopback endpoint. */
    } else {
        memcpy(&discovery->rooms[slot].address, source, sizeof(*source));
    }
    discovery->rooms[slot].active = 1;
    memcpy(discovery->rooms[slot].name, packet + 11, 31);
    discovery->rooms[slot].name[31] = 0;
    discovery->rooms[slot].game_port = (int)discovery_get_u16(packet + 6);
    discovery->rooms[slot].players = packet[8];
    discovery->rooms[slot].max_players = packet[9];
    discovery->rooms[slot].state = packet[10];
    discovery->rooms[slot].last_seen_ms = now;
}

int rasterfall_net_discovery_test(void)
{
    struct rasterfall_net_discovery discovery;
    struct sockaddr_in broadcast_source, loopback_source;
    unsigned char packet[DISCOVERY_PACKET_SIZE];
    rasterfall_net_discovery_init(&discovery);
    memset(packet, 0, sizeof(packet));
    packet[0] = DISCOVERY_MAGIC_0; packet[1] = DISCOVERY_MAGIC_1;
    packet[2] = DISCOVERY_MAGIC_2; packet[3] = DISCOVERY_MAGIC_3;
    packet[4] = DISCOVERY_RESPONSE;
    packet[5] = RASTERFALL_NET_PROTOCOL_VERSION;
    discovery_put_u16(packet + 6, RASTERFALL_NET_DEFAULT_PORT);
    packet[8] = 1; packet[9] = RASTERFALL_NET_PLAYER_MAX;
    memcpy(packet + 11, "LOCAL", 5);
    memset(&broadcast_source, 0, sizeof(broadcast_source));
    broadcast_source.sin_family = AF_INET;
    broadcast_source.sin_port = htons(RASTERFALL_NET_DEFAULT_PORT);
    broadcast_source.sin_addr.s_addr = inet_addr("192.0.2.1");
    memset(&loopback_source, 0, sizeof(loopback_source));
    loopback_source.sin_family = AF_INET;
    loopback_source.sin_port = htons(RASTERFALL_NET_DEFAULT_PORT);
    loopback_source.sin_addr.s_addr = inet_addr("127.0.0.1");
    discovery_store_room(&discovery, &broadcast_source, packet);
    discovery_store_room(&discovery, &loopback_source, packet);
    if (discovery.room_count != 1 || !discovery.rooms[0].active ||
        discovery.rooms[0].address.sin_addr.s_addr !=
            loopback_source.sin_addr.s_addr ||
        discovery.rooms[0].game_port != RASTERFALL_NET_DEFAULT_PORT)
        return 1;
    discovery_store_room(&discovery, &broadcast_source, packet);
    if (discovery.room_count != 1 ||
        discovery.rooms[0].address.sin_addr.s_addr !=
            loopback_source.sin_addr.s_addr)
        return 2;
    return 0;
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
    now = discovery_monotonic_ms();
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
