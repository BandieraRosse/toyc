#include "tlibc_everything.h"
#include "rasterfall_net_transport.h"

long rasterfall_net_transport_now_ms(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

int rasterfall_net_transport_open(int port, int reuse_address)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    if (reuse_address) {
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        __close(fd);
        return -1;
    }
    return fd;
}

void rasterfall_net_transport_close(int fd)
{
    if (fd >= 0) __close(fd);
}

long rasterfall_net_transport_send(int fd, const struct sockaddr_in *destination,
                                   const void *packet, int size)
{
    if (fd < 0 || !destination || !packet || size <= 0) return -1;
    return sendto(fd, packet, (size_t)size, 0,
                  (const struct sockaddr *)destination, sizeof(*destination));
}

long rasterfall_net_transport_receive(int fd, struct sockaddr_in *source,
                                      void *packet, int capacity)
{
    socklen_t source_len = sizeof(*source);
    if (fd < 0 || !source || !packet || capacity <= 0) return -1;
    return recvfrom(fd, packet, (size_t)capacity, 0,
                    (struct sockaddr *)source, &source_len);
}
