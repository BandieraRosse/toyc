/* Explicit preview task. The worker exclusively owns scene rendering while
 * the main thread polls input and presents the independent loading surface. */
#ifdef TOYC_WINDOWS
#include "rf_gpu_graphics.h"
#include "pthread.h"
#undef effects
static struct {
    struct rasterfall_session *prepared;
    struct rf_gpu_scene_world_resources resources;
    struct rf_gpu_scene_world_gpu_probe target;
    struct camera camera;
    int selected,phase;
    uint64_t serial;
} rf_table_preview;
static int rf_table_preview_close(struct rf_game_runtime *r,struct rf_gpu_scene_world_gpu_probe *main)
{
    if(main && main->graphics) {
        if(rf_gpu_graphics_scene_retire(main->graphics)<0)return -1;
        rf_gpu_graphics_scene_video_at(main->graphics,0,NULL,0,0,0,0);
    }
    r->preloaded_session=NULL;
    rf_gpu_scene_world_gpu_probe_close(&rf_table_preview.target);
    rf_gpu_scene_world_resources_invalidate(&rf_table_preview.resources);
    if(rf_table_preview.prepared) {
        rasterfall_session_unload(rf_table_preview.prepared);
        free(rf_table_preview.prepared);rf_table_preview.prepared=NULL;
    }
    rf_table_preview.phase=0;rf_table.preview_state=0;rf_table.preview_requested=0;rf_table.preview_percent=0;
    return 0;
}
static int rf_table_preview_render(struct rf_game_runtime *r,struct rf_gpu_vulkan_context *gpu,int *stage)
{
    struct rasterfall_session *p=rf_table_preview.prepared;
    struct preview_values {
        struct rf_gpu_scene_world_input_v2 world[RF_GPU_SCENE_MAX_WORLD_V2];
        struct rf_gpu_scene_world_render_frame_v1 map;
        struct rf_gpu_scene_world_floor_frame_v1 floor;
        struct rf_gpu_scene_world_prop_frame_v1 props;
        struct rf_gpu_scene_local_frame source;
        struct rf_gpu_scene_enemy_frame_v1 actors;
        struct rf_gpu_scene_pose_v1 poses[TOY_GAME_MAX_ACTORS];
        struct rf_gpu_scene_flag_frame_v1 flags;
        struct rf_gpu_scene_projectile_frame_v1 projectiles;
        struct rf_gpu_scene_interactable_frame_v1 items;
        struct rasterfall_effects efx;
        struct rasterfall_render_context context;
    } *v=calloc(1,sizeof(*v));
    if(!v)return -1;
    uint32_t count=0,poses=0;int result=-1;
    uint64_t gen=p->scene_local.world_generation,id=p->scene_local.frame_id+1;
    v->context.session=p;v->context.effects=&v->efx;v->context.net=&r->net;
    v->context.gpu_scene_lighting=1;v->context.textures_enabled=1;
    v->context.character_gpu_skinning=r->render_context.character_gpu_skinning;
    v->context.model_texture=r->render_context.model_texture;
    v->context.wall_texture=r->render_context.wall_texture;
    rasterfall_render_bind(&v->context);
    rf_gpu_scene_enemy_begin(id,gen);
    rf_gpu_scene_enemy_aux_camera(NULL);rf_gpu_scene_enemy_unit_camera(NULL);
    if(rf_gpu_scene_enemy_collect_independent(&rf_table_preview.camera,0,gen)<0 ||
       rf_gpu_scene_actor_collect_independent(&rf_table_preview.camera,640,360)<0 ||
       rf_gpu_scene_enemy_freeze(&v->actors)<0 ||
       rf_gpu_scene_world_render_freeze(&p->map_ops,1,id,gen,v->world,RF_GPU_SCENE_MAX_WORLD_V2,&count,&v->map)<0 ||
       rf_gpu_scene_world_floor_freeze(&p->map_ops,rasterfall_world_uses_authored_ground(p->world_id),id,gen,&v->floor)<0 ||
       rf_gpu_scene_world_prop_freeze(&p->map_ops,id,gen,&v->props)<0 ||
       rf_gpu_scene_local_freeze_presentation(&p->scene_local,&p->game_state,&rf_table_preview.camera,640,360,1,v->world,count,&v->source)<0 ||
       rf_gpu_scene_pose_extract_all(&v->source,v->poses,TOY_GAME_MAX_ACTORS,&poses)<0 ||
       rf_gpu_scene_flag_freeze(p,id,gen,&v->flags)<0 ||
       rf_gpu_scene_projectile_freeze(&p->game_state,id,gen,&v->projectiles)<0 ||
       rf_gpu_scene_interactable_freeze(p,&v->efx,1,id,gen,&v->items)<0)goto done;
    for(unsigned i=0;i<v->actors.modular_count;++i) {
        if(poses>=TOY_GAME_MAX_ACTORS)goto done;
        v->poses[poses++]=v->actors.modular[i];
    }
    if(rf_gpu_scene_world_resources_prepare(&rf_table_preview.resources,&v->source.snapshot,
        &v->map,&v->floor,&v->props)<0)goto done;
    struct rf_gpu_scene_layers_input layers={0};struct rf_gpu_scene_world_gpu_probe_stats stats={0};
    layers.world_only=1;layers.map=&v->map;layers.props=&v->props;
    layers.source_game=&p->game_state;layers.source_effects=&v->efx;layers.actor_presentations=&v->actors;
    rf_weaver_frame(p,&layers.weaver);
    rf_table_preview.target.offscreen_only=1;rf_table_preview.target.quiet=1;
    rf_table_preview.target.layers=&layers;
    __atomic_store_n(stage,3,__ATOMIC_RELEASE);
    result=rf_gpu_scene_world_gpu_probe_frame(&rf_table_preview.target,gpu,&rf_table_preview.resources,
        &rf_table_preview.camera,640,360,v->poses,poses,&v->flags,&v->projectiles,&v->items,
        &v->actors,r->render_context.model_texture,&stats,NULL);
    rf_table_preview.target.layers=NULL;
done:
    rasterfall_render_bind(&r->render_context);free(v);return result;
}
struct rf_table_preview_job {
    struct rf_game_runtime *runtime;
    struct rf_gpu_vulkan_context *gpu;
    enum rasterfall_world_id world;
    uint64_t seed;
    int stage,done,cancel,result;
};
static void *rf_table_preview_worker(void *argument)
{
    struct rf_table_preview_job *job=argument;
    struct rf_game_runtime *r=job->runtime;
    struct rasterfall_session *p=calloc(1,sizeof(*p));
    rf_table_preview.prepared=p;job->result=-1;
    if(!p)goto done;
    p->player_movement=r->session->player_movement;p->gameplay_config=r->session->gameplay_config;
    if(rasterfall_session_load(p,rasterfall_world_map_path(job->world))<0)goto done;
    if(__atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE))goto done;
    rasterfall_session_reset(p,&rf_table_preview.camera,job->seed);
    p->scene_local.world_generation=0x100000000ULL+(++rf_table_preview.serial);
    __atomic_store_n(&job->stage,2,__ATOMIC_RELEASE);
    if(__atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE))goto done;
    job->result=rf_table_preview_render(r,job->gpu,&job->stage);
done:
    __atomic_store_n(&job->done,1,__ATOMIC_RELEASE);return NULL;
}
/* Display-only estimate approaches each stage's cap; never unlocks deployment. */
static int rf_table_preview_estimate(int stage,int64_t elapsed_us)
{
    int base=stage==1?10:stage==2?40:75;
    int cap=stage==1?39:stage==2?74:95;
    int64_t scale=stage==1?2000000:stage==2?4000000:6000000;
    if(elapsed_us<0)elapsed_us=0;
    if(elapsed_us>600000000)elapsed_us=600000000;
    return base+(int)((cap-base)*elapsed_us/(elapsed_us+scale));
}
static int rf_table_preview_prepare(struct rf_game_runtime *r,struct rf_core *core,
    struct rf_gpu_vulkan_context *gpu,struct rf_gpu_scene_world_gpu_probe *main)
{
    struct rf_table_preview_job job={0};pthread_t worker;
    int ui_failed=0,last_stage=1;
    uint32_t *background=NULL,background_width=0,background_height=0;
    int64_t stage_started=rf_core_time_us(core),audit_at=0,next_paint=0;
    if(main && main->graphics && rf_gpu_graphics_scene_retire(main->graphics)<0)return -1;
    if(toy_window_begin_loading(core->window)<0)return -1;
    /* Read only once, before handing the shared device queue to the worker. */
    if(main && main->graphics && rf_gpu_graphics_scene_snapshot(main->graphics,
        &background,&background_width,&background_height)<0) {
        toy_window_end_loading(core->window);return -1;
    }
    job.runtime=r;job.gpu=gpu;job.world=rf_table.maps[rf_table_preview.selected].world;
    job.seed=r->session->seed?r->session->seed:1;job.stage=1;
    if(pthread_create(&worker,NULL,rf_table_preview_worker,&job)) {
        free(background);
        toy_window_end_loading(core->window);
        rf_table.preview_state=-1;rf_table_preview.phase=-1;return 0;
    }
    while(!__atomic_load_n(&job.done,__ATOMIC_ACQUIRE)) {
        struct rf_input_frame input;struct toy_surface surface;
        if(rf_core_poll_events_timeout(core,8)<0 || rf_core_get_input_frame(core,&input)<0)ui_failed=1;
        if(ui_failed || rf_core_should_exit(core)) {
            __atomic_store_n(&job.cancel,1,__ATOMIC_RELEASE);continue;
        }
        int ready=toy_window_begin_frame(core->window,&surface);
        if(ready<0){ui_failed=1;continue;}
        if(!ready)continue;
        int stage=__atomic_load_n(&job.stage,__ATOMIC_ACQUIRE);
        int64_t now=rf_core_time_us(core);
        if(stage!=last_stage){last_stage=stage;stage_started=now;}
        if(rf_table_preview.selected==rf_table.selected && !__atomic_load_n(&job.cancel,__ATOMIC_ACQUIRE)) {
            rf_table.preview_state=stage==1?1:2;
            int percent=rf_table_preview_estimate(stage,now-stage_started);
            if(percent>rf_table.preview_percent)rf_table.preview_percent=percent;
        }
        if(rf_table.open)rf_table_input(r,surface.width,surface.height,input.pointer_x,input.pointer_y,
            core->events.button_pressed && core->events.button==BTN_LEFT,
            action_pressed(&input,RF_ACTION_CANCEL),action_pressed(&input,RF_ACTION_UI_UP),
            action_pressed(&input,RF_ACTION_UI_DOWN),action_pressed(&input,RF_ACTION_CONFIRM));
        if(!rf_table.open || rf_table_preview.selected!=rf_table.selected)
            __atomic_store_n(&job.cancel,1,__ATOMIC_RELEASE);
        if(now<next_paint)continue;
        next_paint=now+16667;
        struct rasterfall_canvas canvas=rasterfall_canvas_surface(&surface);
        rasterfall_canvas_rect(&canvas,0,0,surface.width,surface.height,0x101F2D,255);
        if(background) {
            for(int y=0;y<surface.height;++y) {
                uint32_t *row=(uint32_t *)((unsigned char *)surface.pixels+(size_t)y*surface.stride);
                const uint32_t *source=background+(uint64_t)y*background_height/surface.height*background_width;
                if((uint32_t)surface.width==background_width)memcpy(row,source,(size_t)surface.width*4);
                else for(int x=0;x<surface.width;++x)row[x]=source[(uint64_t)x*background_width/surface.width];
            }
        }
        if(rf_table.open)rf_table_draw(&canvas);
        else {
            rasterfall_canvas_text(&canvas,32,32,"正在结束后台预览，返回大厅…",0xDBF2EF);
            rasterfall_canvas_rect(&canvas,input.pointer_x-4,input.pointer_y,9,1,0xFFFFFF,255);
            rasterfall_canvas_rect(&canvas,input.pointer_x,input.pointer_y-4,1,9,0xFFFFFF,255);
        }
        if(canvas.failed || toy_window_present_loading(core->window)<0)ui_failed=1;
        if(getenv("RF_UI_AUDIT") && now>=audit_at) {
            __printf("TABLE-LOADING {\"stage\":%d,\"percent\":%d,\"selected\":%d,\"open\":%d,\"pointer_x\":%d,\"pointer_y\":%d,\"pending\":%d}\n",
                stage,rf_table.preview_percent,rf_table.selected,rf_table.open,
                rf_table.pointer_x,rf_table.pointer_y,r->device_service.pending_world);
            fflush(stdout);audit_at=now+100000;
        }
    }
    /* Completion publication precedes join. No resource is read, moved or freed
     * by the main thread until the worker has stopped using the scene owner. */
    pthread_join(worker,NULL);toy_window_end_loading(core->window);free(background);
    if(!rf_table.open)rf_core_set_pointer_lock(core,!r->rts_active);
    if(ui_failed)return -1;
    if(!rf_table.open || rf_core_should_exit(core) ||
       __atomic_load_n(&job.cancel,__ATOMIC_ACQUIRE) || rf_table_preview.selected!=rf_table.selected) {
        int requested=rf_table.preview_requested;
        if(rf_table_preview_close(r,main)<0)return -1;
        if(rf_table.open && !rf_core_should_exit(core))rf_table.preview_requested=requested;
        return 0;
    }
    if(job.result<0) {
        if(rf_table_preview_close(r,main)<0)return -1;
        rf_table.preview_map=rf_table.selected;rf_table.preview_state=-1;rf_table_preview.phase=-1;
        __printf("TABLE-PREVIEW failed map=%d\n",rf_table.selected);return 0;
    }
    rf_table_preview.phase=3;rf_table.preview_state=3;rf_table.preview_percent=100;
    r->preloaded_session=rf_table_preview.prepared;
    __printf("TABLE-PREVIEW ready map=%d simulation_steps=0\n",rf_table.selected);return 0;
}
static int rf_table_preview_tick(struct rf_game_runtime *r,struct rf_gpu_vulkan_context *gpu,
    struct rf_gpu_scene_world_gpu_probe *main,int enabled,struct rf_core *core)
{
    if(!rf_table.open || r->session->world_id!=RASTERFALL_WORLD_OUTPOST) {
        rf_table.preview_requested=0;
        if(rf_table_preview.phase)return rf_table_preview_close(r,main);
        return 0;
    }
    if(!enabled){rf_table.preview_state=4;rf_table.preview_map=rf_table.selected;return 0;}
    if(rf_table_preview.selected!=rf_table.selected ||
       (rf_table_preview.phase && !rf_table.preview_state)) {
        int requested=rf_table.preview_requested;
        if(rf_table_preview_close(r,main)<0)return -1;
        rf_table.preview_requested=requested;
    }
    rf_table_preview.selected=rf_table.selected;rf_table.preview_map=rf_table.selected;
    if(!rf_table_preview.phase) {
        if(!rf_table.preview_requested)return 0;
        rf_table.preview_requested=0;
        rf_table_preview.phase=1;rf_table.preview_state=1;rf_table.preview_percent=10;return 0;
    }
    if(rf_table_preview.phase==1) {
        return rf_table_preview_prepare(r,core,gpu,main);
    }
    return 0;
}
static void rf_table_preview_view(int width,int height)
{
    if(!rf_table.open)return;
    memset(&rf_player_aux,0,sizeof(rf_player_aux));
    if(rf_table.preview_state!=3 || rf_table.preview_map!=rf_table.selected)return;
    struct rf_table_layout o=rf_table_layout(width,height);
    rf_player_aux.kind=RF_GPU_AUX_FROZEN;rf_player_aux.visible=1;
    rf_player_aux.frozen_image=rf_table_preview.target.graphics;
    rf_player_aux.x=o.video.x;rf_player_aux.y=o.video.y;
    rf_player_aux.width=o.video.w;rf_player_aux.height=o.video.h;
}
#define effects (*active_effects)
#endif
