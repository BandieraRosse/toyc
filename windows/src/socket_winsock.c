#define WIN32_LEAN_AND_MEAN
#define socket win_socket_decl
#define connect win_connect_decl
#define getsockname win_getsockname_decl
#define setsockopt win_setsockopt_decl
#define bind win_bind_decl
#define listen win_listen_decl
#define accept win_accept_decl
#define recv win_recv_decl
#define sendto win_sendto_decl
#define recvfrom win_recvfrom_decl
#define shutdown win_shutdown_decl
#include <winsock2.h>
#undef socket
#undef connect
#undef getsockname
#undef setsockopt
#undef bind
#undef listen
#undef accept
#undef recv
#undef sendto
#undef recvfrom
#undef shutdown
#include <windows.h>
#include <stdint.h>

typedef unsigned int socklen_t;

#define TOY_SOCKET_FD_BASE 0x40000000
#define TOY_SOCKET_SLOTS 128
static SOCKET socket_table[TOY_SOCKET_SLOTS];

static SOCKET socket_value(int fd)
{
    int slot = fd - TOY_SOCKET_FD_BASE;
    if (slot < 0 || slot >= TOY_SOCKET_SLOTS ||
        socket_table[slot] == INVALID_SOCKET)
        return INVALID_SOCKET;
    return socket_table[slot];
}

static int socket_fd(SOCKET value)
{
    for (int i = 0; i < TOY_SOCKET_SLOTS; i++) {
        if (socket_table[i] == INVALID_SOCKET) {
            socket_table[i] = value;
            return TOY_SOCKET_FD_BASE + i;
        }
    }
    return -1;
}

int toy_socket_close(int fd)
{
    int slot = fd - TOY_SOCKET_FD_BASE;
    SOCKET value = socket_value(fd);
    if (value == INVALID_SOCKET) return -1;
    socket_table[slot] = INVALID_SOCKET;
    return closesocket(value);
}

/* The declarations above are renamed only to keep the Toyc ABI visible. */
__declspec(dllimport) int WSAAPI win_connect(SOCKET, const struct sockaddr *, int) __asm__("connect");
__declspec(dllimport) int WSAAPI win_getsockname(SOCKET, struct sockaddr *, int *) __asm__("getsockname");
__declspec(dllimport) int WSAAPI win_setsockopt(SOCKET, int, int, const char *, int) __asm__("setsockopt");
__declspec(dllimport) int WSAAPI win_bind(SOCKET, const struct sockaddr *, int) __asm__("bind");
__declspec(dllimport) int WSAAPI win_listen(SOCKET, int) __asm__("listen");
__declspec(dllimport) SOCKET WSAAPI win_accept(SOCKET, struct sockaddr *, int *) __asm__("accept");
__declspec(dllimport) int WSAAPI win_recv(SOCKET, char *, int, int) __asm__("recv");
__declspec(dllimport) int WSAAPI win_sendto(SOCKET, const char *, int, int,
                                             const struct sockaddr *, int) __asm__("sendto");
__declspec(dllimport) int WSAAPI win_recvfrom(SOCKET, char *, int, int,
                                               struct sockaddr *, int *) __asm__("recvfrom");
__declspec(dllimport) int WSAAPI win_shutdown(SOCKET, int) __asm__("shutdown");

static int winsock_ready;
static int socket_table_initialized;

static void ensure_winsock(void)
{
    WSADATA data;
    if (!socket_table_initialized) {
        for (int i = 0; i < TOY_SOCKET_SLOTS; i++)
            socket_table[i] = INVALID_SOCKET;
        socket_table_initialized = 1;
    }
    if (!winsock_ready && WSAStartup(MAKEWORD(2, 2), &data) == 0)
        winsock_ready = 1;
}

int toy_socket(int domain, int type, int protocol)
{
    SOCKET s;
    ensure_winsock();
    s = WSASocketA(domain, type & 0xff, protocol, NULL, 0, 0);
    if (s == INVALID_SOCKET) return -1;
    if (type & 00004000) {
        u_long enabled = 1;
        ioctlsocket(s, FIONBIO, &enabled);
    }
    {
        int fd = socket_fd(s);
        if (fd < 0) closesocket(s);
        return fd;
    }
}

int toy_connect(int fd, const struct sockaddr *addr, socklen_t len)
{ return win_connect(socket_value(fd), addr, (int)len); }
int toy_getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{ return win_getsockname(socket_value(fd), addr, (int *)len); }
int toy_setsockopt(int fd, int level, int optname, const void *value, socklen_t len)
{ return win_setsockopt(socket_value(fd), level, optname, value, (int)len); }
int toy_bind(int fd, const struct sockaddr *addr, socklen_t len)
{ return win_bind(socket_value(fd), addr, (int)len); }
int toy_listen(int fd, int backlog)
{ return win_listen(socket_value(fd), backlog); }

int toy_accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    SOCKET result = win_accept(socket_value(fd), addr, (int *)len);
    return result == INVALID_SOCKET ? -1 : socket_fd(result);
}

ssize_t toy_recv(int fd, void *buf, size_t len, int flags)
{ return win_recv(socket_value(fd), (char *)buf, (int)len, flags); }
ssize_t toy_sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t addrlen)
{ return win_sendto(socket_value(fd), (const char *)buf, (int)len, flags,
                    addr, (int)addrlen); }
ssize_t toy_recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *addr, socklen_t *addrlen)
{ return win_recvfrom(socket_value(fd), (char *)buf, (int)len, flags,
                      addr, (int *)addrlen); }
int toy_shutdown(int fd, int how)
{ return win_shutdown(socket_value(fd), how); }

unsigned short tlibc_htons(unsigned short value) { return htons(value); }
uint16_t tlibc_ntohs(uint16_t value) { return ntohs(value); }
unsigned int tlibc_inet_addr(const char *value) { return inet_addr(value); }
char *tlibc_inet_ntoa(struct in_addr value)
{ return inet_ntoa(*(struct in_addr *)&value); }
