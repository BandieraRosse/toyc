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
#define PCM_FORMAT      SNDRV_PCM_FORMAT_S16_LE

/* 读取 MP3 文件的块大小（流式读取，避免 mmap 整个大文件） */
#define READ_BLOCK_SZ   (256 * 1024)   /* 256 KB 块 */

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
    int total_frames;           /* 已播放帧数（用于日志） */
};

/* ── 文件读取 ── */

static int read_file_chunk(struct mp3_player *p)
{
    unsigned char *dst;
    int space;

    if (p->buf_pos > 0 && p->buf_pos < p->buf_len) {
        /* 移动未消费数据到头部 */
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
        return 0;  /* 缓冲区满，无法读取更多 */

    long n = __read(p->mp3_fd, dst, (unsigned long)space);
    if (n < 0)
        return -1;  /* 读错误 */
    if (n == 0)
        return 0;   /* EOF */

    p->buf_len += (int)n;
    return (int)n;
}

/* ── 音频输出 ── */

static int open_audio(struct mp3_player *p)
{
    int ret = tlibc_pcm_open(&p->pcm, p->dev_path,
                             (unsigned int)p->info.channels,
                             (unsigned int)p->info.hz, PCM_BITS);
    if (ret < 0) {
        PRINT_COLOR(RED_COLOR_PRINT,
            "mp3_player: 无法打开 PCM 设备 (errno=%d)\n", -ret);
        return ret;
    }

    ret = tlibc_pcm_configure(&p->pcm);
    if (ret < 0) {
        PRINT_COLOR(RED_COLOR_PRINT,
            "mp3_player: PCM 配置失败 (errno=%d)\n", -ret);
        tlibc_pcm_close(&p->pcm);
        return ret;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT,
        "mp3_player: 音频设备已配置 — %dHz/%dbit/%dch, 缓冲区 %lu 帧\n",
        p->pcm.rate, PCM_BITS, p->pcm.channels, p->pcm.buffer_size);
    return 0;
}

static int write_pcm(struct mp3_player *p, mp3d_sample_t *samples,
                      int samples_per_ch)
{
    /* samples_per_ch 是每声道采样数（mp3dec_decode_frame 返回值）
     * 使用 tlibc_pcm_write 而非 write_all：
     *   write_all 内部会 drain，导致设备停止，后续写入失败 (EBADFD)。
     *   我们用 write + 最后统一 drain。 */
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

    PRINT_COLOR(CYAN_COLOR_PRINT,
        "mp3_player: %s — %d Hz, %d ch, %d kbps, Layer %d\n",
        "MPEG audio",
        p->info.hz, p->info.channels,
        p->info.bitrate_kbps, p->info.layer);

    /* 打开音频设备 */
    ret = open_audio(p);
    if (ret < 0)
        return 1;

    /* 写入第一帧 PCM */
    ret = write_pcm(p, pcm_buf, samples);
    if (ret < 0) {
        PRINT_COLOR(RED_COLOR_PRINT,
            "mp3_player: PCM 写入失败 (errno=%d)\n", -ret);
        tlibc_pcm_close(&p->pcm);
        return 1;
    }

    /* 解码并播放后续帧 */
    for (;;) {
        /* 确保缓冲区有足够数据 */
        while (p->buf_pos + 4 >= p->buf_len) {
            ret = read_file_chunk(p);
            if (ret < 0) {
                PRINT_COLOR(RED_COLOR_PRINT,
                    "mp3_player: 读取错误\n");
                tlibc_pcm_drain(&p->pcm);
                tlibc_pcm_close(&p->pcm);
                return 1;
            }
            if (ret == 0) {
                /* EOF — 正常结束 */
                goto done;
            }
        }

        /* 解码一帧 */
        samples = mp3dec_decode_frame(&p->dec,
                     p->buf + p->buf_pos,
                     p->buf_len - p->buf_pos,
                     pcm_buf, &p->info);

        if (samples > 0) {
            /* 检查格式是否变化（对比原始 MP3 格式，而非 ALSA 协商后的值） */
            if (p->mp3_rate != p->info.hz
                || p->mp3_channels != p->info.channels) {
                PRINT_COLOR(YELLOW_COLOR_PRINT,
                    "mp3_player: 流格式变化 %dch/%dHz → %dch/%dHz，重新配置设备\n",
                    p->mp3_channels, p->mp3_rate,
                    p->info.channels, p->info.hz);
                p->mp3_rate = p->info.hz;
                p->mp3_channels = p->info.channels;
                tlibc_pcm_drain(&p->pcm);
                tlibc_pcm_close(&p->pcm);
                /* 重新打开音频设备 */
                p->pcm.channels = 0;
                ret = open_audio(p);
                if (ret < 0)
                    return 1;
            }

            ret = write_pcm(p, pcm_buf, samples);
            if (ret < 0) {
                PRINT_COLOR(RED_COLOR_PRINT,
                    "mp3_player: PCM 写入失败 (errno=%d)\n", -ret);
                tlibc_pcm_drain(&p->pcm);
                tlibc_pcm_close(&p->pcm);
                return 1;
            }
        }

        /* 推进偏移量 */
        if (p->info.frame_bytes <= 0) {
            /* 解码器无法定位下一帧 */
            break;
        }
        p->buf_pos += p->info.frame_bytes;
    }

done:
    /* 等待播放完成 */
    PRINT_COLOR(GREEN_COLOR_PRINT,
        "mp3_player: 播放完成 — %d 帧\n", p->total_frames);
    tlibc_pcm_drain(&p->pcm);
    tlibc_pcm_close(&p->pcm);
    return 0;
}

/* ── 入口 ── */

int main(int argc, char **argv)
{
    const char *file_path;
    struct mp3_player player;

    /* 清零状态 */
    memset(&player, 0, sizeof(player));

    if (argc < 2) {
        __printf("用法: mp3_player <file.mp3> [pcm_device]\n");
        __printf("  示例: mp3_player song.mp3\n");
        __printf("        mp3_player song.mp3 /dev/snd/pcmC0D0p\n");
        return 1;
    }

    file_path = argv[1];
    player.dev_path = (argc > 2) ? argv[2] : NULL;

    /* 打开 MP3 文件 */
    player.mp3_fd = __openat(AT_FDCWD, file_path, O_RDONLY, 0);
    if (player.mp3_fd < 0) {
        __printf("mp3_player: 无法打开 '%s' (errno=%d)\n",
                 file_path, -player.mp3_fd);
        return 1;
    }

    /* 分配缓冲区 */
    player.buf_cap = READ_BLOCK_SZ;
    player.buf = (unsigned char *)tlibc_malloc((unsigned long)player.buf_cap);
    if (!player.buf) {
        __printf("mp3_player: 内存不足\n");
        __close(player.mp3_fd);
        return 1;
    }

    /* 初始化解码器 */
    mp3dec_init(&player.dec);

    /* 播放 */
    int ret = play_mp3(&player);

    /* 清理 */
    if (player.buf)
        tlibc_free(player.buf);
    if (player.mp3_fd >= 0)
        __close(player.mp3_fd);

    return ret;
}
