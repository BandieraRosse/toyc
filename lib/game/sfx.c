/*
 * sfx.c — 程序合成 SFX 引擎（平台无关）。
 *
 * 全部音效运行时生成，无 WAV 资源：正弦 DDS（1024 项查表）+ xorshift32
 * 噪声 + 一阶低通上色，线性/平方包络衰减，8 voice 求和混音后饱和钳制
 * 输出 S16 立体声。设备无关，宿主只需周期性调用 toy_sfx_render 取样本。
 *
 * 实现约束（自托管友好）：
 *  - 扫频相位增量每 128 样本更新一次（避免每样本 64 位除法）
 *  - 禁整结构赋值（逐字段拷贝）
 */

#include "toy_game.h"
#include "string.h"
#include "math.h"
#include "tlibc_compat.h"

#define SIN_TABLE_SIZE 1024

static short sine_table[SIN_TABLE_SIZE];
static int sine_table_ready;

static void ensure_sine_table(void)
{
    int i;
    if (sine_table_ready) return;
    for (i = 0; i < SIN_TABLE_SIZE; i++) {
        double v = sin(2.0 * M_PI * (double)i / (double)SIN_TABLE_SIZE);
        sine_table[i] = (short)(v * 32767.0);
    }
    sine_table_ready = 1;
}

static int sine_at(int phase)
{
    int index = (phase >> 16) & (SIN_TABLE_SIZE - 1);
    return sine_table[index];
}

static uint32_t xorshift32(uint32_t *seed)
{
    uint32_t x = *seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return x;
}

/* 波形与包络参数（全部按 44100Hz 定标，rate 可不同但用同一套时间常数） */
struct sfx_spec {
    int len_ms;
    int vol;        /* 样本幅度 0-32768 */
    int freq0;      /* 起始频率 Hz（0 = 无正弦） */
    int freq1;      /* 扫频终点 Hz（0 = 无扫频） */
};

static const struct sfx_spec sfx_specs[TOY_SFX_PLAYER_DEATH + 1] = {
    { 100, 6000, 0,   0   },   /* GUNSHOT：噪声 + 平方衰减 */
    {  10, 3200, 0,   0   },   /* DRY_FIRE：短噪声 */
    {  35, 4000, 900, 900 },   /* RELOAD_START：双咔嗒（2ms 噪声 + 25ms 方波） */
    {   8, 3600, 900, 900 },   /* RELOAD_DONE：单咔嗒 */
    {  30, 4200, 1800, 1800 }, /* HIT_MARKER：高频短音 */
    { 200, 4600, 150, 60  },   /* KILL：下扫 */
    { 250, 3400, 100, 50  },   /* BITE：低频下扫 + 二次谐波 */
    { 600, 5200, 400, 60  },   /* PLAYER_DEATH：长下扫 */
};

static int voice_noise_sample(struct toy_sfx_voice *v)
{
    int sample;
    v->seed = xorshift32(&v->seed);
    sample = (int)(v->seed & 0xffff) - 32768;
    v->lp += (sample - v->lp) * 217 >> 8;   /* 一阶低通 ~0.85 */
    return v->lp;
}

static int voice_sine_step(struct toy_sfx_voice *v)
{
    /* 每 128 样本更新一次扫频相位增量（避免每样本 64 位除法） */
    if ((v->pos & 127) == 0) {
        long long span = (long long)v->step1 - v->step0;
        v->step = v->step0 + (int)(span * (v->pos >> 7) /
                                   (long long)((v->len + 127) >> 7));
    }
    v->phase += v->step;
    return sine_at(v->phase);
}

void toy_sfx_init(struct toy_sfx *sfx, int rate)
{
    memset(sfx, 0, sizeof(struct toy_sfx));
    sfx->rate = rate > 0 ? rate : TOY_SFX_RATE;
    sfx->enabled = 1;
    ensure_sine_table();
}

void toy_sfx_play(struct toy_sfx *sfx, int kind)
{
    struct toy_sfx_voice *v = NULL;
    int i, victim = -1, remain = 0x7fffffff;
    const struct sfx_spec *spec;
    if (!sfx || !sfx->enabled) return;
    if (kind < 0 || kind > TOY_SFX_PLAYER_DEATH) return;
    spec = &sfx_specs[kind];
    for (i = 0; i < TOY_SFX_MAX_VOICES; i++) {
        struct toy_sfx_voice *cand = &sfx->voices[i];
        if (!cand->active) { victim = i; break; }
        if (cand->len - cand->pos < remain) {
            remain = cand->len - cand->pos;
            victim = i;
        }
    }
    if (victim < 0) return;
    v = &sfx->voices[victim];
    v->active = 1;
    v->kind = kind;
    v->pos = 0;
    v->len = spec->len_ms * sfx->rate / 1000;
    v->phase = 0;
    v->step0 = (int)((long long)spec->freq0 * 65536 / sfx->rate);
    v->step1 = (int)((long long)spec->freq1 * 65536 / sfx->rate);
    v->step = v->step0;
    v->vol = spec->vol;
    v->seed = (uint32_t)(kind * 2654435761u + 12345u);
    v->lp = 0;
}

void toy_sfx_music(struct toy_sfx *sfx, int enabled)
{
    if (!sfx) return;
    sfx->music_enabled = enabled ? 1 : 0;
}

/* 低音量循环琶音：全部运行时合成，不需要音乐资源文件。 */
static void render_music(struct toy_sfx *sfx, int *left, int *right)
{
    static const int melody_hz[16] = {
        220, 262, 330, 392, 247, 294, 370, 440,
        196, 247, 330, 392, 220, 277, 330, 415
    };
    static const int bass_hz[4] = { 110, 123, 98, 110 };
    unsigned int beat_len = (unsigned int)sfx->rate / 4;
    unsigned int note = (sfx->music_pos / beat_len) & 15;
    unsigned int bar = (note >> 2) & 3;
    unsigned int within = sfx->music_pos % beat_len;
    unsigned int melody_step;
    unsigned int bass_step;
    int env;
    int melody;
    int bass;

    if (!sfx->music_enabled || beat_len == 0) return;
    melody_step = (unsigned int)((unsigned long long)melody_hz[note] *
                                 65536ULL / (unsigned int)sfx->rate);
    bass_step = (unsigned int)((unsigned long long)bass_hz[bar] *
                               65536ULL / (unsigned int)sfx->rate);
    sfx->melody_phase += melody_step;
    sfx->bass_phase += bass_step;
    env = 900 * (int)(beat_len - within) / (int)beat_len;
    melody = sine_at((int)sfx->melody_phase) * env / 32768;
    bass = sine_at((int)sfx->bass_phase) * 550 / 32768;
    *left += melody + bass;
    *right += melody * 3 / 4 + bass;
    sfx->music_pos++;
}

static int render_voice(struct toy_sfx_voice *v)
{
    int sample = 0;
    int amp, env;
    int remain = v->len - v->pos;
    if (remain <= 0) return 0;
    env = v->vol * remain / v->len;
    switch (v->kind) {
    case TOY_SFX_GUNSHOT:
        /* 噪声 + 低通 + 平方衰减（枪声锐尾） */
        amp = env * env / 32768;
        sample = voice_noise_sample(v) * amp / 32768;
        break;
    case TOY_SFX_DRY_FIRE:
        sample = voice_noise_sample(v) * env / 32768;
        break;
    case TOY_SFX_RELOAD_START:
        /* 双咔嗒：0-2ms 噪声 + 25-30ms 方波 */
        if (v->pos < v->len / 17) {
            sample = voice_noise_sample(v) * env / 32768;
        } else if (v->pos >= v->len * 25 / 35 && v->pos < v->len * 30 / 35) {
            int square = (v->phase & 0x80000000) ? env : -env;
            v->phase += v->step0;
            sample = square;
        }
        break;
    case TOY_SFX_RELOAD_DONE:
        sample = (v->phase & 0x80000000) ? env : -env;
        v->phase += v->step0;
        break;
    case TOY_SFX_HIT_MARKER:
    case TOY_SFX_KILL:
    case TOY_SFX_BITE:
    case TOY_SFX_PLAYER_DEATH:
        sample = voice_sine_step(v) * env / 32768;
        if (v->kind == TOY_SFX_BITE) {
            /* 二次谐波叠加，幅度减半 */
            int phase2 = v->phase * 2;
            int index2 = (phase2 >> 16) & (SIN_TABLE_SIZE - 1);
            sample += sine_table[index2] * env / 65536;
        }
        break;
    default:
        break;
    }
    return sample;
}

void toy_sfx_render(struct toy_sfx *sfx, short *out, int frames)
{
    int f, v;
    if (!sfx || !sfx->enabled || !out || frames <= 0) {
        if (out && frames > 0) memset(out, 0, (unsigned long)frames * 2 * 2);
        return;
    }
    for (f = 0; f < frames; f++) {
        int left = 0, right = 0;
        render_music(sfx, &left, &right);
        for (v = 0; v < TOY_SFX_MAX_VOICES; v++) {
            struct toy_sfx_voice *voice = &sfx->voices[v];
            if (!voice->active) continue;
            {
                int sample = render_voice(voice);
                left += sample;
                right += sample;
            }
            voice->pos++;
            if (voice->pos >= voice->len) voice->active = 0;
        }
        if (left > 32767) left = 32767;
        else if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        else if (right < -32768) right = -32768;
        out[2 * f] = (short)left;
        out[2 * f + 1] = (short)right;
    }
}
