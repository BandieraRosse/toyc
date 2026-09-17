#include "rf_gpu.h"
#include "rf_gpu_raster_abi.h"
#include "rf_gpu_vulkan_backend.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"overlay GPU fixture FAIL line %d: %s\n",__LINE__,#x); goto done; } } while(0)
static void triangle(struct rf_gpu_raster_cmd_v1 *cmd){
struct rf_gpu_raster_flat_triangle_v1 *t=&cmd->payload.flat_triangle;
cmd->kind=RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1;cmd->byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
cmd->flags=RF_GPU_RASTER_FLAG_DEPTH_TEST_V1|RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1|RF_GPU_RASTER_FLAG_OPAQUE_V1;
t->a.x=0;t->a.y=0;t->a.inv_z=2200;t->b.x=7;t->b.y=0;t->b.inv_z=1200;
t->c.x=0;t->c.y=5;t->c.inv_z=200;t->area=-35;t->bbox_minx=0;t->bbox_maxx=6;t->bbox_miny=0;t->bbox_maxy=4;
t->color=0xff406080U;t->light_q8=256;
}
static uint32_t fog_reference(uint32_t scene,int d,const struct rf_gpu_post_params_v1 *p){
unsigned int f=0,inv,r,g,b;if(d<=p->fog_far_inv_z)f=p->max_density_q8;else if(d<p->fog_near_inv_z)f=(unsigned int)(p->fog_near_inv_z-d)*p->max_density_q8/(unsigned int)(p->fog_near_inv_z-p->fog_far_inv_z);if(f>256)f=256;inv=256-f;r=(((scene>>16)&255)*inv+((p->fog_color>>16)&255)*f)>>8;g=(((scene>>8)&255)*inv+((p->fog_color>>8)&255)*f)>>8;b=((scene&255)*inv+(p->fog_color&255)*f)>>8;return 0xff000000U|(r<<16)|(g<<8)|b;}
int main(void){
enum{W=7,H=5,OS=10,CS=11,OUTS=12};
struct {struct rf_gpu_raster_stream_header_v1 header;struct rf_gpu_raster_cmd_v1 commands[3];} stream;
struct rf_gpu gpu;struct rf_gpu_vulkan_context context;struct rf_gpu_raster raster;
struct rf_gpu_post_params_v1 post;uint32_t overlay[OS*H],expected[OUTS*H],actual[OUTS*H],base[OUTS*H];int32_t depth[OUTS*H],base_depth[OUTS*H];unsigned char coverage[CS*H];unsigned int x,y,mismatches=0;int result=1;
memset(&gpu,0,sizeof(gpu));memset(&context,0,sizeof(context));memset(&raster,0,sizeof(raster));memset(&stream,0,sizeof(stream));memset(overlay,0,sizeof(overlay));memset(coverage,0,sizeof(coverage));
stream.header.magic=RF_GPU_RASTER_ABI_MAGIC;stream.header.version=RF_GPU_RASTER_ABI_VERSION;stream.header.header_size=RF_GPU_RASTER_STREAM_HEADER_V1_SIZE;stream.header.command_size=RF_GPU_RASTER_CMD_V1_SIZE;stream.header.command_count=3;stream.header.framebuffer_width=W;stream.header.framebuffer_height=H;stream.header.endian_tag=RF_GPU_RASTER_ENDIAN_LITTLE;
stream.commands[0].kind=RF_GPU_RASTER_CMD_CLEAR_COLOR_V1;stream.commands[0].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;stream.commands[0].payload.clear.value=0x00102030U;stream.commands[1].kind=RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1;stream.commands[1].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
triangle(&stream.commands[2]);
for(y=0;y<H;y++)for(x=0;x<OUTS;x++){expected[y*OUTS+x]=x<W?0xff102030U:0x13579bdfU;actual[y*OUTS+x]=0x13579bdfU;depth[y*OUTS+x]=0x12345678;}
overlay[0]=0xff000000U;coverage[0]=255;overlay[1]=0xffffffffU;coverage[1]=255;overlay[2]=0xff204060U;coverage[2]=128;
for(y=1;y<4;y++)for(x=1;x<6;x++){overlay[y*OS+x]=0xff55aa33U;coverage[y*CS+x]=255;}
overlay[(H-1)*OS+W-1]=0xffabcdefU;coverage[(H-1)*CS+W-1]=255;
CHECK(!rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context));CHECK(!rf_gpu_raster_init(&gpu,&raster,W,H));
CHECK(!rf_gpu_raster_composite_diagnostic(&gpu,&raster,&stream,sizeof(stream),overlay,coverage,OS,CS,actual,depth,W,H,OUTS,OUTS));
for(y=0;y<H;y++)for(x=0;x<W;x++)expected[y*OUTS+x]=depth[y*OUTS+x]>0?0xff406080U:0xff102030U;
CHECK(!rf_gpu_overlay_composite_reference(expected,OUTS,overlay,OS,coverage,CS,W,H));
for(y=0;y<H;y++)for(x=0;x<W;x++)if(actual[y*OUTS+x]!=expected[y*OUTS+x])mismatches++;
CHECK(!mismatches);printf("overlay GPU differential PASS mismatch=0 coverage=0/128/255\n");
memcpy(base,actual,sizeof(base));memcpy(base_depth,depth,sizeof(depth));
memset(&post,0,sizeof(post));post.mode=RF_GPU_POST_IDENTITY;CHECK(!rf_gpu_raster_set_post(&raster,&post));
CHECK(!rf_gpu_raster_composite_diagnostic(&gpu,&raster,&stream,sizeof(stream),overlay,coverage,OS,CS,actual,depth,W,H,OUTS,OUTS));
for(y=0;y<H;y++)for(x=0;x<W;x++)if(actual[y*OUTS+x]!=base[y*OUTS+x])mismatches++;
CHECK(!mismatches);printf("post identity GPU differential PASS mismatch=0 odd=7x5 non-tight-stride=10/11/12 alpha=ff\n");
memset(coverage,0,sizeof(coverage));post.mode=RF_GPU_POST_DEPTH_FOG_V0;post.fog_far_inv_z=256;post.fog_near_inv_z=2048;post.fog_color=0xff90a0b0U;post.max_density_q8=192;CHECK(!rf_gpu_raster_set_post(&raster,&post));
CHECK(!rf_gpu_raster_composite_diagnostic(&gpu,&raster,&stream,sizeof(stream),overlay,coverage,OS,CS,actual,depth,W,H,OUTS,OUTS));mismatches=0;
for(y=0;y<H;y++)for(x=0;x<W;x++){uint32_t scene=base_depth[y*OUTS+x]>0?0xff406080U:0xff102030U;uint32_t e=fog_reference(scene,base_depth[y*OUTS+x],&post);if(actual[y*OUTS+x]!=e||((actual[y*OUTS+x]>>24)!=255))mismatches++;}
CHECK(!mismatches);printf("post fog GPU differential PASS mismatch=0 depth=Q20-inverse-z boundary/partial/saturated alpha=ff\n");result=0;
done:rf_gpu_raster_shutdown(&raster);rf_gpu_shutdown(&gpu);return result;}
