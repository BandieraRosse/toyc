#ifndef TOYC_RASTERFALL_MODEL_H
#define TOYC_RASTERFALL_MODEL_H

#include "toy_assets.h"
#include "toy_renderer.h"

/*
 * RFM2 is the deliberately small runtime mesh format produced by
 * app/glb2rmesh.  All integers are little-endian.  The file layout is:
 *
 *   header[64]                 magic/version/counts/scale/bounds/offsets
 *   primitive_count * 16       first index/count/material index
 *   material_count * 16        base color/metallic/roughness/texture index
 *   vertex_count * 24 bytes    x,y,z: int32; nx,ny,nz: int16; u,v: uint16
 *   index_count * 4 bytes      uint32 triangle indices
 *
 * Positions are Rasterfall world units.  Normals are signed Q15 and UVs are
 * unsigned Q16 (the exporter clamps UVs to [0, 1]). Multiple glTF primitives
 * are concatenated into one RFM2 mesh; their material assignments are stored
 * and rendered by Rasterfall's material path. Material bytes 8..11 hold a
 * texture-table index (0xffffffff means no texture). Version 4 stores PMX
 * material alpha in byte 4 (0..255). Version 5 stores the toon index in byte
 * 5 and its kind in byte 6 (0 none, 1 texture-table, 2 shared). Bytes 12..15 contain a
 * packed sphere-table index in bits 0..15 and PMX sphere mode in bits 16..17.
 * Texture files are kept beside the mesh by the offline importer.
 */
#define RASTERFALL_MODEL_MAGIC 0x324d4652U /* "RFM2" in little-endian */
#define RASTERFALL_MODEL_VERSION 5
#define RASTERFALL_MODEL_VERTEX_BYTES 24
#define RASTERFALL_MODEL_HEADER_BYTES 64
#define RASTERFALL_MODEL_PRIMITIVE_BYTES 16
#define RASTERFALL_MODEL_MATERIAL_BYTES 16

struct rasterfall_model_header {
    unsigned int magic;
    unsigned int version;
    unsigned int vertex_count;
    unsigned int index_count;
    unsigned int position_scale;
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
};

#define RASTERFALL_MODEL_MAX_GALLERY 128

struct rasterfall_model_asset {
    const unsigned char *data;
    int data_size;
    unsigned int format_version;
    unsigned int vertex_count;
    unsigned int index_count;
    unsigned int primitive_count;
    unsigned int material_count;
    const unsigned char *primitives;
    const unsigned char *materials;
    const unsigned char *vertices;
    const unsigned char *indices;
    struct toy_texture_asset *texture_assets;
    struct toy_texture_view *texture_views;
    unsigned int texture_count;
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
};

int rasterfall_model_load(struct rasterfall_model_asset *asset,
                           const char *path);
void rasterfall_model_unload(struct rasterfall_model_asset *asset);

#endif
