#include "audio.h"
#include <SDL.h>

#define AUDIO_FREQUENCY 48000
#define AUDIO_BUFFER_SIZE 512

static SDL_AudioDeviceID device_id;
static SDL_AudioSpec want_aspec, have_aspec;
static unsigned buffer_pos = 0;
static GB_sample_t audio_buffer[AUDIO_BUFFER_SIZE];

bool GB_audio_is_playing(void)
{
    return SDL_GetAudioDeviceStatus(device_id) == SDL_AUDIO_PLAYING;
}

void GB_audio_set_paused(bool paused)
{
    GB_audio_clear_queue();
    SDL_PauseAudioDevice(device_id, paused);
}

void GB_audio_clear_queue(void)
{
    SDL_ClearQueuedAudio(device_id);
}

unsigned GB_audio_get_frequency(void)
{
    return have_aspec.freq;
}

size_t GB_audio_get_queue_length(void)
{
    return SDL_GetQueuedAudioSize(device_id);
}

void GB_audio_queue_sample(GB_sample_t *sample)
{
    audio_buffer[buffer_pos++] = *sample;

    if (buffer_pos == AUDIO_BUFFER_SIZE) {
        buffer_pos = 0;
        SDL_QueueAudio(device_id, (const void *)audio_buffer, sizeof(audio_buffer));
    }
}

void GB_audio_init(void)
{
    /* Configure Audio */
    memset(&want_aspec, 0, sizeof(want_aspec));
    want_aspec.freq = AUDIO_FREQUENCY;
    want_aspec.format = AUDIO_S16SYS;
    want_aspec.channels = 2;
    want_aspec.samples = 512;
    device_id = SDL_OpenAudioDevice(0, 0, &want_aspec, &have_aspec, 0);
}
