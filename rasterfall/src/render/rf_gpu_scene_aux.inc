/* One persistent GPU camera/asset preview. World updates consume the exact
 * frame values used by the main view; no renderer history is advanced twice.
 * The target is never mapped. Resources survive hide/show and are drained by
 * the containing Scene owner. Failures clear video and retry at most 3 times. */
static int scene_aux_weapon(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_vulkan_context *context,const struct rf_gpu_scene_aux_view *view,
    unsigned width,unsigned height,uint64_t frame_id)
{
    struct rf_weaver_transform transform,yaw,tilt;
    uint32_t palette[15],draws=0;
    struct camera camera={0};
    struct rf_mesh_weaver_frame frame={0};
    struct rf_gpu_lighting lighting;
    if(view->weapon<0 || view->weapon>=TOY_GAME_WEAPON_COUNT)return -1;
    if(!probe->graphics)probe->graphics=rf_gpu_graphics_create(context);
    if(!probe->graphics || rf_gpu_graphics_resize(probe->graphics,width,height)<0)return -1;
    if(!probe->weaver)probe->weaver=calloc(1,sizeof(*probe->weaver));
    if(!probe->weaver)return -1;
    struct rf_weaver_gpu_mesh *mesh=&probe->weaver->weapons[view->weapon];
    rf_weaver_transform_identity(&transform);rf_weaver_gpu_palette(&transform,1,palette);
    if(!mesh->resource && rf_weaver_gpu_weapon_create(probe,view->weapon,palette)<0)return -1;
    double center[3];
    for(unsigned k=0;k<3;++k) {
        center[k]=((double)mesh->source.minimum[k]+mesh->source.maximum[k])*.5;
    }
    /* Authored weapons point along local Z. Begin at a readable side/quarter
     * view, with the barrel horizontal, instead of looking into the muzzle. */
    rf_weaver_transform_axis(&yaw,1,(65+view->rotation_degrees%360)*3.141592653589793/180.0);
    rf_weaver_transform_axis(&tilt,0,-10*3.141592653589793/180.0);
    rf_weaver_transform_multiply(&transform,&tilt,&yaw);
    for(unsigned k=0;k<3;++k)transform.position[k]=-
        (transform.rotation[k*3]*center[0]+transform.rotation[k*3+1]*center[1]+
         transform.rotation[k*3+2]*center[2]);
    rf_weaver_gpu_palette(&transform,1,palette);
    if(rf_gpu_graphics_skinned_resource_update(probe->graphics,mesh->resource,
        mesh->vertex_count,NULL,0,palette,15)!=0)return -1;
    /* Fit the transformed true bounds to both axes, including depth and the
     * 64 RFU near plane. This works for pistols and long rifles alike. */
    double distance=80,focal=width*.75;
    for(unsigned corner=0;corner<8;++corner) {
        double source[3],point[3];
        for(unsigned k=0;k<3;++k)source[k]=(corner&(1u<<k))?mesh->source.maximum[k]:mesh->source.minimum[k];
        rf_weaver_transform_point(&transform,source,point);
        for(unsigned k=0;k<3;++k)point[k]*=512.0/RF_WEAVER_LOCAL_UNITS;
        double required=fabs(point[0])*focal/(width*.41)-point[2];
        if(required>distance)distance=required;
        required=fabs(point[1])*focal/(height*.41)-point[2];
        if(required>distance)distance=required;
        required=80-point[2];if(required>distance)distance=required;
    }
    camera.z=-(int)(distance+1);
    camera.cy=camera.pitch_cy=1024;
    for(unsigned i=0;i<mesh->run_count;++i) {
        const struct rf_weaver_gpu_run *run=&mesh->runs[i];
        if(rf_weaver_gpu_append(probe,&frame,&camera,width,height,mesh->resource,
            &run->material,run->first,run->count,run->material.color,0,0,0,&draws)<0)return -1;
    }
    rf_gpu_lighting_default(&lighting);
    lighting.environment[0]=.48f;lighting.environment[1]=.52f;lighting.environment[2]=.60f;
    lighting.sun_direction[0]=-.35f;lighting.sun_direction[1]=.65f;lighting.sun_direction[2]=-.68f;
    lighting.sun_color[0]=lighting.sun_color[1]=lighting.sun_color[2]=1.0f;lighting.sun_color[3]=1.8f;
    lighting.count=2;
    for(unsigned i=0;i<lighting.count;++i) {
        struct rf_gpu_light *fill=&lighting.lights[i];
        fill->position_radius[0]=i?512:-512;fill->position_radius[1]=256;
        fill->position_radius[2]=-768;fill->position_radius[3]=4096;
        fill->color_intensity[0]=fill->color_intensity[1]=fill->color_intensity[2]=1;
        fill->color_intensity[3]=1.2f;fill->direction_outer[3]=-1;
    }
    if(rf_gpu_graphics_set_lighting(probe->graphics,&lighting)<0 ||
        rf_gpu_graphics_scene_background(probe->graphics,.018f,.030f,.045f)<0)return -1;
    return rf_gpu_graphics_scene_offscreen(probe->graphics,probe->batch,draws,frame_id);
}

static void scene_aux_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_vulkan_context *context,struct rf_gpu_scene_world_resources *world,
    const struct camera *main_camera,const struct rf_gpu_scene_pose_v1 *poses,unsigned pose_count,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct rf_gpu_scene_interactable_frame_v1 *interactables,
    const struct rf_gpu_scene_enemy_frame_v1 *enemies,const struct toy_texture_view *texture)
{
    const struct rf_gpu_scene_aux_view *view=probe->layers?probe->layers->aux_view:NULL;
    rf_gpu_graphics_scene_video(probe->graphics,NULL,0,0,0,0);
    if(!view || !view->visible || probe->offscreen_only) {
        probe->aux_visible=0;probe->aux_status.state=RF_GPU_AUX_HIDDEN;return;
    }
    int changed=!probe->aux_visible || probe->aux_id!=view->stable_id ||
        probe->aux_generation!=view->generation || probe->aux_kind!=view->kind ||
        probe->aux_world_generation!=world->world_generation;
    if(changed) {
        probe->aux_next_us=0;probe->aux_attempts=0;
        probe->aux_status.state=RF_GPU_AUX_CONNECTING;
        probe->aux_id=view->stable_id;probe->aux_generation=view->generation;
        probe->aux_world_generation=world->world_generation;probe->aux_kind=view->kind;
    }
    probe->aux_visible=1;
    unsigned width=view->render_width?view->render_width:320;
    unsigned height=view->render_height?view->render_height:180;
    unsigned rate=view->refresh_hz?view->refresh_hz:12;
    if(rate>15)rate=15;
    if(rate<1)rate=1;
    if(width<64 || height<64 || width>640 || height>360 ||
        !view->stable_id || !view->generation || view->width<=0 || view->height<=0 ||
        view->kind<RF_GPU_AUX_WORLD || view->kind>RF_GPU_AUX_WEAPON) {
        probe->aux_status.state=RF_GPU_AUX_UNAVAILABLE;return;
    }
    if(probe->aux_status.last_update_us>view->now_us)probe->aux_next_us=0;
    if(view->now_us>=probe->aux_next_us && probe->aux_attempts<3) {
        int64_t start=rf_core_clock_now_us();
        struct rf_gpu_scene_world_gpu_probe_stats stats={0};
        struct rf_gpu_graphics_stats before={0},after={0};
        int result=-1;
        if(!probe->aux)probe->aux=calloc(1,sizeof(*probe->aux));
        if(probe->aux) {
            probe->aux->offscreen_only=1;probe->aux->quiet=1;
            rf_gpu_graphics_get_stats(probe->aux->graphics,&before);
            if(view->kind==RF_GPU_AUX_WEAPON)
                result=scene_aux_weapon(probe->aux,context,view,width,height,enemies->frame_id);
            else {
                struct rf_gpu_scene_layers_input layers=*probe->layers;
                if(probe->aux->graphics)rf_gpu_graphics_scene_background(probe->aux->graphics,0,0,0);
                layers.aux_view=NULL;layers.world_only=1;layers.show_viewmodel=0;
                layers.ui_context=NULL;layers.ui_layout=NULL;layers.light_camera=main_camera;
                probe->aux->layers=&layers;
                result=rf_gpu_scene_world_gpu_probe_frame(probe->aux,context,world,
                    &view->camera,width,height,poses,pose_count,flags,projectiles,
                    interactables,enemies,texture,&stats,NULL);
                probe->aux->layers=NULL;
            }
            rf_gpu_graphics_get_stats(probe->aux->graphics,&after);
        }
        probe->aux_status.last_cpu_us=rf_core_clock_now_us()-start;
        if(result<0) {
            if(probe->aux)rf_gpu_scene_world_gpu_probe_close(probe->aux);
            ++probe->aux_attempts;probe->aux_status.state=RF_GPU_AUX_UNAVAILABLE;
            probe->aux_next_us=view->now_us+1000000;
            if(!probe->quiet)__printf("SCENE-AUX unavailable kind=%d attempt=%u\n",view->kind,probe->aux_attempts);
            return;
        }
        probe->aux_attempts=0;probe->aux_status.state=RF_GPU_AUX_LIVE;
        probe->aux_status.frames++;probe->aux_status.last_update_us=view->now_us;
        probe->aux_status.last_upload_bytes=after.mesh_upload_bytes+after.texture_upload_bytes-
            before.mesh_upload_bytes-before.texture_upload_bytes;
        struct rf_gpu_scene_timing timing;
        rf_gpu_graphics_scene_timing(probe->aux->graphics,&timing);
        probe->aux_status.last_gpu_ms=timing.world_draw_ms;
        probe->aux_status.gpu_time_valid=timing.valid;
        probe->aux_next_us=view->now_us+1000000/rate;
    }
    if(probe->aux_status.state==RF_GPU_AUX_LIVE && probe->aux &&
        rf_gpu_graphics_scene_video(probe->graphics,probe->aux->graphics,
            view->x,view->y,view->width,view->height)<0)
        probe->aux_status.state=RF_GPU_AUX_UNAVAILABLE;
}
