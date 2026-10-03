/* Offline audit of the actual native producer helpers. No GPU is opened. */
#include "tlibc_everything.h"
#include "rf_gpu_graphics.h"
#include "rasterfall_camera.h"
#include "rf_mesh_weaver_presentation.h"
#include "rf_weaver_blueprints_generated.h"
#include <limits.h>
#include <math.h>
/* Rendering functions are deliberately unreachable. These declaration-only
 * producer surroundings let the compiler discard them without a GPU shim. */
struct audit_layers { struct rf_mesh_weaver_frame weaver; };
struct rf_gpu_scene_world_gpu_probe {
    struct rf_mesh_weaver_gpu *weaver;
    struct rf_gpu_graphics *graphics;
    struct rf_gpu_graphics_batch_item *batch;
    uint32_t batch_capacity;
    struct audit_layers *layers;
    int quiet;
};
extern int scene_batch_reserve(struct rf_gpu_graphics_batch_item **,uint32_t *,uint32_t);
extern void scene_draw_camera(struct rf_gpu_graphics_draw *,const struct camera *,unsigned,unsigned);
#include "../rasterfall/src/render/rf_mesh_weaver_gpu.inc"

static int audit_failures;
static void audit_require(int condition,const char *message)
{
    if(!condition && audit_failures++<20)__printf("FAIL %s\n",message);
}
static void audit_free(struct rf_weaver_gpu_mesh *mesh)
{
    rf_weaver_mesh_free(&mesh->source);free(mesh->edges);free(mesh->points);
    memset(mesh,0,sizeof(*mesh));
}
static int audit_load(struct rf_weaver_gpu_mesh *mesh,int weapon,const char *root)
{
    const struct rasterfall_weapon_asset_profile *p=rasterfall_weapon_asset_profile(weapon);
    if(!p || !p->model_path)return -1;
    char path[1024];snprintf(path,sizeof(path),"%s/%s",root,p->model_path);
    if(rf_weaver_mesh_load(&mesh->source,path,weapon)<0 ||
       rf_weaver_gpu_edges_create(mesh)<0 || rf_weaver_gpu_points_create(mesh)<0)return -1;
    mesh->vertex_count=mesh->point_first+mesh->point_count*RF_WEAVER_GPU_POINT_VERTICES;
    return 0;
}
static void audit_weapon(int weapon,const char *root)
{
    struct rf_weaver_gpu_mesh mesh={0},second={0};
    int ok=audit_load(&mesh,weapon,root)==0;
    audit_require(ok,"source load");if(!ok)goto done;
    audit_require(mesh.vertex_count<=RF_WEAVER_GPU_VERTEX_LIMIT,"all cached geometry within backend vertex limit");
    audit_require(mesh.vertex_count%3==0,"complete cached triangles");
    audit_require(mesh.point_count<=RF_WEAVER_GPU_POINT_LIMIT,"bounded markers");
    if(RF_WEAVER_GPU_VERTEX_LIMIT-mesh.point_first>=RF_WEAVER_GPU_POINT_VERTICES)
        audit_require(mesh.point_count>0,"available budget retains topology markers");
    for(unsigned i=0;i<mesh.point_count;++i) {
        const struct rf_weaver_gpu_point *p=&mesh.points[i];
        audit_require(p->face<mesh.source.count && p->corner<3,"source identity in range");
        if(p->face>=mesh.source.count || p->corner>=3)continue;
        const struct rf_weaver_face *f=&mesh.source.faces[p->face];
        audit_require(!memcmp(p->position,f->vertex[p->corner].position,sizeof(p->position)),"marker is an actual source topology vertex");
        audit_require(p->threshold==f->threshold,"threshold belongs to actual incident face");
        if(i)audit_require(mesh.points[i-1].threshold<=p->threshold,"sorted growth thresholds");
        for(unsigned j=0;j<i;++j)
            audit_require(rf_weaver_gpu_point_compare(mesh.points[j].position,p->position)!=0,"no duplicate welded topology point");
        uint32_t bind[RF_WEAVER_GPU_POINT_VERTICES*22+2];
        bind[0]=0x12345678;bind[RF_WEAVER_GPU_POINT_VERTICES*22+1]=0x76543210;
        rf_weaver_gpu_bind_point(bind+1,p);
        audit_require(bind[0]==0x12345678 && bind[RF_WEAVER_GPU_POINT_VERTICES*22+1]==0x76543210,"marker bind write remains bounded");
        long long center[3]={0};
        for(unsigned v=0;v<RF_WEAVER_GPU_POINT_VERTICES;++v) {
            const uint32_t *b=bind+1+v*22;
            for(unsigned influence=0;influence<4;++influence)
                audit_require(b[14+influence*2]==0 && b[15+influence*2]==65535,"position and normal channels follow rigid gun bone");
            for(unsigned k=0;k<3;++k)center[k]+=(int)b[k]-p->position[k];
        }
        audit_require(!center[0] && !center[1] && !center[2],"glyph remains centered on actual topology position");
    }
    unsigned maximum=0,nonempty=0;
    for(unsigned step=0;step<=1000;++step) {
        double progress=step/1000.0,solid=progress-RF_WEAVER_ACTIVE_BEHIND,front=progress+RF_WEAVER_ACTIVE_AHEAD;
        unsigned start=rf_weaver_gpu_point_limit(&mesh,solid),end=rf_weaver_gpu_point_limit(&mesh,front);
        audit_require(start<=end && end<=mesh.point_count,"valid single draw window");
        for(unsigned i=start;i<end;++i)
            audit_require(mesh.points[i].threshold>solid && mesh.points[i].threshold<=front,"all bright points within unchanged active face band");
        if(start)audit_require(mesh.points[start-1].threshold<=solid,"first marker is lower bound");
        if(end<mesh.point_count)audit_require(mesh.points[end].threshold>front,"last marker is upper bound");
        if(end>start)++nonempty;
        if(end-start>maximum)maximum=end-start;
    }
    ok=audit_load(&second,weapon,root)==0;audit_require(ok,"repeat source load");
    if(ok) {
        audit_require(second.vertex_count==mesh.vertex_count && second.point_count==mesh.point_count,"repeat budget deterministic");
        for(unsigned i=0;i<mesh.point_count && i<second.point_count;++i) {
            const struct rf_weaver_gpu_point *a=&mesh.points[i],*b=&second.points[i];
            audit_require(!rf_weaver_gpu_point_compare(a->position,b->position) && a->threshold==b->threshold && a->face==b->face && a->corner==b->corner,"repeat selection/source/threshold deterministic");
        }
    }
    __printf("weapon=%d faces=%u edges=%u topology_points=%u cached_vertices=%u active_max=%u nonempty=%u/1001\n",
        weapon,mesh.source.count,mesh.edge_count,mesh.point_count,mesh.vertex_count,maximum,nonempty);
done:
    audit_free(&mesh);audit_free(&second);
}
static void audit_overflow(void)
{
    struct rf_weaver_gpu_mesh mesh={0};
    const unsigned original_count=RF_WEAVER_GPU_VERTEX_LIMIT/3-5;
    mesh.source.count=original_count;
    mesh.source.faces=calloc(mesh.source.count,sizeof(*mesh.source.faces));
    audit_require(mesh.source.faces!=NULL,"large synthetic allocation");if(!mesh.source.faces)return;
    mesh.source.maximum[2]=mesh.source.count;
    for(unsigned i=0;i<mesh.source.count;++i) {
        struct rf_weaver_face *f=&mesh.source.faces[i];f->source=i;f->threshold=i/(double)mesh.source.count;
        f->vertex[0].position[0]=(int)i*30;f->vertex[0].position[2]=i;
        f->vertex[1].position[0]=(int)i*30+10;f->vertex[1].position[2]=i;
        f->vertex[2].position[0]=(int)i*30+5;f->vertex[2].position[1]=10;f->vertex[2].position[2]=i;
    }
    size_t source_bytes=(size_t)mesh.source.count*sizeof(*mesh.source.faces);
    struct rf_weaver_face *original=malloc(source_bytes);
    audit_require(original!=NULL,"source snapshot allocation");
    if(!original) {audit_free(&mesh);return;}
    memcpy(original,mesh.source.faces,source_bytes);
    audit_require(rf_weaver_gpu_edges_create(&mesh)==0 && rf_weaver_gpu_points_create(&mesh)==0,"overbudget effects degrade without failure");
    unsigned count=mesh.point_first+mesh.point_count*RF_WEAVER_GPU_POINT_VERTICES;
    audit_require(count<=RF_WEAVER_GPU_VERTEX_LIMIT && mesh.source.count==original_count &&
        !memcmp(original,mesh.source.faces,source_bytes),"oversized detail preserves complete source entity under vertex budget");
    __printf("synthetic faces=%u sparse_edges=%u points=%u vertices=%u\n",mesh.source.count,mesh.edge_count,mesh.point_count,count);
    free(original);audit_free(&mesh);
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    /* Rejected blueprints are included: merely selecting one can load its
     * idle preview. Unrelated inventory models do not own this contract. */
    for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)
        audit_weapon(rf_weaver_blueprints[i].geometry.weapon,argv[1]);
    audit_overflow();
    __printf("weaver_gpu_cache_audit failures=%d\n",audit_failures);
    return audit_failures?1:0;
}
