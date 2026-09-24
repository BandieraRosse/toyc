#ifndef RASTERFALL_ENEMY_VISUAL_H
#define RASTERFALL_ENEMY_VISUAL_H

#include <stdint.h>

/* Presentation-owned history. Zero on source/world replacement; a slot number
 * alone cannot identify a lifetime. Sampling never reads a clock or game array. */
struct rasterfall_infected_motion {
    int valid, x, z, phase, moving;
    uint64_t moved_us, sampled_us;
};
struct rasterfall_infected_sample {
    struct rasterfall_infected_motion next;
    int swing;
};
int rasterfall_infected_sample_motion(
    const struct rasterfall_infected_motion *previous,
    int active, int x, int z, uint64_t now_us,
    struct rasterfall_infected_sample *out);

/* Local presentation selection. Never serialized or stored in toy_game. */
enum rasterfall_enemy_visual_family {
    RASTERFALL_ENEMY_VISUAL_AUTO = -1,
    RASTERFALL_ENEMY_VISUAL_LEGACY,
    RASTERFALL_ENEMY_VISUAL_BLOCK_INFECTED,
    RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED
};
void rasterfall_render_set_enemy_visual_family(int family);
int rasterfall_render_enemy_visual_capture(const char *output);
#endif
