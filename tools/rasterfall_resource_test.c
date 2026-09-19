/* Hosted lifecycle integration tool. Run from a Rasterfall asset root.
 * Uses real world loading, static prop Draw/reference and renderer resizing;
 * native window/swapchain coverage remains a separate GPU smoke test. */
#include "tlibc_everything.h"
#include "rf_core_host.h"
#include "rf_game_lifecycle.h"
#include "rasterfall_prop.h"
#include "rasterfall_render_resources.h"

int main(void)
{
    struct rf_game_runtime *game = tlibc_malloc(sizeof(*game));
    struct rasterfall_session *session = tlibc_malloc(sizeof(*session));
    struct rf_core *core = tlibc_malloc(sizeof(*core));
    struct rasterfall_resource_registry *registry = rasterfall_render_resources();
    struct rasterfall_resource_stats stats;
    struct rasterfall_resource_handle before, after, missing;
    struct rasterfall_prop_instance prop = {RASTERFALL_PROP_ASSET_ARCH_BEAM,
        0, -900, 300, 0, 1000, 0};
    const struct rasterfall_prop_asset_profile *profile = rasterfall_prop_asset_profile(prop.asset_id);
    struct toy_renderer renderer;
    struct toy_surface surface;
    struct camera camera;
    uint32_t pixels[128 * 128];
    unsigned int i, loads;
    int failure = 0, initialized = 0;
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0);
#define CHECK(x) do { if (!(x)) { failure = __LINE__; goto done; } } while (0)
    CHECK(game && session && core && profile);
    memset(game, 0, sizeof(*game)); memset(session, 0, sizeof(*session));
    memset(core, 0, sizeof(*core)); memset(&camera, 0, sizeof(camera));
    memset(&surface, 0, sizeof(surface));
    surface.pixels = pixels;
    camera.z = -2000; camera.y = -350; camera.cy = camera.pitch_cy = 1024;
    CHECK(rf_game_init(game, core, session, "rasterfall/assets/maps/outpost.map") == 0);
    initialized = 1;
    for (i = 0; i < 12; ++i) {
        surface.width = surface.height = (i & 1) ? 128 : 96;
        surface.stride = surface.width * 4;
        CHECK(toy_renderer_begin(&renderer, &surface, 0) == 0);
        CHECK(rasterfall_resources_frame_begin(registry) == 0);
        CHECK(rasterfall_render_static_prop(&renderer, &camera, &prop) >= 0);
        CHECK(rasterfall_resources_load(registry, profile->model_path, &before) == 0);
        CHECK(rasterfall_resources_pin(registry, before) == 0);
        loads = registry->loads;
        CHECK(rf_game_request_world(game, (enum rasterfall_world_id)999) < 0);
        CHECK(rasterfall_resources_resolve(registry, before) != NULL);
        CHECK(rf_game_request_world(game, (i & 1) ? RASTERFALL_WORLD_OUTPOST :
            RASTERFALL_WORLD_CAMPAIGN_01) == 0);
        CHECK(rasterfall_resources_resolve(registry, before) != NULL);
        CHECK(rasterfall_render_static_prop(&renderer, &camera, &prop) >= 0);
        CHECK(rasterfall_resources_load(registry, profile->model_path, &after) == 0);
        CHECK(after.slot != before.slot && registry->loads == loads + 1);
        CHECK(toy_renderer_flush(&renderer) >= 0);
        rasterfall_resources_frame_complete(registry);
        CHECK(!rasterfall_resources_resolve(registry, before));
        rasterfall_resources_stats(registry, &stats);
        CHECK(stats.live == 1 && !stats.retired && !stats.pinned);
        /* Pure extent change must reuse the mesh generation. */
        surface.width = surface.height = (i & 1) ? 96 : 128;
        surface.stride = surface.width * 4;
        CHECK(toy_renderer_begin(&renderer, &surface, 0) == 0);
        CHECK(rasterfall_resources_frame_begin(registry) == 0);
        CHECK(rasterfall_render_static_prop(&renderer, &camera, &prop) >= 0);
        CHECK(rasterfall_resources_load(registry, profile->model_path, &before) == 0);
        CHECK(before.slot == after.slot && before.generation == after.generation);
        CHECK(toy_renderer_flush(&renderer) >= 0);
        rasterfall_resources_frame_complete(registry);
    }
    CHECK(renderer.submitted_triangles > 0);
    CHECK(rasterfall_resources_load(registry, "rasterfall/assets/models/hg1b-missing.rmesh", &missing) < 0);
    CHECK(!missing.generation);
    loads = registry->loads;
    CHECK(rasterfall_resources_load(registry, "rasterfall/assets/models/hg1b-missing.rmesh", &missing) < 0);
    CHECK(registry->loads == loads);
    CHECK(rasterfall_resources_frame_begin(registry) == 0);
    CHECK(rasterfall_resources_pin(registry, after) == 0);
    rf_game_shutdown(game); initialized = 0;
    CHECK(rasterfall_resources_resolve(registry, after) != NULL);
    rasterfall_resources_frame_complete(registry);
    rasterfall_resources_stats(registry, &stats);
    CHECK(!stats.live && !stats.retired && !stats.pinned && !stats.failed);
done:
    toy_renderer_destroy(&renderer);
    if (initialized) rf_game_shutdown(game);
    rasterfall_resources_invalidate(registry);
    rasterfall_resources_frame_complete(registry);
    tlibc_free(game); tlibc_free(session); tlibc_free(core);
    __printf("resource-world-lifecycle: %s line=%d\n", failure ? "FAIL" : "PASS", failure);
    return failure != 0;
#undef CHECK
}
