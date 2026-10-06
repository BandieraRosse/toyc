/* Independent persistent GPU camera/asset views. Updates consume the exact
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
    lighting.environment[0]=150.796447f;lighting.environment[1]=163.362818f;lighting.environment[2]=188.495559f;
    lighting.sun_direction[0]=-.35f;lighting.sun_direction[1]=.65f;lighting.sun_direction[2]=-.68f;
    lighting.sun_color[0]=lighting.sun_color[1]=lighting.sun_color[2]=1.0f;lighting.sun_color[3]=180.0f;
    lighting.count=2;
    for(unsigned i=0;i<lighting.count;++i) {
        struct rf_gpu_light *fill=&lighting.lights[i];
        fill->position_radius[0]=i?512:-512;fill->position_radius[1]=256;
        fill->position_radius[2]=-768;fill->position_radius[3]=4096;
        fill->color_intensity[0]=fill->color_intensity[1]=fill->color_intensity[2]=1;
        fill->color_intensity[3]=120.0f;fill->direction_outer[3]=-1;
    }
    if(rf_gpu_graphics_set_lighting(probe->graphics,&lighting)<0 ||
        rf_gpu_graphics_scene_background(probe->graphics,.018f,.030f,.045f)<0)return -1;
    return rf_gpu_graphics_scene_offscreen(probe->graphics,probe->batch,draws,frame_id);
}

static void scene_aux_prepare_one(struct rf_gpu_scene_world_gpu_probe *probe,unsigned slot_index,
    struct rf_gpu_vulkan_context *context,struct rf_gpu_scene_world_resources *world,
    const struct camera *main_camera,const struct rf_gpu_scene_pose_v1 *poses,unsigned pose_count,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct rf_gpu_scene_interactable_frame_v1 *interactables,
    const struct rf_gpu_scene_enemy_frame_v1 *enemies,const struct toy_texture_view *texture)
{
    struct rf_gpu_scene_aux_slot *slot=&probe->aux[slot_index];
    const struct rf_gpu_scene_aux_view *view=probe->layers?
        (slot_index?probe->layers->unit_view:probe->layers->aux_view):NULL;
    rf_gpu_graphics_scene_video_at(probe->graphics,slot_index,NULL,0,0,0,0);
    if(!view || !view->visible || probe->offscreen_only) {
        slot->visible=0;slot->status.state=RF_GPU_AUX_HIDDEN;return;
    }
    int changed=!slot->visible || slot->id!=view->stable_id ||
        slot->generation!=view->generation || slot->kind!=view->kind ||
        slot->world_generation!=world->world_generation;
    if(changed) {
        slot->next_us=0;slot->attempts=0;
        slot->status.state=RF_GPU_AUX_CONNECTING;
        slot->id=view->stable_id;slot->generation=view->generation;
        slot->world_generation=world->world_generation;slot->kind=view->kind;
    }
    slot->visible=1;
    unsigned width=view->render_width?view->render_width:320;
    unsigned height=view->render_height?view->render_height:180;
    unsigned rate=view->refresh_hz?view->refresh_hz:12;
    if(rate>15)rate=15;
    if(rate<1)rate=1;
    if(width<64 || height<64 || width>640 || height>360 ||
        !view->stable_id || !view->generation || view->width<=0 || view->height<=0 ||
        view->kind<RF_GPU_AUX_WORLD || view->kind>RF_GPU_AUX_WEAPON) {
        slot->status.state=RF_GPU_AUX_UNAVAILABLE;return;
    }
    if(slot->status.last_update_us>view->now_us)slot->next_us=0;
    if(view->now_us>=slot->next_us && slot->attempts<3) {
        int64_t start=rf_core_clock_now_us();
        struct rf_gpu_scene_world_gpu_probe_stats stats={0};
        struct rf_gpu_graphics_stats before={0},after={0};
        int result=-1;
        if(!slot->owner)slot->owner=calloc(1,sizeof(*slot->owner));
        if(slot->owner) {
            slot->owner->offscreen_only=1;slot->owner->quiet=1;
            rf_gpu_graphics_get_stats(slot->owner->graphics,&before);
            if(view->kind==RF_GPU_AUX_WEAPON)
                result=scene_aux_weapon(slot->owner,context,view,width,height,enemies->frame_id);
            else {
                struct rf_gpu_scene_layers_input layers=*probe->layers;
                if(slot->owner->graphics)rf_gpu_graphics_scene_background(slot->owner->graphics,0,0,0);
                layers.aux_view=layers.unit_view=NULL;layers.world_only=1;layers.show_viewmodel=0;
                layers.ui_context=NULL;layers.ui_layout=NULL;layers.light_camera=main_camera;
                slot->owner->shared_parent=probe;
                slot->owner->layers=&layers;
                result=rf_gpu_scene_world_gpu_probe_frame(slot->owner,context,world,
                    &view->camera,width,height,poses,pose_count,flags,projectiles,
                    interactables,enemies,texture,&stats,NULL);
                slot->owner->layers=NULL;slot->owner->shared_parent=NULL;
            }
            rf_gpu_graphics_get_stats(slot->owner->graphics,&after);
        }
        slot->status.last_cpu_us=rf_core_clock_now_us()-start;
        if(result<0) {
            if(slot->owner)rf_gpu_scene_world_gpu_probe_close(slot->owner);
            ++slot->attempts;slot->status.state=RF_GPU_AUX_UNAVAILABLE;
            slot->next_us=view->now_us+1000000;
            if(!probe->quiet)__printf("SCENE-AUX unavailable kind=%d attempt=%u\n",view->kind,slot->attempts);
            return;
        }
        slot->attempts=0;slot->status.state=RF_GPU_AUX_LIVE;
        slot->status.frames++;slot->status.last_update_us=view->now_us;
        slot->status.last_upload_bytes=after.mesh_upload_bytes+after.texture_upload_bytes-
            before.mesh_upload_bytes-before.texture_upload_bytes;
        struct rf_gpu_scene_timing timing;
        rf_gpu_graphics_scene_timing(slot->owner->graphics,&timing);
        slot->status.last_gpu_ms=timing.world_draw_ms;
        slot->status.gpu_time_valid=timing.valid;
        /* Absolute phases retain the requested rate without drifting back
         * into simultaneous updates after a slow frame. Source changes still
         * request their first fresh frame immediately. */
        uint64_t period=1000000/rate;
        slot->next_us=view->now_us-view->now_us%period+slot_index*(period/2);
        if(slot->next_us<=view->now_us)slot->next_us+=period;
    }
    if(slot->status.state==RF_GPU_AUX_LIVE && slot->owner &&
        rf_gpu_graphics_scene_video_at(probe->graphics,slot_index,slot->owner->graphics,
            view->x,view->y,view->width,view->height)<0)
        slot->status.state=RF_GPU_AUX_UNAVAILABLE;
}

static void scene_aux_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_vulkan_context *context,struct rf_gpu_scene_world_resources *world,
    const struct camera *main_camera,const struct rf_gpu_scene_pose_v1 *poses,unsigned pose_count,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct rf_gpu_scene_interactable_frame_v1 *interactables,
    const struct rf_gpu_scene_enemy_frame_v1 *enemies,const struct toy_texture_view *texture)
{
    for(unsigned i=0;i<2;++i)scene_aux_prepare_one(probe,i,context,world,main_camera,
        poses,pose_count,flags,projectiles,interactables,enemies,texture);
}
