/* HG-2A: real indexed graphics, numeric/image oracles, persistent resources.
 * This executable never links the Rasterfall per-triangle frontend. */
#include "rf_gpu_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_PIXELS (320*240)
static uint32_t pixels[MAX_PIXELS], saved[MAX_PIXELS];
static float depths[MAX_PIXELS], saved_depths[MAX_PIXELS];
static struct rf_gpu_graphics_vertex vertices[7];
static const uint32_t indices[] = {0,1,2,0,2,3, 2,1,0,3,2,0, 4,5,6};
static const uint32_t texels[] = {0xff3923,0x2bef59,0x415dff,0xf7bd35};
static unsigned checks;

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); goto done; } ++checks; } while (0)

static struct rf_gpu_graphics_draw draw(uint32_t w, uint32_t h)
{
    struct rf_gpu_graphics_draw d;
    memset(&d,0,sizeof(d));
    d.translation_scale[2]=256; d.translation_scale[3]=1000;
    d.rotation[1]=1024; d.view[1]=d.view[3]=1024;
    d.projection[0]=(int)w; d.projection[1]=(int)h;
    d.projection[2]=64; d.projection[3]=(int)w*3/4;
    d.material[0]=0xd76b35; d.material[1]=256;
    d.texture[0]=d.texture[1]=2;
    d.index_count=6; d.double_sided=1;
    return d;
}

static uint32_t rgba(uint32_t rgb)
{
    return 0xff000000U | ((rgb&255)<<16) | (rgb&0xff00) | ((rgb>>16)&255);
}

/* Numeric oracle for unlit linear HDR at the fixture's unit exposure.
 * RGBA16F storage and final UNORM conversion may differ by one output code. */
static unsigned hdr_channel(double linear)
{
    double mapped=linear*(2.51*linear+0.03)/
        (linear*(2.43*linear+0.59)+0.14)*(2.43/2.51);
    double srgb=mapped<=0.0031308 ? 12.92*mapped : 1.055*pow(mapped,1.0/2.4)-0.055;
    return (unsigned)(srgb*255.0+0.5);
}
static uint32_t hdr_linear(double r,double g,double b)
{
    return 0xff000000u|hdr_channel(r)|(hdr_channel(g)<<8)|(hdr_channel(b)<<16);
}
static uint32_t hdr_rgb(uint32_t rgb)
{
    double linear[3];
    for(unsigned i=0;i<3;++i) {
        double c=((rgb>>(16-i*8))&255)/255.0;
        linear[i]=c<=0.04045 ? c/12.92 : pow((c+0.055)/1.055,2.4);
    }
    return hdr_linear(linear[0],linear[1],linear[2]);
}
static int color_near(uint32_t actual,uint32_t expected)
{
    if((actual>>24)!=(expected>>24))return 0;
    for(unsigned c=0;c<3;++c)
        if(abs((int)((actual>>(c*8))&255)-(int)((expected>>(c*8))&255))>1)return 0;
    return 1;
}

static int triangle_reuse_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *resource=NULL,*reference=NULL;
    struct rf_gpu_graphics_vertex v[9];
    struct rf_gpu_graphics_stats before,after;
    struct rf_gpu_graphics_draw d=draw(128,96);
    struct rf_gpu_graphics_batch_item item={0};
    uint32_t ix[9]={0,1,2,3,4,5,6,7,8};
    uint64_t positions,normals,uvs;
    uint32_t position_delta,normal_delta;
    int result=-1;
    for (unsigned i=0;i<9;++i) v[i]=vertices[indices[i%6]];
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
    resource=rf_gpu_graphics_resource_create(g,v,6,ix,6,texels,2,2);
    CHECK(resource!=NULL);
    rf_gpu_graphics_get_stats(g,&before);
    CHECK(rf_gpu_graphics_triangle_resource_update(g,resource,v,9)==1);
    v[0].position[0]=32768;
    CHECK(rf_gpu_graphics_triangle_resource_update(g,resource,v,6)<0);
    v[0]=vertices[indices[0]];
    for (unsigned i=0;i<6;++i) v[i].position[0]+=12;
    CHECK(rf_gpu_graphics_triangle_resource_update(g,resource,v,3)==0);
    CHECK(rf_gpu_graphics_resource_bind(g,resource)==0);
    CHECK(rf_gpu_graphics_validate_draw(g,&d)<0); /* Active range shrank. */
    CHECK(rf_gpu_graphics_triangle_resource_update(g,resource,v,6)==0);
    CHECK(rf_gpu_graphics_resource_diff_vertices(g,resource,v,6,&positions,&normals,&uvs,
        &position_delta,&normal_delta)==0);
    CHECK(!positions && !normals && !uvs);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.texture_upload_bytes==before.texture_upload_bytes);
    CHECK(after.mesh_upload_bytes-before.mesh_upload_bytes==9*sizeof(*v));
    item.resource=resource;item.draw=d;
    CHECK(rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS)==0);
    memcpy(saved,pixels,128*96*4);memcpy(saved_depths,depths,128*96*4);
    reference=rf_gpu_graphics_resource_create(g,v,6,ix,6,texels,2,2);
    CHECK(reference && rf_gpu_graphics_resource_bind(g,reference)==0);
    item.resource=reference;
    CHECK(rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(!memcmp(saved,pixels,128*96*4) && !memcmp(saved_depths,depths,128*96*4));
    result=0;
done:
    rf_gpu_graphics_destroy(g);
    printf("SCENE triangle reuse/grow/reject/pixels: %s\n",result?"FAIL":"PASS");
    return result;
}

static int resource_bounds_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *offset=NULL,*reference=NULL;
    struct rf_gpu_graphics_vertex local[6],rebased[6];
    struct rf_gpu_graphics_batch_item item={0};
    struct rf_gpu_graphics_stats before,after;
    uint32_t ix[6]={0,1,2,3,4,5};
    const int pivot[3]={4096,1024,2048};
    const int rotations[4][2]={{0,1024},{1024,0},{724,724},{-724,724}};
    int result=-1;
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
    for (unsigned i=0;i<6;++i) {
        rebased[i]=local[i]=vertices[indices[i]];
        for (unsigned k=0;k<3;++k) local[i].position[k]+=pivot[k];
    }
    offset=rf_gpu_graphics_resource_create(g,local,6,ix,6,texels,2,2);
    reference=rf_gpu_graphics_resource_create(g,rebased,6,ix,6,texels,2,2);
    CHECK(offset && reference);
    /* Equivalent world geometry with a distant local pivot must retain all
     * visible pixels under yaw, scale, vertical pivot and large map positions. */
    for (unsigned trial=0;trial<4;++trial) {
        struct rf_gpu_graphics_draw d=draw(128,96);
        int scale=trial==3?2:1;
        d.rotation[0]=rotations[trial][0];d.rotation[1]=rotations[trial][1];
        d.translation_scale[3]=scale*1000;d.translation_scale[2]=512;
        d.quality[2]=3;
        if (trial==3) {
            d.camera[0]=d.translation_scale[0]=200000;
            d.camera[2]=200000;d.translation_scale[2]+=200000;
        }
        item.resource=reference;item.draw=d;
        CHECK(rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS)==0);
        memcpy(saved,pixels,128*96*4);memcpy(saved_depths,depths,128*96*4);
        item.resource=offset;item.draw=d;
        item.draw.translation_scale[0]-=(pivot[0]*d.rotation[1]+pivot[2]*d.rotation[0])/1024*scale;
        item.draw.translation_scale[2]-=(pivot[2]*d.rotation[1]-pivot[0]*d.rotation[0])/1024*scale;
        item.draw.rotation[2]=pivot[1];
        CHECK(rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS)==0);
        CHECK(!memcmp(saved,pixels,128*96*4) && !memcmp(saved_depths,depths,128*96*4));
    }
    /* The origin-centred legacy bound overlaps the camera here, although the
     * entire model is outside. Main-view rejection must reduce actual draws. */
    item.draw=draw(128,96);item.draw.quality[2]=3;
    rf_gpu_graphics_get_stats(g,&before);
    CHECK(rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS)==0);
    rf_gpu_graphics_get_stats(g,&after);
    if (!getenv("RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS") && !getenv("RF_GPU_SCENE_DISABLE_DRAW_CULL"))
        CHECK(after.indexed_draws==before.indexed_draws);
    /* A dynamic upload must replace, rather than accumulate, these bounds. */
    CHECK(rf_gpu_graphics_triangle_resource_update(g,offset,rebased,6)==0);
    CHECK(rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS)==0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.indexed_draws>before.indexed_draws);
    result=0;
done:
    rf_gpu_graphics_destroy(g);
    printf("SCENE offset bounds/transform/update: %s\n",result?"FAIL":"PASS");
    return result;
}

static int scene_color_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *colored=NULL,*plain=NULL;
    struct rf_gpu_scene_color_vertex v[6];
    struct rf_gpu_graphics_vertex ref[6];
    struct rf_gpu_graphics_batch_item split[2]={0},merged={0};
    uint32_t ix[6]={0,1,2,3,4,5},white=0xffffff;
    const uint32_t colors[2]={0xff1234,0x1256ff};
    int result=-1;
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
    for (unsigned i=0;i<6;++i) {
        ref[i]=vertices[indices[i]];
        memcpy(v[i].position,ref[i].position,sizeof(v[i].position));
        v[i].light_q8=ref[i].uv[0]=160+(i%3)*80;
        v[i].rgb24=colors[i/3];ref[i].uv[1]=0;
    }
    colored=rf_gpu_graphics_scene_color_resource_create(g,v,6,ix,6);
    plain=rf_gpu_graphics_resource_create(g,ref,6,ix,6,&white,1,1);
    CHECK(colored && plain);
    for (unsigned trial=0;trial<6;++trial) {
        if (trial>=4) {
            const int xy[6][2]={{16,16},{100,16},{100,80},{16,16},{100,80},{16,80}};
            for (unsigned i=0;i<6;++i) {
                v[i].position[0]=ref[i].position[0]=xy[i][0];
                v[i].position[1]=ref[i].position[1]=xy[i][1];
                v[i].position[2]=ref[i].position[2]=1024;
            }
            CHECK(rf_gpu_graphics_triangle_resource_update(g,plain,ref,6)==0);
        }
        for (unsigned i=0;i<6;++i) {
            v[i].rgb24=colors[(i/3+trial)%2];
            if (trial==2 || trial==3) v[i].light_q8=i<3?128:64;
        }
        CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)==0);
        for (unsigned i=0;i<2;++i) {
            split[i].resource=plain;split[i].draw=draw(128,96);
            split[i].draw.texture[0]=split[i].draw.texture[1]=1;
            split[i].draw.material[0]=colors[(i+trial)%2];split[i].draw.material[3]=1;
            split[i].draw.first_index=i*3;split[i].draw.index_count=3;
            if (trial==1) { /* Exercise hardware near clipping. */
                split[i].draw.rotation[0]=724;split[i].draw.rotation[1]=724;
                split[i].draw.translation_scale[2]=64;
            }
            if (trial==2) {
                split[i].draw.texture[2]=i?64:128;split[i].draw.scene_layer=RF_GPU_SCENE_TRANSPARENT;
            }
            if (trial==3) {
                split[i].draw.material[3]=0;split[i].draw.quality[2]=3;
                split[i].draw.texture[2]=i?64:128;split[i].draw.scene_layer=RF_GPU_SCENE_TRANSPARENT;
            }
            if (trial>=4) {
                split[i].draw.texture[3]=trial==4?1:2;
                split[i].draw.scene_layer=trial==4?RF_GPU_SCENE_VIEWMODEL:RF_GPU_SCENE_OVERLAY;
            }
        }
        CHECK(rf_gpu_graphics_scene_capture(g,split,2,pixels,depths,MAX_PIXELS)==0);
        unsigned covered=0;
        for (unsigned i=0;i<128*96;++i) if (pixels[i]!=0xff000000) covered++;
        CHECK(covered>0);
        memcpy(saved,pixels,128*96*4);memcpy(saved_depths,depths,128*96*4);
        merged=split[0];merged.resource=colored;merged.draw.material[3]=2;merged.draw.index_count=6;
        if (trial==2 || trial==3) merged.draw.texture[2]=256;
        CHECK(rf_gpu_graphics_scene_capture(g,&merged,1,pixels,depths,MAX_PIXELS)==0);
        CHECK(!memcmp(saved,pixels,128*96*4) && !memcmp(saved_depths,depths,128*96*4));
    }
    CHECK(rf_gpu_graphics_resource_bind(g,colored)==0);
    merged.draw.texture[2]=256;
    CHECK(rf_gpu_graphics_validate_draw(g,&merged.draw)==0);
    v[1].light_q8++;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)==0);
    CHECK(rf_gpu_graphics_validate_draw(g,&merged.draw)<0);
    v[1].light_q8=v[0].light_q8;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)==0);
    CHECK(rf_gpu_graphics_validate_draw(g,&merged.draw)==0);
    merged.draw.texture[2]=0;
    merged.draw.integer_depth=1;
    CHECK(rf_gpu_graphics_validate_draw(g,&merged.draw)<0);
    merged.draw.integer_depth=0;merged.draw.material[3]=1;
    CHECK(rf_gpu_graphics_validate_draw(g,&merged.draw)<0);
    CHECK(rf_gpu_graphics_triangle_resource_update(g,colored,ref,6)<0);
    v[1].rgb24^=1;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)<0);
    v[1].rgb24=v[0].rgb24;v[0].light_q8=384;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)==0);
    v[0].light_q8=385;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)<0);
    v[0].light_q8=384;v[0].rgb24=0x1000000;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)<0);
    v[0].rgb24=v[1].rgb24;v[0].position[0]=32768;
    CHECK(rf_gpu_graphics_scene_color_resource_update(g,colored,v,6)<0);
    result=0;
done:
    rf_gpu_graphics_destroy(g);
    printf("SCENE color merge/update/clip/alpha/reject: %s\n",result?"FAIL":"PASS");
    return result;
}

static int skin_batch_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics *other=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *r[2]={0};
    struct rf_gpu_graphics_vertex v[3]={0},expected[3];
    uint32_t bind[66]={0},palette[15]={0},ix[3]={0,1,2},white=0xffffff;
    struct rf_gpu_graphics_stats before,after;
    uint64_t pm,nm,um;uint32_t pd,nd;
    float one=1.0f,shift=12.0f;
    int result=-1;
    memcpy(&palette[0],&one,4);memcpy(&palette[4],&one,4);memcpy(&palette[8],&one,4);
    for (unsigned i=0;i<3;++i) {
        v[i].position[0]=(int)i*8;v[i].position[2]=128;
        for (unsigned n=0;n<3;++n) v[i].normals[n*3+1]=32767;
        memcpy(bind+i*22,&v[i],sizeof(v[i]));
        for (unsigned n=0;n<4;++n) bind[i*22+15+n*2]=65535;
    }
    CHECK(g!=NULL && other!=NULL);
    for (unsigned i=0;i<2;++i) {
        r[i]=rf_gpu_graphics_skinned_resource_create(g,NULL,3,ix,3,bind,66,palette,15,&white,1,1);
        CHECK(r[i]!=NULL);
    }
    memcpy(&palette[9],&shift,4);
    memcpy(expected,v,sizeof(v));
    for (unsigned i=0;i<3;++i) expected[i].position[0]+=12;
    rf_gpu_graphics_get_stats(g,&before);
    CHECK(rf_gpu_graphics_skin_batch_begin(g)==0);
    CHECK(rf_gpu_graphics_skin_batch_begin(g)<0);
    for (unsigned i=0;i<2;++i)
        CHECK(rf_gpu_graphics_skinned_resource_update(g,r[i],3,bind,66,palette,15)==0);
    CHECK(rf_gpu_graphics_skinned_resource_update(g,r[0],3,bind,66,palette,15)<0);
    CHECK(rf_gpu_graphics_resource_destroy(g,r[0])<0);
    CHECK(rf_gpu_graphics_resource_bind(other,r[0])<0);
    CHECK(rf_gpu_graphics_resource_diff_vertices(g,r[0],expected,3,&pm,&nm,&um,&pd,&nd)<0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.queue_submits==before.queue_submits);
    CHECK(rf_gpu_graphics_skin_batch_end(g)==0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.queue_submits==before.queue_submits+1 && after.fence_waits==before.fence_waits+1);
    for (unsigned i=0;i<2;++i) {
        CHECK(rf_gpu_graphics_resource_diff_vertices(g,r[i],expected,3,&pm,&nm,&um,&pd,&nd)==0);
        CHECK(!pm && !nm && !um);
    }
    rf_gpu_graphics_get_stats(g,&before);
    CHECK(rf_gpu_graphics_skin_batch_begin(g)==0);
    CHECK(rf_gpu_graphics_skinned_resource_update(g,r[0],3,NULL,0,palette,15)==0);
    CHECK(rf_gpu_graphics_skin_batch_end(g)==0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.mesh_upload_bytes==before.mesh_upload_bytes);
    CHECK(after.queue_submits==before.queue_submits && after.fence_waits==before.fence_waits);
    CHECK(after.skin_reused==before.skin_reused+1);
    CHECK(rf_gpu_graphics_resource_diff_vertices(g,r[0],expected,3,&pm,&nm,&um,&pd,&nd)==0);
    CHECK(!pm && !nm && !um);
    /* A cancelled changed pose must never become a cache hit. */
    shift=20.0f;memcpy(&palette[9],&shift,4);
    for(unsigned i=0;i<3;++i) expected[i].position[0]+=8;
    CHECK(rf_gpu_graphics_skin_batch_begin(g)==0);
    CHECK(rf_gpu_graphics_skinned_resource_update(g,r[0],3,NULL,0,palette,15)==0);
    rf_gpu_graphics_skin_batch_cancel(g);
    CHECK(rf_gpu_graphics_skin_batch_end(g)<0);
    rf_gpu_graphics_get_stats(g,&before);
    CHECK(rf_gpu_graphics_skin_batch_begin(g)==0);
    CHECK(rf_gpu_graphics_skinned_resource_update(g,r[0],3,NULL,0,palette,15)==0);
    CHECK(rf_gpu_graphics_skin_batch_end(g)==0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.queue_submits==before.queue_submits+1);
    CHECK(after.mesh_upload_bytes-before.mesh_upload_bytes==sizeof(palette));
    CHECK(rf_gpu_graphics_resource_diff_vertices(g,r[0],expected,3,&pm,&nm,&um,&pd,&nd)==0);
    CHECK(!pm && !nm && !um);
    result=0;
done:
    rf_gpu_graphics_destroy(other);
    rf_gpu_graphics_destroy(g);
    printf("SCENE skin batch/duplicate/cancel/device vertices: %s\n",result?"FAIL":"PASS");
    return result;
}

static int scene_layers_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *resource=NULL;
    struct rf_gpu_graphics_vertex v[24]={0};
    uint32_t ix[36],white=0xffffff;
    struct rf_gpu_graphics_batch_item items[6]={0};
    struct rf_gpu_lighting lighting;
    uint32_t sky_corner;
    const int invz[6]={1,8192,4096,12288,1024,1};
    const unsigned layers[6]={RF_GPU_SCENE_SKY,RF_GPU_SCENE_WORLD,
        RF_GPU_SCENE_TRANSPARENT,RF_GPU_SCENE_EFFECTS,RF_GPU_SCENE_VIEWMODEL,RF_GPU_SCENE_OVERLAY};
    const unsigned colors[6]={0x0000ff,0xff0000,0x00ff00,0xffff00,0xff00ff,0x00ffff};
    int result=-1;
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
    rf_gpu_lighting_default(&lighting);lighting.environment[3]=1;
    CHECK(!rf_gpu_graphics_set_lighting(g,&lighting));
    for (unsigned i=0;i<6;++i) {
        int x0=i==0?0:32,x1=i==0?128:96,y0=i==0?0:16,y1=i==0?96:80;
        for (unsigned k=0;k<4;++k) {
            v[i*4+k].position[0]=(k==1 || k==2)?x1:x0;
            v[i*4+k].position[1]=k>=2?y1:y0;v[i*4+k].position[2]=invz[i];
        }
        const unsigned face[6]={0,1,2,0,2,3};
        for (unsigned k=0;k<6;++k) ix[i*6+k]=i*4+face[k];
    }
    resource=rf_gpu_graphics_resource_create(g,v,24,ix,36,&white,1,1);
    CHECK(resource!=NULL);
    for (unsigned i=0;i<6;++i) {
        items[i].resource=resource;items[i].draw=draw(128,96);
        items[i].draw.texture[0]=items[i].draw.texture[1]=1;
        items[i].draw.texture[3]=(i==0 || i==5)?2:1;
        items[i].draw.texture[2]=(i==2 || i==3)?128:(i==0 || i==5)?255:0;
        items[i].draw.material[0]=colors[i];items[i].draw.scene_layer=layers[i];
        items[i].draw.quality[2]=3; /* Isolate layer composition from surface lighting. */
        items[i].draw.first_index=i*6;
    }
    /* SKY is now procedural HDR. Compare its unobstructed pixel to an isolated
     * sky pass, rather than expecting the old flat draw material color. */
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    sky_corner=pixels[0];CHECK((sky_corner&0xffffff)!=0 && depths[0]==0);
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[0]==sky_corner && depths[0]==0);
    CHECK(color_near(pixels[48*128+64],hdr_rgb(0xff0000)) && depths[48*128+64]==0.5f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,3,pixels,depths,MAX_PIXELS)==0);
    CHECK(color_near(pixels[48*128+64],hdr_rgb(0xff0000)) && depths[48*128+64]==0.5f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,4,pixels,depths,MAX_PIXELS)==0);
    CHECK(color_near(pixels[48*128+64],hdr_linear(1,128.0/255.0,0)) && depths[48*128+64]==0.5f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,5,pixels,depths,MAX_PIXELS)==0);
    CHECK(color_near(pixels[48*128+64],hdr_rgb(0xff00ff)) && depths[48*128+64]==0.0625f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,6,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(0x00ffff) && depths[48*128+64]==0.0625f);
    struct rf_gpu_graphics_stats before,after;
    rf_gpu_graphics_get_stats(g,&before);
    items[5].draw.scene_layer=RF_GPU_SCENE_WORLD;
    CHECK(rf_gpu_graphics_scene_capture(g,items,6,pixels,depths,MAX_PIXELS)<0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(before.frames==after.frames);
    printf("SCENE layered depth/blend/preflight PASS\n");result=0;
done:
    rf_gpu_graphics_destroy(g);return result;
}

static int precision_material_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *r=NULL;
    struct rf_gpu_graphics_vertex v[6];
    uint32_t ix[6]={0,1,2,3,4,5};
    struct rf_gpu_graphics_batch_item items[2]={0};
    struct rf_gpu_lighting lighting;
    struct rf_gpu_graphics_stats before,after;
    int result=-1;
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
    rf_gpu_lighting_default(&lighting);lighting.environment[3]=1;
    CHECK(!rf_gpu_graphics_set_lighting(g,&lighting));
    for (unsigned i=0;i<6;++i) v[i]=vertices[indices[i]];
    r=rf_gpu_graphics_resource_create(g,v,6,ix,6,texels,2,2);
    CHECK(r!=NULL);
    for (unsigned i=0;i<2;++i) {
        items[i].resource=r;items[i].draw=draw(128,96);
        items[i].draw.translation_scale[2]=4000+i;
        items[i].draw.material[0]=i?0x00ff00:0xff0000;
        items[i].draw.quality[2]=3;
    }
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(color_near(pixels[48*128+64],hdr_rgb(0xff0000)));
    CHECK(fabs(depths[48*128+64]-64.0/4000.0)<0.0000001);
    memcpy(saved,pixels,128*96*4);
    struct rf_gpu_graphics_batch_item swap=items[0];items[0]=items[1];items[1]=swap;
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(!memcmp(saved,pixels,128*96*4));
    /* Quantized reciprocal-depth compatibility is retired. Rejection must
     * leave the last complete image and submission counters untouched. */
    for (unsigned i=0;i<2;++i) items[i].draw.quality[0]=1;
    rf_gpu_graphics_get_stats(g,&before);
    memcpy(saved_depths,depths,128*96*4);
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)<0);
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(before.frames==after.frames && before.queue_submits==after.queue_submits);
    CHECK(!memcmp(saved,pixels,128*96*4) && !memcmp(saved_depths,depths,128*96*4));
    items[0].draw=draw(128,96);items[0].draw.material[2]=1;
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    memcpy(saved,pixels,128*96*4);
    items[0].draw.quality[3]=1;
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(memcmp(saved,pixels,128*96*4));
    items[0].draw.material[2]=0;items[0].draw.quality[3]=0;
    items[0].draw.rotation[3]=1;
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    memcpy(saved,pixels,128*96*4);
    items[0].draw.quality[2]=1;
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(memcmp(saved,pixels,128*96*4));
    items[0].draw.quality[2]=3;
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(color_near(pixels[48*128+64],hdr_rgb(items[0].draw.material[0])));
    memcpy(saved,pixels,128*96*4);memcpy(saved_depths,depths,128*96*4);
    for (unsigned i=0;i<6;++i)
        for (unsigned k=0;k<3;++k) v[i].position[k]*=128;
    r=rf_gpu_graphics_resource_create(g,v,6,ix,6,texels,2,2);
    CHECK(r!=NULL);
    items[0].resource=r;items[0].draw.quality[1]=65536;
    CHECK(rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(!memcmp(saved,pixels,128*96*4) && !memcmp(saved_depths,depths,128*96*4));
    result=0;
done:
    rf_gpu_graphics_destroy(g);
    printf("SCENE float depth/order, bilinear, smooth/unlit: %s\n",result?"FAIL":"PASS");
    return result;
}

static int texture_set_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_resource *a=NULL,*b=NULL;
    struct rf_gpu_graphics_vertex v[6];uint32_t ix[6]={0,1,2,3,4,5};
    uint32_t white=0xffffff,edges[4]={0xff0000,0x00ff00,0x0000ff,0xffffff};
    uint32_t *checker=malloc(512*512*4);
    struct rf_gpu_graphics_texture_image images[3]={{checker,512,512},{&white,1,1},{edges,2,2}};
    struct rf_gpu_graphics_batch_item item={0};struct rf_gpu_graphics_stats before,after;
    int result=-1;
    CHECK(g && checker && !rf_gpu_graphics_resize(g,128,96));
    for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x)checker[y*512+x]=((x+y)&1)?white:0;
    for(unsigned i=0;i<6;++i)v[i]=vertices[indices[i]];
    a=rf_gpu_graphics_resource_create(g,v,6,ix,6,&white,1,1);
    b=rf_gpu_graphics_resource_create(g,v,6,ix,6,&white,1,1);CHECK(a && b);
    CHECK(!rf_gpu_graphics_resource_texture_set(g,a,images,3));
    CHECK(!rf_gpu_graphics_resource_share_textures(g,b,a));
    CHECK(!rf_gpu_graphics_resource_destroy(g,a));a=NULL;
    item.resource=b;item.draw=draw(128,96);item.draw.quality[2]=3;item.draw.quality[3]=2;
    item.draw.texture[0]=item.draw.texture[1]=1;item.draw.material[2]=2;
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));
    memcpy(saved,pixels,128*96*4);
    item.draw.material[2]=0;
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));
    CHECK(!memcmp(saved,pixels,128*96*4)); /* White texture preserves linear color factor. */
    item.draw.material[0]=white;item.draw.material[2]=1;item.draw.texture[0]=item.draw.texture[1]=512;
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));
    memcpy(saved,pixels,128*96*4);
    item.draw.material[0]=0xbcbcbc;item.draw.material[2]=0;item.draw.texture[0]=item.draw.texture[1]=1;
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));
    for(unsigned i=0;i<128*96;++i) if(depths[i]>0)
        for(unsigned c=0;c<3;++c)CHECK(abs((int)((saved[i]>>(8*c))&255)-(int)((pixels[i]>>(8*c))&255))<=1);
    /* Constant edge UV magnifies the corner; clamp cannot blend the opposite edge. */
    for(unsigned i=0;i<6;++i)v[i].uv[0]=v[i].uv[1]=0;
    CHECK(!rf_gpu_graphics_triangle_resource_update(g,b,v,6));
    item.draw.material[0]=white;item.draw.material[2]=3;item.draw.texture[0]=item.draw.texture[1]=2;
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));memcpy(saved,pixels,128*96*4);
    item.draw.material[0]=edges[0];item.draw.material[2]=0;item.draw.texture[0]=item.draw.texture[1]=1;
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));CHECK(!memcmp(saved,pixels,128*96*4));
    item.draw.material[0]=white;item.draw.material[2]=3;item.draw.texture[0]=item.draw.texture[1]=2;
    images[0].width=1025;rf_gpu_graphics_get_stats(g,&before);
    CHECK(rf_gpu_graphics_resource_texture_set(g,b,images,3)<0);
    CHECK(!rf_gpu_graphics_scene_capture(g,&item,1,pixels,depths,MAX_PIXELS));CHECK(!memcmp(saved,pixels,128*96*4));
    rf_gpu_graphics_get_stats(g,&after);CHECK(before.texture_upload_bytes==after.texture_upload_bytes);
    item.draw.material[2]=4;CHECK(!rf_gpu_graphics_resource_bind(g,b));
    CHECK(rf_gpu_graphics_validate_draw(g,&item.draw)<0);
    result=0;
done:
    free(checker);rf_gpu_graphics_destroy(g);
    printf("SCENE texture set: %s (linear factor, clamp, mip, sharing, failure retention)\n",result?"FAIL":"PASS");
    return result;
}

static int auxiliary_video_test(struct rf_gpu_vulkan_context *context)
{
    struct rf_gpu_graphics *g=rf_gpu_graphics_create(context),*video=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics *unit=rf_gpu_graphics_create(context);
    struct rf_gpu_graphics_batch_item source={0},unit_source={0},items[2]={{0}};
    struct rf_gpu_graphics_vertex hud[4]={0};
    const uint32_t quad[6]={0,1,2,0,2,3};
    struct rf_gpu_graphics_stats before,after;
    struct rf_gpu_lighting lighting;
    uint32_t white=0xffffff;
    int result=-1;
    CHECK(g && video && unit && !rf_gpu_graphics_resize(g,128,96) &&
        !rf_gpu_graphics_resize(video,64,64) && !rf_gpu_graphics_resize(unit,64,64));
    rf_gpu_lighting_default(&lighting);lighting.environment[3]=1;
    CHECK(!rf_gpu_graphics_set_lighting(g,&lighting) && !rf_gpu_graphics_set_lighting(video,&lighting) &&
        !rf_gpu_graphics_set_lighting(unit,&lighting));
    CHECK(!rf_gpu_graphics_scene_background(video,.018f,.030f,.045f));
    CHECK(rf_gpu_graphics_scene_background(video,-1,0,0)<0);
    source.resource=rf_gpu_graphics_resource_create(video,vertices,4,quad,6,&white,1,1);
    unit_source.resource=rf_gpu_graphics_resource_create(unit,vertices,4,quad,6,&white,1,1);
    items[0].resource=rf_gpu_graphics_resource_create(g,vertices,4,quad,6,&white,1,1);
    CHECK(source.resource && unit_source.resource && items[0].resource);
    source.draw=draw(64,64);source.draw.quality[2]=3;source.draw.material[0]=0xff0000;
    source.draw.texture[0]=source.draw.texture[1]=1;
    unit_source.draw=source.draw;unit_source.draw.material[0]=0xffff00;
    items[0].draw=draw(128,96);items[0].draw.quality[2]=3;items[0].draw.material[0]=0x0000ff;
    items[0].draw.texture[0]=items[0].draw.texture[1]=1;
    for(unsigned i=0;i<4;++i) {
        hud[i].position[0]=(i==1 || i==2)?68:60;
        hud[i].position[1]=i>=2?52:44;hud[i].position[2]=1;
    }
    items[1].resource=rf_gpu_graphics_resource_create(g,hud,4,quad,6,&white,1,1);
    CHECK(items[1].resource);
    items[1].draw=items[0].draw;items[1].draw.material[0]=0x00ffff;
    items[1].draw.texture[3]=2;items[1].draw.texture[2]=255;
    items[1].draw.scene_layer=RF_GPU_SCENE_OVERLAY;
    rf_gpu_graphics_get_stats(video,&before);
    CHECK(!rf_gpu_graphics_scene_offscreen(video,&source,1,1));
    CHECK(!rf_gpu_graphics_scene_offscreen(unit,&unit_source,1,1));
    CHECK(!rf_gpu_graphics_scene_video(g,video,32,16,64,64));
    CHECK(!rf_gpu_graphics_scene_video_at(g,1,unit,0,0,32,32));
    CHECK(rf_gpu_graphics_scene_video_at(g,RF_GPU_GRAPHICS_VIDEO_SLOTS,unit,0,0,32,32)<0);
    CHECK(!rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS));
    { uint32_t background=pixels[17*128+33];
      CHECK((background&255)>0 && ((background>>16)&255)>(background&255)); }
    CHECK(color_near(pixels[40*128+64],hdr_rgb(0xff0000)));
    CHECK(color_near(pixels[16*128+16],hdr_rgb(0xffff00))); /* Both cameras are visible. */
    CHECK(pixels[48*128+64]==rgba(0x00ffff)); /* HUD is above the video. */
    source.draw.material[0]=0x00ff00;
    CHECK(!rf_gpu_graphics_scene_offscreen(video,&source,1,2));
    CHECK(!rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS));
    CHECK(color_near(pixels[40*128+64],hdr_rgb(0x00ff00))); /* Real target update. */
    CHECK(color_near(pixels[16*128+16],hdr_rgb(0xffff00))); /* Independent cadence. */
    rf_gpu_graphics_get_stats(video,&after);
    CHECK(after.target_builds==before.target_builds && after.mesh_upload_bytes==before.mesh_upload_bytes);
    CHECK(after.submits_by_kind[RF_GPU_SUBMIT_READBACK]==before.submits_by_kind[RF_GPU_SUBMIT_READBACK]);
    CHECK(!after.bridge_roundtrips && !after.raster_bridge_transfers);
    CHECK(!rf_gpu_graphics_scene_video(g,NULL,0,0,0,0));
    CHECK(!rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS));
    CHECK(color_near(pixels[40*128+64],hdr_rgb(0x0000ff)));
    CHECK(color_near(pixels[16*128+16],hdr_rgb(0xffff00))); /* Hiding story preserves unit. */
    CHECK(!rf_gpu_graphics_scene_video(g,video,32,16,64,64));
    CHECK(!rf_gpu_graphics_scene_video_at(g,1,NULL,0,0,0,0));
    CHECK(!rf_gpu_graphics_scene_capture(g,items,1,pixels,depths,MAX_PIXELS));
    CHECK(color_near(pixels[40*128+64],hdr_rgb(0x00ff00))); /* Hiding unit preserves story. */
    CHECK(!color_near(pixels[16*128+16],hdr_rgb(0xffff00)));
    CHECK(rf_gpu_graphics_scene_video(g,video,100,16,64,64)<0);
    result=0;
done:
    if(g)for(unsigned i=0;i<RF_GPU_GRAPHICS_VIDEO_SLOTS;++i)
        rf_gpu_graphics_scene_video_at(g,i,NULL,0,0,0,0);
    rf_gpu_graphics_destroy(g);rf_gpu_graphics_destroy(video);rf_gpu_graphics_destroy(unit);
    printf("SCENE auxiliary video: %s (two independent views, GPU copy, update, HUD order, hide, reuse, no readback)\n",result?"FAIL":"PASS");
    return result;
}

int main(void)
{
    struct rf_gpu gpu;
    struct rf_gpu_vulkan_context context={0};
    const int positions[7][3]={{-64,-64,0},{64,-64,0},{64,64,0},{-64,64,0},
                             {-16,-16,32},{48,-16,128},{0,48,128}};
    const int normals[9]={-10000,29000,1000,20000,16000,21000,0,32767,0};
    int result=1;
    context.require_graphics=1;
    if (rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context)<0) {
        fprintf(stderr,"Scene GPU init: %s\n",gpu.message);return 2;
    }
    for(unsigned i=0;i<7;++i) {
        memcpy(vertices[i].position,positions[i],12);
        memcpy(vertices[i].normals,normals,36);
        vertices[i].uv[0]=(i==1 || i==2)?65535:0;
        vertices[i].uv[1]=(i==2 || i==3)?65535:0;
    }
    if(getenv("RF_GPU_TEXTURE_TEST")) {
        CHECK(texture_set_test(&context)==0);result=0;goto done;
    }
    if(getenv("RF_GPU_AUX_TEST")) {
        CHECK(auxiliary_video_test(&context)==0);result=0;goto done;
    }
    if (getenv("RF_GPU_COLOR_TEST")) {
        CHECK(scene_color_test(&context)==0);
        result=0;goto done;
    }
    if (getenv("RF_GPU_PREPARATION_TEST")) {
        CHECK(triangle_reuse_test(&context)==0);
        CHECK(resource_bounds_test(&context)==0);
        CHECK(scene_color_test(&context)==0);
        CHECK(skin_batch_test(&context)==0);
        result=0;goto done;
    }
    CHECK(scene_layers_test(&context)==0);
    CHECK(triangle_reuse_test(&context)==0);
    CHECK(resource_bounds_test(&context)==0);
    CHECK(scene_color_test(&context)==0);
    CHECK(skin_batch_test(&context)==0);
    CHECK(precision_material_test(&context)==0);
    CHECK(texture_set_test(&context)==0);
    CHECK(auxiliary_video_test(&context)==0);
    result=0;
done:
    rf_gpu_shutdown(&gpu);
    printf("Scene graphics: %s checks=%u\n",result?"FAIL":"PASS",checks);
    return result;
}
