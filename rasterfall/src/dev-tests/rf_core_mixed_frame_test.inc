/* Exercise production recording/partition/lifetime, with an executor that
 * independently observes exact order and models preflight/submission faults.
 * GPU numeric/occlusion correctness remains the separate GPU-MIXED mixed gate. */
struct mixed_test_executor {
    unsigned int events[512], layers[512];
    unsigned long count, checked, finished, calls;
    int reject_color, fail_span, fail_finish;
};

static int mixed_test_preflight(void *context, const struct rf_core_mixed_frame *f)
{
    struct mixed_test_executor *e = context;
    unsigned long i;
    e->checked++;
    for (i = 0; i < f->raster_count; ++i)
        if (rf_core_cmd_fallback_reason_v1(&f->raster[i]) ||
            (e->reject_color && f->raster[i].color == (unsigned int)e->reject_color))
            return -1;
    for (i = 0; i < f->draw_count; ++i)
        if (e->reject_color &&
            f->draws[i].item.material.color == (unsigned int)e->reject_color)
            return -1;
    return 0;
}

static int mixed_test_span(void *context, const struct rf_core_mixed_frame *f,
    const struct rf_core_mixed_span *s)
{
    struct mixed_test_executor *e = context;
    unsigned long i;
    e->calls++;
    if (e->fail_span && e->calls == (unsigned long)e->fail_span) return -1;
    for (i = 0; i < s->count; ++i) {
        if (e->count >= 512) return -1;
        e->events[e->count] = s->kind == RF_CORE_MIXED_DRAW ?
            f->draws[s->first + i].item.material.color : f->raster[s->first + i].color;
        e->layers[e->count++] = s->layer;
    }
    return 0;
}

static int mixed_test_finish(void *context, const struct rf_core_mixed_frame *f)
{
    struct mixed_test_executor *e = context;
    (void)f;
    e->finished++;
    return e->fail_finish ? -1 : 0;
}

int rf_core_mixed_frame_logic_test(void)
{
    struct rf_core_mixed_frame f;
    struct rasterfall_resource_registry *registry = tlibc_malloc(sizeof(*registry));
    struct rasterfall_model_asset *mesh = tlibc_malloc(sizeof(*mesh));
    struct rasterfall_draw_view view;
    struct rasterfall_draw_instance instance, bad;
    struct rasterfall_draw_item item;
    struct toy_raster_cmd commands[2];
    struct toy_renderer sink;
    struct mixed_test_executor observed;
    const struct rf_core_mixed_executor executor = {
        mixed_test_preflight, mixed_test_span, mixed_test_finish
    };
    const unsigned int expected[] = {11, 101, 22, 102, 103, 31, 32, 41, 42, 51, 52};
    unsigned long i, count;
    int failure = 0;
    memset(&f, 0, sizeof(f));
    if (registry) memset(registry, 0, sizeof(*registry));
    if (!registry || !mesh) { failure = __LINE__; goto done; }
    memset(registry, 0, sizeof(*registry)); memset(mesh, 0, sizeof(*mesh));
    memset(&view, 0, sizeof(view)); memset(&instance, 0, sizeof(instance));
    memset(&item, 0, sizeof(item)); memset(commands, 0, sizeof(commands));
    memset(&sink, 0, sizeof(sink));
    memset(&observed, 0, sizeof(observed));
    registry->slots[0].model = mesh;
    registry->slots[0].active = 1; registry->slots[0].generation = 1;
    mesh->primitive_count = 1; mesh->index_count = 3;
    instance.mesh = mesh; instance.mesh_handle.generation = 1;
    instance.scale_milli = 1000; instance.yaw_cos_q10 = 1024;
    instance.scene_light_q8 = 256;
    view.width = 128; view.height = 72; view.focal = 96; view.near_z = 64;
    item.index_count = 3;
    commands[0].area = commands[1].area = -1;
    commands[0].material_alpha = commands[1].material_alpha = 255;
#define MIXED_CHECK(x) do { if (!(x)) { failure = __LINE__; goto done; } } while (0)
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) < 0);
    MIXED_CHECK(rasterfall_resources_frame_begin(registry) == 0);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(!f.sky_enabled && !f.direction_sy && !f.direction_cy &&
        !f.pitch_sy && !f.pitch_cy);
    sink.surface.width = 128; sink.surface.height = 72;
    MIXED_CHECK(rf_core_mixed_world_consume(&sink, commands, 1, &f) == 0 &&
        f.raster_count == 1);
    sink.surface.width++;
    MIXED_CHECK(rf_core_mixed_world_consume(&sink, commands, 1, &f) < 0 &&
        f.raster_count == 1);
    MIXED_CHECK(rf_core_mixed_set_producer(&f, &sink,
        RF_CORE_PRODUCER_ENEMY_BODY) == 0);
    sink.surface.width--;
    MIXED_CHECK(rf_core_mixed_world_consume(&sink, commands, 1, &f) == 0 &&
        f.span_count == 2 &&
        f.spans[0].producer == RF_CORE_PRODUCER_WORLD_MAP &&
        f.spans[1].producer == RF_CORE_PRODUCER_ENEMY_BODY);
    f.sky_enabled = 1; f.direction_sy = 3; f.direction_cy = 4;
    f.pitch_sy = 5; f.pitch_cy = 6;
    rf_core_mixed_reset(&f);
    MIXED_CHECK(!f.sky_enabled && !f.direction_sy && !f.direction_cy &&
        !f.pitch_sy && !f.pitch_cy);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(rf_core_mixed_require_draws(&f, 3) == 0);
    MIXED_CHECK(rf_core_mixed_freeze(&f) < 0 && f.state == RF_CORE_MIXED_RECORDING);
    MIXED_CHECK(rf_core_mixed_require_draws(&f, ULONG_MAX) < 0 &&
        f.required_draw_count == 3);
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_WORLD, commands, ULONG_MAX) < 0 &&
        !f.raster_count && !f.span_count);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) < 0);
    bad = instance; bad.mesh_handle.generation = 0;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &bad, &item) < 0 && !f.draw_count);
    bad.mesh_handle.generation = 2;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &bad, &item) < 0 && !f.span_count);
    view.width++;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) < 0);
    view.width--;
    item.first_index = 3;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) < 0);
    item.first_index = 0;
    {
        struct toy_texture_view foreign;
        memset(&foreign, 0, sizeof(foreign));
        item.material.texture = &foreign;
        MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) < 0);
        item.material.texture = NULL;
    }
    commands[0].color = 11; commands[1].color = 31; commands[1].transparent = 1;
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_WORLD, commands, 2) == 0);
    item.material.color = 101;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) == 0);
    commands[0].color = 22; commands[1].color = 32;
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_WORLD, commands, 2) == 0);
    item.material.color = 102;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) == 0);
    item.material.color = 103;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) == 0);
    MIXED_CHECK(f.required_draw_count == 3 && f.draw_count == 3);
    /* Producer stack state is now destroyed; frozen snapshots must survive. */
    item.material.color = 999; instance.x = 999; view.camera.body.x = 999;
    MIXED_CHECK(f.draws[0].instance.x == 0 && f.draws[0].view.camera.body.x == 0);
    commands[0].color = 41; commands[1].color = 42;
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_EFFECTS, commands, 2) == 0);
    count = f.span_count;
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) < 0);
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_WORLD, commands, 2) < 0);
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_TRANSPARENT, commands, 2) < 0);
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_OVERLAY, commands, 2) < 0);
    MIXED_CHECK(f.span_count == count);
    commands[0].color = 51; commands[1].color = 52;
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_VIEWMODEL, commands, 2) == 0);
    commands[0].color = commands[1].color = 999;
    /* World unload retires backing, but cannot release this frame's pin. */
    rasterfall_resources_invalidate(registry);
    MIXED_CHECK(registry->slots[0].pinned && registry->slots[0].model == mesh);
    MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
    f.required_draw_count++;
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 &&
        f.state == RF_CORE_MIXED_FROZEN && !observed.checked);
    f.required_draw_count--;
    MIXED_CHECK(rf_core_mixed_freeze(&f) < 0);
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_VIEWMODEL, commands, 1) < 0);
    /* A later Draw rejection must also prevent earlier Raster submissions. */
    observed.reject_color = 103;
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 &&
        f.state == RF_CORE_MIXED_FROZEN && !observed.calls && !observed.finished);
    /* Reject the LAST command: absolutely no earlier span or finish may run. */
    observed.reject_color = 52;
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 &&
        f.state == RF_CORE_MIXED_FROZEN && !observed.calls && !observed.finished);
    observed.reject_color = 0;
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) == 0);
    MIXED_CHECK(observed.count == sizeof(expected) / sizeof(expected[0]) &&
        observed.checked == 3 && observed.finished == 1);
    for (i = 0; i < observed.count; ++i) {
        unsigned int layer = i < 5 ? RF_RENDER_LAYER_WORLD :
            i < 7 ? RF_RENDER_LAYER_TRANSPARENT :
            i < 9 ? RF_RENDER_LAYER_EFFECTS : RF_RENDER_LAYER_VIEWMODEL;
        MIXED_CHECK(observed.events[i] == expected[i] && observed.layers[i] == layer);
    }
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 && observed.finished == 1);
    /* Completion/reset and changing sizes exercise capacity reuse, including
     * the same shrink/grow case as the existing retained partition regression. */
    for (i = 0; i < 4; ++i) {
        unsigned long n, batches = i == 1 ? 1 : i == 2 ? 80 : 24;
        rf_core_mixed_reset(&f);
        MIXED_CHECK(rf_core_mixed_begin(&f, registry, 129, 73) == 0);
        for (n = 0; n < batches; ++n) {
            commands[0].color = (unsigned int)(n + 1);
            commands[1].color = (unsigned int)(n + 201);
            MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_WORLD, commands, 2) == 0);
        }
        MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
        memset(&observed, 0, sizeof(observed));
        MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) == 0);
        MIXED_CHECK(observed.count == batches * 2 && observed.finished == 1);
        for (n = 0; n < batches; ++n)
            MIXED_CHECK(observed.events[n] == n + 1 &&
                observed.events[batches + n] == n + 201);
    }
    /* CPU-skinned Draw dynamic Draws own an exact frame copy of CPU-skinned positions
     * and source normals.  Mutating the producer buffer after submission must
     * not affect the frozen GPU upload, and reset must retain only capacity. */
    {
        struct rasterfall_dynamic_draw_vertex dynamic[6], expected_dynamic[6];
        memset(dynamic, 0, sizeof(dynamic));
        for (i = 0; i < 6; ++i) {
            unsigned int component;
            for (component = 0; component < 3; ++component)
                dynamic[i].position[component] = (int32_t)(1000 + i * 10 + component);
            dynamic[i].uv[0] = (int32_t)(i * 100); dynamic[i].uv[1] = (int32_t)(i * 200);
            for (component = 0; component < 9; ++component)
                dynamic[i].normals[component] = (int32_t)(-12000 + i * 100 + component);
        }
        memcpy(expected_dynamic, dynamic, sizeof(dynamic));
        rf_core_mixed_reset(&f);
        MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
        instance.mesh_handle.generation = 0;
        item.index_count = 6; item.material.color = 601;
        MIXED_CHECK(rf_core_mixed_dynamic_draw(&f, &view, &instance, &item,
            dynamic, 6) == 0);
        item.index_count = 3; item.material.color = 602;
        MIXED_CHECK(rf_core_mixed_dynamic_draw(&f, &view, &instance, &item,
            dynamic + 3, 3) == 0);
        memset(dynamic, 0x5a, sizeof(dynamic));
        MIXED_CHECK(f.dynamic_vertex_count == 9 && f.draw_count == 2 &&
            f.draws[0].dynamic_first_vertex == 0 &&
            f.draws[0].dynamic_vertex_count == 6 &&
            f.draws[1].dynamic_first_vertex == 6 &&
            f.draws[1].dynamic_vertex_count == 3 &&
            !memcmp(f.dynamic_vertices, expected_dynamic, sizeof(expected_dynamic)) &&
            !memcmp(f.dynamic_vertices + 6, expected_dynamic + 3,
                3 * sizeof(*expected_dynamic)));
        MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
        memset(&observed, 0, sizeof(observed));
        MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) == 0 &&
            observed.count == 2 && observed.events[0] == 601 &&
            observed.events[1] == 602);
        rf_core_mixed_reset(&f);
        MIXED_CHECK(!f.dynamic_vertex_count && !f.reference_vertex_count &&
            f.reference_vertex_capacity >= 9);
        item.index_count = 3;
    }
    /* GPU skinning keeps one finalized palette per instance and immutable bind
     * corner/influence input beside the CPU-skinned Draw reference rollback stream. */
    {
        struct rasterfall_dynamic_draw_vertex reference[3];
        struct rasterfall_skinned_draw_vertex bind[3], expected_bind[3];
        struct rasterfall_model_skin_palette_bone palette[2], expected_palette[2];
        unsigned int skin_instance;
        memset(reference,0,sizeof(reference)); memset(bind,0,sizeof(bind));
        memset(palette,0,sizeof(palette));
        for (i=0;i<2;++i) {
            palette[i].rotation[0]=palette[i].rotation[4]=palette[i].rotation[8]=1.0;
            palette[i].position[0]=(double)(i+10); palette[i].rest[1]=(int32_t)(i+20);
        }
        for (i=0;i<3;++i) for (unsigned int k=0;k<4;++k) {
            bind[i].influences[k].bone0=(uint16_t)(k&1);
            bind[i].influences[k].bone1=(uint16_t)((k+1)&1);
            bind[i].influences[k].weight=32768;
            bind[i].influences[k].type=(uint8_t)(k!=0);
        }
        memcpy(expected_bind,bind,sizeof(bind)); memcpy(expected_palette,palette,sizeof(palette));
        rf_core_mixed_reset(&f);
        MIXED_CHECK(rf_core_mixed_begin(&f,registry,128,72)==0 &&
            rf_core_mixed_skin_instance(&f,palette,2,&skin_instance)==0 &&
            skin_instance==0);
        item.index_count=3; item.material.color=603;
        MIXED_CHECK(rf_core_mixed_skinned_draw(&f,&view,&instance,&item,
            reference,bind,3,skin_instance)==0);
        memset(bind,0x5a,sizeof(bind)); memset(palette,0x5a,sizeof(palette));
        MIXED_CHECK(f.skin_instance_count==1 && f.skin_palette_count==2 &&
            f.skin_vertex_count==3 && f.draws[0].skin_instance==0 &&
            !memcmp(f.skin_vertices,expected_bind,sizeof(expected_bind)) &&
            !memcmp(f.skin_palette,expected_palette,sizeof(expected_palette)));
        MIXED_CHECK(rf_core_mixed_freeze(&f)==0);
        memset(&observed,0,sizeof(observed));
        MIXED_CHECK(rf_core_mixed_execute(&f,&executor,&observed)==0 &&
            observed.count==1 && observed.events[0]==603);
        rf_core_mixed_reset(&f);
        MIXED_CHECK(!f.skin_instance_count && !f.skin_palette_count &&
            !f.skin_vertex_count && f.skin_instance_capacity>=1 &&
            f.skin_palette_capacity>=2 && f.skin_vertex_capacity>=3);
        MIXED_CHECK(rf_core_mixed_begin(&f,registry,128,72)==0 &&
            rf_core_mixed_skin_instance(&f,expected_palette,2,&skin_instance)==0 &&
            rf_core_mixed_skinned_draw(&f,&view,&instance,&item,NULL,
                expected_bind,3,skin_instance)==0 &&
            f.dynamic_vertex_count==3 && !f.reference_vertex_count &&
            f.draws[0].dynamic_vertex_count==3 &&
            !f.draws[0].reference_vertex_count &&
            rf_core_mixed_freeze(&f)==0);
        memset(&observed,0,sizeof(observed));
        MIXED_CHECK(rf_core_mixed_execute(&f,&executor,&observed)==0 &&
            observed.count==1 && observed.events[0]==603);
        rf_core_mixed_reset(&f);
        MIXED_CHECK(rf_core_mixed_begin(&f,registry,128,72)==0 &&
            rf_core_mixed_skin_instance(&f,expected_palette,2,&skin_instance)==0);
        expected_bind[0].influences[0].bone0=2;
        MIXED_CHECK(rf_core_mixed_skinned_draw(&f,&view,&instance,&item,
            reference,expected_bind,3,skin_instance)==0 &&
            rf_core_mixed_freeze(&f)==0 &&
            rf_core_mixed_execute(&f,&executor,&observed)<0 &&
            f.state==RF_CORE_MIXED_FROZEN);
    }
    /* Failed execution is not eligible for retry or a final Post/present. */
    rf_core_mixed_reset(&f);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(rf_core_mixed_raster(&f, RF_RENDER_LAYER_WORLD, commands, 2) == 0);
    MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
    memset(&observed, 0, sizeof(observed)); observed.fail_span = 2;
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 &&
        f.state == RF_CORE_MIXED_FAILED && observed.count == 1 && !observed.finished);
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 && observed.calls == 2);
    rf_core_mixed_reset(&f);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
    memset(&observed, 0, sizeof(observed)); observed.fail_finish = 1;
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 &&
        f.state == RF_CORE_MIXED_FAILED && observed.finished == 1 && !observed.calls);
    rf_core_mixed_reset(&f);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
    memset(&observed, 0, sizeof(observed));
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) == 0 &&
        !observed.calls && observed.finished == 1);
    /* A subsequent registry frame must never resurrect an old frozen plan,
     * even if the same generation was pinned again. */
    rf_core_mixed_reset(&f);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
    rasterfall_resources_frame_complete(registry);
    mesh = NULL; /* retired allocation was released by the real destructor */
    MIXED_CHECK(rasterfall_resources_frame_begin(registry) == 0);
    memset(&observed, 0, sizeof(observed));
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 && !observed.checked);
    mesh = tlibc_malloc(sizeof(*mesh));
    MIXED_CHECK(mesh != NULL);
    memset(mesh, 0, sizeof(*mesh));
    mesh->index_count = 3; mesh->primitive_count = 1;
    registry->slots[0].model = mesh; registry->slots[0].generation++;
    registry->slots[0].active = 1;
    instance.mesh = mesh; instance.mesh_handle.generation = registry->slots[0].generation;
    rf_core_mixed_reset(&f);
    MIXED_CHECK(rf_core_mixed_begin(&f, registry, 128, 72) == 0);
    MIXED_CHECK(rf_core_mixed_draw(&f, &view, &instance, &item) == 0);
    MIXED_CHECK(rf_core_mixed_freeze(&f) == 0);
    rasterfall_resources_frame_complete(registry);
    MIXED_CHECK(rasterfall_resources_frame_begin(registry) == 0);
    MIXED_CHECK(rasterfall_resources_pin(registry, instance.mesh_handle) == 0);
    MIXED_CHECK(rf_core_mixed_execute(&f, &executor, &observed) < 0 && !observed.checked);
    rasterfall_resources_frame_complete(registry);
    registry->frame_epoch = ULLONG_MAX;
    MIXED_CHECK(rasterfall_resources_frame_begin(registry) < 0);
done:
    rf_core_mixed_destroy(&f);
    /* The fixture owns one empty model. Avoid double freeing if retirement
     * already collected it, while also handling early assertion failures. */
    if (registry && mesh && registry->slots[0].model == mesh) {
        rasterfall_resources_invalidate(registry);
        rasterfall_resources_frame_complete(registry);
        mesh = NULL;
    }
    tlibc_free(mesh); tlibc_free(registry);
    if (failure) printf("Core mixed frame FAILED at line %d\n", failure);
#undef MIXED_CHECK
    return failure ? -1 : 0;
}
