/* No window or timing fixture: exercise real CPU warm/discard ownership and
 * registry guards. Driver/present behavior is verified by native smoke. */
int rf_core_frame_lifecycle_logic_test(void)
{
    struct rf_core core={0};
    struct toy_renderer renderer={0};
    struct rasterfall_resource_registry *registry=rasterfall_render_resources();
    int world_depth=1,vm_depth=2;
    unsigned char coverage=1;
    int failure=0;
#define FRAME_CHECK(value) do { if(!(value)) { failure=__LINE__;goto done; } } while(0)
    core.renderer=&renderer;
    renderer.frame_budget_ms=200;renderer.frame_deadline_us=123;
    FRAME_CHECK(!registry->frame_active);
    FRAME_CHECK(rf_core_prepare_cpu_world_frame(&core,7)==0);
    FRAME_CHECK(!renderer.frame_budget_ms && !renderer.frame_deadline_us);
    FRAME_CHECK(!core.cpu_warm_world_generation && core.cpu_frame_world_generation==7);
    FRAME_CHECK(rasterfall_resources_frame_begin(registry)==0);
    FRAME_CHECK(rasterfall_resources_frame_begin(registry)<0);
    /* A renderer with no pool has idle zero counts; discard needs no fake
     * done=count assignment to make a before-dispatch cancellation reusable. */
    renderer.job_cancelled=1;
    FRAME_CHECK(!renderer.worker_count && !renderer.job_done_count);
    FRAME_CHECK(rf_core_discard_frame(&core)==0 && !registry->frame_active &&
        !renderer.job_cancelled && !renderer.frame_deadline_us);
    FRAME_CHECK(rasterfall_resources_frame_begin(registry)==0);
    registry->slots[0].pinned=1;
    renderer.depth=&vm_depth;renderer.cmd_count=5;
    /* Synthetic in-flight state checks refusal only; real thread startup and
     * cancellation/drain are exercised by the isolated native worker harness. */
    renderer.worker_count=1;renderer.job_done_count=0;
    core.viewmodel_active=1;core.world_depth=&world_depth;
    toy_renderer_bind_coverage(&renderer,&coverage,1);
    FRAME_CHECK(rf_core_discard_frame(&core)<0);
    FRAME_CHECK(registry->frame_active && registry->slots[0].pinned==1 &&
        renderer.depth==&vm_depth && renderer.cmd_count==5);
    renderer.job_done_count=1;
    FRAME_CHECK(rf_core_discard_frame(&core)==0);
    FRAME_CHECK(!registry->frame_active && !registry->slots[0].pinned &&
        renderer.depth==&world_depth && !core.viewmodel_active && !renderer.cmd_count &&
        !renderer.coverage && !renderer.coverage_stride);
    FRAME_CHECK(!core.cpu_warm_world_generation && !core.cpu_frame_world_generation);
    FRAME_CHECK(rasterfall_resources_frame_begin(registry)==0);
    rasterfall_resources_frame_complete(registry);
    FRAME_CHECK(rf_core_prepare_cpu_world_frame(&core,7)==0);
    core_cpu_world_presented(&core);
    FRAME_CHECK(core.cpu_warm_world_generation==7 && renderer.frame_budget_ms==200);
    FRAME_CHECK(rf_core_prepare_cpu_world_frame(&core,7)==0 && renderer.frame_budget_ms==200);
    FRAME_CHECK(rf_core_prepare_cpu_world_frame(&core,8)==0 && !renderer.frame_budget_ms);
    FRAME_CHECK(rf_core_discard_frame(&core)==0 && core.cpu_warm_world_generation==7);
    FRAME_CHECK(rf_core_prepare_cpu_world_frame(&core,8)==0 && !renderer.frame_budget_ms);
    core_cpu_world_presented(&core);
    FRAME_CHECK(core.cpu_warm_world_generation==8 && renderer.frame_budget_ms==200);
    core.gpu_frame.renderer=RF_CORE_RENDERER_GPU_SCENE;
    FRAME_CHECK(rf_core_prepare_cpu_world_frame(&core,9)<0 && renderer.frame_budget_ms==200);
    FRAME_CHECK(rf_core_discard_frame(&core)<0);
done:
    rasterfall_resources_frame_complete(registry);
    if(failure)__printf("core-frame: FAIL line=%d\n",failure);
    else __printf("core-frame: PASS CPU world warmup, successful-present budget, drained discard, VM restore, registry next-begin, GPU rejection\n");
#undef FRAME_CHECK
    return failure!=0;
}
