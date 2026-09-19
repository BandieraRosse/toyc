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

static unsigned form_light(const struct rf_gpu_graphics_draw *d)
{
    int n[3]={0};
    if (!d->rotation[3]) return 256;
    for (unsigned i=0;i<3;++i) {
        const int32_t *p=&vertices[0].normals[i*3];
        n[0]+=(int16_t)((p[0]*d->rotation[1]+p[2]*d->rotation[0])/1024);
        n[1]+=p[1];
        n[2]+=(int16_t)((p[2]*d->rotation[1]-p[0]*d->rotation[0])/1024);
    }
    for (unsigned i=0;i<3;++i) n[i]/=3;
    int dot=(n[0]*(-13377)+n[1]*26755+n[2]*(-13377))/32767;
    if(dot<0)dot=0;
    if(dot>32767)dot=32767;
    return 136+(unsigned)dot*120/32767;
}

static uint32_t shaded(const struct rf_gpu_graphics_draw *d, uint32_t rgb)
{
    unsigned form=form_light(d), result=0;
    for(unsigned shift=0;shift<24;shift+=8) {
        unsigned c=(rgb>>shift)&255;
        if(d->material[2]) c=c*(d->material[1]*form/256)/256;
        else c=(c*form/256)*d->material[1]/256;
        result|=c<<shift;
    }
    return rgba(result);
}

struct projected { double x,y,z,u,v,iz; };
static struct projected project(const struct rf_gpu_graphics_draw *d, unsigned id)
{
    const struct rf_gpu_graphics_vertex *v=&vertices[id];
    int p[3],x,z,y;
    struct projected r;
    p[0]=(v->position[0]*d->rotation[1]+v->position[2]*d->rotation[0])/1024;
    p[1]=v->position[1]-d->rotation[2];
    p[2]=(v->position[2]*d->rotation[1]-v->position[0]*d->rotation[0])/1024;
    for(unsigned i=0;i<3;++i)
        p[i]=p[i]*d->translation_scale[3]/1000+d->translation_scale[i]-d->camera[i];
    x=(p[0]*d->view[1]-p[2]*d->view[0])/1024;
    z=(p[0]*d->view[0]+p[2]*d->view[1])/1024;
    y=(p[1]*d->view[3]-z*d->view[2])/1024;
    z=(p[1]*d->view[2]+z*d->view[3])/1024;
    r.x=d->projection[0]/2.0+(double)x*d->projection[3]/z;
    r.y=d->projection[1]/2.0-(double)y*d->projection[3]/z;
    r.z=z; r.iz=1048576/z;
    r.u=v->uv[0]/65536.0; r.v=v->uv[1]/65536.0;
    return r;
}

static double edge(struct projected a,struct projected b,double x,double y)
{ return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); }
static double floor_value(double x)
{ long long i=(long long)x; return (double)(i-(x<(double)i)); }
static double distance(struct projected a,struct projected b)
{ double x=a.x-b.x,y=a.y-b.y;return sqrt(x*x+y*y); }

/* Independent analytic projection oracle. The one-pixel edge band is fixed
 * before testing; depth/color errors in the interior are never hidden by an
 * image-wide threshold. Texture cell boundaries get a 1e-4 UV exclusion. */
static int verify_oracle(const struct rf_gpu_graphics_draw *d, const char *label)
{
    unsigned inside=0,outside=0,edges=0,depth_errors=0,color_errors=0,coverage_errors=0;
    int near_crossing=0;
    for(unsigned t=d->first_index;t<d->first_index+d->index_count;++t)
        if(project(d,indices[t]).z<64)near_crossing=1;
    unsigned width=(unsigned)d->projection[0],height=(unsigned)d->projection[1];
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        double px=x+.5,py=y+.5;
        int covered=0,uncertain=0,uv_boundary=0;
        double expected_depth=0;
        uint32_t expected_color=0xff000000;
        for(unsigned t=d->first_index;t<d->first_index+d->index_count;t+=3) {
            struct projected a=project(d,indices[t]),b=project(d,indices[t+1]),c=project(d,indices[t+2]);
            double area=edge(a,b,c.x,c.y);
            if(fabs(area)<1e-8)continue;
            double e0=edge(b,c,px,py),e1=edge(c,a,px,py),e2=edge(a,b,px,py);
            double l0=distance(b,c),l1=distance(c,a),l2=distance(a,b);
            if(fabs(e0)<=l0 || fabs(e1)<=l1 || fabs(e2)<=l2) uncertain=1;
            double w0=e0/area,w1=e1/area,w2=e2/area;
            if(w0<0 || w1<0 || w2<0)continue;
            double inv=w0/a.z+w1/b.z+w2/c.z;
            double dx=((b.y-c.y)/a.z+(c.y-a.y)/b.z+(a.y-b.y)/c.z)/area;
            double dy=((c.x-b.x)/a.z+(a.x-c.x)/b.z+(b.x-a.x)/c.z)/area;
            if(fabs(inv-1.0/64.0)<=sqrt(dx*dx+dy*dy))uncertain=1;
            if(inv>1.0/64.0)continue;
            covered=1;
            expected_depth=floor_value(w0*a.iz+w1*b.iz+w2*c.iz)/16384.0;
            uint32_t rgb=d->material[0];
            if(d->material[2]) {
                double u=(w0*a.u/a.z+w1*b.u/b.z+w2*c.u/c.z)/inv;
                double v=(w0*a.v/a.z+w1*b.v/b.z+w2*c.v/c.z)/inv;
                u-=floor_value(u);v-=floor_value(v);
                uv_boundary=fabs(u-.5)<1e-4 || fabs(v-.5)<1e-4 || u<1e-4 || v<1e-4;
                rgb=texels[(unsigned)(v*2)*2+(unsigned)(u*2)];
            }
            expected_color=shaded(d,rgb);
        }
        unsigned p=y*width+x;
        if(uncertain){++edges;continue;}
        if(covered) {
            ++inside;
            if(depths[p]==0)++coverage_errors;
            // One inverse-Z unit permits interpolation float rounding only.
            if(!near_crossing && fabs(depths[p]-expected_depth)>1.01/16384.0)++depth_errors;
            if(!uv_boundary && pixels[p]!=expected_color)++color_errors;
        } else {
            ++outside;
            if(depths[p]!=0 || pixels[p]!=0xff000000)++coverage_errors;
        }
    }
    printf("oracle %s inside=%u outside=%u edge-band=%u coverage-errors=%u color-errors=%u depth-errors=%u depth-oracle=%s\n",
        label,inside,outside,edges,coverage_errors,color_errors,depth_errors,
        near_crossing?"HG-2B-pending":"analytic");
    return inside && outside && !coverage_errors && !color_errors && !depth_errors ? 0:-1;
}

static int capture(const char *prefix,unsigned w,unsigned h)
{
    char path[1024];FILE *f;
    if(snprintf(path,sizeof(path),"%s.ppm",prefix)>=(int)sizeof(path))return -1;
    f=fopen(path,"wb");if(!f)return -1;
    fprintf(f,"P6\n%u %u\n255\n",w,h);
    for(unsigned i=0;i<w*h;++i){unsigned char c[3]={(unsigned char)pixels[i],(unsigned char)(pixels[i]>>8),(unsigned char)(pixels[i]>>16)};
        if(fwrite(c,1,3,f)!=3){fclose(f);return -1;}}
    if(fclose(f))return -1;
    if(snprintf(path,sizeof(path),"%s.depth-f32",prefix)>=(int)sizeof(path))return -1;
    f=fopen(path,"wb");if(!f)return -1;
    int ok=fwrite(depths,sizeof(float),w*h,f)==w*h;
    return fclose(f)==0 && ok ? 0:-1;
}

int main(int argc,char **argv)
{
    struct rf_gpu gpu;
    struct rf_gpu_vulkan_context context;
    struct rf_gpu_graphics *g=NULL;
    struct rf_gpu_graphics_draw d, pair[2];
    struct rf_gpu_graphics_stats initial,stats;
    const int positions[7][3]={{-64,-64,0},{64,-64,0},{64,64,0},{-64,64,0},
                             {-16,-16,32},{48,-16,128},{0,48,128}};
    const int normals[9]={-10000,29000,1000,20000,16000,21000,0,32767,0};
    int result=1;
    if(argc>2){fprintf(stderr,"usage: %s [capture-prefix]\n",argv[0]);return 2;}
    memset(&context,0,sizeof(context));context.require_graphics=1;
    if(rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context)<0){
        fprintf(stderr,"graphics GPU init: %s\n",gpu.message);return 2;}
    printf("HG-2A adapter=%s vendor=%x device=%x type=%u queue=%u\n",gpu.info.adapter_name,
        gpu.info.vendor_id,gpu.info.device_id,gpu.info.adapter_type,gpu.info.queue_family);
    for(unsigned i=0;i<7;++i){
        memcpy(vertices[i].position,positions[i],12);memcpy(vertices[i].normals,normals,36);
        vertices[i].uv[0]=(i==1 || i==2)?65535:0;
        vertices[i].uv[1]=(i==2 || i==3)?65535:0;
    }
    CHECK((g=rf_gpu_graphics_create(&context))!=NULL);
    { uint32_t bad[]={0,1,99};
      CHECK(rf_gpu_graphics_upload(g,vertices,7,bad,3,texels,2,2)<0); }
    CHECK(rf_gpu_graphics_upload(g,vertices,7,indices,15,texels,2,2)==0);
    CHECK(rf_gpu_graphics_upload(g,vertices,7,indices,15,texels,2,2)<0);
    CHECK(rf_gpu_graphics_resize(g,128,96)==0);
    rf_gpu_graphics_get_stats(g,&initial);
    CHECK(initial.mesh_upload_bytes==sizeof(vertices)+sizeof(indices));
    CHECK(initial.texture_upload_bytes==sizeof(texels));
    d=draw(128,96);
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(verify_oracle(&d,"flat") == 0);
    CHECK(pixels[48*128+64]==rgba(d.material[0]));
    CHECK(depths[48*128+64]==.25f);
    memcpy(saved,pixels,128*96*4);memcpy(saved_depths,depths,128*96*4);
    for(unsigned i=0;i<4;++i){
        CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
        CHECK(memcmp(saved,pixels,128*96*4)==0 && memcmp(saved_depths,depths,128*96*4)==0);
    }
    // Culling: reverse winding must invert visibility, double-sided restores it.
    d.double_sided=0;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    int front_visible=depths[48*128+64]!=0;
    d.first_index=6;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK((depths[48*128+64]!=0)!=front_visible);
    printf("winding original-front=%d\n",front_visible);
    CHECK(front_visible);
    d.double_sided=1;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(memcmp(saved,pixels,128*96*4)==0);
    // Depth must reject a farther later draw, then accept same-depth later draw.
    pair[0]=pair[1]=draw(128,96);pair[1].material[0]=0x35bde1;
    pair[1].translation_scale[2]=512;
    CHECK(rf_gpu_graphics_render(g,pair,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(pair[0].material[0]) && depths[48*128+64]==.25f);
    pair[1].translation_scale[2]=256;
    CHECK(rf_gpu_graphics_render(g,pair,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(pair[1].material[0]));
    pair[1].translation_scale[2]=128;
    CHECK(rf_gpu_graphics_render(g,pair,2,pixels,depths,MAX_PIXELS)==0);
    CHECK(pixels[48*128+64]==rgba(pair[1].material[0]) && depths[48*128+64]==.5f);
    // Integer yaw/scale/pivot/camera and material overrides, no CPU frontend.
    d=draw(128,96);d.rotation[0]=512;d.rotation[1]=886;d.rotation[2]=-23;
    d.rotation[3]=1;d.translation_scale[3]=1375;d.translation_scale[0]=-11;
    d.camera[0]=7;d.camera[1]=13;d.material[1]=173;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(verify_oracle(&d,"transform-form-Q8")==0);
    d.material[2]=1;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(verify_oracle(&d,"nearest-perspective-Q8")==0);
    d.view[0]=89;d.view[1]=1020;d.view[2]=53;d.view[3]=1022;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(verify_oracle(&d,"camera-yaw-pitch")==0);
    // Preflight is atomic: rejected draws do not record or submit half a frame.
    rf_gpu_graphics_get_stats(g,&stats);uint64_t frames=stats.frames;
    pair[0]=d;pair[1]=d;pair[1].index_count=UINT32_MAX;
    CHECK(rf_gpu_graphics_render(g,pair,2,pixels,depths,MAX_PIXELS)<0);
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,1)<0);
    CHECK(rf_gpu_graphics_resize(g,0,96)<0);
    rf_gpu_graphics_get_stats(g,&stats);CHECK(stats.frames==frames);
    // A fully behind-near primitive must produce no color or depth.
    d=draw(128,96);d.translation_scale[2]=32;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    for(unsigned i=0;i<128*96;++i)CHECK(depths[i]==0 && pixels[i]==0xff000000);
    // Near-crossing hardware clip: verify coverage analytically, independently
    // of quantized inverse-Z compatibility reserved for HG-2B.
    d.translation_scale[2]=0;d.first_index=12;d.index_count=3;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(verify_oracle(&d,"near-crossing-coverage")==0);
    { unsigned covered=0;
      for(unsigned i=0;i<128*96;++i){CHECK(depths[i]>=0 && depths[i]<=1);
          if(depths[i]>0)++covered;}
      CHECK(covered>100 && covered<128*96/2);
      printf("near-crossing covered=%u\n",covered); }
    // Resize destroys targets only. A second extent then restoration gives an
    // exact repeat of the original flat frame with zero mesh/texture reupload.
    CHECK(rf_gpu_graphics_resize(g,320,240)==0);
    d=draw(320,240);d.material[2]=1;d.rotation[3]=1;d.material[1]=173;
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(verify_oracle(&d,"resize-textured")==0);
    if(argc==2)CHECK(capture(argv[1],320,240)==0);
    CHECK(rf_gpu_graphics_resize(g,128,96)==0);
    CHECK(rf_gpu_graphics_resize(g,128,96)==0);
    d=draw(128,96);
    CHECK(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)==0);
    CHECK(memcmp(saved,pixels,128*96*4)==0 && memcmp(saved_depths,depths,128*96*4)==0);
    rf_gpu_graphics_get_stats(g,&stats);
    CHECK(stats.mesh_upload_bytes==initial.mesh_upload_bytes && stats.texture_upload_bytes==initial.texture_upload_bytes);
    CHECK(stats.target_builds==3 && stats.indexed_draws>stats.frames);
    printf("mesh-upload=%llu texture-upload=%llu instance-upload=%llu indexed-draws=%llu frames=%llu targets=%llu\n",
        (unsigned long long)stats.mesh_upload_bytes,(unsigned long long)stats.texture_upload_bytes,
        (unsigned long long)stats.instance_upload_bytes,(unsigned long long)stats.indexed_draws,
        (unsigned long long)stats.frames,(unsigned long long)stats.target_builds);
    result=0;
done:
    rf_gpu_graphics_destroy(g);rf_gpu_shutdown(&gpu);
    printf("HG-2A graphics proof: %s checks=%u\n",result?"FAIL":"PASS",checks);
    return result;
}
