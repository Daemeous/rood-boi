/* A model's content key: see model_key.h. */
#include "model_key.h"
#include <string.h>

#define FNV64 1099511628211ull
static uint64_t mix(uint64_t h, unsigned word) { return (h ^ word) * FNV64; }
static uint64_t mix_words(uint64_t h, const unsigned* words, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        h = mix(h, words[i]);
    return h;
}

/* Where a record's UV lanes are (halfwords: word, half): flat records carry them in words 2 and 3 (a triangle's third in word 2 high half), coloured
 * ones in words 5 / 6 and 7 (word 1 high for a triangle's third). */
static int uv_lanes(int colored, int quad, int word[4], int half[4])
{
    int n = 0;
    if (!colored) {
        word[n] = 3, half[n++] = 0;
        word[n] = 3, half[n++] = 1;
        if (quad)
            word[n] = 4, half[n++] = 0, word[n] = 4, half[n++] = 1;
        else
            word[n] = 2, half[n++] = 1;
    } else {
        int base = quad ? 6 : 5;
        word[n] = base, half[n++] = 0;
        word[n] = base, half[n++] = 1;
        if (quad)
            word[n] = 7, half[n++] = 0, word[n] = 7, half[n++] = 1;
        else
            word[n] = 1, half[n++] = 1;
    }
    return n;
}

uint64_t pc_model_content_key(int kind, const unsigned char* obj, const unsigned* groups, unsigned group_count, const short* vertices,
    unsigned vertex_count, const unsigned* group_counts, const unsigned* records, const unsigned short counts[3])
{
    int colored = kind == 1 || kind == 3;
    uint64_t h = (14695981039346656037ull ^ (unsigned)kind) * FNV64;
    unsigned head[2];
    memcpy(head, obj, sizeof head);
    h = mix_words(h, head, 2);
    h = mix_words(h, groups, group_count);
    for (unsigned i = 0; i < vertex_count * 2; i++) {
        unsigned w;
        memcpy(&w, (const char*)vertices + i * 4, 4);
        h = mix(h, w);
    }
    if (kind >= 2)
        h = mix_words(h, group_counts, 12);
    int ref = -1;
    const unsigned* r = records;
    for (int section = 0; section < 3; section++)
        for (unsigned face = 0; face < counts[section]; face++) {
            int quad;
            unsigned n;
            if (!colored) {
                quad = section == 1 || (section == 2 && (r[0] & 255) == 0x2c);
                n = quad ? 5 : 4;
            } else {
                quad = section == 1 || (section == 2 && (r[2] >> 24) == 0x3c);
                n = quad ? 8 : 6;
            }
            unsigned w[8];
            for (unsigned i = 0; i < n; i++)
                w[i] = r[i];
            int lw[4], lh[4];
            int lanes = uv_lanes(colored, quad, lw, lh);
            for (int k = 0; k < lanes; k++) {
                unsigned shift = lh[k] ? 16 : 0, lane = (r[lw[k]] >> shift) & 0xffff;
                int u = (int)(lane & 255), v = (int)(lane >> 8);
                if (ref < 0)
                    ref = v & 0xC0;
                unsigned moved = (unsigned)u | (unsigned)((v - ref) & 255) << 8;
                w[lw[k]] = (w[lw[k]] & ~(0xffffu << shift)) | (moved << shift);
            }
            h = mix_words(h, w, n);
            r += n;
        }
    h ^= h >> 40;
    h &= 0xffffffffffull;
    return h ? h : 1;
}
