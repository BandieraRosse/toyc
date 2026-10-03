/* Stable grasp contract: animation and representation units may change, but
 * PRIMARY_GRIP/FOREGRIP must still close onto the finalized hand sockets. */
static int scene_firearm_grip_test(void)
{
    static const int bodies[]={RASTERFALL_BODY_RF_BLOCK,RASTERFALL_BODY_RF_HUMANOID_V2,RASTERFALL_BODY_RF_C01_V22H};
    struct rasterfall_model_resource resource={0};
    struct rasterfall_model_instance instance={0};
    struct rasterfall_action_composition c={0};
    int failures=0,samples=0;
    for(int body=0;body<3;++body) {
        if(rasterfall_model_resource_load(&resource,rasterfall_character_body_resource_path(bodies[body]))<0 ||
            rasterfall_model_instance_init(&instance,&resource)<0) { failures++;break; }
        int scale=body==2?1000:835;
        for(int weapon=TOY_GAME_WEAPON_PISTOL;weapon<=TOY_GAME_WEAPON_AWP;++weapon) {
            static const int lengths[]={204,267,1003,880,1120};
            const struct rasterfall_weapon_asset_profile *profile=rasterfall_weapon_asset_profile(weapon);
            struct rasterfall_model_asset model={0};
            struct rasterfall_weapon_model_adapter adapter={0};
            if(rasterfall_model_load(&model,profile->model_path)<0) { failures++;continue; }
            int good=!rasterfall_weapon_model_adapt(weapon,
                (int[3]){model.min_x,model.min_y,model.min_z},(int[3]){model.max_x,model.max_y,model.max_z},&adapter);
            good=good && adapter.reference_length_mm==lengths[weapon] &&
                fabs(adapter.presented_length_mm-lengths[weapon])<2;
            if(!good)failures++;
            if(!body)__printf("FIREARM-SIZE weapon=%d reference_mm=%d measured_mm=%.3f valid=%d\n",
                weapon,lengths[weapon],adapter.presented_length_mm,good);
            rasterfall_model_unload(&model);
        }
        for(int weapon=TOY_GAME_WEAPON_PISTOL;weapon<=TOY_GAME_WEAPON_AWP;++weapon)
        for(int state=0;state<6;++state)
        for(int pitch=-45;pitch<=45;pitch+=45)for(int yaw=-30;yaw<=30;yaw+=30) {
            struct rasterfall_rifle_pose_input input={0};
            struct rasterfall_rifle_diagnostics d={0};
            struct rasterfall_modular_weapon_placement placement;
            c.layers[0].clip=humanoid_action(state==1?RASTERFALL_ACTION_LOCOMOTION_WALK:RASTERFALL_ACTION_LOCOMOTION_IDLE);
            c.layers[0].time_ms=180;
            c.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_AIM);
            input.aim_milli=state==0 || state==2?1000:0;
            input.hip_milli=state==1?1000:0;
            if(state>=3)input.idle_milli[state-3]=1000;
            input.recoil_milli=state==2?1000:0;
            input.pitch_mdeg=pitch*1000;input.yaw_mdeg=yaw*1000;input.target_distance_rfu=8192;
            int good=!rasterfall_action_compose(&instance,&c) &&
                !rasterfall_rifle_pose_solve(&instance,weapon,scale,&input,&d) &&
                !modular_prepare_weapon_placement(&instance,weapon,scale,&placement);
            good=good && !d.reach_clamped[0] && !d.reach_clamped[1] &&
                d.grip_error[0]<3 && d.grip_error[1]<3 && d.wrist_dot[1]>.5 &&
                (state==3 || d.wrist_dot[0]>.5) && d.clearance_rfu>-.5;
            if(!good) {
                if(failures<60)__printf("FIREARM-GRIP body=%d weapon=%d state=%d pitch=%d yaw=%d grip=%.2f,%.2f wrist=%.2f,%.2f reach=%d,%d clearance=%.2f\n",
                    body,weapon,state,pitch,yaw,d.grip_error[0],d.grip_error[1],d.wrist_dot[0],d.wrist_dot[1],d.reach_clamped[0],d.reach_clamped[1],d.clearance_rfu);
                failures++;
            }
            samples++;
        }
        rasterfall_model_instance_unload(&instance);rasterfall_model_resource_unload(&resource);
    }
    rasterfall_model_instance_unload(&instance);rasterfall_model_resource_unload(&resource);
    __printf("firearm grip %s samples=%d failures=%d\n",failures?"FAIL":"PASS",samples,failures);
    return failures!=0;
}

static int scene_grip_pose_test(void)
{
    struct rasterfall_model_instance instance={0};
    struct rasterfall_model_resource catalog={0};
    struct rasterfall_action_composition composition={0};
    struct rasterfall_model_attachment_transform left;
    struct rasterfall_weapon_socket_transform foregrip;
    struct rasterfall_modular_weapon_placement placement;
    double maximum_position=0,maximum_rotation=0,minimum_wrist_dot=1;
    static const int times[]={0,35,90,180,350,600,900,1200};
    int failure=0,constraint_failures=0;
#define GRIP_CHECK(x) do { if (!(x)) { failure=__LINE__;goto done; } } while (0)
    {
        struct toy_game_actor actor={0},saved;
        struct rasterfall_rifle_history history={0};
        struct rasterfall_rifle_pose_input sample,repeated;
        struct rasterfall_weapon_model_adapter a,b;
        actor.actor_id=7;actor.combat_generation=4;actor.state=TOY_GAME_ACTOR_ALIVE;
        actor.combat_target.kind=-1;actor.pitch_cy=1024;
        rasterfall_rifle_sample(&actor,0,&history,&sample);
        GRIP_CHECK(!sample.aim_milli);
        actor.combat_target.kind=0;actor.combat_target.z=4096;
        actor.pitch_sy=1024;saved=actor;
        rasterfall_rifle_sample(&actor,100,&history,&sample);
        GRIP_CHECK(sample.aim_milli>0 && sample.aim_milli<1000 &&
            sample.pitch_mdeg>=44999 && sample.pitch_mdeg<=45001 && sample.target_distance_rfu==4096);
        rasterfall_rifle_sample(&actor,100,&history,&repeated);
        GRIP_CHECK(!memcmp(&sample,&repeated,sizeof(sample)) && !memcmp(&actor,&saved,sizeof(actor)));
        rasterfall_rifle_sample(&actor,350,&history,&sample);
        GRIP_CHECK(sample.aim_milli==1000);
        actor.combat_target.kind=-1;
        rasterfall_rifle_sample(&actor,450,&history,&sample);
        GRIP_CHECK(sample.aim_milli>0 && sample.aim_milli<1000);
        rasterfall_rifle_sample(&actor,700,&history,&sample);
        GRIP_CHECK(!sample.aim_milli);
        actor.combat_generation++;
        rasterfall_rifle_sample(&actor,50,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && history.generation==actor.combat_generation);
        actor.slots[0].weapon=TOY_GAME_WEAPON_AK;
        actor.moving=1;actor.animation.id=TOY_GAME_ANIM_FIRE;
        actor.animation.time_ms=35;
        rasterfall_rifle_sample(&actor,150,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && sample.hip_milli>0 && sample.hip_milli<1000 &&
            sample.recoil_milli==1000);
        rasterfall_rifle_sample(&actor,400,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && sample.hip_milli==1000);
        saved=actor;
        rasterfall_rifle_sample(&actor,400,&history,&repeated);
        GRIP_CHECK(!memcmp(&sample,&repeated,sizeof(sample)) && !memcmp(&actor,&saved,sizeof(actor)));
        actor.moving=0;
        rasterfall_rifle_sample(&actor,500,&history,&sample);
        GRIP_CHECK(sample.aim_milli>0 && sample.aim_milli<1000 &&
            sample.hip_milli>0 && sample.hip_milli<1000);
        rasterfall_rifle_sample(&actor,750,&history,&sample);
        GRIP_CHECK(sample.aim_milli==1000 && !sample.hip_milli);
        actor.moving=1;actor.reloading=1;
        rasterfall_rifle_sample(&actor,1000,&history,&sample);
        rasterfall_rifle_sample(&actor,1250,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && !sample.hip_milli);
        actor.reloading=0;actor.slots[0].weapon=TOY_GAME_WEAPON_SMG;
        rasterfall_rifle_sample(&actor,1500,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && sample.hip_milli==1000);
        actor.slots[0].weapon=TOY_GAME_WEAPON_AK;actor.combat_generation++;
        rasterfall_rifle_sample(&actor,10,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && sample.hip_milli==1000);
        actor.state=TOY_GAME_ACTOR_DOWNED;actor.combat_generation++;
        rasterfall_rifle_sample(&actor,20,&history,&sample);
        GRIP_CHECK(!sample.aim_milli && !sample.hip_milli);
        GRIP_CHECK(!rasterfall_weapon_model_adapt(TOY_GAME_WEAPON_AK,
            (int[3]){-33,-256,-460},(int[3]){30,91,614},&a));
        GRIP_CHECK(!rasterfall_weapon_model_adapt(TOY_GAME_WEAPON_AK,
            (int[3]){-66+80,-512+60,-920+100},(int[3]){60+80,182+60,1228+100},&b));
        GRIP_CHECK(a.reference_length_mm==880 && fabs(a.presented_length_mm-880)<2 &&
            a.presented_length_mm==b.presented_length_mm && a.scale_milli==b.scale_milli*2);
        GRIP_CHECK(a.basis[0]==-1 && a.basis[4]==1 && a.basis[8]==-1);
        GRIP_CHECK(rasterfall_weapon_model_adapt(TOY_GAME_WEAPON_AK,
            (int[3]){0,0,0},(int[3]){1,1,0},&b)<0);
    }
    GRIP_CHECK(!modular_actor_resources_init());
    {
        struct toy_game_actor actor={0},saved;
        struct rasterfall_rifle_history history={0},before;
        struct rasterfall_rifle_pose_input sample,repeated;
        unsigned seen=0;
        actor.active=1;actor.actor_id=11;actor.kind=TOY_GAME_ACTOR_AI;
        actor.state=TOY_GAME_ACTOR_ALIVE;actor.combat_generation=3;
        actor.combat_target.kind=-1;actor.pitch_cy=1024;
        actor.slots[0].weapon=TOY_GAME_WEAPON_AK;actor.animation.id=TOY_GAME_ANIM_IDLE;
        saved=actor;
        for(unsigned tick=0;tick<=90000;tick+=100) {
            before=history;
            rasterfall_rifle_sample(&actor,tick,&history,&sample);
            GRIP_CHECK(history.idle_active && !sample.recoil_milli && !sample.hip_milli);
            if(before.idle_active && tick<before.idle_until)
                GRIP_CHECK(history.idle_pose==before.idle_pose);
            if(before.idle_active && tick>=before.idle_until)
                GRIP_CHECK(history.idle_pose!=before.idle_pose);
            seen|=1u<<history.idle_pose;
            before=history;
            rasterfall_rifle_sample(&actor,tick,&history,&repeated);
            GRIP_CHECK(!memcmp(&sample,&repeated,sizeof(sample)) &&
                !memcmp(&history,&before,sizeof(history)) && !memcmp(&actor,&saved,sizeof(actor)));
        }
        GRIP_CHECK(seen==15);
        actor.combat_target.kind=0;
        rasterfall_rifle_sample(&actor,90250,&history,&sample);
        GRIP_CHECK(!history.idle_active && sample.aim_milli==1000);
        for(int i=0;i<RASTERFALL_RIFLE_IDLE_COUNT;++i)GRIP_CHECK(!sample.idle_milli[i]);
        actor.combat_target.kind=-1;actor.moving=1;
        rasterfall_rifle_sample(&actor,90500,&history,&sample);
        GRIP_CHECK(!history.idle_active && sample.hip_milli==1000);
        actor.moving=0;actor.reloading=1;
        rasterfall_rifle_sample(&actor,90750,&history,&sample);
        GRIP_CHECK(!history.idle_active);
        actor.reloading=0;actor.airborne_ms=100;
        rasterfall_rifle_sample(&actor,91000,&history,&sample);
        GRIP_CHECK(!history.idle_active);
        actor.airborne_ms=0;actor.control_disabled=1;
        rasterfall_rifle_sample(&actor,91250,&history,&sample);
        GRIP_CHECK(!history.idle_active);
        actor.control_disabled=0;
        rasterfall_rifle_sample(&actor,100,&history,&sample);
        GRIP_CHECK(history.tick==100 && history.idle_active);
        actor.state=TOY_GAME_ACTOR_DOWNED;
        rasterfall_rifle_sample(&actor,350,&history,&sample);
        GRIP_CHECK(!history.idle_active);
    }
    GRIP_CHECK(!rasterfall_model_resource_load(&catalog,
        rasterfall_character_body_resource_path(RASTERFALL_BODY_RF_C01_V22H)));
    GRIP_CHECK(!rasterfall_weapon_socket_transform(TOY_GAME_WEAPON_AK,
        RASTERFALL_WEAPON_SOCKET_FOREGRIP,&foregrip));
    for (int representation=0;representation<3;++representation) {
        int scale=representation==2 ? 1000 : 835;
        double shoulder_height=0;
        GRIP_CHECK(!rasterfall_model_instance_init(&instance,representation==2 ?
            &catalog : &modular_actor_runtime.body));
        for(int state=0;state<6;++state)
        for(int pitch=-75;pitch<=75;pitch+=15) for(int yaw=-45;yaw<=45;yaw+=45) {
            struct rasterfall_rifle_pose_input aim={0};
            struct rasterfall_rifle_diagnostics d;
            memset(&composition,0,sizeof(composition));
            composition.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_IDLE);
            composition.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_AIM);
            GRIP_CHECK(!rasterfall_action_compose(&instance,&composition));
            if(representation==1) {
                instance.pose.position_scale*=128;
                for(unsigned i=0;i<instance.pose.bone_count;++i) {
                    instance.pose.bones[i].rest_x*=128;
                    instance.pose.bones[i].rest_y*=128;
                    instance.pose.bones[i].rest_z*=128;
                }
                for(int i=0;i<RASTERFALL_ATTACHMENT_COUNT;++i)
                    for(int axis=0;axis<3;++axis)instance.pose.attachments[i].local_position[axis]*=128;
                GRIP_CHECK(!rasterfall_model_instance_update_bones(&instance));
            }
            aim.aim_milli=state==2 || state>=4?0:state==3?500:1000;
            aim.hip_milli=state>=4?1000:0;
            aim.pitch_mdeg=pitch*1000;aim.yaw_mdeg=yaw*1000;
            aim.armor_milli=state==1 || state==5?1000:0;
            aim.target_distance_rfu=state==1 || state==5?1024:16384;
            aim.recoil_milli=state==3?1000:0;
            GRIP_CHECK(!rasterfall_rifle_pose_solve(&instance,TOY_GAME_WEAPON_AK,scale,&aim,&d));
            int good=!d.reach_clamped[0] && !d.reach_clamped[1] &&
                d.grip_error[0]<3 && d.grip_error[1]<3 &&
                d.wrist_dot[0]>.57 && d.wrist_dot[1]>.57 &&
                d.clearance_rfu>-.5 && ((state==2 || state==3) || d.aim_error_degrees<2.5);
            if(!pitch && !yaw) {
                if(state==0)shoulder_height=d.stock_actual[1];
                if(state==4)GRIP_CHECK(d.stock_actual[1]<shoulder_height-40);
                if(state==4) {
                    int head=rasterfall_model_humanoid_bone(&instance.pose,RASTERFALL_HUMANOID_HEAD);
                    GRIP_CHECK(head>=0 && instance.pose.bone_transforms[head].rotation[8]>.97);
                }
            }
            if(!good || getenv("RF_RIFLE_PROBE"))
                __printf("RIFLE-CONSTRAINT body=%d state=%d pitch=%d yaw=%d error=%.2f,%.2f wrist=%.3f,%.3f clamped=%d,%d clearance=%.2f shift=%.2f aim=%.3f valid=%d\n",
                    representation,state,pitch,yaw,d.grip_error[0],d.grip_error[1],
                    d.wrist_dot[0],d.wrist_dot[1],d.reach_clamped[0],d.reach_clamped[1],
                    d.clearance_rfu,d.clearance_shift_rfu,d.aim_error_degrees,good);
            if(!good)++constraint_failures;
        }
        if(representation!=1)for(int armored=0;armored<2;++armored)
        for(int from=0;from<4;++from)for(int to=0;to<4;++to)
        for(int blend=0;blend<=1000;blend+=250) {
            struct rasterfall_rifle_pose_input rifle={0};
            struct rasterfall_rifle_diagnostics d;
            memset(&composition,0,sizeof(composition));
            composition.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_IDLE);
            composition.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_IDLE);
            GRIP_CHECK(!rasterfall_action_compose(&instance,&composition));
            if(from==3)rifle.aim_milli+=1000-blend;else rifle.idle_milli[from]+=1000-blend;
            if(to==3)rifle.aim_milli+=blend;else rifle.idle_milli[to]+=blend;
            rifle.armor_milli=armored*1000;
            GRIP_CHECK(!rasterfall_rifle_pose_solve(&instance,TOY_GAME_WEAPON_AK,scale,&rifle,&d));
            int free=rifle.idle_milli[RASTERFALL_RIFLE_IDLE_SINGLE];
            int good=!d.reach_clamped[1] && d.grip_error[1]<3 && d.wrist_dot[1]>.57 &&
                (free || (!d.reach_clamped[0] && d.grip_error[0]<3 && d.wrist_dot[0]>.57)) &&
                d.clearance_rfu>-.5;
            if(!good) {
                __printf("RIFLE-IDLE body=%d from=%d to=%d blend=%d grip=%.2f,%.2f wrist=%.3f,%.3f clamp=%d,%d clearance=%.2f\n",
                    representation,from,to,blend,d.grip_error[0],d.grip_error[1],d.wrist_dot[0],d.wrist_dot[1],
                    d.reach_clamped[0],d.reach_clamped[1],d.clearance_rfu);
                ++constraint_failures;
            }
            if(free==1000) {
                int wrist=rasterfall_model_humanoid_bone(&instance.pose,RASTERFALL_HUMANOID_LEFT_HAND);
                int shoulder=rasterfall_model_humanoid_bone(&instance.pose,RASTERFALL_HUMANOID_LEFT_UPPER_ARM);
                GRIP_CHECK(instance.pose.bone_transforms[wrist].position[1]<
                    instance.pose.bone_transforms[shoulder].position[1]-120*instance.pose.position_scale/512.0);
            }
        }
        for (int mode=0;mode<5;++mode) for (unsigned sample=0;sample<sizeof(times)/sizeof(times[0]);++sample) {
            double target[3],authored_rotation[9],target_rotation[9],units;
            memset(&composition,0,sizeof(composition));
            composition.layers[0].clip=humanoid_action(mode>=2 ?
                RASTERFALL_ACTION_LOCOMOTION_WALK : RASTERFALL_ACTION_LOCOMOTION_IDLE);
            composition.layers[0].time_ms=times[sample];
            composition.layers[1].clip=humanoid_action(mode==2 || mode==4 ?
                RASTERFALL_ACTION_RIFLE_FIRE : mode==1 ?
                RASTERFALL_ACTION_RIFLE_AIM : RASTERFALL_ACTION_RIFLE_IDLE);
            composition.layers[1].time_ms=times[sample];
            if (mode==2 || mode==4) {
                composition.layers[2].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_RECOIL);
                composition.layers[2].time_ms=times[sample]%600;
            }
            GRIP_CHECK(!rasterfall_action_compose(&instance,&composition));
            /* Composition resets from the immutable definition. Scale only
             * this evaluated instance to test an equivalent authored unit. */
            if (representation==1) {
                instance.pose.position_scale*=128;
                for (unsigned i=0;i<instance.pose.bone_count;++i) {
                    instance.pose.bones[i].rest_x*=128;
                    instance.pose.bones[i].rest_y*=128;
                    instance.pose.bones[i].rest_z*=128;
                }
                for (int i=0;i<RASTERFALL_ATTACHMENT_COUNT;++i)
                    for (int axis=0;axis<3;++axis)
                        instance.pose.attachments[i].local_position[axis]*=128;
                GRIP_CHECK(!rasterfall_model_instance_update_bones(&instance));
            }
            units=instance.pose.position_scale/512.0;
            struct rasterfall_rifle_pose_input rifle={0};
            struct rasterfall_rifle_diagnostics diagnostic;
            rifle.aim_milli=mode==1 || mode==2?1000:0;
            rifle.hip_milli=mode>=3?1000:0;
            rifle.recoil_milli=(mode==2 || mode==4) && times[sample]<180?600:0;
            GRIP_CHECK(!rasterfall_rifle_pose_solve(&instance,TOY_GAME_WEAPON_AK,scale,&rifle,&diagnostic));
            GRIP_CHECK(!diagnostic.reach_clamped[0] && !diagnostic.reach_clamped[1]);
            for (int side=0;side<2;++side) {
                int hand=rasterfall_model_humanoid_bone(&instance.pose,side ?
                    RASTERFALL_HUMANOID_RIGHT_HAND : RASTERFALL_HUMANOID_LEFT_HAND);
                int elbow=instance.pose.bones[hand].parent;
                const struct rasterfall_model_bone_transform *h=&instance.pose.bone_transforms[hand];
                const struct rasterfall_model_bone_transform *e=&instance.pose.bone_transforms[elbow];
                double dot=0,length=0;
                for (int axis=0;axis<3;++axis) {
                    double v=h->position[axis]-e->position[axis];
                    length+=v*v;
                    dot+=v*h->rotation[axis*3]*(side ? -1 : 1);
                }
                dot/=sqrt(length);
                if (dot<minimum_wrist_dot) minimum_wrist_dot=dot;
                if (dot<0.57) __printf("wrist bend representation=%d mode=%d time=%d side=%d dot=%.6f\n",
                    representation,mode,times[sample],side,dot);
                GRIP_CHECK(dot>0.57); /* Less than 55 degrees; never a right-angle fold. */
                int index=rasterfall_model_find_bone(&instance.pose,side ? "RF_R_FINGER_0_1" : "RF_L_FINGER_0_1");
                int middle=rasterfall_model_find_bone(&instance.pose,side ? "RF_R_FINGER_1_2" : "RF_L_FINGER_1_2");
                GRIP_CHECK(index>=0 && middle>=0);
                const double *f=instance.pose.bone_transforms[middle].rotation;
                dot=0;
                for (int axis=0;axis<3;++axis) dot+=f[axis*3]*h->rotation[axis*3];
                GRIP_CHECK(dot<0.5); /* Fingers wrap instead of remaining a rigid open palm. */
                if (side) {
                    f=instance.pose.bone_transforms[index].rotation;dot=0;
                    for (int axis=0;axis<3;++axis) dot+=f[axis*3]*h->rotation[axis*3];
                    GRIP_CHECK(dot>0.9); /* Trigger index reaches forward separately. */
                }
            }
            GRIP_CHECK(!rasterfall_model_instance_attachment_transform(&instance,
                RASTERFALL_ATTACHMENT_FOREGRIP,&left));
            GRIP_CHECK(!modular_prepare_weapon_placement(&instance,TOY_GAME_WEAPON_AK,scale,&placement));
            modular_weapon_model_point(placement.origin,placement.rotation,
                (double[3]){foregrip.position.x,foregrip.position.y,foregrip.position.z},scale,target);
            modular_weapon_quaternion_matrix(foregrip.rotation,authored_rotation);
            rigid_matrix_multiply(placement.rotation,authored_rotation,target_rotation);
            for (int i=0;i<3;++i) {
                double error=fabs(left.position[i]/units-target[i]);
                if (error>maximum_position) maximum_position=error;
                GRIP_CHECK(error<5.0);
            }
            for (int i=0;i<9;++i) {
                double error=fabs(left.rotation[i]-target_rotation[i]);
                if (error>maximum_rotation) maximum_rotation=error;
                GRIP_CHECK(error<0.035);
            }
        }
        memset(&composition,0,sizeof(composition));
        composition.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_IDLE);
        GRIP_CHECK(!rasterfall_action_compose(&instance,&composition));
        for (unsigned i=0;i<instance.pose.bone_count;++i)
            if (strstr(instance.pose.bones[i].name,"_FINGER_"))
                GRIP_CHECK(!instance.pose.bones[i].rotate_x &&
                    !instance.pose.bones[i].rotate_y && !instance.pose.bones[i].rotate_z);
        rasterfall_model_instance_unload(&instance);
    }
    GRIP_CHECK(!constraint_failures);
done:
    rasterfall_model_instance_unload(&instance);
    rasterfall_model_resource_unload(&catalog);
    if (failure) __printf("scene grip FAIL line=%d position_rfu=%.6f rotation=%.6f\n",
        failure,maximum_position,maximum_rotation);
    else __printf("scene grip PASS constraints=594 motion_samples=120 position_rfu=%.6f rotation=%.6f minimum_wrist_dot=%.6f fingers/reset\n",
        maximum_position,maximum_rotation,minimum_wrist_dot);
#undef GRIP_CHECK
    return failure ? 1 : 0;
}
