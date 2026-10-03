/* Pure PCM contracts: no audio device, platform output, window or GPU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "../rasterfall/src/rasterfall_audio_weaver.inc"

#define CHECK(test) do {if(!(test)){printf("WEAVER-AUDIO FAIL line=%d\n",__LINE__);return 1;}} while(0)
static int magnitude(int n){return n<0 ? -n : n;}
static int silent(const short *samples,unsigned count)
{
    for(unsigned i=0;i<count;++i)if(samples[i])return 0;
    return 1;
}
static void mix_silence(struct rasterfall_weaver_audio_bus *b,unsigned frames)
{
    short block[1024];
    while(frames){unsigned count=frames>512 ? 512 : frames;memset(block,0,sizeof(block));
        rf_weaver_audio_mix(b,block,count);frames-=count;}
}
#ifdef _WIN32
struct worker_input { struct rasterfall_weaver_audio_bus *bus; unsigned quit; };
static DWORD WINAPI mix_worker(void *context)
{
    struct worker_input *work=context;
    do {mix_silence(work->bus,512);} while(!__atomic_load_n(&work->quit,__ATOMIC_ACQUIRE));
    /* Consume the last main-thread command even when it arrived at shutdown. */
    mix_silence(work->bus,4096);return 0;
}
static int threaded_test(void)
{
    struct rasterfall_weaver_audio_bus *bus=rf_weaver_audio_create(48000);CHECK(bus);
    struct worker_input work={bus,0};
    struct rasterfall_weaver_audio_input input={0};
    input.enabled=1;input.world_generation=1;input.left_q8=256;input.right_q8=128;
    rf_weaver_audio_publish(bus,&input);
    HANDLE thread=CreateThread(NULL,0,mix_worker,&work,0,NULL);CHECK(thread);
    input.job_serial=1;input.phase=TOY_WEAVER_WEAVING;
    for(unsigned n=0;n<20000;++n) {
        input.paused=n&1;input.left_q8=n%257;
        if(!(n%127))input.world_generation++;
        rf_weaver_audio_publish(bus,&input);
    }
    input.enabled=0;rf_weaver_audio_publish(bus,&input);
    __atomic_store_n(&work.quit,1,__ATOMIC_RELEASE);
    CHECK(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
    CHECK(!bus->gain[0] && !bus->gain[1] && !bus->gain[2] &&
        !bus->voices[0].active && !bus->voices[1].active &&
        bus->read_pos==bus->write_pos && bus->consumer_epoch==bus->producer_epoch);
    rf_weaver_audio_destroy(bus);return 0;
}
#endif
int main(void)
{
    struct rasterfall_weaver_audio_bus *a=rf_weaver_audio_create(48000),*b=rf_weaver_audio_create(48000);
    struct rasterfall_weaver_audio_bus *other=rf_weaver_audio_create(44100);
    CHECK(a && b && other && !rf_weaver_audio_create(0) && !rf_weaver_audio_create(384000));
    CHECK(a->loop.frames==48000 && other->loop.frames==44100 &&
        a->cues[RF_WEAVER_START].frames==6720 && other->cues[RF_WEAVER_START].frames==6174);
    CHECK(!memcmp(a->loop.samples,b->loop.samples,a->loop.frames*sizeof(short)));
    long sum=0;int maximum=0;
    for(unsigned i=0;i<a->loop.frames;++i){sum+=a->loop.samples[i];
        if(magnitude(a->loop.samples[i])>maximum)maximum=magnitude(a->loop.samples[i]);}
    CHECK(magnitude((int)sum)<48000 && maximum>100 && maximum<1000);
    CHECK(magnitude(a->loop.samples[0]-a->loop.samples[a->loop.frames-1])<30);
    for(unsigned k=0;k<RF_WEAVER_CUE_COUNT;++k) {
        CHECK(!memcmp(a->cues[k].samples,b->cues[k].samples,a->cues[k].frames*sizeof(short)));
        CHECK(!a->cues[k].samples[0] && !a->cues[k].samples[a->cues[k].frames-1]);
        for(unsigned i=0;i<a->cues[k].frames;++i)CHECK(magnitude(a->cues[k].samples[i])<2500);
    }
    struct rasterfall_weaver_audio_input input={0};
    input.world_generation=1;input.enabled=1;input.left_q8=256;input.right_q8=128;
    rf_weaver_audio_publish(a,&input);rf_weaver_audio_publish(b,&input);
    short whole[1024]={0},sliced[1024]={0};
    rf_weaver_audio_mix(a,whole,512);CHECK(silent(whole,1024));
    rf_weaver_audio_mix(b,sliced,512);
    input.job_serial=1;input.phase=TOY_WEAVER_CALIBRATING;
    rf_weaver_audio_publish(a,&input);rf_weaver_audio_publish(b,&input);
    rf_weaver_audio_mix(a,whole,512);
    rf_weaver_audio_mix(b,sliced,17);rf_weaver_audio_mix(b,sliced+34,495);
    CHECK(!memcmp(whole,sliced,sizeof(whole)) && !silent(whole,1024));
    CHECK(a->played[RF_WEAVER_START]==1 && b->played[RF_WEAVER_START]==1);
    for(unsigned i=0;i<100;++i)rf_weaver_audio_publish(a,&input);
    mix_silence(a,48000);CHECK(a->played[RF_WEAVER_START]==1);
    memset(whole,0,sizeof(whole));rf_weaver_audio_mix(a,whole,512);
    for(unsigned i=0;i<512;++i)CHECK(magnitude(whole[i*2]-2*whole[i*2+1])<=2);
    input.paused=1;rf_weaver_audio_publish(a,&input);mix_silence(a,16000);
    CHECK(a->played[RF_WEAVER_PAUSE]==1 && !a->gain[2]);
    memset(whole,0,sizeof(whole));rf_weaver_audio_mix(a,whole,512);CHECK(silent(whole,1024));
    input.paused=0;input.phase=TOY_WEAVER_WEAVING;
    rf_weaver_audio_publish(a,&input);mix_silence(a,512);CHECK(a->played[RF_WEAVER_RESUME]==1);
    input.phase=TOY_WEAVER_READY;input.produced_count=1;
    rf_weaver_audio_publish(a,&input);mix_silence(a,16000);
    CHECK(a->played[RF_WEAVER_DONE]==1 && !a->gain[2]);
    for(unsigned i=0;i<10;++i)rf_weaver_audio_publish(a,&input);
    mix_silence(a,512);CHECK(a->played[RF_WEAVER_DONE]==1);
    /* A whole job outside the audible radius is observed without stale cues. */
    input.left_q8=input.right_q8=0;input.phase=TOY_WEAVER_CALIBRATING;input.job_serial=2;
    rf_weaver_audio_publish(a,&input);mix_silence(a,4800);
    memset(whole,0,sizeof(whole));rf_weaver_audio_mix(a,whole,512);CHECK(silent(whole,1024));
    input.left_q8=input.right_q8=256;rf_weaver_audio_publish(a,&input);mix_silence(a,512);
    CHECK(a->played[RF_WEAVER_START]==1 && a->played[RF_WEAVER_RESUME]==1);
    input.left_q8=input.right_q8=0;input.phase=TOY_WEAVER_READY;input.produced_count=2;
    rf_weaver_audio_publish(a,&input);mix_silence(a,4800);
    input.left_q8=input.right_q8=256;rf_weaver_audio_publish(a,&input);mix_silence(a,512);
    CHECK(a->played[RF_WEAVER_DONE]==1);
    /* Fill the cue ring, then stop: the independent latest control still wins. */
    input.phase=TOY_WEAVER_WEAVING;input.job_serial=3;
    rf_weaver_audio_publish(a,&input);mix_silence(a,4800);
    for(unsigned i=0;i<RF_WEAVER_CUE_RING*3;++i){input.paused=!input.paused;rf_weaver_audio_publish(a,&input);}
    input.paused=1;rf_weaver_audio_publish(a,&input);
    CHECK(a->dropped>0);
    mix_silence(a,16000);CHECK(!a->gain[2]);
    memset(whole,0,sizeof(whole));rf_weaver_audio_mix(a,whole,512);CHECK(silent(whole,1024));
    /* World reset invalidates queued cues and active tails. */
    input.paused=0;rf_weaver_audio_publish(a,&input);
    input.world_generation++;input.job_serial=0;input.produced_count=0;input.phase=TOY_WEAVER_IDLE;
    rf_weaver_audio_publish(a,&input);memset(whole,0,sizeof(whole));rf_weaver_audio_mix(a,whole,512);
    CHECK(silent(whole,1024) && !a->voices[0].active && !a->voices[1].active);
    input.job_serial=1;input.phase=TOY_WEAVER_WEAVING;
    rf_weaver_audio_publish(a,&input);mix_silence(a,4800);
    for(unsigned i=0;i<1024;++i)whole[i]=32760;
    rf_weaver_audio_mix(a,whole,512);int clipped=0;
    for(unsigned i=0;i<1024;++i){CHECK(whole[i]>30000);if(whole[i]==32767)clipped=1;}CHECK(clipped);
    for(unsigned i=0;i<1024;++i)whole[i]=-32760;
    rf_weaver_audio_mix(a,whole,512);clipped=0;
    for(unsigned i=0;i<1024;++i){CHECK(whole[i]< -30000);if(whole[i]== -32768)clipped=1;}CHECK(clipped);
    input.enabled=0;rf_weaver_audio_publish(a,&input);
    memset(whole,0,sizeof(whole));rf_weaver_audio_mix(a,whole,512);CHECK(silent(whole,1024));
    rf_weaver_audio_destroy(a);rf_weaver_audio_destroy(b);rf_weaver_audio_destroy(other);
#ifdef _WIN32
    CHECK(!threaded_test());
#endif
    printf("WEAVER-AUDIO rate/PCM/seam/partition/edges/distance/stop/full-ring/reset/mix/thread passed\n");
    return 0;
}
