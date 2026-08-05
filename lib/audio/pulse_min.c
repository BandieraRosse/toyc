/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * pulse_min.c — WSLg PulseAudio Native Protocol v8 最小播放客户端
 *
 * 不依赖 libpulse/paplay。只实现游戏所需的单播放流：Unix socket 认证、
 * 客户端命名、S16_LE stream、按服务器 REQUEST 发送 PCM、drain 和关闭。
 * 主动使用协议 v8，避免新协议的 proplist、格式扩展、SHM 和 memfd。
 */

#include "tlibc_everything.h"
#include "pulse_min.h"
#include "errno.h"

#define PA_PROTOCOL_VERSION       8
#define PA_COOKIE_SIZE            256
#define PA_INVALID_INDEX          0xffffffffU
#define PA_COMMAND_REPLY          2
#define PA_COMMAND_CREATE_STREAM  3
#define PA_COMMAND_DELETE_STREAM  4
#define PA_COMMAND_AUTH           8
#define PA_COMMAND_SET_NAME       9
#define PA_COMMAND_DRAIN          12
#define PA_COMMAND_REQUEST        61
#define PA_PACKET_CHANNEL         0xffffffffU
#define PA_SAMPLE_S16LE           3
#define PA_CHANNEL_LEFT           1
#define PA_CHANNEL_RIGHT          2
#define PA_VOLUME_NORM            65536U
#define PA_CONTROL_MAX            8192
#define PA_TAG_MAX                2048

struct pa_builder {
    unsigned char data[PA_TAG_MAX];
    int len;
};

struct pa_reader {
    const unsigned char *data;
    int len;
    int pos;
};

static unsigned int be32_load(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static void be32_store(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static int io_write_all(int fd, const void *data, unsigned long size)
{
    const unsigned char *p = (const unsigned char *)data;
    while (size > 0) {
        long n = __write(fd, p, (int)size);
        if (n < 0) return (int)n;
        if (n == 0) return -EIO;
        p += n;
        size -= (unsigned long)n;
    }
    return 0;
}

static int io_read_all(int fd, void *data, unsigned long size)
{
    unsigned char *p = (unsigned char *)data;
    while (size > 0) {
        long n = __read(fd, p, (int)size);
        if (n < 0) return (int)n;
        if (n == 0) return -EPIPE;
        p += n;
        size -= (unsigned long)n;
    }
    return 0;
}

static int put_byte(struct pa_builder *b, unsigned int value)
{
    if (b->len >= PA_TAG_MAX) return -ENOSPC;
    b->data[b->len++] = (unsigned char)value;
    return 0;
}

static int put_raw(struct pa_builder *b, const void *data, int size)
{
    if (size < 0 || b->len + size > PA_TAG_MAX) return -ENOSPC;
    memcpy(b->data + b->len, data, (unsigned long)size);
    b->len += size;
    return 0;
}

static int put_u32(struct pa_builder *b, unsigned int value)
{
    unsigned char tmp[4];
    if (put_byte(b, 'L') < 0) return -ENOSPC;
    be32_store(tmp, value);
    return put_raw(b, tmp, 4);
}

static int put_string(struct pa_builder *b, const char *value)
{
    if (!value) return put_byte(b, 'N');
    if (put_byte(b, 't') < 0) return -ENOSPC;
    return put_raw(b, value, (int)strlen(value) + 1);
}

static int put_bool(struct pa_builder *b, int value)
{
    return put_byte(b, value ? '1' : '0');
}

static int put_arbitrary(struct pa_builder *b, const void *data, int size)
{
    unsigned char tmp[4];
    if (put_byte(b, 'x') < 0) return -ENOSPC;
    be32_store(tmp, (unsigned int)size);
    if (put_raw(b, tmp, 4) < 0) return -ENOSPC;
    return put_raw(b, data, size);
}

static int put_sample_spec(struct pa_builder *b, unsigned int rate,
                           unsigned int channels)
{
    unsigned char tmp[4];
    if (put_byte(b, 'a') < 0 || put_byte(b, PA_SAMPLE_S16LE) < 0 ||
        put_byte(b, channels) < 0) return -ENOSPC;
    be32_store(tmp, rate);
    return put_raw(b, tmp, 4);
}

static int put_channel_map(struct pa_builder *b, unsigned int channels)
{
    if (put_byte(b, 'm') < 0 || put_byte(b, channels) < 0)
        return -ENOSPC;
    if (channels == 1) return put_byte(b, 0); /* mono */
    if (put_byte(b, PA_CHANNEL_LEFT) < 0) return -ENOSPC;
    return put_byte(b, PA_CHANNEL_RIGHT);
}

static int put_volume(struct pa_builder *b, unsigned int channels)
{
    unsigned char tmp[4];
    if (put_byte(b, 'v') < 0 || put_byte(b, channels) < 0)
        return -ENOSPC;
    for (unsigned int i = 0; i < channels; i++) {
        be32_store(tmp, PA_VOLUME_NORM);
        if (put_raw(b, tmp, 4) < 0) return -ENOSPC;
    }
    return 0;
}

static int get_u32(struct pa_reader *r, unsigned int *value)
{
    if (r->pos + 5 > r->len || r->data[r->pos] != 'L') return -EPROTO;
    *value = be32_load(r->data + r->pos + 1);
    r->pos += 5;
    return 0;
}

static int send_frame(int fd, unsigned int channel, unsigned int flags,
                      const void *payload, unsigned int size)
{
    unsigned char desc[20];
    be32_store(desc, size);
    be32_store(desc + 4, channel);
    be32_store(desc + 8, 0);
    be32_store(desc + 12, 0);
    be32_store(desc + 16, flags);
    if (io_write_all(fd, desc, sizeof(desc)) < 0) return -EPIPE;
    return io_write_all(fd, payload, size);
}

static int send_command(struct toy_pulse *p, struct pa_builder *b)
{
    return send_frame(p->fd, PA_PACKET_CHANNEL, 0, b->data,
                      (unsigned int)b->len);
}

/* 读取一个控制包。Native Protocol 也允许服务端发数据包，但播放客户端
 * 不应收到 PCM；发现非控制 channel 时读掉并忽略。 */
static int receive_control(struct toy_pulse *p, unsigned char *payload,
                           int cap, int *payload_len)
{
    unsigned char desc[20];
    for (;;) {
        unsigned int size, channel;
        int ret = io_read_all(p->fd, desc, sizeof(desc));
        if (ret < 0) return ret;
        size = be32_load(desc);
        channel = be32_load(desc + 4);
        if (size == 0 || size > (unsigned int)cap) return -EPROTO;
        ret = io_read_all(p->fd, payload, size);
        if (ret < 0) return ret;
        if (channel == PA_PACKET_CHANNEL) {
            *payload_len = (int)size;
            return 0;
        }
    }
}

static int parse_header(const unsigned char *data, int len,
                        unsigned int *command, unsigned int *tag,
                        struct pa_reader *reader)
{
    reader->data = data;
    reader->len = len;
    reader->pos = 0;
    if (get_u32(reader, command) < 0 || get_u32(reader, tag) < 0)
        return -EPROTO;
    return 0;
}

/* 等待指定 tag 的 reply，同时吸收异步 REQUEST 并累加可写字节。 */
static int wait_reply(struct toy_pulse *p, unsigned int wanted_tag,
                      struct pa_reader *reply)
{
    static unsigned char packet[PA_CONTROL_MAX];
    for (;;) {
        struct pa_reader r;
        unsigned int command, tag;
        int len, ret = receive_control(p, packet, sizeof(packet), &len);
        if (ret < 0) return ret;
        if (parse_header(packet, len, &command, &tag, &r) < 0)
            return -EPROTO;
        if (command == PA_COMMAND_REQUEST) {
            unsigned int channel, bytes;
            if (get_u32(&r, &channel) < 0 || get_u32(&r, &bytes) < 0)
                return -EPROTO;
            if (channel == p->channel)
                p->requested_bytes += bytes;
            continue;
        }
        if (tag != wanted_tag) continue;
        if (command != PA_COMMAND_REPLY) return -EACCES;
        /* Toyc 自托管路径避免整结构赋值。 */
        reply->data = r.data;
        reply->len = r.len;
        reply->pos = r.pos;
        return 0;
    }
}

int toy_pulse_available(void)
{
    struct stat st;
    return stat(TOYC_PULSE_SOCKET, &st) == 0;
}

int toy_pulse_open(struct toy_pulse *p, unsigned int rate,
                   unsigned int channels)
{
    struct sockaddr_un addr;
    struct pa_builder b;
    struct pa_reader reply;
    unsigned char cookie[PA_COOKIE_SIZE];
    unsigned int ignored;
    int ret;

    if (!p || rate < 8000 || rate > 192000 ||
        (channels != 1 && channels != 2)) return -EINVAL;
    memset(p, 0, sizeof(*p));
    p->fd = -1;
    p->rate = rate;
    p->channels = channels;
    p->frame_size = channels * 2;
    p->channel = PA_INVALID_INDEX;
    p->stream_index = PA_INVALID_INDEX;

    p->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (p->fd < 0) return p->fd;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, TOYC_PULSE_SOCKET);
    ret = connect(p->fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) goto fail;

    memset(cookie, 0, sizeof(cookie));
    memset(&b, 0, sizeof(b));
    p->tag = 0;
    put_u32(&b, PA_COMMAND_AUTH);
    put_u32(&b, p->tag);
    put_u32(&b, PA_PROTOCOL_VERSION);
    put_arbitrary(&b, cookie, sizeof(cookie));
    if ((ret = send_command(p, &b)) < 0) goto fail;
    if ((ret = wait_reply(p, p->tag++, &reply)) < 0) {
        fprintf(2, "pulse_min: auth reply failed (%d)\n", -ret);
        goto fail;
    }
    if ((ret = get_u32(&reply, &ignored)) < 0) {
        fprintf(2, "pulse_min: auth reply parse failed\n");
        goto fail;
    }

    memset(&b, 0, sizeof(b));
    put_u32(&b, PA_COMMAND_SET_NAME);
    put_u32(&b, p->tag);
    put_string(&b, "Toyc game audio");
    if ((ret = send_command(p, &b)) < 0) goto fail;
    if ((ret = wait_reply(p, p->tag++, &reply)) < 0) {
        fprintf(2, "pulse_min: set-name reply failed (%d)\n", -ret);
        goto fail;
    }

    memset(&b, 0, sizeof(b));
    put_u32(&b, PA_COMMAND_CREATE_STREAM);
    put_u32(&b, p->tag);
    put_string(&b, "Toyc mixed audio");
    put_sample_spec(&b, rate, channels);
    put_channel_map(&b, channels);
    put_u32(&b, PA_INVALID_INDEX);
    put_string(&b, NULL);
    put_u32(&b, 32768); /* maxlength: ~170ms at 48k stereo */
    put_bool(&b, 0);    /* uncorked */
    put_u32(&b, 8192);  /* target: ~43ms */
    put_u32(&b, 4096);  /* prebuffer: ~21ms */
    put_u32(&b, 2048);  /* request quantum: ~11ms */
    put_u32(&b, 0);     /* sync id */
    put_volume(&b, channels);
    if ((ret = send_command(p, &b)) < 0) goto fail;
    if ((ret = wait_reply(p, p->tag++, &reply)) < 0) {
        fprintf(2, "pulse_min: create-stream reply failed (%d)\n", -ret);
        goto fail;
    }
    if ((ret = get_u32(&reply, &p->channel)) < 0 ||
        (ret = get_u32(&reply, &p->stream_index)) < 0 ||
        (ret = get_u32(&reply, &ignored)) < 0) {
        fprintf(2, "pulse_min: create-stream parse failed (%d)\n", -ret);
        goto fail;
    }
    p->requested_bytes = ignored;
    return 0;

fail:
    toy_pulse_close(p);
    return ret;
}

long toy_pulse_write(struct toy_pulse *p, const void *pcm,
                     unsigned long frames)
{
    const unsigned char *data = (const unsigned char *)pcm;
    unsigned long total, done = 0;
    unsigned char packet[PA_CONTROL_MAX];

    if (!p || p->fd < 0 || !pcm) return -EINVAL;
    total = frames * p->frame_size;
    while (done < total) {
        while (p->requested_bytes == 0) {
            struct pa_reader r;
            unsigned int command, tag;
            int len, ret = receive_control(p, packet, sizeof(packet), &len);
            if (ret < 0) return ret;
            if (parse_header(packet, len, &command, &tag, &r) < 0)
                return -EPROTO;
            if (command == PA_COMMAND_REQUEST) {
                unsigned int channel, bytes;
                if (get_u32(&r, &channel) < 0 || get_u32(&r, &bytes) < 0)
                    return -EPROTO;
                if (channel == p->channel)
                    p->requested_bytes += bytes;
            }
        }
        {
            unsigned long chunk = total - done;
            int ret;
            if (chunk > p->requested_bytes) chunk = p->requested_bytes;
            if (chunk > 16384) chunk = 16384;
            chunk -= chunk % p->frame_size;
            if (chunk == 0) return -EPROTO;
            ret = send_frame(p->fd, p->channel, 0, data + done,
                             (unsigned int)chunk);
            if (ret < 0) return ret;
            p->requested_bytes -= chunk;
            done += chunk;
        }
    }
    return (long)frames;
}

int toy_pulse_drain(struct toy_pulse *p)
{
    struct pa_builder b;
    struct pa_reader reply;
    int ret;
    if (!p || p->fd < 0) return -EINVAL;
    memset(&b, 0, sizeof(b));
    put_u32(&b, PA_COMMAND_DRAIN);
    put_u32(&b, p->tag);
    put_u32(&b, p->channel);
    if ((ret = send_command(p, &b)) < 0) return ret;
    return wait_reply(p, p->tag++, &reply);
}

void toy_pulse_close(struct toy_pulse *p)
{
    if (!p) return;
    if (p->fd >= 0) {
        if (p->stream_index != PA_INVALID_INDEX) {
            struct pa_builder b;
            memset(&b, 0, sizeof(b));
            put_u32(&b, PA_COMMAND_DELETE_STREAM);
            put_u32(&b, p->tag++);
            put_u32(&b, p->channel);
            send_command(p, &b);
        }
        __close(p->fd);
    }
    p->fd = -1;
}
