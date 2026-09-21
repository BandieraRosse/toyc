#ifndef RASTERFALL_RF_CORE_HOST_H
#define RASTERFALL_RF_CORE_HOST_H

#include "toy_audio.h"
#include "toy_input.h"
#include "toy_renderer.h"
#include "toy_window.h"
#include "rf_core_filesystem.h"
#include "rf_gpu.h"
#include "rf_gpu_raster_pack.h"
#include "rf_core_input.h"
#include "rf_viewmodel_contract.h"

struct rf_core_mixed_frame;
struct rf_gpu_mixed_executor;

enum rf_core_renderer {
    RF_CORE_RENDERER_CPU = 0,
    RF_CORE_RENDERER_GPU_COMPUTE = 1
};

enum rf_render_layer_v1 {
    RF_RENDER_LAYER_SKY = 0,
    RF_RENDER_LAYER_WORLD,
    RF_RENDER_LAYER_TRANSPARENT,
    RF_RENDER_LAYER_EFFECTS,
    RF_RENDER_LAYER_VIEWMODEL,
    RF_RENDER_LAYER_OVERLAY,
    RF_RENDER_LAYER_COUNT
};

enum rf_render_layer_backend_v1 {
    RF_RENDER_BACKEND_UNSET = 0,
    RF_RENDER_BACKEND_GPU,
    RF_RENDER_BACKEND_CPU,
    RF_RENDER_BACKEND_COMPOSITE,
    RF_RENDER_BACKEND_UNSUPPORTED
};

enum rf_pre_post_fallback_reason_v1 {
    RF_PRE_POST_FALLBACK_NONE = 0,
    /* Kept for source compatibility with the first Transparent V1 draft.
     * A supported transparent command never sets this bit; unsupported
     * material/texture is reported by the explicit bits below. */
    RF_PRE_POST_FALLBACK_TRANSPARENT = 1U << 0,
    RF_PRE_POST_FALLBACK_EFFECTS_DIRECT_PIXELS = 1U << 1,
    RF_PRE_POST_FALLBACK_VIEWMODEL_COMMANDS = 1U << 2,
    RF_PRE_POST_FALLBACK_VIEWMODEL_DIRECT_PIXELS = 1U << 3,
    RF_PRE_POST_FALLBACK_UNSUPPORTED_GENERIC_COMMAND = 1U << 4,
    RF_PRE_POST_FALLBACK_CONSUMER_FAILURE = 1U << 5,
    RF_PRE_POST_FALLBACK_UNSUPPORTED_MATERIAL = 1U << 6,
    RF_PRE_POST_FALLBACK_UNSUPPORTED_TEXTURE = 1U << 7,
    RF_PRE_POST_FALLBACK_UNSUPPORTED_EDGE = 1U << 8,
    RF_PRE_POST_FALLBACK_UNSUPPORTED_OVERLAY = 1U << 9,
    /* Compatibility spelling; new diagnostics should use the explicit
     * generic-command name above. */
    RF_PRE_POST_FALLBACK_UNSUPPORTED_COMMAND =
        RF_PRE_POST_FALLBACK_UNSUPPORTED_GENERIC_COMMAND
};

struct rf_render_frame_v1 {
    unsigned long long frame_id;
    int camera_x, camera_z;
    int direction_sy, direction_cy;
    int pitch_sy, pitch_cy;
    int width, height;
    unsigned long command_count[RF_RENDER_LAYER_COUNT];
    unsigned long pixel_count[RF_RENDER_LAYER_COUNT];
    unsigned long direct_pixel_count[RF_RENDER_LAYER_COUNT];
    unsigned int layer_backend[RF_RENDER_LAYER_COUNT];
    unsigned int current_layer;
    unsigned int invalid_layer_transitions;
    unsigned long retained_pre_post_commands;
    unsigned int pre_post_cpu_fallback;
    unsigned int pre_post_fallback_reason;
    int sky_enabled;
    int viewmodel_near_z;
    unsigned long viewmodel_coverage_pixels;
};

struct rf_core_gpu_frame_stats {
    unsigned long long frames_attempted, gpu_frames, cpu_fallback_frames;
    unsigned long long mixed_draws;
    unsigned long long mixed_raster_spans, mixed_draw_spans;
    unsigned long long mixed_bridge_transfers, mixed_bridge_bytes;
    unsigned long long mixed_graphics_submits, mixed_graphics_waits;
    unsigned long long mixed_gpu_upload_bytes;
    double mixed_graphics_submit_ms, mixed_graphics_wait_ms, mixed_bridge_ms;
    double mixed_freeze_ms, mixed_cache_collect_ms, mixed_preflight_ms;
    double mixed_texture_measure_ms, mixed_pack_ms, mixed_draw_encode_ms;
    double mixed_draw_batch_prepare_ms, mixed_graphics_draw_ms;
    double mixed_raster_segment_ms;
    double mixed_gpu_raster_ms, mixed_gpu_bridge_import_ms;
    double mixed_gpu_draw_ms, mixed_gpu_bridge_export_ms;
    double mixed_gpu_post_ms, mixed_gpu_overlay_ms, mixed_gpu_present_copy_ms;
    unsigned int mixed_gpu_timing_supported, mixed_gpu_timing_valid;
    unsigned long long mixed_gpu_timing_frame;
    unsigned long long capture_readback_bytes;
    unsigned long long character_diff_frames, character_diff_vertices;
    unsigned long long character_skin_frames, character_skin_vertices;
    unsigned long long character_position_mismatches, character_normal_mismatches;
    unsigned long long character_uv_mismatches;
    unsigned int character_max_position_delta, character_max_normal_delta;
    unsigned long long unsupported_texture, unsupported_transparent;
    unsigned long long unsupported_overlay, unsupported_edge, unsupported_other;
    unsigned long long texture_commands, texture_upload_bytes;
    unsigned long long overlay_upload_bytes, overlay_composite_frames;
    unsigned long long oracle_frames, oracle_failures;
    unsigned long long oracle_color_mismatches, oracle_depth_mismatches;
    unsigned int oracle_max_color_delta;
    unsigned long long oracle_max_depth_delta;
    unsigned int unique_textures;
    double frontend_ms, classification_ms, texture_measure_ms;
    double raster_abi_pack_ms, texture_table_build_ms;
    double cpu_oracle_ms, presentation_copy_ms, present_ms, frame_total_ms;
    double overlay_cpu_draw_ms;
    unsigned long last_commands, last_texture_commands;
    unsigned long last_transparent_commands, last_overlay_commands;
    unsigned long last_edge_commands, last_other_commands;
    int last_path;
    struct rf_gpu_raster_timing last_timing;
    struct rf_gpu_native_present_timing native_present_timing;
};

struct rf_core_gpu_frame {
    struct rf_gpu_raster raster;
    unsigned char *stream;
    unsigned long stream_capacity;
    struct rf_gpu_texture_desc_v1 *texture_descs;
    const struct toy_texture_view **texture_views;
    unsigned int texture_desc_capacity;
    unsigned char *texture_texels;
    unsigned long texture_texel_capacity;
    unsigned int *oracle_color;
    int *oracle_depth;
    unsigned long oracle_pixel_capacity;
    struct toy_surface overlay_surface;
    unsigned char *overlay_coverage;
    unsigned long overlay_pixel_capacity;
    struct toy_raster_cmd *retained_commands;
    unsigned long retained_command_capacity;
    unsigned long retained_command_count;
    /* WORLD flushes are first retained as one raw contiguous prefix.  The
     * prefix is partitioned once at finalize, never per flush. */
    unsigned long retained_world_raw_count;
    unsigned long retained_batch_count[RF_RENDER_LAYER_COUNT];
    int64_t overlay_draw_begin_us;
    unsigned long native_stream_size, native_texture_bytes;
    unsigned int native_texture_count;
    struct rf_core_gpu_frame_stats stats;
    int64_t frontend_begin_us, frame_begin_us;
    int renderer, armed, initialized, native_present, native_presented;
    /* RF_GPU_POLICY_REQUIRED is a frame-lifetime contract, not merely an
     * initialization preference.  Once set, no CPU replay/present is legal. */
    int strict_gpu_only, runtime_failed;
    int native_prepared, overlay_active;
    int retaining_pre_post;
    const char *capture_path;
    int capture_completed;
    int character_vertex_diff_requested, character_vertex_diff_completed;
    int character_skinning;
};

/* The single V0 Core context.  The game may borrow the objects through the
 * accessors, but does not own their lifetime. */
struct rf_core {
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input *input;
    struct toy_surface surface;
    struct toy_renderer *renderer;
    struct rf_core_filesystem filesystem;
    struct rf_gpu gpu;
    struct rf_core_mixed_frame *mixed_frame;
    struct rf_gpu_mixed_executor *mixed_executor;
    struct rf_gpu_post_params_v1 mixed_post;
    uint32_t mixed_clear_color;
    struct rf_core_gpu_frame gpu_frame;
    struct rf_render_frame_v1 render_frame;
    int *viewmodel_depth;
    unsigned char *viewmodel_coverage;
    unsigned long viewmodel_pixel_capacity;
    unsigned long viewmodel_pixel_count;
    int *world_depth;
    int viewmodel_active;
    struct toy_audio audio;
    int audio_ready;
    int exit_requested;
    int initialized;
};

/* Public, read-only snapshot of Core availability.  The snapshot deliberately
 * contains no service pointers or implementation-owned objects. */
#define RF_CORE_VERSION "0.2"
#define RF_CORE_BUILD "RF Core Runtime V0.2"

struct rf_core_status {
    const char *version;
    const char *build;
    int initialized;
    int window_ready;
    int renderer_ready;
    int filesystem_ready;
    int audio_ready;
    int clock_ready;
    int gpu_ready;
    int gpu_policy;
    int gpu_state;
    int renderer_mode;
    unsigned long long gpu_frames_attempted;
    unsigned long long gpu_frames_rendered;
    unsigned long long gpu_frames_fallback;
    int native_present_ready, screen_overlay_composite_ready;
    unsigned int overlay_upload_bytes_per_frame;
    unsigned long long overlay_composite_frames;
};

/* Compatibility name for the future public context spelling.  This is an
 * alias, not a second ownership container. */
typedef struct rf_core rf_core_context;

struct rf_core_config {
    const char *title;
    int width;
    int height;
    struct toy_input *input;
    struct toy_renderer *renderer;
    enum rf_gpu_policy gpu_policy;
    enum rf_core_renderer renderer_mode;
    const struct rf_gpu_backend *gpu_backend;
    void *gpu_backend_context;
    int native_present;
    int gpu_post_fog;
};

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer);
int rf_core_init_config(struct rf_core *core,
                        const struct rf_core_config *config);
int rf_core_init_headless(struct rf_core *core, struct toy_input *input,
                          struct toy_renderer *renderer);
int rf_core_poll_events(struct rf_core *core);
int rf_core_poll_events_timeout(struct rf_core *core, int timeout_ms);
int64_t rf_core_begin_tick(struct rf_core *core);
/* Game/UI may request process termination; Core remains the lifecycle owner. */
int rf_core_request_exit(struct rf_core *core);
int rf_core_should_exit(const struct rf_core *core);
int rf_core_runtime_failed(const struct rf_core *core);
struct rf_core_mixed_frame *rf_core_mixed_current(struct rf_core *core);
void rf_core_mixed_fail(struct rf_core *core);
int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color);
void rf_core_render_frame_begin_v1(struct rf_core *core, int camera_x,
                                  int camera_z, int direction_sy,
                                  int direction_cy, int pitch_sy,
                                  int pitch_cy);
void rf_core_render_frame_record_v1(struct rf_core *core,
                                    enum rf_render_layer_v1 layer,
                                    unsigned long commands,
                                    unsigned long pixels);
void rf_core_render_frame_record_direct_pixels_v1(
    struct rf_core *core, enum rf_render_layer_v1 layer,
    unsigned long pixels);
/* Core-owned independent depth domain and Post coverage for VIEWMODEL. */
int rf_core_viewmodel_begin_v1(struct rf_core *core);
int rf_core_viewmodel_end_v1(struct rf_core *core);
const int *rf_core_viewmodel_depth_v1(const struct rf_core *core);
const unsigned char *rf_core_viewmodel_coverage_v1(const struct rf_core *core);
unsigned long rf_core_viewmodel_coverage_count_v1(const struct rf_core *core);
/* Classify one pending world batch into the opaque world and transparent
 * RenderFrame layers without changing renderer command order. */
void rf_core_render_frame_record_world_v1(
    struct rf_core *core, const struct toy_raster_cmd *commands, int count);
/* Advance the frame submission cursor by exactly one layer. The current
 * layer may be revisited; skipping or moving backwards is invalid. */
int rf_core_render_frame_enter_layer_v1(
    struct rf_core *core, enum rf_render_layer_v1 layer);
int rf_core_get_render_frame_v1(const struct rf_core *core,
                                struct rf_render_frame_v1 *frame);
unsigned int rf_core_render_frame_fallback_reason_v1(
    const struct rf_render_frame_v1 *frame);
/* Core-owned submission point for layered rendering within one frame. */
int rf_core_flush(struct rf_core *core);
int rf_core_end_frame(struct rf_core *core);
/* Returns the normal software surface on the CPU backend, and a cleared
 * Core-owned color+coverage overlay surface on native GPU presentation. */
struct toy_surface *rf_core_begin_screen_overlay(struct rf_core *core);
void rf_core_gpu_world_begin(struct rf_core *core);
void rf_core_gpu_world_flush(struct rf_core *core);
/* Deterministic retained-span contract fixture used by --logic-test. */
int rf_core_retained_span_logic_test_v1(void);
int rf_core_mixed_frame_logic_test(void);
/* Feed a producer's retained commands through the real native hand-off. */
int rf_core_transparent_command_logic_test_v1(
    const struct toy_raster_cmd *commands, int count);
int rf_core_get_gpu_frame_stats(const struct rf_core *core,
                                struct rf_core_gpu_frame_stats *stats);
const char *rf_core_renderer_name(int renderer);
void rf_core_shutdown(struct rf_core *core);
int64_t rf_core_time_us(struct rf_core *core);
/* Clock service entry for pre-host diagnostics that have no Core instance. */
int64_t rf_core_clock_now_us(void);
int rf_core_get_input_frame(const struct rf_core *core,
                            struct rf_input_frame *frame);
int rf_core_get_status(const struct rf_core *core,
                       struct rf_core_status *status);
int rf_core_get_gpu_status(const struct rf_core *core,
                           struct rf_gpu_status *status);

struct toy_window *rf_core_window(struct rf_core *core);
struct toy_window_events *rf_core_events(struct rf_core *core);
struct toy_input *rf_core_input(struct rf_core *core);
struct toy_surface *rf_core_surface(struct rf_core *core);
struct toy_renderer *rf_core_renderer(struct rf_core *core);
struct rf_core_filesystem *rf_core_filesystem_service(struct rf_core *core);
struct toy_audio *rf_core_audio(struct rf_core *core);
int rf_core_audio_ready(const struct rf_core *core);
int rf_core_set_pointer_lock(struct rf_core *core, int locked);
int rf_core_move_window(struct rf_core *core, uint32_t serial);
void rf_core_reset_input(struct rf_core *core);

#endif
