/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * mp3_player_libc — MP3 文件解码播放器（标准 libc + libasound 版本）
 *
 * 对照 toyc 版 app/audio/mp3_player.c 的相同设计，使用标准 C 库和 ALSA libasound。
 * 用于对比验证撕裂（XRUN）问题是否源于 toyc 运行时/ALSA 直写。
 *
 * 双线程架构，环形缓冲 + pthread 条件变量同步：
 *
 *   主线程   →   解码线程（fread → MP3 解码 → 重采样）
 *                        ↓ 环形缓冲（pthread_cond_t 同步）
 *              音频线程（从环形缓冲读取 → snd_pcm_writei 输出）
 *
 * 编译：
 *   cd /home/lu/Desktop/toyc
 *   gcc -O2 -pthread -lasound -o mp3_player_libc mp3_player_libc.c
 *
 * 用法：
 *   ./mp3_player_libc .claude/GodKnows.mp3              # 默认设备
 *   ./mp3_player_libc .claude/GodKnows.mp3 hw:0,0       # 指定设备
 *
 * 依赖：minimp3.h (public domain)，libasound (ALSA 用户态库)
 */

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

/* ── 配置 ── */

/* PCM 输出格式固定为 S16_LE（与 minimp3 默认输出一致） */
#define PCM_BITS        16

/* 读取 MP3 文件的块大小（流式读取，避免 mmap 整个大文件） */
#define READ_BLOCK_SZ   (256 * 1024)   /* 256 KB 块 */

/* 重采样输出缓冲区：最大一帧 1152 帧 × 2ch，48000/44100 最大放大比 ~1.089 */
#define RESAMPLE_BUF_SZ  4096

/* 音频线程每次从环形缓冲读取的最大帧数（outburst 限制，同 mpg123） */
#define PCM_READ_BATCH   8192

/* 预加载阈值：缓冲半满才启动播放，避免首帧即 XRUN */
#define PRELOAD_THRESHOLD  (RING_BUF_SIZE / 2)

/* XRUN 后恢复预加载阈值 */
#define RECOVER_THRESHOLD  (RING_BUF_SIZE / 4)

/* ── 环形缓冲 ──
 *
 * 环形缓冲大小（帧数），必须是 2 的幂。
 * 16384 帧 ≈ 341ms @48kHz，解码线程超前播放线程约 0.3 秒。
 * 解码快于播放时，缓冲填满后解码线程通过 pthread_cond_wait 等待消费；
 * 解码慢于播放时，缓冲排空后音频线程通过 pthread_cond_wait 等待解码。
 */
#define RING_BUF_SHIFT  14
#define RING_BUF_SIZE   (1 << RING_BUF_SHIFT)   /* 16384 */
#define RING_BUF_MASK   (RING_BUF_SIZE - 1)

struct ring_buffer {
    mp3d_sample_t buf[RING_BUF_SIZE * 2];   /* 立体声交错 PCM 数据 */
    volatile unsigned int wpos;              /* 生产者写入位置（单调递增帧计数）*/
    volatile unsigned int rpos;              /* 消费者读取位置（单调递增帧计数）*/
    volatile int eos;                        /* 生产者设置的结束标志 */
    int ch;                                  /* 声道数（1 或 2） */
    pthread_mutex_t mutex;                   /* 互斥锁 */
    pthread_cond_t  cond;                    /* 条件变量（生产者/消费者通知）*/
};

/* 缓冲中可读帧数 */
static inline int ring_avail(const struct ring_buffer *r)
{
    return (int)(r->wpos - r->rpos);
}

/* 缓冲中空闲帧数 */
static inline int ring_free(const struct ring_buffer *r)
{
    return RING_BUF_SIZE - ring_avail(r);
}

/* 生产者：将 data 写入环形缓冲，若缓冲满则阻塞等待消费者读取 */
static void ring_write(struct ring_buffer *r,
                       const mp3d_sample_t *data, int frames)
{
    int ch = r->ch;

    pthread_mutex_lock(&r->mutex);

    while (frames > 0) {
        /* 等待空间 */
        while (ring_free(r) <= 0)
            pthread_cond_wait(&r->cond, &r->mutex);

        int batch = frames;
        int space = ring_free(r);
        if (batch > space)
            batch = space;

        /* 环形缓冲写位置（模运算） */
        unsigned int wp = r->wpos & RING_BUF_MASK;
        int to_end = RING_BUF_SIZE - (int)wp;
        if (batch > to_end)
            batch = to_end;

        /* 拷贝 PCM 数据到环形缓冲 */
        int total = batch * ch;
        for (int i = 0; i < total; i++)
            r->buf[wp * ch + i] = data[i];

        r->wpos += (unsigned int)batch;
        frames -= batch;
        data  += batch * ch;
    }

    /* 唤醒可能在 wpos/rpos 上等待的消费者 */
    pthread_cond_broadcast(&r->cond);
    pthread_mutex_unlock(&r->mutex);
}

/* 消费者：等待并读取最多 max_frames 帧，返回实际读取帧数。0 = EOS */
static int ring_wait_read(struct ring_buffer *r,
                          mp3d_sample_t *dst, int max_frames)
{
    int ch = r->ch;

    pthread_mutex_lock(&r->mutex);

    for (;;) {
        int avail = ring_avail(r);

        if (avail > 0) {
            int batch = (max_frames < avail) ? max_frames : avail;

            /* 环形缓冲读位置（模运算） */
            unsigned int rp = r->rpos & RING_BUF_MASK;
            int to_end = RING_BUF_SIZE - (int)rp;
            if (batch > to_end)
                batch = to_end;

            /* 从环形缓冲拷贝 PCM 数据 */
            int total = batch * ch;
            for (int i = 0; i < total; i++)
                dst[i] = r->buf[rp * ch + i];

            r->rpos += (unsigned int)batch;

            /* 唤醒可能在 rpos 上等待的生产者 */
            pthread_cond_broadcast(&r->cond);
            pthread_mutex_unlock(&r->mutex);
            return batch;
        }

        /* 无数据可用，检查 EOS。但必须先确认 wpos 没有再推进—— */
        /* 生产者可能在 'avail = 0' 和 'eos 检查' 之间写入数据+设 eos */
        if (r->eos) {
            /* 二次确认：生产者写入后设 eos 的窗口极小，但必须堵住 */
            avail = ring_avail(r);
            if (avail > 0) {
                /* 数据已到，下轮读取 */
                continue;
            }
            pthread_mutex_unlock(&r->mutex);
            return 0;   /* 真正 EOS */
        }

        /* 无数据且无 EOS，等待生产者唤醒 */
        pthread_cond_wait(&r->cond, &r->mutex);
        /* 醒来后回到循环顶部，检查 avail 或 eos */
    }
}

/* 等待环形缓冲积累至少 min_frames 帧（不消费），
 * 返回积累后的实际可用帧数。0 = EOS。
 * 用于音频线程预加载。 */
static int ring_wait_avail(struct ring_buffer *r, int min_frames)
{
    pthread_mutex_lock(&r->mutex);

    for (;;) {
        int avail = ring_avail(r);
        if (avail >= min_frames) {
            pthread_mutex_unlock(&r->mutex);
            return avail;
        }

        /* 检查 EOS */
        if (r->eos) {
            avail = ring_avail(r);
            if (avail > 0) {
                pthread_mutex_unlock(&r->mutex);
                return avail;
            }
            pthread_mutex_unlock(&r->mutex);
            return 0;   /* 真正 EOS */
        }

        /* 等待生产者唤醒 */
        pthread_cond_wait(&r->cond, &r->mutex);
    }
}

/* 生产者标记结束 */
static void ring_mark_eos(struct ring_buffer *r)
{
    pthread_mutex_lock(&r->mutex);
    r->eos = 1;
    pthread_cond_broadcast(&r->cond);
    pthread_mutex_unlock(&r->mutex);
}

/* ── 状态 ── */

struct mp3_player {
    /* ── 输入 ── */
    const char *dev_path;
    FILE *mp3_file;

    /* ── 文件读取缓冲（解码线程独占）─ */
    unsigned char *file_buf;
    int file_len;
    int file_cap;
    int file_pos;

    /* ── 解码器状态（解码线程独占）─ */
    mp3dec_t dec;
    mp3dec_frame_info_t info;

    /* ── PCM 格式（首次解码后只读，线程安全）─ */
    int mp3_rate;
    int mp3_channels;

    /* ── ALSA 设备（音频线程独占）─ */
    snd_pcm_t *pcm;
    int alsa_rate;

    /* ── 重采样（解码线程独占）─ */
    mp3d_sample_t resample_buf[RESAMPLE_BUF_SZ];
    double resample_remainder;

    /* ── 环形缓冲（线程共享，条件变量同步）─ */
    struct ring_buffer ring;

    /* ── 线程 ── */
    pthread_t dec_thread;
    pthread_t aud_thread;
    volatile int quit;          /* 任一线程出错时设此标志，通知另一线程退出 */

    /* ── 统计（主线程读取）─ */
    int total_frames;
};

/* ── 文件读取 ── */

static int read_file_chunk(struct mp3_player *p)
{
    unsigned char *dst;
    int space;

    if (p->file_pos > 0 && p->file_pos < p->file_len) {
        int remain = p->file_len - p->file_pos;
        memmove(p->file_buf, p->file_buf + p->file_pos, (size_t)remain);
        p->file_len = remain;
        p->file_pos = 0;
        dst = p->file_buf + p->file_len;
        space = p->file_cap - p->file_len;
    } else {
        p->file_len = 0;
        p->file_pos = 0;
        dst = p->file_buf;
        space = p->file_cap;
    }

    if (space <= 0)
        return 0;

    size_t n = fread(dst, 1, (size_t)space, p->mp3_file);
    if (n == 0) {
        if (feof(p->mp3_file))
            return 0;
        return -1;  /* 读错误 */
    }
    p->file_len += (int)n;
    return (int)n;
}

/* ── 线性插值采样率转换 ──
 *
 * 与 toyc 版完全一致的实现。
 */

static int resample_pcm(const mp3d_sample_t *in, int in_frames,
                        int in_rate, int out_rate, int ch,
                        mp3d_sample_t *out)
{
    if (in_rate == out_rate)
        return in_frames;

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
            if (v > 32767.0)  v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            out[out_idx * ch + c] = (mp3d_sample_t)(v + 0.5);
        }

        pos += step;
        out_idx++;
    }
    return out_idx;
}

/* 带取整补偿的重采样 */
static int resample_pcm_frac(const mp3d_sample_t *in, int in_frames,
                             int in_rate, int out_rate, int ch,
                             mp3d_sample_t *out, double *remainder)
{
    int base = resample_pcm(in, in_frames, in_rate, out_rate, ch, out);
    if (in_rate == out_rate || base <= 0)
        return base;

    double ideal = (double)in_frames * (double)out_rate / (double)in_rate;
    double acc   = ideal - (double)base + *remainder;

    if (acc >= 1.0) {
        int extra = (int)acc;
        acc -= (double)extra;

        mp3d_sample_t *last = out + (base - 1) * ch;
        for (int e = 0; e < extra; e++) {
            mp3d_sample_t *dst = out + (base + e) * ch;
            for (int c = 0; c < ch; c++)
                dst[c] = last[c];
        }
        base += extra;
    }

    *remainder = acc;
    return base;
}

/* ── ALSA 音频输出（音频线程）─ */

/*
 * 尝试打开 PCM 设备，若 dev 为 NULL 则顺序尝试：
 *   1. hw:1,0（板载模拟输出，本机 ALC287）
 *   2. default（PulseAudio / ALSA 默认）
 *   3. hw:0,3（HDMI/DP，无显示器时可能无声）
 * 以匹配 toyc 版扫描 /dev/snd/ 的行为。
 */
static int try_open_device(const char *dev, snd_pcm_t **handle)
{
    int ret = snd_pcm_open(handle, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: 尝试 '%s' 失败: %s\n",
                dev, snd_strerror(ret));
    }
    return ret;
}

static int open_audio(struct mp3_player *p)
{
    snd_pcm_t *handle = NULL;
    snd_pcm_hw_params_t *hw;
    int ret = -1;
    unsigned int rate;

    /* ── 设备发现 ── */
    if (p->dev_path) {
        ret = try_open_device(p->dev_path, &handle);
    } else {
        const char *probe_list[] = {
            "hw:1,0",     /* 板载模拟输出 */
            "default",    /* PulseAudio 等 */
            "hw:0,3",     /* HDMI 回退 */
            NULL
        };
        for (int i = 0; probe_list[i]; i++) {
            ret = try_open_device(probe_list[i], &handle);
            if (ret >= 0) {
                printf("mp3_player: 使用设备 '%s'\n", probe_list[i]);
                break;
            }
        }
    }

    if (ret < 0 || !handle) {
        fprintf(stderr, "mp3_player: 无法打开任何 PCM 设备\n");
        return -1;
    }

    /* 分配并初始化 HW 参数 */
    snd_pcm_hw_params_alloca(&hw);
    ret = snd_pcm_hw_params_any(handle, hw);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: snd_pcm_hw_params_any: %s\n",
                snd_strerror(ret));
        snd_pcm_close(handle);
        return -1;
    }

    ret = snd_pcm_hw_params_set_access(handle, hw,
                                        SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: 设置 interleaved 失败: %s\n",
                snd_strerror(ret));
        snd_pcm_close(handle);
        return -1;
    }

    ret = snd_pcm_hw_params_set_format(handle, hw, SND_PCM_FORMAT_S16_LE);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: 设置 S16_LE 失败: %s\n",
                snd_strerror(ret));
        snd_pcm_close(handle);
        return -1;
    }

    ret = snd_pcm_hw_params_set_channels(handle, hw,
                                          (unsigned int)p->mp3_channels);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: 设置 %d 声道失败: %s\n",
                p->mp3_channels, snd_strerror(ret));
        snd_pcm_close(handle);
        return -1;
    }

    /* 设置采样率（允许设备使用相近值） */
    rate = (unsigned int)p->mp3_rate;
    ret = snd_pcm_hw_params_set_rate_near(handle, hw, &rate, NULL);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: 设置采样率 %u 失败: %s\n",
                (unsigned int)p->mp3_rate, snd_strerror(ret));
        snd_pcm_close(handle);
        return -1;
    }

    /* 缓冲区大小：~500ms，减少 XRUN 概率 */
    {
        snd_pcm_uframes_t buf_frames = rate / 2;  /* 500ms */
        snd_pcm_uframes_t period_frames = rate / 8;  /* ~62.5ms 周期 */
        snd_pcm_hw_params_set_period_size_near(handle, hw, &period_frames, NULL);
        snd_pcm_hw_params_set_buffer_size_near(handle, hw, &buf_frames);
    }

    ret = snd_pcm_hw_params(handle, hw);
    if (ret < 0) {
        fprintf(stderr, "mp3_player: 应用 HW 参数失败: %s\n",
                snd_strerror(ret));
        snd_pcm_close(handle);
        return -1;
    }

    /* ── SW_PARAMS ──
     *
     * start_threshold = buffer 半满才启动（同 mpg123），
     * 配合音频线程预加载，先积累足够数据再播放，防 XRUN。
     */
    {
        snd_pcm_sw_params_t *sw;
        snd_pcm_sw_params_alloca(&sw);
        ret = snd_pcm_sw_params_current(handle, sw);
        if (ret < 0) {
            fprintf(stderr, "mp3_player: 获取 SW 参数失败: %s\n",
                    snd_strerror(ret));
            snd_pcm_close(handle);
            return -1;
        }
        /* 获取实际 buffer 大小，用作 start_threshold */
        snd_pcm_uframes_t actual_buf_size;
        snd_pcm_hw_params_get_buffer_size(hw, &actual_buf_size);
        {
            snd_pcm_uframes_t thresh = actual_buf_size / 2;
            if (thresh < 1) thresh = 1;
            ret = snd_pcm_sw_params_set_start_threshold(handle, sw, thresh);
        }
        if (ret < 0) {
            fprintf(stderr, "mp3_player: 设置 start_threshold 失败: %s\n",
                    snd_strerror(ret));
            snd_pcm_close(handle);
            return -1;
        }
        ret = snd_pcm_sw_params_set_avail_min(handle, sw, 1);
        if (ret < 0) {
            fprintf(stderr, "mp3_player: 设置 avail_min 失败: %s\n",
                    snd_strerror(ret));
            snd_pcm_close(handle);
            return -1;
        }
        ret = snd_pcm_sw_params(handle, sw);
        if (ret < 0) {
            fprintf(stderr, "mp3_player: 应用 SW 参数失败: %s\n",
                    snd_strerror(ret));
            snd_pcm_close(handle);
            return -1;
        }
    }

    p->pcm = handle;
    p->alsa_rate = (int)rate;

    if (p->alsa_rate != p->mp3_rate) {
        printf("mp3_player: 音频设备已配置 — %dHz/%dbit/%dch"
               "  (源 %dHz，将重采样适配)\n",
               p->alsa_rate, PCM_BITS, p->mp3_channels, p->mp3_rate);
    } else {
        printf("mp3_player: 音频设备已配置 — %dHz/%dbit/%dch\n",
               p->alsa_rate, PCM_BITS, p->mp3_channels);
    }
    return 0;
}

/*
 * 封装 snd_pcm_writei，部分写入时自动重试直到写完。
 * EPIPE（XRUN）不自动恢复，返回 -EPIPE 供上层做预加载恢复。
 */
static long pcm_write(struct mp3_player *p,
                      const mp3d_sample_t *data, unsigned long frames)
{
    unsigned long remaining = frames;
    const mp3d_sample_t *ptr = data;

    while (remaining > 0) {
        snd_pcm_sframes_t ret = snd_pcm_writei(p->pcm, ptr, remaining);
        if (ret < 0) {
            /* EPIPE = underrun，返回供上层精确恢复（含预加载等待） */
            if (ret == -EPIPE)
                return -EPIPE;
            /* 其他错误：尝试 recover（如 -ESTRPIPE） */
            ret = snd_pcm_recover(p->pcm, (int)ret, 0);
            if (ret < 0) {
                fprintf(stderr, "mp3_player: PCM 写入错误 (unrecoverable): %s\n",
                        snd_strerror((int)ret));
                return (long)ret;
            }
            continue;
        }
        remaining -= (unsigned long)ret;
        ptr += ret * p->mp3_channels;
    }
    return (long)frames;
}

/* ── 解码线程 ──
 *
 * 从 file_pos 开始连续解码 MP3 帧，重采样后推入环形缓冲。
 * 解码完所有帧或遇到错误后设 EOS 退出。
 */

static void *decoder_thread_func(void *arg)
{
    struct mp3_player *p = (struct mp3_player *)arg;
    mp3d_sample_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];

    for (;;) {
        /* 检查退出标志 */
        if (p->quit)
            goto done;

        /* 填充文件读取缓冲 */
        while (p->file_pos + 4 >= p->file_len) {
            int ret = read_file_chunk(p);
            if (ret < 0) {
                fprintf(stderr, "mp3_player: 读取错误\n");
                p->quit = 1;
                goto done;
            }
            if (ret == 0)
                goto done;              /* EOF */
        }

        /* 解码一帧 */
        int samples = mp3dec_decode_frame(&p->dec,
                         p->file_buf + p->file_pos,
                         p->file_len - p->file_pos,
                         pcm_buf, &p->info);

        if (samples <= 0) {
            /* 无效帧或帧尾 */
            if (p->info.frame_bytes <= 0)
                break;
            p->file_pos += p->info.frame_bytes;
            continue;
        }

        p->file_pos += p->info.frame_bytes;

        /* 重采样 */
        int out_frames = resample_pcm_frac(pcm_buf, samples,
                              p->mp3_rate, p->alsa_rate, p->mp3_channels,
                              p->resample_buf, &p->resample_remainder);

        /* 推入环形缓冲（若满则阻塞等待） */
        ring_write(&p->ring, p->resample_buf, out_frames);
    }

done:
    /* 解码结束，标记 EOS 唤醒消费者 */
    ring_mark_eos(&p->ring);
    return NULL;
}

/* ── 音频线程 ──
 *
 * 带预加载状态机的 ALSA 写入循环（同 toyc 版，参考 mpg123 buffer_loop）：
 *
 *    PRELOADING ──┬── 缓冲 ≥ PRELOAD_THRESHOLD ──→ PLAYING
 *                 │                                    │
 *                 ◄── XRUN / 缓冲跌至低水位 ──────────┘
 */

static void *audio_thread_func(void *arg)
{
    struct mp3_player *p = (struct mp3_player *)arg;
    mp3d_sample_t play_buf[PCM_READ_BATCH * 2];
    int preloading = 1;
    int in_xrun = 0;

    for (;;) {
        if (p->quit)
            break;

        /* ══════════════════════════════════════════════════
         * 预加载：等待环形缓冲积累到安全水位后再写入 ALSA
         * ══════════════════════════════════════════════════ */
        if (preloading) {
            int threshold = in_xrun ? RECOVER_THRESHOLD : PRELOAD_THRESHOLD;
            int avail = ring_wait_avail(&p->ring, threshold);
            if (avail <= 0)     /* EOS */
                break;
            preloading = 0;
            in_xrun = 0;
        }

        /* ══════════════════════════════════════════════════
         * 从环形缓冲读取 PCM
         * ══════════════════════════════════════════════════ */
        int frames = ring_wait_read(&p->ring, play_buf, PCM_READ_BATCH);
        if (frames <= 0)        /* EOS */
            break;

        /* ══════════════════════════════════════════════════
         * 写入 ALSA 设备
         * ══════════════════════════════════════════════════ */
        long ret = pcm_write(p, play_buf, (unsigned long)frames);
        if (ret == -EPIPE) {
            /* XRUN：恢复设备后进入预加载，等待缓冲积累再播放 */
            snd_pcm_prepare(p->pcm);
            preloading = 1;
            in_xrun = 1;
            continue;
        }
        if (ret < 0) {
            fprintf(stderr, "mp3_player: PCM 写入失败 (errno=%ld)\n", -ret);
            p->quit = 1;
            break;
        }

        p->total_frames += frames;

        /* 预防性检查：环形缓冲接近排空时提前预加载 */
        if (ring_avail(&p->ring) < PCM_READ_BATCH) {
            preloading = 1;
        }
    }

    /* 等待硬件播放完缓冲中最后的数据 */
    snd_pcm_drain(p->pcm);
    return NULL;
}

/* ── 主逻辑 ── */

static int play_mp3(struct mp3_player *p)
{
    int ret;
    int rc = 1;

    /* ── 1. 读取并解码第一帧，获取音频格式 ── */

    ret = read_file_chunk(p);
    if (ret <= 0) {
        fprintf(stderr, "mp3_player: 文件为空或读错误\n");
        return 1;
    }

    mp3d_sample_t first_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples = mp3dec_decode_frame(&p->dec,
                     p->file_buf + p->file_pos,
                     p->file_len - p->file_pos,
                     first_pcm, &p->info);
    if (samples <= 0) {
        fprintf(stderr, "mp3_player: 不是有效的 MP3 文件\n");
        return 1;
    }
    p->file_pos += p->info.frame_bytes;
    p->mp3_rate = p->info.hz;
    p->mp3_channels = p->info.channels;

    printf("mp3_player: %d Hz, %d ch, %d kbps, Layer %d\n",
           p->info.hz, p->info.channels,
           p->info.bitrate_kbps, p->info.layer);

    /* ── 2. 打开音频设备 ── */

    ret = open_audio(p);
    if (ret < 0)
        return 1;

    /* ── 3. 初始化环形缓冲 ── */

    pthread_mutex_init(&p->ring.mutex, NULL);
    pthread_cond_init(&p->ring.cond, NULL);
    p->ring.ch = p->mp3_channels;

    /* ── 4. 重采样并推入第一帧 PCM ── */

    {
        int out_frames = resample_pcm_frac(first_pcm, samples,
                              p->mp3_rate, p->alsa_rate, p->mp3_channels,
                              p->resample_buf, &p->resample_remainder);
        ring_write(&p->ring, p->resample_buf, out_frames);
    }

    /* ── 5. 创建解码线程和音频线程 ── */

    ret = pthread_create(&p->dec_thread, NULL, decoder_thread_func, p);
    if (ret != 0) {
        fprintf(stderr, "mp3_player: 创建解码线程失败\n");
        p->quit = 1;
        snd_pcm_close(p->pcm);
        goto destroy_sync;
    }

    ret = pthread_create(&p->aud_thread, NULL, audio_thread_func, p);
    if (ret != 0) {
        fprintf(stderr, "mp3_player: 创建音频线程失败\n");
        p->quit = 1;
        /* 等解码线程退出 */
        pthread_join(p->dec_thread, NULL);
        snd_pcm_close(p->pcm);
        goto destroy_sync;
    }

    /* ── 6. 等待线程结束 ── */

    pthread_join(p->dec_thread, NULL);
    pthread_join(p->aud_thread, NULL);

    printf("mp3_player: 播放完成 — %d 帧\n", p->total_frames);
    rc = 0;

    /* ── 7. 关闭设备 ── */

    snd_pcm_close(p->pcm);

destroy_sync:
    pthread_mutex_destroy(&p->ring.mutex);
    pthread_cond_destroy(&p->ring.cond);
    return rc;
}

/* ── 入口 ── */

int main(int argc, char **argv)
{
    const char *file_path;
    struct mp3_player player;

    memset(&player, 0, sizeof(player));

    if (argc < 2) {
        fprintf(stderr, "用法: mp3_player_libc <file.mp3> [pcm_device]\n");
        fprintf(stderr, "  示例: ./mp3_player_libc .claude/GodKnows.mp3\n");
        fprintf(stderr, "        ./mp3_player_libc .claude/GodKnows.mp3 hw:0,0\n");
        return 1;
    }

    file_path = argv[1];
    player.dev_path = (argc > 2) ? argv[2] : NULL;

    player.mp3_file = fopen(file_path, "rb");
    if (!player.mp3_file) {
        fprintf(stderr, "mp3_player: 无法打开 '%s'\n", file_path);
        return 1;
    }

    player.file_cap = READ_BLOCK_SZ;
    player.file_buf = (unsigned char *)malloc((size_t)player.file_cap);
    if (!player.file_buf) {
        fprintf(stderr, "mp3_player: 内存不足\n");
        fclose(player.mp3_file);
        return 1;
    }

    mp3dec_init(&player.dec);

    int ret = play_mp3(&player);

    free(player.file_buf);
    fclose(player.mp3_file);

    return ret;
}
