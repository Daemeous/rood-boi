/* The studio's stage (floor, fillet, wall: a cyclorama) as a room-format geometry block (the game's, pc/docs/Model_Studio.md): untextured
 * gouraud room polygons that every consumer of a room (the native room list, the scene capture that feeds ray tracing, the probes and
 * shadows) takes as the room. Pure C: studio.c (the in-game studio, over the loaded room) and rood_studio.c (the standalone app) both build it. */
#include "studio_stage.h"
#include "render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(unsigned char* b, unsigned at, unsigned v) { memcpy(b + at, &v, 4); }
static int snap8(double v) { return (int)(floor(v / 8.0 + 0.5)) * 8; }

/* The stage's facets are small and none is thinner than the grid the game keeps its facets on: a room facet's corners are a
 * position and steps of 8 units from it, so a ring whose neighbours are a few units apart collapses to nothing or turns
 * inside out (and is not drawn), and the renderer drops a facet that is large on the screen it is seen from. The rings of the
 * profile are therefore each given the fewest facets (a power of two, so each ring has twice or as many as the one inside it)
 * that keep them no wider than `facet` units, and a ring that has twice the facets of the one inside it is joined to it with
 * three triangles for each of the inner ring's edges. */
typedef struct {
    int rings, segs[160];
    double radius[160], height[160];
} profile_t;

static int ring_segments(double radius, double facet, int least)
{
    int n = least;
    while (n < 512 && 6.2831853 * radius / n > facet)
        n *= 2;
    return n;
}

static void stage_point(const profile_t* p, int ring, int j, short out[3])
{
    double a = 6.2831853 * (j % p->segs[ring]) / p->segs[ring];
    out[0] = (short)snap8(p->radius[ring] * cos(a));
    out[2] = (short)snap8(p->radius[ring] * sin(a));
    out[1] = (short)-snap8(p->height[ring]);
}

/* One facet at `q`: the first corner, the steps to the others (8 units each), the colours (a triangle's record is 32 bytes,
 * a quad's 40). */
static unsigned char* put_facet(unsigned char* q, const short v[4][3], int quad, unsigned colour)
{
    memcpy(q, v[0], 6);
    for (int k = 1; k < (quad ? 4 : 3); k++)
        for (int d = 0; d < 3; d++)
            ((signed char*)q)[(quad && k == 3 ? 32 : 6 + (k - 1) * 3) + d] = (signed char)((v[k][d] - v[0][d]) / 8);
    for (int k = 0; k < (quad ? 4 : 3); k++)
        put32(q, (unsigned)(quad && k == 3 ? 36 : 12 + k * 4), colour | (k == 0 ? (quad ? 0x38u : 0x30u) << 24 : 0));
    return q + (quad ? 40 : 32);
}

/* No stage: a block with one facet that faces away from every view (nothing is drawn, the scene still has a room). */
unsigned pc_stage_build_hidden(float cx, float fy, float cz, unsigned char* block, unsigned length, char* why, int why_size, char* info, int info_size)
{
    unsigned geom_at = 4 + 0x40, need = geom_at + 8 + 32;
    if (need > length) {
        snprintf(why, (unsigned long)why_size, "the room's block is too small for an empty stage");
        return 0;
    }
    memset(block, 0, need);
    put32(block, 0, 1);
    unsigned char* g = block + 4;
    put32(g, 0, 0xff);
    put32(g, 4, (unsigned)(int)cx), put32(g, 8, (unsigned)(int)fy), put32(g, 12, (unsigned)(int)cz);
    put32(g, 0x1c, geom_at);
    put32(block + geom_at, 0, 1), put32(block + geom_at, 4, 0);
    short v[4][3] = { { 0, 3000, 0 }, { 0, 3000, 8 }, { 8, 3000, 0 }, { 0, 0, 0 } }; /* (below the floor, wound to face down) */
    put_facet(block + geom_at + 8, v, 0, 0x606060);
    snprintf(info, (unsigned long)info_size, "no stage: a solid backdrop");
    return need;
}

unsigned pc_stage_build(const pc_stage_cfg* c, float cx, float fy, float cz, unsigned char* block, unsigned length, unsigned* need_out, char* why, int why_size, char* info, int info_size)
{
    double radius = c->radius, fillet = c->fillet, height = c->height;
    double facet = c->facet < 16 ? 16 : c->facet;
    if (radius < 100 || fillet < 1 || height < fillet) {
        snprintf(why, (unsigned long)why_size, "stage: radius >= 100, 1 <= fillet <= height");
        return 0;
    }
    /* the profile: the floor (rings every `floor_step`), the fillet's quarter circle, the wall (rings every `wall_step`) */
    static profile_t p;
    p.rings = 0;
    int floor_steps = (int)ceil(radius / (c->floor_step > 20 ? c->floor_step : 450.0));
    if (floor_steps > 60)
        floor_steps = 60;
    for (int i = 0; i <= floor_steps; i++)
        p.radius[p.rings] = radius * i / floor_steps, p.height[p.rings++] = 0;
    for (int i = 1; i <= 8; i++) {
        double a = 1.5707963 * i / 8;
        p.radius[p.rings] = radius + fillet * sin(a), p.height[p.rings++] = fillet * (1 - cos(a));
    }
    int wall_steps = (int)ceil((height - fillet) / (c->wall_step > 20 ? c->wall_step : 800.0));
    if (wall_steps > 60)
        wall_steps = 60;
    for (int i = 1; i <= wall_steps; i++)
        p.radius[p.rings] = radius + fillet, p.height[p.rings++] = fillet + (height - fillet) * i / wall_steps;
    p.segs[0] = 1;
    for (int i = 1; i < p.rings; i++) {
        int n = ring_segments(p.radius[i], facet, 8);
        int inside = i > 1 ? p.segs[i - 1] : 8;
        if (n > inside * 2)
            n = inside * 2; /* (a ring is at most twice as fine as the one inside it) */
        if (n < inside)
            n = inside;
        p.segs[i] = n;
    }
    unsigned tris = (unsigned)p.segs[1], quads = 0;
    for (int i = 1; i + 1 < p.rings; i++) {
        if (p.segs[i + 1] == p.segs[i])
            quads += (unsigned)p.segs[i];
        else
            tris += 3u * (unsigned)p.segs[i];
    }
    unsigned geom_at = 4 + 0x40, need = geom_at + 8 + tris * 32 + quads * 40;
    if (need_out)
        *need_out = need;
    if (need > length) {
        snprintf(why, (unsigned long)why_size, "stage needs %u bytes, the block is %u", need, length);
        return 0;
    }
    snprintf(info, (unsigned long)info_size, "stage %u of %u bytes: %u triangles, %u quads, %d rings", need, length, tris, quads, p.rings);
    memset(block, 0, need);
    put32(block, 0, 1); /* one geometry group */
    unsigned char* g = block + 4;
    put32(g, 0, 0xff); /* flags: every facing bit, not hidden */
    put32(g, 4, (unsigned)(int)cx);
    put32(g, 8, (unsigned)(int)fy);
    put32(g, 12, (unsigned)(int)cz);
    put32(g, 0x1c, geom_at);
    unsigned char* geo = block + geom_at;
    put32(geo, 0, tris), put32(geo, 4, quads);
    unsigned char* poly = geo + 8;
    unsigned grey = (unsigned)(c->grey < 0 ? 0 : c->grey > 255 ? 255 : c->grey);
    unsigned col = grey | grey << 8 | grey << 16;
    short v[4][3];
    /* the triangles: the fan about the centre, then the joins between rings of different fineness */
    memset(v[0], 0, sizeof v[0]);
    for (int j = 0; j < p.segs[1]; j++) {
        stage_point(&p, 1, j + 1, v[1]);
        stage_point(&p, 1, j, v[2]);
        poly = put_facet(poly, v, 0, col);
    }
    for (int i = 1; i + 1 < p.rings; i++) {
        if (p.segs[i + 1] == p.segs[i])
            continue;
        for (int j = 0; j < p.segs[i]; j++) {
            short a0[3], a1[3], b0[3], b1[3], b2[3];
            stage_point(&p, i, j, a0), stage_point(&p, i, j + 1, a1);
            stage_point(&p, i + 1, 2 * j, b0), stage_point(&p, i + 1, 2 * j + 1, b1), stage_point(&p, i + 1, 2 * j + 2, b2);
            memcpy(v[0], a0, 6), memcpy(v[1], b1, 6), memcpy(v[2], b0, 6);
            poly = put_facet(poly, v, 0, col);
            memcpy(v[0], a0, 6), memcpy(v[1], a1, 6), memcpy(v[2], b1, 6);
            poly = put_facet(poly, v, 0, col);
            memcpy(v[0], a1, 6), memcpy(v[1], b2, 6), memcpy(v[2], b1, 6);
            poly = put_facet(poly, v, 0, col);
        }
    }
    /* the quads between rings of the same fineness */
    for (int i = 1; i + 1 < p.rings; i++) {
        if (p.segs[i + 1] != p.segs[i])
            continue;
        for (int j = 0; j < p.segs[i]; j++) {
            stage_point(&p, i, j, v[0]), stage_point(&p, i, j + 1, v[1]);
            stage_point(&p, i + 1, j, v[2]), stage_point(&p, i + 1, j + 1, v[3]);
            poly = put_facet(poly, v, 1, col);
        }
    }
    return need;
}


/* The block's polygons for the renderer (room_native.c decode_polygons' reading of the same words): returns the count, malloc'd in *out. */
int pc_stage_polygons(const unsigned char* block, unsigned length, void** out)
{
    unsigned count = length >= 4 ? *(const unsigned*)block : 0;
    if (count != 1 || length < 4 + 0x40 + 8)
        return -1;
    unsigned at = *(const unsigned*)(block + 4 + 0x1c);
    const unsigned* geometry = (const unsigned*)(const void*)(block + at);
    unsigned tris = geometry[0], quads = geometry[1], total = tris + quads;
    render_polygon* polygons = calloc(total ? total : 1, sizeof *polygons);
    const unsigned* poly = geometry + 2;
    for (unsigned i = 0; i < total; ++i) {
        int quad = i >= tris;
        render_polygon* o = &polygons[i];
        int x = (short)poly[0], y = (short)(poly[0] >> 16), z = (short)poly[1];
        const signed char* d = (const signed char*)(const void*)poly + 6;
        const signed char* deltas[4] = { NULL, d, d + 3, (const signed char*)(const void*)(poly + 8) };
        unsigned colours[4] = { poly[3], poly[4], poly[5], quad ? poly[9] : 0 };
        for (int k = 0; k < (quad ? 4 : 3); k++) {
            int vx = x, vy = y, vz = z;
            if (k) {
                vx += deltas[k][0] * 8, vy += deltas[k][1] * 8, vz += deltas[k][2] * 8;
            }
            o->position[k][0] = (float)(short)vx, o->position[k][1] = (float)(short)vy, o->position[k][2] = (float)(short)vz;
            o->rgb[k][0] = colours[k] & 0xff, o->rgb[k][1] = (colours[k] >> 8) & 0xff, o->rgb[k][2] = (colours[k] >> 16) & 0xff;
        }
        o->code = (uint8_t)((poly[3] >> 24) & 0xfe);
        o->flags = (uint8_t)((quad ? RENDER_POLY_QUAD : 0) | ((poly[3] >> 24) & 1 ? RENDER_POLY_DOUBLE_SIDED : 0));
        poly += quad ? 10 : 8;
    }
    *out = polygons;
    return (int)total;
}
