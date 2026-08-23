#define WIN32_LEAN_AND_MEAN
#include "toy_platform.h"
#include "toy_assets.h"

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    int i, count = 0;

    if (!paths || max <= 0) return 0;
    for (i = 0; i < toy_embedded_asset_count() && count < max; i++) {
        const char *path = toy_embedded_asset_path(i);
        const char prefix[] = "rasterfall/assets/models/";
        if (path && !strncmp(path, prefix, sizeof(prefix) - 1) &&
            strstr(path, ".rmesh")) {
            strncpy(paths[count], path, TOY_PLATFORM_PATH_MAX - 1);
            paths[count++][TOY_PLATFORM_PATH_MAX - 1] = 0;
        }
    }
    return count;
}
