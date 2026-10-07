/* Infected adapter. Sampling is serial and resets its scratch instance; only
 * the copied final palette leaves this module. No per-vertex work on a hit. */
#include "rf_gpu_character.h"
int rf_gpu_scene_infected_supported(const struct rf_gpu_scene_enemy_item_v1 *v)
{
    return v && v->infected_recipe>=1 && v->infected_recipe<=6 &&
        enemy_visual_recipes[v->infected_recipe-1].type==v->type &&
        v->source_slot<RF_GPU_SCENE_ENEMY_CAPACITY && !v->legacy_kind &&
        !v->shadow && !v->tongue && v->infected_swing>=-24 && v->infected_swing<=24 &&
        (v->infected_bind==0 || v->infected_bind==1) &&
        v->dir_x>=-1024 && v->dir_x<=1024 && v->dir_z>=-1024 && v->dir_z<=1024 &&
        v->x>=-1000000 && v->x<=1000000 && v->z>=-1000000 && v->z<=1000000 &&
        v->lift>=-1000000 && v->lift<=1000000 && v->squash>=0 && v->squash<=2000 &&
        v->alpha>=0 && v->alpha<=255 && v->scene_light_q8>=0 && v->scene_light_q8<=384 &&
        v->roll_sin>=-1024 && v->roll_sin<=1024 && v->roll_cos>=-1024 && v->roll_cos<=1024 &&
        (!v->transformed || (v->pivot_x==v->x && v->pivot_z==v->z &&
            v->pivot_y>=-1000000 && v->pivot_y<=1000000));
}
int rf_gpu_scene_infected_bind(const struct rf_gpu_scene_enemy_item_v1 *item,
    rf_gpu_character_triangle_fn emit,void *context)
{
    if(!emit || !rf_gpu_scene_infected_supported(item) || enemy_visual_load(item->infected_recipe-1)<0)return -1;
    const struct rasterfall_model_asset *m=&enemy_visual_cache[item->infected_recipe-1].resource.definition;
    if(!m->skinning_enabled || !m->bone_count || m->bone_count>RF_GPU_CHARACTER_BONES ||
        m->index_count/3>RF_GPU_SCENE_ENEMY_MAX_TRIANGLES || m->material_bytes<40)return -1;
    for(unsigned p=0;p<m->primitive_count;++p) {
        const unsigned char *primitive=m->primitives+p*16;
        unsigned first=model_u32(primitive),count=model_u32(primitive+4),mi=model_u32(primitive+8);
        if(mi>=m->material_count || first>m->index_count || count>m->index_count-first || count%3)return -1;
        const unsigned char *mat=m->materials+mi*m->material_bytes;
        struct rasterfall_character_render_policy policy=character_render_policy(authored_visual_class(m,mi));
        if(mat[4]!=255 || mat[6] || (mat[7]&~1u) || model_u32(mat+8)!=UINT_MAX ||
            model_u32(mat+12) || model_u32(mat+16) || model_u32(mat+20) ||
            model_u32(mat+24) || model_u32(mat+28) || model_u32(mat+32))return -1;
        for(unsigned i=first;i<first+count;i+=3) {
            struct rf_gpu_character_vertex v[3]={0};
            for(unsigned k=0;k<3;++k) {
                unsigned id=model_u32(m->indices+(i+k)*4);
                if(id>=m->vertex_count)return -1;
                memcpy(v[k].position,m->vertices+id*m->vertex_bytes,12);
                const unsigned char *skin=m->skin_vertices+id*8;
                v[k].bone0=skin[0]|(unsigned)skin[1]<<8;
                v[k].bone1=skin[2]|(unsigned)skin[3]<<8;
                v[k].weight=skin[4]|(unsigned)skin[5]<<8;v[k].type=skin[6];
                if(!v[k].type)v[k].bone1=v[k].bone0;
                if(v[k].type>1 || v[k].bone0>=m->bone_count || v[k].bone1>=m->bone_count)return -1;
                for(unsigned a=0;a<3;++a) {
                    const unsigned char *n=m->vertices+id*m->vertex_bytes+12+a*2;
                    v[k].normal[a]=(short)(n[0]|(unsigned)n[1]<<8);
                }
            }
            if(emit(context,v,model_u32(mat)&0xffffffu,
                RF_GPU_CHARACTER_NORMALS|((mat[7]&1)?RF_GPU_CHARACTER_DOUBLE_SIDED:0)|
                (!policy.lighting_min_q8?RF_GPU_CHARACTER_FORM_LIGHT:0))<0)return -1;
        }
    }
    return 0;
}
int rf_gpu_scene_infected_sample(const struct rf_gpu_scene_enemy_item_v1 *item,
    struct rf_gpu_character_pose *out)
{
    if(!out || !rf_gpu_scene_infected_supported(item) || enemy_visual_load(item->infected_recipe-1)<0)return -1;
    int recipe=item->infected_recipe-1;
    if(!enemy_visual_cache[recipe].scene_status) {
        if(rasterfall_model_instance_init(&enemy_visual_cache[recipe].scene_instance,
            &enemy_visual_cache[recipe].resource)<0)return -1;
        enemy_visual_cache[recipe].scene_status=1;
    }
    if(enemy_visual_cache[recipe].scene_status<0)return -1;
    struct rasterfall_model_instance *instance=&enemy_visual_cache[recipe].scene_instance;
    struct rasterfall_model_asset *m=rasterfall_model_instance_pose(instance);
    struct rf_gpu_character_pose pose={0};
    if(!m || !m->position_scale || m->bone_count>RF_GPU_CHARACTER_BONES ||
        rasterfall_model_instance_reset_pose(instance)<0)return -1;
    if(!item->infected_bind)enemy_visual_apply_pose(m,item->type,item->infected_swing);
    if(rasterfall_model_instance_update_bones(instance)<0 ||
        rasterfall_model_build_skin_palette(m,pose.palette,m->bone_count)<0)return -1;
    pose.bone_count=m->bone_count;
    pose.origin[0]=item->x;pose.origin[1]=item->lift;pose.origin[2]=item->z;
    double scale=(512000/m->position_scale)/1000.0;
    if(scale<.001 || scale>8)return -1;
    double yaw[9]={item->dir_z/1024.0*scale,0,item->dir_x/1024.0*scale,
        0,scale*item->squash/1000.0,0,-item->dir_x/1024.0*scale,0,item->dir_z/1024.0*scale};
    double roll[9]={1,0,0,0,1,0,0,0,1},world[9],offset[3]={0,-900-m->min_y*yaw[4],0};
    if(item->transformed) {
        roll[0]=roll[4]=item->roll_cos/1024.0;
        roll[1]=-item->roll_sin/1024.0;roll[3]=item->roll_sin/1024.0;
        double pivot=item->pivot_y-item->lift,y=offset[1]-pivot;
        offset[0]=roll[1]*y;offset[1]=pivot+roll[4]*y;
    }
    rigid_matrix_multiply(roll,yaw,world);
    for(unsigned i=0;i<pose.bone_count;++i) {
        struct rasterfall_model_skin_palette_bone *b=&pose.palette[i];
        double r[9],p[3];
        rigid_matrix_multiply(world,b->rotation,r);rigid_matrix_vector(world,b->position,p);
        memcpy(b->rotation,r,sizeof(r));
        for(unsigned k=0;k<3;++k)b->position[k]=p[k]+offset[k];
    }
    *out=pose;return 0;
}
