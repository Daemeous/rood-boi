/* Host audio output: a ring of 44.1 kHz stereo frames filled by the game
 * thread (pc_audio_frame) and drained into an SDL audio stream by SDL's
 * device thread (PipeWire, PulseAudio or ALSA, whichever SDL finds). Without
 * an audio device, or with ROOD_AUDIO=none, frames are discarded and the game
 * runs silently. ROOD_AUDIO=wav:<path> records every frame to a WAV file
 * instead (tests). Output starts once PREBUFFER frames are queued and, if the
 * queue runs dry, waits for it to refill rather than padding blocks with
 * silence; it is fed in whole BLOCKs, about 50 ms ahead of the device.
 * Overruns (e.g. --fast) drop the oldest frames. */
#define _GNU_SOURCE
#include "audio.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RING = 16384, BLOCK = 256, PREBUFFER = 2048, AHEAD = 44100 / 20 };
static short ring[RING * 2];
static unsigned head, tail; /* frames; head written by the game thread */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int running, primed;
static FILE* wav;
static uint32_t wav_frames;

static void wav_header(void)
{
    uint32_t bytes = wav_frames * 4;
    uint32_t header[11] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16,
        0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, bytes };
    fseek(wav, 0, SEEK_SET);
    fwrite(header, 4, 11, wav);
    fseek(wav, 0, SEEK_END);
    fflush(wav);
}

void pc_audio_output_push(const short* frames, int count)
{
    if (wav) {
        fwrite(frames, 4, count, wav);
        wav_frames += count;
        if (!(wav_frames % 44100 < (uint32_t)count))
            return;
        wav_header();
        return;
    }
    if (!running)
        return;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < count; ++i) {
        if (head - tail >= RING)
            ++tail;
        ring[(head % RING) * 2] = frames[i * 2];
        ring[(head % RING) * 2 + 1] = frames[i * 2 + 1];
        ++head;
    }
    pthread_mutex_unlock(&lock);
}

/* SDL's device thread asks for more: queue whole blocks until the stream
 * holds AHEAD frames (or a starved ring has refilled to PREBUFFER). */
static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int total)
{
    (void)user, (void)additional, (void)total;
    short block[BLOCK * 2];
    for (;;) {
        int queued = SDL_GetAudioStreamQueued(stream) / 4;
        if (queued >= AHEAD)
            return;
        pthread_mutex_lock(&lock);
        if (head - tail < (primed ? BLOCK : PREBUFFER)) {
            pthread_mutex_unlock(&lock);
            return;
        }
        primed = 1;
        for (unsigned i = 0; i < BLOCK; ++i) {
            block[i * 2] = ring[(tail % RING) * 2];
            block[i * 2 + 1] = ring[(tail % RING) * 2 + 1];
            ++tail;
        }
        if (head == tail)
            primed = 0;
        pthread_mutex_unlock(&lock);
        if (!SDL_PutAudioStreamData(stream, block, sizeof(block))) {
            fprintf(stderr, "[audio] SDL audio write failed (%s); audio stopped\n", SDL_GetError());
            running = 0;
            return;
        }
    }
}

void pc_audio_output_start(void)
{
    const char* mode = getenv("ROOD_AUDIO");
    if (mode && !strcmp(mode, "none"))
        return;
    if (mode && !strncmp(mode, "wav:", 4)) {
        wav = fopen(mode + 4, "wb");
        if (wav)
            wav_header();
        fprintf(stderr, "[audio] recording output to %s\n", mode + 4);
        return;
    }
    SDL_SetAppMetadata("Vagrant Story (rood-boi)", NULL, "rood-boi");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_NAME, "game");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "Game");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "[audio] no audio output (%s); running silently\n", SDL_GetError());
        return;
    }
    const SDL_AudioSpec spec = { SDL_AUDIO_S16LE, 2, 44100 };
    running = 1;
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!stream) {
        running = 0;
        fprintf(stderr, "[audio] cannot open an audio device (%s); running silently\n", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
    fprintf(stderr, "[audio] %s output started (44.1 kHz stereo)\n", SDL_GetCurrentAudioDriver());
}
