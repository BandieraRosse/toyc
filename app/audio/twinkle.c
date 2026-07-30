/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * twinkle — ALSA PCM 播放"小星星"旋律，持续 20 秒以上
 *
 * 机制：
 *   相位累加生成正弦波，逐音符写入 ALSA PCM 设备。
 *   旋律为 C 大调"小星星"（Twinkle Twinkle Little Star），
 *   完整一遍约 12.6s，循环 2 遍约 25.2s（＞20s）。
 *
 *   自引用 fallback（同 beep.c）：若 PulseAudio 占用 PCM 设备，
 *   自动通过 execve 调用 pasuspender 挂起 PA 后重新执行自身。
 *   通过 _BEEP_PASUSPENDER 环境变量防止递归循环。
 *
 * 系统调用：openat, ioctl, write, close, execve
 *
 * 用法：
 *   twinkle              # 播放旋律（默认设备）
 *   twinkle /dev/snd/pcmC0D0p  # 指定设备节点
 *
 * 索引：
 *   main              开设备 → 逐音符生成波形 → 写入 → 循环 → 关闭
 *     play_note        生成单频正弦波并写入 PCM
 *     gen_sine         相位累加正弦波填充
 *     try_pasuspender_fallback  EBUSY → execve pasuspender 重新执行
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "math.h"
#include "string.h"
#include "errno.h"

#define CHANNELS      2
#define BITS          16

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ═══════════════════════════════════════════════════════════════════
 *  小星星旋律数据（C 大调）
 * ═══════════════════════════════════════════════════════════════════ */

/* 音符频率 */
#define C4 261.63
#define D4 293.66
#define E4 329.63
#define F4 349.23
#define G4 392.00
#define A4 440.00
#define  B4 493.88  /* 没用上，但保持完整 */

/* 节拍时长（秒）— 约 63 BPM */
#define BEAT_SEC 0.30

/* 单遍旋律：struct note 数组
 *
 * 简谱：
 *   1 1 5 5 6 6 5 - | 4 4 3 3 2 2 1 -
 *   5 5 4 4 3 3 2 - | 5 5 4 4 3 3 2 -
 *   1 1 5 5 6 6 5 - | 4 4 3 3 2 2 1 - ||
 *
 * 共 6 个乐句 × 7 个音符 = 42 音符
 */
struct note {
    double freq;
    int    beats;   /* 节拍数：1 = 四分音符, 2 = 二分音符 */
};

static const struct note melody[] = {
    /* 乐句 1：1 1 5 5 6 6 5 - */
    {C4, 1}, {C4, 1}, {G4, 1}, {G4, 1}, {A4, 1}, {A4, 1}, {G4, 2},
    /* 乐句 2：4 4 3 3 2 2 1 - */
    {F4, 1}, {F4, 1}, {E4, 1}, {E4, 1}, {D4, 1}, {D4, 1}, {C4, 2},
    /* 乐句 3：5 5 4 4 3 3 2 - */
    {G4, 1}, {G4, 1}, {F4, 1}, {F4, 1}, {E4, 1}, {E4, 1}, {D4, 2},
    /* 乐句 4：5 5 4 4 3 3 2 - */
    {G4, 1}, {G4, 1}, {F4, 1}, {F4, 1}, {E4, 1}, {E4, 1}, {D4, 2},
    /* 乐句 5：1 1 5 5 6 6 5 - */
    {C4, 1}, {C4, 1}, {G4, 1}, {G4, 1}, {A4, 1}, {A4, 1}, {G4, 2},
    /* 乐句 6：4 4 3 3 2 2 1 - */
    {F4, 1}, {F4, 1}, {E4, 1}, {E4, 1}, {D4, 1}, {D4, 1}, {C4, 2},
};
#define MELODY_LEN  (sizeof(melody) / sizeof(melody[0]))

/* ── 辅助函数声明 ── */
static void gen_sine(short *buf, double freq, unsigned int rate,
                     unsigned long frames);
static int  play_note(struct tlibc_pcm *pcm, double freq,
                      unsigned int rate, double duration_sec);
static void try_pasuspender_fallback(int argc, char **argv);
static int  is_pulseaudio_active(void);
static void print_error(const char *msg, int err);

/* ═══════════════════════════════════════════════════════════════════
 *  波形生成
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * gen_sine — 填充 16-bit 立体声正弦波缓冲区（复调消隐平滑起落）
 *
 * 起落渐变：前 5ms 淡入 + 后 5ms 淡出，消除"咔嗒"声。
 */
static void gen_sine(short *buf, double freq, unsigned int rate,
                     unsigned long frames)
{
    double phase = 0.0;
    double step  = 2.0 * M_PI * freq / rate;
    unsigned long fade_len = (unsigned long)(rate * 0.005); /* 5ms */
    if (fade_len > frames / 2)
        fade_len = frames / 4;
    unsigned long i;

    for (i = 0; i < frames; i++) {
        double envelope = 1.0;
        if (i < fade_len)
            envelope = (double)i / (double)fade_len;
        else if (i > frames - fade_len)
            envelope = (double)(frames - 1 - i) / (double)fade_len;

        short sample = (short)(sin(phase) * 30000.0 * envelope);
        buf[i * 2]     = sample;  /* 左声道 */
        buf[i * 2 + 1] = sample;  /* 右声道 */
        phase += step;
        if (phase >= 2.0 * M_PI)
            phase -= 2.0 * M_PI;
    }
}

/*
 * gen_silence — 填充静音（零值）
 */
static void gen_silence(short *buf, unsigned long frames)
{
    unsigned long i;
    for (i = 0; i < frames * 2; i++)
        buf[i] = 0;
}

/*
 * pcm_write_frames — 写入 PCM 数据，含 XRUN 恢复
 *
 * 与 tlibc_pcm_write_all 不同之处在于：
 *   不调用 drain，使设备保持在 RUNNING 状态，
 *   便于后续继续写入（多个音符连续播放）。
 *
 * 返回 0 成功，负值 errno。
 */
static int pcm_write_frames(struct tlibc_pcm *pcm, const short *buf,
                            unsigned long frames)
{
    long ret = tlibc_pcm_write(pcm, buf, frames);
    if (ret == -EPIPE) {
        /* XRUN：恢复设备后重试一次 */
        __ioctl(pcm->fd, SNDRV_PCM_IOCTL_PREPARE, 0);
        ret = tlibc_pcm_write(pcm, buf, frames);
    }
    if (ret < 0)
        return (int)ret;
    if ((unsigned long)ret < frames)
        return -EIO;
    return 0;
}

/*
 * play_note — 生成单频波形 → 写入 PCM 设备
 *
 * 使用 pcm_write_frames 而非 tlibc_pcm_write_all（后者含 drain，
 * 会导致多音符连续播放时 EBADFD）。
 *
 * 在音符之间插入 20ms 无声间隔，使旋律有呼吸感。
 * 返回 0 成功，负值 errno。
 */
static int play_note(struct tlibc_pcm *pcm, double freq,
                     unsigned int rate, double duration_sec)
{
    unsigned long note_frames = (unsigned long)(rate * duration_sec);
    unsigned long gap_frames  = (unsigned long)(rate * 0.020); /* 20ms 间隔 */
    short *buf;
    int ret;

    /* ── 音符 ── */
    buf = (short *)tlibc_malloc(note_frames * pcm->frame_size);
    if (!buf)
        return -ENOMEM;

    gen_sine(buf, freq, rate, note_frames);

    ret = pcm_write_frames(pcm, buf, note_frames);
    tlibc_free(buf);
    if (ret < 0)
        return ret;

    /* ── 间隔（静音）── */
    buf = (short *)tlibc_malloc(gap_frames * pcm->frame_size);
    if (!buf)
        return -ENOMEM;

    gen_silence(buf, gap_frames);

    ret = pcm_write_frames(pcm, buf, gap_frames);
    tlibc_free(buf);
    if (ret < 0)
        return ret;

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  PulseAudio fallback（从 beep.c 移植）
 * ═══════════════════════════════════════════════════════════════════ */

static int is_pulseaudio_active(void)
{
    char path[64];
    int uid = (int)getuid();
    int len = snprintf(path, sizeof(path),
                        "/run/user/%d/pulse/native", uid);
    if (len < 0 || (unsigned long)len >= sizeof(path))
        return 0;

    int fd = __openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    __close(fd);
    return 1;
}

static void try_pasuspender_fallback(int argc, char **argv)
{
    /* 递归防护：已在 pasuspender 下则不再重试 */
    char *flag = get_env_var(global_envp, "_BEEP_PASUSPENDER");
    if (flag && flag[0] == '1')
        return;

    /* 检查 /usr/bin/pasuspender 是否存在 */
    int fd = __openat(AT_FDCWD, "/usr/bin/pasuspender",
                      O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return;
    __close(fd);

    PRINT_COLOR(YELLOW_COLOR_PRINT,
                "twinkle: 设备被 PulseAudio 占用，通过 pasuspender 重试...\n");

    /* ── 构建新 argv ── */
    int new_argc = argc + 2;  /* + "pasuspender" + "--" */
    char **new_argv = tlibc_malloc((unsigned long)(new_argc + 1)
                                   * sizeof(char *));
    if (!new_argv) return;

    new_argv[0] = "/usr/bin/pasuspender";
    new_argv[1] = "--";
    new_argv[2] = argv[0];
    for (int i = 1; i < argc; i++)
        new_argv[i + 2] = argv[i];
    new_argv[new_argc] = NULL;

    /* ── 构建新 envp（追加 _BEEP_PASUSPENDER=1）── */
    int env_count = tlibc_envp_count(global_envp);
    char **new_envp = tlibc_malloc((unsigned long)(env_count + 2)
                                   * sizeof(char *));
    if (!new_envp) {
        tlibc_free(new_argv);
        return;
    }

    for (int i = 0; i < env_count; i++)
        new_envp[i] = global_envp[i];

    /* 递归防护标记 */
    {
        static char beep_flag[] = "_BEEP_PASUSPENDER=1";
        new_envp[env_count] = beep_flag;
    }
    new_envp[env_count + 1] = NULL;

    /* ── execve：成功则当前进程完全替换 ── */
    {
        int ret = execve("/usr/bin/pasuspender", new_argv, new_envp);

        /* 执行到这里说明 execve 失败，清理后返回 */
        PRINT_COLOR(RED_COLOR_PRINT,
                    "twinkle: pasuspender 启动失败 (errno=%d)\n", -ret);
    }
    tlibc_free(new_argv);
    tlibc_free(new_envp);
}

static void print_error(const char *msg, int err)
{
    char buf[128];
    strcpy(buf, "twinkle: ");
    strcat(buf, msg);
    PRINT_COLOR(RED_COLOR_PRINT, "%s (errno=%d)\n", buf, -err);
}

/* ═══════════════════════════════════════════════════════════════════
 *  open_pcm_device — 打开 PCM 设备，含 PulseAudio fallback 链
 *
 *  逻辑同 beep.c 的 main 前半段，返回 0 成功。
 *  成功后 pcm 已打开且配置就绪。
 * ═══════════════════════════════════════════════════════════════════ */

static int open_pcm_device(struct tlibc_pcm *pcm, const char *dev,
                           int argc, char **argv)
{
    int ret;

    if (dev) {
        /* 用户指定了设备 → 直接打开 */
        ret = tlibc_pcm_open(pcm, dev, CHANNELS, 44100, BITS);
        if (ret == -EBUSY) {
            try_pasuspender_fallback(argc, argv);
            /* pasuspender 不返回，此处可继续尝试其他设备 */
            ret = tlibc_pcm_open(pcm, NULL, CHANNELS, 44100, BITS);
        }
    } else {
        /* 先尝试主设备 /dev/snd/pcmC0D0p */
        ret = tlibc_pcm_open(pcm, "/dev/snd/pcmC0D0p",
                             CHANNELS, 44100, BITS);
        if (ret == -EBUSY) {
            try_pasuspender_fallback(argc, argv);
            ret = tlibc_pcm_open(pcm, NULL, CHANNELS, 44100, BITS);
        } else if (ret < 0) {
            ret = tlibc_pcm_open(pcm, NULL, CHANNELS, 44100, BITS);
        }
        if (ret == 0 && is_pulseaudio_active()) {
            int pri_fd = __openat(AT_FDCWD, "/dev/snd/pcmC0D0p",
                                  O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
            if (pri_fd == -EBUSY) {
                tlibc_pcm_close(pcm);
                try_pasuspender_fallback(argc, argv);
                ret = tlibc_pcm_open(pcm, NULL, CHANNELS, 44100, BITS);
            } else if (pri_fd >= 0) {
                __close(pri_fd);
            }
        }
    }

    if (ret < 0) {
        if (ret == -EBUSY || is_pulseaudio_active())
            try_pasuspender_fallback(argc, argv);
        print_error("无法打开 PCM 设备", ret);
        return ret;
    }

    ret = tlibc_pcm_configure(pcm);
    if (ret < 0) {
        if (ret == -EBUSY || is_pulseaudio_active())
            try_pasuspender_fallback(argc, argv);
        print_error("PCM 配置失败", ret);
        tlibc_pcm_close(pcm);
        return ret;
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  主函数
 * ═══════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    struct tlibc_pcm pcm;
    const char *dev = NULL;
    int ret;

    /* ── 参数 ── */
    if (argc > 1)
        dev = argv[1];

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "twinkle: 小星星 ★ 正在初始化 PCM 设备...\n");

    /* ── 打开设备 ── */
    ret = open_pcm_device(&pcm, dev, argc, argv);
    if (ret < 0)
        return 1;

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "twinkle: 设备就绪 %dHz/%dbit/%dch\n",
                pcm.rate, BITS, CHANNELS);

    /* ── 计算一遍总时长 ── */
    {
        double one_pass = 0.0;
        unsigned int i;
        for (i = 0; i < MELODY_LEN; i++) {
            one_pass += melody[i].beats * BEAT_SEC;
            one_pass += 0.020; /* 音符间隔 */
        }
        PRINT_COLOR(GREEN_COLOR_PRINT,
                    "twinkle: 单遍约 %.1fs，循环至 ≥20s\n", one_pass);
    }

    /* ── 播放旋律（循环直到 ≥20 秒）── */
    {
        double total_played = 0.0;
        const double min_duration = 20.0;
        int loop_count = 0;

        while (total_played < min_duration) {
            loop_count++;
            PRINT_COLOR(CYAN_COLOR_PRINT,
                        "twinkle: ── 第 %d 遍 ──\n", loop_count);

            unsigned int i;
            for (i = 0; i < MELODY_LEN; i++) {
                double note_sec = melody[i].beats * BEAT_SEC;

                ret = play_note(&pcm, melody[i].freq,
                                pcm.rate, note_sec);
                if (ret < 0) {
                    print_error("播放失败", ret);
                    tlibc_pcm_close(&pcm);
                    return 1;
                }

                total_played += note_sec + 0.020; /* 音符 + 间隔 */
            }
        }

        PRINT_COLOR(GREEN_COLOR_PRINT,
                    "twinkle: 播放完成（共 %.1fs，%d 遍）✓\n",
                    total_played, loop_count);
    }

    /* ── 清理 ── */
    tlibc_pcm_drain(&pcm);
    tlibc_pcm_close(&pcm);
    return 0;
}
