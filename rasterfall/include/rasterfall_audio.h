#ifndef RASTERFALL_AUDIO_H
#define RASTERFALL_AUDIO_H

#include "toy_audio.h"
#include "toy_assets.h"
#include "toy_game.h"
#include "pthread.h"
#include "rasterfall_weaver_audio.h"

#define RASTERFALL_AUDIO_EVENT_RING 32

struct rasterfall_weaver_audio_bus;

struct rasterfall_audio {
    /* Audio-thread-owned short UI oscillator, triggered through event ring. */
    int ui_kind, ui_remaining, ui_length, ui_phase;
    struct toy_audio *output;
    struct toy_sfx sfx;
    unsigned char events[RASTERFALL_AUDIO_EVENT_RING];
    volatile unsigned int event_wpos;
    volatile unsigned int event_rpos;
    pthread_t thread;
    volatile int quit;
    int running;
    /* Private presentation bus: publish on main, mix on the audio thread. */
    struct rasterfall_weaver_audio_bus *weaver;
    /* Include shove and melee effects as well as the original clips. */
    struct toy_sound_asset assets[TOY_SFX_MOLOTOV_BREAK + 1];
};

enum rasterfall_ui_sound { RF_UI_SOUND_HOVER, RF_UI_SOUND_CLICK,
    RF_UI_SOUND_CHANGE, RF_UI_SOUND_CONFIRM, RF_UI_SOUND_EXIT };
void rasterfall_audio_ui(struct rasterfall_audio *audio,int kind);

void rasterfall_audio_load_assets(struct rasterfall_audio *audio);
int rasterfall_audio_start(struct rasterfall_audio *audio,
                           struct toy_audio *output);
void rasterfall_audio_stop(struct rasterfall_audio *audio);
void rasterfall_audio_play_events(struct rasterfall_audio *audio,
                                  const unsigned char *events, int count);
void rasterfall_audio_unload_assets(struct rasterfall_audio *audio);

#endif
