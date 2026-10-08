/* Single consumer: wide bus sums, linked stereo peak protection and local mix. */
static int rf_audio_weapon_kind(int k)
{
    return k==TOY_SFX_GUNSHOT || (k>=TOY_SFX_SMG && k<=TOY_SFX_AWP) ||
        k==TOY_SFX_BOMB_EXPLODE || k==TOY_SFX_MOLOTOV_BREAK;
}
static int rf_audio_feedback_kind(int k)
{
    return k==TOY_SFX_HIT_MARKER || k==TOY_SFX_KILL || k==TOY_SFX_BITE ||
        k==TOY_SFX_PLAYER_DEATH || k==TOY_SFX_BOMB_BEEP;
}
static void rf_audio_bus_limit(int *left,int *right,int ceiling)
{
    int l=*left<0?-*left:*left,r=*right<0?-*right:*right;
    int peak=l>r?l:r;
    if(peak>ceiling*3/4) {
        int limited=rf_audio_soft_peak(peak,ceiling);
        *left=(int)((long long)*left*limited/peak);
        *right=(int)((long long)*right*limited/peak);
    }
}
static void audio_render_block(struct rasterfall_audio *a,short *pcm,int frames)
{
    int music[SFX_BLOCK_FRAMES*2],effects_pcm[SFX_BLOCK_FRAMES*2];
    short machine[SFX_BLOCK_FRAMES*2]={0},ui[SFX_BLOCK_FRAMES*2]={0};
    int target[RF_AUDIO_CONTROL_COUNT],gains[2][TOY_SFX_MOLOTOV_BREAK+1];
    unsigned range=__atomic_load_n(&a->target_range,__ATOMIC_ACQUIRE);
    /* Trim is independent from source propagation strength. Asset variants
     * with large peaks (sniper/explosion) receive additional compensation. */
    static const int trim[]={96,144,144,144,144,160,160,160,160,160,160,160,
        88,88,96,64,96,80,112};
    for(int c=0;c<RF_AUDIO_CONTROL_COUNT;++c)
        target[c]=(int)__atomic_load_n(&a->target_q8[c],__ATOMIC_ACQUIRE);
    for(int local=0;local<2;++local)for(int k=0;k<=TOY_SFX_MOLOTOV_BREAK;++k) {
        int value=trim[k]*target[RF_AUDIO_EFFECTS]/256;
        if(rf_audio_weapon_kind(k)) {
            value=value*target[RF_AUDIO_WEAPONS]/256;
            if(local && (k==TOY_SFX_GUNSHOT || (k>=TOY_SFX_SMG && k<=TOY_SFX_AWP)))
                value=value*target[RF_AUDIO_SELF]/256;
            if(range==RF_AUDIO_COMFORT)value=value*3/4;
        } else if(rf_audio_feedback_kind(k))value=value*target[RF_AUDIO_FEEDBACK]/256;
        gains[local][k]=value;
    }
    if(a->preview_remaining>0) {
        a->preview_remaining-=frames;
        if(a->preview_remaining<=0)
            toy_sfx_play_spatial(&a->sfx,TOY_SFX_RELOAD_START,-1,256,256,768,1);
    }
    toy_sfx_render_buses(&a->sfx,music,effects_pcm,frames,gains);
    rf_weaver_audio_mix(a->weaver,machine,(unsigned)frames);
    audio_ui_mix(a,ui,frames);
    for(int f=0;f<frames;++f) {
        for(int c=0;c<RF_AUDIO_CONTROL_COUNT;++c)
            a->current_q16[c]=rf_audio_gain_step(a->current_q16[c],target[c]*256);
        int env=a->current_q16[RF_AUDIO_ENVIRONMENT]/256;
        env=env*(a->current_q16[RF_AUDIO_EFFECTS]/256)/256;
        int l=effects_pcm[f*2]+machine[f*2]*env/256;
        int r=effects_pcm[f*2+1]+machine[f*2+1]*env/256;
        rf_audio_bus_limit(&l,&r,range==RF_AUDIO_WIDE?28000:
            range==RF_AUDIO_COMFORT?10000:18000);
        int mg=a->current_q16[RF_AUDIO_MUSIC]/256;
        int ug=a->current_q16[RF_AUDIO_UI]/256;
        l+=music[f*2]*mg/512+ui[f*2]*ug/256;
        r+=music[f*2+1]*mg/512+ui[f*2+1]*ug/256;
        rf_audio_bus_limit(&l,&r,30000);
        /* The output knob follows mastering, so lowering it always lowers
         * every bus by the same factor. Smoothing reaches exact zero. */
        pcm[f*2]=(short)((long long)l*a->current_q16[RF_AUDIO_MASTER]/65536);
        pcm[f*2+1]=(short)((long long)r*a->current_q16[RF_AUDIO_MASTER]/65536);
    }
}
