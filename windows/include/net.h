#ifndef TOYC_WINDOWS_NET_H
#define TOYC_WINDOWS_NET_H

#include "tlibc_types.h"
#include "socket.h"

#define socket      toy_socket
#define connect     toy_connect
#define getsockname toy_getsockname
#define setsockopt  toy_setsockopt
#define bind        toy_bind
#define listen      toy_listen
#define accept      toy_accept
#define recv        toy_recv
#define sendto      toy_sendto
#define recvfrom    toy_recvfrom
#define shutdown    toy_shutdown

typedef unsigned int socklen_t;
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

int socket(int domain, int type, int protocol);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int setsockopt(int fd, int level, int optname, const void *value, socklen_t len);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t recv(int fd, void *buf, size_t len, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t addrlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int fd, int how);
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int fd, struct msghdr *msg, int flags);
unsigned short tlibc_htons(unsigned short value);
uint16_t tlibc_ntohs(uint16_t value);
unsigned int tlibc_inet_addr(const char *value);
char *tlibc_inet_ntoa(struct in_addr value);
int getaddrinfo(const char *restrict host, const char *restrict serv,
                const struct addrinfo *restrict hint,
                struct addrinfo **restrict res);
void freeaddrinfo(struct addrinfo *res);

#define AI_PASSIVE      0x0001
#define AI_CANONNAME    0x0002
#define AI_NUMERICHOST  0x0004
#define EAI_BADFLAGS    -1
#define EAI_NONAME      -2
#define EAI_AGAIN       -3
#define EAI_FAIL        -4
#define EAI_FAMILY      -5
#define EAI_MEMORY      -6
#define EAI_SERVICE     -7
#define EAI_SOCKTYPE    -8
#define EAI_NODATA      -9

#define DNS_PORT         53
#define DNS_TYPE_A        1
#define DNS_TYPE_NS       2
#define DNS_TYPE_CNAME    5
#define DNS_TYPE_SOA      6
#define DNS_TYPE_MX      15
#define DNS_TYPE_AAAA    28
#define DNS_TYPE_ANY    255
#define DNS_CLASS_IN      1
#define DNS_FLAG_QR     (1 << 15)
#define DNS_FLAG_AA     (1 << 10)
#define DNS_FLAG_TC     (1 << 9)
#define DNS_FLAG_RD     (1 << 8)
#define DNS_FLAG_RA     (1 << 7)
#define DNS_RCODE_MASK   0x0F

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

#define DNS_HEADER_SIZE 12
#define DNS_QTYPE_OFF   0
#define DNS_QCLASS_OFF  2

int tlibc_dns_build_query(uint8_t *buf, size_t buf_len,
                          const char *name, uint16_t qtype);
int tlibc_dns_query(uint32_t ns_ip, const uint8_t *query, int qlen,
                    uint8_t *resp, int resp_cap);
int tlibc_dns_name_decode(const uint8_t *resp, int resp_len, int off,
                          char *out, int out_len);
void tlibc_dns_parse_header(const uint8_t *resp, struct dns_header *hdr);
int tlibc_dns_get_question(const uint8_t *resp, int resp_len, int n,
                           char *name, int name_len,
                           uint16_t *type, uint16_t *class);
int tlibc_dns_get_record(const uint8_t *resp, int resp_len,
                         int section, int n, char *name, int name_len,
                         uint16_t *type, uint16_t *class, uint32_t *ttl,
                         const uint8_t **rdata, uint16_t *rdlen);
int tlibc_dns_resolve(const char *hostname, uint32_t *ip_out);
int tlibc_dns_resolve_ns(const char *hostname, uint32_t ns_ip, uint32_t *ip_out);
int tlibc_dns_get_nameserver(uint32_t *ns_ip);

#define htons tlibc_htons
#define ntohs tlibc_ntohs
#define inet_addr tlibc_inet_addr
#define inet_ntoa tlibc_inet_ntoa

#endif
