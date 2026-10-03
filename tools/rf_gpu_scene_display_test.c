/* Native CPU test of the real retained-display path with a strict fake GPU.
 * No window, Vulkan device, gameplay state, or presentation timing is needed.
 * Compile with -flto -fwhole-program so unused Scene entrypoints are removed. */
#include "tlibc_everything.h"
#include "rf_gpu_scene_world_gpu.h"
#include "rf_core_host.h"
#include "rasterfall_render.h"
#include <limits.h>
#include "../rasterfall/src/render/rf_gpu_scene_layers.inc"

struct rf_gpu_graphics { unsigned creates,updates,destroys,live; int fail_create,fail_update; };
struct rf_gpu_graphics_resource {
    struct rf_gpu_scene_color_vertex *vertices;
    unsigned capacity,count;
};
struct rf_gpu_graphics_resource *rf_gpu_graphics_scene_color_resource_create(
    struct rf_gpu_graphics *g,const struct rf_gpu_scene_color_vertex *v,
    uint32_t n,const uint32_t *indices,uint32_t count)
{
    if(g->fail_create || !n || n%3 || n>65536 || count!=n) return NULL;
    for(unsigned i=0;i<n;++i) if(indices[i]!=i) return NULL;
    struct rf_gpu_graphics_resource *r=calloc(1,sizeof(*r));
    if(!r) return NULL;
    r->vertices=malloc(n*sizeof(*v));
    if(!r->vertices) {free(r);return NULL;}
    memcpy(r->vertices,v,n*sizeof(*v));r->capacity=r->count=n;
    g->creates++;g->live++;return r;
}
int rf_gpu_graphics_scene_color_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *r,const struct rf_gpu_scene_color_vertex *v,uint32_t n)
{
    if(g->fail_update || !r || !v || !n || n%3 || n>65536) return -1;
    if(n>r->capacity) return 1;
    memcpy(r->vertices,v,n*sizeof(*v));r->count=n;g->updates++;return 0;
}
int rf_gpu_graphics_resource_destroy(struct rf_gpu_graphics *g,struct rf_gpu_graphics_resource *r)
{
    if(!r || !g->live) return -1;
    free(r->vertices);free(r);g->destroys++;g->live--;return 0;
}
/* Deterministic asymmetric glyphs exercise foreground/background partitions,
 * facing and varying row runs without requiring a font asset or framebuffer. */
unsigned char fb_font_glyph_row(unsigned char ch,int row)
{
    if(ch<=32 || row<0 || row>=16) return 0;
    return (unsigned char)((ch*13u+row*19u)^((ch+row)<<3));
}
int64_t rf_core_clock_now_us(void) { return 0; }
void *__memset(void *p,int value,size_t n)
{
    unsigned char *bytes=p;for(size_t i=0;i<n;++i) bytes[i]=(unsigned char)value;return p;
}

static int equal_source(const struct scene_layer_packet *p,const struct toy_map_draw *d)
{
    struct scene_layer_mesh source={0};source.layer=RF_GPU_SCENE_WORLD;source.emissive=1;
    int result=-1;unsigned index=0;
    if(rf_display_generate(d,scene_host_quad,&source)<0 || source.count!=p->triangles) goto done;
    for(unsigned run=0;run<p->run_count;++run) {
        const struct rf_gpu_graphics_batch_item *item=&p->runs[run];
        const struct rf_gpu_graphics_draw *draw=&item->draw;
        const struct rf_gpu_graphics_resource *r=item->resource;
        if(!r || draw->first_index+draw->index_count>r->count || draw->material[3]!=2 ||
            draw->quality[2]!=3 || draw->texture[2]!=256 || !draw->double_sided) goto done;
        for(unsigned j=0;j<draw->index_count;j+=3,++index) {
            if(index>=source.count) goto done;
            const struct scene_layer_triangle *t=&source.triangles[index];
            for(unsigned k=0;k<3;++k) {
                const struct rf_gpu_scene_color_vertex *v=&r->vertices[draw->first_index+j+k];
                if(v->rgb24!=t->color || v->light_q8!=t->alpha) goto done;
                for(int axis=0;axis<3;++axis) {
                    int expected=t->vertex[k].position[axis]+t->origin[axis];
                    if(v->position[axis]+draw->translation_scale[axis]!=expected ||
                        expected<p->min[axis] || expected>p->max[axis]) goto done;
                }
            }
        }
    }
    result=index==source.count ? 0 : -1;
done:
    free(source.triangles);return result;
}
#define CHECK(test) do {if(!(test)) {printf("DISPLAY-PACKET FAIL line=%d\n",__LINE__);return 1;}} while(0)
int main(void)
{
    struct rf_gpu_graphics gpu={0};struct scene_layer_workspace owner={0};
    struct scene_layer_packet packet={0};struct toy_map_draw d={0};
    owner.graphics=&gpu;
    d.type=TOY_MAP_DRAW_SIGN;d.style=5;d.facing=1;d.color=0x79DDE8;
    d.a=69000;d.b=69160;d.c=d.d=-31000;d.e=-630;d.f=-526;
    d.texture_u=160;d.texture_v=104;strcpy(d.text,"PROGRESS 10%");
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && !equal_source(&packet,&d));
    CHECK(packet.triangles && packet.allocated_triangles>packet.triangles &&
        owner.retained_triangles==packet.allocated_triangles);
    unsigned created=gpu.creates,updated=gpu.updates,capacity=packet.allocated_triangles;
    struct rf_gpu_graphics_resource *same=packet.resources[0];
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && gpu.creates==created && gpu.updates==updated);
    strcpy(d.text,"PROGRESS 11%");
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && !equal_source(&packet,&d));
    CHECK(packet.resources[0]==same && gpu.creates==created && gpu.updates>updated &&
        !gpu.destroys && packet.allocated_triangles==capacity);
    d.color=0x123456;d.facing=-1;d.a+=70000;d.b+=70000;d.c+=90000;d.d+=90000;
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && !equal_source(&packet,&d));
    CHECK(packet.resources[0]==same && gpu.creates==created);
    /* Grow beyond one chunk, then shrink while retaining high-water capacity. */
    d.style=4;d.a=-16000;d.b=16000;d.c=0;d.d=20;d.e=900;d.f=1500;d.texture_u=1;
    memset(d.text,'W',sizeof(d.text)-1);d.text[sizeof(d.text)-1]=0;
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && !equal_source(&packet,&d));
    CHECK(gpu.creates>created && packet.resource_count>1 && packet.triangles>RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES);
    created=gpu.creates;capacity=packet.allocated_triangles;same=packet.resources[0];
    strcpy(d.text,"READY");
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && !equal_source(&packet,&d));
    CHECK(packet.resources[0]==same && gpu.creates==created && packet.allocated_triangles==capacity);
    CHECK(owner.retained_triangles==capacity && capacity<=SCENE_RETAINED_TRIANGLES);
    /* Invalid source is a fallback, never a stale successful packet. */
    d.style=5;d.texture_u=160;d.texture_v=104;d.b=d.a+161;
    CHECK(scene_packet_prepare(&owner,&packet,&d)==1 && packet.valid<0 &&
        !packet.runs && !gpu.live && !owner.retained_triangles);
    d.b=d.a+160;d.e=0;d.f=104;
    owner.retained_triangles=SCENE_RETAINED_TRIANGLES;
    CHECK(scene_packet_prepare(&owner,&packet,&d)==1 && !gpu.live &&
        owner.retained_triangles==SCENE_RETAINED_TRIANGLES);
    scene_packet_clear(&owner,&packet);owner.retained_triangles=0;
    /* An exactly full owner allocates no spare capacity and accounts only for
     * this packet when a subsequent growth must fall back. */
    struct scene_layer_mesh sized={0};sized.layer=RF_GPU_SCENE_WORLD;sized.emissive=1;
    CHECK(!rf_display_generate(&d,scene_host_quad,&sized));
    unsigned other=SCENE_RETAINED_TRIANGLES-sized.count;free(sized.triangles);
    owner.retained_triangles=other;
    CHECK(!scene_packet_prepare(&owner,&packet,&d) &&
        packet.allocated_triangles==packet.triangles && owner.retained_triangles==SCENE_RETAINED_TRIANGLES);
    strcpy(d.text,"READY READY READY");
    CHECK(scene_packet_prepare(&owner,&packet,&d)==1 && !gpu.live && owner.retained_triangles==other);
    scene_packet_clear(&owner,&packet);owner.retained_triangles=0;
    CHECK(!scene_packet_prepare(&owner,&packet,&d));
    gpu.fail_update=1;strcpy(d.text,"FAULT");
    CHECK(scene_packet_prepare(&owner,&packet,&d)<0 && !packet.runs &&
        !gpu.live && !owner.retained_triangles);
    gpu.fail_update=0;scene_packet_clear(&owner,&packet);gpu.fail_create=1;
    CHECK(scene_packet_prepare(&owner,&packet,&d)<0 && !gpu.live && !owner.retained_triangles);
    gpu.fail_create=0;scene_packet_clear(&owner,&packet);
    /* Empty transparent projection releases its former resources. */
    d.style=2;d.a=-16000;d.b=16000;d.e=900;d.f=1500;strcpy(d.text,"LIVE");
    CHECK(!scene_packet_prepare(&owner,&packet,&d));d.text[0]=0;
    CHECK(!scene_packet_prepare(&owner,&packet,&d) && packet.valid>0 &&
        !packet.triangles && !packet.runs && !gpu.live && !owner.retained_triangles);
    scene_packet_clear(&owner,&packet);
    CHECK(gpu.creates==gpu.destroys);
    printf("DISPLAY-PACKET content/order/origin/bounds/reuse/growth/shrink/budget/failure/cleanup passed\n");
    return 0;
}
