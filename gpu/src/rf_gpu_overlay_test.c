#include "rf_gpu.h"
#include "rf_gpu_raster_abi.h"
#include "rf_gpu_vulkan_backend.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"overlay GPU fixture FAIL line %d: %s\n",__LINE__,#x); goto done; } } while(0)
int main(void){
enum{W=7,H=5,OS=10,CS=11,OUTS=12};
struct {struct rf_gpu_raster_stream_header_v1 header;struct rf_gpu_raster_cmd_v1 commands[2];} stream;
struct rf_gpu gpu;struct rf_gpu_vulkan_context context;struct rf_gpu_raster raster;
uint32_t overlay[OS*H],expected[OUTS*H],actual[OUTS*H];int32_t depth[OUTS*H];unsigned char coverage[CS*H];unsigned int x,y,mismatches=0;int result=1;
memset(&gpu,0,sizeof(gpu));memset(&context,0,sizeof(context));memset(&raster,0,sizeof(raster));memset(&stream,0,sizeof(stream));memset(overlay,0,sizeof(overlay));memset(coverage,0,sizeof(coverage));
stream.header.magic=RF_GPU_RASTER_ABI_MAGIC;stream.header.version=RF_GPU_RASTER_ABI_VERSION;stream.header.header_size=RF_GPU_RASTER_STREAM_HEADER_V1_SIZE;stream.header.command_size=RF_GPU_RASTER_CMD_V1_SIZE;stream.header.command_count=2;stream.header.framebuffer_width=W;stream.header.framebuffer_height=H;stream.header.endian_tag=RF_GPU_RASTER_ENDIAN_LITTLE;
stream.commands[0].kind=RF_GPU_RASTER_CMD_CLEAR_COLOR_V1;stream.commands[0].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;stream.commands[0].payload.clear.value=0x00102030U;stream.commands[1].kind=RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1;stream.commands[1].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
for(y=0;y<H;y++)for(x=0;x<OUTS;x++){expected[y*OUTS+x]=x<W?0xff102030U:0x13579bdfU;actual[y*OUTS+x]=0x13579bdfU;depth[y*OUTS+x]=0x12345678;}
overlay[0]=0xff000000U;coverage[0]=255;overlay[1]=0xffffffffU;coverage[1]=255;overlay[2]=0xff204060U;coverage[2]=128;
for(y=1;y<4;y++)for(x=1;x<6;x++){overlay[y*OS+x]=0xff55aa33U;coverage[y*CS+x]=255;}
overlay[(H-1)*OS+W-1]=0xffabcdefU;coverage[(H-1)*CS+W-1]=255;
CHECK(!rf_gpu_overlay_composite_reference(expected,OUTS,overlay,OS,coverage,CS,W,H));
CHECK(!rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context));CHECK(!rf_gpu_raster_init(&gpu,&raster,W,H));
CHECK(!rf_gpu_raster_composite_diagnostic(&gpu,&raster,&stream,sizeof(stream),overlay,coverage,OS,CS,actual,depth,W,H,OUTS,OUTS));
for(y=0;y<H;y++)for(x=0;x<OUTS;x++)if(actual[y*OUTS+x]!=expected[y*OUTS+x])mismatches++;
CHECK(!mismatches);printf("overlay GPU differential PASS mismatch=0 odd=7x5 non-tight-stride=10/11/12 coverage=0/128/255\n");result=0;
done:rf_gpu_raster_shutdown(&raster);rf_gpu_shutdown(&gpu);return result;}
