/* Exercise the actual presentation mixer without opening a device or window. */
#include "../rasterfall/src/rasterfall_audio.c"

/* Refuse the output boundary: this test only renders PCM in memory. */
long toy_audio_write(struct toy_audio *audio,const void *pcm,unsigned long frames)
{ (void)audio;(void)pcm;(void)frames;abort();return -1; }
const char *toy_audio_backend_name(const struct toy_audio *audio)
{ (void)audio;return "PCM contract test"; }

#define CHECK(test) do {if(!(test)){printf("AUDIO FAIL line=%d\n",__LINE__);return 1;}} while(0)
static short block[SFX_BLOCK_FRAMES*2],sample[44100];
static void prepare(struct rasterfall_audio *a)
{
    memset(a,0,sizeof(*a));rasterfall_audio_settings_init(a,0);
    toy_sfx_init(&a->sfx,TOY_SFX_RATE);a->running=1;
    for(int i=0;i<44100;++i)sample[i]=(short)(i%2?30000:-30000);
    for(int k=0;k<=TOY_SFX_MOLOTOV_BREAK;++k)toy_sfx_set_sample(&a->sfx,k,sample,44100);
    for(int i=0;i<100;++i)audio_render_block(a,block,SFX_BLOCK_FRAMES);
}
static long long energy(const short *pcm,int channel)
{
    long long result=0;
    for(int i=0;i<SFX_BLOCK_FRAMES;++i) {int n=pcm[i*2+channel];result+=(long long)n*n;}
    return result;
}
static void settle(struct rasterfall_audio *a)
{for(int i=0;i<100;++i)audio_render_block(a,block,SFX_BLOCK_FRAMES);}
static int mix_contracts(void)
{
    struct rasterfall_audio a,b;prepare(&a);prepare(&b);
    unsigned char gun=TOY_GAME_EV_SHOOT_AK;
    rasterfall_audio_play_events(&a,&gun,1);audio_drain_events(&a);
    audio_render_block(&a,block,SFX_BLOCK_FRAMES);long long normal=energy(block,0);CHECK(normal>0);
    b.settings.volume[RF_AUDIO_SELF]=0;rasterfall_audio_settings_apply(&b,0);settle(&b);
    rasterfall_audio_play_events(&b,&gun,1);audio_drain_events(&b);
    audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(!energy(block,0) && !energy(block,1));
    rasterfall_audio_play_world(&b,gun,512,0,0,7);audio_drain_events(&b);
    audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(energy(block,0)>0);
    prepare(&b);b.settings.volume[RF_AUDIO_WEAPONS]=50;rasterfall_audio_settings_apply(&b,0);settle(&b);
    rasterfall_audio_play_events(&b,&gun,1);audio_drain_events(&b);
    audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(energy(block,0)<normal/8);
    unsigned char reload=TOY_GAME_EV_RELOAD_DONE;
    prepare(&b);b.settings.volume[RF_AUDIO_WEAPONS]=0;rasterfall_audio_settings_apply(&b,0);settle(&b);
    rasterfall_audio_play_events(&b,&reload,1);audio_drain_events(&b);
    audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(energy(block,0)>0);
    const unsigned char weapons[]={TOY_GAME_EV_MELEE,TOY_GAME_EV_MELEE_HIT,TOY_GAME_EV_BOMB_EXPLODE};
    for(unsigned i=0;i<sizeof(weapons);++i) {
        unsigned char event=weapons[i];
        prepare(&b);b.settings.volume[RF_AUDIO_SELF]=0;
        rasterfall_audio_settings_apply(&b,0);settle(&b);
        rasterfall_audio_play_events(&b,&event,1);audio_drain_events(&b);
        audio_render_block(&b,block,SFX_BLOCK_FRAMES);long long full=energy(block,0);CHECK(full>0);
        prepare(&b);b.settings.volume[RF_AUDIO_WEAPONS]=50;
        rasterfall_audio_settings_apply(&b,0);settle(&b);
        rasterfall_audio_play_events(&b,&event,1);audio_drain_events(&b);
        audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(energy(block,0)>0 && energy(block,0)<full/8);
        prepare(&b);b.settings.volume[RF_AUDIO_WEAPONS]=0;
        rasterfall_audio_settings_apply(&b,0);settle(&b);
        rasterfall_audio_play_events(&b,&event,1);audio_drain_events(&b);
        audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(!energy(block,0) && !energy(block,1));
    }
    prepare(&b);b.listener_cy=1024;
    rasterfall_audio_play_world(&b,gun,512,0,0,7);audio_drain_events(&b);
    audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(energy(block,1)>energy(block,0));
    prepare(&b);b.settings.volume[RF_AUDIO_EFFECTS]=0;rasterfall_audio_settings_apply(&b,0);settle(&b);
    rasterfall_audio_ui(&b,RF_UI_SOUND_CONFIRM);audio_drain_events(&b);
    audio_render_block(&b,block,SFX_BLOCK_FRAMES);CHECK(energy(block,0)>0);
    toy_sfx_music(&b.sfx,1);b.settings.volume[RF_AUDIO_MASTER]=0;
    rasterfall_audio_settings_apply(&b,0);settle(&b);
    rasterfall_audio_ui(&b,RF_UI_SOUND_CONFIRM);rasterfall_audio_play_events(&b,&reload,1);
    audio_drain_events(&b);audio_render_block(&b,block,SFX_BLOCK_FRAMES);
    CHECK(!energy(block,0) && !energy(block,1));
    /* Every bus, including the existing machine, follows effects/environment. */
    prepare(&b);b.weaver=rf_weaver_audio_create(TOY_SFX_RATE);CHECK(b.weaver);
    struct rasterfall_weaver_audio_input machine={0};
    machine.enabled=1;machine.world_generation=1;machine.job_serial=1;
    machine.phase=TOY_WEAVER_WEAVING;machine.left_q8=machine.right_q8=256;
    rf_weaver_audio_publish(b.weaver,&machine);settle(&b);CHECK(energy(block,0)>0);
    b.settings.volume[RF_AUDIO_ENVIRONMENT]=0;rasterfall_audio_settings_apply(&b,0);settle(&b);
    CHECK(!energy(block,0));
    b.settings.volume[RF_AUDIO_ENVIRONMENT]=100;b.settings.volume[RF_AUDIO_EFFECTS]=0;
    rasterfall_audio_settings_apply(&b,0);settle(&b);CHECK(!energy(block,0));
    rf_weaver_audio_destroy(b.weaver);
    return 0;
}
static int queue_contracts(void)
{
    struct rasterfall_audio a;prepare(&a);
    unsigned char events[8];memset(events,TOY_GAME_EV_RELOAD_DONE,sizeof(events));
    rasterfall_audio_play_events(&a,events,8);CHECK(a.event_wpos==8);
    rasterfall_audio_listener(&a,0,0,0,0,1024,2);
    audio_drain_events(&a);
    for(int i=0;i<TOY_SFX_MAX_VOICES;++i)CHECK(!a.sfx.voices[i].active);
    unsigned char gun=TOY_GAME_EV_SHOOT_AK;
    for(int n=0;n<8;++n) {
        rasterfall_audio_play_world(&a,gun,512,0,0,7);audio_drain_events(&a);
        audio_render_block(&a,block,SFX_BLOCK_FRAMES);
    }
    int count=0;for(int i=0;i<TOY_SFX_MAX_VOICES;++i)count+=a.sfx.voices[i].active;
    CHECK(count==2);
    for(int i=0;i<TOY_SFX_MAX_VOICES;++i)toy_sfx_play_spatial(&a.sfx,TOY_SFX_RELOAD_DONE,i,256,256,768,1);
    toy_sfx_play_spatial(&a.sfx,TOY_SFX_AK,90,1,1,1,0);
    for(int i=0;i<TOY_SFX_MAX_VOICES;++i)CHECK(a.sfx.voices[i].priority==768);
    /* Aggregate network shots must not duplicate positioned actor fire. */
    unsigned before=a.event_wpos;rasterfall_audio_play_remote_events(&a,&gun,1);
    CHECK(a.event_wpos==before);
    return 0;
}
static int settings_contracts(const char *path)
{
    struct rf_audio_settings s,t;rf_audio_settings_default(&s);
    CHECK(rf_audio_volume_q8(0)==0 && rf_audio_volume_q8(100)==256);
    for(int i=1;i<=100;++i)CHECK(rf_audio_volume_q8(i)>=rf_audio_volume_q8(i-1));
    s.volume[RF_AUDIO_MASTER]=37;s.volume[RF_AUDIO_SELF]=0;s.range=RF_AUDIO_COMFORT;
    CHECK(!rf_audio_settings_save(&s,path));rf_audio_settings_default(&t);
    CHECK(!rf_audio_settings_load(&t,path) && !memcmp(&s,&t,sizeof(s)));
    s.volume[RF_AUDIO_MASTER]=81;CHECK(!rf_audio_settings_save(&s,path));
    CHECK(!rf_audio_settings_load(&t,path) && t.volume[RF_AUDIO_MASTER]==81);
    struct rf_audio_settings before=t;
    CHECK(rf_audio_settings_decode("RFAUDIO 1 2 90 100 100 100 100 100 100 999",&t)<0);
    CHECK(!memcmp(&t,&before,sizeof(t)));
    CHECK(rf_audio_settings_decode("RFAUDIO 1 9 100 100 100 100 100 100 100 100",&t)<0);
    CHECK(rf_audio_settings_decode("RFAUDIO 1 1 100 100 100 100 100 100 100 100 junk",&t)<0);
    struct rf_audio_spatial near,far,left,right;
    rf_audio_spatial_gain(&near,0,0,0,0,1024,1024,24576);
    rf_audio_spatial_gain(&far,0,0,24576,0,1024,1024,24576);
    rf_audio_spatial_gain(&left,-512,0,0,0,1024,1024,24576);
    rf_audio_spatial_gain(&right,512,0,0,0,1024,1024,24576);
    CHECK(near.left_q8==256 && near.right_q8==256 && !far.left_q8 && !far.right_q8);
    CHECK(left.left_q8==right.right_q8 && left.right_q8==right.left_q8 && left.left_q8>left.right_q8);
    return 0;
}
static int peak_contracts(void)
{
    struct rasterfall_audio a;prepare(&a);toy_sfx_music(&a.sfx,1);
    for(int range=RF_AUDIO_WIDE;range<=RF_AUDIO_COMFORT;++range) {
        a.settings.range=range;rasterfall_audio_settings_apply(&a,0);settle(&a);
        for(int i=0;i<TOY_SFX_MAX_VOICES;++i)toy_sfx_play_spatial(&a.sfx,TOY_SFX_BOMB_EXPLODE,i,256,256,256,0);
        rasterfall_audio_ui(&a,RF_UI_SOUND_CONFIRM);audio_drain_events(&a);
        audio_render_block(&a,block,SFX_BLOCK_FRAMES);
        for(int i=0;i<SFX_BLOCK_FRAMES*2;++i)CHECK(block[i]>-30000 && block[i]<30000);
    }
    return 0;
}
static int asset_balance(const char *package_root)
{
    struct rasterfall_audio a={0};struct toy_sfx reference;
    rasterfall_audio_settings_init(&a,0);
    toy_sfx_init(&a.sfx,TOY_SFX_RATE);toy_sfx_init(&reference,TOY_SFX_RATE);
    const int kinds[]={TOY_SFX_GUNSHOT,TOY_SFX_SMG,TOY_SFX_SHOTGUN,TOY_SFX_AK,TOY_SFX_AWP,
        TOY_SFX_MELEE,TOY_SFX_MELEE_HIT,TOY_SFX_BOMB_EXPLODE};
    for(int k=0;k<=TOY_SFX_MOLOTOV_BREAK;++k) {
        char path[1024];
        snprintf(path,sizeof(path),"%s/rasterfall/assets/audio/sfx_%s.tsnd",package_root,sfx_asset_names[k]);
        if(!toy_sound_load(path,&a.assets[k])) {
            toy_sfx_set_sample(&a.sfx,k,(const short *)a.assets[k].data,a.assets[k].frames);
            toy_sfx_set_sample(&reference,k,(const short *)a.assets[k].data,a.assets[k].frames);
        }
    }
    CHECK(a.assets[TOY_SFX_GUNSHOT].blob);
    settle(&a);
    for(unsigned n=0;n<sizeof(kinds)/sizeof(kinds[0]);++n) {
        int k=kinds[n],old_peak=0,new_peak=0;long long old_energy=0,new_energy=0;
        toy_sfx_play(&reference,k);toy_sfx_play_spatial(&a.sfx,k,-1,256,256,768,1);
        for(int chunk=0;chunk<30;++chunk) {
            short old[SFX_BLOCK_FRAMES*2];
            toy_sfx_render_gained(&reference,old,SFX_BLOCK_FRAMES,192,128,28000);
            audio_render_block(&a,block,SFX_BLOCK_FRAMES);
            old_energy+=energy(old,0);new_energy+=energy(block,0);
            for(int i=0;i<SFX_BLOCK_FRAMES*2;++i) {
                int x=old[i]<0?-old[i]:old[i],y=block[i]<0?-block[i]:block[i];
                if(x>old_peak)old_peak=x;
                if(y>new_peak)new_peak=y;
            }
        }
        CHECK(old_energy>0 && new_energy<old_energy && new_peak<old_peak);
        printf("AUDIO %s source=%s old_peak=%d new_peak=%d energy_percent=%lld\n",
            sfx_asset_names[k],a.assets[k].blob?"asset":"synthesis",old_peak,new_peak,new_energy*100/old_energy);
    }
    rasterfall_audio_unload_assets(&a);return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==3);
    CHECK(!settings_contracts(argv[1]));CHECK(!mix_contracts());
    CHECK(!queue_contracts());CHECK(!peak_contracts());CHECK(!asset_balance(argv[2]));
    puts("AUDIO PASS: settings replacement/rejection, independent gains, exact mute, stereo, overlap, priority, world reset, bounded peaks");
    return 0;
}
