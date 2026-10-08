#include "tlibc_everything.h"
#include "core.h"
#include "toy_audio.h"
#include "toy_assets.h"
#include "toy_game.h"
#include "rasterfall_audio.h"

#define SFX_BLOCK_FRAMES 512

#include "rasterfall_audio_weaver.inc"
#include "rasterfall_audio_settings.inc"

static const char *sfx_asset_names[TOY_SFX_MOLOTOV_BREAK + 1] = {
    "gunshot", "dry_fire", "reload_start", "reload_done",
    "hit_marker", "kill", "bite", "death", "shove", "shove_hit",
    "melee", "melee_hit", "smg", "shotgun", "ak", "awp", "bomb_beep",
    "bomb_explode", "molotov_break",
};

void rasterfall_audio_load_assets(struct rasterfall_audio *audio)
{
    int kind, loaded = 0;
    for (kind = 0; kind <= TOY_SFX_MOLOTOV_BREAK; kind++) {
        char path[96];
        snprintf(path, sizeof(path), "rasterfall/assets/audio/sfx_%s.tsnd",
                 sfx_asset_names[kind]);
        if (toy_sound_load(path, &audio->assets[kind]) == 0) loaded++;
    }
    __printf("rasterfall: sound assets %d/%d loaded\n",
             loaded, TOY_SFX_MOLOTOV_BREAK + 1);
}

static void audio_post_command(struct rasterfall_audio *a,const struct rf_audio_event *event)
{
    if(!a || !a->running)return;
    unsigned wp=__atomic_load_n(&a->event_wpos,__ATOMIC_RELAXED);
    unsigned used=wp-__atomic_load_n(&a->event_rpos,__ATOMIC_ACQUIRE);
    /* Leave a reserve for local feedback/UI when many world guns fire. */
    if(used>=RASTERFALL_AUDIO_EVENT_RING ||
        (!event->local && event->kind<64 && used>=RASTERFALL_AUDIO_EVENT_RING-32))return;
    a->events[wp&(RASTERFALL_AUDIO_EVENT_RING-1)]=*event;
    __atomic_store_n(&a->event_wpos,wp+1,__ATOMIC_RELEASE);
}
static void audio_post_event(struct rasterfall_audio *a,int kind,int local)
{
    struct rf_audio_event e={0};
    e.kind=kind;e.source_id=local?-1:-2;e.left_q8=e.right_q8=256;
    e.priority=local?768:128;e.local=local;
    e.world_generation=__atomic_load_n(&a->world_generation,__ATOMIC_RELAXED);
    audio_post_command(a,&e);
}

static void audio_drain_events(struct rasterfall_audio *audio)
{
    unsigned generation=__atomic_load_n(&audio->world_generation,__ATOMIC_ACQUIRE);
    if(generation!=audio->consumer_generation) {
        for(int i=0;i<TOY_SFX_MAX_VOICES;++i)audio->sfx.voices[i].active=0;
        audio->ui_remaining=audio->preview_remaining=0;audio->consumer_generation=generation;
    }
    unsigned rp=__atomic_load_n(&audio->event_rpos,__ATOMIC_RELAXED);
    unsigned end=__atomic_load_n(&audio->event_wpos,__ATOMIC_ACQUIRE);
    while(rp!=end) {
        struct rf_audio_event e=audio->events[rp&(RASTERFALL_AUDIO_EVENT_RING-1)];
        __atomic_store_n(&audio->event_rpos,++rp,__ATOMIC_RELEASE);
        if(e.world_generation!=generation)continue;
        int kind=e.kind;
        if(kind>=64 && kind<=68) {
            static const int durations[]={24,38,60,110,85};
            audio->ui_kind=kind-64;
            audio->ui_length=audio->ui_remaining=durations[kind-64]*TOY_SFX_RATE/1000;
            audio->ui_phase=0;
        } else if(kind==69) {
            toy_sfx_play_spatial(&audio->sfx,TOY_SFX_AK,-1,256,256,768,1);
            audio->preview_remaining=TOY_SFX_RATE/3;
        } else toy_sfx_play_spatial(&audio->sfx,kind,e.source_id,
            e.left_q8,e.right_q8,e.priority,e.local);
    }
}

void rasterfall_audio_ui(struct rasterfall_audio *audio,int kind)
{
    if(audio && audio->running && kind>=0 && kind<=RF_UI_SOUND_EXIT)
        audio_post_event(audio,64+kind,1);
}

void rasterfall_audio_preview(struct rasterfall_audio *audio)
{
    if(audio && audio->running)audio_post_event(audio,69,1);
}

static void audio_ui_mix(struct rasterfall_audio *a,short *pcm,int frames)
{
    static const int tones[]={1200,720,960,660,880};
    for(int i=0;i<frames && a->ui_remaining>0;++i) {
        int elapsed=a->ui_length-a->ui_remaining;
        int frequency=tones[a->ui_kind];
        if(a->ui_kind==RF_UI_SOUND_CONFIRM && elapsed>a->ui_length/2)frequency=990;
        if(a->ui_kind==RF_UI_SOUND_EXIT && elapsed>a->ui_length/2)frequency=550;
        a->ui_phase=(a->ui_phase+frequency*65536/TOY_SFX_RATE)&65535;
        int wave=a->ui_phase<32768?a->ui_phase*2-32768:98304-a->ui_phase*2;
        int envelope=a->ui_remaining*900/a->ui_length;
        int attack=TOY_SFX_RATE/200;
        if(elapsed<attack)envelope=envelope*elapsed/attack;
        int sample=wave*envelope/32768;
        for(int c=0;c<2;++c) {
            int value=pcm[i*2+c]+sample;
            pcm[i*2+c]=(short)(value>32767?32767:value<-32768?-32768:value);
        }
        --a->ui_remaining;
    }
}

#include "rasterfall_audio_engine.inc"

static void *audio_thread_func(void *arg)
{
    struct rasterfall_audio *audio = (struct rasterfall_audio *)arg;
    short play_buf[SFX_BLOCK_FRAMES * 2];
    while (!__atomic_load_n(&audio->quit, __ATOMIC_ACQUIRE)) {
        long ret;
        audio_drain_events(audio);
        audio_render_block(audio,play_buf,SFX_BLOCK_FRAMES);
        ret = toy_audio_write(audio->output, play_buf, SFX_BLOCK_FRAMES);
        if (ret < 0) break;
    }
    return NULL;
}

int rasterfall_audio_start(struct rasterfall_audio *audio,
                           struct toy_audio *output)
{
    int kind;
    if (!audio || !output || audio->running) return -1;
    if(!audio->settings_initialized)rasterfall_audio_settings_init(audio,0);
    audio->output = output;
    for(int c=0;c<RF_AUDIO_CONTROL_COUNT;++c)
        audio->current_q16[c]=(int)__atomic_load_n(&audio->target_q8[c],__ATOMIC_RELAXED)*256;
    audio->consumer_generation=__atomic_load_n(&audio->world_generation,__ATOMIC_RELAXED);
    audio->ui_remaining=audio->preview_remaining=0;
    audio->event_rpos=audio->event_wpos=0;
    __atomic_store_n(&audio->quit, 0, __ATOMIC_RELEASE);
    toy_sfx_init(&audio->sfx, TOY_SFX_RATE);
    for (kind = 0; kind <= TOY_SFX_MOLOTOV_BREAK; kind++)
        if (audio->assets[kind].blob)
            toy_sfx_set_sample(&audio->sfx, kind,
                               (const short *)audio->assets[kind].data,
                               audio->assets[kind].frames);
    toy_sfx_music(&audio->sfx, 1);
    audio->weaver = rf_weaver_audio_create(output->rate);
    if (pthread_create(&audio->thread, NULL, audio_thread_func, audio) != 0) {
        rf_weaver_audio_destroy(audio->weaver);
        audio->weaver = NULL;
        audio->output = NULL;
        return -1;
    }
    audio->running = 1;
    __printf("rasterfall: audio backend: %s\n",
             toy_audio_backend_name(audio->output));
    return 0;
}

void rasterfall_audio_stop(struct rasterfall_audio *audio)
{
    if (!audio->running) return;
    __atomic_store_n(&audio->quit, 1, __ATOMIC_RELEASE);
    pthread_join(audio->thread, NULL);
    rf_weaver_audio_destroy(audio->weaver);
    audio->weaver = NULL;
    audio->output = NULL;
    audio->running = 0;
}

void rasterfall_audio_weaver(struct rasterfall_audio *audio,
    const struct rasterfall_weaver_audio_input *input)
{
    if (audio && audio->running) rf_weaver_audio_publish(audio->weaver, input);
}

static int audio_event_kind(unsigned char event)
{
    switch(event) {
    case TOY_GAME_EV_SHOOT: return TOY_SFX_GUNSHOT;
    case TOY_GAME_EV_DRY_FIRE: return TOY_SFX_DRY_FIRE;
    case TOY_GAME_EV_RELOAD_START: return TOY_SFX_RELOAD_START;
    case TOY_GAME_EV_RELOAD_DONE: return TOY_SFX_RELOAD_DONE;
    case TOY_GAME_EV_KILL: return TOY_SFX_KILL;
    case TOY_GAME_EV_BITE: return TOY_SFX_BITE;
    case TOY_GAME_EV_PLAYER_DEATH: return TOY_SFX_PLAYER_DEATH;
    case TOY_GAME_EV_SHOVE: return TOY_SFX_SHOVE;
    case TOY_GAME_EV_SHOVE_HIT: return TOY_SFX_SHOVE_HIT;
    case TOY_GAME_EV_MELEE: return TOY_SFX_MELEE;
    case TOY_GAME_EV_MELEE_HIT: return TOY_SFX_MELEE_HIT;
    case TOY_GAME_EV_SHOOT_SMG: return TOY_SFX_SMG;
    case TOY_GAME_EV_SHOOT_SHOTGUN: return TOY_SFX_SHOTGUN;
    case TOY_GAME_EV_SHOOT_AK: return TOY_SFX_AK;
    case TOY_GAME_EV_SHOOT_AWP: return TOY_SFX_AWP;
    case TOY_GAME_EV_BOMB_BEEP: return TOY_SFX_BOMB_BEEP;
    case TOY_GAME_EV_BOMB_EXPLODE: return TOY_SFX_BOMB_EXPLODE;
    case TOY_GAME_EV_MOLOTOV_BREAK: return TOY_SFX_MOLOTOV_BREAK;
    default: return -1;
    }
}

static void audio_play_events(struct rasterfall_audio *a,const unsigned char *events,int count,int local)
{
    if(!a || !a->running || !events)return;
    if(count>TOY_GAME_MAX_EVENTS)count=TOY_GAME_MAX_EVENTS;
    for(int i=0;i<count;++i) {
        int k=audio_event_kind(events[i]);
        if(k>=0)audio_post_event(a,k,local);
    }
}
void rasterfall_audio_play_events(struct rasterfall_audio *a,const unsigned char *events,int count)
{ audio_play_events(a,events,count,1); }
void rasterfall_audio_play_remote_events(struct rasterfall_audio *a,const unsigned char *events,int count)
{
    /* Remote shots already arrive through deduplicated positioned actor fire.
     * The aggregate protocol has no source position; retain other feedback. */
    if(!a || !a->running || !events)return;
    if(count>TOY_GAME_MAX_EVENTS)count=TOY_GAME_MAX_EVENTS;
    for(int i=0;i<count;++i) {
        int k=audio_event_kind(events[i]);
        if(k>=0 && k!=TOY_SFX_GUNSHOT && !(k>=TOY_SFX_SMG && k<=TOY_SFX_AWP))
            audio_post_event(a,k,0);
    }
}
void rasterfall_audio_play_world(struct rasterfall_audio *a,unsigned char event,
    int x,int y,int z,int source_id)
{
    if(!a || !a->running)return;
    struct rf_audio_event e={0};struct rf_audio_spatial spatial;
    e.kind=audio_event_kind(event);
    if(e.kind<0)return;
    int far=rf_audio_weapon_kind(e.kind)?24576:4096;
    if(e.kind==TOY_SFX_AWP || e.kind==TOY_SFX_BOMB_EXPLODE)far=32768;
    rf_audio_spatial_gain(&spatial,x-a->listener_x,y-a->listener_y,z-a->listener_z,
        a->listener_sy,a->listener_cy,1024,far);
    e.source_id=source_id;e.left_q8=spatial.left_q8;e.right_q8=spatial.right_q8;
    e.priority=spatial.left_q8>spatial.right_q8?spatial.left_q8:spatial.right_q8;
    if(rf_audio_feedback_kind(e.kind))e.priority+=256;
    e.world_generation=__atomic_load_n(&a->world_generation,__ATOMIC_RELAXED);
    audio_post_command(a,&e);
}
void rasterfall_audio_listener(struct rasterfall_audio *a,
    int x,int y,int z,int sy,int cy,unsigned generation)
{
    a->listener_x=x;a->listener_y=y;a->listener_z=z;
    a->listener_sy=sy;a->listener_cy=cy;
    __atomic_store_n(&a->world_generation,generation,__ATOMIC_RELEASE);
}
void rasterfall_audio_settings_apply(struct rasterfall_audio *a,int save)
{
    for(int c=0;c<RF_AUDIO_CONTROL_COUNT;++c)
        __atomic_store_n(&a->target_q8[c],rf_audio_volume_q8(a->settings.volume[c]),__ATOMIC_RELEASE);
    __atomic_store_n(&a->target_range,(unsigned)a->settings.range,__ATOMIC_RELEASE);
    if(save && a->persistence) {
        a->save_error=rf_audio_settings_save(&a->settings,a->settings_path)<0;
        if(a->save_error)__printf("rasterfall: audio settings save failed: %s\n",a->settings_path);
    }
}
void rasterfall_audio_settings_init(struct rasterfall_audio *a,int persistence)
{
    rf_audio_settings_default(&a->settings);
    a->settings_path=getenv("RF_AUDIO_SAVE_PATH");
    if(!a->settings_path)a->settings_path="rasterfall_audio.cfg";
    a->persistence=(persistence || getenv("RF_AUDIO_SAVE_PATH")) &&
        !getenv("RF_AUDIO_NO_SAVE");
    if(a->persistence) {
        if(rf_audio_settings_load(&a->settings,a->settings_path)<0) {
            int fd=__openat(AT_FDCWD,a->settings_path,O_RDONLY,0);
            if(fd>=0){__close(fd);a->save_error=1;
                __printf("rasterfall: invalid audio settings, using defaults\n");}
        }
    }
    a->settings_initialized=1;
    rasterfall_audio_settings_apply(a,0);
}

void rasterfall_audio_unload_assets(struct rasterfall_audio *audio)
{
    int kind;
    for (kind = 0; kind <= TOY_SFX_MOLOTOV_BREAK; kind++)
        if (audio->assets[kind].blob) toy_sound_unload(&audio->assets[kind]);
}
