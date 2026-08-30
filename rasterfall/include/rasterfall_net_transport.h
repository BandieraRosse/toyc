#ifndef RASTERFALL_NET_TRANSPORT_H
#define RASTERFALL_NET_TRANSPORT_H

#include "net.h"

long rasterfall_net_transport_now_ms(void);
int rasterfall_net_transport_open(int port, int reuse_address);
void rasterfall_net_transport_close(int fd);
long rasterfall_net_transport_send(int fd, const struct sockaddr_in *destination,
                                   const void *packet, int size);
long rasterfall_net_transport_receive(int fd, struct sockaddr_in *source,
                                      void *packet, int capacity);

#endif
