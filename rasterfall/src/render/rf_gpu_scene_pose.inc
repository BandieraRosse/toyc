/* Included by the character producer to reuse its resource, action, IK and
 * attachment evaluators. No game/session lookup or draw recording here. */
#include "rf_gpu_scene_pose.h"

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
        struct rasterfall_modular_weapon_placement weapon;
        double basis[9]={1,0,0,0,1,0,0,0,1}, rotation[9], offset[3], centered[3];
        double center[3];
        if (modular_prepare_weapon_placement(instance,actor->weapon,835,&weapon)<0) return -1;
        if (weapon.asset->asset_basis==1) {
            basis[0]=basis[8]=0; basis[2]=basis[6]=1;
        } else if (weapon.asset->asset_basis==2) basis[0]=basis[8]=-1;
        rigid_matrix_multiply(next->body_to_world.rotation,weapon.rotation,rotation);
        rigid_matrix_multiply(rotation,basis,next->weapon_to_world.rotation);
        rigid_matrix_vector(next->body_to_world.rotation,weapon.origin,offset);
        center[0]=(weapon.model->min_x+weapon.model->max_x)/2;
        center[1]=(weapon.model->min_y+weapon.model->max_y)/2;
        center[2]=(weapon.model->min_z+weapon.model->max_z)/2;
        rigid_matrix_vector(next->weapon_to_world.rotation,center,centered);
        for (int i=0;i<3;++i)
            next->weapon_to_world.translation[i]=next->body_to_world.translation[i]+
                offset[i]*835.0/1000.0-centered[i]*weapon.geometry_scale/1000.0;
        next->weapon_to_world.scale_milli=weapon.geometry_scale;
        next->weapon_valid=1; next->weapon=actor->weapon;
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
    if (rasterfall_action_compose(instance,&composition)<0 ||
        actor->weapon < -1 || actor->weapon>=TOY_GAME_WEAPON_COUNT ||
        (actor->weapon>=0 && rasterfall_weapon_asset_profile(actor->weapon)->skeletal &&
         modular_solve_left_hand(instance,actor->weapon,835)<0)) goto done;
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
