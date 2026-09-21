/* Core-owned preparation, deliberately independent of Vulkan handles. */
#include "rf_core_mixed_frame.h"

static int mixed_reserve(void **data, unsigned long *capacity,
                         unsigned long needed, unsigned long bytes)
{
    unsigned long grown = *capacity ? *capacity : 16;
    void *replacement;
    if (needed <= *capacity) return 0;
    if (!bytes || needed > ULONG_MAX / bytes) return -1;
    while (grown < needed) {
        if (grown > ULONG_MAX / bytes / 2) {
            grown = needed; break;
        }
        grown *= 2;
    }
    replacement = tlibc_malloc(grown * bytes);
    if (!replacement) return -1;
    if (*capacity) memcpy(replacement, *data, *capacity * bytes);
    tlibc_free(*data);
    *data = replacement; *capacity = grown;
    return 0;
}

void rf_core_mixed_reset(struct rf_core_mixed_frame *f)
{
    if (!f || f->state == RF_CORE_MIXED_EXECUTING) return;
    f->raster_count = f->draw_count = f->span_count = 0;
    f->dynamic_vertex_count = 0;
    f->reference_vertex_count = 0;
    f->skin_vertex_count = f->skin_palette_count = f->skin_instance_count = 0;
    f->required_draw_count = 0;
    f->registry = NULL;
    f->registry_epoch = 0;
    f->width = f->height = 0;
    f->last_layer = RF_RENDER_LAYER_WORLD;
    f->state = RF_CORE_MIXED_EMPTY;
}

void rf_core_mixed_destroy(struct rf_core_mixed_frame *f)
{
    if (!f || f->state == RF_CORE_MIXED_EXECUTING) return;
    tlibc_free(f->raster); tlibc_free(f->draws); tlibc_free(f->spans);
    tlibc_free(f->dynamic_vertices);
    tlibc_free(f->skin_vertices); tlibc_free(f->skin_palette);
    tlibc_free(f->skin_instances);
    memset(f, 0, sizeof(*f));
}

int rf_core_mixed_begin(struct rf_core_mixed_frame *f,
    struct rasterfall_resource_registry *registry, int width, int height)
{
    if (!f || f->state != RF_CORE_MIXED_EMPTY || !registry ||
        !registry->frame_active || width <= 0 || height <= 0 ||
        width > 16384 || height > 16384) return -1;
    f->registry = registry; f->width = width; f->height = height;
    f->registry_epoch = registry->frame_epoch;
    f->last_layer = RF_RENDER_LAYER_WORLD;
    f->state = RF_CORE_MIXED_RECORDING;
    return 0;
}

static int mixed_lifetime_valid(const struct rf_core_mixed_frame *f)
{
    return f->registry && f->registry->frame_active &&
        f->registry_epoch == f->registry->frame_epoch;
}

static unsigned int mixed_raster_layer(unsigned int layer,
    const struct toy_raster_cmd *cmd)
{
    return layer == RF_RENDER_LAYER_WORLD && rf_core_cmd_is_transparent_v1(cmd) ?
        RF_RENDER_LAYER_TRANSPARENT : layer;
}

static void mixed_append_span(struct rf_core_mixed_span *spans,
    unsigned long *count, unsigned int kind, unsigned int layer,
    unsigned long first, unsigned long length)
{
    struct rf_core_mixed_span *previous = *count ? &spans[*count - 1] : NULL;
    if (previous && previous->kind == kind && previous->layer == layer &&
        previous->first + previous->count == first) {
        previous->count += length;
    } else {
        struct rf_core_mixed_span *s = &spans[(*count)++];
        s->kind = kind; s->layer = layer; s->first = first; s->count = length;
    }
}

int rf_core_mixed_raster(struct rf_core_mixed_frame *f, unsigned int layer,
    const struct toy_raster_cmd *commands, unsigned long count)
{
    unsigned long i, needed;
    if (!f || f->state != RF_CORE_MIXED_RECORDING ||
        !mixed_lifetime_valid(f) || !commands || !count ||
        (layer != RF_RENDER_LAYER_WORLD && layer != RF_RENDER_LAYER_EFFECTS &&
         layer != RF_RENDER_LAYER_VIEWMODEL) || layer < f->last_layer ||
        count > ULONG_MAX - f->raster_count ||
        count > ULONG_MAX - f->span_count) return -1;
    needed = f->raster_count + count;
    if (mixed_reserve((void **)&f->raster, &f->raster_capacity, needed,
            sizeof(*f->raster)) < 0 ||
        mixed_reserve((void **)&f->spans, &f->span_capacity,
            f->span_count + count, sizeof(*f->spans)) < 0) return -1;
    memcpy(f->raster + f->raster_count, commands, count * sizeof(*commands));
    for (i = 0; i < count; ++i)
        mixed_append_span(f->spans, &f->span_count, RF_CORE_MIXED_RASTER,
            mixed_raster_layer(layer, &commands[i]), f->raster_count + i, 1);
    f->raster_count = needed; f->last_layer = layer;
    return 0;
}

int rf_core_mixed_world_consume(struct toy_renderer *renderer,
    const struct toy_raster_cmd *commands, int count, void *context)
{
    struct rf_core_mixed_frame *f = context;
    if (!renderer || !f || count <= 0 ||
        renderer->surface.width != f->width ||
        renderer->surface.height != f->height ||
        rf_core_mixed_raster(f, RF_RENDER_LAYER_WORLD, commands,
            (unsigned long)count) < 0) return -1;
    return 0;
}

static int mixed_draw_valid(const struct rf_core_mixed_frame *f,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item)
{
    const struct rasterfall_model_asset *mesh;
    unsigned int i;
    if (!view || !instance || !item || !mixed_lifetime_valid(f) ||
        view->width != f->width || view->height != f->height ||
        view->focal != f->width * 3 / 4 || view->near_z != 64) return 0;
    mesh = rasterfall_resources_resolve(f->registry, instance->mesh_handle);
    if (!mesh || mesh != instance->mesh || mesh->skinning_enabled ||
        mesh->bone_count || mesh->has_character_contract ||
        item->primitive >= mesh->primitive_count || !item->index_count ||
        item->first_index % 3 || item->index_count % 3 ||
        item->first_index > mesh->index_count ||
        item->index_count > mesh->index_count - item->first_index) return 0;
    if (!item->material.texture) return 1;
    /* Only immutable texture views owned by this pinned bundle may escape
     * producer scope. Comparing addresses does not dereference foreign data. */
    if (!mesh->textures.views) return 0;
    for (i = 0; i < mesh->textures.count; ++i)
        if (item->material.texture == &mesh->textures.views[i])
            return rf_core_texture_view_valid_v1(item->material.texture) &&
                !item->material.texture->has_transparency;
    return 0;
}

static int mixed_dynamic_draw_valid(const struct rf_core_mixed_frame *f,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item, unsigned int vertex_count)
{
    return view && instance && item && mixed_lifetime_valid(f) &&
        view->width == f->width && view->height == f->height &&
        view->focal == f->width * 3 / 4 && view->near_z == 64 &&
        !instance->mesh_handle.generation && instance->mesh &&
        vertex_count && vertex_count <= 65536 && vertex_count % 3 == 0 &&
        item->first_index == 0 && item->index_count == vertex_count &&
        !item->material.texture;
}

int rf_core_mixed_draw(struct rf_core_mixed_frame *f,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item)
{
    struct rf_core_mixed_draw *d;
    if (!f || f->state != RF_CORE_MIXED_RECORDING ||
        f->last_layer != RF_RENDER_LAYER_WORLD ||
        f->draw_count == ULONG_MAX || f->span_count == ULONG_MAX ||
        !mixed_draw_valid(f, view, instance, item)) return -1;
    if (mixed_reserve((void **)&f->draws, &f->draw_capacity, f->draw_count + 1,
            sizeof(*f->draws)) < 0 ||
        mixed_reserve((void **)&f->spans, &f->span_capacity, f->span_count + 1,
            sizeof(*f->spans)) < 0 ||
        rasterfall_resources_pin(f->registry, instance->mesh_handle) < 0) return -1;
    d = &f->draws[f->draw_count];
    memset(d, 0, sizeof(*d));
    d->view = *view; d->instance = *instance; d->item = *item;
    mixed_append_span(f->spans, &f->span_count, RF_CORE_MIXED_DRAW,
        RF_RENDER_LAYER_WORLD, f->draw_count++, 1);
    return 0;
}

int rf_core_mixed_dynamic_draw(struct rf_core_mixed_frame *f,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item,
    const struct rasterfall_dynamic_draw_vertex *vertices,
    unsigned int vertex_count)
{
    struct rf_core_mixed_draw *d;
    unsigned long first;
    if (!f || f->state != RF_CORE_MIXED_RECORDING ||
        f->last_layer != RF_RENDER_LAYER_WORLD || !vertices ||
        f->draw_count == ULONG_MAX || f->span_count == ULONG_MAX ||
        vertex_count > ULONG_MAX - f->dynamic_vertex_count ||
        vertex_count > ULONG_MAX - f->reference_vertex_count ||
        !mixed_dynamic_draw_valid(f, view, instance, item, vertex_count)) return -1;
    first = f->dynamic_vertex_count;
    if (mixed_reserve((void **)&f->draws, &f->draw_capacity, f->draw_count + 1,
            sizeof(*f->draws)) < 0 ||
        mixed_reserve((void **)&f->spans, &f->span_capacity, f->span_count + 1,
            sizeof(*f->spans)) < 0 ||
        mixed_reserve((void **)&f->dynamic_vertices, &f->reference_vertex_capacity,
            f->reference_vertex_count + vertex_count,
            sizeof(*f->dynamic_vertices)) < 0) return -1;
    memcpy(f->dynamic_vertices + f->reference_vertex_count, vertices,
        (size_t)vertex_count * sizeof(*vertices));
    f->dynamic_vertex_count += vertex_count;
    d = &f->draws[f->draw_count];
    memset(d, 0, sizeof(*d));
    d->view = *view; d->instance = *instance; d->item = *item;
    d->dynamic_first_vertex = first; d->dynamic_vertex_count = vertex_count;
    d->reference_first_vertex=f->reference_vertex_count;
    d->reference_vertex_count=vertex_count;
    f->reference_vertex_count+=vertex_count;
    mixed_append_span(f->spans, &f->span_count, RF_CORE_MIXED_DRAW,
        RF_RENDER_LAYER_WORLD, f->draw_count++, 1);
    return 0;
}

int rf_core_mixed_skin_instance(struct rf_core_mixed_frame *f,
    const struct rasterfall_model_skin_palette_bone *palette,
    unsigned int palette_count, unsigned int *instance_index)
{
    struct rf_core_mixed_skin_instance *instance;
    unsigned long first;
    if (!f || f->state != RF_CORE_MIXED_RECORDING || !mixed_lifetime_valid(f) ||
        !palette || !palette_count || !instance_index ||
        palette_count > ULONG_MAX - f->skin_palette_count ||
        f->skin_instance_count >= UINT_MAX) return -1;
    first=f->skin_palette_count;
    if (mixed_reserve((void **)&f->skin_palette,&f->skin_palette_capacity,
            first+palette_count,sizeof(*f->skin_palette))<0 ||
        mixed_reserve((void **)&f->skin_instances,&f->skin_instance_capacity,
            f->skin_instance_count+1,sizeof(*f->skin_instances))<0) return -1;
    memcpy(f->skin_palette+first,palette,(size_t)palette_count*sizeof(*palette));
    instance=&f->skin_instances[f->skin_instance_count];
    instance->first_palette_bone=first;
    instance->palette_bone_count=palette_count;
    *instance_index=(unsigned int)f->skin_instance_count++;
    f->skin_palette_count+=palette_count;
    return 0;
}

int rf_core_mixed_skinned_draw(struct rf_core_mixed_frame *f,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *item,
    const struct rasterfall_dynamic_draw_vertex *reference_vertices,
    const struct rasterfall_skinned_draw_vertex *bind_vertices,
    unsigned int vertex_count, unsigned int skin_instance)
{
    unsigned long first=f ? f->skin_vertex_count : 0;
    unsigned long reference_first=f ? f->reference_vertex_count : 0;
    struct rf_core_mixed_draw *draw;
    if (!f || !bind_vertices || skin_instance>=f->skin_instance_count ||
        vertex_count>ULONG_MAX-f->skin_vertex_count ||
        vertex_count>ULONG_MAX-f->dynamic_vertex_count ||
        (reference_vertices && vertex_count>ULONG_MAX-f->reference_vertex_count) ||
        mixed_reserve((void **)&f->skin_vertices,&f->skin_vertex_capacity,
            first+vertex_count,sizeof(*f->skin_vertices))<0) return -1;
    if (reference_vertices &&
        mixed_reserve((void **)&f->dynamic_vertices,&f->reference_vertex_capacity,
            reference_first+vertex_count,sizeof(*f->dynamic_vertices))<0) return -1;
    if (!mixed_dynamic_draw_valid(f,view,instance,item,vertex_count) ||
        f->draw_count==ULONG_MAX || f->span_count==ULONG_MAX ||
        mixed_reserve((void **)&f->draws,&f->draw_capacity,f->draw_count+1,
            sizeof(*f->draws))<0 ||
        mixed_reserve((void **)&f->spans,&f->span_capacity,f->span_count+1,
            sizeof(*f->spans))<0) return -1;
    if (reference_vertices) {
        memcpy(f->dynamic_vertices+reference_first,reference_vertices,
            (size_t)vertex_count*sizeof(*reference_vertices));
        f->reference_vertex_count+=vertex_count;
    }
    memcpy(f->skin_vertices+first,bind_vertices,
        (size_t)vertex_count*sizeof(*bind_vertices));
    f->skin_vertex_count+=vertex_count;
    draw=&f->draws[f->draw_count]; memset(draw,0,sizeof(*draw));
    draw->view=*view; draw->instance=*instance; draw->item=*item;
    draw->dynamic_first_vertex=f->dynamic_vertex_count;
    draw->dynamic_vertex_count=vertex_count;
    draw->reference_first_vertex=reference_first;
    draw->reference_vertex_count=reference_vertices ? vertex_count : 0;
    draw->skin_first_vertex=first;
    draw->skin_vertex_count=vertex_count;
    draw->skin_instance=skin_instance;
    mixed_append_span(f->spans,&f->span_count,RF_CORE_MIXED_DRAW,
        RF_RENDER_LAYER_WORLD,f->draw_count++,1);
    f->dynamic_vertex_count+=vertex_count;
    return 0;
}

int rf_core_mixed_require_draws(struct rf_core_mixed_frame *f,
    unsigned long count)
{
    if (!f || f->state != RF_CORE_MIXED_RECORDING ||
        !mixed_lifetime_valid(f) || count > ULONG_MAX - f->required_draw_count)
        return -1;
    f->required_draw_count += count;
    return 0;
}

int rf_core_mixed_freeze(struct rf_core_mixed_frame *f)
{
    struct rf_core_mixed_span *ordered = NULL;
    unsigned long i, count = 0;
    unsigned int layer;
    if (!f || f->state != RF_CORE_MIXED_RECORDING ||
        !mixed_lifetime_valid(f) ||
        (f->required_draw_count && f->required_draw_count != f->draw_count)) return -1;
    if (f->span_count) {
        ordered = tlibc_malloc(f->span_count * sizeof(*ordered));
        if (!ordered) return -1;
    }
    /* Draws participate at their original WORLD position, never appended
     * en masse before or after opaque RasterCmds. Only transparent WORLD
     * moves, preserving order within every layer and backend run. */
    for (layer = RF_RENDER_LAYER_WORLD; layer <= RF_RENDER_LAYER_VIEWMODEL; ++layer)
        for (i = 0; i < f->span_count; ++i) {
            const struct rf_core_mixed_span *s = &f->spans[i];
            if (s->layer == layer)
                mixed_append_span(ordered, &count, s->kind, s->layer,
                                  s->first, s->count);
        }
    tlibc_free(f->spans); f->spans = ordered;
    /* Allocation is the pre-coalescing count, not the old capacity. */
    f->span_capacity = f->span_count; f->span_count = count;
    f->state = RF_CORE_MIXED_FROZEN;
    return 0;
}

int rf_core_mixed_raster_preflight(const struct rf_core_mixed_frame *f)
{
    unsigned long i;
    if (!f) return -1;
    for (i = 0; i < f->raster_count; ++i)
        if (rf_core_cmd_fallback_reason_v1(&f->raster[i]) != RF_PRE_POST_FALLBACK_NONE)
            return -1;
    return 0;
}

static int mixed_skin_draw_valid(const struct rf_core_mixed_frame *f,
    const struct rf_core_mixed_draw *draw)
{
    const struct rf_core_mixed_skin_instance *instance;
    if (!draw->skin_vertex_count) return 1;
    if (draw->skin_instance>=f->skin_instance_count) return 0;
    instance=&f->skin_instances[draw->skin_instance];
    if (!instance->palette_bone_count ||
        instance->first_palette_bone>f->skin_palette_count ||
        instance->palette_bone_count>f->skin_palette_count-
            instance->first_palette_bone) return 0;
    for (unsigned long n=draw->skin_first_vertex;
         n<draw->skin_first_vertex+draw->skin_vertex_count;++n)
        for (unsigned int k=0;k<4;++k) {
            const struct rasterfall_skin_influence *influence=
                &f->skin_vertices[n].influences[k];
            if (influence->type>1 ||
                influence->bone0>=instance->palette_bone_count ||
                (influence->type &&
                 influence->bone1>=instance->palette_bone_count)) return 0;
        }
    return 1;
}

int rf_core_mixed_execute(struct rf_core_mixed_frame *f,
    const struct rf_core_mixed_executor *executor, void *context)
{
    unsigned long i;
    if (!f || f->state != RF_CORE_MIXED_FROZEN || !executor ||
        !executor->preflight || !executor->span || !executor->finish ||
        !mixed_lifetime_valid(f) ||
        (f->required_draw_count && f->required_draw_count != f->draw_count)) return -1;
    /* Check lifetime again: a plan cannot outlive its registry frame pin. */
    for (i = 0; i < f->draw_count; ++i) {
        const struct rf_core_mixed_draw *d = &f->draws[i];
        if (d->dynamic_vertex_count) {
            if (!mixed_dynamic_draw_valid(f, &d->view, &d->instance, &d->item,
                    d->dynamic_vertex_count) ||
                d->dynamic_first_vertex > f->dynamic_vertex_count ||
                d->dynamic_vertex_count > f->dynamic_vertex_count -
                    d->dynamic_first_vertex ||
                (d->skin_vertex_count &&
                 (d->skin_vertex_count != d->dynamic_vertex_count ||
                  d->skin_instance >= f->skin_instance_count ||
                  d->skin_first_vertex > f->skin_vertex_count ||
                  d->skin_vertex_count > f->skin_vertex_count-d->skin_first_vertex ||
                  !mixed_skin_draw_valid(f,d))) ||
                d->reference_first_vertex>f->reference_vertex_count ||
                d->reference_vertex_count>f->reference_vertex_count-
                    d->reference_first_vertex)
                return -1;
        } else if (!mixed_draw_valid(f, &d->view, &d->instance, &d->item) ||
            !f->registry->slots[d->instance.mesh_handle.slot].pinned) return -1;
    }
    f->state = RF_CORE_MIXED_EXECUTING;
    if (executor->preflight(context, f) < 0) {
        f->state = RF_CORE_MIXED_FROZEN;
        return -1;
    }
    for (i = 0; i < f->span_count; ++i)
        if (executor->span(context, f, &f->spans[i]) < 0) {
            f->state = RF_CORE_MIXED_FAILED;
            return -1;
        }
    if (executor->finish(context, f) < 0) {
        f->state = RF_CORE_MIXED_FAILED;
        return -1;
    }
    f->state = RF_CORE_MIXED_COMPLETE;
    return 0;
}

#include "dev-tests/rf_core_mixed_frame_test.inc"
