/* Block keeps rigid, flat-shaded pieces, but pose, joints and sockets now use
 * the standard RFCHAR instance and RFANIM pipeline. This serial scratch owner
 * resets on every sample: frozen Scene replay never inherits another actor's IK. */
static struct rasterfall_model_resource block_body_resource;
static struct rasterfall_model_instance block_body_instance;
static struct vec3 *block_body_vertices;

static int block_body_ready(void)
{
    if (block_body_instance.resource) return 0;
    if (!block_body_resource.definition.bone_count &&
        rasterfall_model_resource_load(&block_body_resource,
            rasterfall_character_body_resource_path(RASTERFALL_BODY_RF_BLOCK))<0) return -1;
    if (!block_body_vertices)
        block_body_vertices=tlibc_malloc(block_body_resource.definition.vertex_count*sizeof(*block_body_vertices));
    if (!block_body_vertices) return -1;
    return rasterfall_model_instance_init(&block_body_instance,&block_body_resource);
}

static void block_role_add(struct rasterfall_model_asset *pose,int role,int x,int y,int z)
{
    int bone=rasterfall_model_humanoid_bone(pose,role);
    if (bone<0) return;
    pose->bones[bone].rotate_x+=x;
    pose->bones[bone].rotate_y+=y;
    pose->bones[bone].rotate_z+=z;
}

static int block_character_sample(const struct rasterfall_procedural_humanoid_state *state,
    struct rasterfall_model_instance *instance,struct rasterfall_rigid_transform *world)
{
    struct rasterfall_action_composition composition={0};
    struct rasterfall_actor_pose legacy;
    struct rasterfall_rifle_pose_input rifle=state->rifle;
    int action=state->animation_id,time=state->animation_time_ms;
    int walk=state->moving || action==TOY_GAME_ANIM_MOVE;
    int fallen=state->downed || action==TOY_GAME_ANIM_DOWNED || action==TOY_GAME_ANIM_DEATH ||
        action==TOY_GAME_ANIM_REVIVE;
    int duration=action==TOY_GAME_ANIM_RELOAD && state->weapon>=0 ?
        toy_game_weapon_info(state->weapon)->reload_ms : toy_game_animation_info(action)->duration_ms;
    composition.layers[0].clip=humanoid_action(walk && !fallen ?
        RASTERFALL_ACTION_LOCOMOTION_WALK:RASTERFALL_ACTION_LOCOMOTION_IDLE);
    composition.layers[0].time_ms=state->moving ? state->locomotion_time_ms : time;
    if (state->weapon>=0) {
        composition.layers[1].clip=humanoid_action(state->rifle.aim_milli || action==TOY_GAME_ANIM_FIRE ?
            RASTERFALL_ACTION_RIFLE_AIM:RASTERFALL_ACTION_RIFLE_IDLE);
        composition.layers[1].time_ms=time;
    }
    if (!composition.layers[0].clip || (state->weapon>=0 && !composition.layers[1].clip) ||
        rasterfall_action_compose(instance,&composition)<0) return -1;
    struct rasterfall_model_asset *pose=rasterfall_model_instance_pose(instance);
    if (state->weapon>=TOY_GAME_WEAPON_PISTOL && state->weapon<=TOY_GAME_WEAPON_AWP) {
        if (!state->rifle_valid) {
            memset(&rifle,0,sizeof(rifle));
            rifle.aim_milli=walk?0:1000;rifle.hip_milli=walk?1000:0;
            rifle.pitch_mdeg=state->aim_pitch_mdeg;rifle.yaw_mdeg=state->aim_yaw_mdeg;
            rifle.recoil_milli=action==TOY_GAME_ANIM_FIRE ? rasterfall_rifle_recoil_sample(state->weapon,time) : 0;
        }
        if (rasterfall_rifle_pose_solve(instance,state->weapon,835,&rifle,NULL)<0) return -1;
    } else if (state->weapon<0) {
        block_role_add(pose,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,0,0,-85);
        block_role_add(pose,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,0,0,85);
    }
    /* Existing gameplay actions remain visible as local rotational overlays.
     * No target-driven resizing, rest edits or root-motion writes are allowed. */
    rasterfall_actor_animation_sample(action,time,duration,&legacy);
    block_role_add(pose,RASTERFALL_HUMANOID_CHEST,legacy.body_pitch,0,0);
    if (action==TOY_GAME_ANIM_RELOAD) {
        int wave=legacy.weapon_pitch*45/RASTERFALL_RELOAD_WEAPON_PITCH;
        block_role_add(pose,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,-wave,0,0);
        block_role_add(pose,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,wave,0,0);
        block_role_add(pose,RASTERFALL_HUMANOID_LEFT_FOREARM,-wave*2,0,0);
    } else if (action==TOY_GAME_ANIM_SHOVE) {
        block_role_add(pose,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,-35,legacy.left_arm_rotation,0);
        block_role_add(pose,RASTERFALL_HUMANOID_LEFT_FOREARM,0,35,0);
    } else if (action==TOY_GAME_ANIM_MELEE || action==TOY_GAME_ANIM_THROW) {
        block_role_add(pose,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,legacy.right_upper_pitch+60,0,0);
        block_role_add(pose,RASTERFALL_HUMANOID_RIGHT_FOREARM,legacy.right_forearm_pitch+30,0,0);
    }
    if (rasterfall_model_instance_update_bones(instance)<0) return -1;
    modular_rigid_identity(world);
    double yaw=state->aim_yaw_mdeg*M_PI/180000.0;
    double sy=(state->sy*cos(yaw)+state->cy*sin(yaw))/1024.0;
    double cy=(state->cy*cos(yaw)-state->sy*sin(yaw))/1024.0;
    world->rotation[0]=world->rotation[8]=cy;
    world->rotation[2]=sy;world->rotation[6]=-sy;
    world->scale_milli=835;
    world->translation[0]=state->x+sy*legacy.forward_shift;
    world->translation[1]=RASTERFALL_WORLD_GROUND_Y+state->lift;
    world->translation[2]=state->z+cy*legacy.forward_shift;
    if (fallen) {
        int progress=1000;
        if (action==TOY_GAME_ANIM_DEATH || action==TOY_GAME_ANIM_REVIVE) {
            progress=duration>0 ? time*1000/duration : 1000;
            if (progress>1000) progress=1000;
            if (action==TOY_GAME_ANIM_REVIVE) progress=1000-progress;
        }
        double angle=-progress*M_PI/2000.0;
        double fall[9]={1,0,0,0,cos(angle),-sin(angle),0,sin(angle),cos(angle)};
        double rotated[9];
        rigid_matrix_multiply(world->rotation,fall,rotated);
        memcpy(world->rotation,rotated,sizeof(rotated));
        world->translation[1]+=progress*95/1000.0;
    }
    return 0;
}

typedef int (*block_box_fn)(void *,int,int,int,int,int,int,int,uint32_t);
static const int block_box_faces[]={0,2,3,0,3,1,4,5,7,4,7,6,0,1,5,0,5,4,
    2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
static int block_equipment_bind(const struct rasterfall_model_asset *model,
    int head,int x0,int x1,int y0,int y1,int z0,int z1,double points[8][3])
{
    int role=head?RASTERFALL_HUMANOID_HEAD:
        y1<-400?RASTERFALL_HUMANOID_HIPS:RASTERFALL_HUMANOID_CHEST;
    int bone=rasterfall_model_humanoid_bone(model,role);
    if(bone<0)return -1;
    for (int i=0;i<8;++i) {
        double ly=(i&2)?y1:y0;
        points[i][0]=-((i&1)?x1:x0)*.0018*512;
        points[i][1]=(head ? 1.72+ly*.0018 : .88+(ly+620)*.68/520)*512;
        points[i][2]=((i&4)?z1:z0)*.0016*512;
    }
    return bone;
}
struct block_equipment_draw {
    struct toy_renderer *renderer;
    const struct camera *camera;
    const struct rasterfall_model_instance *instance;
    const struct rasterfall_rigid_transform *world;
};
static int block_equipment_box(void *opaque,
    int head,int x0,int x1,int y0,int y1,int z0,int z1,uint32_t color)
{
    const struct block_equipment_draw *draw=opaque;
    const struct rasterfall_model_asset *pose=rasterfall_model_instance_final_pose(draw->instance);
    double bind[8][3];struct vec3 points[8];int pixels=0;
    int bone=block_equipment_bind(pose,head,x0,x1,y0,y1,z0,z1,bind);
    if(bone<0)return -1;
    const struct rasterfall_model_bone_transform *transform=&pose->bone_transforms[bone];
    for(int i=0;i<8;++i) {
        double rest[3]={pose->bones[bone].rest_x,pose->bones[bone].rest_y,pose->bones[bone].rest_z};
        double local[3],out[3];
        for(int k=0;k<3;++k)local[k]=bind[i][k]-rest[k];
        rigid_matrix_vector(transform->rotation,local,out);
        for(int k=0;k<3;++k)out[k]+=transform->position[k];
        modular_weapon_world_point(draw->world,out,&points[i]);
    }
    for(unsigned i=0;i<sizeof(block_box_faces)/sizeof(block_box_faces[0]);i+=3)
        pixels+=draw_world_triangle(draw->renderer,draw->camera,&points[block_box_faces[i]],
            &points[block_box_faces[i+1]],&points[block_box_faces[i+2]],color);
    return pixels;
}

/* Non-firearm assets have no authored grip contract yet. Keep their legacy basis
 * and size, but anchor their geometry to the finalized right-hand socket. */
static struct rasterfall_model_asset *block_weapon_model(int weapon)
{
    static struct rasterfall_model_asset models[TOY_GAME_WEAPON_COUNT];
    const char *path=rasterfall_weapon_model_path(weapon);
    if (!path) return NULL;
    if (!models[weapon].data && rasterfall_model_load(&models[weapon],path)<0) return NULL;
    return &models[weapon];
}

static int render_block_other_weapon(struct toy_renderer *renderer,const struct camera *camera,
    const struct rasterfall_model_instance *instance,const struct rasterfall_rigid_transform *world,
    int weapon,int muzzle_flash)
{
    struct rasterfall_model_asset *model=block_weapon_model(weapon);
    struct rasterfall_model_attachment_transform hand;
    struct rasterfall_weapon_socket_transform primary;
    double grip[9],inverse[9],rotation[9];
    if (!rasterfall_weapon_model_path(weapon)) return 0;
    if (!model || rasterfall_model_instance_attachment_transform(instance,RASTERFALL_ATTACHMENT_WEAPON_R,&hand)<0 ||
        rasterfall_weapon_socket_transform(TOY_GAME_WEAPON_AK,RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP,&primary)<0) return -1;
    modular_weapon_quaternion_matrix(primary.rotation,grip);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)inverse[i*3+j]=grip[j*3+i];
    rigid_matrix_multiply(hand.rotation,inverse,rotation);
    int length=model->max_x-model->min_x,pixels=0;
    if(model->max_y-model->min_y>length)length=model->max_y-model->min_y;
    if(model->max_z-model->min_z>length)length=model->max_z-model->min_z;
    if(length<=0)return -1;
    double scale=(weapon==TOY_GAME_WEAPON_AWP?920:weapon==TOY_GAME_WEAPON_SHOTGUN?760:380)*
        ((double)RASTERFALL_HUMAN_HEIGHT_RFU/RASTERFALL_LEGACY_ACTOR_HEIGHT_RFU)*1000/world->scale_milli/length;
    for(unsigned i=0;i<model->primitive_count;++i) {
        const unsigned char *primitive=model->primitives+i*RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices=model->indices+model_u32(primitive)*4;
        unsigned count=model_u32(primitive+4),material=model_u32(primitive+8);
        uint32_t color=model_u32(model->materials+material*model->material_bytes);
        for(unsigned j=0;j+2<count;j+=3) {
            struct vec3 v[3];
            for(int k=0;k<3;++k) {
                unsigned id=model_u32(indices+(j+k)*4);
                const int *p=(const int *)(model->vertices+id*model->vertex_bytes);
                double local[3]={p[0]-(model->min_x+model->max_x)*.5,
                    p[1]-(model->min_y+model->max_y)*.5,p[2]-(model->min_z+model->max_z)*.5},out[3];
                if(weapon==TOY_GAME_WEAPON_PISTOL)local[2]=p[2]-model->min_z;
                if(weapon==TOY_GAME_WEAPON_SHOTGUN) {
                    local[0]=p[2]-(model->min_z+model->max_z)*.5;local[2]=p[0]-model->min_x;
                } else if(weapon==TOY_GAME_WEAPON_AWP) { local[0]=-local[0];local[2]=-local[2]; }
                for(int n=0;n<3;++n)local[n]*=scale;
                rigid_matrix_vector(rotation,local,out);
                for(int n=0;n<3;++n)out[n]+=hand.position[n]*512.0/instance->pose.position_scale;
                modular_weapon_world_point(world,out,&v[k]);
            }
            pixels+=draw_world_triangle(renderer,camera,&v[0],&v[1],&v[2],color);
        }
    }
    if(muzzle_flash>0 && weapon<=TOY_GAME_WEAPON_AWP) {
        double local[3]={0,15,length*scale*.5},out[3];struct vec3 muzzle;
        rigid_matrix_vector(rotation,local,out);
        for(int k=0;k<3;++k)out[k]+=hand.position[k]*512.0/instance->pose.position_scale;
        modular_weapon_world_point(world,out,&muzzle);
        int saved=active_scene_light_override_q8;active_scene_light_override_q8=-1;
        pixels+=draw_cuboid(renderer,camera,muzzle.x-32,muzzle.x+32,
            muzzle.y-32,muzzle.y+32,muzzle.z-32,muzzle.z+32,0xE7C058);
        active_scene_light_override_q8=saved;
    }
    return pixels;
}

static int render_block_body(struct toy_renderer *renderer,const struct camera *camera,
    const struct rasterfall_model_instance *instance,const struct rasterfall_rigid_transform *world,
    const struct rasterfall_character_profile *character)
{
    const struct rasterfall_model_asset *model=rasterfall_model_instance_final_pose(instance);
    int pixels=0;
    for(unsigned i=0;i<model->vertex_count;++i) {
        int p[3],normal[3];double local[3];
        if(rasterfall_model_instance_skin_vertex(instance,i,p,normal)<0)return -1;
        for(int k=0;k<3;++k)local[k]=p[k]*512.0/model->position_scale;
        modular_weapon_world_point(world,local,&block_body_vertices[i]);
    }
    for(unsigned i=0;i<model->primitive_count;++i) {
        const unsigned char *primitive=model->primitives+i*RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices=model->indices+model_u32(primitive)*4;
        unsigned count=model_u32(primitive+4),material=model_u32(primitive+8);
        uint32_t tag=model_u32(model->materials+material*model->material_bytes);
        uint32_t color=tag==0xd94f70?character->body_color:tag==0x25354a?character->leg_color:
            tag==0xf0c3a5?character->skin_color:character->hair_color;
        for(unsigned j=0;j+2<count;j+=3)
            pixels+=draw_world_triangle(renderer,camera,&block_body_vertices[model_u32(indices+j*4)],
                &block_body_vertices[model_u32(indices+(j+1)*4)],&block_body_vertices[model_u32(indices+(j+2)*4)],color);
    }
    return pixels;
}
