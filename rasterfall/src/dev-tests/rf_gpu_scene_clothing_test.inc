/* Stable skin-follower contract; visual fit stays in native review captures. */
static int scene_clothing_pose_test(void)
{
    static struct rasterfall_model_instance host;
    static struct rasterfall_model_asset view,bad;
    static struct rasterfall_model_bone altered[RF_GPU_SCENE_POSE_BONES];
    struct rasterfall_character_visual_recipe recipe={0};
    struct rasterfall_action_composition action={0};
    struct rf_gpu_scene_actor_v1 actor={0};
    struct rf_gpu_scene_local_presentation presentation={0};
    static struct rf_gpu_scene_pose_v1 scene,repeat;
    struct rasterfall_model_skin_palette_bone before[RF_GPU_SCENE_POSE_BONES];
    int failure=0, leg_moved[2]={0,0},leg_blended[2]={0,0};
#define CLOTH_CHECK(x) do { if (!(x)) { failure=__LINE__; goto done; } } while (0)
    CLOTH_CHECK(!modular_actor_resources_init());
    memset(&host,0,sizeof(host));
    CLOTH_CHECK(!rasterfall_model_instance_init(&host,&modular_actor_runtime.body));
    recipe.body_resource_id=RASTERFALL_BODY_RF_HUMANOID_V2;
    CLOTH_CHECK(!rasterfall_character_recipe_hidden_materials(&recipe));
    recipe.clothing_count=1;recipe.clothing_resources[0]=RASTERFALL_CLOTHING_FIELD_JACKET;
    CLOTH_CHECK(rasterfall_character_recipe_hidden_materials(&recipe)==(1u<<1));
    recipe.clothing_count=2;recipe.clothing_resources[1]=RASTERFALL_CLOTHING_COMBAT_TROUSERS;
    CLOTH_CHECK(rasterfall_character_recipe_hidden_materials(&recipe)==3);
    actor.cy=1024;actor.weapon=-1;presentation.scene_light_q8=256;
    action.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_WALK);
    action.layers[0].time_ms=180;
    action.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_IDLE);
    CLOTH_CHECK(action.layers[0].clip && action.layers[1].clip &&
        !rasterfall_action_compose(&host,&action));
    memset(before,0,sizeof(before));memset(&scene,0,sizeof(scene));memset(&repeat,0,sizeof(repeat));
    CLOTH_CHECK(!rasterfall_model_build_skin_palette(&host.pose,before,host.pose.bone_count));
    CLOTH_CHECK(!scene_pose_from_instance(&scene,&host,&recipe,&actor,&presentation) &&
        scene.clothing_count==2 && scene.body_hidden_material_mask==3 &&
        !memcmp(before,scene.palette,sizeof(before)));
    for(unsigned i=0;i<recipe.clothing_count;++i) {
        const struct rasterfall_model_asset *garment=
            &modular_actor_runtime.clothing[recipe.clothing_resources[i]].definition;
        CLOTH_CHECK(scene.clothing_resources[i]==(unsigned)recipe.clothing_resources[i] &&
            rasterfall_model_shared_skin_compatible(&host.pose,garment));
        bad=*garment;bad.position_scale++;
        CLOTH_CHECK(!rasterfall_model_shared_skin_compatible(&host.pose,&bad));
        bad=*garment;memcpy(altered,garment->bones,garment->bone_count*sizeof(*altered));
        bad.bones=altered;altered[0].rest_y++;
        CLOTH_CHECK(!rasterfall_model_shared_skin_compatible(&host.pose,&bad));
        view=*garment;view.skinning_enabled=1;view.animation.pose=host.pose.animation.pose;
        for(unsigned vertex=0;vertex<view.vertex_count;++vertex) {
            int position[3],normal[3];
            const unsigned char *skin=view.skin_vertices+vertex*RASTERFALL_MODEL_SKIN_VERTEX_BYTES;
            CLOTH_CHECK(!rasterfall_model_skin_vertex_palette(&view,scene.palette,
                scene.bone_count,vertex,position,normal));
            if(recipe.clothing_resources[i]==RASTERFALL_CLOTHING_COMBAT_TROUSERS) {
                const int *bind=(const int *)(view.vertices+vertex*view.vertex_bytes);
                for(int side=0;side<2;++side) {
                    int upper=rasterfall_model_find_bone(&view,side ? "RF_R_UPPER_LEG":"RF_L_UPPER_LEG");
                    int lower=rasterfall_model_find_bone(&view,side ? "RF_R_LOWER_LEG":"RF_L_LOWER_LEG");
                    unsigned b0=skin[0]|((unsigned)skin[1]<<8),
                        b1=skin[2]|((unsigned)skin[3]<<8);
                    if(skin[6]==1 && ((b0==(unsigned)upper && b1==(unsigned)lower) ||
                        (b1==(unsigned)upper && b0==(unsigned)lower))) {
                        leg_blended[side]++;
                        if(memcmp(position,bind,sizeof(position))) leg_moved[side]++;
                    }
                }
            }
        }
    }
    CLOTH_CHECK(leg_blended[0] && leg_blended[1] && leg_moved[0] && leg_moved[1]);
    CLOTH_CHECK(!rasterfall_model_build_skin_palette(&host.pose,repeat.palette,host.pose.bone_count) &&
        !memcmp(before,repeat.palette,sizeof(before)));
    memset(&repeat,0,sizeof(repeat));
    CLOTH_CHECK(!scene_pose_from_instance(&repeat,&host,&recipe,&actor,&presentation) &&
        !memcmp(&scene,&repeat,sizeof(scene)));
    recipe.clothing_count=RASTERFALL_CHARACTER_RECIPE_CLOTHING+1;
    CLOTH_CHECK(scene_pose_from_instance(&repeat,&host,&recipe,&actor,&presentation)<0);
    recipe.clothing_count=1;recipe.clothing_resources[0]=-1;
    CLOTH_CHECK(scene_pose_from_instance(&repeat,&host,&recipe,&actor,&presentation)<0);
done:
    rasterfall_model_instance_unload(&host);
    if(failure) __printf("scene clothing FAIL line=%d\n",failure);
    else __printf("scene clothing PASS coverage/shared-palette/bind-mismatch/both-knees/read-only/replay/invalid\n");
#undef CLOTH_CHECK
    return failure!=0;
}
