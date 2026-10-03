/* Included by the character producer to reuse its resource, action, IK and
 * attachment evaluators. No game/session lookup or draw recording here. */
#include "rf_gpu_scene_pose.h"
static struct rasterfall_model_resource scene_body_resources[RASTERFALL_BODY_RESOURCE_COUNT];

void rf_gpu_scene_pose_body_release(void)
{
    for (int i=0;i<RASTERFALL_BODY_RESOURCE_COUNT;++i)
        rasterfall_model_resource_unload(&scene_body_resources[i]);
}

static int scene_pose_weapon_from_instance(struct rf_gpu_scene_pose_v1 *next,
    const struct rasterfall_model_instance *instance,int weapon_id,int scale)
{
    struct rasterfall_modular_weapon_placement weapon;
    double rotation[9],offset[3],centered[3],center[3];
    struct rasterfall_weapon_model_adapter adapter;
    if (modular_prepare_weapon_placement(instance,weapon_id,scale,&weapon)<0) return -1;
    if(rasterfall_weapon_model_adapt(weapon_id,
        (int[3]){weapon.model->min_x,weapon.model->min_y,weapon.model->min_z},
        (int[3]){weapon.model->max_x,weapon.model->max_y,weapon.model->max_z},&adapter)<0) return -1;
    rigid_matrix_multiply(next->body_to_world.rotation,weapon.rotation,rotation);
    rigid_matrix_multiply(rotation,adapter.basis,next->weapon_to_world.rotation);
    rigid_matrix_vector(next->body_to_world.rotation,weapon.origin,offset);
    for(int i=0;i<3;++i)center[i]=adapter.center[i];
    rigid_matrix_vector(next->weapon_to_world.rotation,center,centered);
    for (int i=0;i<3;++i)
        next->weapon_to_world.translation[i]=next->body_to_world.translation[i]+
            offset[i]*scale/1000.0-centered[i]*weapon.geometry_scale/1000.0;
    next->weapon_to_world.scale_milli=weapon.geometry_scale;
    next->weapon_valid=1;next->weapon=weapon_id;
    if(weapon.has_muzzle) {
        const struct rasterfall_cal_vec3 *m=&weapon.authored[RASTERFALL_WEAPON_SOCKET_MUZZLE].position;
        double local[3];struct vec3 world;
        modular_weapon_model_point(weapon.origin,weapon.rotation,
            (double[3]){m->x,m->y,m->z},scale,local);
        modular_weapon_world_point(&next->body_to_world,local,&world);
        next->weapon_muzzle[0]=world.x;next->weapon_muzzle[1]=world.y;next->weapon_muzzle[2]=world.z;
    }
    return 0;
}

static int scene_pose_body_sample(int body_id,uint64_t frame,uint64_t world,
    const struct rf_gpu_scene_body_sample *sample,int x,int y,int z,int sy,int cy,
    const char *diagnostic_model,int freeze,struct rf_gpu_scene_pose_v1 *out)
{
    struct rasterfall_model_resource *resource;
    struct rasterfall_model_instance *instance;
    struct rasterfall_action_composition composition={0};
    const char *body_path=rasterfall_character_body_resource_path(body_id);
    char path[256];
    int result=-1;
    int scale=body_id==RASTERFALL_BODY_RF_C01_V22H?1000:835;
    if (!sample || !out || !body_path || sample->action_time_ms<0 ||
        snprintf(path,sizeof(path),"%s",body_path)>=(int)sizeof(path)) return -1;
    if (diagnostic_model && diagnostic_model[0]) {
        if (snprintf(path,sizeof(path),"%s",diagnostic_model)>=(int)sizeof(path)) return -1;
    }
    resource=&scene_body_resources[body_id];
    instance=tlibc_malloc(sizeof(*instance));
    if (!instance) return -1;
    memset(instance,0,sizeof(*instance));
    if ((!resource->definition.bone_count && rasterfall_model_resource_load(resource,path)<0) ||
        !resource->definition.bone_count ||
        resource->definition.bone_count>RF_GPU_SCENE_POSE_BONES ||
        rasterfall_model_instance_init(instance,resource)<0) goto done;
    composition.layers[0].clip=humanoid_action(sample->walk ?
        RASTERFALL_ACTION_LOCOMOTION_WALK : RASTERFALL_ACTION_LOCOMOTION_IDLE);
    composition.layers[1].clip=humanoid_action(sample->rifle.aim_milli || sample->fire ?
        RASTERFALL_ACTION_RIFLE_AIM : RASTERFALL_ACTION_RIFLE_IDLE);
    if (!composition.layers[0].clip || !composition.layers[1].clip) goto done;
    composition.layers[0].time_ms=(int)(sample->lower_time_ms%composition.layers[0].clip->duration_ms);
    composition.layers[1].time_ms=sample->action_time_ms%composition.layers[1].clip->duration_ms;
    if (sample->fire) {
        composition.layers[2].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_RECOIL);
        composition.layers[2].time_ms=sample->action_time_ms;
        if (!composition.layers[2].clip) goto done;
    }
    if (!(freeze && !sample->armed) &&
        rasterfall_action_compose(instance,&composition)<0) goto done;
    if (sample->armed && rasterfall_rifle_pose_solve(instance,sample->weapon,
            scale,&sample->rifle,NULL)<0) goto done;
    memset(out,0,sizeof(*out));
    out->abi_version=1;out->byte_size=sizeof(*out);
    out->frame_id=frame;out->world_generation=world;out->actor_count=1;
    out->character_id=RASTERFALL_CHARACTER_NONE;out->body_resource_id=body_id;
    out->bone_count=resource->definition.bone_count;out->scene_light_q8=256;
    out->body_to_world.rotation[0]=out->body_to_world.rotation[8]=cy/1024.0;
    out->body_to_world.rotation[2]=sy/1024.0;
    out->body_to_world.rotation[6]=-sy/1024.0;
    out->body_to_world.rotation[4]=1;
    out->body_to_world.translation[0]=x;out->body_to_world.translation[1]=y;
    out->body_to_world.translation[2]=z;
    out->body_to_world.scale_milli=scale;
    if (sample->armed && scene_pose_weapon_from_instance(out,instance,sample->weapon,scale)<0) goto done;
    out->muzzle_flash=sample->fire && sample->action_time_ms<65;
    result=rasterfall_model_build_skin_palette(rasterfall_model_instance_final_pose(instance),
        out->palette,out->bone_count);
done:
    rasterfall_model_instance_unload(instance);
    tlibc_free(instance);return result;
}

int rf_gpu_scene_pose_body_sample(int body_id,uint64_t frame,uint64_t world,
    const struct rf_gpu_scene_body_sample *sample,int x,int y,int z,int sy,int cy,
    struct rf_gpu_scene_pose_v1 *out)
{
    return scene_pose_body_sample(body_id,frame,world,sample,x,y,z,sy,cy,NULL,0,out);
}

int rf_gpu_scene_pose_body_action(int body_id,uint64_t frame,uint64_t world,
    uint64_t time_ms,int action,int x,int y,int z,int cy,
    const struct rasterfall_rifle_pose_input *rifle,struct rf_gpu_scene_pose_v1 *out)
{
    struct rf_gpu_scene_body_sample sample={0};
    if (action<RF_GPU_SCENE_BODY_IDLE || action>RF_GPU_SCENE_BODY_WALK_RIFLE) return -1;
    sample.lower_time_ms=time_ms;
    sample.armed=action>=RF_GPU_SCENE_BODY_RIFLE_IDLE;
    sample.weapon=TOY_GAME_WEAPON_AK;
    sample.walk=action==RF_GPU_SCENE_BODY_WALK || action==RF_GPU_SCENE_BODY_WALK_FIRE ||
        action==RF_GPU_SCENE_BODY_WALK_RIFLE;
    sample.fire=action==RF_GPU_SCENE_BODY_WALK_FIRE;
    sample.action_time_ms=(int)(time_ms%(sample.fire?600:2400));
    sample.rifle.aim_milli=action==RF_GPU_SCENE_BODY_RIFLE_AIM?1000:0;
    sample.rifle.hip_milli=sample.walk && sample.armed?1000:0;
    if (sample.fire) {
        int t=sample.action_time_ms;
        sample.rifle.recoil_milli=t<35?t*1000/35:t<180?(180-t)*1000/145:0;
    }
    if(rifle)sample.rifle=*rifle;
    const char *pitch=getenv("RF_GPU_CHARACTER_AIM_PITCH");
    const char *yaw=getenv("RF_GPU_CHARACTER_AIM_YAW");
    const char *distance=getenv("RF_GPU_CHARACTER_AIM_DISTANCE");
    const char *freeze=getenv("RF_GPU_CHARACTER_FREEZE");
    const char *idle=getenv("RF_GPU_CHARACTER_IDLE_POSE");
    if(idle && action==RF_GPU_SCENE_BODY_RIFLE_IDLE) {
        int pose=atoi(idle);
        if(pose>=1 && pose<=RASTERFALL_RIFLE_IDLE_COUNT) {
            memset(&sample.rifle,0,sizeof(sample.rifle));
            sample.rifle.idle_milli[pose-1]=1000;
        }
    }
    if(pitch)sample.rifle.pitch_mdeg=atoi(pitch)*1000;
    if(yaw)sample.rifle.yaw_mdeg=atoi(yaw)*1000;
    if(distance)sample.rifle.target_distance_rfu=atoi(distance);
    return scene_pose_body_sample(body_id,frame,world,&sample,x,y,z,0,cy,
        getenv("RF_GPU_CHARACTER_MODEL"),freeze && freeze[0]=='1',out);
}

int rf_gpu_scene_pose_body(int body_id,uint64_t frame,uint64_t world,
    uint64_t time_ms,int walk,int x,int y,int z,int cy,struct rf_gpu_scene_pose_v1 *out)
{
    return rf_gpu_scene_pose_body_action(body_id,frame,world,time_ms,
        walk ? RF_GPU_SCENE_BODY_WALK : RF_GPU_SCENE_BODY_IDLE,x,y,z,cy,NULL,out);
}

static int scene_pose_from_instance(struct rf_gpu_scene_pose_v1 *next,
    struct rasterfall_model_instance *instance,
    const struct rasterfall_character_visual_recipe *recipe,
    const struct rf_gpu_scene_actor_v1 *actor,
    const struct rf_gpu_scene_local_presentation *p)
{
    struct rasterfall_modular_pose_cache gear;
    if (!next || !instance || !recipe || !actor || !p ||
        !rasterfall_model_instance_final_pose(instance) ||
        !modular_actor_runtime.body.definition.bone_count ||
        modular_actor_runtime.body.definition.bone_count>RF_GPU_SCENE_POSE_BONES ||
        recipe->attachment_count>RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS)
        return -1;
    next->actor_count=1; next->identity=actor->identity;
    next->scene_light_q8=p->scene_light_q8;
    next->bone_count=modular_actor_runtime.body.definition.bone_count;
    next->body_resource_id=recipe->body_resource_id;
    next->character_id=p->character_id;
    next->shirt_color=recipe->shirt_color;
    next->pants_color=recipe->pants_color;
    if (recipe->clothing_count>RASTERFALL_CHARACTER_RECIPE_CLOTHING) return -1;
    next->clothing_count=recipe->clothing_count;
    next->body_hidden_material_mask=rasterfall_character_recipe_hidden_materials(recipe);
    for (unsigned i=0;i<recipe->clothing_count;++i) {
        const struct rasterfall_character_clothing_profile *clothing=
            rasterfall_character_clothing_profile(recipe->clothing_resources[i]);
        if (!clothing || clothing->body_resource_id!=recipe->body_resource_id) return -1;
        next->clothing_resources[i]=(uint32_t)recipe->clothing_resources[i];
    }
    next->bind_normals=rasterfall_model_instance_final_pose(instance)->animation.pose==RASTERFALL_MODEL_POSE_BIND;
    if (rasterfall_model_build_skin_palette(rasterfall_model_instance_final_pose(instance),
            next->palette,next->bone_count)<0) return -1;
    modular_rigid_identity(&next->body_to_world);
    next->body_to_world.translation[0]=actor->x;
    next->body_to_world.translation[1]=actor->y;
    next->body_to_world.translation[2]=actor->z;
    next->body_to_world.rotation[0]=next->body_to_world.rotation[8]=actor->cy/1024.0;
    next->body_to_world.rotation[2]=actor->sy/1024.0;
    next->body_to_world.rotation[6]=-actor->sy/1024.0;
    next->body_to_world.scale_milli=835;
    if (actor->weapon>=0 && rasterfall_weapon_asset_profile(actor->weapon)->skeletal) {
        if (scene_pose_weapon_from_instance(next,instance,actor->weapon,835)<0) return -1;
    }
    memset(&gear,0,sizeof(gear));
    if (modular_prepare_gear_transforms(instance,recipe,modular_actor_runtime.gear,&gear)<0) return -1;
    next->attachment_count=gear.gear_count;
    for (unsigned int i=0;i<gear.gear_count;++i) {
        next->attachments[i].resource_id=recipe->attachments[i].gear_resource_id;
        next->attachments[i].host_socket=recipe->attachments[i].host_socket;
        modular_rigid_compose(&next->body_to_world,&gear.gear_local[i],
                             &next->attachments[i].model_to_world);
    }
    return 0;
}

int rf_gpu_scene_pose_extract_at(const struct rf_gpu_scene_local_frame *frame,
    uint32_t actor_index,struct rf_gpu_scene_pose_v1 *out)
{
    const struct rf_gpu_scene_actor_v1 *actor;
    const struct rf_gpu_scene_local_presentation *p;
    const struct rasterfall_character_visual_recipe *recipe;
    struct rf_gpu_scene_pose_v1 *next;
    struct rasterfall_model_instance *instance;
    struct rasterfall_action_composition composition;
    struct rf_gpu_scene_frozen_v1 *metadata;
    int result=-1;
    if (!frame || !out) return -1;
    metadata=tlibc_malloc(sizeof(*metadata));
    if (!metadata) return -1;
    result=rf_gpu_scene_extract_v1(&frame->snapshot,metadata);
    tlibc_free(metadata);
    if (result<0 || (actor_index>=frame->snapshot.actor_count &&
        (frame->snapshot.actor_count || actor_index))) return -1;
    next=tlibc_malloc(sizeof(*next));
    if (!next) return -1;
    memset(next,0,sizeof(*next));
    next->abi_version=1; next->byte_size=sizeof(*next);
    next->frame_id=frame->snapshot.frame_id;
    next->world_generation=frame->snapshot.world_generation;
    if (!frame->snapshot.actor_count) { *out=*next; tlibc_free(next); return 0; }
    actor=&frame->snapshot.actors[actor_index];
    p=actor_index ? &frame->presentations[actor_index] : &frame->presentation;
    recipe=rasterfall_character_visual_recipe_for_character(p->character_id);
    if (memcmp(&p->identity,&actor->identity,sizeof(p->identity)) ||
        !recipe || !recipe->attachment_count ||
        p->scene_light_q8<0 || p->scene_light_q8>384 ||
        p->state==TOY_GAME_ACTOR_DOWNED ||
        (p->lower_walk!=0 && p->lower_walk!=1) ||
        actor->animation_time_ms<0 || modular_actor_resources_init()<0 ||
        !modular_actor_runtime.body.definition.bone_count ||
        modular_actor_runtime.body.definition.bone_count>RF_GPU_SCENE_POSE_BONES) {
        tlibc_free(next); return -1;
    }
    instance=tlibc_malloc(sizeof(*instance));
    if (!instance) { tlibc_free(next); return -1; }
    memset(instance,0,sizeof(*instance));
    result=-1;
    if (rasterfall_model_instance_init(instance,&modular_actor_runtime.body)<0) goto done;
    memset(&composition,0,sizeof(composition));
    composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].clip=humanoid_action(
        p->lower_walk ? RASTERFALL_ACTION_LOCOMOTION_WALK : RASTERFALL_ACTION_LOCOMOTION_IDLE);
    composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].clip=humanoid_action(
        actor->animation_id==TOY_GAME_ANIM_FIRE ? RASTERFALL_ACTION_RIFLE_AIM : RASTERFALL_ACTION_RIFLE_IDLE);
    if (!composition.layers[0].clip || !composition.layers[1].clip ||
        composition.layers[0].clip->duration_ms<=0) goto done;
    composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].time_ms=
        (int)(p->lower_time_ms%(uint64_t)composition.layers[0].clip->duration_ms);
    composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].time_ms=actor->animation_time_ms;
    if (actor->animation_id==TOY_GAME_ANIM_FIRE) {
        composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_RECOIL);
        composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].time_ms=actor->animation_time_ms;
        if (!composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].clip) goto done;
    }
    if (p->evasion.weight_milli) {
        if (p->evasion.side != -1 && p->evasion.side != 1) goto done;
        composition.secondary_additive.clip=humanoid_action(p->evasion.side < 0 ?
            RASTERFALL_ACTION_EVADE_LEFT : RASTERFALL_ACTION_EVADE_RIGHT);
        composition.secondary_additive.time_ms=p->evasion.time_ms;
        composition.secondary_additive_weight_milli=p->evasion.weight_milli;
        if (!composition.secondary_additive.clip) goto done;
    }
    if (rasterfall_action_compose(instance,&composition)<0 ||
        actor->weapon < -1 || actor->weapon>=TOY_GAME_WEAPON_COUNT ||
        (actor->weapon>=0 && rasterfall_weapon_asset_profile(actor->weapon)->skeletal &&
         rasterfall_rifle_pose_solve(instance,actor->weapon,835,&p->rifle,NULL)<0)) goto done;
    if (scene_pose_from_instance(next,instance,recipe,actor,p)<0) goto done;
    *out=*next; result=0;
done:
    rasterfall_model_instance_unload(instance);
    tlibc_free(instance); tlibc_free(next);
    return result;
}

int rf_gpu_scene_pose_extract(const struct rf_gpu_scene_local_frame *frame,
                             struct rf_gpu_scene_pose_v1 *out)
{
    return rf_gpu_scene_pose_extract_at(frame,0,out);
}

/* Frozen opaque_box from gpu_scene_render_fixture.map. Reuses the normal
 * map mesh builder, including face colors and primitive-local origins. */
struct rasterfall_model_asset *rf_gpu_scene_fixture_map(void)
{
    struct persistent_map_mesh_build build={0};
    struct rasterfall_model_asset *model=NULL;
    if (!persistent_map_mesh_add_box(&build,-4900,-3900,-900,550,2400,3300,0xB66A35,0))
        model=persistent_map_mesh_finish(&build);
    tlibc_free(build.patches);
    return model;
}
