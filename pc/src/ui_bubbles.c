/* Vector talk bubbles (ui_bubbles vector; README "2D / UI").
 *
 * The game draws a talk bubble's frame each frame in BATTLE.PRG
 * func_800CD3E4 from a shape table (D_800EB588[shape]): outline vertices
 * placed in the box's rectangle (mirrored by the flip bits), a tail part
 * spliced into the outline, then
 *
 * - a drop shadow (OT slot 1): each fill polygon at +2, +2 in black, each
 *   with a black one-pixel polyline around it;
 * - the bubble (OT slot 2): the fill polygons, textured with a 64 x 64
 *   paper texture anchored to the screen (U, V = X, Y modulo 64 through a
 *   texture window) at the box's brightness, and one dark line (GP0 0x40)
 *   per outline edge on top;
 * - the text (slot 3), drawn by the font routines.
 *
 * Upscaled, its one-pixel lines are where the internal resolution shows: a
 * thin line with steps beside smooth polygon edges. ui_bubbles vector
 * replaces the frame's packets in the hardware target (the CPU rasterizer
 * still draws them; native VRAM stays the PS1's) with
 *
 * - the shadow: the fill polygons offset as the game offsets them, black,
 *   and an anti-aliased black outline along the offset outline;
 * - the fill: the game's own polygons, textured exactly as its packets are
 *   (page, CLUT, window, brightness; nearest), so the paper look stays;
 * - the outline: one closed anti-aliased stroke along the outline through
 *   the game's vertices (the fill's edges run along its centre, so no fill
 *   shows outside it), ui_bubble_outline native pixels thick (plus
 *   ui_outline_boost output pixels), round where the outline turns.
 *
 * The geometry is the game's of that frame: func_800CD3E4 reports the box,
 * the shape and the packets it built (pc_bubble_frame), and this file
 * places the shape's vertices exactly as the routine does. With
 * ui_subpixel the box's unrounded 16.16 rectangle (func_800CB83C,
 * pc_bubble_rect) places them between pixels (smooth zoom); otherwise the
 * whole-pixel points are the packets' own. Each bubble is checked against
 * its packets (the whole-pixel placement must reproduce every line and
 * polygon); a bubble that does not match is drawn as the original. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_bubbles.h"

extern int pc_hw_scale, pc_hw_active, pc_ui_subpixel;
extern void pc_hw_set_2d(int is_2d);

int pc_ui_bubbles = PC_UI_BUBBLES_ORIGINAL;
float pc_ui_bubble_outline = 1.0f;
const char* const pc_ui_bubbles_names[2] = { "original", "vector" };

#define MAX_POINTS 96
#define MAX_PATH 128
#define MAX_TRIANGLES 128
#define MAX_BUBBLES 8

typedef struct {
    uintptr_t start, end; /* the frame's packets */
    int id, shape, flip, tail, valid;
    unsigned shadow_drawn, main_drawn; /* ordering table serial last drawn */
    int point_count, path_count, triangle_count;
    float points[MAX_POINTS][2]; /* screen pixels (before the drawing offset) */
    int game_points[MAX_POINTS][2];
    int path[MAX_PATH]; /* closed outline: point indices */
    int triangles[MAX_TRIANGLES][3];
    int align[MAX_TRIANGLES][2]; /* each fill polygon's texture origin (the game's, X and Y down to 64) */
    unsigned fill_rgb, line_rgb, shadow_rgb, tpage, clut, window;
} bubble;

static bubble pending[MAX_BUBBLES], active[MAX_BUBBLES];
static int pending_count, active_count;
static unsigned ot_serial = 1;
static struct {
    int set;
    int x, y, w, h; /* 16.16 */
} rects[MAX_BUBBLES];
static pc_ui_bubbles_stats stats;
/* ROOD_UI_DEBUG: 1 reports bubbles drawn as the original, 5 also each
 * bubble's points, outline and polygons and the packet that did not match. */
static int debug_level(void)
{
    static int level = -1;
    if (level < 0)
        level = getenv("ROOD_UI_DEBUG") ? atoi(getenv("ROOD_UI_DEBUG")) : 0;
    return level;
}
static bubble last_drawn[MAX_BUBBLES];
static int last_drawn_count;

void pc_bubble_rect(int id, int x, int y, int w, int h)
{
    if (id < 0 || id >= MAX_BUBBLES)
        return;
    rects[id].set = 1;
    rects[id].x = x, rects[id].y = y, rects[id].w = w, rects[id].h = h;
}

/* func_800CD3A0: a shape vertex (x | y << 8, 0-255 of the box) mirrored. */
static int flip_vertex(int v, int flip)
{
    if (flip & 1)
        v = (v & 0xFF) | ((0xFF - (v >> 8)) << 8);
    if (flip & 2)
        v = (~v & 0xFF) | (v & 0xFF00);
    return v;
}
static unsigned u32_at(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static int s16_at(const unsigned char* p) { return (int16_t)(p[0] | p[1] << 8); }

/* The routine's geometry: points (whole pixels as the game computes them,
 * and as placed: unrounded when sub-pixel), the outline's segments in the
 * order it draws them and its fill polygons. Returns 0 on a shape this
 * cannot follow. */
typedef struct {
    int count;
    int a[MAX_PATH], b[MAX_PATH];
} segments;
typedef struct {
    int count;
    int v[MAX_TRIANGLES][4]; /* [3] = -1: triangle */
} polygons;
static int build(bubble* B, const unsigned char* shape, int x, int y, int w, int h, const float* exact,
    segments* seg, polygons* poly)
{
    unsigned header = u32_at(shape);
    int tail = B->tail, flip = B->flip;
    int parts = (header >> 8) & 0xFF, count = shape[2], total = header & 0xFF;
    if (total > MAX_POINTS || count > total)
        return 0;
    unsigned part = u32_at(shape + tail * 4);
    const unsigned char* verts = shape + (shape[1] + 1) * 4;
#define VERT(i) (verts[(i) * 2] | verts[(i) * 2 + 1] << 8)
    float X = exact ? exact[0] : (float)x, Y = exact ? exact[1] : (float)y;
    float W = exact ? exact[2] : (float)w, H = exact ? exact[3] : (float)h;
    int i = 0;
    for (; i < count; ++i) {
        int v = flip_vertex(VERT(i), flip);
        B->game_points[i][0] = x + (((v & 0xFF) * w) >> 8);
        B->game_points[i][1] = y + (((v >> 8) * h) >> 8);
        B->points[i][0] = X + (float)(v & 0xFF) * W / 256.0f;
        B->points[i][1] = Y + (float)(v >> 8) * H / 256.0f;
    }
    if ((unsigned)(tail - 1) < (unsigned)parts) {
        int v = flip_vertex(VERT(part & 0xFF), flip);
        for (; i < total; ++i) {
            int n = flip_vertex(VERT(i), flip);
            int k = n >> 8;
            n &= 0xFF;
            if (n >= 0x80)
                n -= 0x100;
            if (k >= 0x80)
                k -= 0x100;
            B->game_points[i][0] = x + (((n + (v & 0xFF)) * w) >> 8);
            B->game_points[i][1] = y + (((k + (v >> 8)) * h) >> 8);
            B->points[i][0] = X + (float)(n + (v & 0xFF)) * W / 256.0f;
            B->points[i][1] = Y + (float)(k + (v >> 8)) * H / 256.0f;
        }
    } else
        part = 0xFFFF;
    B->point_count = i;
    if (!exact) /* whole pixels: the game's own points */
        for (int k = 0; k < i; k++)
            B->points[k][0] = (float)B->game_points[k][0], B->points[k][1] = (float)B->game_points[k][1];
    verts += total * 2;
    /* The outline, as the routine walks it. */
    int outline = (header >> 16) & 0xFF;
    seg->count = 0;
#define SEGMENT(p, q)                                                                                                \
    do {                                                                                                             \
        if (seg->count >= MAX_PATH || (p) >= B->point_count || (q) >= B->point_count)                                \
            return 0;                                                                                                \
        seg->a[seg->count] = (p), seg->b[seg->count] = (q), ++seg->count;                                            \
    } while (0)
    for (int n = 0; n < outline;) {
        if (n == (int)(part & 0xFF)) {
            int v = 0;
            for (int k = 0; k < tail; ++k)
                v += shape[k * 4 + 2];
            for (int k = 0; k < shape[tail * 4 + 2]; ++k) {
                SEGMENT(n, v + k);
                n = v + k;
            }
            v = n;
            n = (part >> 8) & 0xFF;
            SEGMENT(n, v);
        }
        int v = (n + 1) % outline;
        SEGMENT(n, v);
        n = n + 1;
    }
#undef SEGMENT
    /* The fill polygons of the main part and the tail. */
    poly->count = 0;
    for (int n = 0; n <= shape[1]; ++n)
        for (int k = 0; k < shape[n * 4 + 3]; ++k, verts += 4) {
            if (n != 0 && n != tail)
                continue;
            if (poly->count >= MAX_TRIANGLES)
                return 0;
            int* p = poly->v[poly->count++];
            for (int j = 0; j < 4; j++)
                p[j] = verts[j];
            if (p[3] == 0xFF)
                p[3] = -1;
            for (int j = 0; j < 4; j++)
                if (p[j] >= B->point_count)
                    return 0;
        }
#undef VERT
    return 1;
}

/* The routine's packets (the order it allocates them): its outline lines
 * (GP0 0x40), fill polygons (0x24 / 0x2C) and shadow packets (polygon 0x20
 * / 0x28 and black polyline 0x48) must be the geometry's. */
static int check_packets(bubble* B, const segments* seg, const polygons* poly)
{
    const uint32_t* p = (const uint32_t*)B->start;
    const uint32_t* end = (const uint32_t*)B->end;
    int lines = 0, fills = 0, shadows = 0;
    int debug = debug_level() == 5;
#define MISMATCH(what)                                                                                               \
    do {                                                                                                             \
        if (debug)                                                                                                   \
            fprintf(stderr, "[ui]   packet word %d of %d: %s (%08x)\n", (int)(p - (const uint32_t*)B->start),      \
                (int)(end - (const uint32_t*)B->start), what, p[0]);                                                 \
        return 0;                                                                                                    \
    } while (0)
    while (p < end) {
        unsigned len = p[0] >> 24;
        if (p + 1 + len > end)
            MISMATCH("past the end");
        unsigned code = len ? p[1] >> 24 : 0;
        if (len == 3 && code == 0x40) {
            if (lines >= seg->count)
                MISMATCH("extra line");
            const int* a = B->game_points[seg->a[lines]];
            const int* b = B->game_points[seg->b[lines]];
            if ((int16_t)p[2] != a[0] || (int16_t)(p[2] >> 16) != a[1] || (int16_t)p[3] != b[0]
                || (int16_t)(p[3] >> 16) != b[1])
                MISMATCH("line");
            B->line_rgb = p[1] & 0xFFFFFF;
            ++lines;
        } else if ((len == 7 || len == 9) && (code == 0x24 || code == 0x2C)) {
            if (fills >= poly->count || (code == 0x2C) != (poly->v[fills][3] >= 0))
                MISMATCH("polygon kind");
            for (int j = 0; j < (code == 0x2C ? 4 : 3); j++) {
                const int* q = B->game_points[poly->v[fills][j]];
                if ((int16_t)p[2 + j * 2] != q[0] || (int16_t)(p[2 + j * 2] >> 16) != q[1])
                    MISMATCH("polygon");
            }
            B->fill_rgb = p[1] & 0xFFFFFF;
            B->clut = p[3] >> 16;
            B->tpage = (p[5] >> 16) & 0xFFFF;
            ++fills;
        } else if (len == 12 && (code == 0x20 || code == 0x28) && (p[6] >> 24) == 0x48) {
            B->shadow_rgb = p[6] & 0xFFFFFF;
            ++shadows;
        } else if (len == 3 && code == 0xE1) {
            B->window = (p[2] >> 24) == 0xE2 ? p[2] & 0xFFFFF : 0;
        }
        /* vs_battle_insertTpage's packet is two words; its tag says
         * sizeof(sizeof(int)) / 4 words, 2 in the 64-bit build. */
        p += len && code == 0xE2 && p[1] == 0xE2000000 ? 2 : 1 + len;
    }
#undef MISMATCH
    if (debug)
        fprintf(stderr, "[ui]   packets: %d lines (%d), %d fills (%d), %d shadows\n", lines, seg->count, fills,
            poly->count, shadows);
    return lines == seg->count && fills == poly->count && shadows == poly->count && lines > 2;
}

/* The outline's segments as one closed path of point indices. */
static int chain(bubble* B, const segments* seg)
{
    if (seg->count < 3 || seg->count > MAX_PATH)
        return 0;
    int used[MAX_PATH] = { 0 };
    int at = seg->a[0];
    B->path_count = 0;
    B->path[B->path_count++] = at;
    used[0] = 1;
    at = seg->b[0];
    for (int step = 1; step < seg->count; step++) {
        B->path[B->path_count++] = at;
        int next = -1;
        for (int k = 0; k < seg->count && next < 0; k++)
            if (!used[k] && (seg->a[k] == at || seg->b[k] == at))
                next = k;
        if (next < 0)
            return 0;
        used[next] = 1;
        at = seg->a[next] == at ? seg->b[next] : seg->a[next];
    }
    return at == B->path[0];
}

void pc_bubble_frame(int id, const void* box, const void* shape, const void* start, const void* end)
{
    ++stats.frames;
    if (id < 0 || id >= MAX_BUBBLES || !box || !shape)
        return;
    const unsigned char* b = box;
    unsigned flags = u32_at(b);
    int fresh = rects[id].set;
    rects[id].set = 0;
    if (pending_count >= MAX_BUBBLES)
        return;
    bubble* B = &pending[pending_count];
    memset(B, 0, sizeof *B);
    B->start = (uintptr_t)start, B->end = (uintptr_t)end;
    B->id = id;
    B->shape = (flags >> 8) & 15, B->flip = (flags >> 6) & 3, B->tail = (flags >> 12) & 15;
    int x = s16_at(b + 0x24), y = s16_at(b + 0x26), w = s16_at(b + 0x28), h = s16_at(b + 0x2A);
    float exact[4];
    int subpixel = fresh && pc_ui_subpixel;
    if (subpixel) {
        exact[0] = (float)rects[id].x / 65536.0f, exact[1] = (float)rects[id].y / 65536.0f;
        exact[2] = (float)rects[id].w / 65536.0f, exact[3] = (float)rects[id].h / 65536.0f;
    }
    segments seg;
    polygons poly;
    B->valid = build(B, shape, x, y, w, h, subpixel ? exact : NULL, &seg, &poly) && check_packets(B, &seg, &poly)
        && chain(B, &seg);
    if (debug_level() == 5) {
        /* The geometry: points, outline segments, fill polygons. */
        fprintf(stderr, "[ui] bubble %d shape %d flip %d tail %d valid %d: %d points", id, B->shape, B->flip, B->tail,
            B->valid, B->point_count);
        for (int i = 0; i < B->point_count; i++)
            fprintf(stderr, " %d:(%d,%d)", i, B->game_points[i][0], B->game_points[i][1]);
        fprintf(stderr, "\n[ui]   outline");
        for (int i = 0; i < seg.count; i++)
            fprintf(stderr, " %d-%d", seg.a[i], seg.b[i]);
        fprintf(stderr, "\n[ui]   polygons");
        for (int i = 0; i < poly.count; i++)
            fprintf(stderr, " [%d %d %d %d]", poly.v[i][0], poly.v[i][1], poly.v[i][2], poly.v[i][3]);
        fprintf(stderr, "\n");
    }
    if (B->valid) {
        ++stats.verified;
        B->triangle_count = 0;
        for (int k = 0; k < poly.count; k++) {
            const int* v = poly.v[k];
            int ax = B->game_points[v[0]][0], ay = B->game_points[v[0]][1];
            for (int j = 1; j < (v[3] >= 0 ? 4 : 3); j++) {
                ax = B->game_points[v[j]][0] < ax ? B->game_points[v[j]][0] : ax;
                ay = B->game_points[v[j]][1] < ay ? B->game_points[v[j]][1] : ay;
            }
            /* As the routine: the texture's origin at X and Y rounded down
             * to 64 (arithmetic shifts). */
            ax = (ax >> 6) << 6, ay = (ay >> 6) << 6;
            int tris = v[3] >= 0 ? 2 : 1;
            for (int t = 0; t < tris && B->triangle_count < MAX_TRIANGLES; t++) {
                int* tri = B->triangles[B->triangle_count];
                /* FT4 corners 0 1 2 3: triangles 0 1 2 and 1 3 2. */
                tri[0] = t ? v[1] : v[0], tri[1] = t ? v[3] : v[1], tri[2] = v[2];
                B->align[B->triangle_count][0] = ax, B->align[B->triangle_count][1] = ay;
                ++B->triangle_count;
            }
        }
    } else {
        ++stats.mismatched;
        static int reported;
        if (debug_level() && reported++ < 8)
            fprintf(stderr, "[ui] bubble %d (shape %d, flip %d, tail %d) does not match its packets: original\n", id,
                B->shape, B->flip, B->tail);
    }
    ++pending_count;
}

void pc_ui_bubbles_frame(void)
{
    memcpy(active, pending, (size_t)pending_count * sizeof *active);
    active_count = pending_count;
    pending_count = 0;
    ++ot_serial;
}

/* ---- drawing ---- */

typedef struct {
    float ox, oy; /* drawing offset */
    const int* clip;
    int rgb[3];
} pen;

static void cover_triangle(const pen* P, const float* a, const float* b, const float* c, float ca, float cb, float cc)
{
    float xy[6] = { a[0] + P->ox, a[1] + P->oy, b[0] + P->ox, b[1] + P->oy, c[0] + P->ox, c[1] + P->oy };
    int uv[6] = { (int)(ca * 256.0f + 0.5f), 0, (int)(cb * 256.0f + 0.5f), 0, (int)(cc * 256.0f + 0.5f), 0 };
    int rgb[9];
    for (int i = 0; i < 3; i++)
        memcpy(rgb + i * 3, P->rgb, sizeof P->rgb);
    pc_hw_shape_triangle(xy, uv, rgb, 0x20, 0, 0, 0, P->clip, 0, 1);
}
static void cover_quad(const pen* P, const float* a, const float* b, const float* c, const float* d, float ca, float cb,
    float cc, float cd)
{
    cover_triangle(P, a, b, c, ca, cb, cc);
    cover_triangle(P, b, d, c, cb, cd, cc);
}

/* A closed anti-aliased stroke through points (screen pixels): each
 * segment a band (feathered both sides: coverage 1 across the core, 0 at
 * the outer rails), and where the path turns, the gap on the outer side
 * filled by a round wedge (core and feather), so nothing overlaps there;
 * the inner sides overlap (inside the stroke). */
static void stroke(const pen* P, const float (*pts)[2], int count, float width, float feather)
{
    float half = width * 0.5f;
    float core = half - feather * 0.5f, outer = half + feather * 0.5f, peak = 1.0f;
    if (core < 0.0f) {
        /* Thinner than the feather: a fainter line. */
        peak = width / feather;
        core = 0.0f;
        outer = feather * 0.5f;
    }
    /* Distinct points (zero-length segments dropped). */
    float p[MAX_PATH][2];
    int n = 0;
    for (int i = 0; i < count; i++) {
        if (n && fabsf(pts[i][0] - p[n - 1][0]) < 1e-4f && fabsf(pts[i][1] - p[n - 1][1]) < 1e-4f)
            continue;
        p[n][0] = pts[i][0], p[n][1] = pts[i][1];
        ++n;
    }
    while (n > 1 && fabsf(p[0][0] - p[n - 1][0]) < 1e-4f && fabsf(p[0][1] - p[n - 1][1]) < 1e-4f)
        --n;
    if (n < 2)
        return;
    float dir[MAX_PATH][2];
    for (int i = 0; i < n; i++) {
        const float* a = p[i];
        const float* b = p[(i + 1) % n];
        float dx = b[0] - a[0], dy = b[1] - a[1], len = sqrtf(dx * dx + dy * dy);
        dir[i][0] = dx / len, dir[i][1] = dy / len;
    }
    for (int i = 0; i < n; i++) {
        const float* a = p[i];
        const float* b = p[(i + 1) % n];
        float nx = -dir[i][1], ny = dir[i][0];
        float r[4] = { -outer, -core, core, outer };
        float c[4] = { 0.0f, peak, peak, 0.0f };
        float A[4][2], Bq[4][2];
        for (int k = 0; k < 4; k++) {
            A[k][0] = a[0] + nx * r[k], A[k][1] = a[1] + ny * r[k];
            Bq[k][0] = b[0] + nx * r[k], Bq[k][1] = b[1] + ny * r[k];
        }
        for (int k = 0; k < 3; k++) {
            if (k == 1 && core <= 0.0f)
                continue;
            cover_quad(P, A[k], Bq[k], A[k + 1], Bq[k + 1], c[k], c[k], c[k + 1], c[k + 1]);
        }
    }
    /* Joins: the outer side's gap between the incoming and outgoing
     * normals, a round wedge about the point. */
    float step = 2.0f * 3.14159265f / (outer * (float)pc_hw_scale * 2.0f + 8.0f);
    if (step > 0.5f)
        step = 0.5f;
    for (int i = 0; i < n; i++) {
        const float* d0 = dir[(i + n - 1) % n];
        const float* d1 = dir[i];
        float turn = d0[0] * d1[1] - d0[1] * d1[0], dot = d0[0] * d1[0] + d0[1] * d1[1];
        float angle = atan2f(turn, dot); /* > 0: turns towards +normal; the outer side is -normal */
        if (fabsf(angle) < 1e-3f)
            continue;
        float side = angle > 0 ? -1.0f : 1.0f;
        float start = atan2f(d0[0] * side, -d0[1] * side); /* the incoming normal on the outer side */
        int steps = (int)ceilf(fabsf(angle) / step);
        if (steps < 1)
            steps = 1;
        const float* c = p[i];
        for (int s = 0; s < steps; s++) {
            float t0 = start + angle * (float)s / (float)steps, t1 = start + angle * (float)(s + 1) / (float)steps;
            float e0[2] = { cosf(t0), sinf(t0) }, e1[2] = { cosf(t1), sinf(t1) };
            float i0[2] = { c[0] + e0[0] * core, c[1] + e0[1] * core }, i1[2] = { c[0] + e1[0] * core, c[1] + e1[1] * core };
            float o0[2] = { c[0] + e0[0] * outer, c[1] + e0[1] * outer },
                  o1[2] = { c[0] + e1[0] * outer, c[1] + e1[1] * outer };
            if (core > 0.0f)
                cover_triangle(P, c, i0, i1, peak, peak, peak);
            cover_quad(P, i0, i1, o0, o1, peak, peak, 0.0f, 0.0f);
        }
    }
}

static float outline_width(void)
{
    float w = pc_ui_bubble_outline < 0.25f ? 0.25f : pc_ui_bubble_outline > 8.0f ? 8.0f : pc_ui_bubble_outline;
    if (pc_ui_outline_boost > 0)
        w += (float)pc_ui_outline_boost * pc_hw_native_per_output();
    return w;
}
static float feather_width(void)
{
    /* One output pixel (at least one internal pixel). */
    float f = pc_hw_native_per_output(), internal = 1.0f / (float)(pc_hw_scale > 0 ? pc_hw_scale : 1);
    return f > internal ? f : internal;
}

static void outline_points(const bubble* B, float dx, float dy, float (*out)[2])
{
    for (int i = 0; i < B->path_count; i++) {
        out[i][0] = B->points[B->path[i]][0] + dx;
        out[i][1] = B->points[B->path[i]][1] + dy;
    }
}

static void draw_shadow(const bubble* B, int ox, int oy, const int* clip)
{
    int rgb[9];
    for (int i = 0; i < 9; i++)
        rgb[i] = (B->shadow_rgb >> ((i % 3) * 8)) & 255;
    int uv[6] = { 0 };
    for (int t = 0; t < B->triangle_count; t++) {
        float xy[6];
        for (int j = 0; j < 3; j++) {
            xy[j * 2] = B->points[B->triangles[t][j]][0] + 2.0f + (float)ox;
            xy[j * 2 + 1] = B->points[B->triangles[t][j]][1] + 2.0f + (float)oy;
        }
        pc_hw_shape_triangle(xy, uv, rgb, 0x20, 0, 0, 0, clip, 0, 0);
    }
    pen P = { (float)ox, (float)oy, clip, { (int)(B->shadow_rgb & 255), (int)((B->shadow_rgb >> 8) & 255),
                                              (int)((B->shadow_rgb >> 16) & 255) } };
    float pts[MAX_PATH][2];
    outline_points(B, 2.0f, 2.0f, pts);
    stroke(&P, (const float(*)[2])pts, B->path_count, outline_width(), feather_width());
}

static void draw_main(const bubble* B, int ox, int oy, const int* clip)
{
    int rgb[9];
    for (int i = 0; i < 9; i++)
        rgb[i] = (B->fill_rgb >> ((i % 3) * 8)) & 255;
    for (int t = 0; t < B->triangle_count; t++) {
        float xy[6];
        int uv[6];
        for (int j = 0; j < 3; j++) {
            const float* q = B->points[B->triangles[t][j]];
            xy[j * 2] = q[0] + (float)ox;
            xy[j * 2 + 1] = q[1] + (float)oy;
            /* The game's U, V: X, Y from the polygon's texture origin. */
            uv[j * 2] = (int)floorf(q[0] - (float)B->align[t][0] + 0.5f);
            uv[j * 2 + 1] = (int)floorf(q[1] - (float)B->align[t][1] + 0.5f);
        }
        pc_hw_shape_triangle(xy, uv, rgb, 0x24, B->tpage, B->clut, B->window, clip, 0, 0);
    }
    pen P = { (float)ox, (float)oy, clip, { (int)(B->line_rgb & 255), (int)((B->line_rgb >> 8) & 255),
                                              (int)((B->line_rgb >> 16) & 255) } };
    float pts[MAX_PATH][2];
    outline_points(B, 0.0f, 0.0f, pts);
    stroke(&P, (const float(*)[2])pts, B->path_count, outline_width(), feather_width());
}

static void note_drawn(const bubble* B)
{
    for (int i = 0; i < last_drawn_count; i++)
        if (last_drawn[i].id == B->id) {
            last_drawn[i] = *B;
            return;
        }
    if (last_drawn_count < MAX_BUBBLES)
        last_drawn[last_drawn_count++] = *B;
}

void pc_ui_bubble_packet(const void* packet, int draw_x, int draw_y, const int* clip)
{
    pc_hw_suppress = 0;
    if (pc_ui_bubbles != PC_UI_BUBBLES_VECTOR || !pc_hw_active)
        return;
    uintptr_t at = (uintptr_t)packet;
    for (int i = 0; i < active_count; i++) {
        bubble* B = &active[i];
        if (!B->valid || at < B->start || at >= B->end)
            continue;
        const uint32_t* p = packet;
        unsigned len = p[0] >> 24, code = len ? p[1] >> 24 : 0;
        int shadow = len == 12 && (code == 0x20 || code == 0x28);
        unsigned* drawn = shadow ? &B->shadow_drawn : &B->main_drawn;
        if (*drawn != ot_serial) {
            *drawn = ot_serial;
            pc_hw_set_2d(1);
            if (shadow)
                draw_shadow(B, draw_x, draw_y, clip);
            else {
                draw_main(B, draw_x, draw_y, clip);
                ++stats.drawn;
                note_drawn(B);
            }
        }
        pc_hw_suppress = 1;
        return;
    }
}

void pc_ui_bubbles_get_stats(pc_ui_bubbles_stats* s)
{
    *s = stats;
    s->count = last_drawn_count;
    for (int i = 0; i < last_drawn_count; i++) {
        const bubble* B = &last_drawn[i];
        s->bubble[i].id = B->id, s->bubble[i].shape = B->shape, s->bubble[i].flip = B->flip;
        s->bubble[i].tail = B->tail, s->bubble[i].valid = B->valid, s->bubble[i].drawn = 1;
        s->bubble[i].points = B->path_count;
        float b[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
        int g[4] = { 1 << 30, 1 << 30, -(1 << 30), -(1 << 30) };
        for (int k = 0; k < B->path_count; k++) {
            const float* q = B->points[B->path[k]];
            const int* r = B->game_points[B->path[k]];
            b[0] = fminf(b[0], q[0]), b[1] = fminf(b[1], q[1]), b[2] = fmaxf(b[2], q[0]), b[3] = fmaxf(b[3], q[1]);
            g[0] = r[0] < g[0] ? r[0] : g[0], g[1] = r[1] < g[1] ? r[1] : g[1];
            g[2] = r[0] > g[2] ? r[0] : g[2], g[3] = r[1] > g[3] ? r[1] : g[3];
        }
        memcpy(s->bubble[i].bounds, b, sizeof b);
        memcpy(s->bubble[i].game_bounds, g, sizeof g);
        s->bubble[i].thickness = outline_width();
        s->bubble[i].line_rgb = B->line_rgb;
    }
}

int pc_ui_bubble_path(int index, float (*points)[2], int max)
{
    if (index < 0 || index >= last_drawn_count)
        return 0;
    const bubble* B = &last_drawn[index];
    int n = B->path_count < max ? B->path_count : max;
    for (int i = 0; i < n; i++) {
        points[i][0] = B->points[B->path[i]][0];
        points[i][1] = B->points[B->path[i]][1];
    }
    return n;
}
