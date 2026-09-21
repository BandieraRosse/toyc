#ifndef RF_CORE_MIXED_FRAME_H
#define RF_CORE_MIXED_FRAME_H

#include "rf_core_host.h"
#include "rasterfall_draw.h"

/* HG-2B ordered frame preparation. This does not enable hardware producers.
 * The caller owns the registry frame and all non-Draw RasterCmd textures until
 * synchronous execution/replay completes. Draw backing is pinned here. */
enum rf_core_mixed_kind { RF_CORE_MIXED_RASTER, RF_CORE_MIXED_DRAW };
enum rf_core_mixed_state {
    RF_CORE_MIXED_EMPTY, RF_CORE_MIXED_RECORDING, RF_CORE_MIXED_FROZEN,
    RF_CORE_MIXED_EXECUTING, RF_CORE_MIXED_COMPLETE, RF_CORE_MIXED_FAILED
};
struct rf_core_mixed_draw {
    struct rasterfall_draw_view view;
    struct rasterfall_draw_instance instance;
    struct rasterfall_draw_item item;
    unsigned long dynamic_first_vertex;
    unsigned int dynamic_vertex_count;
    unsigned long reference_first_vertex;
    unsigned int reference_vertex_count;
    unsigned long skin_first_vertex;
    unsigned int skin_vertex_count, skin_instance;
};
struct rf_core_mixed_skin_instance {
    unsigned long first_palette_bone;
    unsigned int palette_bone_count;
};
struct rf_core_mixed_span {
    unsigned int kind, layer;
    unsigned long first, count;
};
struct rf_core_mixed_frame {
    struct toy_raster_cmd *raster;
    struct rf_core_mixed_draw *draws;
    struct rf_core_mixed_span *spans;
    struct rasterfall_dynamic_draw_vertex *dynamic_vertices;
    struct rasterfall_skinned_draw_vertex *skin_vertices;
    struct rasterfall_model_skin_palette_bone *skin_palette;
    struct rf_core_mixed_skin_instance *skin_instances;
    unsigned long raster_count, raster_capacity, draw_count, draw_capacity;
    unsigned long required_draw_count;
    unsigned long span_count, span_capacity;
    unsigned long dynamic_vertex_count;
    unsigned long reference_vertex_count, reference_vertex_capacity;
    unsigned long skin_vertex_count, skin_vertex_capacity;
    unsigned long skin_palette_count, skin_palette_capacity;
    unsigned long skin_instance_count, skin_instance_capacity;
    struct rasterfall_resource_registry *registry;
    unsigned long long registry_epoch;
    int width, height;
    unsigned int state, last_layer;
};

/* Zero initialize before first use. Reset does not release registry pins:
 * only its frame owner may declare GPU/replay completion. */
void rf_core_mixed_reset(struct rf_core_mixed_frame *frame);
void rf_core_mixed_destroy(struct rf_core_mixed_frame *frame);
int rf_core_mixed_begin(struct rf_core_mixed_frame *frame,
    struct rasterfall_resource_registry *registry, int width, int height);
/* WORLD transparency is classified per command. Explicit TRANSPARENT, SKY,
 * OVERLAY and backwards layer submissions are rejected. Clear/sky, Post and
 * overlay belong to the executor, outside this pre-Post record. */
int rf_core_mixed_raster(struct rf_core_mixed_frame *frame, unsigned int layer,
    const struct toy_raster_cmd *commands, unsigned long count);
/* toy_renderer WORLD consumer for normal producer interleaving. */
int rf_core_mixed_world_consume(struct toy_renderer *renderer,
    const struct toy_raster_cmd *commands, int count, void *context);
int rf_core_mixed_draw(struct rf_core_mixed_frame *frame,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item);
int rf_core_mixed_dynamic_draw(struct rf_core_mixed_frame *frame,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item,
    const struct rasterfall_dynamic_draw_vertex *vertices,
    unsigned int vertex_count);
int rf_core_mixed_skin_instance(struct rf_core_mixed_frame *frame,
    const struct rasterfall_model_skin_palette_bone *palette,
    unsigned int palette_count, unsigned int *instance_index);
int rf_core_mixed_skinned_draw(struct rf_core_mixed_frame *frame,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item,
    const struct rasterfall_dynamic_draw_vertex *reference_vertices,
    const struct rasterfall_skinned_draw_vertex *bind_vertices,
    unsigned int vertex_count, unsigned int skin_instance);
/* Strict producers declare each hardware Draw before selecting a consumer.
 * Freeze rejects a missing Draw, including an unexpected reference lowering. */
int rf_core_mixed_require_draws(struct rf_core_mixed_frame *frame,
    unsigned long count);
/* Stable partition across the COMPLETE WORLD, including Draw positions.
 * Allocation/rejection leaves the record unchanged and available to replay. */
int rf_core_mixed_freeze(struct rf_core_mixed_frame *frame);
/* Reuse normal Core Raster V1 eligibility before a hosted packer reads texels. */
int rf_core_mixed_raster_preflight(const struct rf_core_mixed_frame *frame);

/* Backend checks the entire immutable plan (resources, numeric eligibility,
 * pack, target and terminal operations) before any execute call. A rejected
 * preflight leaves FROZEN for explicit replay; an execution failure poisons
 * the frame, prohibiting retry against a partially modified GPU target.
 * Callbacks must not mutate/reenter the frame or complete its registry. */
struct rf_core_mixed_executor {
    int (*preflight)(void *context, const struct rf_core_mixed_frame *frame);
    int (*span)(void *context, const struct rf_core_mixed_frame *frame,
                const struct rf_core_mixed_span *span);
    /* Exactly once, including an empty frame: finish VIEWMODEL/Post/overlay
     * and output according to the backend's separately validated contract. */
    int (*finish)(void *context, const struct rf_core_mixed_frame *frame);
};
int rf_core_mixed_execute(struct rf_core_mixed_frame *frame,
    const struct rf_core_mixed_executor *executor, void *context);
int rf_core_mixed_frame_logic_test(void);

#endif
