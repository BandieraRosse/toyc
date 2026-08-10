#ifndef TOYC_RASTERFALL_MODEL_H
#define TOYC_RASTERFALL_MODEL_H

/*
 * RFM2 is the deliberately small runtime mesh format produced by
 * app/glb2rmesh.  All integers are little-endian.  The file layout is:
 *
 *   header[64]                 magic/version/counts/scale/bounds/offsets
 *   primitive_count * 16       first index/count/material index
 *   material_count * 16        base color/metallic/roughness
 *   vertex_count * 24 bytes    x,y,z: int32; nx,ny,nz: int16; u,v: uint16
 *   index_count * 4 bytes      uint32 triangle indices
 *
 * Positions are Rasterfall world units.  Normals are signed Q15 and UVs are
 * unsigned Q16 (the exporter clamps UVs to [0, 1]). Multiple glTF primitives
 * are concatenated into one RFM2 mesh; their material assignments are stored
 * and rendered by Rasterfall's pure-color material path. Image textures
 * are deliberately not part of RFM2 yet.
 */
#define RASTERFALL_MODEL_MAGIC 0x324d4652U /* "RFM2" in little-endian */
#define RASTERFALL_MODEL_VERSION 2
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

#define RASTERFALL_MODEL_MAX_GALLERY 22

struct rasterfall_model_asset {
    const unsigned char *data;
    int data_size;
    unsigned int vertex_count;
    unsigned int index_count;
    unsigned int primitive_count;
    unsigned int material_count;
    const unsigned char *primitives;
    const unsigned char *materials;
    const unsigned char *vertices;
    const unsigned char *indices;
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
};

int rasterfall_model_load(struct rasterfall_model_asset *asset,
                           const char *path);
void rasterfall_model_unload(struct rasterfall_model_asset *asset);

#endif
