#ifndef RASTERFALL_RENDER_H
#define RASTERFALL_RENDER_H

#include "toy_renderer.h"
#include "toy_assets.h"
#include "rasterfall_session.h"
#include "rasterfall_effects.h"
#include "rasterfall_net.h"

#define RASTERFALL_BAKED_LM_W 32
#define RASTERFALL_BAKED_LM_H 24

struct rasterfall_render_context {
    struct rasterfall_session *session;
    struct rasterfall_effects *effects;
    const struct rasterfall_net *net;
    struct toy_texture_view *wall_texture;
    unsigned short lightmap[RASTERFALL_BAKED_LM_W *
                            RASTERFALL_BAKED_LM_H];
    int textures_enabled;
    int fixed_floor_lighting;
};

void rasterfall_render_bind(struct rasterfall_render_context *ctx);
void rasterfall_render_bake_lightmap(void);

int rasterfall_render_scene(struct toy_renderer *renderer,
                            const struct camera *camera);
void rasterfall_render_sign_text(struct toy_surface *surface,
                                 const struct camera *camera);
void rasterfall_render_gallery_selection(struct toy_surface *surface,
                                         const struct camera *camera);
int rasterfall_render_interactables(struct toy_renderer *renderer,
                                    const struct camera *camera);
int rasterfall_render_enemies(struct toy_renderer *renderer,
                              const struct camera *camera);
int rasterfall_render_ai_teammate(struct toy_renderer *renderer,
                                  const struct camera *camera);
int rasterfall_render_network_teammate(struct toy_renderer *renderer,
                                       const struct camera *camera,
                                       const struct rasterfall_net *net);
void rasterfall_render_ai_teammate_name(struct toy_renderer *renderer,
                                        const struct camera *camera);
void rasterfall_render_network_teammate_status(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_net *net);
int rasterfall_render_tracers(struct toy_renderer *renderer,
                              const struct camera *camera);
int rasterfall_render_particles(struct toy_renderer *renderer,
                                const struct camera *camera);

#endif
