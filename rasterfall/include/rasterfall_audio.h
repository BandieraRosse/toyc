#ifndef RASTERFALL_AUDIO_H
#define RASTERFALL_AUDIO_H

#include "toy_audio.h"
#include "toy_assets.h"
#include "toy_game.h"
#include "pthread.h"

#define RASTERFALL_AUDIO_EVENT_RING 32

struct rasterfall_audio {
    struct toy_audio output;
    struct toy_sfx sfx;
    unsigned char events[RASTERFALL_AUDIO_EVENT_RING];
    volatile unsigned int event_wpos;
    volatile unsigned int event_rpos;
    pthread_t thread;
    volatile int quit;
    int running;
    /* Include shove and melee effects as well as the original clips. */
    struct toy_sound_asset assets[TOY_SFX_MOLOTOV_BREAK + 1];
};

void rasterfall_audio_load_assets(struct rasterfall_audio *audio);
int rasterfall_audio_start(struct rasterfall_audio *audio);
void rasterfall_audio_stop(struct rasterfall_audio *audio);
void rasterfall_audio_play_events(struct rasterfall_audio *audio,
                                  const unsigned char *events, int count);
void rasterfall_audio_unload_assets(struct rasterfall_audio *audio);

#endif
