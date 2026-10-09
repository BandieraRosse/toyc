#include "tlibc_everything.h"
#include "rasterfall_options.h"
#include "rf_game_lifecycle.h"
#include "rf_numeric.h"

/* Process entry only.  Core host setup and the game loop are implemented by
 * the runtime module; keeping this translation unit deliberately boring makes
 * the ownership boundary visible to tools and to future platform ports. */
int main(int argc, char **argv)
{
    struct rasterfall_options options;
    struct rf_game_config game_config;
    int result;
    const char *numeric_diagnostics,*numeric_strict;

    rasterfall_options_init(&options,
                            rasterfall_options_default_textures_enabled());
    result = rasterfall_options_parse(&options, argc, argv);
    if (result != 0) return result < 0 ? 2 : 0;
    if (rasterfall_options_load_movement(&options)<0) return 2;
    if (rasterfall_options_load_gameplay(&options)<0) return 2;
    if (options.movement_config_check || options.gameplay_config_check) return 0;

    game_config.options = &options;
    game_config.map_path = "rasterfall/assets/maps/outpost.map";
    numeric_diagnostics=getenv("RF_NUMERIC_DIAGNOSTICS");
    numeric_strict=getenv("RF_NUMERIC_STRICT");
    rf_numeric_reset(numeric_diagnostics && numeric_diagnostics[0]=='1');
    rf_numeric_set_strict(numeric_strict && numeric_strict[0]=='1');
    result=rf_game_runtime_run(&game_config);
    rf_numeric_dump();
    return result;
}
