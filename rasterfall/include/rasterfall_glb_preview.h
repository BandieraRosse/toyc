#ifndef TOYC_RASTERFALL_GLB_PREVIEW_H
#define TOYC_RASTERFALL_GLB_PREVIEW_H

struct rasterfall_glb_preview {
    void *implementation;
    double *positions;
    double *normals;
    unsigned int *indices;
    int vertex_count, index_count, duration_ms;
    double min[3], max[3];
};

int rasterfall_glb_preview_load(struct rasterfall_glb_preview *preview,
                                const char *path);
void rasterfall_glb_preview_unload(struct rasterfall_glb_preview *preview);
int rasterfall_glb_preview_select_animation(struct rasterfall_glb_preview *preview,
                                            const char *name);
int rasterfall_glb_preview_sample(struct rasterfall_glb_preview *preview,
                                  int time_ms);

#endif
