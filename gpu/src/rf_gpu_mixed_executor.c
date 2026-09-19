#include "rf_gpu_mixed_executor.h"
#include "rf_gpu_raster_pack.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct encoded_draw { struct rf_gpu_graphics_draw draw; uint32_t texture; };
struct rf_gpu_mixed_executor {
    struct rf_gpu *gpu;
    struct rf_gpu_vulkan_context *context;
    struct rasterfall_resource_registry *registry;
    struct rf_gpu_graphics *graphics;
    struct rf_gpu_resource_cache *cache;
    struct rf_gpu_raster raster;
    struct rf_gpu_mixed_output output;
    struct rf_gpu_mixed_stats stats;
    struct encoded_draw *draws;
    struct rf_gpu_texture_resources_v1 textures;
    void *stream;
    size_t stream_size;
    uint32_t cursor, pending_end, command_count;
    int started;
};

static void release_plan(struct rf_gpu_mixed_executor *e)
{
    free(e->draws); free(e->stream);
    free(e->textures.descs); free(e->textures.views); free(e->textures.texels);
    e->draws = NULL; e->stream = NULL;
    memset(&e->textures, 0, sizeof(e->textures));
}
static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static int encode_draw(struct rf_gpu_mixed_executor *e,
    const struct rf_core_mixed_frame *f, unsigned long index)
{
    const struct rf_core_mixed_draw *src = &f->draws[index];
    const struct rasterfall_draw_instance *i = &src->instance;
    const struct rasterfall_draw_material *m = &src->item.material;
    const struct rasterfall_model_asset *mesh = i->mesh;
    const struct camera *c = &src->view.camera;
    struct encoded_draw *out = &e->draws[index];
    struct rf_gpu_graphics_draw *d = &out->draw;
    struct rf_gpu_cached_submesh info;
    const unsigned char *primitive;
    if (m->ambient || m->specular || (m->color & 0xff000000U) ||
        (m->double_sided != 0 && m->double_sided != 1)) return -1;
    out->texture = RF_GPU_CACHE_FLAT_TEXTURE;
    if (m->texture) {
        for (uint32_t n = 0; n < mesh->textures.count; ++n)
            if (m->texture == &mesh->textures.views[n]) { out->texture = n; break; }
        if (out->texture == RF_GPU_CACHE_FLAT_TEXTURE) return -1;
    }
    /* prepare validates backing ranges before the primitive dereference. */
    if (rf_gpu_resource_cache_prepare(e->cache, f->registry_epoch,
        i->mesh_handle, src->item.primitive, out->texture, &info) < 0) return -1;
    primitive = mesh->primitives + src->item.primitive * RASTERFALL_MODEL_PRIMITIVE_BYTES;
    if (src->item.first_index != read32(primitive) ||
        src->item.index_count != info.index_count) return -1;
    memset(d, 0, sizeof(*d));
    d->translation_scale[0]=i->x; d->translation_scale[1]=i->y;
    d->translation_scale[2]=i->z; d->translation_scale[3]=i->scale_milli;
    d->rotation[0]=i->yaw_sin_q10; d->rotation[1]=i->yaw_cos_q10;
    d->rotation[2]=mesh->min_y; d->rotation[3]=i->form_lighting;
    d->camera[0]=c->x; d->camera[1]=c->y; d->camera[2]=c->z;
    d->view[0]=c->sy; d->view[1]=c->cy; d->view[2]=c->pitch_sy; d->view[3]=c->pitch_cy;
    d->projection[0]=f->width; d->projection[1]=f->height;
    d->projection[2]=src->view.near_z; d->projection[3]=src->view.focal;
    d->material[0]=m->color; d->material[1]=i->scene_light_q8;
    d->material[2]=m->texture != NULL;
    d->texture[0]=info.texture_width; d->texture[1]=info.texture_height;
    d->index_count=info.index_count;
    d->double_sided=!i->force_backface_culling && m->double_sided;
    d->integer_depth=1;
    return rf_gpu_resource_cache_bind(e->cache, f->registry_epoch, i->mesh_handle,
        src->item.primitive, out->texture) < 0 ? -1 :
        rf_gpu_graphics_validate_draw(e->graphics, d);
}
static int preflight(void *context, const struct rf_core_mixed_frame *f)
{
    struct rf_gpu_mixed_executor *e=context;
    struct toy_renderer renderer;
    struct toy_raster_cmd *ordered=NULL;
    uint32_t transparent=UINT32_MAX, viewmodel=UINT32_MAX, count=0, unique=0;
    size_t bytes=0, capacity;
    int result=-1;
    release_plan(e);
    e->started=0; e->cursor=0; e->pending_end=2;
    if (f->registry != e->registry || f->raster_count > INT_MAX-4 ||
        f->draw_count > ULONG_MAX/sizeof(*e->draws) ||
        (e->output.strict_native && !e->output.present_timing) ||
        (e->output.present_timing && !e->gpu->info.capabilities.native_presentation_v1) ||
        (e->output.present_timing ?
            (e->output.color || e->output.depth || !e->output.overlay_color ||
             !e->output.overlay_coverage ||
             e->output.overlay_stride < (unsigned)f->width ||
             e->output.coverage_stride < (unsigned)f->width) :
            (!e->output.color || !e->output.depth ||
             e->output.color_stride < (unsigned)f->width ||
             e->output.depth_stride < (unsigned)f->width)) ||
        rf_core_mixed_raster_preflight(f) < 0) return -1;
    if (rf_gpu_raster_resize(e->gpu, &e->raster, f->width, f->height) < 0 ||
        rf_gpu_graphics_resize(e->graphics, f->width, f->height) < 0 ||
        rf_gpu_raster_set_post(&e->raster, &e->output.post) < 0) return -1;
    ordered=calloc(f->raster_count ? f->raster_count : 1, sizeof(*ordered));
    e->draws=calloc(f->draw_count ? f->draw_count : 1, sizeof(*e->draws));
    if (!ordered || !e->draws) goto done;
    for (unsigned long n=0; n<f->span_count; ++n) {
        const struct rf_core_mixed_span *s=&f->spans[n];
        if (s->kind != RF_CORE_MIXED_RASTER) continue;
        if (s->layer == RF_RENDER_LAYER_TRANSPARENT && transparent == UINT32_MAX) transparent=count;
        if (s->layer == RF_RENDER_LAYER_VIEWMODEL && viewmodel == UINT32_MAX) viewmodel=count;
        memcpy(ordered+count, f->raster+s->first, s->count*sizeof(*ordered));
        count+=(uint32_t)s->count;
    }
    memset(&renderer,0,sizeof(renderer));
    renderer.surface.width=f->width; renderer.surface.height=f->height;
    renderer.cmds=ordered; renderer.cmd_count=count;
    if (rf_gpu_raster_measure_textures_toy_v1(&renderer,&unique,&bytes)<0 || bytes>ULONG_MAX) goto done;
    e->textures.descs=calloc(unique ? unique : 1,sizeof(*e->textures.descs));
    e->textures.views=calloc(unique ? unique : 1,sizeof(*e->textures.views));
    e->textures.texels=malloc(bytes ? bytes : 1);
    e->textures.desc_capacity=e->textures.view_capacity=unique;
    e->textures.texel_capacity=bytes;
    capacity=rf_gpu_raster_stream_size_v1(count+4);
    if (!capacity || capacity>ULONG_MAX || !e->textures.descs || !e->textures.views ||
        !e->textures.texels || !(e->stream=malloc(capacity))) goto done;
    if (rf_gpu_raster_pack_toy_textured_spans_v2(&renderer,e->output.clear_color,0,
        e->stream,capacity,&e->stream_size,&e->textures,transparent,viewmodel)<0) goto done;
    e->command_count=((struct rf_gpu_raster_stream_header_v1 *)e->stream)->command_count;
    /* All WORLD inverse depths must survive the integer attachment bridge.
     * VIEWMODEL is independent and never exported to graphics. */
    if (f->draw_count) {
        const struct rf_gpu_raster_cmd_v1 *cmd=(const void *)((const struct rf_gpu_raster_stream_header_v1 *)e->stream+1);
        for (uint32_t n=2;n<e->command_count;++n) {
            if (cmd[n].kind==RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1) break;
            if (cmd[n].kind>=3 && cmd[n].kind<=5) {
                const struct rf_gpu_raster_flat_triangle_v1 *t=&cmd[n].payload.flat_triangle;
                if (t->a.inv_z<0 || t->a.inv_z>16384 || t->b.inv_z<0 ||
                    t->b.inv_z>16384 || t->c.inv_z<0 || t->c.inv_z>16384) goto done;
            }
        }
    }
    if (rf_gpu_vulkan_raster_preflight(e->context,e->raster.implementation,e->stream,
        (unsigned long)e->stream_size,e->textures.descs,e->textures.desc_count,
        e->textures.texels,(unsigned long)e->textures.texel_size,f->width,f->height)<0) goto done;
    for (unsigned long n=0;n<f->draw_count;++n) if (encode_draw(e,f,n)<0) goto done;
    result=0;
done:
    free(ordered);
    return result;
}
static int segment(struct rf_gpu_mixed_executor *e, const struct rf_core_mixed_frame *f, int final)
{
    char message[RF_GPU_MESSAGE_CAPACITY];
    uint32_t end=final ? e->command_count : e->pending_end;
    if (e->started && e->cursor==end && !final) return 0;
    if (final && e->output.present_timing) {
        if (rf_gpu_vulkan_raster_segment_present(e->context,e->raster.implementation,e->stream,
            (unsigned long)e->stream_size,e->textures.descs,e->textures.desc_count,e->textures.texels,
            (unsigned long)e->textures.texel_size,e->cursor,end,
            e->started ? RF_GPU_RASTER_LOAD_EXISTING : RF_GPU_RASTER_CLEAR,
            e->output.overlay_color,e->output.overlay_coverage,
            e->output.overlay_stride,e->output.coverage_stride,f->width,f->height,
            e->output.present_timing,e->output.capture_color,message,sizeof(message))<0) return -1;
        /* The backend reports these after presentation. Treat a violation as
         * an execution failure so Core cannot retry a partly submitted frame. */
        if (e->output.strict_native &&
            (e->output.present_timing->color_readback_bytes ||
             e->output.present_timing->cpu_framebuffer_copy_bytes ||
             e->output.present_timing->width != (unsigned)f->width ||
             e->output.present_timing->height != (unsigned)f->height)) return -1;
    } else if (rf_gpu_vulkan_raster_segment(e->context,e->raster.implementation,e->stream,
        (unsigned long)e->stream_size,e->textures.descs,e->textures.desc_count,e->textures.texels,
        (unsigned long)e->textures.texel_size,e->cursor,end,
        e->started ? RF_GPU_RASTER_LOAD_EXISTING : RF_GPU_RASTER_CLEAR,final,
        final ? e->output.color : NULL,final ? e->output.depth : NULL,
        f->width,f->height,e->output.color_stride,e->output.depth_stride,message,sizeof(message))<0) return -1;
    if (!e->started) e->stats.clears++;
    e->started=1; e->cursor=end; e->stats.raster_segments++;
    return 0;
}
static int span(void *context, const struct rf_core_mixed_frame *f, const struct rf_core_mixed_span *s)
{
    struct rf_gpu_mixed_executor *e=context;
    struct rf_gpu_graphics_batch_item *batch;
    int result;
    if (s->kind==RF_CORE_MIXED_RASTER) {
        /* No graphics follow transparent/effects/viewmodel: leave that entire
         * suffix to finish, including its markers and independent depth. */
        if (s->layer==RF_RENDER_LAYER_WORLD) e->pending_end+=(uint32_t)s->count;
        return 0;
    }
    if (!s->count) return 0;
    if (s->count > 65536 || s->count > SIZE_MAX/sizeof(*batch)) return -1;
    batch = malloc(s->count*sizeof(*batch));
    if (!batch) return -1;
    for (unsigned long n=s->first;n<s->first+s->count;++n) {
        const struct rf_core_mixed_draw *d=&f->draws[n];
        batch[n-s->first].resource=rf_gpu_resource_cache_resource(e->cache,
            f->registry_epoch,d->instance.mesh_handle,d->item.primitive,e->draws[n].texture);
        batch[n-s->first].draw=e->draws[n].draw;
        if (!batch[n-s->first].resource) { free(batch); return -1; }
    }
    result=segment(e,f,0);
    if (result==0) result=rf_gpu_graphics_raster_batch(e->graphics,
        e->raster.implementation,batch,(uint32_t)s->count);
    free(batch);
    if (result<0) return -1;
    e->stats.draw_spans++;
    e->stats.draws+=s->count;
    return 0;
}
static int finish(void *context, const struct rf_core_mixed_frame *f)
{
    struct rf_gpu_mixed_executor *e=context;
    if (segment(e,f,1)<0) return -1;
    e->stats.finishes++;
    if (!e->output.present_timing) e->stats.readback_bytes+=(uint64_t)f->width*f->height*8;
    if (e->output.capture_color) e->stats.readback_bytes+=(uint64_t)f->width*f->height*4;
    return 0;
}
struct rf_gpu_mixed_executor *rf_gpu_mixed_create(struct rf_gpu *gpu,
    struct rf_gpu_vulkan_context *context, struct rasterfall_resource_registry *registry)
{
    struct rf_gpu_mixed_executor *e;
    if (!gpu || !context || !registry || gpu->backend!=&rf_gpu_vulkan_backend ||
        gpu->backend_context!=context) return NULL;
    e=calloc(1,sizeof(*e));
    if (!e) return NULL;
    e->gpu=gpu; e->context=context; e->registry=registry;
    e->graphics=rf_gpu_graphics_create(context);
    if (!e->graphics || !(e->cache=rf_gpu_resource_cache_create(e->graphics,registry)) ||
        rf_gpu_raster_init(gpu,&e->raster,1,1)<0) { rf_gpu_mixed_destroy(e); return NULL; }
    return e;
}
int rf_gpu_mixed_render(struct rf_gpu_mixed_executor *e,
    struct rf_core_mixed_frame *f, const struct rf_gpu_mixed_output *output)
{
    static const struct rf_core_mixed_executor executor={preflight,span,finish};
    if (!e || !f || !output) return -1;
    e->output=*output;
    rf_gpu_resource_cache_collect(e->cache);
    return rf_core_mixed_execute(f,&executor,e);
}
void rf_gpu_mixed_get_stats(struct rf_gpu_mixed_executor *e, struct rf_gpu_mixed_stats *stats)
{
    if (!e || !stats) return;
    *stats=e->stats; rf_gpu_graphics_get_stats(e->graphics,&stats->graphics);
}
void rf_gpu_mixed_destroy(struct rf_gpu_mixed_executor *e)
{
    if (!e) return;
    release_plan(e); rf_gpu_raster_shutdown(&e->raster);
    rf_gpu_resource_cache_destroy(e->cache); rf_gpu_graphics_destroy(e->graphics); free(e);
}
