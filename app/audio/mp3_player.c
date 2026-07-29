/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * mp3_player — MP3 文件解码播放器
 *
 * 基于 minimp3 (public domain) 解码，ALSA PCM 直写输出。
 * 不依赖 libc/libasound，仅通过 syscall 操作 ALSA 内核接口。
 *
 * 用法：
 *   mp3_player <file.mp3>             # 播放指定文件
 *   mp3_player <file.mp3> /dev/snd/pcmC0D0p  # 指定设备
 *
 * 系统调用：openat, read, lseek, close, ioctl, write, mmap, munmap
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "errno.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

/* ── 配置 ── */

/* PCM 输出格式固定为 S16_LE（与 minimp3 默认输出一致） */
#define PCM_BITS        16

/* 读取 MP3 文件的块大小（流式读取，避免 mmap 整个大文件） */
#define READ_BLOCK_SZ   (256 * 1024)   /* 256 KB 块 */

/* 重采样输出缓冲区：最大一帧 1152 帧 × 2ch，48000/44100 最大放大比 ~1.089 */
#define RESAMPLE_BUF_SZ  4096

/* ── 状态 ── */

struct mp3_player {
    const char *dev_path;       /* PCM 设备路径，NULL 表示自动扫描 */
    int mp3_fd;                 /* MP3 文件 fd */
    unsigned char *buf;         /* 文件数据缓冲区 */
    int buf_len;                /* 缓冲区有效数据长度 */
    int buf_cap;                /* 缓冲区总容量 */
    int buf_pos;                /* 当前解码位置 */

    mp3dec_t dec;               /* minimp3 解码器状态 */
    mp3dec_frame_info_t info;   /* 当前帧信息 */
    struct tlibc_pcm pcm;       /* ALSA PCM 设备 */

    int mp3_rate;               /* 原始 MP3 采样率（第一帧确定） */
    int mp3_channels;           /* 原始 MP3 声道数（第一帧确定） */
    int alsa_rate;              /* 实际 PCM 设备采样率（配置后确定） */
    int total_frames;           /* 已播放帧数（用于日志） */

    /* 重采样临时缓冲区（避免栈上大分配） */
    mp3d_sample_t resample_buf[RESAMPLE_BUF_SZ];
};

/* ── 文件读取 ── */

static int read_file_chunk(struct mp3_player *p)
{
    unsigned char *dst;
    int space;

    if (p->buf_pos > 0 && p->buf_pos < p->buf_len) {
        int remain = p->buf_len - p->buf_pos;
        memmove(p->buf, p->buf + p->buf_pos, remain);
        p->buf_len = remain;
        p->buf_pos = 0;
        dst = p->buf + p->buf_len;
        space = p->buf_cap - p->buf_len;
    } else {
        p->buf_len = 0;
        p->buf_pos = 0;
        dst = p->buf;
        space = p->buf_cap;
    }

    if (space <= 0)
        return 0;
    long n = __read(p->mp3_fd, dst, (unsigned long)space);
    if (n < 0)  return -1;
    if (n == 0) return  0;
    p->buf_len += (int)n;
    return (int)n;
}

/* ── 线性插值采样率转换 ── */

static int resample_pcm(const mp3d_sample_t *in, int in_frames,
                        int in_rate, int out_rate, int ch,
                        mp3d_sample_t *out)
{
    if (in_rate == out_rate)
        return in_frames;

    /* 步进 = 输入帧 / 输出帧 = in_rate / out_rate
     * 对每帧输出，pos 前进此步长 */
    double step  = (double)in_rate / (double)out_rate;
    double pos   = 0.0;
    int out_idx  = 0;

    while (pos < in_frames - 1.0) {
        int i0 = (int)pos;
        int i1 = i0 + 1;
        if (i1 >= in_frames)
            break;
        double frac = pos - (double)i0;

        for (int c = 0; c < ch; c++) {
            double a = in[i0 * ch + c];
            double b = in[i1 * ch + c];
            double v = a + frac * (b - a);
            /* 钳位到 int16_t 范围 */
            if (v > 32767.0)  v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            out[out_idx * ch + c] = (mp3d_sample_t)(v + 0.5);
        }

        pos += step;
        out_idx++;
    }
    return out_idx;
}

/* ── 音频输出 ── */

static int open_audio(struct mp3_player *p)
{
    int ret = tlibc_pcm_open(&p->pcm, p->dev_path,
                             (unsigned int)p->info.channels,
                             (unsigned int)p->info.hz, PCM_BITS);
    if (ret < 0) {
        __printf("mp3_player: 无法打开 PCM 设备 (errno=%d)\n", -ret);
        return ret;
    }

    ret = tlibc_pcm_configure(&p->pcm);
    if (ret < 0) {
        __printf("mp3_player: PCM 配置失败 (errno=%d)\n", -ret);
        tlibc_pcm_close(&p->pcm);
        return ret;
    }

    /* 记录 ALSA 实际协商出的采样率 */
    p->alsa_rate = (int)p->pcm.rate;

    if (p->alsa_rate != p->info.hz) {
        __printf("mp3_player: 音频设备已配置 — %dHz/%dbit/%dch"
                 "  (源 %dHz，将重采样适配)\n",
                 p->alsa_rate, PCM_BITS, p->pcm.channels, p->info.hz);
    } else {
        __printf("mp3_player: 音频设备已配置 — %dHz/%dbit/%dch, 缓冲区 %lu 帧\n",
                 p->alsa_rate, PCM_BITS, p->pcm.channels, p->pcm.buffer_size);
    }
    return 0;
}

static int write_pcm(struct mp3_player *p, mp3d_sample_t *samples,
                      int samples_per_ch)
{
    /* samples_per_ch 是 mp3dec_decode_frame 返回值（每声道采样数）。
     * 使用 tlibc_pcm_write 而非 write_all：
     *   write_all 内部会 drain，导致设备停止，后续写入失败 (EBADFD)。 */
    long ret = tlibc_pcm_write(&p->pcm, samples,
                               (unsigned long)samples_per_ch);
    if (ret < 0)
        return (int)ret;

    p->total_frames += samples_per_ch;
    return 0;
}

/* ── 主逻辑 ── */

static int play_mp3(struct mp3_player *p)
{
    int ret;

    /* 第一轮读取 */
    ret = read_file_chunk(p);
    if (ret <= 0) {
        __printf("mp3_player: 文件为空或读错误\n");
        return 1;
    }

    /* 解码第一帧以获取音频格式 */
    mp3d_sample_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples = mp3dec_decode_frame(&p->dec,
                     p->buf + p->buf_pos,
                     p->buf_len - p->buf_pos,
                     pcm_buf, &p->info);
    if (samples <= 0) {
        __printf("mp3_player: 不是有效的 MP3 文件\n");
        return 1;
    }
    p->buf_pos += p->info.frame_bytes;
    p->mp3_rate = p->info.hz;
    p->mp3_channels = p->info.channels;

    __printf("mp3_player: %d Hz, %d ch, %d kbps, Layer %d\n",
             p->info.hz, p->info.channels,
             p->info.bitrate_kbps, p->info.layer);

    /* 打开音频设备 */
    ret = open_audio(p);
    if (ret < 0)
        return 1;

    /* 重采样第一帧并写入 */
    {
        int out_frames = resample_pcm(pcm_buf, samples,
                              p->mp3_rate, p->alsa_rate, p->mp3_channels,
                              p->resample_buf);
        ret = write_pcm(p, p->resample_buf, out_frames);
        if (ret < 0) {
            __printf("mp3_player: PCM 写入失败 (errno=%d)\n", -ret);
            tlibc_pcm_close(&p->pcm);
            return 1;
        }
    }

    /* 解码并播放后续帧 */
    for (;;) {
        while (p->buf_pos + 4 >= p->buf_len) {
            ret = read_file_chunk(p);
            if (ret < 0) {
                __printf("mp3_player: 读取错误\n");
                tlibc_pcm_drain(&p->pcm);
                tlibc_pcm_close(&p->pcm);
                return 1;
            }
            if (ret == 0)
                goto done;
        }

        samples = mp3dec_decode_frame(&p->dec,
                     p->buf + p->buf_pos,
                     p->buf_len - p->buf_pos,
                     pcm_buf, &p->info);

        if (samples > 0) {
            /* 检查格式是否变化 */
            if (p->mp3_rate != p->info.hz
                || p->mp3_channels != p->info.channels) {
                __printf("mp3_player: 流格式变化"
                         " %dch/%dHz → %dch/%dHz，重新配置设备\n",
                         p->mp3_channels, p->mp3_rate,
                         p->info.channels, p->info.hz);
                p->mp3_rate = p->info.hz;
                p->mp3_channels = p->info.channels;
                tlibc_pcm_drain(&p->pcm);
                tlibc_pcm_close(&p->pcm);
                p->pcm.channels = 0;
                ret = open_audio(p);
                if (ret < 0)
                    return 1;
            }

            /* 重采样后再写入 */
            {
                int out_frames = resample_pcm(pcm_buf, samples,
                                      p->mp3_rate, p->alsa_rate,
                                      p->mp3_channels, p->resample_buf);
                ret = write_pcm(p, p->resample_buf, out_frames);
                if (ret < 0) {
                    __printf("mp3_player: PCM 写入失败 (errno=%d)\n", -ret);
                    tlibc_pcm_drain(&p->pcm);
                    tlibc_pcm_close(&p->pcm);
                    return 1;
                }
            }
        }

        if (p->info.frame_bytes <= 0)
            break;
        p->buf_pos += p->info.frame_bytes;
    }

done:
    __printf("mp3_player: 播放完成 — %d 帧\n", p->total_frames);
    tlibc_pcm_drain(&p->pcm);
    tlibc_pcm_close(&p->pcm);
    return 0;
}

/* ── 入口 ── */

int main(int argc, char **argv)
{
    const char *file_path;
    struct mp3_player player;

    memset(&player, 0, sizeof(player));

    if (argc < 2) {
        __printf("用法: mp3_player <file.mp3> [pcm_device]\n");
        __printf("  示例: mp3_player song.mp3\n");
        __printf("        mp3_player song.mp3 /dev/snd/pcmC0D0p\n");
        return 1;
    }

    file_path = argv[1];
    player.dev_path = (argc > 2) ? argv[2] : NULL;

    player.mp3_fd = __openat(AT_FDCWD, file_path, O_RDONLY, 0);
    if (player.mp3_fd < 0) {
        __printf("mp3_player: 无法打开 '%s' (errno=%d)\n",
                 file_path, -player.mp3_fd);
        return 1;
    }

    player.buf_cap = READ_BLOCK_SZ;
    player.buf = (unsigned char *)tlibc_malloc((unsigned long)player.buf_cap);
    if (!player.buf) {
        __printf("mp3_player: 内存不足\n");
        __close(player.mp3_fd);
        return 1;
    }

    mp3dec_init(&player.dec);

    int ret = play_mp3(&player);

    if (player.buf)
        tlibc_free(player.buf);
    if (player.mp3_fd >= 0)
        __close(player.mp3_fd);

    return ret;
}
