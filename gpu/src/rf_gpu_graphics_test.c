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
    const int invz[6]={1,8192,4096,12288,1024,1};
    const unsigned layers[6]={RF_GPU_SCENE_SKY,RF_GPU_SCENE_WORLD,
        RF_GPU_SCENE_TRANSPARENT,RF_GPU_SCENE_EFFECTS,RF_GPU_SCENE_VIEWMODEL,RF_GPU_SCENE_OVERLAY};
    const unsigned colors[6]={0x0000ff,0xff0000,0x00ff00,0xffff00,0xff00ff,0x00ffff};
    int result=-1;
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
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
        items[i].draw.first_index=i*6;
    }
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[0]==rgba(0x0000ff) && depths[0]==0);
    CHECK(pixels[48*128+64]==rgba(0xff0000) && depths[48*128+64]==0.5f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,3,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(0xff0000) && depths[48*128+64]==0.5f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,4,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(0xff8000) && depths[48*128+64]==0.5f);
    CHECK(rf_gpu_graphics_scene_capture(g,items,5,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(0xff00ff) && depths[48*128+64]==0.0625f);
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
    int result=-1;
    CHECK(g && rf_gpu_graphics_resize(g,128,96)==0);
    for (unsigned i=0;i<6;++i) v[i]=vertices[indices[i]];
    r=rf_gpu_graphics_resource_create(g,v,6,ix,6,texels,2,2);
    CHECK(r!=NULL);
    for (unsigned i=0;i<2;++i) {
        items[i].resource=r;items[i].draw=draw(128,96);
        items[i].draw.translation_scale[2]=4000+i;
        items[i].draw.material[0]=i?0x00ff00:0xff0000;
    }
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(0xff0000));
    memcpy(saved,pixels,128*96*4);
    struct rf_gpu_graphics_batch_item swap=items[0];items[0]=items[1];items[1]=swap;
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(!memcmp(saved,pixels,128*96*4));
    /* The old reciprocal-depth bucket cannot distinguish these surfaces. */
    for (unsigned i=0;i<2;++i) items[i].draw.quality[0]=1;
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    uint32_t first=pixels[48*128+64];
    swap=items[0];items[0]=items[1];items[1]=swap;
    CHECK(rf_gpu_graphics_scene_capture(g,items,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(first!=pixels[48*128+64]);
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
    CHECK(pixels[48*128+64]==rgba(items[0].draw.material[0]));
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
    if (getenv("RF_GPU_COLOR_TEST")) {
        CHECK(scene_color_test(&context)==0);
        result=0;goto done;
    }
    if (getenv("RF_GPU_PREPARATION_TEST")) {
        CHECK(triangle_reuse_test(&context)==0);
        CHECK(scene_color_test(&context)==0);
        CHECK(skin_batch_test(&context)==0);
        result=0;goto done;
    }
    CHECK(scene_layers_test(&context)==0);
    CHECK(triangle_reuse_test(&context)==0);
    CHECK(scene_color_test(&context)==0);
    CHECK(skin_batch_test(&context)==0);
    CHECK(precision_material_test(&context)==0);
    result=0;
done:
    rf_gpu_shutdown(&gpu);
    printf("Scene graphics: %s checks=%u\n",result?"FAIL":"PASS",checks);
    return result;
}
