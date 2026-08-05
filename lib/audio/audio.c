/* SPDX-License-Identifier: MIT
 * 统一游戏音频输出：WSLg 使用内置 Pulse 客户端，原生 Linux 使用 ALSA。 */

#include "tlibc_everything.h"
#include "toy_audio.h"
#include "errno.h"

int toy_audio_open(struct toy_audio *audio, unsigned int rate,
                   unsigned int channels)
{
    int ret;
    if (!audio || channels < 1 || channels > 2) return -EINVAL;
    memset(audio, 0, sizeof(*audio));
    audio->rate = rate;
    audio->channels = channels;

    if (toy_pulse_available()) {
        ret = toy_pulse_open(&audio->pulse, rate, channels);
        if (ret == 0) {
            audio->backend = TOY_AUDIO_WSLG;
            return 0;
        }
        /* WSLg socket 存在就不尝试不存在的 /dev/snd，保留真实错误。 */
        return ret;
    }

    ret = tlibc_pcm_open(&audio->alsa, NULL, channels, rate, 16);
    if (ret < 0) return ret;
    ret = tlibc_pcm_configure(&audio->alsa);
    if (ret < 0) {
        tlibc_pcm_close(&audio->alsa);
        return ret;
    }
    audio->backend = TOY_AUDIO_ALSA;
    return 0;
}

long toy_audio_write(struct toy_audio *audio, const void *pcm,
                     unsigned long frames)
{
    if (!audio || !pcm) return -EINVAL;
    if (audio->backend == TOY_AUDIO_WSLG)
        return toy_pulse_write(&audio->pulse, pcm, frames);
    if (audio->backend == TOY_AUDIO_ALSA)
        return tlibc_pcm_write(&audio->alsa, pcm, frames);
    return -ENODEV;
}

int toy_audio_drain(struct toy_audio *audio)
{
    if (!audio) return -EINVAL;
    if (audio->backend == TOY_AUDIO_WSLG)
        return toy_pulse_drain(&audio->pulse);
    if (audio->backend == TOY_AUDIO_ALSA)
        return tlibc_pcm_drain(&audio->alsa);
    return -ENODEV;
}

void toy_audio_close(struct toy_audio *audio)
{
    if (!audio) return;
    if (audio->backend == TOY_AUDIO_WSLG)
        toy_pulse_close(&audio->pulse);
    else if (audio->backend == TOY_AUDIO_ALSA)
        tlibc_pcm_close(&audio->alsa);
    audio->backend = TOY_AUDIO_NONE;
}

const char *toy_audio_backend_name(const struct toy_audio *audio)
{
    if (!audio) return "none";
    if (audio->backend == TOY_AUDIO_WSLG) return "WSLg native pulse";
    if (audio->backend == TOY_AUDIO_ALSA) return "ALSA";
    return "none";
}
