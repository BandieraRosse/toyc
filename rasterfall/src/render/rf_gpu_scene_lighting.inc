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
        light->color_intensity[0]=1;light->color_intensity[1]=0.76f;light->color_intensity[2]=0.46f;
        light->color_intensity[3]=rf_gpu_light_peak_candela(6700,0.35f,0.7f);
        light->direction_outer[1]=-1;light->direction_outer[3]=0.35f;light->inner_shadow[0]=0.7f;return 1;
    }
    double yaw=p->yaw_degrees*3.141592653589793/180.0;
    float s=(float)sin(yaw),c=(float)cos(yaw),scale=p->scale_milli/1000.0f;
    light->position_radius[0]=p->x+scale*(c*profile->position[0]+s*profile->position[2]);
    light->position_radius[1]=-900+p->y+scale*profile->position[1];
    light->position_radius[2]=p->z+scale*(-s*profile->position[0]+c*profile->position[2]);
    light->position_radius[3]=profile->radius*scale;
    for(unsigned k=0;k<3;++k)light->color_intensity[k]=profile->color[k];
    light->color_intensity[3]=rf_gpu_light_peak_candela(profile->luminous_flux_lm,profile->outer_cosine,profile->inner_cosine);
    light->direction_outer[0]=c*profile->direction[0]+s*profile->direction[2];
    light->direction_outer[1]=profile->direction[1];
    light->direction_outer[2]=-s*profile->direction[0]+c*profile->direction[2];
    light->direction_outer[3]=profile->outer_cosine;light->inner_shadow[0]=profile->inner_cosine;return 1;
}
static void scene_light_insert(struct rf_gpu_lighting *out,const struct rf_gpu_light *light,
    const struct camera *camera)
{
    /* Persistent lights stay in map order and can never be displaced by camera
     * ranking. Only the bounded transient suffix uses the proximity policy. */
    if(light->inner_shadow[2]>0.5f) {
        if(out->count<RF_GPU_INDIRECT_LIGHT_CAP)out->lights[out->count++]=*light;
        return;
    }
    unsigned first=0;
    while(first<out->count && out->lights[first].inner_shadow[2]>0.5f)++first;
    unsigned cap=first+32;if(cap>RF_GPU_LIGHT_CAP)cap=RF_GPU_LIGHT_CAP;
    float score[RF_GPU_LIGHT_CAP+1];unsigned count=out->count;
    struct rf_gpu_light candidates[RF_GPU_LIGHT_CAP+1];
    memcpy(candidates,out->lights,count*sizeof(*candidates));candidates[count]=*light;
    for(unsigned i=first;i<=count;++i) {
        float dx=candidates[i].position_radius[0]-camera->x,dy=candidates[i].position_radius[1]-camera->y;
        float dz=candidates[i].position_radius[2]-camera->z;
        score[i]=candidates[i].color_intensity[3]/(1+(dx*dx+dy*dy+dz*dz)/(512*512));
    }
    unsigned at=count;
    while(at>first && score[at]>score[at-1]) {
        struct rf_gpu_light t=candidates[at-1];candidates[at-1]=candidates[at];candidates[at]=t;
        float f=score[at-1];score[at-1]=score[at];score[at]=f;--at;
    }
    out->count=count<cap?count+1:count;
    memcpy(out->lights,candidates,out->count*sizeof(*candidates));
}
struct scene_arch_floor_rect { int x0,x1,z0,z1,y;uint32_t color; };
static int scene_arch_floor_horizontal(const void *a,const void *b)
{
    const struct scene_arch_floor_rect *p=a,*q=b;
#define RECT_COMPARE(field) if(p->field!=q->field)return p->field<q->field?-1:1
    RECT_COMPARE(y);RECT_COMPARE(color);RECT_COMPARE(z0);RECT_COMPARE(z1);
    RECT_COMPARE(x0);RECT_COMPARE(x1);
    return 0;
#undef RECT_COMPARE
}
static int scene_arch_floor_vertical(const void *a,const void *b)
{
    const struct scene_arch_floor_rect *p=a,*q=b;
#define RECT_COMPARE(field) if(p->field!=q->field)return p->field<q->field?-1:1
    RECT_COMPARE(y);RECT_COMPARE(color);RECT_COMPARE(x0);RECT_COMPARE(x1);
    RECT_COMPARE(z0);RECT_COMPARE(z1);
    return 0;
#undef RECT_COMPARE
}
/* The floor producer subdivides planar rectangles for local render origins.
 * Recover those exact quads from the frozen mesh, then join only touching
 * rectangles with identical spans and reflectance. No holes are filled and
 * no render geometry, material boundary or collision input is changed. */
static int scene_architecture_floor(const struct rf_gpu_scene_world_resources *world,
    struct scene_arch_floor_rect **out,unsigned *count)
{
    *out=NULL;*count=0;
    if(!world->opaque[RF_GPU_SCENE_WORLD_FLOOR].generation)return 0;
    const struct rasterfall_model_asset *m=rasterfall_resources_resolve_active(
        &world->registry,world->opaque[RF_GPU_SCENE_WORLD_FLOOR]);
    if(!m || m->index_count%6 || m->material_bytes<32 || m->vertex_bytes<12 ||
       !world_range(m,m->indices,(uint64_t)m->index_count*4) ||
       !world_range(m,m->vertices,(uint64_t)m->vertex_count*m->vertex_bytes) ||
       !world_range(m,m->primitives,(uint64_t)m->primitive_count*16) ||
       !world_range(m,m->materials,(uint64_t)m->material_count*m->material_bytes))return -1;
    unsigned capacity=m->index_count/6,n=0;
    struct scene_arch_floor_rect *rects=capacity?malloc((size_t)capacity*sizeof(*rects)):NULL;
    if(capacity && !rects)return -1;
    for(unsigned p=0;p<m->primitive_count;++p) {
        const unsigned char *primitive=m->primitives+p*16;
        unsigned first=world_u32(primitive),indices=world_u32(primitive+4),mat=world_u32(primitive+8);
        if(indices%6 || first>m->index_count || indices>m->index_count-first || mat>=m->material_count)goto fail;
        const unsigned char *material=m->materials+mat*m->material_bytes;
        int ox=(int32_t)world_u32(material+24),oz=(int32_t)world_u32(material+28);
        for(unsigned i=0;i<indices;i+=6) {
            unsigned ids[6];int v[4][3];
            for(unsigned k=0;k<6;++k)ids[k]=world_u32(m->indices+(first+i+k)*4);
            if(ids[0]!=ids[3] || ids[2]!=ids[4] || n==capacity)goto fail;
            for(unsigned k=0;k<4;++k) {
                unsigned id=ids[k==3?5:k];if(id>=m->vertex_count)goto fail;
                for(unsigned a=0;a<3;++a)v[k][a]=(int32_t)world_u32(m->vertices+id*m->vertex_bytes+a*4);
            }
            if(v[0][1]!=v[1][1] || v[0][1]!=v[2][1] || v[0][1]!=v[3][1] ||
               v[0][0]!=v[3][0] || v[1][0]!=v[2][0] || v[0][2]!=v[1][2] ||
               v[2][2]!=v[3][2] || v[0][0]>=v[1][0] || v[0][2]>=v[2][2])goto fail;
            rects[n++]=(struct scene_arch_floor_rect){v[0][0]+ox,v[1][0]+ox,
                v[0][2]+oz,v[2][2]+oz,v[0][1],world_u32(material)&0xffffffu};
        }
    }
    if(n!=capacity)goto fail;
    qsort(rects,n,sizeof(*rects),scene_arch_floor_horizontal);
    unsigned joined=0;
    for(unsigned i=0;i<n;++i) {
        struct scene_arch_floor_rect *last=joined?&rects[joined-1]:NULL,*r=&rects[i];
        if(last && last->y==r->y && last->color==r->color && last->z0==r->z0 &&
           last->z1==r->z1 && last->x1==r->x0)last->x1=r->x1;
        else rects[joined++]=*r;
    }
    qsort(rects,joined,sizeof(*rects),scene_arch_floor_vertical);n=0;
    for(unsigned i=0;i<joined;++i) {
        struct scene_arch_floor_rect *last=n?&rects[n-1]:NULL,*r=&rects[i];
        if(last && last->y==r->y && last->color==r->color && last->x0==r->x0 &&
           last->x1==r->x1 && last->z1==r->z0)last->z1=r->z1;
        else rects[n++]=*r;
    }
    fprintf(stderr,"SCENE-ARCHITECTURE floor triangles=%u compact=%u exact_rectangles=1\n",m->index_count/3,n*2);
    *out=rects;*count=n;return 0;
fail:
    free(rects);fprintf(stderr,"SCENE-ARCHITECTURE invalid floor quad\n");return -1;
}
static int scene_architecture_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_world_resources *world)
{
    const char *clone=getenv("RF_GPU_AUX_LIGHTING_CLONE");
    if(probe->shared_parent && !(clone && !strcmp(clone,"0")) &&
        !rf_gpu_graphics_clone_lighting(probe->graphics,probe->shared_parent->graphics)) {
        probe->lighting_world_generation=world->world_generation;
        probe->lighting_map_generation=world->map_generation;return 0;
    }
    if(probe->lighting_world_generation==world->world_generation &&
       probe->lighting_map_generation==world->map_generation)return 0;
    struct scene_arch_floor_rect *floor=NULL;unsigned floor_count=0;
    if(scene_architecture_floor(world,&floor,&floor_count)<0)return -1;
    uint32_t count=0,at=0;
    /* These are the same opaque meshes submitted to WORLD, including finite
     * slabs, stair treads and doorway lintels. Never consume collision boxes. */
    for(unsigned kind=0;kind<=RF_GPU_SCENE_WORLD_BOUNDARY;++kind) {
        if(kind==RF_GPU_SCENE_WORLD_BOX || kind==RF_GPU_SCENE_WORLD_FLOOR)continue;
        if(!world->opaque[kind].generation)continue;
        const struct rasterfall_model_asset *m=rasterfall_resources_resolve_active(&world->registry,world->opaque[kind]);
        if(!m || m->index_count%3 || count+m->index_count/3>65536) {
            fprintf(stderr,"SCENE-ARCHITECTURE invalid/capacity kind=%u triangles=%u accumulated=%u limit=65536\n",
                kind,m?m->index_count/3:0,count);free(floor);return -1;
        }
        count+=m->index_count/3;
    }
    if(count+world->render_count+(uint64_t)floor_count*2>65536) {
        fprintf(stderr,"SCENE-ARCHITECTURE capacity triangles=%u render-items=%u limit=65536\n",
            count+floor_count*2,world->render_count);free(floor);return -1;
    }
    count+=world->render_count+floor_count*2;
    struct rf_gpu_occlusion_primitive *triangles=count?calloc(count,sizeof(*triangles)):NULL;
    if(count && !triangles){free(floor);return -1;}
    int result=-1;
    for(unsigned kind=0;kind<=RF_GPU_SCENE_WORLD_BOUNDARY;++kind) {
        if(kind==RF_GPU_SCENE_WORLD_BOX || kind==RF_GPU_SCENE_WORLD_FLOOR)continue;
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
    for(unsigned i=0;i<floor_count;++i) {
        const struct scene_arch_floor_rect *r=&floor[i];
        float vertices[4][3]={{r->x0,r->y,r->z0},{r->x1,r->y,r->z0},
            {r->x1,r->y,r->z1},{r->x0,r->y,r->z1}};
        const unsigned corners[2][3]={{0,1,2},{0,2,3}};
        for(unsigned t=0;t<2;++t) {
            triangles[at].diffuse_rgb=r->color;
            for(unsigned v=0;v<3;++v)memcpy(triangles[at].p[v],vertices[corners[t][v]],12);
            ++at;
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
        t->bottom_rgb=d->bottom_color;
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
        if(!result)result=rf_gpu_graphics_prepare_daylight(probe->graphics);
    }
    if(!result){probe->lighting_world_generation=world->world_generation;probe->lighting_map_generation=world->map_generation;}
done:
    free(floor);free(triangles);return result;
}
static int scene_lighting_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_world_resources *world,const struct camera *camera,uint64_t frame)
{
    int architecture_changed=probe->lighting_world_generation!=world->world_generation ||
        probe->lighting_map_generation!=world->map_generation;
    if(scene_architecture_prepare(probe,world)<0)return -1;
    struct rf_gpu_lighting lights;rf_gpu_lighting_default(&lights);
    lights.disable_indirect=probe->layers && probe->layers->fixed_lighting;
    lights.sky_illuminance=lights.disable_indirect?0:120;
    if(probe->layers && !probe->layers->world_only) {
        memcpy(lights.cutaway_bounds,probe->layers->cutaway_bounds,sizeof(lights.cutaway_bounds));
        memcpy(lights.cutaway_height,probe->layers->cutaway_height,sizeof(lights.cutaway_height));
    }
    /* Sky variants change the one shared sun, including WORLD shadows. */
    const char *sky=getenv("RF_GPU_SKY_PRESET"),*sky_time=getenv("RF_GPU_SKY_TIME");
    if(sky && !strcmp(sky,"rain")) {
        lights.sky_cloud[0]=0.64f;lights.sky_cloud[1]=1.65f;
        lights.sky_weather[3]=1.5f;
        lights.sun_color[3]=269.0f;
    } else if(sky && !strcmp(sky,"warm")) {
        lights.sun_direction[0]=-0.86f;lights.sun_direction[1]=0.36f;lights.sun_direction[2]=-0.36f;
        lights.sun_color[1]=0.79f;lights.sun_color[2]=0.60f;lights.sun_color[3]=246.28f;
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
            light.color_intensity[3]=rf_gpu_light_peak_candela((explosion?52000.0f:10400.0f)*fade,-1,0);
            light.direction_outer[3]=-1;scene_light_insert(&lights,&light,camera);
        }
    }
    if(probe->layers && !probe->layers->fixed_lighting && probe->layers->flashlight) {
        const struct camera *lamp=probe->layers->light_camera ? probe->layers->light_camera : camera;
        struct rf_gpu_light light={0};
        light.position_radius[0]=(float)lamp->x;light.position_radius[1]=(float)lamp->y;light.position_radius[2]=(float)lamp->z;
        light.position_radius[3]=15360;light.color_intensity[0]=light.color_intensity[1]=light.color_intensity[2]=1;
        light.color_intensity[3]=rf_gpu_light_peak_candela(3000.0f,0.85f,0.94f);
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
            work.color_intensity[2]=.86f;work.color_intensity[3]=rf_gpu_light_peak_candela(1000.0f,-1,0);
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
            light.color_intensity[3]=rf_gpu_light_peak_candela(i?30000.0f:45000.0f,0.3f,0.65f);light.direction_outer[1]=-1;
            light.direction_outer[3]=0.3f;light.inner_shadow[0]=0.65f;
            scene_light_insert(&lights,&light,camera);
        }
    }
    /* Explicit normal-map diagnostic: change the fixed sources in place,
     * through the production light APIs. Geometry, camera and gameplay stay
     * on their ordinary paths. Emissive lamp surfaces are not a lux reading. */
    const char *sequence=getenv("RF_GPU_FIXTURE_SEQUENCE");
    if(sequence && sequence[0]) {
        char *end=NULL;unsigned long interval=strtoul(sequence,&end,10);
        if(!end || *end || interval<1 || interval>100000 || !frame) {
            fprintf(stderr,"Invalid RF_GPU_FIXTURE_SEQUENCE (frames 1..100000)\n");return -1;
        }
        unsigned phase=(unsigned)(((frame-1)/interval)%4),count=0;
        struct rf_gpu_light fixtures[RF_GPU_INDIRECT_LIGHT_CAP];
        for(unsigned i=0;i<lights.count;++i)if(lights.lights[i].inner_shadow[2]>0.5f) {
            struct rf_gpu_light *light=&lights.lights[i];
            if(phase==1) {light->color_intensity[0]=4;light->color_intensity[1]=light->color_intensity[2]=0.05f;}
            if(phase==2)light->color_intensity[3]=0;
            if(count==RF_GPU_INDIRECT_LIGHT_CAP)return -1;
            fixtures[count++]=*light;
        }
        if(architecture_changed || probe->lighting_fixture_sequence_phase!=phase+1) {
            if(rf_gpu_graphics_set_indirect_lights(probe->graphics,fixtures,count)<0)return -1;
            probe->lighting_fixture_sequence_phase=phase+1;
            fprintf(stdout,"SCENE-FIXTURE-SEQUENCE frame=%llu phase=%u lights=%u diagnostic=1\n",
                (unsigned long long)frame,phase,count);
        }
    }
    return rf_gpu_graphics_set_lighting(probe->graphics,&lights);
}
