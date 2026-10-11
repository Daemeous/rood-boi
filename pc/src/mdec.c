/* libpress (movie decoding) over a software MDEC, for the intro movie
 * (TITLE 22C.c). The PS1 decodes a movie frame in two steps:
 *  - DecDCTvlc2, on the CPU, expands the frame's Huffman bitstream (BS v2/v3)
 *    into MDEC run-length codes. Below is a translation of Sony's MIPS
 *    (src/TITLE/TITLE.PRG/libpress/VLC_C.s); its lookup table comes from the
 *    game's own DecDCTvlcBuild (libpress/BUILD.c, compiled natively).
 *  - The MDEC chip turns the codes into pixels: DecDCTin feeds it a command
 *    and codes by DMA, DecDCTout receives decoded macroblocks by DMA, and the
 *    DecDCToutCallback handler runs when each output transfer completes.
 * The chip follows the psx-spx description of the MDEC: dequantisation with
 * the uploaded tables, an integer IDCT with the uploaded scale table, and
 * YCbCr to RGB in 4, 8, 15 or 24 bits. Host transfers complete at once, so
 * DecDCTout decodes before it returns and then raises the callback; a
 * callback that starts the next transfer (the game decodes a frame strip by
 * strip this way) is run from a loop rather than recursively. */
#include "psx_types.h" /* psx_long: the SDK's 32-bit long on every build */
#include <stdint.h>
#include <string.h>

typedef void (*callback_t)(void);

/* DecDCTReset's tables (LIBPRESS.c D_80075B48 / D_80075B88, both the MPEG
 * intra matrix in zigzag order, and the IDCT scale table D_80075BCC). */
static const unsigned char default_iq[64] = { 0x02, 0x10, 0x10, 0x13, 0x10, 0x13, 0x16, 0x16,
    0x16, 0x16, 0x16, 0x16, 0x1A, 0x18, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A, 0x1A, 0x1A, 0x1A, 0x1B,
    0x1B, 0x1B, 0x1D, 0x1D, 0x1D, 0x22, 0x22, 0x22, 0x1D, 0x1D, 0x1D, 0x1B, 0x1B, 0x1D, 0x1D,
    0x20, 0x20, 0x22, 0x22, 0x25, 0x26, 0x25, 0x23, 0x23, 0x22, 0x23, 0x26, 0x26, 0x28, 0x28,
    0x28, 0x30, 0x30, 0x2E, 0x2E, 0x38, 0x38, 0x3A, 0x45, 0x45, 0x53 };
static const uint16_t default_scale[64] = { 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82,
    0x5A82, 0x5A82, 0x7D8A, 0x6A6D, 0x471C, 0x18F8, 0xE707, 0xB8E3, 0x9592, 0x8275, 0x7641,
    0x30FB, 0xCF04, 0x89BE, 0x89BE, 0xCF04, 0x30FB, 0x7641, 0x6A6D, 0xE707, 0x8275, 0xB8E3,
    0x471C, 0x7D8A, 0x18F8, 0x9592, 0x5A82, 0xA57D, 0xA57D, 0x5A82, 0x5A82, 0xA57D, 0xA57D,
    0x5A82, 0x471C, 0x8275, 0x18F8, 0x6A6D, 0x9592, 0xE707, 0x7D8A, 0xB8E3, 0x30FB, 0x89BE,
    0x7641, 0xCF04, 0xCF04, 0x7641, 0x89BE, 0x30FB, 0x18F8, 0xB8E3, 0x6A6D, 0x8275, 0x7D8A,
    0x9592, 0x471C, 0xE707 };
/* Raster position of each zigzag-order coefficient. */
static const unsigned char zigzag[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4,
    5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43,
    36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55,
    62, 63 };

static struct {
    unsigned char iq_y[64], iq_c[64];
    int16_t scale[64];
    /* Current decode command: codes still to read, output format. */
    const uint16_t *in, *in_end;
    int depth, is_signed, stp;
    /* One decoded macroblock (or 8x8 block in 4/8-bit modes) being read out. */
    unsigned char out[16 * 16 * 3];
    int out_len, out_pos;
} mdec;

static callback_t out_callback, in_callback;
static int dispatching, callbacks_due;

static int signed10(int v) { return (v & 0x3FF) - ((v & 0x200) << 1); }

/* psx-spx real_idct_core: two passes of the scale-table matrix, each
 * transposing; (sum + 0xFFF) / 0x2000 rounding per pass. */
static void idct(int* block)
{
    int temp[64], *src = block, *dst = temp;
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++) {
                int64_t sum = 0;
                for (int z = 0; z < 8; z++)
                    sum += (int64_t)src[y + z * 8] * (mdec.scale[x + z * 8] / 8);
                dst[x + y * 8] = (int)((sum + 0xFFF) >> 13);
            }
        int* swap = src;
        src = dst;
        dst = swap;
    }
}

/* Reads one block's codes into coefficients and transforms it. Returns 0 when
 * the input ran out first. */
static int decode_block(int* block, const unsigned char* qt)
{
    memset(block, 0, 64 * sizeof *block);
    unsigned n;
    do {
        if (mdec.in >= mdec.in_end)
            return 0;
        n = *mdec.in++;
    } while (n == 0xFE00);
    int q_scale = (n >> 10) & 0x3F, k = 0;
    int value = signed10(n) * qt[0];
    for (;;) {
        if (q_scale == 0)
            value = signed10(n) * 2;
        value = value < -0x400 ? -0x400 : value > 0x3FF ? 0x3FF : value;
        block[q_scale ? zigzag[k] : k] = value;
        if (mdec.in >= mdec.in_end)
            break;
        n = *mdec.in++;
        k += ((n >> 10) & 0x3F) + 1;
        if (k > 63)
            break;
        value = (signed10(n) * qt[k] * q_scale + 4) >> 3;
    }
    idct(block);
    return 1;
}

static int clamp8(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }

/* Decodes the next macroblock into mdec.out (zeros once the codes run out). */
static void decode_macroblock(void)
{
    static int cr[64], cb[64], y[4][64];
    int ok;
    mdec.out_pos = 0;
    if (mdec.depth < 2) {
        /* Monochrome: one luma block, 8 or 4 bits per pixel. */
        ok = decode_block(y[0], mdec.iq_y);
        mdec.out_len = mdec.depth ? 64 : 32;
        for (int i = 0; i < 64; i++) {
            int v = ok ? clamp8(y[0][i]) : 0;
            if (!mdec.is_signed)
                v ^= 0x80;
            v &= 0xFF;
            if (mdec.depth)
                mdec.out[i] = v;
            else if (i & 1)
                mdec.out[i >> 1] |= (v >> 4) << 4;
            else
                mdec.out[i >> 1] = v >> 4;
        }
        return;
    }
    ok = decode_block(cr, mdec.iq_c) && decode_block(cb, mdec.iq_c);
    for (int i = 0; i < 4; i++)
        ok = ok && decode_block(y[i], mdec.iq_y);
    int bytes = mdec.depth == 2 ? 3 : 2;
    mdec.out_len = 256 * bytes;
    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 16; px++) {
            unsigned char* p = mdec.out + (py * 16 + px) * bytes;
            if (!ok) {
                memset(p, 0, bytes);
                continue;
            }
            int c = (px >> 1) + (py >> 1) * 8;
            int r = cr[c], b = cb[c];
            int g = (-352 * b - 731 * r) >> 10; /* -0.3437 Cb - 0.7143 Cr */
            r = (1436 * r) >> 10;               /* 1.402 Cr */
            b = (1815 * b) >> 10;               /* 1.772 Cb */
            int luma = y[(px >> 3) + (py >> 3) * 2][(px & 7) + (py & 7) * 8];
            int rgb[3] = { clamp8(luma + r), clamp8(luma + g), clamp8(luma + b) };
            for (int i = 0; i < 3; i++)
                rgb[i] = (mdec.is_signed ? rgb[i] : rgb[i] ^ 0x80) & 0xFF;
            if (bytes == 3) {
                p[0] = rgb[0];
                p[1] = rgb[1];
                p[2] = rgb[2];
            } else {
                /* 15-bit: signed output is converted as unsigned's bit pattern. */
                int c15 = 0;
                for (int i = 0; i < 3; i++) {
                    int v = (rgb[i] + 4) >> 3;
                    c15 |= (v > 31 ? 31 : v) << (i * 5);
                }
                c15 |= mdec.stp << 15;
                p[0] = c15;
                p[1] = c15 >> 8;
            }
        }
}

/* Executes an MDEC command block (command word, then its parameter words). */
void pc_mdec_command(const uint32_t* words)
{
    uint32_t command = words[0];
    switch (command >> 29) {
    case 1: /* decode macroblocks */
        mdec.depth = (command >> 27) & 3;
        mdec.is_signed = (command >> 26) & 1;
        mdec.stp = (command >> 25) & 1;
        mdec.in = (const uint16_t*)(words + 1);
        mdec.in_end = mdec.in + (command & 0xFFFF) * 2;
        mdec.out_len = mdec.out_pos = 0;
        break;
    case 2: /* quantisation tables: luma, and chroma when bit 0 is set */
        memcpy(mdec.iq_y, words + 1, 64);
        memcpy(mdec.iq_c, (command & 1) ? (const unsigned char*)(words + 17) : mdec.iq_y, 64);
        break;
    case 3: /* IDCT scale table */
        memcpy(mdec.scale, words + 1, sizeof mdec.scale);
        break;
    }
}

/* Reads `bytes` of decoded output, decoding macroblocks as needed. */
void pc_mdec_read(void* buffer, int bytes)
{
    unsigned char* dst = buffer;
    while (bytes > 0) {
        if (mdec.out_pos == mdec.out_len)
            decode_macroblock();
        int n = mdec.out_len - mdec.out_pos;
        n = n < bytes ? n : bytes;
        memcpy(dst, mdec.out + mdec.out_pos, n);
        mdec.out_pos += n;
        dst += n;
        bytes -= n;
    }
}

static void raise_out_callback(void)
{
    if (!out_callback)
        return;
    ++callbacks_due;
    if (dispatching)
        return;
    dispatching = 1;
    while (callbacks_due) {
        --callbacks_due;
        if (out_callback)
            out_callback();
    }
    dispatching = 0;
}

/* Mode 0 also restores the default tables. DecDCTReset(0) calls ResetCallback
 * on the PS1, where it changes nothing once the interrupt system is set up;
 * the host ResetCallback would drop the game's VSync/DrawSync handlers. */
void DecDCTReset(int mode)
{
    mdec.in = mdec.in_end = 0;
    mdec.out_len = mdec.out_pos = 0;
    callbacks_due = 0;
    if (mode == 0) {
        memcpy(mdec.iq_y, default_iq, 64);
        memcpy(mdec.iq_c, default_iq, 64);
        memcpy(mdec.scale, default_scale, sizeof mdec.scale);
    }
}

/* LIBPRESS.c DecDCTin: mode bit 0 selects 24-bit output (else 15-bit), bit 1
 * the STP bit; both are patched into the command word in place. */
void DecDCTin(psx_ulong* buf, int mode)
{
    if (mode & 1)
        *buf &= 0xF7FFFFFF;
    else
        *buf |= 0x08000000;
    if (mode & 2)
        *buf |= 0x02000000;
    else
        *buf &= 0xFDFFFFFF;
    pc_mdec_command((const uint32_t*)buf);
    if (in_callback)
        in_callback();
}

/* size is in words. */
void DecDCTout(psx_ulong* buf, int size)
{
    pc_mdec_read(buf, size * 4);
    raise_out_callback();
}

int DecDCToutCallback(callback_t func)
{
    callback_t old = out_callback;
    out_callback = func;
    return (int)(intptr_t)old;
}

int DecDCTinCallback(callback_t func)
{
    callback_t old = in_callback;
    in_callback = func;
    return (int)(intptr_t)old;
}

/* Transfers finish before DecDCTin/DecDCTout return. */
int DecDCTinSync(int mode) { return (void)mode, 0; }
int DecDCToutSync(int mode) { return (void)mode, 0; }

/* ---- DecDCTvlc2: VLC_C.s, label for label ------------------------------
 * Registers keep their roles: a0 bitstream, a1 output (bytes), v0 the 32-bit
 * bit window (next bit in bit 31), v1 the bits consumed past the window's
 * low halfword, t4 the quantiser scale << 10, t5 the block within the
 * macroblock for v3 DC prediction (0 for v2), t7/t8/t9 the Cr/Cb/Y DC
 * predictors. The table (DECDCTTAB, from DecDCTvlcBuild) holds two v3 DC
 * tables, a 13-bit main table of 8-byte entries (bits to consume; up to three
 * codes) and a 9-bit table for longer codes. */
static uint32_t decode_size = 0xFFFFFF; /* decodeSize: DecDCTvlcSize2 */
static struct {
    const uint16_t* a0;
    unsigned char* a1;
    uint32_t v0, v1, t4;
    int t5, t7, t8, t9;
} vlc_state;                 /* D_80075C98: a partial decode, resumed by bs == NULL */
static unsigned char* vlc_end; /* D_80075CBC: end of the frame's run-length codes */

int DecDCTvlcSize2(int size)
{
    int previous = decode_size;
    decode_size = size - 1 > 0 ? (uint32_t)size * 2 : 0xFFFFFF;
    return previous;
}

#define STORE(p, v) (*(uint16_t*)(p) = (uint16_t)(v))
#define REFILL()                                                                                   \
    do {                                                                                           \
        int carry = v1 & 0x10;                                                                     \
        v1 &= 0xF;                                                                                 \
        if (carry)                                                                                 \
            v0 |= (uint32_t)*a0++ << v1;                                                           \
    } while (0)

int DecDCTvlc2(psx_ulong* bs, psx_ulong* buf, uint16_t* table)
{
    const unsigned char* a2 = (const unsigned char*)table + 0x800;
    const unsigned char* a3 = a2 + 0x10000;
    const uint16_t* a0;
    unsigned char *a1, *t6;
    uint32_t v0, v1, t4, t0, t1, t2, t3, at;
    int t5, t7, t8, t9;
    if (!bs) {
        a0 = vlc_state.a0;
        a1 = vlc_state.a1;
        v0 = vlc_state.v0;
        v1 = vlc_state.v1;
        t4 = vlc_state.t4;
        t5 = vlc_state.t5;
        t7 = vlc_state.t7;
        t8 = vlc_state.t8;
        t9 = vlc_state.t9;
        t6 = a1 + decode_size * 2;
        goto l13;
    }
    t5 = t7 = t8 = t9 = 0;
    t6 = (unsigned char*)buf + decode_size * 2;
    {
        const uint16_t* h = (const uint16_t*)bs;
        uint32_t header = (uint32_t)bs[0];
        t4 = (uint32_t)h[2] << 10;
        if ((int)h[3] - 3 >= 0)
            t5 = 1; /* version 3: DC differences */
        v0 = ((uint32_t)h[4] << 16) | h[5];
        a0 = h + 6;
        v1 = 0;
        buf[0] = header;
        vlc_end = (unsigned char*)buf + (header & 0xFFFF) * 4 + 4;
        a1 = (unsigned char*)buf + 2;
    }
l2:
    t0 = v0 >> 22;
    if (t5 == 0)
        goto l10;
    a1 += 2;
    if (t0 == 0x3FF)
        goto l19;
    {
        const unsigned char* dc = t5 - 3 < 0 ? a2 - 0x400 : a2 - 0x800;
        const uint16_t* entry = (const uint16_t*)(dc + (v0 >> 24) * 4);
        t1 = entry[0];
        t2 = entry[1];
        t0 = 0;
        v0 <<= t1 & 31;
        if (t2) {
            /* t2 bits of difference; a clear top bit means negative. */
            at = (32 - t2) & 31;
            t0 = v0 >> at;
            int negative = (int32_t)v0 >= 0;
            v0 <<= t2 & 31;
            if (negative)
                t0 -= 0xFFFFFFFFu >> at;
            v1 += t2;
        }
        v1 += t1;
        REFILL();
    }
    if (t5 - 2 > 0) {
        t1 = t9 + t0;
        t9 += t0;
    } else if (t5 - 2 == 0) {
        t1 = t8 + t0;
        t8 += t0;
    } else {
        t1 = t7 + t0;
        t7 += t0;
    }
    t1 = ((t1 << 2) & 0x3FF) | t4;
    ++t5;
    STORE(a1, t1);
    if (t5 == 7)
        t5 -= 6;
    goto l12;
l10:
    a1 += 2;
    if (t0 == 0x1FF)
        goto l19;
    v0 <<= 10;
    v1 += 10;
    REFILL();
    STORE(a1, t4 | t0);
l12:
    /* Host guard: a valid frame ends (DC 0x1FF/0x3FF) within its header's
     * size; stop at that bound if a corrupt stream never does. */
    if (a1 >= vlc_end) {
        a1 += 2;
        goto l19;
    }
    if ((int32_t)(a1 - t6) >= 0) {
        a1 += 2;
        goto l22;
    }
    a1 += 2;
l13:
    {
        const uint32_t* entry = (const uint32_t*)(a2 + (v0 >> 19) * 8);
        t1 = entry[0];
        if (t1) {
            at = t1 & 0xFF;
            t3 = entry[1];
        } else {
            v0 <<= 8;
            v1 += 8;
            REFILL();
            t1 = *(const uint32_t*)(a3 + (v0 >> 23) * 4);
            t3 = 0;
            at = t1 & 0xFF;
        }
    }
    v0 <<= at & 31;
    v1 += at;
    REFILL();
    t1 >>= 16;
    if (t1 == 0x7C1F)
        goto l18;
    STORE(a1, t1);
    if (t1 == 0xFE00)
        goto l2;
    a1 += 2;
    if (t3 == 0)
        goto l13;
    t2 = t3 & 0xFFFF;
    if (t2 == 0x7C1F)
        goto l18;
    STORE(a1, t2);
    if (t2 == 0xFE00)
        goto l2;
    t2 = t3 >> 16;
    a1 += 2;
    if (t2 == 0)
        goto l13;
    if (t2 == 0x7C1F)
        goto l18;
    STORE(a1, t2);
    if (t2 == 0xFE00)
        goto l2;
    a1 += 2;
    goto l13;
l18:
    /* Escape: the next 16 bits are the run-length code itself. */
    STORE(a1, v0 >> 16);
    a1 += 2;
    t0 = *a0++;
    v0 <<= 16;
    v0 |= t0 << v1;
    goto l13;
l19:
    while ((int32_t)(a1 - vlc_end) < 0) {
        STORE(a1, 0xFE00);
        a1 += 2;
    }
    return 0;
l22:
    vlc_state.a0 = a0;
    vlc_state.a1 = a1;
    vlc_state.v0 = v0;
    vlc_state.v1 = v1;
    vlc_state.t4 = t4;
    vlc_state.t5 = t5;
    vlc_state.t7 = t7;
    vlc_state.t8 = t8;
    vlc_state.t9 = t9;
    return 1;
}
