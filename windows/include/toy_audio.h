#ifndef TOYC_WINDOWS_TOY_AUDIO_H
#define TOYC_WINDOWS_TOY_AUDIO_H

#include "tlibc_types.h"

enum toy_audio_backend { TOY_AUDIO_NONE = 0, TOY_AUDIO_SDL };

struct toy_audio {
    enum toy_audio_backend backend;
    unsigned int rate;
    unsigned int channels;
    unsigned long long device;
    unsigned long long stream;
    unsigned int native_format;
    int native_freq;
    unsigned char native_channels;
};

int toy_audio_open(struct toy_audio *audio, unsigned int rate, unsigned int channels);
long toy_audio_write(struct toy_audio *audio, const void *pcm, unsigned long frames);
int toy_audio_drain(struct toy_audio *audio);
void toy_audio_close(struct toy_audio *audio);
const char *toy_audio_backend_name(const struct toy_audio *audio);

#endif
