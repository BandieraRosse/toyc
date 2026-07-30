/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * mp3_player — MP3 文件解码播放器
 *
 * 双线程架构，通过环形缓冲 + futex 同步，彻底解决单线程阻塞导致的 PCM 撕裂：
 *
 *   主线程   →   解码线程（读取文件 → MP3 解码 → 重采样）
 *                        ↓ 环形缓冲（futex 同步）
 *              音频线程（从环形缓冲读取 → ALSA PCM 写入）
 *
 * 基于 minimp3 (public domain) 解码，ALSA PCM 直写输出。
 * 不依赖 libc/libasound，仅通过 syscall 操作 ALSA 内核接口。
 *
 * 用法：
 *   mp3_player <file.mp3>             # 播放指定文件
 *   mp3_player <file.mp3> /dev/snd/pcmC0D0p  # 指定设备
 *
 * 系统调用：openat, read, lseek, close, ioctl, write, mmap, munmap,
 *           futex, clone
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "errno.h"
#include "pthread.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

/* ── 配置 ── */

/* PCM 输出格式固定为 S16_LE（与 minimp3 默认输出一致） */
#define PCM_BITS        16

/* 读取 MP3 文件的块大小（流式读取，避免 mmap 整个大文件） */
#define READ_BLOCK_SZ   (256 * 1024)   /* 256 KB 块 */

/* 重采样输出缓冲区：最大一帧 1152 帧 × 2ch，48000/44100 最大放大比 ~1.089 */
#define RESAMPLE_BUF_SZ  4096

/* 音频线程每次从环形缓冲读取的最大帧数（outburst 限制，同 mpg123） */
#define PCM_READ_BATCH   8192

/* 预加载阈值：缓冲半满才启动播放，避免首帧即 XRUN（同 mpg123 buffer_loop） */
#define PRELOAD_THRESHOLD  (RING_BUF_SIZE / 2)

/* XRUN 后恢复预加载阈值：缓冲 1/4 即可恢复，比首次启动更快 */
#define RECOVER_THRESHOLD  (RING_BUF_SIZE / 4)

/* ── 环形缓冲 ──
 *
 * 环形缓冲大小（帧数），必须是 2 的幂。
 * 16384 帧 ≈ 341ms @48kHz，解码线程超前播放线程约 0.3 秒。
 * 解码快于播放时，缓冲填满后解码线程通过 futex 等待消费；
 * 解码慢于播放时，缓冲排空后音频线程通过 futex 等待解码。
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

    while (frames > 0) {
        /* 等待空间 */
        while (ring_free(r) <= 0) {
            unsigned int cur = r->rpos;
            __futex((unsigned int *)&r->rpos, 0 /* FUTEX_WAIT */,
                    cur, NULL, NULL, 0);
        }

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

        /* 确保数据写入对消费者可见后再更新 wpos */
        __sync_synchronize();

        r->wpos += (unsigned int)batch;
        frames -= batch;
        data  += batch * ch;

        /* 唤醒可能在 wpos 上等待的消费者 */
        __futex((unsigned int *)&r->wpos, 1 /* FUTEX_WAKE */,
                (unsigned int)-1, NULL, NULL, 0);
    }
}

/* 消费者：等待并读取最多 max_frames 帧，返回实际读取帧数。0 = EOS */
static int ring_wait_read(struct ring_buffer *r,
                          mp3d_sample_t *dst, int max_frames)
{
    int ch = r->ch;

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

            /* 确保数据读取后再更新 rpos */
            __sync_synchronize();

            r->rpos += (unsigned int)batch;

            /* 唤醒可能在 rpos 上等待的生产者 */
            __futex((unsigned int *)&r->rpos, 1 /* FUTEX_WAKE */,
                    (unsigned int)-1, NULL, NULL, 0);

            return batch;
        }

        /* 无数据可用，检查 EOS。但必须先确认 wpos 没有再推进—— */
        /* 生产者可能在 'avail = 0' 和 'eos 检查' 之间写入数据+设 eos */
        __sync_synchronize();
        if (r->eos) {
            /* 二次确认：生产者写入后设 eos 的窗口极小，但必须堵住 */
            __sync_synchronize();
            avail = ring_avail(r);
            if (avail > 0) {
                /* 数据已到，下轮读取 */
                continue;
            }
            return 0;   /* 真正 EOS */
        }

        /* 无数据且无 EOS，在 wpos 上等待生产者唤醒 */
        {
            unsigned int cur = r->wpos;
            __futex((unsigned int *)&r->wpos, 0 /* FUTEX_WAIT */,
                    cur, NULL, NULL, 0);
        }
        /* 醒来后回到循环顶部，检查 avail 或 eos */
    }
}

/* 等待环形缓冲积累至少 min_frames 帧（不消费），
 * 返回积累后的实际可用帧数。0 = EOS。
 * 用于音频线程预加载：等待解码线程填满半缓冲再启动播放。 */
static int ring_wait_avail(struct ring_buffer *r, int min_frames)
{
    for (;;) {
        int avail = ring_avail(r);
        if (avail >= min_frames)
            return avail;

        /* 检查 EOS */
        __sync_synchronize();
        if (r->eos) {
            __sync_synchronize();
            avail = ring_avail(r);
            if (avail > 0)
                return avail;
            return 0;   /* 真正 EOS */
        }

        /* 在 wpos 上等待解码线程推进 */
        {
            unsigned int cur = r->wpos;
            __futex((unsigned int *)&r->wpos, 0 /* FUTEX_WAIT */,
                    cur, NULL, NULL, 0);
        }
    }
}

/* 生产者标记结束 */
static void ring_mark_eos(struct ring_buffer *r)
{
    __sync_synchronize();
    r->eos = 1;
    /* 唤醒可能正在 wpos 上睡眠的消费者 */
    __futex((unsigned int *)&r->wpos, 1 /* FUTEX_WAKE */,
            (unsigned int)-1, NULL, NULL, 0);
}

/* ── 状态 ── */

struct mp3_player {
    /* ── 输入 ── */
    const char *dev_path;
    int mp3_fd;

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
    struct tlibc_pcm pcm;
    int alsa_rate;

    /* ── 重采样（解码线程独占）─ */
    mp3d_sample_t resample_buf[RESAMPLE_BUF_SZ];
    double resample_remainder;

    /* ── 环形缓冲（线程共享，futex 同步）─ */
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
        memmove(p->file_buf, p->file_buf + p->file_pos, (unsigned long)remain);
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
    long n = __read(p->mp3_fd, dst, (unsigned long)space);
    if (n < 0)  return -1;
    if (n == 0) return  0;
    p->file_len += (int)n;
    return (int)n;
}

/* ── 线性插值采样率转换 ── */

static int resample_pcm(const mp3d_sample_t *in, int in_frames,
                        int in_rate, int out_rate, int ch,
                        mp3d_sample_t *out)
{
    if (in_rate == out_rate) {
        int total = in_frames * ch;
        for (int i = 0; i < total; i++)
            out[i] = in[i];
        return in_frames;
    }

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

static int open_audio(struct mp3_player *p)
{
    int ret = tlibc_pcm_open(&p->pcm, p->dev_path,
                             (unsigned int)p->mp3_channels,
                             (unsigned int)p->mp3_rate, PCM_BITS);
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

    p->alsa_rate = (int)p->pcm.rate;

    if (p->alsa_rate != p->mp3_rate) {
        __printf("mp3_player: 音频设备已配置 — %dHz/%dbit/%dch"
                 "  (源 %dHz，将重采样适配)\n",
                 p->alsa_rate, PCM_BITS, p->pcm.channels, p->mp3_rate);
    } else {
        __printf("mp3_player: 音频设备已配置 — %dHz/%dbit/%dch, 缓冲区 %lu 帧\n",
                 p->alsa_rate, PCM_BITS, p->pcm.channels, p->pcm.buffer_size);
    }
    return 0;
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
                __printf("mp3_player: 读取错误\n");
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
 * 带预加载状态机的 ALSA 写入循环（参考 mpg123 buffer_loop）：
 *
 *    PRELOADING ──┬── 缓冲 ≥ PRELOAD_THRESHOLD ──→ PLAYING
 *                 │                                    │
 *                 ◄── XRUN / 缓冲跌至低水位 ──────────┘
 *
 * 预加载确保 ALSA 内核缓冲填满到安全水位才启动播放，
 * 避免因解码线程调度延迟导致的 XRUN。
 */

static void *audio_thread_func(void *arg)
{
    struct mp3_player *p = (struct mp3_player *)arg;
    mp3d_sample_t play_buf[PCM_READ_BATCH * 2];
    int preloading = 1;      /* 预加载状态：等待缓冲积累 */
    int in_xrun = 0;         /* 刚发生过 XRUN，需降级恢复门限 */

    for (;;) {
        if (p->quit)
            break;

        /* ══════════════════════════════════════════════════
         * 预加载：等待环形缓冲积累到安全水位后再写入 ALSA
         * 同 mpg123 buffer_loop 的 preloading 机制
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
        long ret = tlibc_pcm_write(&p->pcm, play_buf,
                                    (unsigned long)frames);
        if (ret == -EPIPE) {
            /* XRUN：恢复设备 + 进入预加载状态。
             * mpg123 的做法：pause → 等缓冲积累 → resume。
             * 这里恢复设备后不急于重写，先积累缓冲再播放。 */
            __ioctl(p->pcm.fd, SNDRV_PCM_IOCTL_PREPARE, 0);
            preloading = 1;
            in_xrun = 1;
            continue;
        }
        if (ret < 0) {
            __printf("mp3_player: PCM 写入失败 (errno=%ld)\n", -ret);
            p->quit = 1;
            break;
        }

        p->total_frames += frames;

        /* 预防性检查：如果环形缓冲已接近排空，提前进入预加载 */
        if (ring_avail(&p->ring) < PCM_READ_BATCH) {
            preloading = 1;
        }
    }

    /* 等待硬件播放完缓冲中最后的数据 */
    tlibc_pcm_drain(&p->pcm);
    return NULL;
}

/* ── 主逻辑 ── */

static int play_mp3(struct mp3_player *p)
{
    int ret;

    /* ── 1. 读取并跳过 Xing/Info 帧、预热解码器 ──
     *
     * CBR 和部分 VBR MP3 文件的首帧是 Xing/Info 元数据帧，
     * 不含实际音频。minimp3 解码该帧后，其零值比特库会污染
     * 后续帧，导致全声道无声。
     *
     * 处理策略：
     *   1. 检测帧头偏移 36 处的 "Info"/"Xing" 签名，跳过该帧
     *   2. 重置解码器避免比特库污染
     *   3. 重置后解码器需预热：跳过 main_data_begin>0 导致的全零帧
     *   4. 取首个非零 PCM 帧作为实际音频起始
     */

    ret = read_file_chunk(p);
    if (ret <= 0) {
        __printf("mp3_player: 文件为空或读错误\n");
        return 1;
    }

    mp3d_sample_t first_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int decoder_reset = 0;
    int found_audio = 0;
    int first_samples = 0;

    while (!found_audio) {
        /* 确保有足够数据解码一帧 */
        while (p->file_pos + 4 >= p->file_len) {
            int r = read_file_chunk(p);
            if (r < 0) {
                __printf("mp3_player: 读取错误\n");
                return 1;
            }
            if (r == 0) {
                __printf("mp3_player: 未找到有效的 MP3 音频帧\n");
                return 1;
            }
        }

        int samples = mp3dec_decode_frame(&p->dec,
                         p->file_buf + p->file_pos,
                         p->file_len - p->file_pos,
                         first_pcm, &p->info);
        if (samples <= 0) {
            if (p->info.frame_bytes <= 0)
                break;
            p->file_pos += p->info.frame_bytes;
            continue;
        }

        /* 检测 Xing/Info 帧签名（位于帧头偏移 36 或 21 处） */
        {
            int side_sz = (p->info.channels == 1) ? 17 : 32;
            int xing_off = p->file_pos + p->info.frame_offset
                           + 4 + side_sz;
            if (xing_off + 4 <= p->file_len) {
                const unsigned char *d = p->file_buf + xing_off;
                if ((d[0] == 'X' && d[1] == 'i' && d[2] == 'n' && d[3] == 'g') ||
                    (d[0] == 'I' && d[1] == 'n' && d[2] == 'f' && d[3] == 'o')) {
                    /* 跳过 Xing/Info 帧，重置解码器防止比特库污染 */
                    mp3dec_init(&p->dec);
                    p->file_pos += p->info.frame_bytes;
                    decoder_reset = 1;
                    continue;
                }
            }
        }

        p->file_pos += p->info.frame_bytes;

        /* 跳过解码器预热帧（重置后比特库为空，解码为全零） */
        if (decoder_reset) {
            int nz = 0;
            int total = samples * p->info.channels;
            for (int i = 0; i < total; i++) {
                if (first_pcm[i] != 0) { nz = 1; break; }
            }
            if (!nz)
                continue;
        }

        /* 找到第一帧真实音频 */
        p->mp3_rate = p->info.hz;
        p->mp3_channels = p->info.channels;
        first_samples = samples;
        found_audio = 1;
    }

    __printf("mp3_player: %d Hz, %d ch, %d kbps, Layer %d\n",
             p->info.hz, p->info.channels,
             p->info.bitrate_kbps, p->info.layer);

    /* ── 2. 打开音频设备 ── */

    ret = open_audio(p);
    if (ret < 0)
        return 1;

    /* ── 3. 初始化环形缓冲 ── */

    p->ring.ch = p->mp3_channels;

    /* ── 4. 重采样并推入第一帧 PCM ── */

    {
        int out_frames = resample_pcm_frac(first_pcm, first_samples,
                              p->mp3_rate, p->alsa_rate, p->mp3_channels,
                              p->resample_buf, &p->resample_remainder);
        ring_write(&p->ring, p->resample_buf, out_frames);
    }

    /* ── 5. 创建解码线程和音频线程 ── */

    ret = pthread_create(&p->dec_thread, NULL, decoder_thread_func, p);
    if (ret != 0) {
        __printf("mp3_player: 创建解码线程失败\n");
        p->quit = 1;
        tlibc_pcm_close(&p->pcm);
        return 1;
    }

    ret = pthread_create(&p->aud_thread, NULL, audio_thread_func, p);
    if (ret != 0) {
        __printf("mp3_player: 创建音频线程失败\n");
        p->quit = 1;
        /* 等解码线程退出 */
        pthread_join(p->dec_thread, NULL);
        tlibc_pcm_close(&p->pcm);
        return 1;
    }

    /* ── 6. 等待线程结束 ── */

    pthread_join(p->dec_thread, NULL);
    pthread_join(p->aud_thread, NULL);

    __printf("mp3_player: 播放完成 — %d 帧\n", p->total_frames);

    /* ── 7. 关闭设备 ── */

    tlibc_pcm_close(&p->pcm);
    return 0;
}

/* ── 入口 ── */

/*
 * try_pasuspender_fallback — 通过 pasuspender 重新执行自身
 *
 * 当自动选择的最佳音频设备被 PulseAudio 占用时，
 * 通过 execve /usr/bin/pasuspender 挂起 PulseAudio 后重新运行。
 *
 * 成功时不返回（当前进程被替换），失败时返回（pasuspender 不可用）。
 */
static void try_pasuspender_fallback(int argc, char **argv)
{
    /* 递归防护：已在 pasuspender 下则不再重试 */
    if (get_env_var(global_envp, "_MP3_PASUSPENDER") != NULL)
        return;

    /* 检查 /usr/bin/pasuspender 是否存在 */
    int fd = __openat(AT_FDCWD, "/usr/bin/pasuspender",
                      O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return;
    __close(fd);

    __printf("mp3_player: 设备被 PulseAudio 占用，通过 pasuspender 重试...\n");

    /* ── 构建新 argv ── */
    int new_argc = argc + 2;
    char **new_argv = tlibc_malloc((unsigned long)(new_argc + 1)
                                   * sizeof(char *));
    if (!new_argv) return;

    new_argv[0] = "/usr/bin/pasuspender";
    new_argv[1] = "--";
    new_argv[2] = argv[0];
    for (int i = 1; i < argc; i++)
        new_argv[i + 2] = argv[i];
    new_argv[new_argc] = NULL;

    /* ── 构建新 envp（追加 _MP3_PASUSPENDER=1）── */
    int env_count = tlibc_envp_count(global_envp);
    char **new_envp = tlibc_malloc((unsigned long)(env_count + 2)
                                   * sizeof(char *));
    if (!new_envp) {
        tlibc_free(new_argv);
        return;
    }

    for (int i = 0; i < env_count; i++)
        new_envp[i] = global_envp[i];

    {
        static char pa_flag[] = "_MP3_PASUSPENDER=1";
        new_envp[env_count] = pa_flag;
    }
    new_envp[env_count + 1] = NULL;

    /* ── execve：成功则当前进程完全替换 ── */
    {
        int ret = execve("/usr/bin/pasuspender", new_argv, new_envp);
        PRINT_COLOR(RED_COLOR_PRINT,
            "mp3_player: pasuspender 启动失败 (errno=%d)\n", -ret);
    }
}

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

    /* ── PulseAudio 冲突提前检测 ──
     *
     * 在自动模式下，如果 PulseAudio 占用最佳模拟设备，
     * 在打开音频前主动尝试 pasuspender 回退，
     * 避免已打开次优设备（如 HDMI）后再切换的麻烦。
     */
    if (!player.dev_path) {
        struct tlibc_audio_dev devs[16];
        int ndev = tlibc_audio_scan(devs, 16, 0, 0);
        if (ndev > 1 && devs[0].busy && tlibc_check_pulseaudio()) {
            /* 最佳设备被占且 PulseAudio 在运行 → 尝试 pasuspender */
            try_pasuspender_fallback(argc, argv);
            /* 执行到这里说明 pasuspender 失败，继续使用 fallback */
            __printf("mp3_player: pasuspender 不可用，"
                     "将使用次优设备 %s (评分 %d)\n",
                     devs[1].name, devs[1].score);
        }
    }

    player.mp3_fd = __openat(AT_FDCWD, file_path, O_RDONLY, 0);
    if (player.mp3_fd < 0) {
        __printf("mp3_player: 无法打开 '%s' (errno=%d)\n",
                 file_path, -player.mp3_fd);
        return 1;
    }

    player.file_cap = READ_BLOCK_SZ;
    player.file_buf = (unsigned char *)tlibc_malloc(
                         (unsigned long)player.file_cap);
    if (!player.file_buf) {
        __printf("mp3_player: 内存不足\n");
        __close(player.mp3_fd);
        return 1;
    }

    mp3dec_init(&player.dec);

    int ret = play_mp3(&player);

    if (player.file_buf)
        tlibc_free(player.file_buf);
    if (player.mp3_fd >= 0)
        __close(player.mp3_fd);

    return ret;
}
