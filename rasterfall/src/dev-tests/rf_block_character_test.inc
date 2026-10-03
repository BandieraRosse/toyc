/* Stable Block contract: rigid bone lengths, common rig, finalized grip,
 * independent instances, and stateless replay across unrelated actions. */
static int block_character_pose_test(void)
{
    struct rasterfall_procedural_humanoid_state state={0};
    struct rasterfall_rigid_transform world;
    static struct rasterfall_model_instance peer;
    struct rasterfall_model_skin_palette_bone saved[RF_GPU_SCENE_POSE_BONES]={0},replay[RF_GPU_SCENE_POSE_BONES]={0};
    int result=1;
#define BLOCK_CHECK(x) do { if (!(x)) { __printf("block rig FAIL line=%d\n",__LINE__); goto done; } } while (0)
    BLOCK_CHECK(!block_body_ready() && !modular_actor_resources_init());
    BLOCK_CHECK(rasterfall_model_shared_skin_compatible(&block_body_resource.definition,
        &modular_actor_runtime.body.definition));
    BLOCK_CHECK(!rasterfall_model_instance_init(&peer,&block_body_resource));
    state.cy=1024;state.weapon=TOY_GAME_WEAPON_AK;state.rifle_valid=1;
    state.animation_id=TOY_GAME_ANIM_FIRE;state.animation_time_ms=40;
    state.moving=1;state.locomotion_time_ms=230;state.rifle.hip_milli=1000;
    BLOCK_CHECK(!block_character_sample(&state,&peer,&world));
    BLOCK_CHECK(!rasterfall_model_build_skin_palette(&peer.pose,saved,peer.pose.bone_count));
    for(int action=0;action<TOY_GAME_ANIM_COUNT;++action)
        for(int hold=0;hold<6;++hold) {
            struct rasterfall_procedural_humanoid_state next=state;
            next.animation_id=action;next.animation_time_ms=190;
            memset(&next.rifle,0,sizeof(next.rifle));
            if(hold==0)next.rifle.aim_milli=1000;
            if(hold==1)next.rifle.hip_milli=1000;
            if(hold>=2 && hold<5)next.rifle.idle_milli[hold-2]=1000;
            next.rifle.pitch_mdeg=hold==5?65000:0;
            if(hold==5)next.rifle.aim_milli=1000;
            BLOCK_CHECK(!block_character_sample(&next,&block_body_instance,&world));
            const struct rasterfall_model_asset *pose=&block_body_instance.pose;
            const struct rasterfall_model_asset *definition=&block_body_resource.definition;
            for(unsigned bone=0;bone<pose->bone_count;++bone) {
                int parent=pose->bones[bone].parent;
                BLOCK_CHECK(pose->bones[bone].rest_x==definition->bones[bone].rest_x &&
                    pose->bones[bone].rest_y==definition->bones[bone].rest_y &&
                    pose->bones[bone].rest_z==definition->bones[bone].rest_z);
                if(parent<0)continue;
                double posed=0,bind=0;
                double rest[3]={pose->bones[bone].rest_x-pose->bones[parent].rest_x,
                    pose->bones[bone].rest_y-pose->bones[parent].rest_y,
                    pose->bones[bone].rest_z-pose->bones[parent].rest_z};
                for(int k=0;k<3;++k) {
                    double d=pose->bone_transforms[bone].position[k]-pose->bone_transforms[parent].position[k];
                    posed+=d*d;bind+=rest[k]*rest[k];
                }
                BLOCK_CHECK(fabs(sqrt(posed)-sqrt(bind))<.001);
            }
            if(action==TOY_GAME_ANIM_IDLE && hold!=2) {
                struct rasterfall_modular_weapon_placement weapon;
                double target[3];
                BLOCK_CHECK(!modular_prepare_weapon_placement(&block_body_instance,TOY_GAME_WEAPON_AK,835,&weapon));
                const struct rasterfall_cal_vec3 *fore=&weapon.authored[RASTERFALL_WEAPON_SOCKET_FOREGRIP].position;
                BLOCK_CHECK(!rasterfall_weapon_socket_transform(TOY_GAME_WEAPON_AK,RASTERFALL_WEAPON_SOCKET_FOREGRIP,
                    &weapon.authored[RASTERFALL_WEAPON_SOCKET_FOREGRIP]));
                modular_weapon_model_point(weapon.origin,weapon.rotation,(double[3]){fore->x,fore->y,fore->z},835,target);
                struct rasterfall_model_attachment_transform actual;
                BLOCK_CHECK(!rasterfall_model_instance_attachment_transform(&block_body_instance,RASTERFALL_ATTACHMENT_FOREGRIP,&actual));
                double error=0;
                for(int k=0;k<3;++k) {
                    double d=actual.position[k]-target[k];error+=d*d;
                }
                if(sqrt(error)>=12) __printf("block grip hold=%d error=%.3f actual=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f)\n",
                    hold,sqrt(error),actual.position[0],actual.position[1],actual.position[2],target[0],target[1],target[2]);
                BLOCK_CHECK(sqrt(error)<12);
            }
        }
    BLOCK_CHECK(!rasterfall_model_build_skin_palette(&peer.pose,replay,peer.pose.bone_count));
    BLOCK_CHECK(!memcmp(saved,replay,peer.pose.bone_count*sizeof(*saved)));
    BLOCK_CHECK(!block_character_sample(&state,&block_body_instance,&world));
    BLOCK_CHECK(!rasterfall_model_build_skin_palette(&block_body_instance.pose,replay,peer.pose.bone_count));
    BLOCK_CHECK(!memcmp(saved,replay,peer.pose.bone_count*sizeof(*saved)));
    state.locomotion_time_ms+=400;
    BLOCK_CHECK(!block_character_sample(&state,&peer,&world));
    BLOCK_CHECK(!rasterfall_model_build_skin_palette(&peer.pose,replay,peer.pose.bone_count));
    BLOCK_CHECK(memcmp(saved,replay,peer.pose.bone_count*sizeof(*saved)));
    __printf("block rig PASS standard hierarchy/fixed lengths/grip/instance isolation/replay/walking fire\n");
    result=0;
done:
    rasterfall_model_instance_unload(&peer);
    return result;
#undef BLOCK_CHECK
}
