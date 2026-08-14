/*
 * gen_sfx — 把 rasterfall/lib/sfx.c 的程序合成音效离线渲染为 TSND 资产。
 *
 * Rasterfall 的 8 种核心音效（枪声/空枪/换弹/命中/击杀/被咬/死亡）由
 * rasterfall/lib/sfx.c 在运行时程序合成。本工具链接同一引擎，把它们一次性渲染成
 * rasterfall/assets/audio/sfx_*.tsnd（44100Hz 单声道 PCM16，从引擎的立体声输出
 * 下混取均值——voice 在左右声道等幅），作为资产库中的离线
 * 表示：可供播放验证、供其他宿主引用，也保证引擎改动时能一键重建资产。
 * 合成参数与引擎逐字节一致（同 seed 的 xorshift 噪声），输出确定性可复现。
 * Rasterfall 启动时加载这些资产并以 toy_sfx 样本模式播放；加载失败的
 * kind 回退程序合成，背景音乐仍为运行时合成。
 *
 * 宿主编译说明：sfx.c 通过 tlibc_compat.h 把 memset/memmove 重映射到 tlibc
 * 自研实现，host gcc 编译时用 -D__memset=memset -D__memmove=memmove 退化为
 * no-op，-D_GNU_SOURCE 暴露 math.h 的 M_PI。
 *
 * 用法（从仓库根目录执行）：
 *   make generate-assets           # 重建全部音效资产并校验
 *   build/gen_sfx                  # 只渲染，默认输出 rasterfall/assets/audio/
 *   build/gen_sfx tmp/sfx          # 自定义输出目录
 * 工具汇总见 tools/README.md。
 *
 * 索引：
 *   main            逐个渲染 8 种音效 → 写 TSND → 打印汇总
 *     render_kind   触发单个 voice 并采集 PCM，裁剪尾部静音
 *     write_tsnd    逐字段写 32 字节 TSND 头部 + PCM16 载荷
 *     p16/p32       小端字段写入（与 tools/toyasset.c 布局一致）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "toy_game.h"

#define SFX_RATE     44100
#define BLOCK_FRAMES 512
#define MAX_FRAMES   44100   /* 上限 1 秒：最长的 PLAYER_DEATH 为 750ms = 33075 帧，含尾块 */

/* 与 enum toy_sfx_kind 一一对应，生成 rasterfall/assets/audio/sfx_<name>.tsnd */
static const char *sfx_names[TOY_SFX_MOLOTOV_BREAK + 1] = {
    "gunshot", "dry_fire", "reload_start", "reload_done",
    "hit_marker", "kill", "bite", "death", "shove", "shove_hit",
    "melee", "melee_hit", "smg", "shotgun", "ak", "awp", "bomb_beep",
    "bomb_explode", "molotov_break",
};

static void p16(FILE *f, unsigned v)
{
    fputc(v & 255, f);
    fputc((v >> 8) & 255, f);
}

static void p32(FILE *f, unsigned long v)
{
    fputc(v & 255, f);
    fputc((v >> 8) & 255, f);
    fputc((v >> 16) & 255, f);
    fputc((v >> 24) & 255, f);
}

/* 触发单个音效并采集 PCM 样本，返回裁剪尾部静音后的帧数 */
static unsigned long render_kind(struct toy_sfx *sfx, short *out, int kind)
{
    short block[BLOCK_FRAMES * 2];   /* toy_sfx_render 输出交错立体声 */
    unsigned long total = 0;
    int v, active, i;

    toy_sfx_init(sfx, SFX_RATE);
    toy_sfx_play(sfx, kind);
    do {
        if (total + BLOCK_FRAMES > MAX_FRAMES) {
            fprintf(stderr, "gen_sfx: %s exceeds %u frames\n",
                    sfx_names[kind], MAX_FRAMES);
            return 0;
        }
        toy_sfx_render(sfx, block, BLOCK_FRAMES);
        /* voice 在左右声道等幅，取均值下混为单声道 */
        for (i = 0; i < BLOCK_FRAMES; i++)
            out[total + i] = (short)(((int)block[2 * i] +
                                      block[2 * i + 1]) / 2);
        total += BLOCK_FRAMES;
        active = 0;
        for (v = 0; v < TOY_SFX_MAX_VOICES; v++)
            if (sfx->voices[v].active) active = 1;
    } while (active);
    /* 尾块里 voice 提前结束的部分是纯零，裁掉 */
    while (total > 0 && out[total - 1] == 0) total--;
    return total;
}

static void write_tsnd(const char *path, const short *pcm, unsigned long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "gen_sfx: cannot open %s\n", path);
        exit(1);
    }
    fwrite("TSND", 1, 4, f);
    p16(f, 1);              /* version */
    p16(f, 32);             /* header_size */
    p32(f, SFX_RATE);       /* rate */
    p16(f, 1);              /* channels 1 */
    p16(f, 16);             /* bits 16 */
    p32(f, frames);
    p32(f, 32);             /* data_offset */
    p32(f, frames * 2);     /* data_size */
    p32(f, 0);              /* reserved */
    fwrite(pcm, sizeof(short), frames, f);
    if (fclose(f) != 0) {
        fprintf(stderr, "gen_sfx: write failed: %s\n", path);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : "rasterfall/assets/audio";
    static short pcm[MAX_FRAMES];   /* 64KB BSS，避免大栈 */
    struct toy_sfx sfx;
    unsigned long total_bytes = 0;
    int kind;

    for (kind = 0; kind <= TOY_SFX_PLAYER_DEATH; kind++) {
        char path[512];
        unsigned long frames;
        snprintf(path, sizeof(path), "%s/sfx_%s.tsnd", outdir, sfx_names[kind]);
        frames = render_kind(&sfx, pcm, kind);
        if (frames == 0) return 1;
        write_tsnd(path, pcm, frames);
        printf("gen_sfx: %-14s %5lu ms %7lu frames %7lu bytes\n",
               sfx_names[kind], frames * 1000 / SFX_RATE, frames, frames * 2);
        total_bytes += frames * 2;
    }
    printf("gen_sfx: wrote %d sounds to %s, %lu bytes total\n",
           TOY_SFX_MOLOTOV_BREAK + 1, outdir, total_bytes);
    return 0;
}
