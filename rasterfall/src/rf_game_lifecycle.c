#include "tlibc_everything.h"
#include "rf_game_lifecycle.h"

int rf_game_init(struct rf_game *game, struct rasterfall_session *session,
                 const char *map_path)
{
    if (!game || !session || !map_path) return -1;
    game->session = session;
    if (rasterfall_session_load(session, map_path) < 0) {
        game->session = NULL;
        return -1;
    }
    return 0;
}

void rf_game_shutdown(struct rf_game *game)
{
    if (!game || !game->session) return;
    rasterfall_session_unload(game->session);
    game->session = NULL;
}
