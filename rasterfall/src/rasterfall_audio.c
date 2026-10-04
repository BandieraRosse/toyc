#include "tlibc_everything.h"
#include "core.h"
#include "toy_audio.h"
#include "toy_assets.h"
#include "toy_game.h"
#include "rasterfall_audio.h"

#define SFX_BLOCK_FRAMES 512

#include "rasterfall_audio_weaver.inc"

static const char *sfx_asset_names[TOY_SFX_MOLOTOV_BREAK + 1] = {
    "gunshot", "dry_fire", "reload_start", "reload_done",
    "hit_marker", "kill", "bite", "death", "shove", "shove_hit",
    "melee", "melee_hit", "smg", "shotgun", "ak", "awp", "bomb_beep",
    "bomb_explode", "molotov_break",
};

void rasterfall_audio_load_assets(struct rasterfall_audio *audio)
{
    int kind, loaded = 0;
    for (kind = 0; kind <= TOY_SFX_MOLOTOV_BREAK; kind++) {
        char path[96];
        snprintf(path, sizeof(path), "rasterfall/assets/audio/sfx_%s.tsnd",
                 sfx_asset_names[kind]);
        if (toy_sound_load(path, &audio->assets[kind]) == 0) loaded++;
    }
    __printf("rasterfall: sound assets %d/%d loaded\n",
             loaded, TOY_SFX_MOLOTOV_BREAK + 1);
}

static void audio_post_event(struct rasterfall_audio *audio, int kind)
{
    unsigned int wp = audio->event_wpos;
    if (wp - audio->event_rpos >= RASTERFALL_AUDIO_EVENT_RING) return;
    audio->events[wp & (RASTERFALL_AUDIO_EVENT_RING - 1)] = (unsigned char)kind;
    __sync_synchronize();
    audio->event_wpos = wp + 1;
}

static void audio_drain_events(struct rasterfall_audio *audio)
{
    while (audio->event_rpos != audio->event_wpos) {
        unsigned int rp = audio->event_rpos;
        int kind;
        __sync_synchronize();
        kind = audio->events[rp & (RASTERFALL_AUDIO_EVENT_RING - 1)];
        audio->event_rpos = rp + 1;
        toy_sfx_play(&audio->sfx, kind);
    }
}

static void *audio_thread_func(void *arg)
{
    struct rasterfall_audio *audio = (struct rasterfall_audio *)arg;
    short play_buf[SFX_BLOCK_FRAMES * 2];
    while (!__atomic_load_n(&audio->quit, __ATOMIC_ACQUIRE)) {
        long ret;
        audio_drain_events(audio);
        /* Quieter combat/music bed leaves room for nearby machine feedback.
         * SFX soft peak <28000 plus machine soft peak <4096 fits PCM16. */
        toy_sfx_render_gained(&audio->sfx, play_buf, SFX_BLOCK_FRAMES,192,128,28000);
        rf_weaver_audio_mix(audio->weaver, play_buf, SFX_BLOCK_FRAMES);
        ret = toy_audio_write(audio->output, play_buf, SFX_BLOCK_FRAMES);
        if (ret < 0) break;
    }
    return NULL;
}

int rasterfall_audio_start(struct rasterfall_audio *audio,
                           struct toy_audio *output)
{
    int kind;
    if (!audio || !output || audio->running) return -1;
    audio->output = output;
    __atomic_store_n(&audio->quit, 0, __ATOMIC_RELEASE);
    toy_sfx_init(&audio->sfx, TOY_SFX_RATE);
    for (kind = 0; kind <= TOY_SFX_MOLOTOV_BREAK; kind++)
        if (audio->assets[kind].blob)
            toy_sfx_set_sample(&audio->sfx, kind,
                               (const short *)audio->assets[kind].data,
                               audio->assets[kind].frames);
    toy_sfx_music(&audio->sfx, 1);
    audio->weaver = rf_weaver_audio_create(output->rate);
    if (pthread_create(&audio->thread, NULL, audio_thread_func, audio) != 0) {
        rf_weaver_audio_destroy(audio->weaver);
        audio->weaver = NULL;
        audio->output = NULL;
        return -1;
    }
    audio->running = 1;
    __printf("rasterfall: audio backend: %s\n",
             toy_audio_backend_name(audio->output));
    return 0;
}

void rasterfall_audio_stop(struct rasterfall_audio *audio)
{
    if (!audio->running) return;
    __atomic_store_n(&audio->quit, 1, __ATOMIC_RELEASE);
    pthread_join(audio->thread, NULL);
    rf_weaver_audio_destroy(audio->weaver);
    audio->weaver = NULL;
    audio->output = NULL;
    audio->running = 0;
}

void rasterfall_audio_weaver(struct rasterfall_audio *audio,
    const struct rasterfall_weaver_audio_input *input)
{
    if (audio && audio->running) rf_weaver_audio_publish(audio->weaver, input);
}

void rasterfall_audio_play_events(struct rasterfall_audio *audio,
                                  const unsigned char *events, int count)
{
    int i;
    if (count > 4) count = 4;
    for (i = 0; i < count; i++) {
        switch (events[i]) {
        case TOY_GAME_EV_SHOOT: audio_post_event(audio, TOY_SFX_GUNSHOT); break;
        case TOY_GAME_EV_DRY_FIRE: audio_post_event(audio, TOY_SFX_DRY_FIRE); break;
        case TOY_GAME_EV_RELOAD_START: audio_post_event(audio, TOY_SFX_RELOAD_START); break;
        case TOY_GAME_EV_RELOAD_DONE: audio_post_event(audio, TOY_SFX_RELOAD_DONE); break;
        case TOY_GAME_EV_KILL: audio_post_event(audio, TOY_SFX_KILL); break;
        case TOY_GAME_EV_BITE: audio_post_event(audio, TOY_SFX_BITE); break;
        case TOY_GAME_EV_PLAYER_DEATH: audio_post_event(audio, TOY_SFX_PLAYER_DEATH); break;
        case TOY_GAME_EV_SHOVE: audio_post_event(audio, TOY_SFX_SHOVE); break;
        case TOY_GAME_EV_SHOVE_HIT: audio_post_event(audio, TOY_SFX_SHOVE_HIT); break;
        case TOY_GAME_EV_MELEE: audio_post_event(audio, TOY_SFX_MELEE); break;
        case TOY_GAME_EV_MELEE_HIT: audio_post_event(audio, TOY_SFX_MELEE_HIT); break;
        case TOY_GAME_EV_SHOOT_SMG: audio_post_event(audio, TOY_SFX_SMG); break;
        case TOY_GAME_EV_SHOOT_SHOTGUN: audio_post_event(audio, TOY_SFX_SHOTGUN); break;
        case TOY_GAME_EV_SHOOT_AK: audio_post_event(audio, TOY_SFX_AK); break;
        case TOY_GAME_EV_SHOOT_AWP: audio_post_event(audio, TOY_SFX_AWP); break;
        case TOY_GAME_EV_BOMB_BEEP: audio_post_event(audio, TOY_SFX_BOMB_BEEP); break;
        case TOY_GAME_EV_BOMB_EXPLODE: audio_post_event(audio, TOY_SFX_BOMB_EXPLODE); break;
        case TOY_GAME_EV_MOLOTOV_BREAK: audio_post_event(audio, TOY_SFX_MOLOTOV_BREAK); break;
        default: break;
        }
    }
}

void rasterfall_audio_unload_assets(struct rasterfall_audio *audio)
{
    int kind;
    for (kind = 0; kind <= TOY_SFX_MOLOTOV_BREAK; kind++)
        if (audio->assets[kind].blob) toy_sound_unload(&audio->assets[kind]);
}
