/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * evdev_kbd — TTY 图形模式下的 evdev 键盘输入抽象
 *
 * 机制：扫描 /dev/input/event[0..31]，通过 ioctl 给每个设备打分
 *       （按键数 + 总线类型 + 名称），选评分最高的键盘。
 *       然后从该设备读取 struct input_event。
 * 系统调用：openat, ioctl(EVIOCGBIT/EVIOCGID/EVIOCGNAME), read, fcntl, close
 *
 * 本模块被编译到 tlibc.a 中，应用代码链接即可使用。
 *
 * 索引：
 *   evdev_kbd_open     扫描所有 evdev 设备 → 评分选最佳键盘 → 打开
 *     keyboard_score    给 fd 打键盘分（按键数 + 总线 + 名称）
 *   evdev_kbd_fd       返回底层 fd，供 poll/select 使用
 *   evdev_kbd_read     从设备读取 struct input_event → 提取 KEY_* 编码
 *   evdev_kbd_close    关闭 fd 并释放内存
 *   evdev_kbd_name     返回设备名称
 */

#include "core.h"
#include "fcntl.h"
#include "errno.h"
#include "linux_input.h"
#include "evdev_kbd.h"

/* ── 扫描上限 ── */
#define MAX_EVENT_DEV  32

/* ── 内部结构 ── */
struct evdev_kbd {
    int fd;
    char name[128];
};

/* ── 给 fd 的键盘度打分（0=不是键盘，分越高越像真键盘）──
 *
 * 评分规则：
 *   必要条件（缺一淘汰）：EV_KEY + EV_LED + EV_REP
 *   阈值：纯按键 20 键，有 REL/ABS 的复合设备 80 键
 *   键鼠复合体检测：有 REL/ABS → 名字必须含 "Keyboard"，否则淘汰
 *   基础分 = 标准键盘键数（KEY_* 0-255）×3
 *   必要条件加分：EV_LED + EV_REP +300
 *   名字含 "Keyboard"：+300（确保名字标明的键盘总是优先）
 *   总线加分：USB/Bluetooth +300（名字含 "Keyboard"）/+50（无名设备）
 *            I2C +100 / i8042 +50 / BUS_HOST -500
 *   名字惩罚："extra"/"radio"/"sensor" -300 / "button"/"mouse"/"touch" -200
 */
static int keyboard_score(int fd, char *name_out, int name_sz)
{
    unsigned char evbits[8] = {0};

    /* 读事件类型能力位图 */
    if (__ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;

    /* 必须有按键事件能力 */
    if (!test_bit(EV_KEY, evbits, sizeof(evbits)))
        return 0;

    int has_led = test_bit(EV_LED, evbits, sizeof(evbits));
    int has_rep = test_bit(EV_REP, evbits, sizeof(evbits));
    int has_rel = test_bit(EV_REL, evbits, sizeof(evbits));
    int has_abs = test_bit(EV_ABS, evbits, sizeof(evbits));

    /* 阈值：有 REL/ABS 的设备需要更多按键才能被考虑 */
    int no_white_th = 20;           /* 白色键（无 REL/ABS）最小按键数 */
    int has_mouse_th = 80;          /* 鼠标类设备最小按键数 */
    int threshold = (has_rel || has_abs) ? has_mouse_th : no_white_th;

    /* 统计按键数，区分标准键盘键 (KEY_*, 0-255) 和扩展键/BTN (256+) */
    unsigned char keybits[128] = {0};
    int key_std = 0, key_ext = 0;
    if (__ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
        for (int i = 0; i < 128 * 8; i++) {
            if (test_bit(i, keybits, sizeof(keybits))) {
                if (i < 256)
                    key_std++;      /* 标准键盘键区 */
                else
                    key_ext++;      /* BTN_* / 扩展键 */
            }
        }
    }
    /* 标准键盘键数不足 → 不可能是指点设备 */
    if (key_std < threshold)
        return 0;
    /* 扩展键（BTN_*）多于标准键 → 游戏手柄/鼠标，不是键盘 */
    if (key_ext > key_std)
        return 0;

    /* ── 基础分：标准键盘键数 ×3（强调"打字键"而非按钮）── */
    int score = key_std * 3;

    /* ── EV_LED + EV_REP：真实键盘的必要条件 ──
     *   所有键盘（PS/2, USB HID, I2C, 蓝牙）都必须支持：
     *     - EV_LED：Caps/Num/Scroll 指示灯
     *     - EV_REP：按键自动重复
     *   没有这两者的设备（无线键鼠复合体、传感器、面板、鼠标等）
     *   不可能是指点键盘，直接淘汰。 */
    if (!has_led || !has_rep)
        return 0;
    score += 300;

    /* ── 读取设备名（只读一次，后续所有检测共用）── */
    char dev_name[128] = {0};
    int name_has_kbd = 0;
    if (__ioctl(fd, EVIOCGNAME(sizeof(dev_name)), dev_name) >= 0) {
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='k' && (dev_name[i+1]|32)=='e' &&
                (dev_name[i+2]|32)=='y' && (dev_name[i+3]|32)=='b' &&
                (dev_name[i+4]|32)=='o' && (dev_name[i+5]|32)=='a' &&
                (dev_name[i+6]|32)=='r' && (dev_name[i+7]|32)=='d') {
                name_has_kbd = 1;
                break;
            }
        }
    }

    /* ── 键鼠复合体检测 ──
     *   设备有鼠标能力（EV_REL/EV_ABS）但名字里没写 "Keyboard"：
     *   这是无线键鼠一体 dongle 等复合设备，不是纯粹的打字键盘。
     *   注意：带 TrackPoint 的键盘名字里有 "Keyboard"（如
     *   "AT Translated Set 2 keyboard"），不会被误杀。 */
    if ((has_rel || has_abs) && !name_has_kbd)
        return 0;

    /* ── 总线类型 ──
     *   USB/蓝牙设备只有名字含 "Keyboard" 才给信任加分（+300），
     *   否则只给基础分 +50。避免无线键鼠复合体（如 YICHIP）
     *   靠总线优势击败名字带 "Keyboard" 的内置键盘。 */
    {
        struct input_id id;
        if (__ioctl(fd, EVIOCGID, &id) >= 0) {
            switch (id.bustype) {
            case BUS_USB:
                score += name_has_kbd ? 300 : 50;
                break;
            case BUS_BLUETOOTH:
                score += name_has_kbd ? 300 : 50;
                break;
            case BUS_I2C:       score += 100; break;   /* I2C-HID 现代笔记本内置 */
            case BUS_I8042:     score += 50;  break;   /* i8042 PS/2 传统内置 */
            case BUS_HOST:      score -= 500; break;   /* 电源/传感器，非键盘 */
            }
        }
    }

    /* ── 设备名检测（用已读取的 dev_name，避免重复 ioctl）── */
    if (dev_name[0]) {
        if (name_has_kbd)
            score += 300;       /* "Keyboard" 字样 → 肯定是真键盘，大额加分 */

        /* "extra" → 功能键 */
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='e' && (dev_name[i+1]|32)=='x' &&
                (dev_name[i+2]|32)=='t' && (dev_name[i+3]|32)=='r' &&
                (dev_name[i+4]|32)=='a') {
                score -= 300;
                break;
            }
        }
        /* "radio" → 无线控制 */
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='r' && (dev_name[i+1]|32)=='a' &&
                (dev_name[i+2]|32)=='d' && (dev_name[i+3]|32)=='i' &&
                (dev_name[i+4]|32)=='o') {
                score -= 300;
                break;
            }
        }
        /* "sensor" → 传感器 */
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='s' && (dev_name[i+1]|32)=='e' &&
                (dev_name[i+2]|32)=='n' && (dev_name[i+3]|32)=='s' &&
                (dev_name[i+4]|32)=='o' && (dev_name[i+5]|32)=='r') {
                score -= 300;
                break;
            }
        }
        /* "button" → 按钮面板 */
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='b' && (dev_name[i+1]|32)=='u' &&
                (dev_name[i+2]|32)=='t' && (dev_name[i+3]|32)=='t' &&
                (dev_name[i+4]|32)=='o' && (dev_name[i+5]|32)=='n') {
                score -= 200;
                break;
            }
        }
        /* "mouse" → 鼠标上的伪键盘接口 */
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='m' && (dev_name[i+1]|32)=='o' &&
                (dev_name[i+2]|32)=='u' && (dev_name[i+3]|32)=='s' &&
                (dev_name[i+4]|32)=='e') {
                score -= 200;
                break;
            }
        }
        /* "touch" → 触摸屏/触摸板 */
        for (int i = 0; dev_name[i]; i++) {
            if ((dev_name[i]|32)=='t' && (dev_name[i+1]|32)=='o' &&
                (dev_name[i+2]|32)=='u' && (dev_name[i+3]|32)=='c' &&
                (dev_name[i+4]|32)=='h') {
                score -= 200;
                break;
            }
        }
        /* 传出名称给调用者 */
        if (name_out) {
            int n = 0;
            while (dev_name[n] && n < name_sz - 1) { name_out[n] = dev_name[n]; n++; }
            name_out[n] = '\0';
        }
    }

    return score;
}

/*
 * 拼接 "/dev/input/event<N>" 到 buf（最大 32 字节），
 * 避免依赖 snprintf 无法 inline 的问题。
 */
static void make_event_path(char *buf, int num)
{
    const char *prefix = "/dev/input/event";
    while (*prefix) *buf++ = *prefix++;
    if (num >= 10) *buf++ = '0' + (num / 10);
    *buf++ = '0' + (num % 10);
    *buf = '\0';
}

/* ═══════════════════════════════════════════════
 * 公开 API
 * ═══════════════════════════════════════════════ */

struct evdev_kbd *evdev_kbd_open(void)
{
    struct evdev_kbd *kbd;

    kbd = (struct evdev_kbd *)tlibc_malloc(sizeof(struct evdev_kbd));
    if (!kbd) return NULL;

    kbd->fd = -1;
    kbd->name[0] = '\0';

    /* 扫描 /dev/input/event[0..MAX_EVENT_DEV) 选评分最高的键盘 */
    {
        int best_fd = -1;
        int best_score = 0;
        char best_name[128];
        best_name[0] = '\0';

        for (int i = 0; i < MAX_EVENT_DEV; i++) {
            char path[32];
            make_event_path(path, i);

            int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
            if (fd < 0)
                continue;

            char tmp_name[128];
            int score = keyboard_score(fd, tmp_name, sizeof(tmp_name));
            if (score > best_score) {
                best_score = score;
                best_fd = fd;
                /* 复制名称 */
                int n = 0;
                while (tmp_name[n] && n < (int)sizeof(best_name) - 1) {
                    best_name[n] = tmp_name[n];
                    n++;
                }
                best_name[n] = '\0';
            } else {
                __close(fd);
            }
        }

        if (best_fd >= 0) {
            kbd->fd = best_fd;
            /* 复制名称到 kbd 结构 */
            int n = 0;
            while (best_name[n] && n < (int)sizeof(kbd->name) - 1) {
                kbd->name[n] = best_name[n];
                n++;
            }
            kbd->name[n] = '\0';

            /* 设为非阻塞 */
            __fcntl(best_fd, F_SETFL, O_NONBLOCK);
        }
    }

    if (kbd->fd < 0) {
        tlibc_free(kbd);
        return NULL;
    }

    return kbd;
}

int evdev_kbd_fd(struct evdev_kbd *kbd)
{
    if (!kbd) return -1;
    return kbd->fd;
}

int evdev_kbd_read(struct evdev_kbd *kbd, int *type_out)
{
    struct input_event ev = {0};
    int n;

    if (!kbd || kbd->fd < 0)
        return 0;

    n = __read(kbd->fd, &ev, sizeof(ev));

    /* 非阻塞无数据或部分读取都算无事件 */
    if (n != (int)sizeof(ev))
        return 0;

    /* 只返回 EV_KEY 事件 */
    if (ev.type != EV_KEY)
        return 0;

    if (type_out)
        *type_out = ev.value;

    return (int)ev.code;
}

void evdev_kbd_close(struct evdev_kbd *kbd)
{
    if (!kbd) return;
    if (kbd->fd >= 0)
        __close(kbd->fd);
    tlibc_free(kbd);
}

const char *evdev_kbd_name(struct evdev_kbd *kbd)
{
    if (!kbd) return "";
    return kbd->name;
}
