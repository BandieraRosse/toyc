/* Shared, clock-explicit choreography. No actors, shots or targets are spawned. */
static int outpost_rifle_cycle_visible, outpost_rifle_cycle_status;
static uint64_t outpost_rifle_cycle_time_us;
struct outpost_rifle_cycle_sample {
    int z, sy, cy, phase;
    struct rf_gpu_scene_body_sample body;
};

static void outpost_rifle_cycle_sample_at(uint64_t time_ms,
    struct outpost_rifle_cycle_sample *out)
{
    static const int angles[2][5][2]={
        {{0,0},{20,-30},{-20,30},{45,0},{0,35}},
        {{0,0},{15,-25},{-15,25},{25,0},{0,-30}}};
    unsigned t=(unsigned)(time_ms%25000),reverse=(unsigned)((time_ms/25000)%2);
    int moving=t>=12000 && t<24000,turning=t>=24000;
    int at=moving?(int)t-12000:(int)t;
    int stage=turning?4:at/2400;
    int phase=turning?2400:at%2400;
    int blend=phase<450?phase*1000/450:1000;
    int previous_pitch=stage?angles[moving][stage-1][0]:0;
    int previous_yaw=stage?angles[moving][stage-1][1]:(moving?35:0);
    double turn=turning?(t-24000)/1000.0:0;
    turn=turn*turn*(3-2*turn);
    double heading=(reverse+turn)*M_PI;
    memset(out,0,sizeof(*out));
    out->z=-2200+(t<12000?0:t>=24000?4400:(int)(t-12000)*4400/12000);
    if(reverse)out->z=-out->z;
    out->sy=(int)(sin(heading)*1024);out->cy=(int)(cos(heading)*1024);
    out->phase=turning?3:moving?2:phase<800?0:1;
    out->body.armed=1;out->body.walk=moving;
    out->body.weapon=TOY_GAME_WEAPON_AK;
    out->body.lower_time_ms=(time_ms/25000)*12000+
        (t<12000?0:t>=24000?12000:t-12000);
    out->body.fire=!turning && (moving || (phase>=800 && phase<2250));
    out->body.action_time_ms=out->body.fire?(at%300):0;
    out->body.rifle.hip_milli=t<300?1000-(int)t*1000/300:
        t<12000?0:t<12300?((int)t-12000)*1000/300:1000;
    out->body.rifle.aim_milli=1000-out->body.rifle.hip_milli;
    out->body.rifle.pitch_mdeg=previous_pitch*(1000-blend)+angles[moving][stage][0]*blend;
    out->body.rifle.yaw_mdeg=previous_yaw*(1000-blend)+angles[moving][stage][1]*blend;
    if(turning) {
        out->body.rifle.pitch_mdeg=(int)(angles[1][4][0]*1000*(1-turn));
        out->body.rifle.yaw_mdeg=(int)(angles[1][4][1]*1000*(1-turn));
    }
    out->body.rifle.target_distance_rfu=8192;
    if(out->body.fire) {
        int shot=out->body.action_time_ms;
        out->body.rifle.recoil_milli=shot<35?shot*1000/35:shot<180?(180-shot)*1000/145:0;
    }
}

void rasterfall_render_set_outpost_rifle_cycle(int visible,uint64_t time_us)
{
    if(!visible || !outpost_rifle_cycle_visible)outpost_rifle_cycle_status=0;
    if(!visible && outpost_rifle_cycle_visible && !outpost_model_lab_visible && !outpost_weapon_cycle_visible)
        rf_gpu_scene_pose_body_release();
    outpost_rifle_cycle_visible=visible;
    outpost_rifle_cycle_time_us=time_us;
    struct outpost_rifle_cycle_sample sample;
    outpost_rifle_cycle_sample_at(time_us/1000,&sample);
    static const char *phases[]={"AIM","FIRE","MOVE + FIRE","TURN"};
    static const char *names[]={"BLOCK","HUMANOID","RF-C01"};
    for(int i=0;i<3;++i) {
        char channel[64],text[128];
        snprintf(channel,sizeof(channel),"rifle_cycle_lane_%d",i);
        snprintf(text,sizeof(text),"%s / %s / P%d Y%d",names[i],
            visible?phases[sample.phase]:"OFF",sample.body.rifle.pitch_mdeg/1000,
            sample.body.rifle.yaw_mdeg/1000);
        rasterfall_render_terminal_set(channel,text);
    }
}

int rasterfall_render_outpost_rifle_cycle_status(void)
{ return outpost_rifle_cycle_status; }
