/* CD-ROM XA audio for the host CD drive (cd_drive.c): sectors the drive plays
 * in real-time ADPCM mode are decoded here (psx-spx "CDROM XA Audio ADPCM
 * Compression"), mixed through the drive's CD-to-SPU attenuators (DsMix /
 * CdMix), resampled from 37.8 or 18.9 kHz to 44.1 kHz and pushed into the
 * software SPU's CD-audio input, which applies the CD volume and enable bits
 * the game writes (audio_spu.cpp). The drive delivers sectors from the vsync
 * tick at the disc's rate, so the frames pushed per tick match what
 * pc_audio_frame renders; a short lead-in of silence whenever the input has
 * run dry absorbs the burstiness of whole sectors. */
#include "audio.h"
#include <string.h>

static const int pos_table[4] = { 0, 60, 115, 98 }, neg_table[4] = { 0, 0, -52, -55 };
static int old[2], older[2];
/* Last three input frames (Catmull-Rom history) and the output position. */
static int history[3][2];
static unsigned position; /* in 1/44100 of an input sample, past history[1] */

void pc_xa_reset(void)
{
    memset(old, 0, sizeof old);
    memset(older, 0, sizeof older);
    memset(history, 0, sizeof history);
    position = 0;
}

static short clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

/* Decodes an XA audio sector (raw 2352 bytes) into interleaved samples:
 * returns frames (stereo pairs, or mono samples) and the channel count. */
int pc_xa_decode_sector(const unsigned char* sector, short* out, int* channels)
{
    int coding = sector[19];
    int stereo = (coding & 3) == 1, eight_bit = ((coding >> 4) & 3) == 1;
    int units = eight_bit ? 4 : 8, written = 0;
    *channels = stereo ? 2 : 1;
    for (int g = 0; g < 18; g++) {
        const unsigned char* group = sector + 24 + g * 128;
        for (int u = 0; u < units; u++) {
            int param = group[4 + u];
            int range = param & 0xF, filter = (param >> 4) & 3;
            if (range > 12)
                range = 9; /* reserved ranges act as 9 */
            int ch = stereo ? u & 1 : 0;
            /* Units are interleaved L/R in stereo; mono units follow each other. */
            int base = stereo ? (g * (units / 2) + (u >> 1)) * 28 : (g * units + u) * 28;
            for (int j = 0; j < 28; j++) {
                int t;
                if (eight_bit)
                    t = (signed char)group[16 + u + j * 4] << 8;
                else {
                    int nibble = (group[16 + (u >> 1) + j * 4] >> ((u & 1) * 4)) & 0xF;
                    t = (nibble ^ 8) - 8;
                    t <<= 12;
                }
                int s = (t >> range)
                      + ((old[ch] * pos_table[filter] + older[ch] * neg_table[filter] + 32) >> 6);
                s = clamp16(s);
                older[ch] = old[ch];
                old[ch] = s;
                out[(base + j) * *channels + ch] = s;
            }
            written += 28;
        }
    }
    return written / *channels;
}

static int catmull(int p0, int p1, int p2, int p3, unsigned t, unsigned scale)
{
    double x = (double)t / scale;
    double v = p1
             + 0.5 * x
                   * (p2 - p0
                       + x * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + x * (3.0 * (p1 - p2) + p3 - p0)));
    return clamp16((int)(v < 0 ? v - 0.5 : v + 0.5));
}

/* Plays one XA audio sector through the CD mixer. atv: CD(L)->SPU(L),
 * CD(L)->SPU(R), CD(R)->SPU(R), CD(R)->SPU(L), 0x80 = 100%. */
void pc_xa_play_sector(const unsigned char* sector, const unsigned char atv[4])
{
    static short decoded[18 * 224 * 2];
    static int input[18 * 224 + 3][2];
    static short output[(18 * 224 + 3) * 2 * 2];
    int channels, frames = pc_xa_decode_sector(sector, decoded, &channels);
    unsigned rate = ((sector[19] >> 2) & 3) == 1 ? 18900 : 37800;
    memcpy(input, history, sizeof history);
    for (int i = 0; i < frames; i++) {
        int l = decoded[i * channels], r = decoded[i * channels + channels - 1];
        input[i + 3][0] = clamp16((l * atv[0] + r * atv[3]) >> 7);
        input[i + 3][1] = clamp16((r * atv[2] + l * atv[1]) >> 7);
    }
    /* Output sample n sits at input index 1 + position / 44100 of `input`
     * (history[0..2] are indices 0..2); it needs its neighbours on both sides. */
    int n = 0;
    for (;;) {
        unsigned k = 1 + position / 44100, t = position % 44100;
        if (k + 2 > (unsigned)frames + 2)
            break;
        for (int c = 0; c < 2; c++)
            output[n * 2 + c] = catmull(input[k - 1][c], input[k][c], input[k + 1][c], input[k + 2][c], t, 44100);
        ++n;
        position += rate;
    }
    position -= frames * 44100;
    memcpy(history, input + frames, sizeof history);
    if (pc_spu_cd_pending() == 0) {
        static const short silence[1470 * 2];
        pc_spu_push_cd(silence, 1470);
    }
    pc_spu_push_cd(output, n);
}
