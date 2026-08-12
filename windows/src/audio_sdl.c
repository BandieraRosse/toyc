#include <SDL2/SDL.h>
#include "toy_audio.h"

int toy_audio_open(struct toy_audio *audio, unsigned int rate, unsigned int channels)
{
    SDL_AudioSpec want;
    if (!audio || channels < 1 || channels > 2) return -1;
    SDL_memset(audio, 0, sizeof(*audio));
    SDL_memset(&want, 0, sizeof(want));
    want.freq = (int)rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)channels;
    want.samples = 1024;
    want.callback = NULL;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return -1;
    {
        SDL_AudioSpec have;
        audio->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        audio->native_freq = have.freq;
        audio->native_channels = have.channels;
        if (audio->device)
            audio->stream = (unsigned long long)SDL_NewAudioStream(
                AUDIO_S16SYS, (Uint8)channels, (int)rate,
                have.format, have.channels, have.freq);
        audio->native_format = have.format;
    }
    if (!audio->device || !audio->stream) {
        if (audio->device) SDL_CloseAudioDevice(audio->device);
        audio->device = 0;
        return -1;
    }
    audio->rate = rate;
    audio->channels = channels;
    audio->backend = TOY_AUDIO_SDL;
    SDL_PauseAudioDevice(audio->device, 0);
    return 0;
}

long toy_audio_write(struct toy_audio *audio, const void *pcm, unsigned long frames)
{
    Uint32 bytes;
    if (!audio || audio->backend != TOY_AUDIO_SDL || !pcm) return -1;
    bytes = (Uint32)(frames * audio->channels * sizeof(short));
    /* SDL_QueueAudio is non-blocking. Without a bound the mixer thread can
     * generate audio much faster than the device consumes it, putting every
     * later gunshot behind an ever-growing music queue. Keep latency below
     * roughly four mixer blocks. */
    while (SDL_GetQueuedAudioSize(audio->device) >
           (Uint32)(audio->rate / 10 * audio->channels * sizeof(short)))
        SDL_Delay(1);
    if (SDL_AudioStreamPut((SDL_AudioStream *)audio->stream, pcm, (int)bytes) < 0)
        return -1;
    while (SDL_AudioStreamAvailable((SDL_AudioStream *)audio->stream) > 0) {
        unsigned char buffer[8192];
        int available = SDL_AudioStreamAvailable((SDL_AudioStream *)audio->stream);
        int want = available > (int)sizeof(buffer) ? (int)sizeof(buffer) : available;
        int got = SDL_AudioStreamGet((SDL_AudioStream *)audio->stream, buffer, want);
        if (got <= 0 || SDL_QueueAudio(audio->device, buffer, (Uint32)got) < 0)
            return -1;
    }
    return (long)frames;
}

int toy_audio_drain(struct toy_audio *audio)
{
    if (!audio || audio->backend != TOY_AUDIO_SDL) return -1;
    SDL_AudioStreamFlush((SDL_AudioStream *)audio->stream);
    while (SDL_AudioStreamAvailable((SDL_AudioStream *)audio->stream) > 0) {
        unsigned char buffer[8192];
        int available = SDL_AudioStreamAvailable((SDL_AudioStream *)audio->stream);
        int want = available > (int)sizeof(buffer) ? (int)sizeof(buffer) : available;
        int got = SDL_AudioStreamGet((SDL_AudioStream *)audio->stream, buffer, want);
        if (got <= 0) break;
        SDL_QueueAudio(audio->device, buffer, (Uint32)got);
    }
    while (SDL_GetQueuedAudioSize(audio->device) != 0) SDL_Delay(1);
    return 0;
}

void toy_audio_close(struct toy_audio *audio)
{
    if (!audio) return;
    if (audio->stream) SDL_FreeAudioStream((SDL_AudioStream *)audio->stream);
    audio->stream = 0;
    if (audio->device) SDL_CloseAudioDevice(audio->device);
    audio->device = 0;
    audio->backend = TOY_AUDIO_NONE;
}

const char *toy_audio_backend_name(const struct toy_audio *audio)
{
    return audio && audio->backend == TOY_AUDIO_SDL ? "SDL audio" : "none";
}
