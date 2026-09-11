#include "core.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rf_core_filesystem.h"

int rf_core_filesystem_init(struct rf_core_filesystem *filesystem)
{
    if (!filesystem) return -1;
    memset(filesystem, 0, sizeof(*filesystem));
    filesystem->initialized = 1;
    return 0;
}

int rf_core_filesystem_read(struct rf_core_filesystem *filesystem,
                            const char *logical_path,
                            struct rf_core_file_blob *out)
{
    if (!filesystem || !filesystem->initialized || !logical_path || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->data = toy_asset_load_file(logical_path, &out->size);
    if (!out->data || !out->size) {
        if (out->data) tlibc_free(out->data);
        memset(out, 0, sizeof(*out));
        return -1;
    }
    return 0;
}

void rf_core_filesystem_release(struct rf_core_filesystem *filesystem,
                                struct rf_core_file_blob *blob)
{
    (void)filesystem;
    if (!blob) return;
    if (blob->data) tlibc_free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

void rf_core_filesystem_shutdown(struct rf_core_filesystem *filesystem)
{
    if (!filesystem) return;
    filesystem->initialized = 0;
}
