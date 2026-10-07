/* Standard Block topology, sampled by the same producer as CPU presentation.
 * Bind vertices are generated only on appearance changes; animation emits a
 * bounded palette. All output positions are relative to the frozen origin. */
#include "rf_gpu_scene_block.h"

static int scene_block_valid(const struct rf_gpu_scene_procedural_item_v1 *item)
{
    return item && !item->target_half_width && !item->target_half_height &&
        !item->vertex_lighting && item->double_sided>=0 && item->double_sided<=1 &&
        item->state.x>=-1000000 && item->state.x<=1000000 &&
        item->state.z>=-1000000 && item->state.z<=1000000 &&
        item->state.lift>=-1000000 && item->state.lift<=1000000 &&
        item->state.sy>=-1024 && item->state.sy<=1024 &&
        item->state.cy>=-1024 && item->state.cy<=1024 &&
        item->state.animation_id>=0 && item->state.animation_id<TOY_GAME_ANIM_COUNT &&
        item->state.animation_time_ms>=0 && item->state.animation_time_ms<=60000 &&
        item->state.locomotion_time_ms>=0 && item->state.locomotion_time_ms<=60000 &&
        item->state.aim_pitch_mdeg>=-75000 && item->state.aim_pitch_mdeg<=75000 &&
        item->state.aim_yaw_mdeg>=-50000 && item->state.aim_yaw_mdeg<=50000 &&
        item->state.moving>=0 && item->state.moving<=1 &&
        item->state.profession_id>=0 && item->state.profession_id<RASTERFALL_PROFESSION_COUNT &&
        item->state.weapon>=-1 && item->state.weapon<TOY_GAME_WEAPON_COUNT &&
        item->scene_light_q8>=0 && item->scene_light_q8<=384;
}
int rf_gpu_scene_block_supported(const struct rf_gpu_scene_procedural_item_v1 *item)
{ return scene_block_valid(item); }

struct scene_block_build {
    rf_gpu_scene_block_triangle_fn emit;
    void *context;
    const struct rasterfall_model_asset *body;
};
static int scene_block_equipment(void *opaque,int head,int x0,int x1,int y0,int y1,
    int z0,int z1,uint32_t color)
{
    struct scene_block_build *build=opaque;
    double points[8][3];
    int bone=block_equipment_bind(build->body,head,x0,x1,y0,y1,z0,z1,points);
    if(bone<0)return -1;
    for(unsigned i=0;i<sizeof(block_box_faces)/sizeof(block_box_faces[0]);i+=3) {
        struct rf_gpu_scene_block_vertex v[3]={0};
        for(int k=0;k<3;++k) {
            for(int axis=0;axis<3;++axis) {
                double value=points[block_box_faces[i+k]][axis];
                v[k].position[axis]=(int)(value<0?value-.5:value+.5);
            }
            v[k].bone0=v[k].bone1=bone;v[k].weight=65535;
        }
        if(build->emit(build->context,v,color,0)<0)return -1;
    }
    return 0;
}
static int scene_block_flash_triangle(void *opaque,const struct rf_gpu_scene_enemy_point *a,
    const struct rf_gpu_scene_enemy_point *b,const struct rf_gpu_scene_enemy_point *c,unsigned color)
{
    struct scene_block_build *build=opaque;
    const struct rf_gpu_scene_enemy_point *points[3]={a,b,c};
    struct rf_gpu_scene_block_vertex v[3]={0};
    for(int k=0;k<3;++k) {
        v[k].position[0]=points[k]->x;v[k].position[1]=points[k]->y;v[k].position[2]=points[k]->z;
        v[k].bone0=v[k].bone1=build->body->bone_count+1;v[k].weight=65535;
    }
    return build->emit(build->context,v,color,1);
}
int rf_gpu_scene_block_bind(const struct rf_gpu_scene_procedural_item_v1 *item,
    rf_gpu_scene_block_triangle_fn emit,void *context)
{
    if(!scene_block_valid(item) || !emit || block_body_ready()<0)return -1;
    const struct rasterfall_model_asset *body=&block_body_resource.definition;
    /* The authored Block and its legacy equipment share 512-unit bind space. */
    if(body->position_scale!=512 || body->bone_count>RF_GPU_SCENE_POSE_BONES)return -1;
    struct scene_block_build build={emit,context,body};
    for(int part=0;part<2;++part) {
        const struct rasterfall_model_asset *m=part?block_weapon_model(item->state.weapon):body;
        if(!m) { if(part && !rasterfall_weapon_model_path(item->state.weapon))continue;return -1; }
        for(unsigned p=0;p<m->primitive_count;++p) {
            const unsigned char *primitive=m->primitives+p*RASTERFALL_MODEL_PRIMITIVE_BYTES;
            unsigned first=model_u32(primitive),count=model_u32(primitive+4),mi=model_u32(primitive+8);
            unsigned color=model_u32(m->materials+mi*m->material_bytes);
            if(!part)color=color==0xd94f70?item->body_color:color==0x25354a?item->leg_color:
                color==0xf0c3a5?item->skin_color:item->hair_color;
            for(unsigned i=0;i<count;i+=3) {
                struct rf_gpu_scene_block_vertex v[3]={0};
                for(int k=0;k<3;++k) {
                    unsigned id=model_u32(m->indices+(first+i+k)*4);
                    memcpy(v[k].position,m->vertices+id*m->vertex_bytes,12);
                    v[k].bone0=v[k].bone1=body->bone_count;v[k].weight=65535;
                    if(!part) {
                        const unsigned char *skin=m->skin_vertices+id*8;
                        v[k].bone0=skin[0]|(unsigned)skin[1]<<8;
                        v[k].bone1=skin[2]|(unsigned)skin[3]<<8;
                        v[k].weight=skin[4]|(unsigned)skin[5]<<8;v[k].type=skin[6];
                        if(!v[k].type)v[k].bone1=v[k].bone0;
                    }
                }
                if(emit(context,v,color,0)<0)return -1;
            }
        }
    }
    /* Keep the tiny flash in immutable topology, suppress its draw when off.
     * Reuse the ordinary cuboid's face colors and winding. */
    if(item->state.weapon>=TOY_GAME_WEAPON_PISTOL && item->state.weapon<=TOY_GAME_WEAPON_AWP) {
        if(scene_procedural_emit)return -1;
        struct toy_renderer renderer={0};struct camera camera={0};
        scene_procedural_emit=scene_block_flash_triangle;scene_procedural_context=&build;
        scene_procedural_failed=0;
        int result=draw_cuboid(&renderer,&camera,-32,32,-32,32,-32,32,0xE7C058);
        int failed=scene_procedural_failed;
        scene_procedural_emit=NULL;scene_procedural_context=NULL;
        if(result<0 || failed)return -1;
    }
    const struct rasterfall_profession_visual_profile *profession=
        rasterfall_profession_visual_profile(item->state.profession_id);
    return profession?block_profession_boxes(profession,scene_block_equipment,&build):0;
}

int rf_gpu_scene_block_sample(const struct rf_gpu_scene_procedural_item_v1 *item,
    struct rf_gpu_scene_block_pose *out)
{
    struct rasterfall_rigid_transform world;
    if(!out || !scene_block_valid(item) || block_body_ready()<0 ||
        block_character_sample(&item->state,&block_body_instance,&world)<0)return -1;
    const struct rasterfall_model_asset *body=rasterfall_model_instance_final_pose(&block_body_instance);
    if(body->position_scale!=512 || body->bone_count>RF_GPU_SCENE_POSE_BONES)return -1;
    memset(out,0,sizeof(*out));
    out->bone_count=body->bone_count+2;
    out->origin[0]=item->state.x;out->origin[1]=item->state.lift;out->origin[2]=item->state.z;
    if(rasterfall_model_build_skin_palette(body,out->palette,body->bone_count)<0)return -1;
    for(unsigned i=0;i<body->bone_count;++i) {
        struct rasterfall_model_skin_palette_bone *bone=&out->palette[i];
        double rotation[9],position[3];
        rigid_matrix_multiply(world.rotation,bone->rotation,rotation);
        rigid_matrix_vector(world.rotation,bone->position,position);
        for(int k=0;k<9;++k)bone->rotation[k]=rotation[k]*world.scale_milli/1000.0;
        for(int k=0;k<3;++k)bone->position[k]=world.translation[k]-out->origin[k]+
            position[k]*world.scale_milli/1000.0;
    }
    struct rasterfall_model_skin_palette_bone *weapon=&out->palette[body->bone_count];
    struct rasterfall_model_skin_palette_bone *flash=weapon+1;
    weapon->rotation[0]=weapon->rotation[4]=weapon->rotation[8]=1;
    flash->rotation[0]=flash->rotation[4]=flash->rotation[8]=1;
    if(item->state.weapon>=TOY_GAME_WEAPON_PISTOL && item->state.weapon<=TOY_GAME_WEAPON_AWP) {
        struct rf_gpu_scene_pose_v1 pose={0};pose.body_to_world=world;
        if(scene_pose_weapon_from_instance(&pose,&block_body_instance,item->state.weapon,world.scale_milli)<0)return -1;
        for(int k=0;k<9;++k)weapon->rotation[k]=pose.weapon_to_world.rotation[k]*pose.weapon_to_world.scale_milli/1000.0;
        for(int k=0;k<3;++k) {
            weapon->position[k]=pose.weapon_to_world.translation[k]-out->origin[k];
            flash->position[k]=pose.weapon_muzzle[k]-out->origin[k];
        }
        out->flash=item->state.muzzle_flash>0;
    } else if(rasterfall_weapon_model_path(item->state.weapon)) {
        const struct rasterfall_model_asset *m=block_weapon_model(item->state.weapon);
        struct rasterfall_model_attachment_transform hand;
        struct rasterfall_weapon_socket_transform primary;
        double grip[9],inverse[9],local_rotation[9],rotation[9],center[3],offset[3],position[3];
        if(!m || rasterfall_model_instance_attachment_transform(&block_body_instance,RASTERFALL_ATTACHMENT_WEAPON_R,&hand)<0 ||
            rasterfall_weapon_socket_transform(TOY_GAME_WEAPON_AK,RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP,&primary)<0)return -1;
        modular_weapon_quaternion_matrix(primary.rotation,grip);
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)inverse[i*3+j]=grip[j*3+i];
        rigid_matrix_multiply(hand.rotation,inverse,local_rotation);
        rigid_matrix_multiply(world.rotation,local_rotation,rotation);
        int length=m->max_x-m->min_x;
        if(m->max_y-m->min_y>length)length=m->max_y-m->min_y;
        if(m->max_z-m->min_z>length)length=m->max_z-m->min_z;
        if(length<=0)return -1;
        double scale=380.0*RASTERFALL_HUMAN_HEIGHT_RFU/RASTERFALL_LEGACY_ACTOR_HEIGHT_RFU/length;
        center[0]=(m->min_x+m->max_x)*.5;center[1]=(m->min_y+m->max_y)*.5;center[2]=(m->min_z+m->max_z)*.5;
        rigid_matrix_vector(rotation,center,offset);rigid_matrix_vector(world.rotation,hand.position,position);
        for(int k=0;k<9;++k)weapon->rotation[k]=rotation[k]*scale;
        for(int k=0;k<3;++k)weapon->position[k]=world.translation[k]-out->origin[k]+
            position[k]*512.0/body->position_scale*world.scale_milli/1000.0-offset[k]*scale;
    }
    return 0;
}
