#include "tlibc_everything.h"
#include "rasterfall_options.h"
#include "rf_game_lifecycle.h"

/* Process entry only.  Core host setup and the game loop are implemented by
 * the runtime module; keeping this translation unit deliberately boring makes
 * the ownership boundary visible to tools and to future platform ports. */
int main(int argc, char **argv)
{
    struct rasterfall_options options;
    struct rf_game_config game_config;
    int result;

    rasterfall_options_init(&options,
                            rasterfall_options_default_textures_enabled());
    result = rasterfall_options_parse(&options, argc, argv);
    if (result != 0) return result < 0 ? 2 : 0;

    game_config.options = &options;
    game_config.map_path = "rasterfall/assets/maps/outpost.map";
    return rf_game_runtime_run(&game_config);
}
