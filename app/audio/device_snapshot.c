/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * device_snapshot — 系统设备快照
 *
 * 输出当前系统的音频和输入设备配置，用于：
 *   1) 验证自动设备选择是否如预期工作
 *   2) 在不同电脑上运行，收集设备表验证数据
 *
 * 用法:
 *   device_snapshot             输出快照到 stdout
 *   device_snapshot -v          详细模式（含 HW_REFINE）
 *   device_snapshot -p <path>   对比指定快照（TODO）
 *
 * 系统调用：openat, ioctl, read, close
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "errno.h"
#include "evdev_kbd.h"
#include "evdev_mouse.h"

/* ── 颜色 ── */
#define RST   CLEAR_COLOR_PRINT
#define CYN   CYAN_COLOR_PRINT
#define GRN   GREEN_COLOR_PRINT
#define YEL   YELLOW_COLOR_PRINT
#define RED   RED_COLOR_PRINT
#define BLU   BLUE_COLOR_PRINT

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    __printf("%s══════════════════════════════════════════════════\n", CYN);
    __printf("  设备快照 — Device Snapshot\n");
    {
        /* 获取 hostname 简易方式：读 /proc/sys/kernel/hostname */
        char hnbuf[64];
        int fd = __openat(AT_FDCWD, "/proc/sys/kernel/hostname",
                          O_RDONLY | O_CLOEXEC, 0);
        if (fd >= 0) {
            int n = (int)__read(fd, hnbuf, 63);
            __close(fd);
            if (n > 0) {
                hnbuf[n] = '\0';
                while (n > 0 && (hnbuf[n-1] == '\n' || hnbuf[n-1] == ' '))
                    hnbuf[--n] = '\0';
                __printf("  主机: %s%s%s\n", YEL, hnbuf, CYN);
            }
        }
    }
    __printf("%s══════════════════════════════════════════════════\n%s", CYN, RST);

    /* ══════════════════════════════════════
     *  音频设备
     * ══════════════════════════════════════ */

    __printf("\n%s▶ 音频播放设备%s\n", CYN, RST);

    struct tlibc_audio_dev devs[32];
    int ndev = tlibc_audio_scan(devs, 32, 48000, 2);

    if (ndev <= 0) {
        __printf("  %s未发现音频设备%s\n", RED, RST);
    } else {
        __printf("  %s%-4s %-22s %-30s %-8s %s%s\n",
                 BLU, "#", "设备", "名称", "类型", "评分", RST);
        __printf("  %s%-4s %-22s %-30s %-8s %s%s\n",
                 BLU, "---", "------", "----", "----", "----", RST);

        for (int i = 0; i < ndev; i++) {
            const struct tlibc_audio_dev *d = &devs[i];
            const char *status = "";
            if (d->busy) status = " ⚠EBUSY";

            const char *clr;
            switch (d->type) {
            case DEV_TYPE_ANALOG: clr = GRN; break;
            case DEV_TYPE_HDMI:   clr = YEL; break;
            case DEV_TYPE_USB:    clr = MAGANTA_COLOR_PRINT; break;
            default:              clr = RST;  break;
            }

            __printf("  %s#%-3d %-22s %-30s [%s] %3d%s%s\n",
                     clr, i, d->path, d->name,
                     tlibc_audio_dev_type_str(d->type),
                     d->score, status, RST);
        }

        /* 自动选中分析 */
        __printf("\n  %s→ 自动选中分析:%s\n", CYN, RST);
        int best_avail = -1;
        for (int i = 0; i < ndev; i++) {
            if (!devs[i].busy) {
                best_avail = i;
                break;
            }
        }

        if (best_avail == 0) {
            __printf("    %s✓ %s (评分 %d)%s\n",
                     GRN, devs[0].path, devs[0].score, RST);
            __printf("    → 最佳匹配，设备可用\n");
        } else if (best_avail > 0) {
            __printf("    %s%s (评分 %d)%s\n",
                     YEL, devs[best_avail].path,
                     devs[best_avail].score, RST);
            __printf("    → 最佳设备 %s(%d分) 被占用，使用次优\n",
                     devs[0].name, devs[0].score);
        } else {
            __printf("    %s所有设备均被占用 (EBUSY)%s\n", RED, RST);
            int pa = tlibc_check_pulseaudio();
            if (pa)
                __printf("    → PulseAudio 活跃，可尝试 pasuspender\n");
            else
                __printf("    → 原因未知\n");
        }
    }

    /* ══════════════════════════════════════
     *  输入设备 — 键盘
     * ══════════════════════════════════════ */

    __printf("\n%s▶ 键盘设备%s\n", CYN, RST);

    {
        struct evdev_kbd *kbd = evdev_kbd_open();
        if (kbd) {
            __printf("  选中: %s (%s)\n", evdev_kbd_name(kbd), "evdev_kbd 评分系统");
            evdev_kbd_close(kbd);
        } else {
            __printf("  %s未发现键盘%s\n", RED, RST);
        }
    }

    /* ══════════════════════════════════════
     *  输入设备 — 鼠标
     * ══════════════════════════════════════ */

    __printf("\n%s▶ 鼠标设备%s\n", CYN, RST);

    {
        struct evdev_mouse *mouse = evdev_mouse_open(1024, 768);
        if (mouse) {
            __printf("  选中: %s (%s)\n", evdev_mouse_name(mouse), "evdev_mouse 评分系统");
            evdev_mouse_close(mouse);
        } else {
            __printf("  %s未发现鼠标%s\n", RED, RST);
        }
    }

    /* ══════════════════════════════════════
     *  设备表（可 check in 的格式）
     * ══════════════════════════════════════ */

    __printf("\n%s══════════════════════════════════════════════════\n", CYN);
    __printf("  快照结束 — 可将以上输出记录到\n");
    __printf("  compiler-tests/device-tables/<hostname>.txt\n");
    __printf("  用于回归验证设备发现逻辑\n");
    __printf("%s══════════════════════════════════════════════════\n%s", CYN, RST);

    return 0;
}
