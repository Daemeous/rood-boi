/* The model content key (src/model_key.c): a model loaded into another texture slot (its v bytes moved by whole pages) has the same key, a
 * different model, or a changed U or a changed vertex, another one; 600 distinct synthetic models do not collide. */
#include "model_key.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char obj[8];
    unsigned groups[2];
    short vertices[3][4];
    unsigned records[8 * 3];
    unsigned short counts[3];
    unsigned group_counts[12];
} Model;

/* A coloured model of one triangle and one quad whose texture coordinates start at row `v0`. */
static void make(Model* m, unsigned seed, int v0)
{
    memset(m, 0, sizeof *m);
    m->obj[0] = 1;
    m->counts[0] = 1, m->counts[1] = 1;
    memcpy(m->obj + 2, m->counts, sizeof m->counts);
    m->groups[0] = 3u << 16;
    for (int i = 0; i < 3; i++)
        m->vertices[i][0] = (short)(seed * 7 + i), m->vertices[i][1] = (short)(seed + i * 3), m->vertices[i][2] = (short)(i * seed);
    unsigned* r = m->records;
    /* a coloured triangle: 6 words (word 2's top byte is not 0x3c: not a quad), UVs in 5 and word 1's high half */
    r[2] = 0x24000000u | seed, r[3] = 24u << 24;
    r[5] = (unsigned)((10 + seed % 7) | ((v0 + 3) << 8)) | (unsigned)((20 | ((v0 + 9) << 8)) << 16);
    r[1] = (unsigned)(0x1234 | (unsigned)((30 + seed % 5) | ((v0 + 31) << 8)) << 16);
    r += 6;
    /* a coloured quad: 8 words */
    r[2] = 0x3c000000u | seed, r[3] = 32u << 24;
    r[6] = (unsigned)(4 | ((v0 + 1) << 8)) | (unsigned)((5 | ((v0 + 2) << 8)) << 16);
    r[7] = (unsigned)(6 | ((v0 + 40) << 8)) | (unsigned)((7 | ((v0 + 50) << 8)) << 16);
}
static unsigned long long key(const Model* m)
{
    return pc_model_content_key(1, m->obj, m->groups, 1, &m->vertices[0][0], 3, m->group_counts, m->records, m->counts);
}

int main(void)
{
    Model a, b;
    make(&a, 5, 0x00);
    unsigned long long k = key(&a);
    assert(k > 0 && k < (1ull << 40));
    for (int page = 1; page < 4; page++) { /* the same model at another texture slot: v bytes + 64 per slot row */
        make(&b, 5, 0x40 * page + 0x00);
        assert(key(&b) == k);
    }
    make(&b, 5, 0);
    b.records[5] ^= 1; /* a U changed */
    assert(key(&b) != k);
    make(&b, 5, 0);
    b.vertices[1][2] += 1;
    assert(key(&b) != k);
    make(&b, 5, 0);
    b.records[2] ^= 0x100; /* another record word (the colour/shape) */
    assert(key(&b) != k);
    make(&b, 6, 0);
    assert(key(&b) != k);
    static unsigned long long seen[600];
    for (unsigned s = 0; s < 600; s++) {
        make(&b, s + 1, (int)(s % 4) * 0x40);
        seen[s] = key(&b);
        for (unsigned t = 0; t < s; t++)
            assert(seen[t] != seen[s]);
    }
    puts("model_key: ok");
    return 0;
}
