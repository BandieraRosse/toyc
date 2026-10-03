/* Fifteen independent samples of the production pose evaluator. The owner
 * supplies one frozen time; extraction never advances a clock or an actor. */
static int outpost_weapon_cycle_status;
static uint64_t outpost_weapon_cycle_time_us;

static void outpost_weapon_cycle_sample_at(uint64_t time_ms,int weapon,
    struct outpost_rifle_cycle_sample *out)
{
    unsigned t=(unsigned)(time_ms%60000);
    outpost_rifle_cycle_sample_at(t<8000?0:t<58000?t-8000:0,out);
    out->z/=4;
    out->body.weapon=weapon;
    if(t<8000 || t>=58000) {
        memset(&out->body.rifle,0,sizeof(out->body.rifle));
        out->phase=4;out->body.walk=out->body.fire=0;
        out->body.action_time_ms=0;
        int blend=t<7500?1000:t<8000?(8000-(int)t)*2:t<58500?((int)t-58000)*2:1000;
        out->body.rifle.idle_milli[RASTERFALL_RIFLE_IDLE_LOW]=blend;
        out->body.rifle.hip_milli=1000-blend;
    } else if(out->body.fire) {
        int interval=toy_game_weapon_info(weapon)->cooldown_ms;
        int shot=(int)((t-8000)%interval);
        out->body.action_time_ms=shot;
        out->body.rifle.recoil_milli=rasterfall_rifle_recoil_sample(weapon,shot);
    }
}

void rasterfall_render_set_outpost_weapon_cycle(int visible,uint64_t time_us)
{
    if(!visible || !outpost_weapon_cycle_visible)outpost_weapon_cycle_status=0;
    if(!visible && outpost_weapon_cycle_visible && !outpost_model_lab_visible && !outpost_rifle_cycle_visible)
        rf_gpu_scene_pose_body_release();
    outpost_weapon_cycle_visible=visible;outpost_weapon_cycle_time_us=time_us;
    static const char *names[]={"BLOCK","HUMANOID","RF-C01"};
    static const char *weapons[]={"PISTOL","SMG","SHOTGUN","AK","AWP"};
    static const char *phases[]={"AIM","FIRE","MOVE FIRE","TURN","LOW READY"};
    for(int row=0;row<3;++row)for(int w=0;w<5;++w) {
        struct outpost_rifle_cycle_sample sample;
        char channel[64],text[128];
        outpost_weapon_cycle_sample_at(time_us/1000,w,&sample);
        snprintf(channel,sizeof(channel),"weapon_cycle_station_%d",row*5+w);
        snprintf(text,sizeof(text),"%s / %s / %s",names[row],weapons[w],
            row==2 && outpost_weapon_cycle_status<0?"NO ASSET":visible?phases[sample.phase]:"OFF");
        rasterfall_render_terminal_set(channel,text);
    }
}

int rasterfall_render_outpost_weapon_cycle_status(void)
{ return outpost_weapon_cycle_status; }
