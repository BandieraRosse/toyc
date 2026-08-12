#ifndef TOYC_WINDOWS_SOCKET_H
#define TOYC_WINDOWS_SOCKET_H

#include "tlibc_types.h"

typedef uint16_t in_port_t;
typedef uint16_t sa_family_t;

struct in_addr { uint32_t s_addr; };

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};

#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_NONBLOCK  00004000

#define PF_INET        2
#define PF_UNIX        1
#define AF_INET        PF_INET
#define AF_UNIX        PF_UNIX

#define SOL_SOCKET     1
#define SO_REUSEADDR   2
#define SO_BROADCAST   6
#define INADDR_ANY     0

#define SHUT_RD        0
#define SHUT_WR        1
#define SHUT_RDWR      2

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[108];
};

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct msghdr {
    void *msg_name;
    unsigned int msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))

#endif
