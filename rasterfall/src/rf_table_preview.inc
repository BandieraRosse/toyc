/* Preview starts only on an explicit UI request. Loading and rendering run
 * serially on successive main-thread frames, without stepping the session. */
#ifdef TOYC_WINDOWS
#include "rf_gpu_graphics.h"
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
    rf_table_preview.phase=0;rf_table.preview_state=0;rf_table.preview_requested=0;
    return 0;
}
static int rf_table_preview_render(struct rf_game_runtime *r,struct rf_gpu_vulkan_context *gpu)
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
    result=rf_gpu_scene_world_gpu_probe_frame(&rf_table_preview.target,gpu,&rf_table_preview.resources,
        &rf_table_preview.camera,640,360,v->poses,poses,&v->flags,&v->projectiles,&v->items,
        &v->actors,r->render_context.model_texture,&stats,NULL);
    rf_table_preview.target.layers=NULL;
done:
    rasterfall_render_bind(&r->render_context);free(v);return result;
}
static int rf_table_preview_tick(struct rf_game_runtime *r,struct rf_gpu_vulkan_context *gpu,
    struct rf_gpu_scene_world_gpu_probe *main,int enabled)
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
        rf_table_preview.phase=1;rf_table.preview_state=1;return 0;
    }
    if(rf_table_preview.phase==1) {
        rf_table_preview.prepared=calloc(1,sizeof(struct rasterfall_session));
        struct rasterfall_session *p=rf_table_preview.prepared;
        if(!p)goto failed;
        p->player_movement=r->session->player_movement;p->gameplay_config=r->session->gameplay_config;
        if(rasterfall_session_load(p,rasterfall_world_map_path(rf_table.maps[rf_table.selected].world))<0)goto failed;
        rasterfall_session_reset(p,&rf_table_preview.camera,r->session->seed?r->session->seed:1);
        p->scene_local.world_generation=0x100000000ULL+(++rf_table_preview.serial);
        rf_table_preview.phase=2;rf_table.preview_state=2;return 0;
    }
    if(rf_table_preview.phase==2) {
        if(rf_table_preview_render(r,gpu)<0)goto failed;
        rf_table_preview.phase=3;rf_table.preview_state=3;
        r->preloaded_session=rf_table_preview.prepared;
        __printf("TABLE-PREVIEW ready map=%d simulation_steps=0\n",rf_table.selected);
    }
    return 0;
failed:
    r->preloaded_session=NULL;rf_table.preview_state=-1;rf_table_preview.phase=-1;
    __printf("TABLE-PREVIEW failed map=%d\n",rf_table.selected);return 0;
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
