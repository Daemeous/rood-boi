/* A screen that owns the display: the bookkeeping (screen_owner.h). */
#include "screen_owner.h"

#include <stdlib.h>
#include <string.h>

int pc_screen_owner = 1;
int pc_screen_owner_fill[3] = { 0, 0, 0 };
long pc_screen_owner_completed, pc_screen_owner_cleared;

#define MAX_W 512 /* the widest drawing area followed (a wider one is never covered) */
#define MAX_H 512
#define WORDS (MAX_W / 64)

static struct {
    int area[4];                    /* the drawing area the rows below are of */
    uint64_t row[MAX_H][WORDS];     /* opaque pixels covered, per row */
    int row_count[MAX_H];           /* ... how many in the row */
    long covered;                   /* ... in all */
    int done;                       /* completed once in this table */
    uint8_t side[2][MAX_H];         /* side rows opaque 2D reached: left, right */
} state;

/* The setting, with ROOD_SCREEN_OWNER=0 / 1 over it (comparisons of builds: build_compare --new-env). */
int pc_screen_owner_on(void)
{
    static int env = -2;
    if (env == -2)
        env = getenv("ROOD_SCREEN_OWNER") && *getenv("ROOD_SCREEN_OWNER") ? atoi(getenv("ROOD_SCREEN_OWNER")) != 0 : -1;
    return env >= 0 ? env : pc_screen_owner != 0;
}

void pc_screen_owner_begin(void)
{
    state.area[2] = state.area[3] = 0;
    state.covered = 0;
    state.done = 0;
}

static void reset_for(const int clip[4])
{
    memset(state.row, 0, sizeof state.row);
    memset(state.row_count, 0, sizeof state.row_count);
    memset(state.side, 0, sizeof state.side);
    memcpy(state.area, clip, sizeof state.area);
    state.covered = 0;
    state.done = 0;
}

static int follows(const int clip[4])
{
    if (clip[2] <= 0 || clip[3] <= 0 || clip[2] > MAX_W || clip[3] > MAX_H)
        return 0;
    if (memcmp(state.area, clip, sizeof state.area))
        reset_for(clip);
    return 1;
}

int pc_screen_owner_cover(const int clip[4], int x0, int y0, int x1, int y1)
{
    if (!follows(clip))
        return 0;
    if (x0 < clip[0])
        x0 = clip[0];
    if (y0 < clip[1])
        y0 = clip[1];
    if (x1 > clip[0] + clip[2])
        x1 = clip[0] + clip[2];
    if (y1 > clip[1] + clip[3])
        y1 = clip[1] + clip[3];
    if (x1 <= x0 || y1 <= y0 || state.done)
        return 0;
    int a = x0 - clip[0], b = x1 - clip[0];
    for (int y = y0 - clip[1]; y < y1 - clip[1]; y++) {
        int added = 0;
        for (int w = a >> 6; w <= (b - 1) >> 6; w++) {
            int lo = w == a >> 6 ? a & 63 : 0, hi = w == (b - 1) >> 6 ? (b - 1) & 63 : 63;
            uint64_t mask = (hi == 63 ? ~0ull : (1ull << (hi + 1)) - 1) & ~((1ull << lo) - 1);
            added += __builtin_popcountll(mask & ~state.row[y][w]);
            state.row[y][w] |= mask;
        }
        state.row_count[y] += added;
        state.covered += added;
    }
    if (state.covered == (long)clip[2] * clip[3]) {
        state.done = 1;
        ++pc_screen_owner_completed;
        return 1;
    }
    return 0;
}

void pc_screen_owner_side_drawn(const int clip[4], int right, int y0, int y1)
{
    if (!follows(clip))
        return;
    if (y0 < clip[1])
        y0 = clip[1];
    if (y1 > clip[1] + clip[3])
        y1 = clip[1] + clip[3];
    for (int y = y0; y < y1; y++)
        state.side[right ? 1 : 0][y - clip[1]] = 1;
}

int pc_screen_owner_side_runs(const int clip[4], int right, int runs[][2], int max)
{
    if (!follows(clip))
        return 0;
    int n = 0;
    for (int y = 0; y <= clip[3]; y++) {
        int open = y < clip[3] && !state.side[right ? 1 : 0][y];
        if (open && (n == 0 || runs[n - 1][1] != clip[1] + y)) {
            if (n == max)
                break;
            runs[n][0] = clip[1] + y, runs[n][1] = clip[1] + y + 1, ++n;
        } else if (open)
            runs[n - 1][1] = clip[1] + y + 1;
    }
    return n;
}

#define CACHE 64
static struct {
    unsigned tpage, clut;
    int uv[4];
    uint32_t tiles;
    int valid, opaque;
} cache[CACHE];
static int cache_next;

void pc_screen_owner_vram_changed(uint32_t tiles)
{
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valid && (cache[i].tiles & tiles))
            cache[i].valid = 0;
}

static int texels_opaque(const uint16_t* vram, unsigned tpage, unsigned clut, const int uv[4])
{
    int depth = (int)(tpage >> 7 & 3), tx = (int)(tpage & 15) * 64, ty = (int)(tpage >> 4 & 1) * 256;
    int cx = (int)(clut & 63) * 16, cy = (int)(clut >> 6 & 511);
    for (int v = uv[1]; v <= uv[3]; v++)
        for (int u = uv[0]; u <= uv[2]; u++) {
            size_t row = (size_t)((ty + v) & 511) * 1024;
            unsigned texel;
            if (depth == 0) {
                unsigned word = vram[row + (size_t)((tx + (u >> 2)) & 1023)];
                texel = vram[(size_t)(cy & 511) * 1024 + (size_t)((cx + (int)(word >> ((u & 3) * 4) & 15)) & 1023)];
            } else if (depth == 1) {
                unsigned word = vram[row + (size_t)((tx + (u >> 1)) & 1023)];
                texel = vram[(size_t)(cy & 511) * 1024 + (size_t)((cx + (int)(word >> ((u & 1) * 8) & 255)) & 1023)];
            } else
                texel = vram[row + (size_t)((tx + u) & 1023)];
            if (texel == 0)
                return 0;
        }
    return 1;
}

int pc_screen_owner_texels_opaque(const uint16_t* vram, unsigned tpage, unsigned clut, const int uv[4], uint32_t tiles)
{
    if (uv[2] < uv[0] || uv[3] < uv[1])
        return 0;
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valid && cache[i].tpage == tpage && cache[i].clut == clut && !memcmp(cache[i].uv, uv, sizeof cache[i].uv))
            return cache[i].opaque;
    int opaque = texels_opaque(vram, tpage, clut, uv);
    int at = cache_next++ % CACHE;
    cache[at].tpage = tpage, cache[at].clut = clut, cache[at].tiles = tiles, cache[at].valid = 1, cache[at].opaque = opaque;
    memcpy(cache[at].uv, uv, sizeof cache[at].uv);
    return opaque;
}
