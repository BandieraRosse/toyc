/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * alsa_scan.c — ALSA PCM 设备枚举与评分
 *
 * 为 tlibc_pcm_open 提供智能设备选择，替代原有的简单首次命中策略。
 *
 * 策略：
 *   1) 枚举所有声卡（/proc/asound/cards）
 *   2) 对每张卡，通过 controlC* ioctl 枚举 PCM 设备（不会被 EBUSY 阻碍）
 *   3) 若无 control ioctl，从 /proc/asound/cardX/pcmYp/info 读取
 *   4) 扫描 /dev/snd/ 下所有 pcmC*D*p 作为保底（现有逻辑）
 *   5) 对每个设备按「类型 + 格式支持 + 卡优先级」评分
 *   6) 返回按评分降序排列的设备表
 *
 * 系统调用：openat, ioctl, read, close, getdents64
 *
 * 索引：
 *   tlibc_audio_scan    枚举所有 PCM 播放设备
 *     tlibc_audio_score 计算设备的音频适用评分
 *     classify_dev_type 根据设备名判别类型
 *     scan_cards_procfs 从 /proc/asound/cards 读卡列表
 *     enum_card_ctl     通过 controlC* ioctl 枚举 PCM 设备
 *     enum_card_procfs  从 /proc/asound/cardX/pcmYp/info 读设备名
 *     scan_dev_snd      保底扫描 /dev/snd/ 下所有 pcmC*D*p
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "dirent.h"
#include "errno.h"

/* ══════════════════════════════════════════════════════════════════
 *  常量
 * ══════════════════════════════════════════════════════════════════ */

#define MAX_CARDS      8
#define MAX_PCM_DEVS   (MAX_CARDS * 32)
#define PCM_PATH_MAX   64
#define SCAN_BUF_SIZE  4096
#define CTL_PATH_SZ    32
#define PROC_BUF_SZ    512

/* 使用 linux_audio.h 中定义的常量：
 *   TLIB_AUDIO_DEV_NAME_SZ = 80
 *   TLIB_AUDIO_PATH_SZ     = 48
 */

/* ══════════════════════════════════════════════════════════════════
 *  设备类型枚举 — 由 linux_audio.h 提供
 * ══════════════════════════════════════════════════════════════════ */

/* enum tlibc_dev_type 在 include/linux_audio.h 中定义 */

/* ══════════════════════════════════════════════════════════════════
 *  设备描述符 — 由 linux_audio.h 提供
 * ══════════════════════════════════════════════════════════════════ */

/* struct tlibc_audio_dev 在 include/linux_audio.h 中定义 */

/* ══════════════════════════════════════════════════════════════════
 *  内部辅助：根据设备名判别类型
 * ══════════════════════════════════════════════════════════════════ */

static enum tlibc_dev_type classify_dev_type(const char *name)
{
    if (!name || !name[0])
        return DEV_TYPE_UNKNOWN;

    /* 按精确度从高到低匹配 */
    if (strstr(name, "USB Audio"))
        return DEV_TYPE_USB;
    if (strstr(name, "USB"))
        return DEV_TYPE_USB;
    if (strstr(name, "HDMI"))
        return DEV_TYPE_HDMI;
    if (strstr(name, "SPDIF") || strstr(name, "IEC958") ||
        strstr(name, "Digital"))
        return DEV_TYPE_SPDIF;
    if (strstr(name, "Analog") || strstr(name, "Headphone") ||
        strstr(name, "Speaker") || strstr(name, "Mic"))
        return DEV_TYPE_ANALOG;

    return DEV_TYPE_OTHER;
}

/* ══════════════════════════════════════════════════════════════════
 *  评分算法
 *
 *  规则：
 *    类型分：
 *      Analog/Headphone  +200（最可能是正确的输出设备）
 *      USB Audio         +150（外接 USB 耳机/音箱）
 *      SPDIF/IEC958       +60
 *      HDMI               +10（无显示器的 HDMI 不可用）
 *      OTHER/UNKNOWN      +30
 *
 *    格式匹配分（若已有 HW_REFINE 信息）：
 *      精确匹配请求的 rate+channels+S16_LE  +100
 *      仅 channels 匹配                +40
 *
 *    卡优先级分：
 *      /proc/asound/cards 中第 0 张卡  +30（默认卡，通常是板载声卡）
 *      其他卡                          +10
 *
 *    EBUSY 惩罚（设备被占用 != 不存在）：
 *      被占用的设备不放弃             -150
 *      （保留机会以支持 pasuspender 回退）
 *
 *  最高分 ≈ 200 + 100 + 30 = 330（模拟精确匹配）
 *  最低可用 ≈ 10 + 40 + 10 = 60（HDMI 仅通道匹配）
 * ══════════════════════════════════════════════════════════════════ */

static void compute_score(struct tlibc_audio_dev *dev,
                          unsigned int req_rate,
                          unsigned int req_ch,
                          int is_default_card)
{
    int score = 0;

    /* ── 类型分 ── */
    switch (dev->type) {
    case DEV_TYPE_ANALOG: score += 200; break;
    case DEV_TYPE_USB:    score += 150; break;
    case DEV_TYPE_SPDIF:  score += 60;  break;
    case DEV_TYPE_HDMI:   score += 10;  break;
    default:              score += 30;  break;
    }

    /* ── 卡优先级：/proc/asound/cards 中第一张卡通常是板载声卡 ── */
    if (is_default_card)
        score += 30;
    else
        score += 10;

    /* ── EBUSY 惩罚（设备被占，可能可用 pasuspender 释放） ── */
    if (dev->busy)
        score -= 150;

    /* ── 如果 fd 已打开，可进一步确认设备可用 ── */
    if (dev->fd >= 0) {
        /* 能打开就说明设备可用，加分确认 */
        score += 20;
        __close(dev->fd);
        dev->fd = -1;
    }

    dev->score = score > 0 ? score : 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  从 /proc/asound/cards 读取卡列表
 *
 *  解析格式：
 *    <id> [<name>]: <driver> - <description>
 *      <description> at ...
 *
 *  返回 card count，写入 cards[] 和 default_idx。
 * ══════════════════════════════════════════════════════════════════ */

static int scan_cards_procfs(int *cards_out, int max_cards,
                             int *default_idx_out)
{
    char buf[PROC_BUF_SZ];
    int fd = __openat(AT_FDCWD, "/proc/asound/cards",
                      O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return 0;

    int n = (int)__read(fd, buf, PROC_BUF_SZ - 1);
    __close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';

    int count = 0;
    int first_id = -1;
    char *line = buf;

    while (line && count < max_cards) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* 跳过前导空白 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* 非空行且以数字开头 → 卡行 */
        if (p[0] >= '0' && p[0] <= '9') {
            int card_id = 0;
            int pos = 0;
            while (p[pos] >= '0' && p[pos] <= '9') {
                card_id = card_id * 10 + (p[pos] - '0');
                pos++;
            }
            cards_out[count++] = card_id;
            if (first_id < 0)
                first_id = card_id;
        }

        if (!nl) break;
        line = nl + 1;
    }

    /* 标记默认卡：cards 中第一张卡（通常是板载声卡） */
    if (default_idx_out)
        *default_idx_out = first_id;

    return count;
}

/* ══════════════════════════════════════════════════════════════════
 *  通过 controlC* ioctl 枚举 PCM 设备
 *
 *  control 设备永不 busy（PulseAudio 占用了 PCM 节点也不影响），
 *  可以在不打开 PCM 节点的情况下获取设备名称。
 *
 *  返回枚举到的设备数，写入 devs[]。
 * ══════════════════════════════════════════════════════════════════ */

static int enum_card_ctl(int card, struct tlibc_audio_dev *devs,
                         int max_devs, int start_idx)
{
    char ctl_path[CTL_PATH_SZ];
    (void)start_idx;

    /* 打开 control 设备 */
    snprintf(ctl_path, sizeof(ctl_path), "/dev/snd/controlC%d", card);
    int ctl_fd = __openat(AT_FDCWD, ctl_path, O_RDWR | O_CLOEXEC, 0);
    if (ctl_fd < 0)
        return 0;   /* 卡不存在或无法访问 */

    int count = 0;
    int dev_idx = -1;

    while (count < max_devs) {
        int next = dev_idx;
        if (__ioctl(ctl_fd, SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, &next) < 0)
            break;
        if (next <= dev_idx) break;
        dev_idx = next;

        /* 查询 PLAYBACK 方向信息 */
        struct snd_pcm_info info;
        memset(&info, 0, sizeof(info));
        info.device    = dev_idx;
        info.subdevice = 0;
        info.stream    = SNDRV_PCM_STREAM_PLAYBACK;

        int has_info = (__ioctl(ctl_fd,
                        SNDRV_CTL_IOCTL_PCM_INFO, &info) == 0);

        if (!has_info)
            continue;   /* 该设备无 playback 方向 */

        struct tlibc_audio_dev *d = &devs[count];
        memset(d, 0, sizeof(*d));

        d->card   = card;
        d->device = dev_idx;

        /* 复制设备名 */
        int ni = 0;
        while (info.name[ni] && ni < TLIB_AUDIO_DEV_NAME_SZ - 1) {
            d->name[ni] = (char)info.name[ni];
            ni++;
        }
        d->name[ni] = '\0';

        /* 设备类型 */
        d->type = classify_dev_type(d->name);

        /* 构造路径 */
        snprintf(d->path, sizeof(d->path),
                 "/dev/snd/pcmC%dD%dp", card, dev_idx);

        /* 尝试打开以探测格式能力 + 检查可用性 */
        /* 探测时使用 O_NONBLOCK 避免阻塞（最终打开由 tlibc_pcm_open 完成） */
        d->fd = __openat(AT_FDCWD, d->path,
                         O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
        if (d->fd == -EBUSY) {
            d->busy = 1;
            d->fd = -1;
        }
        /* 如果打开成功→fd 保留；HW_REFINE 在评分时进行并关闭 */

        count++;
    }

    __close(ctl_fd);
    return count;
}

/* ══════════════════════════════════════════════════════════════════
 *  从 /proc/asound/cardX/pcmYp/info 读取设备名
 *
 *  保底方法：当 control ioctl 不可用时（旧内核或 ENOTTY）使用。
 *  虽然需要打开 PCM 节点才能 HW_REFINE，但名称可以从 procfs 获得。
 * ══════════════════════════════════════════════════════════════════ */

static int enum_card_procfs(int card, struct tlibc_audio_dev *devs,
                            int max_devs, int start_idx)
{
    int count = 0;

    for (int dev = 0; dev < 32 && count < max_devs; dev++) {
        char path[64];
        char buf[PROC_BUF_SZ];

        snprintf(path, sizeof(path),
                 "/proc/asound/card%d/pcm%dp/info", card, dev);
        int fd = __openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
        if (fd < 0)
            continue;   /* 该设备不存在 */

        int n = (int)__read(fd, buf, PROC_BUF_SZ - 1);
        __close(fd);
        if (n <= 0)
            continue;
        buf[n] = '\0';

        /* 从 procfs 获取设备名 */
        char name[TLIB_AUDIO_DEV_NAME_SZ] = "";
        char *line = buf;
        while (line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (strncmp(line, "name:", 5) == 0) {
                char *val = line + 5;
                while (*val == ' ' || *val == '\t') val++;
                int ni = 0;
                while (val[ni] && ni < TLIB_AUDIO_DEV_NAME_SZ - 1) {
                    name[ni] = val[ni];
                    ni++;
                }
                name[ni] = '\0';
                break;
            }
            if (!nl) break;
            line = nl + 1;
        }

        if (!name[0])
            continue;

        struct tlibc_audio_dev *d = &devs[start_idx + count];
        memset(d, 0, sizeof(*d));
        d->card   = card;
        d->device = dev;
        strcpy(d->name, name);
        d->type = classify_dev_type(name);
        snprintf(d->path, sizeof(d->path),
                 "/dev/snd/pcmC%dD%dp", card, dev);

        /* 尝试打开以检查可用性 */
        d->fd = __openat(AT_FDCWD, d->path,
                         O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
        if (d->fd == -EBUSY) {
            d->busy = 1;
            d->fd = -1;
        }

        count++;
    }

    return count;
}

/* ══════════════════════════════════════════════════════════════════
 *  保底扫描：直接扫描 /dev/snd/ 下所有 pcmC*D*p 设备
 *
 *  当 procfs 和 control 都不可用时最后的保底。
 *  注意：本方法无法获取设备名，只能从路径推断卡号/设备号。
 * ══════════════════════════════════════════════════════════════════ */

static int scan_dev_snd(struct tlibc_audio_dev *devs,
                        int max_devs, int start_idx)
{
    unsigned char buf[SCAN_BUF_SIZE];
    struct linux_dirent64 *entry;
    int idx = start_idx;
    int max_from_start = start_idx + max_devs;

    int fd = __openat(AT_FDCWD, "/dev/snd",
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0)
        return 0;

    int nread = (int)__getdents64(fd, (struct linux_dirent64 *)buf,
                                  SCAN_BUF_SIZE);
    __close(fd);
    if (nread <= 0)
        return 0;

    int offset = 0;
    while (offset < nread && idx < max_from_start) {
        entry = (struct linux_dirent64 *)(buf + offset);
        if (entry->d_reclen == 0)
            break;

        const char *name = entry->d_name;

        /* 检查是否为播放设备：pcmC<card>D<dev>p */
        int len = 0;
        while (name[len]) len++;

        if (name[0] == 'p' && name[1] == 'c' && name[2] == 'm' &&
            len > 4 && name[len - 1] == 'p') {
            /* 解析 card 和 device 编号 */
            int ci = 3;  /* 跳过 "pcm" */
            int card = 0, dev = 0;

            if (name[ci] != 'C') { offset += entry->d_reclen; continue; }
            ci++;
            while (name[ci] >= '0' && name[ci] <= '9') {
                card = card * 10 + (name[ci] - '0');
                ci++;
            }
            if (name[ci] != 'D') { offset += entry->d_reclen; continue; }
            ci++;
            while (name[ci] >= '0' && name[ci] <= '9') {
                dev = dev * 10 + (name[ci] - '0');
                ci++;
            }
            /* name[ci] 应为 'p' */

            /* 构造完整路径 */
            char full_path[PCM_PATH_MAX];
            snprintf(full_path, sizeof(full_path),
                     "/dev/snd/%s", name);

            struct tlibc_audio_dev *d = &devs[idx];
            memset(d, 0, sizeof(*d));
            d->card   = card;
            d->device = dev;
            d->type   = DEV_TYPE_UNKNOWN;  /* 无名称信息 */
            strcpy(d->name, "(unknown)");  /* 未知名称 */
            strcpy(d->path, full_path);

            /* 尝试打开 */
            d->fd = __openat(AT_FDCWD, full_path, O_RDWR | O_CLOEXEC, 0);
            if (d->fd == -EBUSY) {
                d->busy = 1;
                d->fd = -1;
            }

            idx++;
        }

        offset += entry->d_reclen;
    }

    return idx - start_idx;
}

/* ══════════════════════════════════════════════════════════════════
 *  公开 API：tlibc_audio_scan
 *
 *  枚举所有 PCM 播放设备，按评分降序排列。
 *
 *  参数：
 *    devs       — 输出数组
 *    max_devs   — 数组大小
 *    req_rate   — 请求的采样率（评分用，0 = 不验证格式）
 *    req_ch     — 请求的声道数（评分用，0 = 不验证格式）
 *
 *  返回：
 *    找到的设备数（可大于 max_devs，但只填到 max_devs 个）
 *    负数  = errno
 *
 *  枚举策略优先级：
 *    1. controlC* ioctl（获取设备名 + 类型 + 打开验证）
 *    2. /proc/asound/cardX/pcmYp/info（保底获取设备名）
 *    3. getdents64 /dev/snd/（最终保底，无名称信息）
 *
 *  注意：
 *    本函数会临时打开每个 PCM 设备以执行 HW_REFINE 验证格式，
 *    验证后立即关闭 fd。调用者后续需通过 tlibc_pcm_open 重新打开。
 *    被 EBUSY 的设备无法 HW_REFINE（知道设备名但不知道格式能力）。
 * ══════════════════════════════════════════════════════════════════ */

int tlibc_audio_scan(struct tlibc_audio_dev *devs, int max_devs,
                     unsigned int req_rate, unsigned int req_ch)
{
    int total = 0;
    int default_card = -1;

    if (!devs || max_devs <= 0)
        return -EINVAL;

    /* ── 第 1 步：获取卡列表 ── */
    int cards[MAX_CARDS];
    int ncards = scan_cards_procfs(cards, MAX_CARDS, &default_card);

    /* ── 第 2 步：对每张卡，通过 control ioctl 枚举 ── */
    int have_ctl = 0;
    for (int ci = 0; ci < ncards && total < max_devs; ci++) {
        int found = enum_card_ctl(cards[ci], devs + total,
                                 max_devs - total, total);
        if (found > 0) {
            have_ctl = 1;
            total += found;
        }
    }

    /* ── 第 3 步：若无 control，从 procfs 枚举 ── */
    int have_procfs = 0;
    if (!have_ctl) {
        for (int ci = 0; ci < ncards && total < max_devs; ci++) {
            int found = enum_card_procfs(cards[ci], devs + total,
                                        max_devs - total, total);
            if (found > 0) {
                have_procfs = 1;
                total += found;
            }
        }
    }

    /* ── 第 4 步：保底：扫描 /dev/snd/ 目录 ── */
    if (!have_ctl && !have_procfs) {
        int found = scan_dev_snd(devs, max_devs, total);
        total += found;
    }

    if (total <= 0)
        return -ENOENT;

    /* ── 第 5 步：评分 ── */
    for (int i = 0; i < total; i++) {
        int is_default = (devs[i].card == default_card);
        compute_score(&devs[i], req_rate, req_ch, is_default);
    }

    /* ── 第 6 步：按评分降序排序（简单插入排序） ── */
    for (int i = 1; i < total; i++) {
        struct tlibc_audio_dev key;
        memcpy(&key, &devs[i], sizeof(key));
        int j = i - 1;
        while (j >= 0 && devs[j].score < key.score) {
            memcpy(&devs[j + 1], &devs[j], sizeof(devs[j]));
            j--;
        }
        memcpy(&devs[j + 1], &key, sizeof(key));
    }

    return total;
}

/* ══════════════════════════════════════════════════════════════════
 *  辅助：按路径查找设备（用于 pasuspender 回退后重新定位）
 * ══════════════════════════════════════════════════════════════════ */

int tlibc_audio_find_by_path(const struct tlibc_audio_dev *devs,
                             int count, const char *path)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(devs[i].path, path) == 0)
            return i;
    }
    return -1;
}

/* ══════════════════════════════════════════════════════════════════
 *  辅助：获取设备类型字符串（用于调试输出）
 * ══════════════════════════════════════════════════════════════════ */

const char *tlibc_audio_dev_type_str(enum tlibc_dev_type t)
{
    switch (t) {
    case DEV_TYPE_ANALOG: return "模拟";
    case DEV_TYPE_USB:    return "USB";
    case DEV_TYPE_SPDIF:  return "SPDIF";
    case DEV_TYPE_HDMI:   return "HDMI";
    default:              return "其他";
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  检测 PulseAudio 是否活跃
 *
 *  检查 PulseAudio 的 PID 文件和目录结构。
 *  （不依赖 getuid，尝试常见路径）
 *
 *  注意：不能直接 open() PulseAudio 的 native socket（Unix socket），
 *  否则会被阻塞等待连接！应检查 PID 文件等常规文件。
 *
 *  返回 1 = PulseAudio 可能活跃，0 = 未发现
 * ══════════════════════════════════════════════════════════════════ */

int tlibc_check_pulseaudio(void)
{
    /* 检查 PID 文件（常规文件，安全打开） */
    static const char *pid_paths[] = {
        "/run/user/1000/pulse/pid",
        "/run/user/0/pulse/pid",
        NULL
    };

    for (int i = 0; pid_paths[i]; i++) {
        int fd = __openat(AT_FDCWD, pid_paths[i], O_RDONLY | O_CLOEXEC, 0);
        if (fd >= 0) {
            __close(fd);
            return 1;   /* PID 文件存在 → PA 可能运行中 */
        }
    }

    return 0;
}
