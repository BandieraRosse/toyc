#ifndef RASTERFALL_ENEMY_VISUAL_H
#define RASTERFALL_ENEMY_VISUAL_H

/* Local presentation selection. Never serialized or stored in toy_game. */
enum rasterfall_enemy_visual_family {
    RASTERFALL_ENEMY_VISUAL_LEGACY,
    RASTERFALL_ENEMY_VISUAL_BLOCK_INFECTED,
    RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED
};
void rasterfall_render_set_enemy_visual_family(int family);
int rasterfall_render_enemy_visual_capture(const char *output);
#endif
