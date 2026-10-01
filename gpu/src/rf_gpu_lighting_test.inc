/* Behaviour tests use real raster depth, HDR and the normal lighting shaders. */
static unsigned lighting_test_rgb(uint32_t c)
{ return (c&255)+((c>>8)&255)+((c>>16)&255); }
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
    fprintf(stdout,"LIGHTING PASS shadow_pixels=%u spot_shadow_pixels=%u moving_light_pixels=%u material_pixels=%u style_pixels=%u baked_independent=1 resize=1\n",
        shadow_pixels,spot_pixels,moving_pixels,material_pixels,style_pixels);
    result=0;
done:
    if(r) rf_gpu_graphics_resource_destroy(g,r);
    free(a);free(b);free(da);free(db);return result;
#undef LIGHT_CHECK
}
