/* Original procedural machine PCM and its private, single-consumer bus.
 * Included by rasterfall_audio.c and the standalone CPU contract test.
 * Allocation/synthesis and publish run on the main thread; only mix mutates
 * voices, sample cursors and gain envelopes. No Game or platform audio calls. */
#include "rasterfall_weaver_audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RF_WEAVER_CUE_RING 16u
#define RF_WEAVER_LOOP_RUNNING (1u << 18)
enum rf_weaver_audio_cue { RF_WEAVER_START, RF_WEAVER_PAUSE, RF_WEAVER_RESUME, RF_WEAVER_DONE, RF_WEAVER_CUE_COUNT };
struct rf_weaver_audio_pcm { short *samples; unsigned frames; };
struct rf_weaver_audio_command { unsigned epoch,kind; };
struct rf_weaver_audio_voice { unsigned kind,cursor; int active; };
struct rasterfall_weaver_audio_bus {
    unsigned rate,fade_frames;
    short *storage;
    struct rf_weaver_audio_pcm loop,cues[RF_WEAVER_CUE_COUNT];
    /* Complete latest state: high 32 bits epoch, low bits L/R Q8 and run.
     * Unlike a bounded event ring, a stop can never be dropped. */
    uint64_t target;
    unsigned write_pos,read_pos;
    struct rf_weaver_audio_command commands[RF_WEAVER_CUE_RING];
    /* Producer owns observed/producer_epoch/dropped. */
    struct rasterfall_weaver_audio_input observed;
    unsigned producer_epoch,dropped;
    int observed_valid;
    /* Consumer owns all following fields. */
    unsigned consumer_epoch,loop_cursor;
    int gain[3],goal[3];
    unsigned remaining[3];
    struct rf_weaver_audio_voice voices[2];
    unsigned played[RF_WEAVER_CUE_COUNT];
};

static short rf_weaver_pcm_sample(double value)
{
    if(value>32767.0)value=32767.0;
    if(value< -32768.0)value=-32768.0;
    return (short)value;
}
static void rf_weaver_audio_synthesize(struct rasterfall_weaver_audio_bus *b)
{
    const double tau=6.28318530717958647693;
    for(unsigned i=0;i<b->loop.frames;++i) {
        double t=(double)i/b->rate;
        /* Integral cycles make the one-second loop continuous at its seam. */
        double mod=0.84+0.16*sin(tau*8.0*t);
        double tone=sin(tau*120.0*t)+0.24*sin(tau*240.0*t)+0.10*sin(tau*480.0*t);
        b->loop.samples[i]=rf_weaver_pcm_sample(560.0*mod*tone);
    }
    for(unsigned kind=0;kind<RF_WEAVER_CUE_COUNT;++kind) {
        struct rf_weaver_audio_pcm *pcm=&b->cues[kind];
        double duration=(double)(pcm->frames-1)/b->rate;
        for(unsigned i=0;i<pcm->frames;++i) {
            double t=(double)i/b->rate,u=t/duration;
            double envelope=sin(tau*0.5*u);envelope*=envelope;
            double sample;
            if(kind==RF_WEAVER_DONE) {
                /* Two quiet, overlapping original completion tones. */
                double a=t<0.16 ? sin(tau*0.5*t/0.16) : 0.0;
                double c=t>=0.08 ? sin(tau*0.5*(t-0.08)/(duration-0.08)) : 0.0;
                sample=1150.0*(a*a*sin(tau*392.0*t)+c*c*sin(tau*523.25*t));
            } else {
                double first=kind==RF_WEAVER_START ? 135.0 : 180.0;
                double last=kind==RF_WEAVER_PAUSE ? 75.0 : kind==RF_WEAVER_START ? 300.0 : 240.0;
                double phase=tau*(first*t+0.5*(last-first)*t*t/duration);
                sample=(kind==RF_WEAVER_START ? 1300.0 : 900.0)*envelope*
                    (sin(phase)+0.12*sin(2.0*phase));
            }
            pcm->samples[i]=rf_weaver_pcm_sample(sample);
        }
        pcm->samples[0]=pcm->samples[pcm->frames-1]=0;
    }
}
static struct rasterfall_weaver_audio_bus *rf_weaver_audio_create(unsigned rate)
{
    static const unsigned durations[RF_WEAVER_CUE_COUNT]={140,90,80,240};
    if(rate<8000 || rate>192000) return NULL;
    struct rasterfall_weaver_audio_bus *b=calloc(1,sizeof(*b));
    if(!b)return NULL;
    b->rate=rate;b->fade_frames=rate*60/1000;b->loop.frames=rate;
    unsigned total=rate;
    for(unsigned k=0;k<RF_WEAVER_CUE_COUNT;++k) {
        b->cues[k].frames=rate*durations[k]/1000;
        total+=b->cues[k].frames;
    }
    b->storage=malloc((size_t)total*sizeof(*b->storage));
    if(!b->storage){free(b);return NULL;}
    b->loop.samples=b->storage;unsigned offset=rate;
    for(unsigned k=0;k<RF_WEAVER_CUE_COUNT;++k) {
        b->cues[k].samples=b->storage+offset;offset+=b->cues[k].frames;
    }
    rf_weaver_audio_synthesize(b);return b;
}
static void rf_weaver_audio_destroy(struct rasterfall_weaver_audio_bus *b)
{
    if(b){free(b->storage);free(b);}
}
static int rf_weaver_audio_busy(const struct rasterfall_weaver_audio_input *in)
{
    return in->enabled && in->job_serial && in->phase>=TOY_WEAVER_CALIBRATING &&
        in->phase<=TOY_WEAVER_DELIVERING;
}
static unsigned rf_weaver_audio_gain(int gain)
{
    return gain<0 ? 0u : gain>256 ? 256u : (unsigned)gain;
}
static void rf_weaver_audio_post(struct rasterfall_weaver_audio_bus *b,unsigned kind)
{
    unsigned wp=__atomic_load_n(&b->write_pos,__ATOMIC_RELAXED);
    unsigned rp=__atomic_load_n(&b->read_pos,__ATOMIC_ACQUIRE);
    if(wp-rp>=RF_WEAVER_CUE_RING){b->dropped++;return;}
    b->commands[wp&(RF_WEAVER_CUE_RING-1)].epoch=b->producer_epoch;
    b->commands[wp&(RF_WEAVER_CUE_RING-1)].kind=kind;
    __atomic_store_n(&b->write_pos,wp+1,__ATOMIC_RELEASE);
}
static void rf_weaver_audio_publish(struct rasterfall_weaver_audio_bus *b,
    const struct rasterfall_weaver_audio_input *input)
{
    if(!b || !input)return;
    int reset=!b->observed_valid || input->world_generation!=b->observed.world_generation ||
        input->enabled!=b->observed.enabled;
    if(reset){b->producer_epoch++;if(!b->producer_epoch)b->producer_epoch++;}
    unsigned left=input->enabled ? rf_weaver_audio_gain(input->left_q8) : 0;
    unsigned right=input->enabled ? rf_weaver_audio_gain(input->right_q8) : 0;
    int active=rf_weaver_audio_busy(input) && !input->paused;
    uint64_t target=((uint64_t)b->producer_epoch<<32)|left|(right<<9)|
        (active ? RF_WEAVER_LOOP_RUNNING : 0);
    __atomic_store_n(&b->target,target,__ATOMIC_RELEASE);
    /* Observe inaudible transitions too; returning nearby never replays them. */
    if(!reset && input->enabled && (left || right)) {
        const struct rasterfall_weaver_audio_input *old=&b->observed;
        int previous=rf_weaver_audio_busy(old) && !old->paused;
        if(input->produced_count!=old->produced_count)
            rf_weaver_audio_post(b,RF_WEAVER_DONE);
        else if(input->job_serial!=old->job_serial && active)
            rf_weaver_audio_post(b,RF_WEAVER_START);
        else if(input->job_serial==old->job_serial && rf_weaver_audio_busy(input)) {
            if(previous && !active)rf_weaver_audio_post(b,RF_WEAVER_PAUSE);
            else if(!previous && active)rf_weaver_audio_post(b,RF_WEAVER_RESUME);
        }
    }
    b->observed=*input;b->observed_valid=1;
}
static void rf_weaver_audio_goal(struct rasterfall_weaver_audio_bus *b,int channel,int goal)
{
    if(goal!=b->goal[channel]){b->goal[channel]=goal;b->remaining[channel]=b->fade_frames;}
}
static void rf_weaver_audio_drain(struct rasterfall_weaver_audio_bus *b)
{
    unsigned rp=__atomic_load_n(&b->read_pos,__ATOMIC_RELAXED);
    unsigned wp=__atomic_load_n(&b->write_pos,__ATOMIC_ACQUIRE);
    while(rp!=wp) {
        struct rf_weaver_audio_command command=b->commands[rp&(RF_WEAVER_CUE_RING-1)];
        if(command.epoch==b->consumer_epoch && command.kind<RF_WEAVER_CUE_COUNT) {
            unsigned slot=0;
            for(unsigned i=0;i<2;++i) {
                if(!b->voices[i].active){slot=i;break;}
                if(b->voices[i].cursor>b->voices[slot].cursor)slot=i;
            }
            b->voices[slot].kind=command.kind;b->voices[slot].cursor=0;b->voices[slot].active=1;
            b->played[command.kind]++;
        }
        __atomic_store_n(&b->read_pos,++rp,__ATOMIC_RELEASE);
    }
}
static short rf_weaver_audio_add(short original,int sample)
{
    int mixed=original+sample;
    return (short)(mixed>32767 ? 32767 : mixed< -32768 ? -32768 : mixed);
}
static void rf_weaver_audio_mix(struct rasterfall_weaver_audio_bus *b,short *stereo,unsigned frames)
{
    if(!b || !stereo || !frames)return;
    uint64_t target=__atomic_load_n(&b->target,__ATOMIC_ACQUIRE);
    unsigned epoch=(unsigned)(target>>32);
    if(epoch!=b->consumer_epoch) {
        memset(b->voices,0,sizeof(b->voices));memset(b->gain,0,sizeof(b->gain));
        memset(b->goal,0,sizeof(b->goal));memset(b->remaining,0,sizeof(b->remaining));
        b->loop_cursor=0;b->consumer_epoch=epoch;
    }
    rf_weaver_audio_goal(b,0,(int)(target&511u)*256);
    rf_weaver_audio_goal(b,1,(int)((target>>9)&511u)*256);
    rf_weaver_audio_goal(b,2,target&RF_WEAVER_LOOP_RUNNING ? 65536 : 0);
    rf_weaver_audio_drain(b);
    for(unsigned f=0;f<frames;++f) {
        for(unsigned k=0;k<3;++k) if(b->remaining[k]) {
            b->gain[k]+=(b->goal[k]-b->gain[k])/(int)b->remaining[k];b->remaining[k]--;
        }
        int sample=0;
        if(b->gain[2] || b->goal[2]) {
            sample=b->loop.samples[b->loop_cursor]*b->gain[2]/65536;
            if(++b->loop_cursor==b->loop.frames)b->loop_cursor=0;
        }
        for(unsigned k=0;k<2;++k) if(b->voices[k].active) {
            struct rf_weaver_audio_voice *v=&b->voices[k];
            const struct rf_weaver_audio_pcm *pcm=&b->cues[v->kind];
            sample+=pcm->samples[v->cursor++];
            if(v->cursor==pcm->frames)v->active=0;
        }
        stereo[f*2]=rf_weaver_audio_add(stereo[f*2],sample*b->gain[0]/65536);
        stereo[f*2+1]=rf_weaver_audio_add(stereo[f*2+1],sample*b->gain[1]/65536);
    }
}
