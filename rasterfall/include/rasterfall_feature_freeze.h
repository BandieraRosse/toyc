#ifndef RASTERFALL_FEATURE_FREEZE_H
#define RASTERFALL_FEATURE_FREEZE_H

/* Normal-runtime boundary while the GPU renderer is frozen.  The legacy
 * implementations remain available to isolated diagnostics, but they do not
 * contribute presentation semantics to a normal frame. */
#define RASTERFALL_LEGACY_ANIME_RENDERING_ENABLED 0
#define RASTERFALL_DESKTOP_RUNTIME_ENABLED 0

#define RASTERFALL_DESKTOP_UNAVAILABLE_MESSAGE \
    "DESKTOP TEMPORARILY UNAVAILABLE"
#define RASTERFALL_CONSOLE_UNAVAILABLE_MESSAGE \
    "CONSOLE TEMPORARILY UNAVAILABLE"

#endif
