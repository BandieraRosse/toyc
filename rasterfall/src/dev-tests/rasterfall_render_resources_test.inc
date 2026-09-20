/* Inject only file loading, keeping the production allocation, identity,
 * deduplication, retirement and model/texture destructor paths intact. */
static int resource_test_load(struct rasterfall_model_asset *model, const char *path)
{
    unsigned char *data;
    if (!strcmp(path, "missing")) return -1;
    data = tlibc_malloc(16);
    if (!data) return -1;
    memset(data, 0x5a, 16);
    model->data = data; model->data_size = 16;
    model->materials = data; model->material_count = 1;
    model->textures.assets = tlibc_malloc(sizeof(*model->textures.assets));
    if (!model->textures.assets) return -1;
    memset(model->textures.assets, 0, sizeof(*model->textures.assets));
    model->textures.count = 1;
    model->textures.views = tlibc_malloc(sizeof(*model->textures.views));
    if (!model->textures.views) return -1;
    memset(model->textures.views, 0, sizeof(*model->textures.views));
    data = tlibc_malloc(3);
    if (!data) return -1;
    data[0] = 17; data[1] = 31; data[2] = 63;
    model->textures.assets[0].data = data;
    model->textures.views[0].data = data;
    model->textures.views[0].data_size = 3;
    model->textures.views[0].width = model->textures.views[0].height = 1;
    model->textures.views[0].channels = 3;
    return 0;
}

static struct rasterfall_model_asset *resource_test_build_adopted(void)
{
    struct rasterfall_model_asset *model =
        tlibc_malloc(sizeof(*model));
    if (!model) return NULL;
    memset(model, 0, sizeof(*model));
    if (resource_test_load(model, "generated") < 0) {
        rasterfall_model_unload(model);
        tlibc_free(model);
        return NULL;
    }
    return model;
}

int rasterfall_render_resources_logic_test(void)
{
    struct rasterfall_resource_registry *r = tlibc_malloc(sizeof(*r));
    struct rasterfall_resource_handle old, shared, next, bad;
    const struct rasterfall_model_asset *backing;
    const struct toy_texture_view *texture;
    struct rasterfall_resource_stats stats;
    unsigned int i, loads;
    int failure = 0;
    if (!r) return 1;
    memset(r, 0, sizeof(*r));
#define RESOURCE_CHECK(x) do { if (!(x)) { failure = __LINE__; goto done; } } while (0)
    RESOURCE_CHECK(resource_load(r, "mesh", &old, resource_test_load) == 0);
    RESOURCE_CHECK(resource_load(r, "mesh", &shared, resource_test_load) == 0);
    RESOURCE_CHECK(old.slot == shared.slot && old.generation == shared.generation && r->loads == 1);
    RESOURCE_CHECK(rasterfall_resources_pin(r, old) < 0);
    RESOURCE_CHECK(rasterfall_resources_frame_begin(r) == 0);
    RESOURCE_CHECK(rasterfall_resources_frame_begin(r) < 0);
    RESOURCE_CHECK(rasterfall_resources_pin(r, old) == 0);
    RESOURCE_CHECK(rasterfall_resources_pin(r, shared) == 0);
    backing = rasterfall_resources_resolve(r, old);
    texture = &backing->textures.views[0];
    rasterfall_resources_invalidate(r);
    RESOURCE_CHECK(rasterfall_resources_resolve(r, old) == backing);
    RESOURCE_CHECK(backing->materials[0] == 0x5a && texture->data[2] == 63);
    RESOURCE_CHECK(rasterfall_resources_pin(r, old) < 0);
    RESOURCE_CHECK(resource_load(r, "mesh", &next, resource_test_load) == 0);
    RESOURCE_CHECK(next.slot != old.slot);
    RESOURCE_CHECK(rasterfall_resources_pin(r, next) == 0);
    rasterfall_resources_stats(r, &stats);
    RESOURCE_CHECK(stats.live == 1 && stats.retired == 1 && stats.pinned == 2 && !stats.releases);
    rasterfall_resources_frame_complete(r);
    RESOURCE_CHECK(!rasterfall_resources_resolve(r, old));
    RESOURCE_CHECK(rasterfall_resources_resolve(r, next) != NULL);
    RESOURCE_CHECK(rasterfall_resources_pin(r, old) < 0);
    rasterfall_resources_stats(r, &stats);
    RESOURCE_CHECK(stats.live == 1 && !stats.retired && !stats.pinned && stats.releases == 1);
    /* Resize/device recreation does not invalidate this CPU registry. New
     * frame reuse must preserve identity and perform no load. */
    loads = r->loads;
    for (i = 0; i < 32; ++i) {
        RESOURCE_CHECK(rasterfall_resources_frame_begin(r) == 0);
        RESOURCE_CHECK(resource_load(r, "mesh", &shared, resource_test_load) == 0);
        RESOURCE_CHECK(shared.slot == next.slot && shared.generation == next.generation);
        RESOURCE_CHECK(rasterfall_resources_pin(r, shared) == 0);
        rasterfall_resources_frame_complete(r);
    }
    RESOURCE_CHECK(r->loads == loads);
    rasterfall_resources_invalidate(r);
    /* Generated world meshes use the same generation, pin and retirement
     * contract as file-loaded bundles. A duplicate identity is rejected
     * without transferring ownership; after invalidation an in-flight old
     * generation may coexist with the rebuilt mesh until frame completion. */
    {
        struct rasterfall_model_asset *adopted = resource_test_build_adopted();
        struct rasterfall_model_asset *duplicate;
        struct rasterfall_model_asset *rebuilt;
        struct rasterfall_resource_handle generated, duplicate_handle, regenerated;
        RESOURCE_CHECK(adopted != NULL);
        RESOURCE_CHECK(rasterfall_resources_adopt(r, "@world/ground", adopted,
                                                  &generated) == 0);
        RESOURCE_CHECK(rasterfall_resources_resolve(r, generated) == adopted);
        RESOURCE_CHECK(rasterfall_resources_resolve_active(r, generated) == adopted);
        duplicate = resource_test_build_adopted();
        RESOURCE_CHECK(duplicate != NULL);
        RESOURCE_CHECK(rasterfall_resources_adopt(r, "@world/ground", duplicate,
                                                  &duplicate_handle) < 0);
        RESOURCE_CHECK(duplicate_handle.slot == generated.slot &&
                       duplicate_handle.generation == generated.generation);
        rasterfall_model_unload(duplicate);
        tlibc_free(duplicate);
        RESOURCE_CHECK(rasterfall_resources_frame_begin(r) == 0);
        RESOURCE_CHECK(rasterfall_resources_pin(r, generated) == 0);
        rasterfall_resources_invalidate(r);
        RESOURCE_CHECK(rasterfall_resources_resolve(r, generated) == adopted);
        RESOURCE_CHECK(!rasterfall_resources_resolve_active(r, generated));
        rebuilt = resource_test_build_adopted();
        RESOURCE_CHECK(rebuilt != NULL);
        RESOURCE_CHECK(rasterfall_resources_adopt(r, "@world/ground", rebuilt,
                                                  &regenerated) == 0);
        RESOURCE_CHECK(regenerated.slot != generated.slot &&
                       rasterfall_resources_resolve(r, regenerated) == rebuilt);
        rasterfall_resources_stats(r, &stats);
        RESOURCE_CHECK(stats.live == 1 && stats.retired == 1 && stats.pinned == 1);
        rasterfall_resources_frame_complete(r);
        RESOURCE_CHECK(!rasterfall_resources_resolve(r, generated));
        RESOURCE_CHECK(rasterfall_resources_resolve(r, regenerated) == rebuilt);
        rasterfall_resources_stats(r, &stats);
        RESOURCE_CHECK(stats.live == 1 && !stats.retired && !stats.pinned);
        rasterfall_resources_invalidate(r);
    }
    /* Repeated world unload/reload, including failed loads and pinned exit. */
    for (i = 0; i < 32; ++i) {
        RESOURCE_CHECK(resource_load(r, "mesh", &next, resource_test_load) == 0);
        RESOURCE_CHECK(!rasterfall_resources_resolve(r, old));
        RESOURCE_CHECK(resource_load(r, "missing", &bad, resource_test_load) < 0 && !bad.generation);
        loads = r->loads;
        RESOURCE_CHECK(resource_load(r, "missing", &bad, resource_test_load) < 0 && r->loads == loads);
        RESOURCE_CHECK(rasterfall_resources_frame_begin(r) == 0);
        RESOURCE_CHECK(rasterfall_resources_pin(r, next) == 0);
        rasterfall_resources_invalidate(r);
        RESOURCE_CHECK(rasterfall_resources_resolve(r, next) != NULL);
        rasterfall_resources_frame_complete(r);
        RESOURCE_CHECK(!rasterfall_resources_resolve(r, next));
        rasterfall_resources_stats(r, &stats);
        RESOURCE_CHECK(!stats.live && !stats.retired && !stats.pinned && !stats.failed);
    }
    /* Exhaustion never overwrites pinned resources; wrap never resurrects a
     * stale generation. Both failures leave an invalid output handle. */
    for (i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) r->slots[i].generation = UINT_MAX;
    RESOURCE_CHECK(resource_load(r, "mesh", &bad, resource_test_load) < 0 && !bad.generation);
    r->slots[1].generation = UINT_MAX - 1;
    RESOURCE_CHECK(resource_load(r, "mesh", &next, resource_test_load) == 0 && next.generation == UINT_MAX);
    rasterfall_resources_invalidate(r);
    RESOURCE_CHECK(resource_load(r, "mesh", &bad, resource_test_load) < 0 && !bad.generation);
done:
    rasterfall_resources_invalidate(r);
    rasterfall_resources_frame_complete(r);
    tlibc_free(r);
    if (failure) __printf("draw-resources: FAIL line=%d\n", failure);
    else __printf("draw-resources: PASS identity, pinning, generated adoption, texture lifetime, reload, failure, generation exhaustion\n");
#undef RESOURCE_CHECK
    return failure != 0;
}
