#ifndef TOYC_PULSE_MIN_H
#define TOYC_PULSE_MIN_H

#include "tlibc_types.h"

#define TOYC_PULSE_SOCKET "/mnt/wslg/PulseServer"

/* PulseAudio Native Protocol v8 的最小播放客户端。
 * 仅支持默认 sink、S16_LE、1/2 声道和普通 socket PCM 传输。 */
struct toy_pulse {
    int fd;
    unsigned int channel;
    unsigned int stream_index;
    unsigned int tag;
    unsigned int rate;
    unsigned int channels;
    unsigned int frame_size;
    unsigned long requested_bytes;
};

int toy_pulse_available(void);
int toy_pulse_open(struct toy_pulse *pulse, unsigned int rate,
                   unsigned int channels);
long toy_pulse_write(struct toy_pulse *pulse, const void *pcm,
                     unsigned long frames);
int toy_pulse_drain(struct toy_pulse *pulse);
void toy_pulse_close(struct toy_pulse *pulse);

#endif
