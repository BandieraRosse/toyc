/* Mesh Weaver's native Scene owner.  Immutable component/weapon bind geometry
 * stays on the GPU; a frame changes only rigid palettes and draw windows.
 * Included after rf_gpu_scene_layers.inc for the existing batch/camera helpers. */
#include "rf_mesh_weaver_presentation.h"
#include "rf_mesh_weaver_layout_generated.h"
#include "rasterfall_calibration.h"
#include "toy_mesh_weaver.h"

#define RF_WEAVER_GPU_VERTEX_LIMIT 65535u
#define RF_WEAVER_GPU_MACHINE_BONES 41u
#define RF_WEAVER_GPU_EFFECT_VERTICES 768u
#define RF_WEAVER_GPU_POINT_LIMIT 256u
#define RF_WEAVER_GPU_POINT_VERTICES 24u

struct rf_weaver_gpu_edge { int point[2][3]; double threshold; };
/* One quantized topology position, with its actual incident source face and
 * corner retained for audit. The face's reveal threshold owns this marker. */
struct rf_weaver_gpu_point {
    int position[3];
    double threshold;
    unsigned face, corner;
};

struct rf_weaver_gpu_run {
    unsigned first, count;
    struct rf_weaver_material material;
    int optical;
};
struct rf_weaver_gpu_mesh {
    struct rf_gpu_graphics_resource *resource;
    struct rf_weaver_mesh source;
    struct rf_weaver_gpu_run runs[RF_WEAVER_MAX_MATERIALS];
    unsigned run_count, vertex_count;
    struct rf_weaver_gpu_edge *edges;
    unsigned edge_count, edge_first;
    struct rf_weaver_gpu_point *points;
    unsigned point_count, point_first;
};
struct rf_mesh_weaver_gpu {
    struct rf_weaver_gpu_mesh machine;
    struct rf_weaver_gpu_mesh weapons[TOY_GAME_WEAPON_COUNT];
    struct rf_gpu_graphics_resource *effects;
    struct rf_gpu_scene_color_vertex effect_vertices[RF_WEAVER_GPU_EFFECT_VERTICES];
    unsigned effect_count;
    float work_light_position[3];
    int work_light_valid;
};

static int rf_weaver_gpu_round(double value)
{ return (int)(value+(value<0?-.5:.5)); }
static void rf_weaver_gpu_cross(const double a[3],const double b[3],double out[3])
{
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
static int rf_weaver_gpu_normalize(double v[3],double length)
{
    double norm=sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(norm<.000001)return 0;
    for(unsigned k=0;k<3;++k)v[k]*=length/norm;
    return 1;
}

static uint32_t rf_weaver_gpu_float(double value)
{
    float f=(float)value;uint32_t word;memcpy(&word,&f,sizeof(word));return word;
}
static void rf_weaver_gpu_palette(const struct rf_weaver_transform *bones,
    unsigned count,uint32_t *words)
{
    for(unsigned bone=0;bone<count;++bone) {
        uint32_t *p=words+bone*15;
        for(unsigned k=0;k<9;++k)p[k]=rf_weaver_gpu_float(bones[bone].rotation[k]);
        for(unsigned k=0;k<3;++k) {
            p[9+k]=rf_weaver_gpu_float(bones[bone].position[k]);
            p[12+k]=0; /* Authored component origin is the rigid bind pivot. */
        }
    }
}
static void rf_weaver_gpu_bind_face(uint32_t *words,
    const struct rf_weaver_face *face,unsigned bone)
{
    for(unsigned corner=0;corner<3;++corner) {
        uint32_t *v=words+corner*22;
        memset(v,0,22*sizeof(*v));
        for(unsigned k=0;k<3;++k)v[k]=(uint32_t)face->vertex[corner].position[k];
        for(unsigned n=0;n<3;++n)for(unsigned k=0;k<3;++k)
            v[5+n*3+k]=(uint32_t)face->vertex[n].normal[k];
        for(unsigned influence=0;influence<4;++influence) {
            v[14+influence*2]=bone|(bone<<16);
            v[15+influence*2]=65535; /* BDEF1: position and all corner normals. */
        }
    }
}
static int rf_weaver_gpu_create_resource(struct rf_gpu_graphics *graphics,
    struct rf_weaver_gpu_mesh *mesh,uint32_t *bind,const uint32_t *palette,
    unsigned palette_words)
{
    uint32_t white=0xffffff,*indices;
    if(!mesh->vertex_count || mesh->vertex_count>RF_WEAVER_GPU_VERTEX_LIMIT)return -1;
    indices=malloc((size_t)mesh->vertex_count*sizeof(*indices));
    if(!indices)return -1;
    for(unsigned i=0;i<mesh->vertex_count;++i)indices[i]=i;
    mesh->resource=rf_gpu_graphics_skinned_resource_create(graphics,NULL,
        mesh->vertex_count,indices,mesh->vertex_count,bind,mesh->vertex_count*22,
        palette,palette_words,&white,1,1);
    free(indices);return mesh->resource?0:-1;
}
static void rf_weaver_gpu_mesh_close(struct rf_gpu_graphics *graphics,
    struct rf_weaver_gpu_mesh *mesh)
{
    if(mesh->resource)rf_gpu_graphics_resource_destroy(graphics,mesh->resource);
    rf_weaver_mesh_free(&mesh->source);free(mesh->edges);free(mesh->points);
    memset(mesh,0,sizeof(*mesh));
}
static void rf_weaver_gpu_close(struct rf_gpu_scene_world_gpu_probe *probe)
{
    if(!probe || !probe->weaver)return;
    rf_weaver_gpu_mesh_close(probe->graphics,&probe->weaver->machine);
    for(unsigned i=0;i<TOY_GAME_WEAPON_COUNT;++i)
        rf_weaver_gpu_mesh_close(probe->graphics,&probe->weaver->weapons[i]);
    if(probe->weaver->effects)rf_gpu_graphics_resource_destroy(probe->graphics,probe->weaver->effects);
    free(probe->weaver);probe->weaver=NULL;
}
static int rf_weaver_gpu_material(struct rf_weaver_gpu_mesh *mesh,
    const struct rf_weaver_material *material,int optical)
{
    for(unsigned i=0;i<mesh->run_count;++i) {
        const struct rf_weaver_gpu_run *r=&mesh->runs[i];
        if(r->material.color==material->color && r->material.roughness==material->roughness &&
           r->material.metallic==material->metallic && r->optical==optical)return (int)i;
    }
    if(mesh->run_count>=RF_WEAVER_MAX_MATERIALS)return -1;
    unsigned index=mesh->run_count++;
    mesh->runs[index].material=*material;mesh->runs[index].optical=optical;
    return (int)index;
}
static int rf_weaver_gpu_machine_create(struct rf_gpu_scene_world_gpu_probe *probe,
    const uint32_t *palette)
{
    static const char *paths[]={
        "rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_tray.rmesh",
        "rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_yoke.rmesh",
        "rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_core.rmesh",
        "rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_petal.rmesh"};
    static const unsigned instances[]={1,8,8,24},first_bone[]={0,1,9,17};
    struct rf_weaver_mesh parts[4]={{0}};
    int material_map[4][RF_WEAVER_MAX_MATERIALS];
    struct rf_weaver_gpu_mesh *machine=&probe->weaver->machine;
    uint32_t *bind=NULL;unsigned faces=0;int result=-1;
    for(unsigned part=0;part<4;++part) {
        if(rf_weaver_mesh_load(&parts[part],paths[part],-1)<0)goto done;
        if(parts[part].count>(RF_WEAVER_GPU_VERTEX_LIMIT/3-faces)/instances[part])goto done;
        faces+=parts[part].count*instances[part];
        for(unsigned m=0;m<parts[part].material_count;++m) {
            /* The authored core's fourth material is its optical aperture;
             * other blue metal remains a normally lit mechanical material. */
            material_map[part][m]=rf_weaver_gpu_material(machine,&parts[part].materials[m],part==2 && m==3);
            if(material_map[part][m]<0)goto done;
        }
    }
    machine->vertex_count=faces*3;
    bind=malloc((size_t)machine->vertex_count*22*sizeof(*bind));if(!bind)goto done;
    unsigned at=0;
    for(unsigned material=0;material<machine->run_count;++material) {
        struct rf_weaver_gpu_run *run=&machine->runs[material];run->first=at*3;
        for(unsigned part=0;part<4;++part)for(unsigned instance=0;instance<instances[part];++instance)
            for(unsigned face=0;face<parts[part].count;++face) {
                const struct rf_weaver_face *f=&parts[part].faces[face];
                if(material_map[part][f->material]!=(int)material)continue;
                rf_weaver_gpu_bind_face(bind+(size_t)at*66,f,first_bone[part]+instance);++at;
            }
        run->count=at*3-run->first;
    }
    if(at!=faces || rf_weaver_gpu_create_resource(probe->graphics,machine,bind,palette,
        RF_WEAVER_GPU_MACHINE_BONES*15)<0)goto done;
    if(!probe->quiet)__printf("SCENE-WEAVER machine triangles=%u materials=%u rigid_bones=41 immutable_bind=1\n",faces,machine->run_count);
    result=0;
done:
    for(unsigned i=0;i<4;++i)rf_weaver_mesh_free(&parts[i]);
    free(bind);if(result)rf_weaver_gpu_mesh_close(probe->graphics,machine);return result;
}
static int rf_weaver_gpu_point_compare(const int a[3],const int b[3])
{
    for(unsigned k=0;k<3;++k)if(a[k]!=b[k])return a[k]<b[k]?-1:1;
    return 0;
}
static int rf_weaver_gpu_edge_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_gpu_edge *a=pa,*b=pb;
    int result=rf_weaver_gpu_point_compare(a->point[0],b->point[0]);
    return result?result:rf_weaver_gpu_point_compare(a->point[1],b->point[1]);
}
static int rf_weaver_gpu_edge_growth_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_gpu_edge *a=pa,*b=pb;
    if(a->threshold!=b->threshold)return a->threshold<b->threshold?-1:1;
    return rf_weaver_gpu_edge_compare(pa,pb);
}
static int rf_weaver_gpu_edges_create(struct rf_weaver_gpu_mesh *mesh)
{
    unsigned count=0;
    struct rf_weaver_gpu_edge *edges=malloc((size_t)mesh->source.count*3*sizeof(*edges));
    if(!edges)return -1;
    for(unsigned face=0;face<mesh->source.count;++face)for(unsigned edge=0;edge<3;++edge) {
        const struct rf_weaver_face *f=&mesh->source.faces[face];
        const int *a=f->vertex[edge].position,*b=f->vertex[(edge+1)%3].position;
        int order=rf_weaver_gpu_point_compare(a,b);if(!order)continue;
        struct rf_weaver_gpu_edge *e=&edges[count++];
        memcpy(e->point[0],order<0?a:b,3*sizeof(int));
        memcpy(e->point[1],order<0?b:a,3*sizeof(int));e->threshold=f->threshold;
    }
    qsort(edges,count,sizeof(*edges),rf_weaver_gpu_edge_compare);
    unsigned unique=0;
    for(unsigned first=0;first<count;) {
        unsigned end=first+1;double threshold=edges[first].threshold;
        while(end<count && !rf_weaver_gpu_edge_compare(&edges[first],&edges[end])) {
            if(edges[end].threshold>threshold)threshold=edges[end].threshold;
            ++end;
        }
        struct rf_weaver_gpu_edge e=edges[first];e.threshold=threshold;
        double length=0;uint32_t hash=2166136261u;
        for(unsigned k=0;k<3;++k) {
            double d=e.point[1][k]-e.point[0][k];length+=d*d;
            hash=(hash^(uint32_t)e.point[0][k])*16777619u;
            hash=(hash^(uint32_t)e.point[1][k])*16777619u;
        }
        /* Keep long silhouette/topology spans, and one third of short detail
         * edges. Selection is frozen at load, never a per-frame scan/LOD cost. */
        if(length>=16 && (length>=.015*.015*RF_WEAVER_LOCAL_UNITS*RF_WEAVER_LOCAL_UNITS || hash%3==0))
            edges[unique++]=e;
        first=end;
    }
    qsort(edges,unique,sizeof(*edges),rf_weaver_gpu_edge_growth_compare);
    /* Effect detail may thin out on large unsupported previews; the original
     * faces always fit first. A selected idle blueprint must remain loadable. */
    unsigned capacity=(RF_WEAVER_GPU_VERTEX_LIMIT-mesh->source.count*3)/12;
    if(unique>capacity) {
        for(unsigned i=0;i<capacity;++i)edges[i]=edges[(size_t)i*unique/capacity];
        unique=capacity;
    }
    mesh->edges=edges;mesh->edge_count=unique;mesh->edge_first=mesh->source.count*3;
    return 0;
}
static int rf_weaver_gpu_marker_position_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_gpu_point *a=pa,*b=pb;
    int result=rf_weaver_gpu_point_compare(a->position,b->position);
    if(result)return result;
    if(a->face!=b->face)return a->face<b->face?-1:1;
    return a->corner<b->corner?-1:a->corner>b->corner;
}
static int rf_weaver_gpu_marker_growth_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_gpu_point *a=pa,*b=pb;
    if(a->threshold!=b->threshold)return a->threshold<b->threshold?-1:1;
    return rf_weaver_gpu_point_compare(a->position,b->position);
}
static int rf_weaver_gpu_points_create(struct rf_weaver_gpu_mesh *mesh)
{
    unsigned count=mesh->source.count*3;
    struct rf_weaver_gpu_point *points=malloc((size_t)count*sizeof(*points));
    if(!points)return -1;
    for(unsigned i=0;i<count;++i) {
        struct rf_weaver_gpu_point *p=&points[i];
        p->face=i/3;p->corner=i%3;
        const struct rf_weaver_face *face=&mesh->source.faces[p->face];
        memcpy(p->position,face->vertex[p->corner].position,sizeof(p->position));
        p->threshold=face->threshold;
    }
    qsort(points,count,sizeof(*points),rf_weaver_gpu_marker_position_compare);
    unsigned unique=0;
    double span=mesh->source.maximum[2]-mesh->source.minimum[2];
    for(unsigned first=0;first<count;) {
        unsigned end=first+1,best=first;
        double target=span>0?(points[first].position[2]-mesh->source.minimum[2])/span:.5;
        while(end<count && !rf_weaver_gpu_point_compare(points[first].position,points[end].position)) {
            /* Prefer the incident face closest to the vertex's long-axis
             * position. Ties retain the stable material/source/corner order. */
            if(fabs(points[end].threshold-target)<fabs(points[best].threshold-target))best=end;
            ++end;
        }
        points[unique++]=points[best];first=end;
    }
    qsort(points,unique,sizeof(*points),rf_weaver_gpu_marker_growth_compare);
    mesh->point_first=mesh->edge_first+mesh->edge_count*12;
    unsigned capacity=(RF_WEAVER_GPU_VERTEX_LIMIT-mesh->point_first)/RF_WEAVER_GPU_POINT_VERTICES;
    if(capacity>RF_WEAVER_GPU_POINT_LIMIT)capacity=RF_WEAVER_GPU_POINT_LIMIT;
    if(unique>capacity) {
        /* Evenly stratified over sorted growth order, including both ends.
         * Selection is immutable and needs neither random sampling nor scans
         * in the per-frame producer. */
        for(unsigned i=0;i<capacity;++i)
            points[i]=points[capacity>1?(size_t)i*(unique-1)/(capacity-1):0];
        unique=capacity;
    }
    mesh->points=points;mesh->point_count=unique;
    return 0;
}
static void rf_weaver_gpu_bind_edge(uint32_t *bind,const struct rf_weaver_gpu_edge *edge)
{
    double direction[3],axis[3]={0,0,0},sides[2][3];
    for(unsigned k=0;k<3;++k)direction[k]=edge->point[1][k]-edge->point[0][k];
    unsigned smallest=0;
    for(unsigned k=1;k<3;++k)if(fabs(direction[k])<fabs(direction[smallest]))smallest=k;
    axis[smallest]=1;rf_weaver_gpu_cross(direction,axis,sides[0]);
    rf_weaver_gpu_normalize(sides[0],.0011*RF_WEAVER_LOCAL_UNITS);
    rf_weaver_gpu_cross(direction,sides[0],sides[1]);
    rf_weaver_gpu_normalize(sides[1],.0011*RF_WEAVER_LOCAL_UNITS);
    static const unsigned corners[6]={0,1,2,0,2,3};
    for(unsigned ribbon=0;ribbon<2;++ribbon) {
        int points[4][3];
        for(unsigned k=0;k<3;++k) {
            points[0][k]=rf_weaver_gpu_round(edge->point[0][k]+sides[ribbon][k]);
            points[1][k]=rf_weaver_gpu_round(edge->point[1][k]+sides[ribbon][k]);
            points[2][k]=rf_weaver_gpu_round(edge->point[1][k]-sides[ribbon][k]);
            points[3][k]=rf_weaver_gpu_round(edge->point[0][k]-sides[ribbon][k]);
        }
        for(unsigned triangle=0;triangle<2;++triangle) {
            struct rf_weaver_face face={0};
            for(unsigned corner=0;corner<3;++corner)
                memcpy(face.vertex[corner].position,points[corners[triangle*3+corner]],3*sizeof(int));
            rf_weaver_gpu_bind_face(bind+(ribbon*2+triangle)*66,&face,0);
        }
    }
}
static void rf_weaver_gpu_bind_point(uint32_t *bind,const struct rf_weaver_gpu_point *point)
{
    int corners[6][3];
    int radius=rf_weaver_gpu_round(.0022*RF_WEAVER_LOCAL_UNITS);
    for(unsigned i=0;i<6;++i) {
        memcpy(corners[i],point->position,sizeof(corners[i]));
        corners[i][i/2]+=(i&1)?-radius:radius;
    }
    unsigned at=0;
    for(unsigned x=0;x<2;++x)for(unsigned y=0;y<2;++y)for(unsigned z=0;z<2;++z) {
        struct rf_weaver_face face={0};
        memcpy(face.vertex[0].position,corners[x],sizeof(corners[x]));
        memcpy(face.vertex[1].position,corners[2+y],sizeof(corners[x]));
        memcpy(face.vertex[2].position,corners[4+z],sizeof(corners[x]));
        rf_weaver_gpu_bind_face(bind+at*66,&face,0);++at;
    }
}
static int rf_weaver_gpu_weapon_create(struct rf_gpu_scene_world_gpu_probe *probe,
    int weapon,const uint32_t *palette)
{
    const struct rasterfall_weapon_asset_profile *profile=rasterfall_weapon_asset_profile(weapon);
    struct rf_weaver_gpu_mesh *mesh=&probe->weaver->weapons[weapon];
    uint32_t *bind=NULL;int result=-1;
    if(!profile || rf_weaver_mesh_load(&mesh->source,profile->model_path,weapon)<0 ||
        mesh->source.count>RF_WEAVER_GPU_VERTEX_LIMIT/3)goto done;
    if(rf_weaver_gpu_edges_create(mesh)<0 || rf_weaver_gpu_points_create(mesh)<0)goto done;
    mesh->vertex_count=mesh->point_first+mesh->point_count*RF_WEAVER_GPU_POINT_VERTICES;
    bind=malloc((size_t)mesh->vertex_count*22*sizeof(*bind));if(!bind)goto done;
    for(unsigned face=0;face<mesh->source.count;++face) {
        const struct rf_weaver_face *f=&mesh->source.faces[face];
        rf_weaver_gpu_bind_face(bind+(size_t)face*66,f,0);
        if(!face || f->material!=mesh->source.faces[face-1].material) {
            if(mesh->run_count>=RF_WEAVER_MAX_MATERIALS)goto done;
            struct rf_weaver_gpu_run *run=&mesh->runs[mesh->run_count++];
            run->first=face*3;run->material=mesh->source.materials[f->material];
        }
        mesh->runs[mesh->run_count-1].count+=3;
    }
    for(unsigned i=0;i<mesh->edge_count;++i)
        rf_weaver_gpu_bind_edge(bind+(size_t)(mesh->edge_first+i*12)*22,&mesh->edges[i]);
    for(unsigned i=0;i<mesh->point_count;++i)
        rf_weaver_gpu_bind_point(bind+(size_t)(mesh->point_first+i*RF_WEAVER_GPU_POINT_VERTICES)*22,&mesh->points[i]);
    if(rf_weaver_gpu_create_resource(probe->graphics,mesh,bind,palette,15)<0)goto done;
    if(!probe->quiet)__printf("SCENE-WEAVER weapon=%d triangles=%u materials=%u preview_edges=%u topology_points=%u vertices=%u immutable_bind=1\n",weapon,mesh->source.count,mesh->run_count,mesh->edge_count,mesh->point_count,mesh->vertex_count);
    result=0;
done:
    free(bind);if(result)rf_weaver_gpu_mesh_close(probe->graphics,mesh);return result;
}
static unsigned rf_weaver_gpu_face_limit(const struct rf_weaver_gpu_mesh *mesh,
    const struct rf_weaver_gpu_run *run,double threshold)
{
    unsigned lo=run->first/3,hi=lo+run->count/3;
    while(lo<hi) {
        unsigned middle=lo+(hi-lo)/2;
        if(mesh->source.faces[middle].threshold<=threshold)lo=middle+1;else hi=middle;
    }
    return lo*3;
}
static unsigned rf_weaver_gpu_edge_limit(const struct rf_weaver_gpu_mesh *mesh,double threshold)
{
    unsigned lo=0,hi=mesh->edge_count;
    while(lo<hi) {
        unsigned middle=lo+(hi-lo)/2;
        if(mesh->edges[middle].threshold<=threshold)lo=middle+1;else hi=middle;
    }
    return lo;
}
static unsigned rf_weaver_gpu_point_limit(const struct rf_weaver_gpu_mesh *mesh,double threshold)
{
    unsigned lo=0,hi=mesh->point_count;
    while(lo<hi) {
        unsigned middle=lo+(hi-lo)/2;
        if(mesh->points[middle].threshold<=threshold)lo=middle+1;else hi=middle;
    }
    return lo;
}
static int rf_weaver_gpu_append(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_mesh_weaver_frame *frame,const struct camera *camera,
    uint32_t width,uint32_t height,struct rf_gpu_graphics_resource *resource,
    const struct rf_weaver_material *material,unsigned first,unsigned count,
    unsigned color,int alpha,float emissive,int unlit,uint32_t *draws)
{
    if(!count)return 0;
    if(*draws==UINT32_MAX || scene_batch_reserve(&probe->batch,&probe->batch_capacity,*draws+1)<0)return -1;
    struct rf_gpu_graphics_batch_item *item=&probe->batch[(*draws)++];memset(item,0,sizeof(*item));
    struct rf_gpu_graphics_draw *d=&item->draw;item->resource=resource;
    d->translation_scale[0]=frame->x;d->translation_scale[1]=frame->y;d->translation_scale[2]=frame->z;
    d->translation_scale[3]=1000;d->rotation[1]=1024;scene_draw_camera(d,camera,width,height);
    d->material[0]=color;d->material[1]=256;d->quality[1]=RF_WEAVER_LOCAL_UNITS;
    d->quality[2]=unlit?3:1;d->texture[0]=d->texture[1]=1;d->texture[2]=alpha;
    d->roughness=material->roughness<.06f?.06f:material->roughness;d->metallic=material->metallic;
    d->emissive=emissive*100.0f;d->first_index=first;d->index_count=count;d->double_sided=1;
    d->scene_layer=alpha?RF_GPU_SCENE_TRANSPARENT:RF_GPU_SCENE_WORLD;
    if(rf_gpu_graphics_resource_bind(probe->graphics,resource)<0 ||
        rf_gpu_graphics_validate_draw(probe->graphics,d)<0)return -1;
    return 0;
}
static int rf_weaver_gpu_effect_triangle(struct rf_mesh_weaver_gpu *owner,
    const double a[3],const double b[3],const double c[3],unsigned color,unsigned alpha)
{
    if(owner->effect_count>RF_WEAVER_GPU_EFFECT_VERTICES-3)return -1;
    const double *points[3]={a,b,c};
    for(unsigned corner=0;corner<3;++corner) {
        struct rf_gpu_scene_color_vertex *v=&owner->effect_vertices[owner->effect_count++];
        for(unsigned k=0;k<3;++k)v->position[k]=rf_weaver_gpu_round(points[corner][k]);
        v->rgb24=color;v->light_q8=(int)alpha;
    }
    return 0;
}
static int rf_weaver_gpu_effect_line(struct rf_mesh_weaver_gpu *owner,
    const double a[3],const double b[3],const double eye[3],double half_width,
    unsigned color,unsigned alpha)
{
    double direction[3],view[3],side[3],points[4][3];
    for(unsigned k=0;k<3;++k) {
        direction[k]=b[k]-a[k];view[k]=eye[k]-(a[k]+b[k])*.5;
    }
    rf_weaver_gpu_cross(direction,view,side);
    if(!rf_weaver_gpu_normalize(side,half_width)) {
        double axis[3]={0,1,0};rf_weaver_gpu_cross(direction,axis,side);
        if(!rf_weaver_gpu_normalize(side,half_width))return 0;
    }
    for(unsigned k=0;k<3;++k) {
        points[0][k]=a[k]+side[k];points[1][k]=b[k]+side[k];
        points[2][k]=b[k]-side[k];points[3][k]=a[k]-side[k];
    }
    if(rf_weaver_gpu_effect_triangle(owner,points[0],points[1],points[2],color,alpha)<0 ||
       rf_weaver_gpu_effect_triangle(owner,points[0],points[2],points[3],color,alpha)<0)return -1;
    return 0;
}
static int rf_weaver_gpu_effect_vertex(struct rf_mesh_weaver_gpu *owner,
    const double position[3],double radius,unsigned color,unsigned alpha)
{
    double points[6][3];
    for(unsigned i=0;i<6;++i) {
        memcpy(points[i],position,sizeof(points[i]));points[i][i/2]+=(i&1)?-radius:radius;
    }
    for(unsigned x=0;x<2;++x)for(unsigned y=0;y<2;++y)for(unsigned z=0;z<2;++z)
        if(rf_weaver_gpu_effect_triangle(owner,points[x],points[2+y],points[4+z],color,alpha)<0)return -1;
    return 0;
}
static int rf_weaver_gpu_work_light(struct rf_gpu_scene_world_gpu_probe *probe,float position[3])
{
    if(!probe || !probe->weaver || !probe->weaver->work_light_valid || !position)return 0;
    memcpy(position,probe->weaver->work_light_position,3*sizeof(float));return 1;
}
static double rf_weaver_gpu_beam_half_width(const double a[3],const double b[3],
    const double eye[3],const struct camera *camera,uint32_t width)
{
    double q[3];
    for(unsigned k=0;k<3;++k)q[k]=(a[k]+b[k])*.5-eye[k];
    double yaw_depth=(q[0]*camera->sy+q[2]*camera->cy)/1024.0;
    double depth=(q[1]*camera->pitch_sy+yaw_depth*camera->pitch_cy)/1024.0;
    double half_width=.00085*RF_WEAVER_LOCAL_UNITS;
    /* Match Scene's perspective at the beam midpoint. A ~one-pixel ribbon
     * stays a thin physical beam: no unlimited distant screen-space growth. */
    unsigned focal=width*3/4;
    if(focal && depth>0 && .5*depth/focal>half_width)half_width=.5*depth/focal;
    if(half_width>.0025*RF_WEAVER_LOCAL_UNITS)half_width=.0025*RF_WEAVER_LOCAL_UNITS;
    return half_width;
}
static int rf_weaver_gpu_effects_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_mesh_weaver_frame *frame,const struct rf_weaver_pose *pose,
    const struct camera *camera,uint32_t width,uint32_t height,uint32_t *draws)
{
    struct rf_mesh_weaver_gpu *owner=probe->weaver;
    const double local_scale=RF_WEAVER_LOCAL_UNITS/512.0;
    double eye[3]={(camera->x-frame->x)*local_scale,(camera->y-frame->y)*local_scale,
                   (camera->z-frame->z)*local_scale};
    owner->effect_count=0;owner->work_light_valid=0;
    if(frame->phase==TOY_WEAVER_CALIBRATING && frame->powered && !frame->pause_reason) {
        double phase=frame->phase_duration_ms>0?frame->phase_ms/frame->phase_duration_ms:0;
        if(phase<0)phase=0;
        if(phase>1)phase=1;
        unsigned alpha=(unsigned)(48*sin(phase*3.141592653589793));
        if(alpha)for(unsigned axis=0;axis<3;++axis)for(unsigned side=0;side<4;++side) {
            double a[3],b[3];
            unsigned other=0;
            for(unsigned k=0;k<3;++k) {
                unsigned hi=k==axis?0:((side>>other++)&1);
                a[k]=rf_mesh_weaver_manufacturing_bounds_m[hi][k]*RF_WEAVER_LOCAL_UNITS;
                b[k]=k==axis?rf_mesh_weaver_manufacturing_bounds_m[1][k]*RF_WEAVER_LOCAL_UNITS:a[k];
            }
            if(rf_weaver_gpu_effect_line(owner,a,b,eye,.0008*RF_WEAVER_LOCAL_UNITS,0x64b1bb,alpha)<0)return -1;
        }
    }
    unsigned targets=0;double average[3]={0,0,0};
    if(frame->powered && !frame->pause_reason)for(unsigned h=0;h<8;++h)if(pose->beam_mask&(1u<<h)) {
        double beam_width=rf_weaver_gpu_beam_half_width(pose->beam_start[h],pose->beam_end[h],eye,camera,width);
        if(rf_weaver_gpu_effect_line(owner,pose->beam_start[h],pose->beam_end[h],eye,
            beam_width,0x5db9c5,88)<0 ||
           rf_weaver_gpu_effect_vertex(owner,pose->beam_end[h],.0036*RF_WEAVER_LOCAL_UNITS,0xd4f1f2,235)<0)return -1;
        for(unsigned k=0;k<3;++k)average[k]+=pose->beam_end[h][k];
        ++targets;
    }
    if(targets) {
        owner->work_light_valid=1;
        const int origin[3]={frame->x,frame->y,frame->z};
        for(unsigned k=0;k<3;++k)owner->work_light_position[k]=(float)(origin[k]+average[k]/targets/local_scale);
    }
    if(!owner->effect_count)return 0;
    if(!owner->effects) {
        uint32_t indices[RF_WEAVER_GPU_EFFECT_VERTICES];
        struct rf_gpu_scene_color_vertex empty[RF_WEAVER_GPU_EFFECT_VERTICES]={0};
        for(unsigned i=0;i<RF_WEAVER_GPU_EFFECT_VERTICES;++i)indices[i]=i;
        owner->effects=rf_gpu_graphics_scene_color_resource_create(probe->graphics,
            empty,RF_WEAVER_GPU_EFFECT_VERTICES,indices,RF_WEAVER_GPU_EFFECT_VERTICES);
        if(!owner->effects)return -1;
    }
    if(rf_gpu_graphics_scene_color_resource_update(probe->graphics,owner->effects,
            owner->effect_vertices,owner->effect_count)!=0 || *draws==UINT32_MAX ||
       scene_batch_reserve(&probe->batch,&probe->batch_capacity,*draws+1)<0)return -1;
    struct rf_gpu_graphics_batch_item *item=&probe->batch[(*draws)++];memset(item,0,sizeof(*item));
    struct rf_gpu_graphics_draw *d=&item->draw;item->resource=owner->effects;
    d->translation_scale[0]=frame->x;d->translation_scale[1]=frame->y;d->translation_scale[2]=frame->z;
    d->translation_scale[3]=1000;d->rotation[1]=1024;scene_draw_camera(d,camera,width,height);
    d->material[0]=0xffffff;d->material[1]=256;d->material[3]=2;
    d->quality[1]=RF_WEAVER_LOCAL_UNITS;d->quality[2]=3;
    d->texture[0]=d->texture[1]=1;d->texture[2]=256;d->double_sided=1;
    d->index_count=owner->effect_count;d->scene_layer=RF_GPU_SCENE_EFFECTS;
    if(rf_gpu_graphics_resource_bind(probe->graphics,owner->effects)<0 ||
       rf_gpu_graphics_validate_draw(probe->graphics,d)<0)return -1;
    return 0;
}
static int rf_weaver_gpu_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item **items,uint32_t *draws)
{
    if(!probe)return 0;
    if(probe->weaver)probe->weaver->work_light_valid=0;
    if(!probe->layers || !probe->layers->weaver.present)return 0;
    const struct rf_mesh_weaver_frame *frame=&probe->layers->weaver;
    if(!probe->weaver)probe->weaver=calloc(1,sizeof(*probe->weaver));
    if(!probe->weaver)return -1;
    probe->batch=*items;
    struct rf_weaver_gpu_mesh *machine=&probe->weaver->machine,*weapon=NULL;
    struct rf_weaver_pose pose;uint32_t machine_palette[RF_WEAVER_GPU_MACHINE_BONES*15],gun_palette[15];
    /* The current full gun is also the pose sampler's immutable topology. */
    if(frame->weapon>=0 && frame->weapon<TOY_GAME_WEAPON_COUNT)weapon=&probe->weaver->weapons[frame->weapon];
    rf_weaver_pose_sample(frame,weapon && weapon->source.faces?&weapon->source:NULL,&pose);
    rf_weaver_gpu_palette(pose.bones,RF_WEAVER_GPU_MACHINE_BONES,machine_palette);
    rf_weaver_gpu_palette(&pose.bones[41],1,gun_palette);
    if(!machine->resource && rf_weaver_gpu_machine_create(probe,machine_palette)<0)goto fail;
    if(weapon && !weapon->resource) {
        if(rf_weaver_gpu_weapon_create(probe,frame->weapon,gun_palette)<0)goto fail;
        rf_weaver_pose_sample(frame,&weapon->source,&pose);
        rf_weaver_gpu_palette(pose.bones,RF_WEAVER_GPU_MACHINE_BONES,machine_palette);
        rf_weaver_gpu_palette(&pose.bones[41],1,gun_palette);
    }
    const char *legacy_skin=getenv("RF_GPU_SCENE_LEGACY_SKIN_BATCH");
    const char *legacy_upload=getenv("RF_GPU_SKIN_LEGACY_UPLOAD");
    int batch=!(legacy_skin && legacy_skin[0]=='1') && !(legacy_upload && legacy_upload[0]=='1');
    if(batch && rf_gpu_graphics_skin_batch_begin(probe->graphics)<0)goto fail;
    if(rf_gpu_graphics_skinned_resource_update(probe->graphics,machine->resource,machine->vertex_count,
            NULL,0,machine_palette,RF_WEAVER_GPU_MACHINE_BONES*15)!=0 ||
       (weapon && rf_gpu_graphics_skinned_resource_update(probe->graphics,weapon->resource,weapon->vertex_count,
            NULL,0,gun_palette,15)!=0)) {
        if(batch)rf_gpu_graphics_skin_batch_cancel(probe->graphics);
        goto fail;
    }
    if(batch && rf_gpu_graphics_skin_batch_end(probe->graphics)<0)goto fail;
    for(unsigned i=0;i<machine->run_count;++i) {
        const struct rf_weaver_gpu_run *run=&machine->runs[i];
        float emission=run->optical && frame->powered ? (float)(.10+.35*pose.opening):0;
        if(rf_weaver_gpu_append(probe,frame,camera,width,height,machine->resource,&run->material,
            run->first,run->count,run->material.color,0,emission,0,draws)<0)goto fail;
    }
    if(weapon && frame->phase>=TOY_WEAVER_CALIBRATING && frame->phase<=TOY_WEAVER_READY) {
        int finished=frame->phase==TOY_WEAVER_DELIVERING || frame->phase==TOY_WEAVER_READY;
        double solid=finished?2:frame->phase==TOY_WEAVER_WEAVING?pose.reveal-RF_WEAVER_ACTIVE_BEHIND:-1;
        double front=finished?2:frame->phase==TOY_WEAVER_WEAVING?pose.reveal+RF_WEAVER_ACTIVE_AHEAD:-1;
        for(unsigned i=0;i<weapon->run_count;++i) {
            const struct rf_weaver_gpu_run *run=&weapon->runs[i];
            unsigned solid_end=rf_weaver_gpu_face_limit(weapon,run,solid);
            unsigned active_end=rf_weaver_gpu_face_limit(weapon,run,front);
            if(rf_weaver_gpu_append(probe,frame,camera,width,height,weapon->resource,&run->material,
                run->first,solid_end-run->first,run->material.color,0,0,0,draws)<0)goto fail;
            if(rf_weaver_gpu_append(probe,frame,camera,width,height,weapon->resource,&run->material,
                solid_end,active_end-solid_end,frame->pause_reason?0xd3a45d:0x60c1c8,100,.35f,0,draws)<0)goto fail;
        }
        if(!finished && weapon->edge_count) {
            const struct rf_weaver_material preview={0x4a929f,.8f,0};
            unsigned active_start=rf_weaver_gpu_edge_limit(weapon,solid);
            unsigned preview_start=rf_weaver_gpu_edge_limit(weapon,front);
            int preview_alpha=frame->pause_reason?25:frame->phase==TOY_WEAVER_CALIBRATING?46:34;
            if(rf_weaver_gpu_append(probe,frame,camera,width,height,weapon->resource,&preview,
                weapon->edge_first+preview_start*12,(weapon->edge_count-preview_start)*12,
                preview.color,preview_alpha,0,1,draws)<0)goto fail;
            if(rf_weaver_gpu_append(probe,frame,camera,width,height,weapon->resource,&preview,
                weapon->edge_first+active_start*12,(preview_start-active_start)*12,
                frame->pause_reason?0xc99652:0x88d7dd,frame->pause_reason?68:135,0,1,draws)<0)goto fail;
        }
        if(!finished && weapon->point_count) {
            const struct rf_weaver_material point_material={0xb1e4e8,.8f,0};
            unsigned start=rf_weaver_gpu_point_limit(weapon,solid);
            unsigned end=rf_weaver_gpu_point_limit(weapon,front);
            if(rf_weaver_gpu_append(probe,frame,camera,width,height,weapon->resource,&point_material,
                weapon->point_first+start*RF_WEAVER_GPU_POINT_VERTICES,
                (end-start)*RF_WEAVER_GPU_POINT_VERTICES,
                frame->pause_reason?0xe2b76b:point_material.color,frame->pause_reason?100:190,0,1,draws)<0)goto fail;
        }
    }
    if(rf_weaver_gpu_effects_prepare(probe,frame,&pose,camera,width,height,draws)<0)goto fail;
    *items=probe->batch;return 0;
fail:
    *items=probe->batch;return -1;
}
