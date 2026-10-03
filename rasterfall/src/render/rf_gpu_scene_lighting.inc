/* Presentation light extraction, independent of CPU world-light fields. */
static void scene_light_insert(struct rf_gpu_lighting *out,const struct rf_gpu_light *light,
    const struct camera *camera)
{
    float score[RF_GPU_LIGHT_CAP+1];unsigned count=out->count;
    struct rf_gpu_light candidates[RF_GPU_LIGHT_CAP+1];
    memcpy(candidates,out->lights,count*sizeof(*candidates));candidates[count]=*light;
    for(unsigned i=0;i<=count;++i) {
        float dx=candidates[i].position_radius[0]-camera->x,dy=candidates[i].position_radius[1]-camera->y;
        float dz=candidates[i].position_radius[2]-camera->z;
        score[i]=candidates[i].color_intensity[3]/(1+(dx*dx+dy*dy+dz*dz)/(512*512));
    }
    unsigned at=count;
    while(at && score[at]>score[at-1]) {
        struct rf_gpu_light t=candidates[at-1];candidates[at-1]=candidates[at];candidates[at]=t;
        float f=score[at-1];score[at-1]=score[at];score[at]=f;--at;
    }
    out->count=count<RF_GPU_LIGHT_CAP?count+1:count;
    memcpy(out->lights,candidates,out->count*sizeof(*candidates));
}
static int scene_lighting_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_world_resources *world,const struct camera *camera)
{
    struct rf_gpu_lighting lights;rf_gpu_lighting_default(&lights);
    /* Sky variants change the one shared sun, including WORLD shadows. */
    const char *sky=getenv("RF_GPU_SKY_PRESET"),*sky_time=getenv("RF_GPU_SKY_TIME");
    if(sky && !strcmp(sky,"rain")) {
        lights.sky_cloud[0]=0.64f;lights.sky_cloud[1]=1.65f;
        lights.sky_weather[3]=1.5f;
        lights.sun_color[3]=2.8f;
    } else if(sky && !strcmp(sky,"warm")) {
        lights.sun_direction[0]=-0.86f;lights.sun_direction[1]=0.36f;lights.sun_direction[2]=-0.36f;
        lights.sun_color[1]=0.79f;lights.sun_color[2]=0.60f;lights.sun_color[3]=3.0f;
        lights.sky_weather[3]=1.25f;
    } else if(sky && sky[0] && strcmp(sky,"clear")) {
        fprintf(stderr,"Invalid RF_GPU_SKY_PRESET (clear/rain/warm)\n");return -1;
    }
    double seconds=probe->layers ? (double)probe->layers->host_time_ms/1000.0 : 0.0;
    if(sky_time && sky_time[0]) {
        char *end=NULL;seconds=strtod(sky_time,&end);
        if(!end || *end || !(seconds>=0 && seconds<=1000000)) {
            fprintf(stderr,"Invalid RF_GPU_SKY_TIME (seconds 0..1000000)\n");return -1;
        }
    }
    lights.sky_weather[0]=(float)(seconds*0.004);
    lights.sky_weather[1]=(float)(seconds*0.0015);
    for(unsigned i=0;!(probe->layers && probe->layers->fixed_lighting) && i<world->prop_count;++i) {
        const struct toy_map_prop *p=&world->prop_items[i].prop;
        if(p->asset_id!=RASTERFALL_PROP_ASSET_LAMP_POST) continue;
        struct rf_gpu_light light={0};
        light.position_radius[0]=(float)p->x;light.position_radius[1]=(float)(-900+p->y+2300*p->scale_milli/1000);
        light.position_radius[2]=(float)p->z;light.position_radius[3]=6144;
        light.color_intensity[0]=1;light.color_intensity[1]=0.76f;light.color_intensity[2]=0.46f;light.color_intensity[3]=28;
        light.direction_outer[1]=-1;light.direction_outer[3]=0.35f;light.inner_shadow[0]=0.7f;
        scene_light_insert(&lights,&light,camera);
    }
    if(probe->layers && !probe->layers->fixed_lighting && probe->layers->source_effects) {
        const struct rasterfall_effects *effects=probe->layers->source_effects;
        for(unsigned i=0;i<RASTERFALL_EFFECT_INSTANCE_SLOTS;++i) {
            const struct rasterfall_effect_instance *p=&effects->instances[i];
            if(!p->active || (p->kind!=RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_CORE &&
                p->kind!=RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH &&
                p->kind!=RASTERFALL_EFFECT_INSTANCE_KIND_PROJECTILE_FLASH)) continue;
            struct rf_gpu_light light={0};
            light.position_radius[0]=(float)p->x;light.position_radius[1]=(float)p->y;light.position_radius[2]=(float)p->z;
            int explosion=p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH;
            light.position_radius[3]=explosion?8192:3072;
            light.color_intensity[0]=1;light.color_intensity[1]=0.52f;light.color_intensity[2]=0.12f;
            float fade=p->lifetime_ms>0?1.0f-(float)p->age_ms/p->lifetime_ms:1;
            if(fade<0) fade=0;
            light.color_intensity[3]=(explosion?70:14)*fade;
            light.direction_outer[3]=-1;scene_light_insert(&lights,&light,camera);
        }
    }
    if(probe->layers && !probe->layers->fixed_lighting && probe->layers->flashlight) {
        struct rf_gpu_light light={0};
        light.position_radius[0]=(float)camera->x;light.position_radius[1]=(float)camera->y;light.position_radius[2]=(float)camera->z;
        light.position_radius[3]=15360;light.color_intensity[0]=light.color_intensity[1]=light.color_intensity[2]=1;
        light.color_intensity[3]=45;
        light.direction_outer[0]=(float)camera->sy*camera->pitch_cy/1048576;
        light.direction_outer[1]=(float)camera->pitch_sy/1024;
        light.direction_outer[2]=(float)camera->cy*camera->pitch_cy/1048576;
        light.direction_outer[3]=0.85f;light.inner_shadow[0]=0.94f;scene_light_insert(&lights,&light,camera);
    }
    if(probe->layers && !probe->layers->fixed_lighting) {
        struct rf_gpu_light work={0};
        if(rf_weaver_gpu_work_light(probe,work.position_radius)) {
            work.position_radius[3]=650;
            work.color_intensity[0]=.22f;work.color_intensity[1]=.78f;
            work.color_intensity[2]=.86f;work.color_intensity[3]=1.2f;
            work.direction_outer[3]=-1;
            scene_light_insert(&lights,&work,camera);
        }
    }
    if(scene_lighting_lab_visible(probe,camera)) {
        for(unsigned i=0;i<2;++i) {
            struct rf_gpu_light light={0};
            scene_lighting_lab_position(i,probe->layers->lighting_time_ms,light.position_radius);
            light.position_radius[3]=12000;light.color_intensity[0]=i?0.2f:1;
            light.color_intensity[1]=i?0.45f:0.55f;light.color_intensity[2]=i?1:0.2f;
            light.color_intensity[3]=220;light.direction_outer[1]=-1;
            light.direction_outer[3]=0.3f;light.inner_shadow[0]=0.65f;
            scene_light_insert(&lights,&light,camera);
        }
    }
    return rf_gpu_graphics_set_lighting(probe->graphics,&lights);
}
