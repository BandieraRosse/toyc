#ifndef TOYC_TOY_ASSETS_H
#define TOYC_TOY_ASSETS_H

#include "tlibc_types.h"

#define TOY_ASSET_VERSION 1

struct toy_texture_asset { const unsigned char *blob, *data; uint32_t width, height; uint32_t data_size, channels; };
struct toy_sound_asset { const unsigned char *blob, *data; uint32_t rate, channels, frames, data_size; };
struct toy_mesh_vertex { int32_t x, y, z; uint32_t color; };
struct toy_mesh_asset { const unsigned char *blob, *vertices, *indices; uint32_t vertex_count, index_count; };

const unsigned char *toy_embedded_asset_find(const char *path, uint32_t *size);
int toy_embedded_asset_count(void);
const char *toy_embedded_asset_path(int index);
unsigned char *toy_asset_load_file(const char *path, uint32_t *size);

int toy_texture_load(const char *path, struct toy_texture_asset *out);
int toy_sound_load(const char *path, struct toy_sound_asset *out);
int toy_mesh_load(const char *path, struct toy_mesh_asset *out);
void toy_texture_unload(struct toy_texture_asset *asset);
void toy_sound_unload(struct toy_sound_asset *asset);
void toy_mesh_unload(struct toy_mesh_asset *asset);

#endif
