#ifndef RASTERFALL_RF_GAME_LIFECYCLE_H
#define RASTERFALL_RF_GAME_LIFECYCLE_H

#include "rasterfall_session.h"

/* Transitional Game-side owner.  It deliberately contains no platform
 * object; update/render callbacks will be moved behind this boundary next. */
struct rf_game {
    struct rasterfall_session *session;
};

int rf_game_init(struct rf_game *game, struct rasterfall_session *session,
                 const char *map_path);
void rf_game_shutdown(struct rf_game *game);

#endif
