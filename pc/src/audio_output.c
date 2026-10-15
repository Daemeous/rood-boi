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

/* Master volume (settings master_volume 0..100, System > Audio): a gain on
 * what leaves the game (the device stream and a ROOD_AUDIO=wav recording),
 * (volume / 100)^2 so equal steps sound about even. Changes ramp over 20 ms
 * (no clicks). At 100 the samples pass untouched (bit-identical). */
enum { GAIN_RAMP = 882 };
static int master_volume = 100;
int pc_master_volume_get(void) { return __atomic_load_n(&master_volume, __ATOMIC_RELAXED); }
void pc_master_volume_set(int volume)
{
    __atomic_store_n(&master_volume, volume < 0 ? 0 : volume > 100 ? 100 : volume, __ATOMIC_RELAXED);
}
static float master_target(void)
{
    float v = pc_master_volume_get() / 100.0f;
    return v * v;
}
/* Scales `count` stereo frames in place; *gain is the stream's current gain
 * (one per consumer thread), moved toward the target per frame. A stream
 * starts at the target (GAIN_UNSET: no samples yet), so a saved low volume
 * never begins with a ramp down from full, whichever of the settings and the
 * audio comes up first. */
#define GAIN_UNSET -1.0f
static void master_gain(short* frames, int count, float* gain)
{
    float target = master_target();
    if (*gain == GAIN_UNSET)
        *gain = target;
    if (*gain == 1.0f && target == 1.0f)
        return;
    const float step = 1.0f / GAIN_RAMP;
    for (int i = 0; i < count; ++i) {
        if (*gain < target)
            *gain = *gain + step < target ? *gain + step : target;
        else if (*gain > target)
            *gain = *gain - step > target ? *gain - step : target;
        for (int c = 0; c < 2; ++c) {
            float v = frames[i * 2 + c] * *gain;
            frames[i * 2 + c] = (short)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int)(v < 0 ? v - 0.5f : v + 0.5f));
        }
    }
}
static float device_gain = GAIN_UNSET, wav_gain = GAIN_UNSET; /* each its consumer thread's */
static float device_gain_seen = GAIN_UNSET;                    /* device_gain, published for the RPC */
/* RPC (system): the gain now (after the ramp): the recording's, else the device stream's (the target before
 * any sample went out). */
float pc_master_gain(void)
{
    float g;
    if (wav)
        g = wav_gain; /* the game thread's own */
    else
        __atomic_load(&device_gain_seen, &g, __ATOMIC_RELAXED);
    return g == GAIN_UNSET ? master_target() : g;
}
/* RPC (system): frames recorded so far (ROOD_AUDIO=wav; tests find a moment in the file). */
unsigned pc_audio_output_recorded(void) { return wav_frames; }

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

/* Battle charging at several vsyncs per field (boot_window.c): the game makes
 * audio faster than the device plays it, so a push that finds TURBO_LEVEL
 * frames queued already is dropped (the sound stays in real time, without
 * the latency a full ring would add); the next push fades in from the last
 * sample kept (no click at the cut). A recording (ROOD_AUDIO=wav) keeps
 * everything. */
enum { TURBO_LEVEL = PREBUFFER, FADE = 64 };
static int turbo, cut; /* (the game thread's) */
static short last_kept[2];
void pc_audio_output_turbo(int on) { turbo = on; }

void pc_audio_output_push(const short* frames, int count)
{
    if (wav) {
        if (wav_gain != 1.0f || master_target() != 1.0f) { /* (also the first block: GAIN_UNSET) */
            short scaled[BLOCK * 2];
            for (int done = 0; done < count; done += BLOCK) {
                int n = count - done < BLOCK ? count - done : BLOCK;
                memcpy(scaled, frames + done * 2, (size_t)n * 4);
                master_gain(scaled, n, &wav_gain);
                fwrite(scaled, 4, n, wav);
            }
        } else
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
    if (turbo && head - tail >= TURBO_LEVEL) {
        pthread_mutex_unlock(&lock);
        cut = 1;
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (head - tail >= RING)
            ++tail;
        for (int c = 0; c < 2; ++c) {
            int v = frames[i * 2 + c];
            if (cut && i < FADE)
                v = last_kept[c] + (v - last_kept[c]) * (i + 1) / FADE;
            ring[(head % RING) * 2 + c] = (short)v;
        }
        ++head;
    }
    pthread_mutex_unlock(&lock);
    if (count > 0) {
        cut = 0;
        last_kept[0] = frames[(count - 1) * 2];
        last_kept[1] = frames[(count - 1) * 2 + 1];
    }
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
        master_gain(block, BLOCK, &device_gain);
        __atomic_store(&device_gain_seen, &device_gain, __ATOMIC_RELAXED);
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
