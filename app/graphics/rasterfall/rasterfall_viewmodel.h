#ifndef RASTERFALL_VIEWMODEL_H
#define RASTERFALL_VIEWMODEL_H

#include "toy_game.h"
#include "toy_renderer.h"
#include "rasterfall_effects.h"

#define VIEWMODEL_KICK_MAX 110

int rasterfall_viewmodel_weapon(const struct toy_game *game);
void rasterfall_viewmodel_muzzle_offset(int weapon, int kick,
                                        int *x, int *y, int *z);
int rasterfall_viewmodel_render(struct toy_renderer *renderer,
                                const struct toy_game *game,
                                const struct rasterfall_effects *effects);

#endif
