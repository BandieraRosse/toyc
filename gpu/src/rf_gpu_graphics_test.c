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

/* HG-2B prerequisite: independent integer CPU clip/project/depth oracle.
 * This fixture deliberately uses identity transforms; z_offset places one
 * corner before near, on the camera plane, or behind the camera. Never
 * project the unclipped corner (which may have z <= 0).
 * Edge exclusions are fixed at one pixel, not fitted to the observed error. */
static int depth_gate_case(struct rf_gpu_graphics *g, int z_offset,
                           int flat, const char *label)
{
    struct rf_gpu_graphics_draw d=draw(128,96);
    int input[3][3], clipped[4][3], count=0;
    struct projected p[4];
    unsigned samples=0, missing=0, errors=0, max_error=0, occlusion=0;
    d.translation_scale[2]=z_offset;
    d.integer_depth=1;
    d.first_index=flat?0:12; d.index_count=3;
    for(unsigned i=0;i<3;++i) {
        memcpy(input[i],vertices[indices[d.first_index+i]].position,sizeof(input[i]));
        input[i][2]+=z_offset;
    }
    for(int i=0;i<3;++i) {
        const int *a=input[(i+2)%3], *b=input[i];
        if((a[2]>=64)!=(b[2]>=64)) {
            for(int k=0;k<2;++k)
                clipped[count][k]=a[k]+(int)((long long)(b[k]-a[k])*(64-a[2])/(b[2]-a[2]));
            clipped[count++][2]=64;
        }
        if(b[2]>=64)memcpy(clipped[count++],b,sizeof(input[i]));
    }
    for(int i=0;i<count;++i) {
        memset(&p[i],0,sizeof(p[i]));
        p[i].x=64+(long long)clipped[i][0]*96/clipped[i][2];
        p[i].y=48-(long long)clipped[i][1]*96/clipped[i][2];
        p[i].iz=1048576/clipped[i][2];
    }
    if(rf_gpu_graphics_render(g,&d,1,pixels,depths,MAX_PIXELS)<0)return -1;
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x) {
        for(int t=1;t+1<count;++t) {
            struct projected a=p[0],b=p[t],c=p[t+1];
            double area=edge(a,b,c.x,c.y);
            if(area==0)continue;
            double e0=edge(b,c,x,y),e1=edge(c,a,x,y),e2=edge(a,b,x,y);
            if(e0/area<=0 || e1/area<=0 || e2/area<=0)continue;
            if(fabs(e0)<=distance(b,c) || fabs(e1)<=distance(c,a) || fabs(e2)<=distance(a,b))continue;
            int expected=(int)((e0*a.iz+e1*b.iz+e2*c.iz)/area);
            float raw=depths[y*128+x]*16384.0f;
            if(!(raw>0 && raw<=16384)){++missing;continue;}
            int actual=(int)raw;
            unsigned delta=(unsigned)abs(actual-expected);
            ++samples;
            if(delta>max_error)max_error=delta;
            if(delta>1)++errors;
            /* A later compute fragment at reference depth must pass >=.
             * Also check one inverse-Z unit farther: it must be rejected.
             * These are buffer-domain compare probes, not a GPU bridge. */
            if(expected<actual || expected-1>=actual)++occlusion;
            break;
        }
    }
    printf("depth-gate %s samples=%u missing=%u depth-errors=%u max-inverse-z-error=%u compare-probe-errors=%u\n",
        label,samples,missing,errors,max_error,occlusion);
    return samples && !missing && !errors && !occlusion ? 0:-1;
}

struct cpu_corner { int x,y,z,u,v; };
struct cpu_screen { int x,y,iz; long long uoz,voz; };
static int oracle_depth[MAX_PIXELS];
static uint32_t oracle_color[MAX_PIXELS];
static unsigned char oracle_edge[MAX_PIXELS];
static int32_t bridge_depth[MAX_PIXELS];

static struct cpu_corner cpu_transform(const struct rf_gpu_graphics_draw *d,unsigned id)
{
    const struct rf_gpu_graphics_vertex *v=&vertices[id];
    int x=(v->position[0]*d->rotation[1]+v->position[2]*d->rotation[0])/1024;
    int y=v->position[1]-d->rotation[2];
    int z=(v->position[2]*d->rotation[1]-v->position[0]*d->rotation[0])/1024;
    x=x*d->translation_scale[3]/1000+d->translation_scale[0]-d->camera[0];
    y=y*d->translation_scale[3]/1000+d->translation_scale[1]-d->camera[1];
    z=z*d->translation_scale[3]/1000+d->translation_scale[2]-d->camera[2];
    int vx=(x*d->view[1]-z*d->view[0])/1024;
    int vz=(x*d->view[0]+z*d->view[1])/1024;
    struct cpu_corner c={vx,(y*d->view[3]-vz*d->view[2])/1024,
        (y*d->view[2]+vz*d->view[3])/1024,v->uv[0],v->uv[1]};
    return c;
}

static struct cpu_corner cpu_intersect(struct cpu_corner a,struct cpu_corner b)
{
    long long n=64-a.z,den=b.z-a.z;
    struct cpu_corner c={a.x+(int)((b.x-a.x)*n/den),a.y+(int)((b.y-a.y)*n/den),64,
        a.u+(int)((b.u-a.u)*n/den),a.v+(int)((b.v-a.v)*n/den)};
    return c;
}

static long long cpu_edge(struct cpu_screen a,struct cpu_screen b,int x,int y)
{ return (long long)(b.x-a.x)*(y-a.y)-(long long)(b.y-a.y)*(x-a.x); }

static int cpu_edge_band(struct cpu_screen a,struct cpu_screen b,long long e)
{
    double dx=b.x-a.x,dy=b.y-a.y;
    return (double)e*e<=dx*dx+dy*dy; // fixed one-pixel band
}

static void cpu_draw(const struct rf_gpu_graphics_draw *d)
{
    int w=d->projection[0],h=d->projection[1];
    for(unsigned t=d->first_index;t<d->first_index+d->index_count;t+=3) {
        struct cpu_corner input[3],out[4]; struct cpu_screen p[4];int count=0;
        for(unsigned i=0;i<3;++i)input[i]=cpu_transform(d,indices[t+i]);
        for(unsigned i=0;i<3;++i) {
            struct cpu_corner a=input[(i+2)%3],b=input[i];
            if((a.z>=64)!=(b.z>=64))out[count++]=cpu_intersect(a,b);
            if(b.z>=64)out[count++]=b;
        }
        for(int i=0;i<count;++i) {
            p[i].x=w/2+(int)((long long)out[i].x*d->projection[3]/out[i].z);
            p[i].y=h/2-(int)((long long)out[i].y*d->projection[3]/out[i].z);
            p[i].iz=1048576/out[i].z;
            p[i].uoz=(long long)out[i].u*1048576/out[i].z;
            p[i].voz=(long long)out[i].v*1048576/out[i].z;
        }
        for(int fan=1;fan+1<count;++fan) {
            struct cpu_screen a=p[0],b=p[fan],c=p[fan+1];
            long long area=cpu_edge(a,b,c.x,c.y);
            if(!area || (!d->double_sided && area>=0))continue;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                long long e0=cpu_edge(b,c,x,y),e1=cpu_edge(c,a,x,y),e2=cpu_edge(a,b,x,y);
                int at=y*w+x;
                if(cpu_edge_band(b,c,e0)||cpu_edge_band(c,a,e1)||cpu_edge_band(a,b,e2))oracle_edge[at]=1;
                if((area>0 && (e0<0 || e1<0 || e2<0)) || (area<0 && (e0>0 || e1>0 || e2>0)))continue;
                long long inv=e0*a.iz+e1*b.iz+e2*c.iz;
                int depth=(int)(inv/area);
                if(depth<oracle_depth[at])continue;
                uint32_t rgb=d->material[0];
                if(d->material[2]) {
                    unsigned u=inv?(unsigned)((e0*a.uoz+e1*b.uoz+e2*c.uoz)/inv)&65535:0;
                    unsigned v=inv?(unsigned)((e0*a.voz+e1*b.voz+e2*c.voz)/inv)&65535:0;
                    rgb=texels[(v*2/65536)*2+u*2/65536];
                }
                oracle_depth[at]=depth;oracle_color[at]=shaded(d,rgb);
            }
        }
    }
}

/* Exact interior depth and color, including UV-cell boundaries. The old
 * float oracle remains an independent unchanged HG-2A baseline. */
static int compat_oracle(struct rf_gpu_graphics *g,const struct rf_gpu_graphics_draw *d,
                         unsigned count,const char *label,int expect_visible)
{
    unsigned size=(unsigned)(d[0].projection[0]*d[0].projection[1]);
    unsigned covered=0,background=0,excluded=0,depth_errors=0,color_errors=0;
    memset(oracle_depth,0,size*sizeof(*oracle_depth));memset(oracle_edge,0,size);
    for(unsigned p=0;p<size;++p)oracle_color[p]=0xff000000;
    for(unsigned i=0;i<count;++i)cpu_draw(&d[i]);
    if(rf_gpu_graphics_render(g,d,count,pixels,depths,MAX_PIXELS)<0)return -1;
    for(unsigned p=0;p<size;++p) {
        if(oracle_edge[p]){++excluded;continue;}
        if(oracle_depth[p])++covered;else ++background;
        if(depths[p]*16384.0f!=oracle_depth[p])++depth_errors;
        if(pixels[p]!=oracle_color[p])++color_errors;
    }
    printf("compat-oracle %s covered=%u background=%u edge-band=%u depth-errors=%u color-errors=%u\n",
        label,covered,background,excluded,depth_errors,color_errors);
    return !depth_errors && !color_errors && (expect_visible?covered>0:covered==0) &&
        covered+background>size/2 ? 0:-1;
}

static int bridge_case(struct rf_gpu_graphics *g,struct rf_gpu_graphics_draw first,
                       struct rf_gpu_graphics_draw second,const char *label)
{
    struct rf_gpu_graphics_draw pair[2]={first,second};
    unsigned size=(unsigned)(first.projection[0]*first.projection[1]);
    unsigned differences=0,encoding_errors=0;
    if(rf_gpu_graphics_render(g,pair,2,saved,saved_depths,MAX_PIXELS)<0 ||
       rf_gpu_graphics_render(g,&first,1,pixels,depths,MAX_PIXELS)<0)return -1;
    for(unsigned i=0;i<size;++i) {
        uint32_t p=pixels[i];
        oracle_color[i]=(p&0xff00ff00u)|((p&255u)<<16)|((p>>16)&255u);
        oracle_depth[i]=(int)(depths[i]*16384.0f);
    }
    // Host output is intentionally destroyed; the continuation must load GPU targets.
    memset(pixels,0xa5,size*4);memset(depths,0x5a,size*4);
    if(rf_gpu_graphics_continue(g,&second,1,pixels,depths,MAX_PIXELS)<0)return -1;
    for(unsigned i=0;i<size;++i)
        if(pixels[i]!=saved[i] || depths[i]!=saved_depths[i])++differences;
    if(rf_gpu_graphics_read_bridge(g,pixels,bridge_depth,MAX_PIXELS)<0)return -1;
    for(unsigned i=0;i<size;++i) {
        if(pixels[i]!=oracle_color[i] || bridge_depth[i]!=oracle_depth[i])++encoding_errors;
    }
    printf("bridge %s pixels=%u continuation-errors=%u compute-encoding-errors=%u\n",
        label,size,differences,encoding_errors);
    return differences || encoding_errors ? -1:0;
}

static int bridge_suite(struct rf_gpu_graphics *g)
{
    struct rf_gpu_graphics_draw a=draw(128,96),b;
    struct rf_gpu_graphics_stats initial,final;
    int failures=0;
    a.integer_depth=1;a.first_index=12;a.index_count=3;a.translation_scale[2]=0;
    a.material[2]=1;b=a;b.material[2]=0;b.material[0]=0x39c3e7;
    rf_gpu_graphics_get_stats(g,&initial);
    b.translation_scale[2]=-80;
    failures+=bridge_case(g,a,b,"clipped-roundtrip")!=0;
    b.translation_scale[2]=0;
    failures+=bridge_case(g,a,b,"equal-later-wins")!=0;
    b.translation_scale[2]=1;
    failures+=bridge_case(g,a,b,"later-farther")!=0;
    b.translation_scale[2]=-1;
    failures+=bridge_case(g,a,b,"later-nearer")!=0;
    // Repeated load passes must neither clear nor accumulate conversion loss.
    b.translation_scale[2]=-80;
    if(rf_gpu_graphics_render(g,&a,1,saved,saved_depths,MAX_PIXELS)<0)return -1;
    for(unsigned n=0;n<3;++n) {
        failures+=rf_gpu_graphics_continue(g,&b,1,pixels,depths,MAX_PIXELS)<0;
        failures+=memcmp(saved,pixels,128*96*4)!=0 || memcmp(saved_depths,depths,128*96*4)!=0;
    }
    rf_gpu_graphics_get_stats(g,&final);uint64_t frames=final.frames,rounds=final.bridge_roundtrips;
    b.integer_depth=2;
    failures+=rf_gpu_graphics_continue(g,&b,1,pixels,depths,MAX_PIXELS)>=0;
    rf_gpu_graphics_get_stats(g,&final);
    failures+=final.frames!=frames || final.bridge_roundtrips!=rounds;
    if(rf_gpu_graphics_resize(g,320,240)<0)return -1;
    a=draw(320,240);a.integer_depth=1;b=a;b.translation_scale[2]=512;
    failures+=rf_gpu_graphics_continue(g,&a,1,pixels,depths,MAX_PIXELS)>=0;
    failures+=rf_gpu_graphics_read_bridge(g,pixels,oracle_depth,MAX_PIXELS)>=0;
    failures+=bridge_case(g,a,b,"resize-load")!=0;
    if(rf_gpu_graphics_resize(g,128,96)<0)return -1;
    a=draw(128,96);a.integer_depth=1;b=a;b.translation_scale[2]=512;
    failures+=bridge_case(g,a,b,"resize-restored-load")!=0;
    rf_gpu_graphics_get_stats(g,&final);
    failures+=final.mesh_upload_bytes!=initial.mesh_upload_bytes || final.texture_upload_bytes!=initial.texture_upload_bytes;
    failures+=final.bridge_roundtrips-initial.bridge_roundtrips!=9;
    failures+=final.bridge_transfer_bytes-initial.bridge_transfer_bytes!=(uint64_t)(8*128*96+320*240)*16;
    printf("bridge-suite failures=%d roundtrips=%llu GPU-image-copy-bytes=%llu\n",failures,
        (unsigned long long)(final.bridge_roundtrips-initial.bridge_roundtrips),
        (unsigned long long)(final.bridge_transfer_bytes-initial.bridge_transfer_bytes));
    return failures?-1:0;
}

static int compat_suite(struct rf_gpu_graphics *g)
{
    struct rf_gpu_graphics_draw d=draw(128,96),pair[2];
    struct rf_gpu_graphics_stats initial,final;
    int failed=0;
    rf_gpu_graphics_get_stats(g,&initial);d.integer_depth=1;
    failed+=compat_oracle(g,&d,1,"flat",1)!=0;
    memcpy(saved,pixels,128*96*4);memcpy(saved_depths,depths,128*96*4);
    d.first_index=12;d.index_count=3;
    failed+=compat_oracle(g,&d,1,"unclipped-slanted",1)!=0;
    d.translation_scale[2]=0;d.material[2]=1;d.material[1]=173;d.rotation[3]=1;
    failed+=compat_oracle(g,&d,1,"clipped-nearest-Q8",1)!=0;
    d.translation_scale[2]=-32;
    failed+=compat_oracle(g,&d,1,"camera-plane-nearest",1)!=0;
    d.translation_scale[2]=-48;
    failed+=compat_oracle(g,&d,1,"behind-camera-nearest",1)!=0;
    d.translation_scale[2]=-80;
    failed+=compat_oracle(g,&d,1,"fully-behind-near",0)!=0;
    d.translation_scale[2]=32;
    failed+=compat_oracle(g,&d,1,"corner-exactly-near",1)!=0;
    d.translation_scale[2]=-64;
    failed+=compat_oracle(g,&d,1,"near-plane-degenerate",0)!=0;
    d.translation_scale[2]=-30;d.translation_scale[0]=-45;d.rotation[0]=512;d.rotation[1]=886;
    failed+=compat_oracle(g,&d,1,"two-corners-clipped",1)!=0;
    d=draw(128,96);d.integer_depth=1;d.rotation[0]=512;d.rotation[1]=886;d.rotation[2]=-23;
    d.rotation[3]=1;d.translation_scale[3]=1375;d.translation_scale[0]=-11;
    d.camera[0]=7;d.camera[1]=13;d.material[1]=173;d.material[2]=1;
    d.view[0]=89;d.view[1]=1020;d.view[2]=53;d.view[3]=1022;
    failed+=compat_oracle(g,&d,1,"transform-camera-nearest",1)!=0;
    d.double_sided=0;
    failed+=compat_oracle(g,&d,1,"single-sided-front",1)!=0;
    d.first_index=6;
    failed+=compat_oracle(g,&d,1,"single-sided-back",0)!=0;
    d.double_sided=1;
    failed+=compat_oracle(g,&d,1,"double-sided-back",1)!=0;
    // A one-RFU thin separation far away, with a large enough projected patch.
    pair[0]=pair[1]=draw(128,96);
    pair[0].integer_depth=pair[1].integer_depth=1;
    pair[0].translation_scale[3]=pair[1].translation_scale[3]=4000;
    pair[0].translation_scale[2]=4096;pair[1].translation_scale[2]=4097;
    pair[1].material[0]=0x23a1d7;
    failed+=compat_oracle(g,pair,2,"far-thin-wall-later-farther",1)!=0;
    struct rf_gpu_graphics_draw swap=pair[0];pair[0]=pair[1];pair[1]=swap;
    failed+=compat_oracle(g,pair,2,"far-thin-wall-later-nearer",1)!=0;
    pair[0]=pair[1]=draw(128,96);pair[0].integer_depth=pair[1].integer_depth=1;
    pair[0].first_index=pair[1].first_index=12;pair[0].index_count=pair[1].index_count=3;
    pair[0].translation_scale[2]=pair[1].translation_scale[2]=0;
    pair[1].material[0]=0x23a1d7;
    failed+=compat_oracle(g,pair,2,"clipped-equal-later-wins",1)!=0;
    pair[1].translation_scale[2]=1;
    failed+=compat_oracle(g,pair,2,"clipped-intersection",1)!=0;
    // Failed preflight must not submit a prefix of a frame.
    rf_gpu_graphics_get_stats(g,&final);uint64_t frames=final.frames;
    pair[1].translation_scale[0]=131072;
    failed+=rf_gpu_graphics_render(g,pair,2,pixels,depths,MAX_PIXELS)>=0;
    pair[1]=pair[0];pair[1].integer_depth=2;
    failed+=rf_gpu_graphics_render(g,pair,2,pixels,depths,MAX_PIXELS)>=0;
    rf_gpu_graphics_get_stats(g,&final);failed+=final.frames!=frames;
    if(rf_gpu_graphics_resize(g,320,240)<0)return -1;
    d=draw(320,240);d.integer_depth=1;d.material[2]=1;d.material[1]=173;
    failed+=compat_oracle(g,&d,1,"resize-nearest",1)!=0;
    if(rf_gpu_graphics_resize(g,129,97)<0)return -1;
    d=draw(129,97);d.integer_depth=1;d.first_index=12;d.index_count=3;d.material[2]=1;
    d.translation_scale[2]=0;
    failed+=compat_oracle(g,&d,1,"odd-extent-clipped",1)!=0;
    if(rf_gpu_graphics_resize(g,128,96)<0)return -1;
    d=draw(128,96);d.integer_depth=1;
    failed+=compat_oracle(g,&d,1,"resize-restored",1)!=0;
    failed+=memcmp(saved,pixels,128*96*4)!=0 || memcmp(saved_depths,depths,128*96*4)!=0;
    rf_gpu_graphics_get_stats(g,&final);
    failed+=final.mesh_upload_bytes!=initial.mesh_upload_bytes || final.texture_upload_bytes!=initial.texture_upload_bytes;
    printf("compat-suite failures=%d mesh-upload=%llu texture-upload=%llu\n",failed,
        (unsigned long long)final.mesh_upload_bytes,(unsigned long long)final.texture_upload_bytes);
    return failed?-1:0;
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
    int depth_gate=argc==2 && strcmp(argv[1],"--depth-gate")==0;
    if(argc>2){fprintf(stderr,"usage: %s [capture-prefix | --depth-gate]\n",argv[0]);return 2;}
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
    vertices[4].uv[0]=3000;vertices[4].uv[1]=62000;
    vertices[5].uv[0]=65000;vertices[5].uv[1]=8000;
    vertices[6].uv[0]=18000;vertices[6].uv[1]=50000;
    CHECK((g=rf_gpu_graphics_create(&context))!=NULL);
    { uint32_t bad[]={0,1,99};
      CHECK(rf_gpu_graphics_upload(g,vertices,7,bad,3,texels,2,2)<0); }
    CHECK(rf_gpu_graphics_upload(g,vertices,7,indices,15,texels,2,2)==0);
    CHECK(rf_gpu_graphics_upload(g,vertices,7,indices,15,texels,2,2)<0);
    CHECK(rf_gpu_graphics_resize(g,128,96)==0);
    if(depth_gate) {
        int failures=0;
        failures+=depth_gate_case(g,256,1,"constant-depth-control")!=0;
        failures+=depth_gate_case(g,0,0,"near-crossing")!=0;
        failures+=depth_gate_case(g,-32,0,"camera-plane-corner")!=0;
        failures+=depth_gate_case(g,-48,0,"behind-camera-corner")!=0;
        failures+=compat_suite(g)!=0;
        failures+=bridge_suite(g)!=0;
        result=failures?1:0;
        printf("HG-2B depth prerequisite: %s failed-cases=%d\n",result?"FAIL":"PASS",failures);
        goto done;
    }
    rf_gpu_graphics_get_stats(g,&initial);
    CHECK(initial.mesh_upload_bytes==sizeof(vertices)+sizeof(indices)*3);
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
    printf("%s: %s checks=%u\n",depth_gate?"HG-2B prerequisite execution":"HG-2A graphics proof",result?"FAIL":"PASS",checks);
    return result;
}
