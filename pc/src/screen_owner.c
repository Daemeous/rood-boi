/* A screen that owns the display: the bookkeeping (screen_owner.h). */
#include "vram_texels.h"
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
    pc_tex_sampling seen; /* the page, palette and box (with their versions) the answer was taken from */
    unsigned tpage, clut;
    int uv[4];
    int valid, opaque;
} cache[CACHE];
static int cache_next;

static int texels_opaque(pc_tex_view view, const pc_tex_sampling* s)
{
    for (int v = s->box[1]; v <= s->box[3]; v++)
        for (int u = s->box[0]; u <= s->box[2]; u++)
            if (pc_tex_texel(view, s, u, v) == 0)
                return 0;
    return 1;
}

int pc_screen_owner_texels_opaque(pc_tex_view view, unsigned tpage, unsigned clut, const int uv[4])
{
    if (uv[2] < uv[0] || uv[3] < uv[1] || !view.image)
        return 0;
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valid && cache[i].tpage == tpage && cache[i].clut == clut && !memcmp(cache[i].uv, uv, sizeof cache[i].uv)
            && view.versions && pc_tex_box_current(view.versions, &cache[i].seen)
            && (cache[i].seen.palette.id == 0 || pc_tex_current(view.versions, cache[i].seen.palette)))
            return cache[i].opaque;
    static const pc_tex_versions none;
    pc_tex_sampling s = pc_tex_describe(view.versions ? view.versions : &none, tpage, clut, 0, uv);
    int opaque = texels_opaque(view, &s);
    if (!view.versions)
        return opaque;
    int at = cache_next++ % CACHE;
    cache[at].seen = s, cache[at].tpage = tpage, cache[at].clut = clut, cache[at].valid = 1, cache[at].opaque = opaque;
    memcpy(cache[at].uv, uv, sizeof cache[at].uv);
    return opaque;
}
