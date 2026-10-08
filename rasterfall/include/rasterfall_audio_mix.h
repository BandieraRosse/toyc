#ifndef RASTERFALL_AUDIO_MIX_H
#define RASTERFALL_AUDIO_MIX_H

/* Leave ordinary low-level samples unchanged; compress peaks toward a strict
 * ceiling without hard saturation. Integer math only in the mixing thread.
 * The caller reserves independent bus headroom before adding PCM16 streams. */
static int rf_audio_soft_peak(int value,int ceiling)
{
    if(ceiling<=0)return value;
    int magnitude=value<0?-value:value;
    int knee=ceiling*3/4;
    if(magnitude<=knee)return value;
    int excess=magnitude-knee,span=ceiling-knee;
    int limited=knee+(int)((long long)excess*span/(excess+span));
    return value<0?-limited:limited;
}

static inline int rf_audio_gain_step(int current,int target)
{
    int delta=target-current;
    if(!delta)return current;
    int step=delta/1323;
    if(!step)step=delta>0?1:-1;
    return current+step;
}

#endif
