/* Movie decoding probe: libpress over the software MDEC (pc/src/mdec.c) and
 * XA-ADPCM (pc/src/cd_drive.c).
 *  - Synthetic checks: a hand-made macroblock (flat colour, then one AC
 *    coefficient) in 24- and 15-bit output, callback chaining, and XA decoding
 *    of hand-made sound groups.
 *  - With the disc image: the first frames of MOV/TITLE.STR go through the
 *    game's own VLC table (DecDCTvlcBuild) and DecDCTvlc2, then decode in
 *    16-pixel strips as the title does. --dump DIR writes them as PPMs, and
 *    --wav PATH the first seconds of its XA audio, for comparison with an
 *    independent decoder (pc/tools/check_fmv.py). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void DecDCTReset(int mode);
void DecDCTin(unsigned long* buf, int mode);
void DecDCTout(unsigned long* buf, int size);
int DecDCToutCallback(void (*func)(void));
int DecDCTvlc2(unsigned long* bs, unsigned long* buf, uint16_t* table);
void DecDCTvlcBuild(uint16_t* table);
int pc_xa_decode_sector(const unsigned char* sector, short* out, int* channels);
void pc_xa_reset(void);

/* xa_audio.c's SPU output; the probe checks decoded samples directly. */
void pc_spu_push_cd(const short* frames, int count) { (void)frames, (void)count; }
int pc_spu_cd_pending(void) { return 1; }

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static unsigned long codes[1024];
static unsigned char pixels[16 * 16 * 3 * 2];

/* One macroblock: Cr, Cb, then four Y blocks with the given DC values. */
static void make_macroblock(int depth_mode, const int dc[6], int ac_y0)
{
    uint16_t* h = (uint16_t*)(codes + 1);
    int n = 0;
    for (int b = 0; b < 6; b++) {
        h[n++] = (1 << 10) | (dc[b] & 0x3FF); /* q_scale 1 */
        if (b == 2 && ac_y0)
            h[n++] = (0 << 10) | (ac_y0 & 0x3FF); /* first AC coefficient (1,0) */
        h[n++] = 0xFE00;
    }
    while (n & 1)
        h[n++] = 0xFE00;
    codes[0] = 0x38000000 | (n / 2);
    (void)depth_mode;
}

static int strip_callbacks;
static void strip_done(void) { ++strip_callbacks; }

static void synthetic_mdec(void)
{
    DecDCTReset(0);
    /* Flat mid grey: zero chroma, zero luma -> 128 in unsigned output. */
    int grey[6] = { 0, 0, 0, 0, 0, 0 };
    make_macroblock(3, grey, 0);
    DecDCTin(codes, 3);
    DecDCTout((unsigned long*)pixels, 16 * 16 * 3 / 4);
    CHECK(pixels[0] == 128 && pixels[1] == 128 && pixels[2] == 128 && pixels[767] == 128,
        "grey macroblock: %d %d %d", pixels[0], pixels[1], pixels[2]);

    /* Bright luma, red chroma: DC 64 * iq 2 = 128 -> IDCT 128/8 = 16 per Y
     * pixel; Cr DC raises red and lowers green. */
    int red[6] = { 64, 0, 64, 64, 64, 64 };
    make_macroblock(3, red, 0);
    DecDCTin(codes, 3);
    DecDCTout((unsigned long*)pixels, 16 * 16 * 3 / 4);
    int r = pixels[0], g = pixels[1], b = pixels[2];
    CHECK(r > 160 && g < 140 && b > 140 && b < 150, "red macroblock: %d %d %d", r, g, b);
    for (int i = 3; i < 768; i += 3)
        CHECK(pixels[i] == r && pixels[i + 1] == g && pixels[i + 2] == b, "flat block differs at %d",
            i / 3);

    /* One horizontal AC coefficient in Y0: a left-to-right gradient. */
    make_macroblock(3, grey, 40);
    DecDCTin(codes, 3);
    DecDCTout((unsigned long*)pixels, 16 * 16 * 3 / 4);
    CHECK(pixels[0] > pixels[7 * 3] && pixels[7 * 3] < 128 && pixels[0] > 128,
        "AC gradient: left %d right %d", pixels[0], pixels[7 * 3]);
    CHECK(pixels[8 * 3] == 128, "AC leaked into Y1: %d", pixels[8 * 3]);

    /* 15-bit output with STP: grey is 16 per channel (rounded 128 >> 3). */
    make_macroblock(0, grey, 0);
    DecDCTin(codes, 2);
    DecDCTout((unsigned long*)pixels, 16 * 16 * 2 / 4);
    unsigned c15 = pixels[0] | pixels[1] << 8;
    CHECK(c15 == (0x8000 | 16 | 16 << 5 | 16 << 10), "15-bit grey: %04x", c15);

    /* Strip-wise read-out with a callback, as the title does (8 x 96 words). */
    DecDCTReset(0);
    DecDCToutCallback(strip_done);
    make_macroblock(3, red, 0);
    DecDCTin(codes, 3);
    DecDCTout((unsigned long*)pixels, 96);
    CHECK(strip_callbacks == 1, "callback count %d", strip_callbacks);
    DecDCToutCallback(0);
}

/* Builds an XA sector with one non-zero sample per unit, in group 0. */
static void synthetic_xa(void)
{
    unsigned char sector[2352] = { 0 };
    short out[18 * 224 * 2];
    int channels;
    sector[18] = 0x64; /* realtime, form 2, audio */
    sector[19] = 0x01; /* stereo, 37.8 kHz, 4-bit */
    unsigned char* group = sector + 24;
    /* Unit 0 (left): shift 12 -> range 0, filter 0: nibble 1 -> 4096. */
    group[4] = 0x00;
    group[16] = 0x01; /* unit 0 sample 0 = +1, unit 1 sample 0 = 0 */
    /* Unit 1 (right): range 0, filter 1 (60/64 of the previous sample). */
    group[5] = 0x10;
    group[16 + 4] = 0x70; /* unit 1 sample 1 = 7, unit 0 sample 1 = 0 */
    pc_xa_reset();
    int frames = pc_xa_decode_sector(sector, out, &channels);
    CHECK(frames == 2016 && channels == 2, "xa frames %d channels %d", frames, channels);
    CHECK(out[0] == 4096 && out[2] == 0, "xa left samples %d %d", out[0], out[2]);
    CHECK(out[3] == 7 * 4096, "xa right sample %d", out[3]);
    /* Filter 1 decays the right channel by 60/64 per sample. */
    CHECK(out[5] == (7 * 4096 * 60 + 32) / 64, "xa right filter %d", out[5]);
}

/* ---- Disc frames ------------------------------------------------------- */
enum { STR_LBA = 257000 };
static uint16_t vlc_table[34816];
static unsigned long frame_bs[0x23000 / 4], frame_codes[0x23000 / 4];
static unsigned char frame_rgb[320 * 240 * 3];
static unsigned char strip[16 * 240 * 3];

static void disc_frames(const char* image, const char* dump, const char* wav_path, int count)
{
    FILE* f = fopen(image, "rb");
    if (!f) {
        printf("movie_probe: no disc image, disc frames skipped\n");
        return;
    }
    DecDCTvlcBuild(vlc_table);
    FILE* wav = wav_path ? fopen(wav_path, "wb") : 0;
    unsigned wav_frames = 0;
    if (wav)
        fseek(wav, 44, SEEK_SET);
    pc_xa_reset();
    int frame = 0, have = 0, sectors_in_frame = 0, decoded = 0, width = 0, height = 0;
    unsigned char sector[2352];
    for (int lba = STR_LBA; decoded < count && lba < STR_LBA + 4000; lba++) {
        if (fseek(f, (long)lba * 2352, SEEK_SET) || fread(sector, 1, 2352, f) != 2352)
            break;
        if (sector[18] & 4) {
            short pcm[18 * 224 * 2];
            int channels, n = pc_xa_decode_sector(sector, pcm, &channels);
            if (wav && channels == 2) {
                fwrite(pcm, 4, n, wav);
                wav_frames += n;
            }
            continue;
        }
        const unsigned char* h = sector + 24;
        int sector_no = h[4] | h[5] << 8, sectors = h[6] | h[7] << 8;
        int number = h[8] | h[9] << 8 | h[10] << 16 | h[11] << 24;
        if (number != frame) {
            frame = number;
            have = 0;
            sectors_in_frame = sectors;
        }
        memcpy((unsigned char*)frame_bs + sector_no * 2016, sector + 24 + 32, 2016);
        width = h[16] | h[17] << 8;
        height = h[18] | h[19] << 8;
        if (++have < sectors_in_frame)
            continue;
        /* A whole frame: VLC, then MDEC in 16-pixel-wide 24-bit strips. */
        int result = DecDCTvlc2(frame_bs, frame_codes, vlc_table);
        CHECK(result == 0, "frame %d: DecDCTvlc2 returned %d", frame, result);
        CHECK((frame_codes[0] >> 16) == 0x3800, "frame %d: command %08lx", frame, frame_codes[0]);
        DecDCTReset(0);
        DecDCTin(frame_codes, 3);
        for (int x = 0; x < width; x += 16) {
            DecDCTout((unsigned long*)strip, 16 * height * 3 / 4);
            for (int y = 0; y < height; y++)
                memcpy(frame_rgb + (y * width + x) * 3, strip + y * 48, 48);
        }
        long sum = 0;
        for (int i = 0; i < width * height * 3; i++)
            sum += frame_rgb[i];
        if (dump) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame%04d.ppm", dump, frame);
            FILE* out = fopen(path, "wb");
            if (out) {
                fprintf(out, "P6\n%d %d\n255\n", width, height);
                fwrite(frame_rgb, 1, width * height * 3, out);
                fclose(out);
            }
        }
        ++decoded;
        if (decoded == count)
            printf("movie_probe: %d frames %dx%d, last mean %ld\n", decoded, width, height,
                sum / (width * height * 3));
    }
    CHECK(decoded == count, "decoded %d of %d frames", decoded, count);
    if (wav) {
        uint32_t bytes = wav_frames * 4;
        uint32_t header[11] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020001,
            37800, 37800 * 4, 0x00100004, 0x61746164, bytes };
        fseek(wav, 0, SEEK_SET);
        fwrite(header, 4, 11, wav);
        fclose(wav);
    }
    fclose(f);
}

int main(int argc, char** argv)
{
    const char *dump = 0, *wav = 0, *image = "disks/SLUS-01040.bin";
    int count = 60;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--disc") && i + 1 < argc)
            image = argv[++i];
    }
    synthetic_mdec();
    synthetic_xa();
    disc_frames(image, dump, wav, count);
    if (failures) {
        fprintf(stderr, "movie_probe: %d failures\n", failures);
        return 1;
    }
    printf("movie_probe: ok\n");
    return 0;
}
