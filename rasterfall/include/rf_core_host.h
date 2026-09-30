#ifndef RASTERFALL_RF_CORE_HOST_H
#define RASTERFALL_RF_CORE_HOST_H

#include "toy_audio.h"
#include "toy_input.h"
#include "toy_renderer.h"
#include "toy_window.h"
#include "rf_core_filesystem.h"
#include "rf_gpu.h"
#include "rf_gpu_graphics.h"
#include "rf_core_input.h"
#include "rf_viewmodel_contract.h"

enum rf_core_renderer {
    RF_CORE_RENDERER_CPU = 0,
    RF_CORE_RENDERER_GPU_SCENE = 1
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
    unsigned long long character_diff_frames, character_diff_vertices;
    unsigned long long character_skin_frames, character_skin_vertices;
    unsigned long long character_position_mismatches, character_normal_mismatches;
    unsigned long long character_uv_mismatches;
    unsigned int character_max_position_delta, character_max_normal_delta;
    unsigned long last_texture_commands, last_overlay_commands;
    unsigned long last_edge_commands, last_other_commands;
    unsigned long long overlay_composite_frames;
    double present_ms, frame_total_ms;
    int last_path;
    struct rf_gpu_native_present_timing native_present_timing;
};

struct rf_core_gpu_frame {
    struct rf_core_gpu_frame_stats stats;
    int64_t frame_begin_us;
    int renderer, initialized, native_present;
    int strict_gpu_only, runtime_failed;
    const char *capture_path;
    int capture_completed;
    int character_vertex_diff_requested, character_vertex_diff_completed;
    int character_skinning;
};

struct rf_core {
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input *input;
    struct toy_surface surface;
    struct toy_renderer *renderer;
    struct rf_core_filesystem filesystem;
    struct rf_gpu gpu;
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
    /* Optional observer for measured Core service initialization. */
    void (*init_event)(void *context, const char *service, int result,
                       int64_t elapsed_us);
    void *init_event_context;
};

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer);
int rf_core_init_config(struct rf_core *core,
                        const struct rf_core_config *config);
int rf_core_init_headless(struct rf_core *core, struct toy_input *input,
                          struct toy_renderer *renderer);
/* Startup presentation before Game owns a frame, including a native window. */
int rf_core_present_boot_frame(struct rf_core *core);
int rf_core_poll_events(struct rf_core *core);
int rf_core_poll_events_timeout(struct rf_core *core, int timeout_ms);
int64_t rf_core_begin_tick(struct rf_core *core);
/* Game/UI may request process termination; Core remains the lifecycle owner. */
int rf_core_request_exit(struct rf_core *core);
int rf_core_should_exit(const struct rf_core *core);
int rf_core_runtime_failed(const struct rf_core *core);
int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color);
/* Native Scene owns targets and resources; acquire extent without recording. */
int rf_core_begin_scene_frame(struct rf_core *core);
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
/* Deterministic retained-span contract fixture used by --logic-test. */
/* Feed a producer's retained commands through the real native hand-off. */
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
