/* Presentation light extraction, independent of CPU world-light fields. */
static int scene_fixture_light(const struct toy_map_prop *p,struct rf_gpu_light *light)
{
    const struct rasterfall_prop_light_profile *profile=rasterfall_prop_light_profile(p->asset_id);
    if(!profile && p->asset_id!=RASTERFALL_PROP_ASSET_LAMP_POST)return 0;
    memset(light,0,sizeof(*light));
    light->inner_shadow[2]=1; /* Also present in the stable DDGI fixture list. */
    if(!profile) {
        light->position_radius[0]=(float)p->x;light->position_radius[1]=(float)(-900+p->y+2300*p->scale_milli/1000);
        light->position_radius[2]=(float)p->z;light->position_radius[3]=6144;
        light->color_intensity[0]=1;light->color_intensity[1]=0.76f;light->color_intensity[2]=0.46f;light->color_intensity[3]=28;
        light->direction_outer[1]=-1;light->direction_outer[3]=0.35f;light->inner_shadow[0]=0.7f;return 1;
    }
    double yaw=p->yaw_degrees*3.141592653589793/180.0;
    float s=(float)sin(yaw),c=(float)cos(yaw),scale=p->scale_milli/1000.0f;
    light->position_radius[0]=p->x+scale*(c*profile->position[0]+s*profile->position[2]);
    light->position_radius[1]=-900+p->y+scale*profile->position[1];
    light->position_radius[2]=p->z+scale*(-s*profile->position[0]+c*profile->position[2]);
    light->position_radius[3]=profile->radius*scale;
    for(unsigned k=0;k<3;++k)light->color_intensity[k]=profile->color[k];
    light->color_intensity[3]=profile->intensity;
    light->direction_outer[0]=c*profile->direction[0]+s*profile->direction[2];
    light->direction_outer[1]=profile->direction[1];
    light->direction_outer[2]=-s*profile->direction[0]+c*profile->direction[2];
    light->direction_outer[3]=profile->outer_cosine;light->inner_shadow[0]=profile->inner_cosine;return 1;
}
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
static int scene_architecture_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_world_resources *world)
{
    if(probe->lighting_world_generation==world->world_generation &&
       probe->lighting_map_generation==world->map_generation)return 0;
    uint32_t count=0,at=0;
    /* These are the same opaque meshes submitted to WORLD, including finite
     * slabs, stair treads and doorway lintels. Never consume collision boxes. */
    for(unsigned kind=0;kind<=RF_GPU_SCENE_WORLD_BOUNDARY;++kind) {
        if(kind==RF_GPU_SCENE_WORLD_BOX)continue;
        if(!world->opaque[kind].generation)continue;
        const struct rasterfall_model_asset *m=rasterfall_resources_resolve_active(&world->registry,world->opaque[kind]);
        if(!m || m->index_count%3 || count+m->index_count/3>65536)return -1;
        count+=m->index_count/3;
    }
    if(count+world->render_count>65536)return -1;
    count+=world->render_count;
    struct rf_gpu_occlusion_primitive *triangles=count?calloc(count,sizeof(*triangles)):NULL;
    if(count && !triangles)return -1;
    int result=-1;
    for(unsigned kind=0;kind<=RF_GPU_SCENE_WORLD_BOUNDARY;++kind) {
        if(kind==RF_GPU_SCENE_WORLD_BOX)continue;
        if(!world->opaque[kind].generation)continue;
        const struct rasterfall_model_asset *m=rasterfall_resources_resolve_active(&world->registry,world->opaque[kind]);
        if(!m || m->material_bytes<32 || m->vertex_bytes<12 ||
           !world_range(m,m->indices,(uint64_t)m->index_count*4) ||
           !world_range(m,m->vertices,(uint64_t)m->vertex_count*m->vertex_bytes) ||
           !world_range(m,m->primitives,(uint64_t)m->primitive_count*16) ||
           !world_range(m,m->materials,(uint64_t)m->material_count*m->material_bytes))goto done;
        for(unsigned p=0;p<m->primitive_count;++p) {
            const unsigned char *primitive=m->primitives+p*16;
            unsigned first=world_u32(primitive),n=world_u32(primitive+4),mat=world_u32(primitive+8);
            if(n%3 || first>m->index_count || n>m->index_count-first || mat>=m->material_count)goto done;
            const unsigned char *material=m->materials+mat*m->material_bytes;
            float x=(int32_t)world_u32(material+24),z=(int32_t)world_u32(material+28);
            for(unsigned i=0;i<n;i+=3) {
                if(at>=count)goto done;
                triangles[at].diffuse_rgb=world_u32(material)&0xffffffu;
                for(unsigned v=0;v<3;++v) {
                    unsigned id=world_u32(m->indices+(first+i+v)*4);
                    if(id>=m->vertex_count)goto done;
                    const unsigned char *point=m->vertices+id*m->vertex_bytes;
                    for(unsigned k=0;k<3;++k)triangles[at].p[v][k]=(int32_t)world_u32(point+k*4);
                    triangles[at].p[v][0]+=x;triangles[at].p[v][2]+=z;
                }
                ++at;
            }
        }
    }
    /* An authored BOX is already an exact solid. Avoid tracing its subdivided
     * faces: the analytic slab intersection is the same geometry, with doors
     * still represented by separate jambs/lintels, never a room-sized proxy. */
    for(unsigned i=0;i<world->render_count;++i) {
        const struct rf_gpu_scene_world_render_item_v1 *item=&world->render_items[i];
        const struct toy_map_draw *d=&item->draw;
        if(!item->visible || item->alpha!=255 || d->type!=TOY_MAP_DRAW_BOX ||
           !strncmp(d->text,"air_gate_",9) || d->a>=d->b || d->c>=d->d || d->f>=d->e)continue;
        struct rf_gpu_occlusion_primitive *t=&triangles[at++];t->solid_box=1;
        t->diffuse_rgb=d->color&0xffffffu;
        t->p[0][0]=d->a;t->p[0][1]=d->f-900;t->p[0][2]=d->c;
        t->p[1][0]=d->b;t->p[1][1]=d->e-900;t->p[1][2]=d->d;
    }
    result=rf_gpu_graphics_set_architecture(probe->graphics,triangles,at);
    if(!result) {
        struct rf_gpu_light fixtures[RF_GPU_INDIRECT_LIGHT_CAP];unsigned lamps=0;
        for(unsigned i=0;i<world->prop_count;++i) {
            struct rf_gpu_light light;
            if(!scene_fixture_light(&world->prop_items[i].prop,&light))continue;
            if(lamps==RF_GPU_INDIRECT_LIGHT_CAP){result=-1;goto done;}
            fixtures[lamps++]=light;
        }
        result=rf_gpu_graphics_set_indirect_lights(probe->graphics,fixtures,lamps);
    }
    if(!result){probe->lighting_world_generation=world->world_generation;probe->lighting_map_generation=world->map_generation;}
done:
    free(triangles);return result;
}
static int scene_lighting_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_world_resources *world,const struct camera *camera)
{
    if(scene_architecture_prepare(probe,world)<0)return -1;
    struct rf_gpu_lighting lights;rf_gpu_lighting_default(&lights);
    lights.disable_indirect=probe->layers && probe->layers->fixed_lighting;
    if(probe->layers && !probe->layers->world_only) {
        memcpy(lights.cutaway_bounds,probe->layers->cutaway_bounds,sizeof(lights.cutaway_bounds));
        memcpy(lights.cutaway_height,probe->layers->cutaway_height,sizeof(lights.cutaway_height));
    }
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
        struct rf_gpu_light light;
        if(scene_fixture_light(p,&light))scene_light_insert(&lights,&light,camera);
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
        const struct camera *lamp=probe->layers->light_camera ? probe->layers->light_camera : camera;
        struct rf_gpu_light light={0};
        light.position_radius[0]=(float)lamp->x;light.position_radius[1]=(float)lamp->y;light.position_radius[2]=(float)lamp->z;
        light.position_radius[3]=15360;light.color_intensity[0]=light.color_intensity[1]=light.color_intensity[2]=1;
        light.color_intensity[3]=45;
        light.direction_outer[0]=(float)lamp->sy*lamp->pitch_cy/1048576;
        light.direction_outer[1]=(float)lamp->pitch_sy/1024;
        light.direction_outer[2]=(float)lamp->cy*lamp->pitch_cy/1048576;
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
