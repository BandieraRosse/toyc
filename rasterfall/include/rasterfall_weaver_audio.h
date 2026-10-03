#ifndef RASTERFALL_WEAVER_AUDIO_H
#define RASTERFALL_WEAVER_AUDIO_H

#include <stdint.h>
#include "toy_mesh_weaver.h"

/* Main-thread, presentation-only input. Keep publishing with zero gain when
 * distant, so returning to the machine does not replay old task transitions.
 * paused includes the global pause and the authoritative task pause reason.
 * left/right gain is 0..256; paused machines still receive listener gains. */
struct rasterfall_weaver_audio_input {
    uint64_t world_generation;
    unsigned job_serial, produced_count;
    int enabled, phase, paused, left_q8, right_q8;
};

struct rasterfall_audio;
void rasterfall_audio_weaver(struct rasterfall_audio *audio,
    const struct rasterfall_weaver_audio_input *input);

#endif
