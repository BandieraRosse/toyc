#ifndef TOYC_AUDIO_H
#define TOYC_AUDIO_H

#include "linux_audio.h"
#include "pulse_min.h"

enum toy_audio_backend {
    TOY_AUDIO_NONE = 0,
    TOY_AUDIO_ALSA,
    TOY_AUDIO_WSLG
};

struct toy_audio {
    enum toy_audio_backend backend;
    unsigned int rate;
    unsigned int channels;
    struct tlibc_pcm alsa;
    struct toy_pulse pulse;
};

int toy_audio_open(struct toy_audio *audio, unsigned int rate,
                   unsigned int channels);
long toy_audio_write(struct toy_audio *audio, const void *pcm,
                     unsigned long frames);
int toy_audio_drain(struct toy_audio *audio);
void toy_audio_close(struct toy_audio *audio);
const char *toy_audio_backend_name(const struct toy_audio *audio);

#endif
