#ifndef RASTERFALL_RF_CORE_FILESYSTEM_H
#define RASTERFALL_RF_CORE_FILESYSTEM_H

#include "tlibc_types.h"

/* Core owns the bytes returned by a filesystem read.  The contents are
 * intentionally opaque to Core: format-specific loaders interpret them. */
struct rf_core_file_blob {
    unsigned char *data;
    uint32_t size;
};

/* V0 service state is deliberately small.  Path resolution and embedded
 * fallback retain the existing toy_asset_load_file() platform semantics. */
struct rf_core_filesystem {
    int initialized;
};

int rf_core_filesystem_init(struct rf_core_filesystem *filesystem);
int rf_core_filesystem_read(struct rf_core_filesystem *filesystem,
                            const char *logical_path,
                            struct rf_core_file_blob *out);
void rf_core_filesystem_release(struct rf_core_filesystem *filesystem,
                                struct rf_core_file_blob *blob);
void rf_core_filesystem_shutdown(struct rf_core_filesystem *filesystem);

#endif
