#include "tlibc_everything.h"
#include "core.h"
#include "linux_audio.h"
#include "toy_audio.h"
#include "toy_assets.h"
#include "toy_game.h"
#include "rasterfall_audio.h"
#include "errno.h"

#define SFX_BLOCK_FRAMES 512

static const char *sfx_asset_names[TOY_SFX_SHOVE_HIT + 1] = {
    "gunshot", "dry_fire", "reload_start", "reload_done",
    "hit_marker", "kill", "bite", "death", "shove", "shove_hit",
};

void rasterfall_audio_load_assets(struct rasterfall_audio *audio)
{
    int kind, loaded = 0;
    for (kind = 0; kind <= TOY_SFX_SHOVE_HIT; kind++) {
        char path[96];
        snprintf(path, sizeof(path), "rasterfall/assets/audio/sfx_%s.tsnd",
                 sfx_asset_names[kind]);
        if (toy_sound_load(path, &audio->assets[kind]) == 0) loaded++;
    }
    __printf("rasterfall: sound assets %d/%d loaded\n",
             loaded, TOY_SFX_SHOVE_HIT + 1);
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
    while (!audio->quit) {
        long ret;
        audio_drain_events(audio);
        toy_sfx_render(&audio->sfx, play_buf, SFX_BLOCK_FRAMES);
        ret = toy_audio_write(&audio->output, play_buf, SFX_BLOCK_FRAMES);
        if (ret == -EPIPE && audio->output.backend == TOY_AUDIO_ALSA) {
            __ioctl(audio->output.alsa.fd, SNDRV_PCM_IOCTL_PREPARE, 0);
            continue;
        }
        if (ret < 0) break;
    }
    return NULL;
}

int rasterfall_audio_start(struct rasterfall_audio *audio)
{
    int kind;
    if (toy_audio_open(&audio->output, TOY_SFX_RATE, 2) < 0) return -1;
    toy_sfx_init(&audio->sfx, TOY_SFX_RATE);
    for (kind = 0; kind <= TOY_SFX_SHOVE_HIT; kind++)
        if (audio->assets[kind].blob)
            toy_sfx_set_sample(&audio->sfx, kind,
                               (const short *)audio->assets[kind].data,
                               audio->assets[kind].frames);
    toy_sfx_music(&audio->sfx, 1);
    if (pthread_create(&audio->thread, NULL, audio_thread_func, audio) != 0) {
        toy_audio_close(&audio->output);
        return -1;
    }
    audio->running = 1;
    __printf("rasterfall: audio backend: %s\n",
             toy_audio_backend_name(&audio->output));
    return 0;
}

void rasterfall_audio_stop(struct rasterfall_audio *audio)
{
    if (!audio->running) return;
    audio->quit = 1;
    pthread_join(audio->thread, NULL);
    toy_audio_close(&audio->output);
    audio->running = 0;
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
        default: break;
        }
    }
}

void rasterfall_audio_unload_assets(struct rasterfall_audio *audio)
{
    int kind;
    for (kind = 0; kind <= TOY_SFX_SHOVE_HIT; kind++)
        if (audio->assets[kind].blob) toy_sound_unload(&audio->assets[kind]);
}
