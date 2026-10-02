/* Behaviour tests use real raster depth, HDR and the normal lighting shaders. */
static unsigned lighting_test_rgb(uint32_t c)
{ return (c&255)+((c>>8)&255)+((c>>16)&255); }
/* Explicit diagnostic only: GPU timestamps exclude the capture readback.
 * Flat HDR background is the control; never use wall time as shader timing. */
static int sky_benchmark(struct rf_gpu_graphics *g)
{
    const unsigned width=1920,height=1080,pixels=width*height;
    struct rf_gpu_graphics_vertex v[6]={0};uint32_t ix[6],white=0xffffff;
    const int corners[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
    uint32_t *rgba=malloc(pixels*4);float *depth=malloc(pixels*4);
    struct rf_gpu_graphics_resource *r=NULL;struct rf_gpu_graphics_batch_item item={0};
    struct rf_gpu_lighting l;double samples[4][32],first[2]={0};int result=-1;
    if(!rgba || !depth) goto done;
    for(unsigned i=0;i<6;++i) {
        v[i].position[0]=corners[i][0]*width;v[i].position[1]=corners[i][1]*height;
        v[i].position[2]=1;ix[i]=i;
    }
    r=rf_gpu_graphics_resource_create(g,v,6,ix,6,&white,1,1);if(!r) goto done;
    item.resource=r;struct rf_gpu_graphics_draw *d=&item.draw;
    d->translation_scale[3]=1000;d->rotation[1]=1024;d->view[1]=1024;
    d->view[2]=724;d->view[3]=724;d->projection[0]=width;d->projection[1]=height;
    d->projection[2]=64;d->projection[3]=width*3/4;
    d->texture[0]=d->texture[1]=1;d->texture[3]=2;d->material[0]=0x718aa0;
    d->index_count=6;d->double_sided=1;
    rf_gpu_lighting_default(&l);
    if(rf_gpu_graphics_resize(g,width,height)<0 || rf_gpu_graphics_set_lighting(g,&l)<0) goto done;
    /* Interleave paired control/sky frames, alternating order to reduce drift
     * from laptop clocks and thermals. Report the median paired difference. */
    for(unsigned frame=0;frame<40;++frame) {
        for(unsigned pass=0;pass<2;++pass) {
            unsigned mode=pass^(frame&1);
            d->scene_layer=mode?RF_GPU_SCENE_SKY:RF_GPU_SCENE_WORLD;
            if(rf_gpu_graphics_scene_capture_at(g,&item,1,rgba,depth,pixels,frame*2+pass+1)<0 ||
                !g->scene_timing.valid) goto done;
            double ms=g->scene_timing.world_draw_ms;
            if(!frame) first[mode]=ms;
            if(frame>=8) samples[mode][frame-8]=ms;
            if(frame>=8 && mode) samples[3][frame-8]=g->scene_timing.sky_compute_ms;
        }
        if(frame>=8) samples[2][frame-8]=samples[1][frame-8]-samples[0][frame-8];
    }
    for(unsigned mode=0;mode<4;++mode) {
        for(unsigned i=1;i<32;++i) {
            double value=samples[mode][i];unsigned j=i;
            while(j && samples[mode][j-1]>value) {samples[mode][j]=samples[mode][j-1];--j;}
            samples[mode][j]=value;
        }
    }
    fprintf(stdout,"SKY-BENCH extent=1920x1080 scale=%u slot_bytes=%llu samples=32 warmup=8 flat_p50_ms=%.3f flat_p95_ms=%.3f sky_p50_ms=%.3f sky_p95_ms=%.3f increment_p50_ms=%.3f first_sample_gpu_ms=%.3f capture_readback_excluded=1 paired_alternating=1\n",
        g->sky_scale,(unsigned long long)(g->sky_buffer.size+64*64*64*8),
        samples[0][16],samples[0][30],samples[1][16],samples[1][30],samples[2][16],first[1]);
    result=0;
    fprintf(stdout,"SKY-COMPUTE samples=32 p50_ms=%.3f p95_ms=%.3f includes_noise_bake=0 excludes_hdr_composite=1\n",
        samples[3][16],samples[3][30]);
done:
    if(r) rf_gpu_graphics_resource_destroy(g,r);
    free(rgba);free(depth);return result;
}
static int sky_regression(struct rf_gpu_graphics *g)
{
    struct rf_gpu_graphics_vertex v[6]={0};
    const int corners[6][2]={{0,0},{96,0},{96,96},{0,0},{96,96},{0,96}};
    uint32_t indices[6],white=0xffffff,a[96*96],b[96*96];float depth[96*96];
    struct rf_gpu_graphics_batch_item item={0};struct rf_gpu_lighting l;
    struct rf_gpu_graphics_resource *r=NULL;int result=-1;
#define SKY_CHECK(x) do {if(!(x)){fprintf(stderr,"SKY FAIL line=%d\n",__LINE__);goto done;}}while(0)
    for(unsigned i=0;i<6;++i) {
        v[i].position[0]=corners[i][0];v[i].position[1]=corners[i][1];v[i].position[2]=1;indices[i]=i;
    }
    r=rf_gpu_graphics_resource_create(g,v,6,indices,6,&white,1,1);SKY_CHECK(r);
    item.resource=r;struct rf_gpu_graphics_draw *d=&item.draw;
    d->translation_scale[3]=1000;d->rotation[1]=1024;
    d->view[1]=1024;d->view[2]=724;d->view[3]=724;
    d->projection[0]=d->projection[1]=96;d->projection[2]=64;d->projection[3]=72;
    d->texture[0]=d->texture[1]=1;d->texture[3]=2;
    d->index_count=6;d->double_sided=1;d->scene_layer=RF_GPU_SCENE_SKY;
    rf_gpu_lighting_default(&l);
    SKY_CHECK(!rf_gpu_graphics_resize(g,96,96) && !rf_gpu_graphics_set_lighting(g,&l));
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,a,depth,96*96));
    for(unsigned i=0;i<96*96;++i) SKY_CHECK(depth[i]==0 && lighting_test_rgb(a[i])>0);
    // Translation cannot move a distant cloud or alter its deterministic sample.
    d->camera[0]=123456;d->camera[1]=9000;d->camera[2]=-65432;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    SKY_CHECK(!memcmp(a,b,sizeof(a)));
    d->view[0]=1024;d->view[1]=0;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    SKY_CHECK(memcmp(a,b,sizeof(a))!=0);
    // WORLD drawn after SKY must produce identical opaque pixels/depth.
    struct rf_gpu_graphics_batch_item pair[2]={item,item};
    pair[1].draw.scene_layer=RF_GPU_SCENE_WORLD;pair[1].draw.texture[3]=1;
    pair[1].draw.quality[2]=3;pair[1].draw.material[0]=0x426387;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&pair[1],1,a,depth,96*96));
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,pair,2,b,depth,96*96));
    SKY_CHECK(!memcmp(a,b,sizeof(a)));
    for(unsigned i=0;i<96*96;++i) SKY_CHECK(depth[i]>0);
    // Cloud-free sun follows the same light vector used by WORLD lighting.
    l.sky_cloud[1]=0;l.sun_direction[0]=1;l.sun_direction[1]=1;l.sun_direction[2]=0;
    SKY_CHECK(!rf_gpu_graphics_set_lighting(g,&l));
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,a,depth,96*96));
    l.sun_direction[0]=-1;
    SKY_CHECK(!rf_gpu_graphics_set_lighting(g,&l));
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    SKY_CHECK(lighting_test_rgb(a[48*96+48])>lighting_test_rgb(b[48*96+48])+30);
    // Zenith and lower hemisphere, including a resized target, remain valid.
    d->view[2]=1024;d->view[3]=0;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    d->view[2]=-1024;
    SKY_CHECK(!rf_gpu_graphics_resize(g,65,67));d->projection[0]=65;d->projection[1]=67;d->projection[3]=48;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    for(unsigned i=0;i<65*67;++i) SKY_CHECK(depth[i]==0 && lighting_test_rgb(b[i])>0);
    l.sky_cloud[3]=0;SKY_CHECK(rf_gpu_graphics_set_lighting(g,&l)<0);
    fprintf(stdout,"SKY PASS deterministic=1 translation_independent=1 rotation=1 shared_sun=1 depth_untouched=1 opaque_occlusion=1 poles_resize=1 invalid_parameters=1\n");
    result=0;
done:
    if(r) rf_gpu_graphics_resource_destroy(g,r);
    rf_gpu_lighting_default(&l);rf_gpu_graphics_set_lighting(g,&l);return result;
#undef SKY_CHECK
}
int rf_gpu_graphics_lighting_regression(struct rf_gpu_graphics *g)
{
    struct rf_gpu_graphics_vertex v[12]={0};
    const int corners[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    uint32_t indices[12],white=0xffffff;
    uint32_t *a=calloc(96*96,4),*b=calloc(96*96,4);
    float *da=calloc(96*96,4),*db=calloc(96*96,4);
    struct rf_gpu_graphics_batch_item items[2]={{0}};
    struct rf_gpu_graphics_resource *r=NULL;
    struct rf_gpu_lighting lights;
    int result=-1;unsigned shadow_pixels=0,spot_pixels=0,moving_pixels=0,material_pixels=0,style_pixels=0;
#define LIGHT_CHECK(x) do {if(!(x)){fprintf(stderr,"LIGHTING FAIL line=%d\n",__LINE__);goto done;}}while(0)
    LIGHT_CHECK(a && b && da && db);
    for(unsigned i=0;i<12;++i) {
        v[i].position[0]=corners[i%6][0]*(i<6?2048:380);
        v[i].position[1]=corners[i%6][1]*(i<6?2048:380);
        v[i].position[2]=i<6?4096:2048;
        for(unsigned k=0;k<3;++k) v[i].normals[k*3+2]=-32767;
        indices[i]=i;
    }
    r=rf_gpu_graphics_resource_create(g,v,12,indices,12,&white,1,1);LIGHT_CHECK(r);
    struct rf_gpu_graphics_draw *d=&items[0].draw;
    items[0].resource=items[1].resource=r;
    d->translation_scale[3]=1000;d->rotation[1]=1024;
    d->view[1]=d->view[3]=1024;d->projection[0]=d->projection[1]=64;
    d->projection[2]=64;d->projection[3]=48;d->texture[0]=d->texture[1]=1;
    d->material[0]=0x808080;d->material[1]=256;d->quality[2]=1;
    d->index_count=6;d->double_sided=1;
    items[1].draw=*d;items[1].draw.first_index=6;
    rf_gpu_lighting_default(&lights);
    lights.sun_direction[0]=-0.35f;lights.sun_direction[1]=0.2f;lights.sun_direction[2]=-1;
    LIGHT_CHECK(!rf_gpu_graphics_resize(g,64,64) && !rf_gpu_graphics_set_lighting(g,&lights));
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,2,b,db,96*96));
    for(unsigned p=0;p<64*64;++p) if(da[p]>0 && da[p]==db[p] &&
        lighting_test_rgb(a[p])>lighting_test_rgb(b[p])+15) ++shadow_pixels;
    LIGHT_CHECK(shadow_pixels>8);
    /* The old scene multiplier is not part of GPU illumination. */
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
    d->material[1]=32;LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
    LIGHT_CHECK(!memcmp(a,b,64*64*4));d->material[1]=256;
    lights.sun_color[3]=0;lights.environment[0]=lights.environment[1]=lights.environment[2]=0.01f;
    lights.count=1;lights.lights[0].position_radius[0]=-1000;lights.lights[0].position_radius[2]=2800;
    lights.lights[0].position_radius[3]=6000;
    lights.lights[0].color_intensity[0]=lights.lights[0].color_intensity[1]=lights.lights[0].color_intensity[2]=1;
    lights.lights[0].color_intensity[3]=5;lights.lights[0].direction_outer[3]=-1;
    LIGHT_CHECK(!rf_gpu_graphics_set_lighting(g,&lights) && !rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
    lights.lights[0].position_radius[0]=1000;
    LIGHT_CHECK(!rf_gpu_graphics_set_lighting(g,&lights) && !rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
    for(unsigned p=0;p<64*64;++p) if(a[p]!=b[p]) ++moving_pixels;
    LIGHT_CHECK(moving_pixels>100);
    lights.lights[0].position_radius[0]=-700;lights.lights[0].position_radius[2]=0;lights.lights[0].color_intensity[3]=30;
    lights.lights[0].direction_outer[2]=1;lights.lights[0].direction_outer[3]=0.5f;
    lights.lights[0].inner_shadow[0]=0.8f;
    LIGHT_CHECK(!rf_gpu_graphics_set_lighting(g,&lights));
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,2,b,db,96*96));
    for(unsigned p=0;p<64*64;++p) if(da[p]>0 && da[p]==db[p] &&
        lighting_test_rgb(a[p])>lighting_test_rgb(b[p])+15) ++spot_pixels;
    LIGHT_CHECK(spot_pixels>8);
    lights.lights[0].direction_outer[3]=-1;lights.lights[0].position_radius[0]=1000;
    lights.lights[0].position_radius[2]=2800;
    LIGHT_CHECK(!rf_gpu_graphics_set_lighting(g,&lights));
    d->roughness=0.15f;d->metallic=1;
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
    d->roughness=0.85f;LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
    for(unsigned p=0;p<64*64;++p) if(a[p]!=b[p]) ++material_pixels;
    LIGHT_CHECK(material_pixels>100);
    d->metallic=0;d->roughness=0.7f;
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
    d->quality[2]=2;LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
    for(unsigned p=0;p<64*64;++p) if(a[p]!=b[p]) ++style_pixels;
    LIGHT_CHECK(style_pixels>100);
    LIGHT_CHECK(!rf_gpu_graphics_resize(g,96,96));d->projection[0]=d->projection[1]=96;d->projection[3]=72;
    LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
    lights.count=RF_GPU_LIGHT_CAP+1;LIGHT_CHECK(rf_gpu_graphics_set_lighting(g,&lights)<0);
    LIGHT_CHECK(!sky_regression(g));
    if(getenv("RF_GPU_SKY_BENCH")) LIGHT_CHECK(!sky_benchmark(g));
    fprintf(stdout,"LIGHTING PASS shadow_pixels=%u spot_shadow_pixels=%u moving_light_pixels=%u material_pixels=%u style_pixels=%u baked_independent=1 resize=1\n",
        shadow_pixels,spot_pixels,moving_pixels,material_pixels,style_pixels);
    result=0;
done:
    if(r) rf_gpu_graphics_resource_destroy(g,r);
    free(a);free(b);free(da);free(db);return result;
#undef LIGHT_CHECK
}
