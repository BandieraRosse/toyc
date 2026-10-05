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
    struct rf_gpu_lighting l;double samples[6][32],first[3]={0};int result=-1;
    int saved_reference=g->sky_reference_scan;
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
    /* Interleave control, candidate skip and full scan, rotating order to reduce drift
     * from laptop clocks and thermals. Report the median paired difference. */
    for(unsigned frame=0;frame<40;++frame) {
        for(unsigned pass=0;pass<3;++pass) {
            unsigned mode=(pass+frame)%3;
            g->sky_reference_scan=mode==2;
            d->scene_layer=mode?RF_GPU_SCENE_SKY:RF_GPU_SCENE_WORLD;
            if(rf_gpu_graphics_scene_capture_at(g,&item,1,rgba,depth,pixels,frame*3+pass+1)<0 ||
                !g->scene_timing.valid) goto done;
            double ms=g->scene_timing.world_draw_ms;
            if(!frame) first[mode]=ms;
            if(frame>=8 && mode<2) samples[mode][frame-8]=ms;
            if(frame>=8 && mode) samples[mode+2][frame-8]=g->scene_timing.sky_compute_ms;
        }
        if(frame>=8) samples[2][frame-8]=samples[1][frame-8]-samples[0][frame-8];
        if(frame>=8) samples[5][frame-8]=samples[4][frame-8]-samples[3][frame-8];
    }
    for(unsigned mode=0;mode<6;++mode) {
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
    fprintf(stdout,"SKY-COMPUTE samples=32 p50_ms=%.3f p95_ms=%.3f reference_p50_ms=%.3f reference_p95_ms=%.3f skip_saving_p50_ms=%.3f includes_noise_bake=0 excludes_hdr_composite=1\n",
        samples[3][16],samples[3][30],samples[4][16],samples[4][30],samples[5][16]);
done:
    g->sky_reference_scan=saved_reference;
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
    int saved_reference=g->sky_reference_scan;
    unsigned saved_scale=g->sky_scale,solar_max_error=0,rotation_max_error=0;
    unsigned sky_scale_max_error=0,sky_scale_error_pixel=0,sky_scale_error_a=0,sky_scale_error_b=0;
    int sky_scale_error_yaw=0;
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
    // Empty-space skipping must preserve the full march, including sparse cloud,
    // weather wrapping, oblique rays, and dense weather with no useful skips.
    for(unsigned sample=0;sample<12;++sample) {
        l.sky_cloud[0]=sample%3==0?0.18f:(sample%3==1?0.42f:0.78f);
        l.sky_weather[0]=(float)sample*13.7f;l.sky_weather[1]=-(float)sample*9.1f;
        d->view[0]=(sample&1)?1024:0;d->view[1]=(sample&1)?0:1024;
        d->view[2]=(sample&2)?256:724;d->view[3]=(sample&2)?991:724;
        SKY_CHECK(!rf_gpu_graphics_set_lighting(g,&l));
        g->sky_reference_scan=0;
        SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,a,depth,96*96));
        g->sky_reference_scan=1;
        SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
        SKY_CHECK(!memcmp(a,b,sizeof(a)));
        // A subpixel camera rotation must not change the cloud integration
        // phase or cross a hard density cutoff into an unrelated bright patch.
        ++d->view[(sample&1)?1:0];
        SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,a,depth,96*96));
        for(unsigned pixel=0;pixel<96*96;++pixel) for(unsigned channel=0;channel<3;++channel) {
            unsigned ca=(a[pixel]>>(channel*8))&255,cb=(b[pixel]>>(channel*8))&255;
            unsigned error=ca>cb?ca-cb:cb-ca;
            if(error>rotation_max_error) rotation_max_error=error;
        }
    }
    SKY_CHECK(rotation_max_error<=16);
    g->sky_reference_scan=0;rf_gpu_lighting_default(&l);
    d->view[0]=0;d->view[1]=1024;d->view[2]=d->view[3]=724;
    SKY_CHECK(!rf_gpu_graphics_set_lighting(g,&l));
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
    // A small rotation sweeps the sub-degree disc across the low-resolution
    // sample grid. Its display pixels must agree with the full-resolution
    // reference: cloud quality is not allowed to make the solar disc jump.
    l.sun_direction[0]=1;
    SKY_CHECK(!rf_gpu_graphics_set_lighting(g,&l));
    for(int yaw=-16;yaw<=16;yaw+=4) {
        d->view[1]=yaw;
        g->sky_scale=4;SKY_CHECK(!gfx_sky_resize(g,96,96));
        SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,a,depth,96*96));
        g->sky_scale=1;SKY_CHECK(!gfx_sky_resize(g,96,96));
        SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
        for(unsigned i=0;i<96*96;++i) for(unsigned channel=0;channel<3;++channel) {
            unsigned ca=(a[i]>>(channel*8))&255,cb=(b[i]>>(channel*8))&255;
            unsigned error=ca>cb?ca-cb:cb-ca;
            if(error>sky_scale_max_error) {
                sky_scale_max_error=error;sky_scale_error_pixel=i;
                sky_scale_error_a=ca;sky_scale_error_b=cb;sky_scale_error_yaw=yaw;
            }
            // The disc remains within one pixel of the centre for this sweep.
            // Compare the disc and inner glare, excluding the low-resolution
            // atmosphere/cirrus and clamped reconstruction at screen edges.
            if(i%96>=40 && i%96<56 && i/96>=40 && i/96<56 && error>solar_max_error)
                solar_max_error=error;
        }
    }
    fprintf(stdout,"SKY SOLAR scale_roi_max_error=%u full_max_error=%u full_max_xy=%u,%u full_max_values=%u,%u yaw=%d\n",
        solar_max_error,sky_scale_max_error,sky_scale_error_pixel%96,sky_scale_error_pixel/96,
        sky_scale_error_a,sky_scale_error_b,sky_scale_error_yaw);
    SKY_CHECK(solar_max_error<=4);
    g->sky_scale=saved_scale;SKY_CHECK(!gfx_sky_resize(g,96,96));
    d->view[1]=0;
    // Zenith and lower hemisphere, including a resized target, remain valid.
    d->view[2]=1024;d->view[3]=0;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    d->view[2]=-1024;
    SKY_CHECK(!rf_gpu_graphics_resize(g,65,67));d->projection[0]=65;d->projection[1]=67;d->projection[3]=48;
    SKY_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,b,depth,96*96));
    for(unsigned i=0;i<65*67;++i) SKY_CHECK(depth[i]==0 && lighting_test_rgb(b[i])>0);
    l.sky_cloud[3]=0;SKY_CHECK(rf_gpu_graphics_set_lighting(g,&l)<0);
    fprintf(stdout,"SKY PASS deterministic=1 translation_independent=1 rotation=1 shared_sun=1 depth_untouched=1 opaque_occlusion=1 poles_resize=1 invalid_parameters=1 skip_reference_equal=12 cloud_rotation_max_error=%u solar_rotation_samples=9 solar_scale_max_error=%u\n",rotation_max_error,solar_max_error);
    result=0;
done:
    if(r) rf_gpu_graphics_resource_destroy(g,r);
    if(g->sky_scale!=saved_scale) {
        g->sky_scale=saved_scale;
        if(gfx_sky_resize(g,g->target.width,g->target.height)<0) result=-1;
    }
    g->sky_reference_scan=saved_reference;
    rf_gpu_lighting_default(&l);rf_gpu_graphics_set_lighting(g,&l);return result;
#undef SKY_CHECK
}
static int lighting_range_regression(struct rf_gpu_graphics *g)
{
    struct rf_gpu_graphics_vertex v[6]={0};
    const int corners[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    uint32_t indices[6],white=0xffffff,rgba[64*64];float depth[64*64];
    struct rf_gpu_graphics_batch_item item={0};struct rf_gpu_lighting lights;
    struct rf_gpu_graphics_resource *r=NULL;
    unsigned upper=0,lower=0,highlight[2]={0};int result=-1;
#define RANGE_CHECK(x) do {if(!(x)){fprintf(stderr,"LIGHTING RANGE FAIL line=%d\n",__LINE__);goto done;}}while(0)
    for(unsigned i=0;i<6;++i) {
        indices[i]=i;v[i].position[0]=corners[i][0]*1024;
        v[i].position[1]=corners[i][1]*1024;v[i].position[2]=2048;
        for(unsigned k=0;k<3;++k) {v[i].normals[k*3+1]=23170;v[i].normals[k*3+2]=-23170;}
    }
    r=rf_gpu_graphics_resource_create(g,v,6,indices,6,&white,1,1);RANGE_CHECK(r);
    item.resource=r;struct rf_gpu_graphics_draw *d=&item.draw;
    d->translation_scale[3]=1000;d->rotation[1]=1024;d->view[1]=d->view[3]=1024;
    d->projection[0]=d->projection[1]=64;d->projection[2]=64;d->projection[3]=48;
    d->texture[0]=d->texture[1]=1;d->material[0]=0x808080;d->material[1]=256;
    d->quality[2]=1;d->index_count=6;d->double_sided=1;
    rf_gpu_lighting_default(&lights);lights.sun_color[3]=0;
    RANGE_CHECK(!rf_gpu_graphics_resize(g,64,64) && !rf_gpu_graphics_set_lighting(g,&lights));
    RANGE_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,rgba,depth,64*64));
    RANGE_CHECK(depth[32*64+32]>0);upper=lighting_test_rgb(rgba[32*64+32]);
    /* With no sun or local light, the shading normal must still distinguish
     * the sky-facing and ground-facing sides of the same material. */
    for(unsigned i=0;i<6;++i) for(unsigned k=0;k<3;++k) v[i].normals[k*3+1]=-23170;
    RANGE_CHECK(!rf_gpu_graphics_triangle_resource_update(g,r,v,6));
    RANGE_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,rgba,depth,64*64));
    lower=lighting_test_rgb(rgba[32*64+32]);RANGE_CHECK(lower>0 && upper>lower+12);
    /* Finite HDR highlights above display white retain their ordering through
     * the real half-float target and tonemap, rather than both clipping white. */
    lights.environment[0]=lights.environment[1]=lights.environment[2]=0;
    RANGE_CHECK(!rf_gpu_graphics_set_lighting(g,&lights));d->material[0]=white;
    for(unsigned i=0;i<2;++i) {
        d->emissive=i?8.0f:4.0f;
        RANGE_CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,rgba,depth,64*64));
        uint32_t c=rgba[32*64+32];highlight[i]=lighting_test_rgb(c);
        RANGE_CHECK(depth[32*64+32]>0 && (c&255)<255 && ((c>>8)&255)<255 && ((c>>16)&255)<255);
    }
    RANGE_CHECK(highlight[1]>highlight[0]+3);
    fprintf(stdout,"LIGHTING RANGE PASS ambient_upper=%u ambient_lower=%u hdr4=%u hdr8=%u\n",
        upper,lower,highlight[0],highlight[1]);result=0;
done:
    if(r) rf_gpu_graphics_resource_destroy(g,r);
    rf_gpu_lighting_default(&lights);rf_gpu_graphics_set_lighting(g,&lights);return result;
#undef RANGE_CHECK
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
    /* Camera and light frusta are independent. Keep shadows from an offscreen
     * caster; compare pixels and depth with the uncullable reference across
     * translations, yaw, scale, near crossings and far cascade boundaries. */
    for (unsigned sample=0;sample<12;++sample) {
        struct rf_gpu_graphics_batch_item pair[2]={items[0],items[1]};
        pair[1].draw.translation_scale[0]=(int)(sample%4)*1800-2400;
        pair[1].draw.translation_scale[2]=sample<8?0:60000;
        pair[1].draw.translation_scale[3]=sample&1?1800:1000;
        pair[1].draw.rotation[0]=sample&2?724:0;
        pair[1].draw.rotation[1]=sample&2?724:1024;
        g->disable_draw_cull=1;
        LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,pair,2,a,da,96*96));
        g->disable_draw_cull=0;
        LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,pair,2,b,db,96*96));
        LIGHT_CHECK(!memcmp(a,b,64*64*4) && !memcmp(da,db,64*64*4));
    }
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
    /* Static building visibility also covers point lights and spots outside
     * the two dynamic shadow slots. Camera visibility is deliberately absent. */
    {
        struct rf_gpu_occlusion_primitive wall[2]={
            {{{-10000,-10000,3400},{10000,-10000,3400},{10000,10000,3400}},0},
            {{{-10000,-10000,3400},{10000,10000,3400},{-10000,10000,3400}},0}};
        lights.lights[0].position_radius[0]=0;
        lights.lights[0].position_radius[2]=2800;
        for(unsigned mode=0;mode<3;++mode) {
            lights.count=mode==1?3:1;
            lights.sun_color[3]=mode==2?2.9f:0;
            for(unsigned i=0;i<lights.count;++i) {
                lights.lights[i]=lights.lights[0];
                lights.lights[i].direction_outer[2]=1;
                lights.lights[i].direction_outer[3]=mode==1?0.1f:-1;
                lights.lights[i].inner_shadow[0]=0.5f;
                lights.lights[i].color_intensity[3]=mode==2?0:5;
            }
            LIGHT_CHECK(!rf_gpu_graphics_set_architecture(g,NULL,0));
            LIGHT_CHECK(!rf_gpu_graphics_set_lighting(g,&lights));
            LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
            LIGHT_CHECK(!rf_gpu_graphics_set_architecture(g,wall,2));
            LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
            unsigned darker=0;
            for(unsigned p=0;p<64*64;++p)if(da[p]>0 && da[p]==db[p] &&
                lighting_test_rgb(a[p])>lighting_test_rgb(b[p])+15)++darker;
            LIGHT_CHECK(darker>100);
            /* Resizing must retain architecture, including the AS descriptor;
             * rejected replacements must leave the previous scene usable. */
            if(mode==1) {
                LIGHT_CHECK(rf_gpu_graphics_set_architecture(g,NULL,1)<0);
                LIGHT_CHECK(!rf_gpu_graphics_resize(g,96,96));
                LIGHT_CHECK(!rf_gpu_graphics_resize(g,64,64));
                LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
                LIGHT_CHECK(!memcmp(a,b,64*64*4));
                /* Reversed triangle winding must remain opaque. */
                for(unsigned j=0;j<2;++j)for(unsigned k=0;k<3;++k) {
                    float tmp=wall[j].p[1][k];wall[j].p[1][k]=wall[j].p[2][k];wall[j].p[2][k]=tmp;
                }
                LIGHT_CHECK(!rf_gpu_graphics_set_architecture(g,wall,2));
                LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
                LIGHT_CHECK(!memcmp(a,b,64*64*4));
            }
            struct rf_gpu_occlusion_primitive solid={{{-10000,-10000,3390},{10000,10000,3410},{0,0,0}},1};
            LIGHT_CHECK(!rf_gpu_graphics_set_architecture(g,&solid,1));
            LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
            unsigned differences=0,max_error=0;
            for(unsigned p=0;p<64*64;++p) {
                if(a[p]!=b[p])++differences;
                unsigned av=lighting_test_rgb(a[p]),bv=lighting_test_rgb(b[p]);
                unsigned error=av>bv?av-bv:bv-av;
                if(error>max_error)max_error=error;
            }
            fprintf(stdout,"ARCHITECTURE BOX mode=%u changed=%u max_error=%u\n",mode,differences,max_error);
            LIGHT_CHECK(max_error<=3);
        }
        /* A blocker behind the receiver must not shadow a finite light ray. */
        lights.sun_color[3]=0;lights.count=1;lights.lights[0].color_intensity[3]=5;
        LIGHT_CHECK(!rf_gpu_graphics_set_lighting(g,&lights));
        for(unsigned i=0;i<2;++i)for(unsigned vtx=0;vtx<3;++vtx)wall[i].p[vtx][2]=5000;
        LIGHT_CHECK(!rf_gpu_graphics_set_architecture(g,wall,2));
        LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,a,da,96*96));
        LIGHT_CHECK(!rf_gpu_graphics_set_architecture(g,NULL,0));
        LIGHT_CHECK(!rf_gpu_graphics_scene_capture(g,items,1,b,db,96*96));
        LIGHT_CHECK(!memcmp(a,b,64*64*4));
        fprintf(stdout,"ARCHITECTURE LIGHT PASS point=1 spot_over_budget=1 sun=1 exact_box=1 finite_segment=1 reset=1 retained_resize=1 double_sided=1\n");
    }
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
    LIGHT_CHECK(!lighting_range_regression(g));
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
