#include "rf_gpu.h"
#include "rf_gpu_raster_abi.h"
#include "rf_gpu_raster_cpu_ref.h"
#include "rf_gpu_raster_pack.h"
#include "rf_gpu_vulkan_backend.h"
#include "toy_renderer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "diff test failed:%d: %s\n", \
    __LINE__, #c); goto done; } } while (0)

struct stream { unsigned char *data; size_t size; };
struct texture_bundle { struct rf_gpu_texture_desc_v1 *descs; uint32_t count; unsigned char *texels; size_t bytes; };
struct outputs {
    uint32_t *cpu_color, *gpu_color;
    int32_t *cpu_depth, *gpu_depth;
    uint32_t width, height;
};

static uint64_t hash_bytes(const void *memory, size_t size)
{
    const unsigned char *p = memory;
    uint64_t h = 1469598103934665603ULL;
    while (size--) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}

static void add_triangle(struct toy_renderer *r, int ax, int ay, int az,
                         int bx, int by, int bz, int cx, int cy, int cz,
                         uint32_t color, int light, int fog)
{
    struct toy_screen_vertex a, b, c;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));
    a.x = ax; a.y = ay; a.inv_z = az;
    b.x = bx; b.y = by; b.inv_z = bz;
    c.x = cx; c.y = cy; c.inv_z = cz;
    toy_renderer_triangle_lit(r, &a, &b, &c, color, light, fog);
}

static void add_vertex_lit_triangle(struct toy_renderer *r,
                         int ax, int ay, int az, int al,
                         int bx, int by, int bz, int bl,
                         int cx, int cy, int cz, int cl,
                         uint32_t color, int fog)
{
    struct toy_screen_vertex a, b, c;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));
    a.x=ax; a.y=ay; a.inv_z=az; a.light=al;
    b.x=bx; b.y=by; b.inv_z=bz; b.light=bl;
    c.x=cx; c.y=cy; c.inv_z=cz; c.light=cl;
    toy_renderer_triangle_planar_vertex_lit(r,&a,&b,&c,color,fog);
}

static void add_textured_triangle(struct toy_renderer *r,
                         int ax,int ay,int az,int au,int av,
                         int bx,int by,int bz,int bu,int bv,
                         int cx,int cy,int cz,int cu,int cv,
                         const struct toy_texture_view *texture,
                         int repeat,int light,int fog)
{
    struct toy_screen_vertex a,b,c;
    memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));memset(&c,0,sizeof(c));
    a.x=ax;a.y=ay;a.inv_z=az;a.u_over_z=(long)au*az;a.v_over_z=(long)av*az;
    b.x=bx;b.y=by;b.inv_z=bz;b.u_over_z=(long)bu*bz;b.v_over_z=(long)bv*bz;
    c.x=cx;c.y=cy;c.inv_z=cz;c.u_over_z=(long)cu*cz;c.v_over_z=(long)cv*cz;
    toy_renderer_triangle_textured_lit(r,&a,&b,&c,texture,repeat,0,light,fog);
}

static int stream_from_renderer(struct toy_renderer *r, uint32_t clear,
                                struct stream *out)
{
    size_t capacity = rf_gpu_raster_stream_size_v1((uint32_t)r->cmd_count + 2);
    out->data = malloc(capacity);
    if (!out->data) return -1;
    if (rf_gpu_raster_pack_toy_v1(r, clear, 0, out->data, capacity,
                                  &out->size) != RF_GPU_RASTER_PACK_OK) {
        free(out->data); memset(out, 0, sizeof(*out)); return -1;
    }
    return 0;
}

static int make_fixture(const char *name, uint32_t width, uint32_t height,
                        uint32_t count, uint32_t seed, struct stream *out)
{
    struct toy_renderer r;
    struct toy_surface surface;
    uint32_t *pixels = calloc((size_t)width * height, 4);
    uint32_t clear = 0x00102030u, i;
    int result = -1;
    if (!pixels) return -1;
    memset(&r, 0, sizeof(r));
    surface.pixels = pixels; surface.width = (int)width;
    surface.height = (int)height; surface.stride = (int)(width * 4);
    toy_renderer_init(&r);
    if (toy_renderer_begin(&r, &surface, clear) < 0) goto done;
    if (!strcmp(name, "clear") || !strcmp(name, "sky")) {
        /* no geometry */
    } else if (!strcmp(name, "geometry-depth-order")) {
        add_triangle(&r, 2,2,100, 16,2,200, 2,10,300, 0x4080c0,256,0);
        add_triangle(&r, 4,3,50, 14,3,50, 4,9,50, 0xff0000,256,0);
        add_triangle(&r, 4,3,400, 14,3,400, 4,9,400, 0x00ff00,256,0);
        add_triangle(&r, 4,3,400, 14,3,400, 4,9,400, 0x0000ff,256,0);
    } else if (!strcmp(name, "shared-edge")) {
        add_triangle(&r, 2,2,77, 12,2,77, 2,10,77, 0xff0000,256,0);
        add_triangle(&r, 12,2,77, 12,10,77, 2,10,77, 0x00ff00,256,0);
    } else if (!strcmp(name, "light-fog-thin")) {
        add_triangle(&r, 0,0,10, 8,0,10, 0,8,10, 0x80c0ff,0,0);
        add_triangle(&r, 9,0,20, 17,0,20, 9,8,20, 0x80c0ff,256,128);
        add_triangle(&r, 0,9,30, 8,9,30, 0,12,30, 0xc08040,384,0);
        add_triangle(&r, 9,9,40, 17,9,40, 9,12,40, 0xabcdef,999,999);
        add_triangle(&r, 1,8,60, 17,8,60, 1,9,60, 0xffffff,256,0);
    } else if (!strcmp(name, "grid")) {
        int y, x, cell = 7;
        for (y = 0; y < (int)height; y += cell) for (x = 0; x < (int)width; x += cell) {
            int x1 = x + cell < (int)width ? x + cell : (int)width - 1;
            int y1 = y + cell < (int)height ? y + cell : (int)height - 1;
            add_triangle(&r,x,y,100,x1,y,100,x,y1,100,0x224466,256,0);
            add_triangle(&r,x1,y,100,x1,y1,100,x,y1,100,0x6688aa,256,0);
        }
    } else if (!strcmp(name, "edges-mixed")) {
        add_triangle(&r,-30,-20,30,(int)width+20,0,400,0,(int)height+30,90,0xff00ff,192,64);
        add_triangle(&r,0,0,500,(int)width-1,0,500,0,(int)height-1,500,0xffff00,384,0);
        add_triangle(&r,(int)width-2,0,600,(int)width-1,(int)height-1,600,0,(int)height-1,600,0x00ffff,256,220);
        add_triangle(&r,1,(int)height/2,700,(int)width-2,(int)height/2,700,1,(int)height/2+1,700,0xffffff,256,0);
    } else if (!strcmp(name, "equal-near-far")) {
        for (i = 0; i < count; ++i) {
            int z = (i % 3 == 0) ? 200 : (i % 3 == 1 ? 900 : 200);
            add_triangle(&r,1,1,z,(int)width-2,1,z,1,(int)height-2,z,
                         (i * 0x10203u) & 0xffffffu, 64 + (int)(i%7)*64,
                         (int)(i%5)*64);
        }
    } else if (!strcmp(name, "vertex-lit-fixed")) {
        /* equal/different/extreme lights, interpolated center, varying depth,
         * fog, shared edge, overlap/equal-depth order, flat mix, offscreen and thin. */
        add_vertex_lit_triangle(&r,1,1,100,256, 14,1,100,256, 1,10,100,256,0x804020,0);
        add_vertex_lit_triangle(&r,16,1,100,0, 31,1,300,256, 16,11,500,384,0x90c0f0,0);
        add_vertex_lit_triangle(&r,1,12,200,0, 16,12,400,384, 1,23,600,256,0xff8040,128);
        add_vertex_lit_triangle(&r,18,13,300,0, 31,13,300,256, 18,23,300,384,0xff0000,0);
        add_vertex_lit_triangle(&r,31,13,300,384, 31,23,300,256, 18,23,300,0,0x00ff00,0);
        add_vertex_lit_triangle(&r,20,15,300,384, 29,15,300,0, 20,22,300,256,0x0000ff,64);
        add_triangle(&r,20,15,300,29,15,300,20,22,300,0xffff00,192,0);
        add_vertex_lit_triangle(&r,-12,2,700,0, 12,2,900,384, 2,20,1100,256,0xabcdef,0);
        add_vertex_lit_triangle(&r,2,25,1200,0, 34,25,1200,384, 2,26,1200,256,0xffffff,0);
    } else { /* deterministic stress */
        uint32_t state = seed ? seed : 1;
        for (i = 0; i < count; ++i) {
            int margin_x = (int)width / 4 + 1, margin_y = (int)height / 4 + 1;
            int ax = (int)(rng_next(&state) % (width + 2u*margin_x)) - margin_x;
            int ay = (int)(rng_next(&state) % (height + 2u*margin_y)) - margin_y;
            int bx = (int)(rng_next(&state) % (width + 2u*margin_x)) - margin_x;
            int by = (int)(rng_next(&state) % (height + 2u*margin_y)) - margin_y;
            int cx = (int)(rng_next(&state) % (width + 2u*margin_x)) - margin_x;
            int cy = (int)(rng_next(&state) % (height + 2u*margin_y)) - margin_y;
            int az = 1 + (int)(rng_next(&state) % 2000000u);
            int bz = 1 + (int)(rng_next(&state) % 2000000u);
            int cz = 1 + (int)(rng_next(&state) % 2000000u);
            int64_t area = ((int64_t)cx-ax)*(by-ay)-((int64_t)cy-ay)*(bx-ax);
            if (area >= 0) { int tx=bx,ty=by,tz=bz; bx=cx;by=cy;bz=cz;cx=tx;cy=ty;cz=tz; }
            add_triangle(&r,ax,ay,az,bx,by,bz,cx,cy,cz,rng_next(&state)&0xffffffu,
                         (int)(rng_next(&state)%513u), (int)(rng_next(&state)%257u));
        }
    }
    result = stream_from_renderer(&r, clear, out);
    if (result == 0 && !strcmp(name, "sky")) {
        struct rf_gpu_raster_stream_header_v1 *header = (void *)out->data;
        struct rf_gpu_raster_cmd_v1 *commands = (void *)(header + 1);
        memset(&commands[0], 0, sizeof(commands[0]));
        commands[0].kind = RF_GPU_RASTER_CMD_SKY_V1;
        commands[0].byte_size = RF_GPU_RASTER_CMD_V1_SIZE;
        commands[0].payload.sky.direction_sy = 724;
        commands[0].payload.sky.direction_cy = 724;
        commands[0].payload.sky.pitch_sy = -128;
        commands[0].payload.sky.pitch_cy = 1016;
        commands[0].payload.sky.zenith_color = 0x3b82c4;
        commands[0].payload.sky.horizon_color = 0xb9e3ff;
        commands[0].payload.sky.ground_color = 0x0f1218;
    }
done:
    toy_renderer_destroy(&r); free(pixels); return result;
}

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return -1;
    ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int read_file(const char *path, struct stream *stream)
{
    FILE *f = fopen(path, "rb"); long size;
    if (!f || fseek(f,0,SEEK_END) || (size=ftell(f)) < 0 || fseek(f,0,SEEK_SET)) {
        if (f) fclose(f);
        return -1;
    }
    stream->data = malloc((size_t)size); stream->size = (size_t)size;
    if (!stream->data || fread(stream->data,1,stream->size,f) != stream->size) {
        free(stream->data); memset(stream,0,sizeof(*stream)); fclose(f); return -1;
    }
    fclose(f); return 0;
}

static int read_texture_bundle(const char *path,struct texture_bundle *bundle)
{
    FILE *f=fopen(path,"rb");uint32_t h[4];size_t desc_bytes;
    if(!f)return 1;
    if(fread(h,1,sizeof(h),f)!=sizeof(h)||h[0]!=0x31544652U||
       h[2]!=sizeof(struct rf_gpu_texture_desc_v1)){fclose(f);return -1;}
    desc_bytes=(size_t)h[1]*sizeof(*bundle->descs);
    bundle->descs=malloc(desc_bytes);bundle->texels=malloc(h[3]);
    if((desc_bytes&&!bundle->descs)||(h[3]&&!bundle->texels)||
       fread(bundle->descs,1,desc_bytes,f)!=desc_bytes||
       fread(bundle->texels,1,h[3],f)!=h[3]||fgetc(f)!=EOF){fclose(f);return -1;}
    fclose(f);bundle->count=h[1];bundle->bytes=h[3];return 0;
}

static int write_bmp(const char *path, const uint32_t *pixels, uint32_t w, uint32_t h)
{
    unsigned char header[54] = { 'B','M' };
    uint32_t file_size = 54 + w*h*4, offset=54, dib=40, planes_bpp=0x00200001u;
    int32_t sw=(int32_t)w, sh=-(int32_t)h; FILE *f;
    memcpy(header+2,&file_size,4); memcpy(header+10,&offset,4); memcpy(header+14,&dib,4);
    memcpy(header+18,&sw,4); memcpy(header+22,&sh,4); memcpy(header+26,&planes_bpp,4);
    f=fopen(path,"wb"); if(!f)return -1;
    if(fwrite(header,1,54,f)!=54 || fwrite(pixels,4,(size_t)w*h,f)!=(size_t)w*h){fclose(f);return -1;}
    return fclose(f);
}

static void save_artifacts(const char *dir, const struct stream *s,
                           const struct outputs *o, const char *report)
{
    char path[512]; uint32_t *diff = malloc((size_t)o->width*o->height*4);
#if defined(_WIN32)
    char command[600]; snprintf(command,sizeof(command),"mkdir \"%s\" 2>NUL",dir);
    if (system(command) != 0 && errno != EEXIST) return;
#else
    char command[600]; snprintf(command,sizeof(command),"mkdir -p \"%s\"",dir);
    if (system(command) != 0) return;
#endif
    snprintf(path,sizeof(path),"%s/commands.bin",dir); write_file(path,s->data,s->size);
    snprintf(path,sizeof(path),"%s/cpu-color.bmp",dir); write_bmp(path,o->cpu_color,o->width,o->height);
    snprintf(path,sizeof(path),"%s/gpu-color.bmp",dir); write_bmp(path,o->gpu_color,o->width,o->height);
    if(diff){for(size_t i=0;i<(size_t)o->width*o->height;i++) diff[i]=
        ((o->cpu_color[i]^o->gpu_color[i])&0xffffffu)?0xffff00ffu:0xff000000u;
        snprintf(path,sizeof(path),"%s/diff-color.bmp",dir);write_bmp(path,diff,o->width,o->height);free(diff);}
    snprintf(path,sizeof(path),"%s/cpu-depth.bin",dir);write_file(path,o->cpu_depth,(size_t)o->width*o->height*4);
    snprintf(path,sizeof(path),"%s/gpu-depth.bin",dir);write_file(path,o->gpu_depth,(size_t)o->width*o->height*4);
    snprintf(path,sizeof(path),"%s/report.txt",dir);write_file(path,report,strlen(report));
}

static int compare_case(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                        const char *name, const struct stream *s,
                        const char *artifact_dir, int run_full_scan,
                        const struct texture_bundle *textures)
{
    const struct rf_gpu_raster_stream_header_v1 *h=(const void*)s->data;
    struct outputs o; struct rf_gpu_cpu_reference_timing ct;
    struct rf_gpu_raster_timing gt,ft; size_t n=(size_t)h->framebuffer_width*h->framebuffer_height;
    uint64_t ch,gh,cdh,gdh; uint64_t cm=0,dm=0,fcm=0,fdm=0; uint32_t firstx=0,firsty=0;
    int have_first=0,maxr=0,maxg=0,maxb=0; int64_t maxd=0; char report[2048];
    memset(&o,0,sizeof(o));o.width=h->framebuffer_width;o.height=h->framebuffer_height;
    o.cpu_color=malloc(n*4);o.gpu_color=malloc(n*4);o.cpu_depth=malloc(n*4);o.gpu_depth=malloc(n*4);
    if(!o.cpu_color||!o.gpu_color||!o.cpu_depth||!o.gpu_depth)return -1;
    memset(&ft,0,sizeof(ft));
    if(raster->width!=o.width||raster->height!=o.height)if(rf_gpu_raster_resize(gpu,raster,o.width,o.height)<0)return -1;
    if(textures&&textures->count){
      if(rf_gpu_raster_cpu_reference_textured_v1(s->data,s->size,textures->descs,textures->count,textures->texels,textures->bytes,o.cpu_color,o.cpu_depth,o.width,o.width,&ct)<0)return -1;
    }else if(rf_gpu_raster_cpu_reference_v1(s->data,s->size,o.cpu_color,o.cpu_depth,o.width,o.width,&ct)<0)return -1;
    if(run_full_scan){
      if(rf_gpu_raster_set_full_scan_diagnostic(raster,1)<0)return -1;
      if(textures&&textures->count){
        if(rf_gpu_raster_render_textured_timed(gpu,raster,s->data,s->size,
           textures->descs,textures->count,textures->texels,textures->bytes,
           o.gpu_color,o.gpu_depth,o.width,o.height,o.width,o.width,&ft)<0)return -1;
      }else if(rf_gpu_raster_render_timed(gpu,raster,s->data,s->size,
                o.gpu_color,o.gpu_depth,o.width,o.height,o.width,o.width,&ft)<0)return -1;
      for(size_t i=0;i<n;i++){if((o.cpu_color[i]&0xffffffu)!=(o.gpu_color[i]&0xffffffu))fcm++;if(o.cpu_depth[i]!=o.gpu_depth[i])fdm++;}
    }
    if(rf_gpu_raster_set_full_scan_diagnostic(raster,0)<0)return -1;
    if(textures&&textures->count){if(rf_gpu_raster_render_textured_timed(gpu,raster,s->data,s->size,textures->descs,textures->count,textures->texels,textures->bytes,o.gpu_color,o.gpu_depth,o.width,o.height,o.width,o.width,&gt)<0)return -1;}
    else if(rf_gpu_raster_render_timed(gpu,raster,s->data,s->size,o.gpu_color,o.gpu_depth,o.width,o.height,o.width,o.width,&gt)<0)return -1;
    for(size_t i=0;i<n;i++){
        uint32_t cc=o.cpu_color[i]&0xffffffu,gc=o.gpu_color[i]&0xffffffu;
        if(cc!=gc){int dr=abs((int)(cc>>16&255)-(int)(gc>>16&255));int dg=abs((int)(cc>>8&255)-(int)(gc>>8&255));int db=abs((int)(cc&255)-(int)(gc&255));cm++;if(dr>maxr)maxr=dr;if(dg>maxg)maxg=dg;if(db>maxb)maxb=db;if(!have_first){firstx=i%o.width;firsty=i/o.width;have_first=1;}}
        if(o.cpu_depth[i]!=o.gpu_depth[i]){int64_t d=(int64_t)o.cpu_depth[i]-o.gpu_depth[i];if(d<0)d=-d;dm++;if(d>maxd)maxd=d;if(!have_first){firstx=i%o.width;firsty=i/o.width;have_first=1;}}
    }
    ch=hash_bytes(o.cpu_color,n*4);gh=hash_bytes(o.gpu_color,n*4);cdh=hash_bytes(o.cpu_depth,n*4);gdh=hash_bytes(o.gpu_depth,n*4);
    snprintf(report,sizeof(report),
      "fixture: %s\nfull-scan diagnostic: %s\nfull-scan color mismatches: %llu\nfull-scan depth mismatches: %llu\ncolor mismatches: %llu\ndepth mismatches: %llu\nfirst mismatch coordinate: %s%u,%u\nCPU color: 0x%08x\nGPU color: 0x%08x\nCPU depth: %d\nGPU depth: %d\nmax R delta: %d\nmax G delta: %d\nmax B delta: %d\nmax depth delta: %lld\nCPU color hash: %016llx\nGPU color hash: %016llx\nCPU depth hash: %016llx\nGPU depth hash: %016llx\nframe size: %ux%u\ncommand count: %u\ntile count: %u\ntotal refs: %llu\naverage refs/tile: %.3f\nmaximum refs/tile: %u\nCPU raster ms: %.3f\nCPU tile binning ms: %.3f\ntile-list upload ms: %.3f\ncommand upload ms: %.3f\nGPU validation ms: %.3f\nGPU upload ms: %.3f\nGPU submit ms: %.3f\nfull-scan execution-wait ms: %.3f\ntile-binned execution-wait ms: %.3f\nGPU readback ms: %.3f\nGPU total ms: %.3f\n",
      name,run_full_scan?"executed":"skipped for real-world replay",(unsigned long long)fcm,(unsigned long long)fdm,(unsigned long long)cm,(unsigned long long)dm,have_first?"":"none ",firstx,firsty,
      have_first?o.cpu_color[(size_t)firsty*o.width+firstx]:0,have_first?o.gpu_color[(size_t)firsty*o.width+firstx]:0,
      have_first?o.cpu_depth[(size_t)firsty*o.width+firstx]:0,have_first?o.gpu_depth[(size_t)firsty*o.width+firstx]:0,
      maxr,maxg,maxb,(long long)maxd,(unsigned long long)ch,(unsigned long long)gh,(unsigned long long)cdh,(unsigned long long)gdh,
      o.width,o.height,gt.command_count,gt.tile_count,gt.total_refs,gt.tile_count?(double)gt.total_refs/gt.tile_count:0.0,gt.max_refs_per_tile,
      ct.raster_ms,gt.cpu_binning_ms,gt.tile_upload_ms,gt.command_upload_ms,gt.pack_validation_ms,gt.upload_ms,gt.submit_ms,ft.execution_wait_ms,gt.execution_wait_ms,gt.readback_ms,gt.total_ms);
    fputs(report,stdout);
    if(cm||dm){save_artifacts(artifact_dir,s,&o,report);fprintf(stderr,"mismatch artifacts: %s\n",artifact_dir);}
    free(o.cpu_color);free(o.gpu_color);free(o.cpu_depth);free(o.gpu_depth);return (cm||dm||fcm||fdm)?-1:0;
}

static int texture_fixture(struct rf_gpu *gpu,struct rf_gpu_raster *raster)
{
    static const unsigned char texels_a[16]={
        255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    static const unsigned char texels_b[64]={
        8,16,24,255, 32,40,48,255, 56,64,72,255, 80,88,96,255,
        104,112,120,255, 128,136,144,255, 152,160,168,255, 176,184,192,255,
        200,208,216,255, 224,232,240,255, 248,128,64,255, 64,128,248,255,
        255,255,0,255, 0,255,255,255, 255,0,255,255, 20,30,40,255};
    struct toy_texture_view ta={texels_a,2,2,sizeof(texels_a),4,0};
    struct toy_texture_view tb={texels_b,4,4,sizeof(texels_b),4,0};
    struct toy_renderer r;struct toy_surface surface;struct stream s={0};
    struct rf_gpu_texture_resources_v1 resources;uint32_t *pixels=NULL,*cc=NULL,*gc=NULL;
    int32_t *cd=NULL,*gd=NULL;size_t n=37u*29u,cap;uint32_t unique;size_t bytes;int result=-1;
    memset(&r,0,sizeof(r));memset(&resources,0,sizeof(resources));
    pixels=calloc(n,4);cc=malloc(n*4);gc=malloc(n*4);cd=malloc(n*4);gd=malloc(n*4);
    if(!pixels||!cc||!gc||!cd||!gd)goto done;
    surface.pixels=pixels;surface.width=37;surface.height=29;surface.stride=37*4;
    toy_renderer_init(&r);if(toy_renderer_begin(&r,&surface,0x101820)<0)goto done;
    /* exact texels, wrap/clamp edges, perspective, light/fog, overlap/equal
     * depth, shared edge, offscreen and thin, repeated and second handles. */
    add_textured_triangle(&r,1,1,128,0,0, 14,1,128,65535,0, 1,12,128,0,65535,&ta,0,256,0);
    add_textured_triangle(&r,16,1,64,-65536,-1, 34,1,256,131072,0, 16,13,128,0,131072,&ta,1,256,0);
    add_textured_triangle(&r,1,14,100,0,0, 18,14,300,65535,0, 1,27,500,0,65535,&tb,0,192,96);
    add_textured_triangle(&r,20,15,300,0,0, 35,15,300,65535,0, 20,27,300,0,65535,&ta,0,256,0);
    add_textured_triangle(&r,20,15,300,65535,65535, 35,15,300,0,65535, 20,27,300,65535,0,&tb,0,256,0);
    add_textured_triangle(&r,-8,4,700,0,0, 8,4,900,65535,0, 0,20,1100,0,65535,&ta,1,384,180);
    add_textured_triangle(&r,2,27,1200,0,0, 35,27,1200,65535,0, 2,28,1200,0,65535,&ta,0,256,0);
    add_triangle(&r,5,5,40,12,5,40,5,11,40,0xabcdef,256,0);
    add_vertex_lit_triangle(&r,24,3,500,0,35,3,500,384,24,12,500,256,0xffffff,64);
    CHECK(rf_gpu_raster_measure_textures_toy_v1(&r,&unique,&bytes)==0&&unique==2&&bytes==80);
    resources.descs=calloc(unique,sizeof(*resources.descs));resources.desc_capacity=unique;
    resources.texels=malloc(bytes);resources.texel_capacity=bytes;
    cap=rf_gpu_raster_stream_size_v1((uint32_t)r.cmd_count+2);s.data=malloc(cap);
    CHECK(resources.descs&&resources.texels&&s.data);
    CHECK(rf_gpu_raster_pack_toy_textured_v1(&r,0x101820,0,s.data,cap,&s.size,&resources)==0);
    CHECK(resources.desc_count==2&&resources.texel_size==80);
    CHECK(rf_gpu_raster_cpu_reference_textured_v1(s.data,s.size,resources.descs,
        resources.desc_count,resources.texels,resources.texel_size,cc,cd,37,37,NULL)==0);
    CHECK(rf_gpu_raster_resize(gpu,raster,37,29)==0);
    CHECK(rf_gpu_raster_set_full_scan_diagnostic(raster,1)==0);
    CHECK(rf_gpu_raster_render_textured_timed(gpu,raster,s.data,s.size,
        resources.descs,resources.desc_count,resources.texels,resources.texel_size,
        gc,gd,37,29,37,37,NULL)==0);
    for(size_t i=0;i<n;i++)if((cc[i]&0xffffffu)!=(gc[i]&0xffffffu)||cd[i]!=gd[i]){
        fprintf(stderr,"texture full-scan mismatch at %llu (%llu,%llu) cpu=%08x/%d gpu=%08x/%d\n",
            (unsigned long long)i,(unsigned long long)(i%37),
            (unsigned long long)(i/37),cc[i],cd[i],gc[i],gd[i]);goto done;}
    CHECK(rf_gpu_raster_set_full_scan_diagnostic(raster,0)==0);
    CHECK(rf_gpu_raster_render_textured_timed(gpu,raster,s.data,s.size,
        resources.descs,resources.desc_count,resources.texels,resources.texel_size,
        gc,gd,37,29,37,37,NULL)==0);
    for(size_t i=0;i<n;i++)if((cc[i]&0xffffffu)!=(gc[i]&0xffffffu)||cd[i]!=gd[i]){
        fprintf(stderr,"texture mismatch at %llu (%llu,%llu) cpu=%08x/%d gpu=%08x/%d\n",
            (unsigned long long)i,(unsigned long long)(i%37),
            (unsigned long long)(i/37),cc[i],cd[i],gc[i],gd[i]);goto done;}
    { struct rf_gpu_texture_desc_v1 saved=resources.descs[0];
      struct rf_gpu_raster_cmd_v1 *cmds=(void*)((struct rf_gpu_raster_stream_header_v1*)s.data+1);
      uint32_t saved_handle=cmds[2].resource_handle;cmds[2].resource_handle=resources.desc_count+1;
      CHECK(rf_gpu_raster_render_textured_timed(gpu,raster,s.data,s.size,
        resources.descs,resources.desc_count,resources.texels,resources.texel_size,
        gc,gd,37,29,37,37,NULL)<0);cmds[2].resource_handle=saved_handle;
      resources.descs[0].texel_offset=(uint32_t)resources.texel_size;
      CHECK(rf_gpu_raster_render_textured_timed(gpu,raster,s.data,s.size,
        resources.descs,resources.desc_count,resources.texels,resources.texel_size,
        gc,gd,37,29,37,37,NULL)<0);resources.descs[0]=saved;
      CHECK(rf_gpu_raster_render_textured_timed(gpu,raster,s.data,s.size,
        resources.descs,resources.desc_count,resources.texels,resources.texel_size-1,
        gc,gd,37,29,37,37,NULL)<0);
    }
    puts("fixture: texture-v1 CPU/full-scan/tile-binned color mismatches: 0 depth mismatches: 0 max delta: 0 invalid-handle/table/bounds: PASS");
    result=0;
done:
    toy_renderer_destroy(&r);free(pixels);free(cc);free(gc);free(cd);free(gd);
    free(s.data);free(resources.descs);free(resources.texels);return result;
}

int main(int argc,char **argv)
{
    struct rf_gpu gpu;struct rf_gpu_vulkan_context context;struct rf_gpu_raster raster;
    struct rf_gpu_status status;struct stream s={0};const char *replay=NULL,*artifacts="build/gpu-raster-diff-mismatch";
    const struct {const char *name;uint32_t w,h,n,seed;} cases[]={
      {"clear",19,13,0,0},{"sky",97,61,0,0},{"geometry-depth-order",19,13,0,0},{"shared-edge",19,13,0,0},
      {"light-fog-thin",19,13,0,0},{"grid",53,29,0,0},{"edges-mixed",37,23,0,0},
      {"equal-near-far",73,41,96,0},{"stress-seed-1",320,180,64,1},
      {"vertex-lit-fixed",37,29,0,0},
      {"stress-seed-2",641,359,256,2},{"stress-seed-0x5246",1279,719,1024,0x5246}};
    int result=1;memset(&context,0,sizeof(context));memset(&raster,0,sizeof(raster));
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--replay-raster-stream")&&i+1<argc)replay=argv[++i];else if(!strcmp(argv[i],"--artifact-dir")&&i+1<argc)artifacts=argv[++i];else{fprintf(stderr,"usage: %s [--replay-raster-stream commands.bin] [--artifact-dir dir]\n",argv[0]);return 2;}}
    CHECK(rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context)==0);
    CHECK(rf_gpu_get_status(&gpu,&status)==0&&status.renderer.raster_v1);
    printf("adapter: %s\n",status.info.adapter_name);
    if(replay){struct texture_bundle tb={0};char tp[1024];int tr;CHECK(read_file(replay,&s)==0);CHECK(rf_gpu_raster_validate_v1(s.data,s.size)==0);snprintf(tp,sizeof(tp),"%s.textures",replay);tr=read_texture_bundle(tp,&tb);CHECK(tr>=0);const struct rf_gpu_raster_stream_header_v1*h=(void*)s.data;CHECK(rf_gpu_raster_init(&gpu,&raster,h->framebuffer_width,h->framebuffer_height)==0);CHECK(compare_case(&gpu,&raster,"replay",&s,artifacts,0,tr==0?&tb:NULL)==0);free(tb.descs);free(tb.texels);free(s.data);s.data=NULL;}
    else {CHECK(rf_gpu_raster_init(&gpu,&raster,19,13)==0);for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){CHECK(make_fixture(cases[i].name,cases[i].w,cases[i].h,cases[i].n,cases[i].seed,&s)==0);CHECK(compare_case(&gpu,&raster,cases[i].name,&s,artifacts,1,NULL)==0);if(i==1)CHECK(write_file("build/gpu-raster-diff-replay.bin",s.data,s.size)==0);free(s.data);s.data=NULL;}CHECK(read_file("build/gpu-raster-diff-replay.bin",&s)==0);CHECK(compare_case(&gpu,&raster,"replay-self-check",&s,artifacts,1,NULL)==0);free(s.data);s.data=NULL;CHECK(texture_fixture(&gpu,&raster)==0);
      /* Failure authority: no partial output and all malformed classes reject. */
      CHECK(make_fixture("clear",19,13,0,0,&s)==0);unsigned char saved=s.data[0];uint32_t guard_color[19*13];int32_t guard_depth[19*13];for(size_t j=0;j<19*13;j++){guard_color[j]=0x13579bdfu;guard_depth[j]=0x12345678;}s.data[0]^=1;CHECK(rf_gpu_raster_validate_v1(s.data,s.size)!=0);CHECK(rf_gpu_raster_render(&gpu,&raster,s.data,s.size,guard_color,guard_depth,19,13,19,19)<0);for(size_t j=0;j<19*13;j++)CHECK(guard_color[j]==0x13579bdfu&&guard_depth[j]==0x12345678);s.data[0]=saved;((struct rf_gpu_raster_stream_header_v1*)s.data)->version++;CHECK(rf_gpu_raster_validate_v1(s.data,s.size)!=0);((struct rf_gpu_raster_stream_header_v1*)s.data)->version--;CHECK(rf_gpu_raster_validate_v1(s.data,s.size-1)!=0);((struct rf_gpu_raster_stream_header_v1*)s.data)->command_count=0xffffffffu;CHECK(rf_gpu_raster_validate_v1(s.data,s.size)!=0);((struct rf_gpu_raster_stream_header_v1*)s.data)->command_count=2;((struct rf_gpu_raster_cmd_v1*)((struct rf_gpu_raster_stream_header_v1*)s.data+1))[0].kind=999;CHECK(rf_gpu_raster_validate_v1(s.data,s.size)!=0);puts("failure-cases: invalid/version/truncated/unsupported/oversized/corrupt/no-partial-output PASS");free(s.data);s.data=NULL;}
    result=0;
done:free(s.data);rf_gpu_raster_shutdown(&raster);rf_gpu_shutdown(&gpu);if(context.implementation)result=1;puts(result?"GPU Raster Differential: FAIL":"GPU Raster Differential: PASS");return result;
}
