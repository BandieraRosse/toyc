#include "rf_gpu_mixed_executor.h"
#include "rf_gpu_raster_pack.h"
#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

_Static_assert(sizeof(struct rasterfall_dynamic_draw_vertex) ==
    sizeof(struct rf_gpu_graphics_vertex), "dynamic Draw vertex ABI");

static double mixed_now_ms(void)
{
    struct timespec value;
    if (__clock_gettime(CLOCK_MONOTONIC, &value) < 0) return 0.0;
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

struct encoded_draw {
    struct rf_gpu_graphics_draw draw;
    struct rf_gpu_graphics_resource *dynamic_resource;
    struct rf_gpu_graphics_resource *resource;
    uint32_t texture;
};
#define RF_GPU_MIXED_FRAMES 2
struct rf_gpu_mixed_executor {
    struct rf_gpu *gpu;
    struct rf_gpu_vulkan_context *context;
    struct rasterfall_resource_registry *registry;
    struct rf_gpu_graphics *graphics[RF_GPU_MIXED_FRAMES];
    /* Extent targets and command state are frame-slot local. Immutable model
     * resources are device-stable and must not be uploaded once per slot. */
    struct rf_gpu_resource_cache *cache;
    struct rf_gpu_raster raster[RF_GPU_MIXED_FRAMES];
    unsigned int active_frame, next_frame;
    struct rasterfall_resource_handle *pins[RF_GPU_MIXED_FRAMES];
    size_t pin_count[RF_GPU_MIXED_FRAMES], pin_capacity[RF_GPU_MIXED_FRAMES];
    struct rf_gpu_graphics_resource **dynamic[RF_GPU_MIXED_FRAMES];
    size_t dynamic_count[RF_GPU_MIXED_FRAMES], dynamic_capacity[RF_GPU_MIXED_FRAMES];
    uint64_t submitted_frame[RF_GPU_MIXED_FRAMES], next_frame_number;
    struct rf_gpu_mixed_gpu_timing completed_timing;
    struct rf_gpu_mixed_output output;
    struct rf_gpu_mixed_stats stats;
    struct encoded_draw *draws;
    struct toy_raster_cmd *ordered;
    struct rf_gpu_graphics_batch_item *batch;
    struct rf_gpu_texture_resources_v1 textures;
    void *stream;
    size_t stream_size;
    size_t draw_capacity, ordered_capacity, batch_capacity, stream_capacity;
    uint32_t cursor, pending_end, command_count;
    int started;
};

static struct rf_gpu_graphics *mixed_graphics(struct rf_gpu_mixed_executor *e)
{ return e->graphics[e->active_frame]; }
static struct rf_gpu_resource_cache *mixed_cache(struct rf_gpu_mixed_executor *e)
{ return e->cache; }
static struct rf_gpu_raster *mixed_raster(struct rf_gpu_mixed_executor *e)
{ return &e->raster[e->active_frame]; }
static void mixed_release_pins(struct rf_gpu_mixed_executor *e, unsigned int frame)
{
    for (size_t n=0;n<e->pin_count[frame];++n)
        rasterfall_resources_unpin(e->registry,e->pins[frame][n]);
    e->pin_count[frame]=0;
}
static void mixed_release_dynamic(struct rf_gpu_mixed_executor *e, unsigned int frame)
{
    for (size_t n=0;n<e->dynamic_count[frame];++n)
        rf_gpu_graphics_resource_destroy(e->graphics[frame],e->dynamic[frame][n]);
    e->dynamic_count[frame]=0;
}

static void release_plan(struct rf_gpu_mixed_executor *e)
{
    free(e->draws); free(e->ordered); free(e->batch); free(e->stream);
    free(e->textures.descs); free(e->textures.views); free(e->textures.texels);
    e->draws = NULL; e->ordered = NULL; e->batch = NULL; e->stream = NULL;
    e->draw_capacity = e->ordered_capacity = e->batch_capacity = 0;
    e->stream_capacity = e->stream_size = 0;
    memset(&e->textures, 0, sizeof(e->textures));
}
static int reserve(void **memory, size_t *capacity, size_t count, size_t item_size)
{
    void *grown;
    size_t next;
    if (count <= *capacity) return 0;
    if (!item_size || count > SIZE_MAX / item_size) return -1;
    next = *capacity ? *capacity : 16;
    while (next < count) {
        if (next > SIZE_MAX / 2) { next = count; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / item_size) next = count;
    grown = realloc(*memory, next * item_size);
    if (!grown) return -1;
    *memory = grown;
    *capacity = next;
    return 0;
}
static int reserve_u32(void **memory, uint32_t *capacity,
                       uint32_t count, size_t item_size)
{
    size_t widened = *capacity;
    if (reserve(memory, &widened, count, item_size) < 0 || widened > UINT32_MAX)
        return -1;
    *capacity = (uint32_t)widened;
    return 0;
}
static void reset_plan(struct rf_gpu_mixed_executor *e)
{
    e->stream_size = 0;
    e->textures.desc_count = 0;
    e->textures.view_count = 0;
    e->textures.texel_size = 0;
}
static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits,&value,sizeof(bits));
    return bits;
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
    if ((!i->vertex_light_q8 && (m->ambient || m->specular)) ||
        (m->color & 0xff000000U) ||
        (m->double_sided != 0 && m->double_sided != 1)) return -1;
    out->texture = RF_GPU_CACHE_FLAT_TEXTURE;
    out->dynamic_resource = NULL;
    out->resource = NULL;
    if (m->texture) {
        for (uint32_t n = 0; n < mesh->textures.count; ++n)
            if (m->texture == &mesh->textures.views[n]) { out->texture = n; break; }
        if (out->texture == RF_GPU_CACHE_FLAT_TEXTURE) return -1;
    }
    if (src->dynamic_vertex_count) {
        if (m->texture || src->dynamic_vertex_count != src->item.index_count ||
            !e->dynamic_count[e->active_frame]) return -1;
        out->dynamic_resource=e->dynamic[e->active_frame][0];
        info.index_count=src->dynamic_vertex_count;
        info.texture_width=info.texture_height=1;
        out->resource=out->dynamic_resource;
    } else {
    /* prepare validates backing ranges before the primitive dereference. */
    if (rf_gpu_resource_cache_prepare(mixed_cache(e), f->registry_epoch,
        i->mesh_handle, src->item.primitive, out->texture, &info) < 0) return -1;
    if (!src->dynamic_vertex_count)
        out->resource=rf_gpu_resource_cache_resource(mixed_cache(e),f->registry_epoch,
            i->mesh_handle,src->item.primitive,out->texture);
    primitive = mesh->primitives + src->item.primitive * RASTERFALL_MODEL_PRIMITIVE_BYTES;
    if (src->item.first_index != read32(primitive) ||
        src->item.index_count != info.index_count) return -1;
    }
    memset(d, 0, sizeof(*d));
    d->translation_scale[0]=i->x+(i->vertex_light_q8 ? (int32_t)m->ambient : 0);
    d->translation_scale[1]=i->y;
    d->translation_scale[2]=i->z+(i->vertex_light_q8 ? (int32_t)m->specular : 0);
    d->translation_scale[3]=i->scale_milli;
    d->rotation[0]=i->yaw_sin_q10; d->rotation[1]=i->yaw_cos_q10;
    d->rotation[2]=mesh->min_y; d->rotation[3]=i->form_lighting;
    d->camera[0]=c->x; d->camera[1]=c->y; d->camera[2]=c->z;
    d->view[0]=c->sy; d->view[1]=c->cy; d->view[2]=c->pitch_sy; d->view[3]=c->pitch_cy;
    d->projection[0]=f->width; d->projection[1]=f->height;
    d->projection[2]=src->view.near_z; d->projection[3]=src->view.focal;
    d->material[0]=m->color; d->material[1]=i->scene_light_q8;
    d->material[2]=m->texture != NULL;
    d->material[3]=i->vertex_light_q8 != 0;
    d->texture[0]=info.texture_width; d->texture[1]=info.texture_height;
    d->first_index=src->dynamic_vertex_count ?
        (uint32_t)src->dynamic_first_vertex : 0;
    d->index_count=info.index_count;
    d->double_sided=!i->force_backface_culling && m->double_sided;
    d->integer_depth=1;
    {
    int bind_result=(src->dynamic_vertex_count ?
        rf_gpu_graphics_resource_bind(mixed_graphics(e),out->dynamic_resource) < 0 :
        rf_gpu_graphics_resource_bind(mixed_graphics(e),out->resource) < 0) ? -1 : 0;
    int validate_result=bind_result<0 ? -1 : (src->dynamic_vertex_count ?
            rf_gpu_graphics_validate_dynamic_draw(mixed_graphics(e), d) :
            rf_gpu_graphics_validate_draw(mixed_graphics(e), d));
    return validate_result;
    }
}
static int preflight(void *context, const struct rf_core_mixed_frame *f)
{
    struct rf_gpu_mixed_executor *e=context;
    struct toy_renderer renderer;
    uint32_t transparent=UINT32_MAX, viewmodel=UINT32_MAX, count=0, unique=0;
    size_t bytes=0, capacity;
    double preflight_start=mixed_now_ms(), phase_start;
    int result=-1;
    reset_plan(e);
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
    if (rf_gpu_vulkan_raster_recycle(mixed_raster(e)->implementation)!=0)
        return -1;
    if (e->submitted_frame[e->active_frame]) {
        rf_gpu_vulkan_mixed_gpu_timing(mixed_raster(e)->implementation,
            &e->completed_timing);
        e->completed_timing.frame_number=e->submitted_frame[e->active_frame];
        e->submitted_frame[e->active_frame]=0;
    }
    mixed_release_dynamic(e,e->active_frame);
    if (reserve((void **)&e->pins[e->active_frame],
            &e->pin_capacity[e->active_frame],f->draw_count ? f->draw_count : 1,
            sizeof(*e->pins[e->active_frame]))<0 ||
        reserve((void **)&e->dynamic[e->active_frame],
            &e->dynamic_capacity[e->active_frame],f->draw_count ? f->draw_count : 1,
            sizeof(*e->dynamic[e->active_frame]))<0 ||
        rf_gpu_raster_resize(e->gpu, mixed_raster(e), f->width, f->height) < 0 ||
        rf_gpu_graphics_resize(mixed_graphics(e), f->width, f->height) < 0 ||
        rf_gpu_graphics_share_color(mixed_graphics(e),mixed_raster(e)->implementation) < 0 ||
        rf_gpu_raster_set_post(mixed_raster(e), &e->output.post) < 0) return -1;
    if (f->dynamic_vertex_count) {
        uint32_t *indices, white=0xffffff;
        uint32_t *bind_words=NULL,*palette_words=NULL;
        if (f->dynamic_vertex_count>UINT32_MAX) return -1;
        indices=malloc((size_t)f->dynamic_vertex_count*sizeof(*indices));
        if (!indices) return -1;
        for (uint32_t n=0;n<(uint32_t)f->dynamic_vertex_count;++n) indices[n]=n;
        if (e->output.character_skinning) {
            if (f->skin_vertex_count!=f->dynamic_vertex_count || !f->skin_palette_count ||
                f->skin_palette_count>65535 || f->skin_palette_count>UINT32_MAX/15) {
                free(indices); return -1;
            }
            bind_words=calloc((size_t)f->dynamic_vertex_count*22,sizeof(*bind_words));
            palette_words=malloc((size_t)f->skin_palette_count*15*sizeof(*palette_words));
            if (!bind_words || !palette_words) { free(bind_words); free(palette_words); free(indices); return -1; }
            for (unsigned long n=0;n<f->skin_palette_count;++n) {
                const struct rasterfall_model_skin_palette_bone *p=&f->skin_palette[n];
                uint32_t *out=palette_words+n*15;
                for(unsigned k=0;k<9;++k) out[k]=float_bits((float)p->rotation[k]);
                for(unsigned k=0;k<3;++k) out[9+k]=float_bits((float)p->position[k]);
                for(unsigned k=0;k<3;++k) out[12+k]=(uint32_t)p->rest[k];
            }
            for (unsigned long d=0;d<f->draw_count;++d) if (f->draws[d].skin_vertex_count) {
                const struct rf_core_mixed_draw *draw=&f->draws[d];
                const struct rf_core_mixed_skin_instance *instance=&f->skin_instances[draw->skin_instance];
                for (unsigned int v=0;v<draw->skin_vertex_count;++v) {
                    const struct rasterfall_skinned_draw_vertex *src=&f->skin_vertices[draw->skin_first_vertex+v];
                    uint32_t *out=bind_words+(draw->dynamic_first_vertex+v)*22;
                    for(unsigned k=0;k<3;++k) out[k]=(uint32_t)src->position[k];
                    for(unsigned k=0;k<2;++k) out[3+k]=(uint32_t)src->uv[k];
                    for(unsigned k=0;k<9;++k) out[5+k]=(uint32_t)src->normals[k];
                    for(unsigned k=0;k<4;++k) {
                        const struct rasterfall_skin_influence *in=&src->influences[k];
                        uint32_t b0=(uint32_t)instance->first_palette_bone+in->bone0;
                        uint32_t b1=(uint32_t)instance->first_palette_bone+in->bone1;
                        out[14+k*2]=(b0&65535u)|(b1<<16);
                        out[15+k*2]=in->weight|((uint32_t)(in->type|
                            (draw->instance.mesh->animation.pose==RASTERFALL_MODEL_POSE_BIND ? 0x100 : 0))<<16);
                    }
                }
            }
            e->dynamic[e->active_frame][0]=rf_gpu_graphics_skinned_resource_create(
                mixed_graphics(e),e->output.character_vertex_diff ?
                    (const struct rf_gpu_graphics_vertex *)f->dynamic_vertices : NULL,
                (uint32_t)f->dynamic_vertex_count,indices,(uint32_t)f->dynamic_vertex_count,
                bind_words,(uint32_t)f->dynamic_vertex_count*22,palette_words,
                (uint32_t)f->skin_palette_count*15,&white,1,1);
            free(bind_words); free(palette_words);
        } else {
            if (f->reference_vertex_count!=f->dynamic_vertex_count) {
                free(indices); return -1;
            }
            e->dynamic[e->active_frame][0]=rf_gpu_graphics_resource_create(
                mixed_graphics(e),(const struct rf_gpu_graphics_vertex *)f->dynamic_vertices,
                (uint32_t)f->dynamic_vertex_count,indices,
                (uint32_t)f->dynamic_vertex_count,&white,1,1);
        }
        free(indices);
        if (!e->dynamic[e->active_frame][0]) return -1;
        if (rf_gpu_graphics_resource_set_frame_dynamic(
                e->dynamic[e->active_frame][0]) < 0) return -1;
        if (e->output.character_skinning) {
            e->stats.character_skin_frames++;
            e->stats.character_skin_vertices+=f->dynamic_vertex_count;
        }
        e->dynamic_count[e->active_frame]=1;
        if (e->output.character_vertex_diff) {
            uint64_t p=0,n=0,uv=0;
            uint32_t max_p=0,max_n=0;
            if (f->reference_vertex_count!=f->dynamic_vertex_count) return -1;
            if (rf_gpu_graphics_resource_diff_vertices(mixed_graphics(e),
                    e->dynamic[e->active_frame][0],
                    (const struct rf_gpu_graphics_vertex *)f->dynamic_vertices,
                    (uint32_t)f->dynamic_vertex_count,&p,&n,&uv,&max_p,&max_n)<0)
                return -1;
            e->stats.character_diff_frames++;
            e->stats.character_diff_vertices+=f->dynamic_vertex_count;
            e->stats.character_position_mismatches+=p;
            e->stats.character_normal_mismatches+=n;
            e->stats.character_uv_mismatches+=uv;
            if (max_p>e->stats.character_max_position_delta)
                e->stats.character_max_position_delta=max_p;
            if (max_n>e->stats.character_max_normal_delta)
                e->stats.character_max_normal_delta=max_n;
            e->stats.readback_bytes+=(uint64_t)f->dynamic_vertex_count*
                sizeof(struct rf_gpu_graphics_vertex);
        }
    }
    if (reserve((void **)&e->ordered, &e->ordered_capacity,
            f->raster_count ? f->raster_count : 1, sizeof(*e->ordered)) < 0 ||
        reserve((void **)&e->draws, &e->draw_capacity,
            f->draw_count ? f->draw_count : 1, sizeof(*e->draws)) < 0) goto done;
    for (unsigned long n=0; n<f->span_count; ++n) {
        const struct rf_core_mixed_span *s=&f->spans[n];
        if (s->kind != RF_CORE_MIXED_RASTER) continue;
        if (s->layer == RF_RENDER_LAYER_TRANSPARENT && transparent == UINT32_MAX) transparent=count;
        if (s->layer == RF_RENDER_LAYER_VIEWMODEL && viewmodel == UINT32_MAX) viewmodel=count;
        memcpy(e->ordered+count, f->raster+s->first, s->count*sizeof(*e->ordered));
        count+=(uint32_t)s->count;
    }
    memset(&renderer,0,sizeof(renderer));
    renderer.surface.width=f->width; renderer.surface.height=f->height;
    renderer.cmds=e->ordered; renderer.cmd_count=count;
    phase_start=mixed_now_ms();
    if (rf_gpu_raster_measure_textures_toy_v1(&renderer,&unique,&bytes)<0 || bytes>ULONG_MAX) goto done;
    e->stats.texture_measure_ms+=mixed_now_ms()-phase_start;
    if (reserve_u32((void **)&e->textures.descs, &e->textures.desc_capacity,
            unique ? unique : 1, sizeof(*e->textures.descs)) < 0 ||
        reserve_u32((void **)&e->textures.views, &e->textures.view_capacity,
            unique ? unique : 1, sizeof(*e->textures.views)) < 0 ||
        reserve((void **)&e->textures.texels, &e->textures.texel_capacity,
            bytes ? bytes : 1, 1) < 0) goto done;
    capacity=rf_gpu_raster_stream_size_v1(count+4);
    if (!capacity || capacity>ULONG_MAX ||
        reserve(&e->stream, &e->stream_capacity, capacity, 1) < 0) goto done;
    phase_start=mixed_now_ms();
    if (rf_gpu_raster_pack_toy_textured_spans_v2(&renderer,e->output.clear_color,0,
        e->stream,capacity,&e->stream_size,&e->textures,transparent,viewmodel)<0) goto done;
    e->stats.pack_ms+=mixed_now_ms()-phase_start;
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
    if (rf_gpu_vulkan_raster_preflight(e->context,mixed_raster(e)->implementation,e->stream,
        (unsigned long)e->stream_size,e->textures.descs,e->textures.desc_count,
        e->textures.texels,(unsigned long)e->textures.texel_size,f->width,f->height)<0) goto done;
    phase_start=mixed_now_ms();
    for (unsigned long n=0;n<f->draw_count;++n) if (encode_draw(e,f,n)<0) {
        __fprintf(2, "mixed preflight: Draw encode failed index=%lu primitive=%u indices=%u asset=%d vertex_light=%d ambient=%u specular=%u double_sided=%d dynamic=%lu\n",
            n, f->draws[n].item.primitive, f->draws[n].item.index_count,
            f->draws[n].instance.asset_id, f->draws[n].instance.vertex_light_q8,
            f->draws[n].item.material.ambient, f->draws[n].item.material.specular,
            f->draws[n].item.material.double_sided,
            f->draws[n].dynamic_vertex_count);
        goto done;
    }
    mixed_release_pins(e,e->active_frame);
    e->stats.draw_encode_ms+=mixed_now_ms()-phase_start;
    result=0;
done:
    e->stats.preflight_ms+=mixed_now_ms()-preflight_start;
    return result;
}
static int segment(struct rf_gpu_mixed_executor *e, const struct rf_core_mixed_frame *f, int final)
{
    char message[RF_GPU_MESSAGE_CAPACITY];
    uint32_t end=final ? e->command_count : e->pending_end;
    double start;
    if (e->started && e->cursor==end && !final) return 0;
    start=mixed_now_ms();
    if (final && e->output.present_timing) {
        if (rf_gpu_vulkan_raster_segment_present(e->context,mixed_raster(e)->implementation,e->stream,
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
    } else if (rf_gpu_vulkan_raster_segment(e->context,mixed_raster(e)->implementation,e->stream,
        (unsigned long)e->stream_size,e->textures.descs,e->textures.desc_count,e->textures.texels,
        (unsigned long)e->textures.texel_size,e->cursor,end,
        e->started ? RF_GPU_RASTER_LOAD_EXISTING : RF_GPU_RASTER_CLEAR,final,
        final ? e->output.color : NULL,final ? e->output.depth : NULL,
        f->width,f->height,e->output.color_stride,e->output.depth_stride,message,sizeof(message))<0) return -1;
    if (!e->started) e->stats.clears++;
    e->started=1; e->cursor=end; e->stats.raster_segments++;
    e->stats.raster_segment_ms+=mixed_now_ms()-start;
    return 0;
}
static int span(void *context, const struct rf_core_mixed_frame *f, const struct rf_core_mixed_span *s)
{
    struct rf_gpu_mixed_executor *e=context;
    int result;
    double start;
    if (s->kind==RF_CORE_MIXED_RASTER) {
        /* No graphics follow transparent/effects/viewmodel: leave that entire
         * suffix to finish, including its markers and independent depth. */
        if (s->layer==RF_RENDER_LAYER_WORLD) e->pending_end+=(uint32_t)s->count;
        return 0;
    }
    if (!s->count) return 0;
    start=mixed_now_ms();
    if (s->count > 65536 || reserve((void **)&e->batch, &e->batch_capacity,
            s->count, sizeof(*e->batch)) < 0) return -1;
    for (unsigned long n=s->first;n<s->first+s->count;++n) {
        e->batch[n-s->first].resource=e->draws[n].resource;
        e->batch[n-s->first].draw=e->draws[n].draw;
        if (!e->batch[n-s->first].resource) return -1;
    }
    e->stats.draw_batch_prepare_ms+=mixed_now_ms()-start;
    result=segment(e,f,0);
    if (result==0) {
        start=mixed_now_ms();
        result=rf_gpu_graphics_raster_batch(mixed_graphics(e),
            mixed_raster(e)->implementation,e->batch,(uint32_t)s->count);
        e->stats.graphics_draw_ms+=mixed_now_ms()-start;
    }
    if (result<0) return -1;
    e->stats.draw_spans++;
    e->stats.draws+=s->count;
    return 0;
}
static int finish(void *context, const struct rf_core_mixed_frame *f)
{
    struct rf_gpu_mixed_executor *e=context;
    if (segment(e,f,1)<0) return -1;
    e->pin_count[e->active_frame]=0;
    for (unsigned long n=0;n<f->draw_count;++n)
        if (!f->draws[n].dynamic_vertex_count)
            e->pins[e->active_frame][e->pin_count[e->active_frame]++]=
                f->draws[n].instance.mesh_handle;
    e->submitted_frame[e->active_frame]=++e->next_frame_number;
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
    for (unsigned int n=0;n<RF_GPU_MIXED_FRAMES;++n) {
        e->graphics[n]=rf_gpu_graphics_create(context);
        if (!e->graphics[n] || rf_gpu_raster_init(gpu,&e->raster[n],1,1)<0) {
            rf_gpu_mixed_destroy(e); return NULL;
        }
    }
    e->cache=rf_gpu_resource_cache_create(e->graphics[0],registry);
    if (!e->cache) { rf_gpu_mixed_destroy(e); return NULL; }
    return e;
}
int rf_gpu_mixed_render(struct rf_gpu_mixed_executor *e,
    struct rf_core_mixed_frame *f, const struct rf_gpu_mixed_output *output)
{
    static const struct rf_core_mixed_executor executor={preflight,span,finish};
    double start;
    if (!e || !f || !output) return -1;
    e->active_frame=e->next_frame;
    e->next_frame=(e->next_frame+1)%RF_GPU_MIXED_FRAMES;
    e->output=*output;
    start=mixed_now_ms();
    rf_gpu_resource_cache_collect(mixed_cache(e));
    e->stats.cache_collect_ms+=mixed_now_ms()-start;
    return rf_core_mixed_execute(f,&executor,e);
}
void rf_gpu_mixed_get_stats(struct rf_gpu_mixed_executor *e, struct rf_gpu_mixed_stats *stats)
{
    if (!e || !stats) return;
    *stats=e->stats;
    memset(&stats->graphics,0,sizeof(stats->graphics));
    for (unsigned int n=0;n<RF_GPU_MIXED_FRAMES;++n) {
        struct rf_gpu_graphics_stats s;
        rf_gpu_graphics_get_stats(e->graphics[n],&s);
#define ADD_GRAPHICS_FIELD(name) stats->graphics.name+=s.name
        ADD_GRAPHICS_FIELD(target_builds); ADD_GRAPHICS_FIELD(frames);
        ADD_GRAPHICS_FIELD(mesh_upload_bytes); ADD_GRAPHICS_FIELD(texture_upload_bytes);
        ADD_GRAPHICS_FIELD(instance_upload_bytes); ADD_GRAPHICS_FIELD(indexed_draws);
        ADD_GRAPHICS_FIELD(bridge_roundtrips); ADD_GRAPHICS_FIELD(queue_submits);
        ADD_GRAPHICS_FIELD(fence_waits); ADD_GRAPHICS_FIELD(raster_bridge_transfers);
        ADD_GRAPHICS_FIELD(bridge_transfer_bytes); ADD_GRAPHICS_FIELD(submit_wall_ms);
        ADD_GRAPHICS_FIELD(fence_wait_wall_ms); ADD_GRAPHICS_FIELD(bridge_wall_ms);
#undef ADD_GRAPHICS_FIELD
    }
    stats->gpu_timing=e->completed_timing;
}
void rf_gpu_mixed_destroy(struct rf_gpu_mixed_executor *e)
{
    if (!e) return;
    release_plan(e);
    for (unsigned int n=0;n<RF_GPU_MIXED_FRAMES;++n)
        if (e->raster[n].implementation)
            rf_gpu_vulkan_raster_recycle(e->raster[n].implementation);
    rf_gpu_resource_cache_destroy(e->cache); e->cache=NULL;
    for (unsigned int n=0;n<RF_GPU_MIXED_FRAMES;++n) {
        mixed_release_pins(e,n);
        mixed_release_dynamic(e,n);
        free(e->pins[n]);
        free(e->dynamic[n]);
        rf_gpu_raster_shutdown(&e->raster[n]);
        rf_gpu_graphics_destroy(e->graphics[n]);
    }
    free(e);
}
