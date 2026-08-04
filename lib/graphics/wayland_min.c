/*
 * Minimal Wayland wl_shm + xdg-shell client.
 *
 * No libwayland dependency: requests and events use the public Wayland wire
 * format directly.  The intentionally small object model is sufficient for a
 * software-rendered toplevel window, double buffering and basic input.
 */

#include "wayland_min.h"
#include "core.h"
#include "net.h"
#include "string.h"
#include "tlibc_compat.h"

#define WL_MAX_OBJECTS 4096
#define WL_IN_CAP       16384
#define WL_FD_CAP       16
#define WL_MSG_CAP      1024

#define WL_SHM_FORMAT_XRGB8888 1
#define WL_SEAT_CAP_POINTER    1
#define WL_SEAT_CAP_KEYBOARD   2
#define WL_POINTER_BUTTON_PRESSED 1
#define WL_KEYBOARD_KEY_PRESSED   1

enum object_kind {
    OBJ_NONE, OBJ_ZOMBIE, OBJ_DISPLAY, OBJ_REGISTRY, OBJ_CALLBACK, OBJ_COMPOSITOR,
    OBJ_SHM, OBJ_SHM_POOL, OBJ_BUFFER, OBJ_SEAT, OBJ_POINTER, OBJ_KEYBOARD,
    OBJ_SURFACE, OBJ_XDG_WM_BASE, OBJ_XDG_SURFACE, OBJ_XDG_TOPLEVEL
};

struct wl_buf {
    uint32_t id;
    uint32_t *data;
    size_t size;
    int fd;
    int busy;
};

struct toywl {
    int fd;
    uint32_t next_id;
    unsigned char kinds[WL_MAX_OBJECTS];
    uint32_t registry, compositor, shm, seat, pointer, keyboard;
    uint32_t surface, wm_base, xdg_surface, toplevel, frame_callback;
    uint32_t compositor_name, shm_name, seat_name, wm_base_name;
    uint32_t compositor_ver, shm_ver, seat_ver, wm_base_ver;
    struct wl_buf buffers[2];
    int draw_index;
    int width, height;
    int wanted_width, wanted_height;
    int configured;
    int frame_ready;
    int pointer_x, pointer_y;
    unsigned char inbuf[WL_IN_CAP];
    int in_len;
    int fds[WL_FD_CAP];
    int fd_count;
    struct toywl_input pending;
};

struct msg_builder {
    unsigned char data[WL_MSG_CAP];
    int len;
};

/* Read through procfs instead of indexing the process-start envp directly.
 * Besides keeping this Linux backend self-contained, this avoids a pending
 * Toyc self-host codegen issue for char ** indexing. */
static int environment_value(const char *name, char *out, int out_cap)
{
    char env[8192];
    int fd = __openat(AT_FDCWD, "/proc/self/environ", O_RDONLY, 0);
    int n, name_len = (int)strlen(name), at = 0;
    if (fd < 0 || out_cap < 2) return -1;
    n = (int)__read(fd, env, sizeof(env));
    __close(fd);
    if (n <= 0) return -1;
    while (at < n) {
        int end = at, j = 0;
        while (end < n && env[end]) end++;
        while (j < name_len && at + j < end && env[at + j] == name[j]) j++;
        if (j == name_len && at + j < end && env[at + j] == '=') {
            int src = at + j + 1, dst = 0;
            while (src < end && dst + 1 < out_cap) out[dst++] = env[src++];
            out[dst] = '\0';
            return 0;
        }
        at = end + 1;
    }
    return -1;
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t new_object(struct toywl *wl, enum object_kind kind)
{
    for (uint32_t attempts = 0; attempts < WL_MAX_OBJECTS - 2; attempts++) {
        uint32_t id = wl->next_id++;
        if (wl->next_id >= WL_MAX_OBJECTS) wl->next_id = 2;
        if (id >= 2 && wl->kinds[id] == OBJ_NONE) {
            wl->kinds[id] = (unsigned char)kind;
            return id;
        }
    }
    return 0;
}

static void mb_init(struct msg_builder *m, uint32_t object, uint16_t opcode)
{
    m->len = 8;
    put32(m->data, object);
    put32(m->data + 4, opcode);
}

static void mb_u32(struct msg_builder *m, uint32_t value)
{
    if (m->len + 4 > WL_MSG_CAP) return;
    put32(m->data + m->len, value);
    m->len += 4;
}

static void mb_string(struct msg_builder *m, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    int padded = (int)((n + 3) & ~3U);
    mb_u32(m, n);
    if (m->len + padded > WL_MSG_CAP) return;
    memcpy(m->data + m->len, s, n);
    memset(m->data + m->len + n, 0, (size_t)padded - n);
    m->len += padded;
}

static int send_request(struct toywl *wl, struct msg_builder *m, int pass_fd)
{
    struct iovec iov;
    struct msghdr msg;
    unsigned char control[CMSG_SPACE(sizeof(int))];
    int sent = 0;

    put32(m->data + 4, ((uint32_t)m->len << 16) | rd32(m->data + 4));
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = m->data;
    iov.iov_len = (size_t)m->len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (pass_fd >= 0) {
        struct cmsghdr *cmsg;
        memset(control, 0, sizeof(control));
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        cmsg = (struct cmsghdr *)control;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        *(int *)CMSG_DATA(cmsg) = pass_fd;
    }
    while (sent < m->len) {
        ssize_t n = sendmsg(wl->fd, &msg, 0);
        if (n <= 0) return -1;
        sent += (int)n;
        iov.iov_base = m->data + sent;
        iov.iov_len = (size_t)(m->len - sent);
        /* An fd belongs only to the first byte of the request. */
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
    }
    return 0;
}

static int request0(struct toywl *wl, uint32_t obj, uint16_t opcode)
{
    struct msg_builder m;
    mb_init(&m, obj, opcode);
    return send_request(wl, &m, -1);
}

static int bind_global(struct toywl *wl, uint32_t name, const char *iface,
                       uint32_t version, enum object_kind kind, uint32_t *out)
{
    struct msg_builder m;
    uint32_t id = new_object(wl, kind);
    if (!id) return -1;
    mb_init(&m, wl->registry, 0);
    mb_u32(&m, name);
    mb_string(&m, iface);
    mb_u32(&m, version);
    mb_u32(&m, id);
    if (send_request(wl, &m, -1) < 0) return -1;
    *out = id;
    return 0;
}

static int queue_fd(struct toywl *wl, int fd)
{
    if (wl->fd_count >= WL_FD_CAP) {
        __close(fd);
        return -1;
    }
    wl->fds[wl->fd_count++] = fd;
    return 0;
}

static int take_fd(struct toywl *wl)
{
    int fd;
    if (wl->fd_count == 0) return -1;
    fd = wl->fds[0];
    for (int i = 1; i < wl->fd_count; i++) wl->fds[i - 1] = wl->fds[i];
    wl->fd_count--;
    return fd;
}

static int receive_bytes(struct toywl *wl, int timeout_ms)
{
    struct pollfd pfd;
    struct iovec iov;
    struct msghdr msg;
    unsigned char control[CMSG_SPACE(sizeof(int) * 4)];
    ssize_t n;

    pfd.fd = wl->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (__poll(&pfd, 1, timeout_ms) <= 0) return 0;
    if (wl->in_len >= WL_IN_CAP) return -1;
    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));
    iov.iov_base = wl->inbuf + wl->in_len;
    iov.iov_len = WL_IN_CAP - (size_t)wl->in_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    n = recvmsg(wl->fd, &msg, 0);
    if (n <= 0) return -1;
    wl->in_len += (int)n;
    if (msg.msg_controllen >= sizeof(struct cmsghdr)) {
        struct cmsghdr *cmsg = (struct cmsghdr *)control;
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            int count = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            int *passed = (int *)CMSG_DATA(cmsg);
            for (int i = 0; i < count; i++) queue_fd(wl, passed[i]);
        }
    }
    return 1;
}

static int interface_is(const unsigned char *p, int payload_len, const char *s)
{
    uint32_t n;
    if (payload_len < 8) return 0;
    n = rd32(p + 4);
    if (n == 0 || n > (uint32_t)(payload_len - 8)) return 0;
    return strlen(s) + 1 == n && memcmp(p + 8, s, n) == 0;
}

static void handle_registry(struct toywl *wl, uint16_t opcode,
                            const unsigned char *p, int len)
{
    uint32_t name, version, n, off;
    if (opcode != 0 || len < 12) return;
    name = rd32(p);
    n = rd32(p + 4);
    off = 8 + ((n + 3) & ~3U);
    if (n == 0 || off + 4 > (uint32_t)len) return;
    version = rd32(p + off);
    if (interface_is(p, len, "wl_compositor")) {
        wl->compositor_name = name; wl->compositor_ver = version;
    } else if (interface_is(p, len, "wl_shm")) {
        wl->shm_name = name; wl->shm_ver = version;
    } else if (interface_is(p, len, "wl_seat")) {
        wl->seat_name = name; wl->seat_ver = version;
    } else if (interface_is(p, len, "xdg_wm_base")) {
        wl->wm_base_name = name; wl->wm_base_ver = version;
    }
}

static void handle_seat(struct toywl *wl, uint16_t opcode,
                        const unsigned char *p, int len)
{
    uint32_t caps;
    struct msg_builder m;
    if (opcode != 0 || len < 4) return;
    caps = rd32(p);
    if ((caps & WL_SEAT_CAP_POINTER) && !wl->pointer) {
        wl->pointer = new_object(wl, OBJ_POINTER);
        mb_init(&m, wl->seat, 0); mb_u32(&m, wl->pointer);
        send_request(wl, &m, -1);
    }
    if ((caps & WL_SEAT_CAP_KEYBOARD) && !wl->keyboard) {
        wl->keyboard = new_object(wl, OBJ_KEYBOARD);
        mb_init(&m, wl->seat, 1); mb_u32(&m, wl->keyboard);
        send_request(wl, &m, -1);
    }
}

static void handle_pointer(struct toywl *wl, uint16_t opcode,
                           const unsigned char *p, int len)
{
    if (opcode == 0 && len >= 16) {
        wl->pointer_x = (int32_t)rd32(p + 8) / 256;
        wl->pointer_y = (int32_t)rd32(p + 12) / 256;
        wl->pending.pointer_x = wl->pointer_x;
        wl->pending.pointer_y = wl->pointer_y;
        wl->pending.pointer_moved = 1;
    } else if (opcode == 2 && len >= 12) {
        wl->pointer_x = (int32_t)rd32(p + 4) / 256;
        wl->pointer_y = (int32_t)rd32(p + 8) / 256;
        wl->pending.pointer_x = wl->pointer_x;
        wl->pending.pointer_y = wl->pointer_y;
        wl->pending.pointer_moved = 1;
    } else if (opcode == 3 && len >= 16) {
        wl->pending.button = rd32(p + 8);
        wl->pending.button_pressed = rd32(p + 12) == WL_POINTER_BUTTON_PRESSED;
    }
}

static void handle_keyboard(struct toywl *wl, uint16_t opcode,
                            const unsigned char *p, int len)
{
    if (opcode == 0) {
        int fd = take_fd(wl);
        if (fd >= 0) __close(fd); /* Keymap is unnecessary for evdev key codes. */
    } else if (opcode == 1) {
        wl->pending.keyboard_focus_changed = 1;
        wl->pending.keyboard_focused = 1;
    } else if (opcode == 2) {
        wl->pending.keyboard_focus_changed = 1;
        wl->pending.keyboard_focused = 0;
    } else if (opcode == 3 && len >= 16) {
        unsigned int key = rd32(p + 8);
        int pressed = rd32(p + 12) == WL_KEYBOARD_KEY_PRESSED;
        int at = wl->pending.key_event_count;
        if (at < TOYWL_MAX_KEY_EVENTS) {
            wl->pending.key_events[at].key = key;
            wl->pending.key_events[at].pressed = pressed;
            wl->pending.key_event_count++;
        }
        if (key == 1 && pressed)
            wl->pending.close_requested = 1;
    }
}

static void ack_configure(struct toywl *wl, uint32_t serial)
{
    struct msg_builder m;
    mb_init(&m, wl->xdg_surface, 4);
    mb_u32(&m, serial);
    send_request(wl, &m, -1);
    wl->configured = 1;
}

static void handle_event(struct toywl *wl, uint32_t object, uint16_t opcode,
                         const unsigned char *p, int len)
{
    enum object_kind kind;
    if (object >= WL_MAX_OBJECTS) return;
    kind = (enum object_kind)wl->kinds[object];
    if (kind == OBJ_DISPLAY && opcode == 0 && len >= 12) {
        uint32_t bad_object = rd32(p), code = rd32(p + 4), n = rd32(p + 8);
        const char *message = n && n <= (uint32_t)(len - 12) ? (const char *)(p + 12) : "unknown";
        __fprintf(2, "toywl: protocol error object=%u code=%u: %s\n",
                  bad_object, code, message);
        wl->pending.close_requested = 1;
    } else if (kind == OBJ_DISPLAY && opcode == 1 && len >= 4) {
        uint32_t deleted = rd32(p);
        if (deleted < WL_MAX_OBJECTS) wl->kinds[deleted] = OBJ_NONE;
    } else if (kind == OBJ_REGISTRY) {
        handle_registry(wl, opcode, p, len);
    } else if (kind == OBJ_CALLBACK && opcode == 0) {
        if (object == wl->frame_callback) {
            wl->frame_callback = 0;
            wl->frame_ready = 1;
        }
        wl->kinds[object] = OBJ_ZOMBIE;
    } else if (kind == OBJ_SEAT) {
        handle_seat(wl, opcode, p, len);
    } else if (kind == OBJ_POINTER) {
        handle_pointer(wl, opcode, p, len);
    } else if (kind == OBJ_KEYBOARD) {
        handle_keyboard(wl, opcode, p, len);
    } else if (kind == OBJ_BUFFER && opcode == 0) {
        for (int i = 0; i < 2; i++)
            if (wl->buffers[i].id == object) wl->buffers[i].busy = 0;
    } else if (kind == OBJ_XDG_WM_BASE && opcode == 0 && len >= 4) {
        struct msg_builder m;
        mb_init(&m, wl->wm_base, 3); mb_u32(&m, rd32(p));
        send_request(wl, &m, -1);
    } else if (kind == OBJ_XDG_SURFACE && opcode == 0 && len >= 4) {
        ack_configure(wl, rd32(p));
    } else if (kind == OBJ_XDG_TOPLEVEL) {
        if (opcode == 0 && len >= 8) {
            int w = (int32_t)rd32(p), h = (int32_t)rd32(p + 4);
            if (w > 0 && h > 0 && (w != wl->wanted_width || h != wl->wanted_height)) {
                wl->wanted_width = w; wl->wanted_height = h;
                wl->pending.resized = 1;
                wl->pending.width = w; wl->pending.height = h;
            }
        } else if (opcode == 1) {
            wl->pending.close_requested = 1;
        }
    }
}

static int parse_events(struct toywl *wl)
{
    int off = 0, count = 0;
    while (wl->in_len - off >= 8) {
        uint32_t object = rd32(wl->inbuf + off);
        uint32_t word = rd32(wl->inbuf + off + 4);
        int size = (int)(word >> 16);
        uint16_t opcode = (uint16_t)word;
        if (size < 8 || size > WL_IN_CAP) return -1;
        if (wl->in_len - off < size) break;
        handle_event(wl, object, opcode, wl->inbuf + off + 8, size - 8);
        off += size;
        count++;
    }
    if (off) {
        memmove(wl->inbuf, wl->inbuf + off, (size_t)(wl->in_len - off));
        wl->in_len -= off;
    }
    return count;
}

static int dispatch_once(struct toywl *wl, int timeout_ms)
{
    int parsed = parse_events(wl);
    if (parsed < 0) return -1;
    if (parsed > 0) return parsed;
    if (receive_bytes(wl, timeout_ms) < 0) return -1;
    return parse_events(wl);
}

static int roundtrip(struct toywl *wl)
{
    struct msg_builder m;
    uint32_t callback = new_object(wl, OBJ_CALLBACK);
    mb_init(&m, 1, 0); mb_u32(&m, callback);
    if (send_request(wl, &m, -1) < 0) return -1;
    while (wl->kinds[callback] != OBJ_NONE)
        if (dispatch_once(wl, -1) < 0) return -1;
    return 0;
}

static int create_buffers(struct toywl *wl, int width, int height)
{
    struct msg_builder m;
    int stride = width * 4;
    size_t size = (size_t)stride * height;

    for (int i = 0; i < 2; i++) {
        uint32_t pool;
        int fd = __memfd_create("toyc-wayland", 1);
        if (fd < 0 || __ftruncate(fd, (off_t)size) < 0) return -1;
        wl->buffers[i].data = __mmap(NULL, size, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, 0);
        if (wl->buffers[i].data == MAP_FAILED) { __close(fd); return -1; }
        pool = new_object(wl, OBJ_SHM_POOL);
        mb_init(&m, wl->shm, 0); mb_u32(&m, pool); mb_u32(&m, (uint32_t)size);
        if (send_request(wl, &m, fd) < 0) return -1;
        wl->buffers[i].id = new_object(wl, OBJ_BUFFER);
        mb_init(&m, pool, 0);
        mb_u32(&m, wl->buffers[i].id); mb_u32(&m, 0);
        mb_u32(&m, (uint32_t)width); mb_u32(&m, (uint32_t)height);
        mb_u32(&m, (uint32_t)stride); mb_u32(&m, WL_SHM_FORMAT_XRGB8888);
        if (send_request(wl, &m, -1) < 0) return -1;
        request0(wl, pool, 1);
        wl->kinds[pool] = OBJ_ZOMBIE;
        wl->buffers[i].fd = fd;
        wl->buffers[i].size = size;
        wl->buffers[i].busy = 0;
        memset(wl->buffers[i].data, 0, size);
    }
    wl->width = width; wl->height = height; wl->draw_index = -1;
    return 0;
}

static void destroy_buffers(struct toywl *wl)
{
    for (int i = 0; i < 2; i++) {
        if (wl->buffers[i].id) {
            request0(wl, wl->buffers[i].id, 0);
            wl->kinds[wl->buffers[i].id] = OBJ_ZOMBIE;
        }
        if (wl->buffers[i].data && wl->buffers[i].data != MAP_FAILED)
            __munmap(wl->buffers[i].data, wl->buffers[i].size);
        if (wl->buffers[i].fd >= 0) __close(wl->buffers[i].fd);
        memset(&wl->buffers[i], 0, sizeof(wl->buffers[i]));
        wl->buffers[i].fd = -1;
    }
}

static int connect_display(void)
{
    char runtime_buf[108], display_buf[64];
    const char *runtime = runtime_buf;
    const char *display = display_buf;
    struct sockaddr_un addr;
    char path[108];
    int fd;
    if (environment_value("XDG_RUNTIME_DIR", runtime_buf, sizeof(runtime_buf)) < 0) {
        __fprintf(2, "toywl: XDG_RUNTIME_DIR is unset\n"); return -1;
    }
    if (environment_value("WAYLAND_DISPLAY", display_buf, sizeof(display_buf)) < 0)
        strcpy(display_buf, "wayland-0");
    if (strlen(runtime) + strlen(display) + 2 > sizeof(path)) {
        __fprintf(2, "toywl: Wayland socket path is too long\n"); return -1;
    }
    {
        int at = 0;
        for (int i = 0; runtime[i]; i++) path[at++] = runtime[i];
        path[at++] = '/';
        for (int i = 0; display[i]; i++) path[at++] = display[i];
        path[at] = '\0';
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    for (int i = 0; path[i]; i++) addr.sun_path[i] = path[i];
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { __fprintf(2, "toywl: socket failed (%d)\n", fd); return -1; }
    {
        int ret = connect(fd, (struct sockaddr *)&addr,
                          (socklen_t)(sizeof(addr.sun_family) + strlen(path) + 1));
        if (ret < 0) {
        __fprintf(2, "toywl: connect %s failed (%d)\n", path, ret);
        __close(fd); return -1;
        }
    }
    return fd;
}

struct toywl *toywl_open(const char *title, int width, int height)
{
    /* Spell out the type: this also avoids an older Toyc sizeof(*ptr-to-struct)
     * code-generation limitation in self-hosted application builds. */
    struct toywl *wl = tlibc_malloc(sizeof(struct toywl));
    struct msg_builder m;
    if (!wl) return NULL;
    memset(wl, 0, sizeof(struct toywl));
    wl->fd = connect_display();
    wl->buffers[0].fd = wl->buffers[1].fd = -1;
    if (wl->fd < 0) { tlibc_free(wl); return NULL; }
    wl->next_id = 2;
    wl->kinds[1] = OBJ_DISPLAY;
    wl->registry = new_object(wl, OBJ_REGISTRY);
    mb_init(&m, 1, 1); mb_u32(&m, wl->registry);
    if (send_request(wl, &m, -1) < 0 || roundtrip(wl) < 0) {
        __fprintf(2, "toywl: registry roundtrip failed\n"); goto fail;
    }
    if (!wl->compositor_name || !wl->shm_name || !wl->wm_base_name) {
        __fprintf(2, "toywl: compositor lacks wl_compositor, wl_shm or xdg_wm_base\n");
        goto fail;
    }
    if (bind_global(wl, wl->compositor_name, "wl_compositor",
                    wl->compositor_ver < 4 ? wl->compositor_ver : 4,
                    OBJ_COMPOSITOR, &wl->compositor) < 0) goto fail;
    if (bind_global(wl, wl->shm_name, "wl_shm", 1, OBJ_SHM, &wl->shm) < 0) goto fail;
    if (bind_global(wl, wl->wm_base_name, "xdg_wm_base",
                    wl->wm_base_ver < 6 ? wl->wm_base_ver : 6,
                    OBJ_XDG_WM_BASE, &wl->wm_base) < 0) goto fail;
    if (wl->seat_name && bind_global(wl, wl->seat_name, "wl_seat",
                    wl->seat_ver < 7 ? wl->seat_ver : 7,
                    OBJ_SEAT, &wl->seat) < 0) goto fail;

    wl->surface = new_object(wl, OBJ_SURFACE);
    mb_init(&m, wl->compositor, 0); mb_u32(&m, wl->surface);
    if (send_request(wl, &m, -1) < 0) goto fail;
    wl->xdg_surface = new_object(wl, OBJ_XDG_SURFACE);
    mb_init(&m, wl->wm_base, 2); mb_u32(&m, wl->xdg_surface); mb_u32(&m, wl->surface);
    if (send_request(wl, &m, -1) < 0) goto fail;
    wl->toplevel = new_object(wl, OBJ_XDG_TOPLEVEL);
    mb_init(&m, wl->xdg_surface, 1); mb_u32(&m, wl->toplevel);
    if (send_request(wl, &m, -1) < 0) goto fail;
    mb_init(&m, wl->toplevel, 2); mb_string(&m, title ? title : "Toyc Wayland");
    if (send_request(wl, &m, -1) < 0) goto fail;
    mb_init(&m, wl->toplevel, 3); mb_string(&m, "toyc-wayland");
    if (send_request(wl, &m, -1) < 0) goto fail;
    request0(wl, wl->surface, 6);
    wl->wanted_width = width; wl->wanted_height = height;
    while (!wl->configured)
        if (dispatch_once(wl, -1) < 0) {
            __fprintf(2, "toywl: initial xdg configure failed\n"); goto fail;
        }
    if (create_buffers(wl, wl->wanted_width, wl->wanted_height) < 0) {
        __fprintf(2, "toywl: wl_shm buffer creation failed\n"); goto fail;
    }
    wl->frame_ready = 1;
    return wl;
fail:
    toywl_close(wl);
    return NULL;
}

int toywl_dispatch(struct toywl *wl, struct toywl_input *input, int timeout_ms)
{
    int r;
    if (!wl) return -1;
    r = dispatch_once(wl, timeout_ms);
    if (r < 0) return -1;
    while (parse_events(wl) > 0) {}
    if (input) {
        memcpy(input, &wl->pending, sizeof(struct toywl_input));
        memset(&wl->pending, 0, sizeof(wl->pending));
    }
    return r;
}

int toywl_begin_frame(struct toywl *wl, struct toywl_frame *frame)
{
    if (!wl || !frame || !wl->frame_ready) return 0;
    if (wl->wanted_width != wl->width || wl->wanted_height != wl->height) {
        if (wl->buffers[0].busy || wl->buffers[1].busy) return 0;
        destroy_buffers(wl);
        if (create_buffers(wl, wl->wanted_width, wl->wanted_height) < 0) return -1;
    }
    for (int i = 0; i < 2; i++) {
        if (!wl->buffers[i].busy) {
            wl->draw_index = i;
            frame->pixels = wl->buffers[i].data;
            frame->width = wl->width; frame->height = wl->height;
            frame->stride = wl->width * 4;
            return 1;
        }
    }
    return 0;
}

int toywl_present(struct toywl *wl)
{
    struct msg_builder m;
    struct wl_buf *b;
    if (!wl || wl->draw_index < 0) return -1;
    b = &wl->buffers[wl->draw_index];
    mb_init(&m, wl->surface, 1); mb_u32(&m, b->id); mb_u32(&m, 0); mb_u32(&m, 0);
    if (send_request(wl, &m, -1) < 0) return -1;
    mb_init(&m, wl->surface, 9); mb_u32(&m, 0); mb_u32(&m, 0);
    mb_u32(&m, (uint32_t)wl->width); mb_u32(&m, (uint32_t)wl->height);
    if (send_request(wl, &m, -1) < 0) return -1;
    wl->frame_callback = new_object(wl, OBJ_CALLBACK);
    mb_init(&m, wl->surface, 3); mb_u32(&m, wl->frame_callback);
    if (send_request(wl, &m, -1) < 0) return -1;
    if (request0(wl, wl->surface, 6) < 0) return -1;
    b->busy = 1; wl->draw_index = -1; wl->frame_ready = 0;
    return 0;
}

void toywl_close(struct toywl *wl)
{
    if (!wl) return;
    destroy_buffers(wl);
    if (wl->pointer) request0(wl, wl->pointer, 1);
    if (wl->keyboard) request0(wl, wl->keyboard, 0);
    if (wl->toplevel) request0(wl, wl->toplevel, 0);
    if (wl->xdg_surface) request0(wl, wl->xdg_surface, 0);
    if (wl->surface) request0(wl, wl->surface, 0);
    if (wl->wm_base) request0(wl, wl->wm_base, 0);
    if (wl->seat && wl->seat_ver >= 5) request0(wl, wl->seat, 3);
    for (int i = 0; i < wl->fd_count; i++) __close(wl->fds[i]);
    if (wl->fd >= 0) __close(wl->fd);
    tlibc_free(wl);
}
