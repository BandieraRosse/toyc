/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * wslplay — 无外部依赖的 WSLg 音频播放测试
 *
 * 直接使用 Tinylibc 的最小 PulseAudio Native Protocol 客户端连接
 * /mnt/wslg/PulseServer，不依赖 paplay、libpulse 或 pulseaudio-utils。
 */

#include "tlibc_everything.h"
#include "pulse_min.h"
#include "math.h"
#include "errno.h"

#define TONE_RATE         48000
#define TONE_CHANNELS     2
#define TONE_FREQUENCY    440
#define TONE_FRAMES       TONE_RATE
#define TONE_CHUNK_FRAMES 512

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void print_usage(void)
{
    printf("用法: wslplay           # 播放一秒 440Hz 正弦音\n");
    printf("      wslplay --test    # 同上\n");
    printf("      wslplay --check   # 检查 WSLg 音频设施\n");
    printf("      wslplay --help\n\n");
    printf("程序直接连接 WSLg，不需要 paplay 或 pulseaudio-utils。\n");
}

static int play_tone(void)
{
    struct toy_pulse pulse;
    short samples[TONE_CHUNK_FRAMES * TONE_CHANNELS];
    double phase = 0.0;
    double step = 2.0 * M_PI * TONE_FREQUENCY / TONE_RATE;
    int written = 0;
    int ret;

    fprintf(2, "wslplay: 连接 WSLg...\n");
    ret = toy_pulse_open(&pulse, TONE_RATE, TONE_CHANNELS);
    if (ret < 0) {
        fprintf(2, "wslplay: 无法连接 WSLg PulseAudio（错误 %d）\n", -ret);
        return 1;
    }
    fprintf(2, "wslplay: stream 已建立，开始播放...\n");

    while (written < TONE_FRAMES) {
        int frames = TONE_FRAMES - written;
        if (frames > TONE_CHUNK_FRAMES) frames = TONE_CHUNK_FRAMES;
        for (int i = 0; i < frames; i++) {
            int frame = written + i;
            double envelope = 1.0;
            short sample;
            if (frame < TONE_RATE / 100)
                envelope = (double)frame / (TONE_RATE / 100);
            else if (frame >= TONE_FRAMES - TONE_RATE / 100)
                envelope = (double)(TONE_FRAMES - frame - 1) /
                           (TONE_RATE / 100);
            sample = (short)(sin(phase) * 24000.0 * envelope);
            samples[i * 2] = sample;
            samples[i * 2 + 1] = sample;
            phase += step;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
        ret = (int)toy_pulse_write(&pulse, samples, (unsigned long)frames);
        if (ret < 0) {
            fprintf(2, "wslplay: PCM 写入失败（错误 %d）\n", -ret);
            toy_pulse_close(&pulse);
            return 1;
        }
        written += frames;
    }

    fprintf(2, "wslplay: PCM 已提交，等待播放完成...\n");
    ret = toy_pulse_drain(&pulse);
    toy_pulse_close(&pulse);
    if (ret < 0) {
        fprintf(2, "wslplay: 等待播放完成失败（错误 %d）\n", -ret);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        print_usage();
        return 2;
    }
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--check") == 0) {
        printf("WSLg PulseAudio socket: %s (%s)\n", TOYC_PULSE_SOCKET,
               toy_pulse_available() ? "可用" : "不存在");
        printf("播放后端: Toyc 内置 PulseAudio Native Protocol v8\n");
        printf("外部依赖: 无\n");
        return toy_pulse_available() ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "--test") != 0) {
        fprintf(2, "wslplay: 未知参数: %s\n", argv[1]);
        return 2;
    }
    return play_tone();
}
