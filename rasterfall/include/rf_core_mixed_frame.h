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
};
struct rf_core_mixed_span {
    unsigned int kind, layer;
    unsigned long first, count;
};
struct rf_core_mixed_frame {
    struct toy_raster_cmd *raster;
    struct rf_core_mixed_draw *draws;
    struct rf_core_mixed_span *spans;
    unsigned long raster_count, raster_capacity, draw_count, draw_capacity;
    unsigned long span_count, span_capacity;
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
int rf_core_mixed_draw(struct rf_core_mixed_frame *frame,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item);
/* Stable partition across the COMPLETE WORLD, including Draw positions.
 * Allocation/rejection leaves the record unchanged and available to replay. */
int rf_core_mixed_freeze(struct rf_core_mixed_frame *frame);

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
