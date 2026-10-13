/* Scene capture for lighting: the current room's polygons in world space with
 * their baked vertex colours and texture references, plus the camera of the
 * latest frame. The original renders rooms straight from packed group-local
 * geometry with camera-composed matrices (func_8009723C); the same data with
 * an identity camera gives world space. Captured once per room data block;
 * the camera every frame. Consumers: the RPC (scene, scene_dump), light
 * estimation, and the hardware renderer's lighting. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    short m[3][3];
    int t[3];
} pc_matrix; /* libgte MATRIX layout on the 32-bit host */
typedef struct {
    short vx, vy, vz, pad;
} pc_svector;
extern pc_matrix* RotMatrixYXZ_gte(pc_svector*, pc_matrix*);
extern unsigned int CFC2(int reg);

#include "scene_capture.h"
#include "room_effects.h"

static scene_triangle* triangles;
static int triangle_count, triangle_capacity;
static const void* captured_data;
static unsigned captured_signature;
static int scene_serial;
static float camera_rotation[3][3], camera_translation[3];
extern float pc_gte_view_ofx(void);
static float camera_h, camera_ofx, camera_ofy;
static int camera_valid;
static unsigned camera_facing; /* scratch 0x1F800010: cutaway direction mask */
unsigned pc_scene_facing(void) { return camera_facing; }

static void add_triangle(const scene_triangle* t)
{
    if (triangle_count == triangle_capacity) {
        triangle_capacity = triangle_capacity ? triangle_capacity * 2 : 4096;
        triangles = realloc(triangles, (size_t)triangle_capacity * sizeof *triangles);
        if (!triangles) {
            triangle_count = triangle_capacity = 0;
            return;
        }
    }
    triangles[triangle_count++] = *t;
}

static unsigned signature(const unsigned char* data)
{
    unsigned count = *(const unsigned*)data, hash = count * 2654435761u;
    for (unsigned i = 0; i < count && i < 64; ++i)
        hash = (hash ^ *(const unsigned*)(data + 4 + i * 0x40 + 0x1c)) * 16777619u;
    return hash;
}

/* Group-local vertices exactly as room_geometry.c decodes them. */
static void decode(const unsigned* poly, unsigned shift, int out[4][3], int quad)
{
    int x = (short)poly[0], y = (short)(poly[0] >> 16), z = (short)poly[1];
    const signed char* d = (const signed char*)poly + 6;
    out[0][0] = x, out[0][1] = y, out[0][2] = z;
    for (int i = 1; i < 3; ++i, d += 3)
        for (int k = 0; k < 3; ++k)
            out[i][k] = (k == 0 ? x : k == 1 ? y : z) + (int)((unsigned)(int)d[k] << shift);
    if (quad) {
        d = (const signed char*)(poly + 8);
        for (int k = 0; k < 3; ++k)
            out[3][k] = (k == 0 ? x : k == 1 ? y : z) + (int)((unsigned)(int)d[k] << shift);
    }
}

/* Called by func_8009723C with the room geometry block before it is drawn. */
void pc_scene_room(const void* data)
{
    const unsigned char* bytes = data;
    unsigned count = *(const unsigned*)bytes, sig;
    if (count == 0 || count > 4096)
        return;
    sig = signature(bytes);
    if (data == captured_data && sig == captured_signature)
        return;
    captured_data = data;
    captured_signature = sig;
    triangle_count = 0;
    ++scene_serial;
    for (unsigned g = 0; g < count; ++g) {
        const unsigned char* group = bytes + 4 + g * 0x40;
        unsigned flags = *(const unsigned*)group;
        if (flags & 0x500) /* disabled, or a camera-facing plane */
            continue;
        const int* position = (const int*)(group + 4);
        const int* angles = (const int*)(group + 0x10);
        pc_svector rotation = { (short)angles[0], (short)angles[1], (short)angles[2], 0 };
        pc_matrix world;
        RotMatrixYXZ_gte(&rotation, &world);
        unsigned shift = flags & 0x800 ? 0 : 3;
        const unsigned* geometry = (const unsigned*)(bytes + *(const unsigned*)(group + 0x1c));
        unsigned tris = geometry[0], quads = geometry[1];
        if (tris > 65535 || quads > 65535)
            continue;
        const unsigned* poly = geometry + 2;
        for (unsigned i = 0; i < tris + quads; ++i) {
            int quad = i >= tris, local[4][3];
            decode(poly, shift, local, quad);
            unsigned colors[4] = { poly[3], poly[4], poly[5], quad ? poly[9] : 0 };
            unsigned uv[4] = { poly[6], poly[7], (poly[4] >> 24) | ((poly[5] >> 24) << 8),
                quad ? (poly[8] >> 24) | ((poly[9] >> 24) << 8) : 0 };
            scene_vertex v[4];
            for (int j = 0; j < (quad ? 4 : 3); ++j) {
                for (int r = 0; r < 3; ++r)
                    v[j].position[r] = (world.m[r][0] * (float)local[j][0]
                                           + world.m[r][1] * (float)local[j][1]
                                           + world.m[r][2] * (float)local[j][2])
                                           / 4096.0f
                                     + (float)position[r];
                v[j].rgb[0] = colors[j] & 0xff;
                v[j].rgb[1] = (colors[j] >> 8) & 0xff;
                v[j].rgb[2] = (colors[j] >> 16) & 0xff;
                v[j].u = uv[j] & 0xff;
                v[j].v = (uv[j] >> 8) & 0xff;
            }
            scene_triangle t;
            t.tpage = (uint16_t)(poly[7] >> 16);
            t.clut = (uint16_t)(poly[6] >> 16);
            t.group = (uint16_t)g;
            t.flags = flags;
            t.code = (uint8_t)(poly[3] >> 24);
            t.quad = quad ? 1 : 0;
            t.polygon = (uint32_t)((const unsigned char*)poly - bytes);
            t.vertex[0] = v[0], t.vertex[1] = v[1], t.vertex[2] = v[2];
            add_triangle(&t);
            if (quad) {
                t.quad = 2;
                t.vertex[0] = v[1], t.vertex[1] = v[3], t.vertex[2] = v[2];
                add_triangle(&t);
            }
            poly += quad ? 10 : 8;
        }
    }
    fprintf(stderr, "[scene] room geometry captured: %d triangles\n", triangle_count);
    if (getenv("ROOD_SCENE_DEBUG")) {
        const pc_matrix* cam = (const pc_matrix*)0x1f800014;
        for (unsigned g = 0; g < count && g < 3; ++g) {
            const unsigned char* group = bytes + 4 + g * 0x40;
            const pc_matrix* actual = (const pc_matrix*)(group + 0x20);
            const int* position = (const int*)(group + 4);
            const int* angles = (const int*)(group + 0x10);
            fprintf(stderr, "[scene] group %u flags %08x pos %d %d %d ang %d %d %d\n", g,
                *(const unsigned*)group, position[0], position[1], position[2], angles[0], angles[1], angles[2]);
            fprintf(stderr, "[scene]   actual R %d %d %d / %d %d %d / %d %d %d T %d %d %d\n",
                actual->m[0][0], actual->m[0][1], actual->m[0][2], actual->m[1][0], actual->m[1][1],
                actual->m[1][2], actual->m[2][0], actual->m[2][1], actual->m[2][2], actual->t[0],
                actual->t[1], actual->t[2]);
            fprintf(stderr, "[scene]   camera R %d %d %d / %d %d %d / %d %d %d T %d %d %d\n",
                cam->m[0][0], cam->m[0][1], cam->m[0][2], cam->m[1][0], cam->m[1][1], cam->m[1][2],
                cam->m[2][0], cam->m[2][1], cam->m[2][2], cam->t[0], cam->t[1], cam->t[2]);
        }
    }
}

/* ---- animated UVs (room texture effects) ---- */

static int* animated; /* triangle indices */
static int animated_count, animated_capacity, animated_scene = -1, uv_serial;
static unsigned animated_generation;
/* Once per game frame (room_effects.c, after its update): the UVs of the
 * triangles whose polygons an effect rewrites, as drawn. A modelled effect's
 * offset (overrides included) on its base UVs; another effect's polygon
 * (not modelled) its bytes as the game wrote them. */
void pc_scene_uv_refresh(void)
{
    static const int u_byte[4] = { 0x18, 0x1c, 0x13, 0x23 }, v_byte[4] = { 0x19, 0x1d, 0x17, 0x27 };
    int count = 0;
    const pc_uv_effect* effects = pc_uv_effects(&count);
    if (!captured_data)
        return;
    unsigned generation = pc_uv_effects_generation();
    if (animated_scene != scene_serial || animated_generation != generation) {
        animated_scene = scene_serial;
        animated_generation = generation;
        animated_count = 0;
        for (int i = 0; count && i < triangle_count; i++) {
            int hit = 0;
            for (int e = 0; e < count && !hit; e++)
                hit = effects[e].polygon == triangles[i].polygon;
            if (!hit)
                continue;
            if (animated_count == animated_capacity) {
                int capacity = animated_capacity ? animated_capacity * 2 : 64;
                int* grown = realloc(animated, (size_t)capacity * sizeof *grown);
                if (!grown)
                    break;
                animated = grown;
                animated_capacity = capacity;
            }
            animated[animated_count++] = i;
        }
    }
    int changed = 0;
    for (int n = 0; n < animated_count; n++) {
        scene_triangle* t = &triangles[animated[n]];
        const pc_uv_effect* x = NULL;
        for (int e = 0; e < count && !x; e++)
            if (effects[e].polygon == t->polygon)
                x = &effects[e];
        if (!x)
            continue;
        /* the triangle's corners in the polygon (a quad's second half: 1, 3, 2) */
        static const int corners[3][3] = { { 0, 1, 2 }, { 0, 1, 2 }, { 1, 3, 2 } };
        const unsigned char* p = (const unsigned char*)captured_data + t->polygon;
        for (int j = 0; j < 3; j++) {
            int k = corners[t->quad < 3 ? t->quad : 0][j];
            uint8_t u = x->modelled ? (uint8_t)(x->u[k] + x->offset[0]) : p[u_byte[k]];
            uint8_t v = x->modelled ? (uint8_t)(x->v[k] + x->offset[1]) : p[v_byte[k]];
            if (t->vertex[j].u != u || t->vertex[j].v != v) {
                t->vertex[j].u = u, t->vertex[j].v = v;
                changed = 1;
            }
        }
    }
    uv_serial += changed;
}
const int* pc_scene_animated(int* count)
{
    *count = animated_scene == scene_serial ? animated_count : 0;
    return animated;
}
int pc_scene_uv_serial(void) { return uv_serial; }
int pc_scene_triangle_at(const float p[3], float slack)
{
    int best = -1;
    float best_d = slack;
    for (int i = 0; i < triangle_count; i++) {
        const float *a = triangles[i].vertex[0].position, *b = triangles[i].vertex[1].position,
                    *c = triangles[i].vertex[2].position;
        float e0[3], e1[3], q[3], n[3];
        for (int k = 0; k < 3; k++)
            e0[k] = b[k] - a[k], e1[k] = c[k] - a[k], q[k] = p[k] - a[k];
        n[0] = e0[1] * e1[2] - e0[2] * e1[1];
        n[1] = e0[2] * e1[0] - e0[0] * e1[2];
        n[2] = e0[0] * e1[1] - e0[1] * e1[0];
        float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
        if (nn <= 0)
            continue;
        float d = fabsf(q[0] * n[0] + q[1] * n[1] + q[2] * n[2]) / sqrtf(nn);
        if (d > best_d)
            continue;
        /* barycentric inside test (a little slack at the edges) */
        float d00 = e0[0] * e0[0] + e0[1] * e0[1] + e0[2] * e0[2], d01 = e0[0] * e1[0] + e0[1] * e1[1] + e0[2] * e1[2];
        float d11 = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2], d20 = q[0] * e0[0] + q[1] * e0[1] + q[2] * e0[2];
        float d21 = q[0] * e1[0] + q[1] * e1[1] + q[2] * e1[2], den = d00 * d11 - d01 * d01;
        if (den == 0)
            continue;
        float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
        if (v < -0.02f || w < -0.02f || v + w > 1.02f)
            continue;
        best = i, best_d = d;
    }
    return best;
}

/* Called each frame with the camera view matrix (0x1F800014) in effect. */
void pc_scene_camera(const void* view)
{
    const pc_matrix* m = view;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            camera_rotation[r][c] = m->m[r][c] / 4096.0f;
        camera_translation[r] = (float)m->t[r];
    }
    camera_h = (float)(CFC2(26) & 0xffff);
    camera_ofx = pc_gte_view_ofx(); /* widescreen: as narrowed (PsyX_GTE.cpp) */
    camera_ofy = (int)CFC2(25) / 65536.0f;
    camera_valid = 1;
    camera_facing = *(const unsigned*)0x1f800010;
}

/* ---- queries (RPC, light estimation, renderer) ---- */

int pc_scene_triangles(void) { return triangle_count; }
/* The room data block last captured (the native room draws only it). */
const void* pc_scene_room_data(void) { return captured_data; }
const scene_triangle* pc_scene_triangle_data(int* count)
{
    *count = triangle_count;
    return triangles;
}
/* World-space positions, 9 floats per triangle, for shadow rendering; valid
 * until the next room capture (compare pc_scene_serial). */
const float* pc_scene_positions(int* count)
{
    static float* positions;
    static int positions_serial = -1, capacity, opaque;
    if (positions_serial != scene_serial) {
        if (triangle_count > capacity) {
            capacity = triangle_count;
            positions = realloc(positions, (size_t)capacity * 9 * sizeof(float));
        }
        /* Translucent surfaces (glows, glass) don't cast. */
        opaque = 0;
        for (int i = 0; positions && i < triangle_count; i++) {
            if (triangles[i].code & 2)
                continue;
            for (int j = 0; j < 3; j++)
                memcpy(positions + opaque * 9 + j * 3, triangles[i].vertex[j].position, 3 * sizeof(float));
            ++opaque;
        }
        positions_serial = scene_serial;
    }
    *count = positions ? opaque : 0;
    return positions;
}
/* While an interpolated frame is presented, its camera (gpu_hw.c). */
extern int pc_hw_replay_camera(float r[3][3], float t[3]) __attribute__((weak));
extern int pc_hw_replay_projection(float* h, float* ofx, float* ofy) __attribute__((weak));
/* The camera the presented picture was drawn with (gpu_hw.c): the game
 * shows each frame a frame after building it, so while the camera moves the
 * GTE camera captured here is ahead of the picture. */
extern int pc_hw_view_camera(float r[3][3], float t[3], float* h, float* ofx, float* ofy) __attribute__((weak));
/* The GTE camera of the frame the game is building (no overrides). */
int pc_scene_game_camera(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy)
{
    if (!camera_valid)
        return 0;
    memcpy(rotation, camera_rotation, sizeof camera_rotation);
    memcpy(translation, camera_translation, sizeof camera_translation);
    *h = camera_h, *ofx = camera_ofx, *ofy = camera_ofy;
    return 1;
}
/* The camera of the picture being presented (or last presented): the one
 * its surfaces were drawn with, else (no native room) the replay's or the
 * game's. Lighting, path tracing and the editor reconstruct world positions
 * from the picture's depth through it. */
int pc_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy)
{
    if (!pc_scene_game_camera(rotation, translation, h, ofx, ofy))
        return 0;
    if (pc_hw_view_camera && pc_hw_view_camera(rotation, translation, h, ofx, ofy))
        return 1;
    if (pc_hw_replay_camera)
        pc_hw_replay_camera(rotation, translation);
    if (pc_hw_replay_projection)
        pc_hw_replay_projection(h, ofx, ofy);
    return 1;
}
/* Inverse of a camera's 3x3 (its rows carry different scales: X is
 * stretched for the display's pixel aspect, so R^T / s^2 is not enough). */
static int invert_camera(const float r[3][3], float inverse[3][3])
{
    float c[3][3] = {
        { r[1][1] * r[2][2] - r[1][2] * r[2][1], r[0][2] * r[2][1] - r[0][1] * r[2][2], r[0][1] * r[1][2] - r[0][2] * r[1][1] },
        { r[1][2] * r[2][0] - r[1][0] * r[2][2], r[0][0] * r[2][2] - r[0][2] * r[2][0], r[0][2] * r[1][0] - r[0][0] * r[1][2] },
        { r[1][0] * r[2][1] - r[1][1] * r[2][0], r[0][1] * r[2][0] - r[0][0] * r[2][1], r[0][0] * r[1][1] - r[0][1] * r[1][0] },
    };
    float det = r[0][0] * c[0][0] + r[0][1] * c[1][0] + r[0][2] * c[2][0];
    if (fabsf(det) < 1e-9f)
        return 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inverse[i][j] = c[i][j] / det;
    return 1;
}
int pc_scene_camera_inverse(float inverse[3][3])
{
    float r[3][3], t[3], h, ofx, ofy;
    return pc_scene_camera_get(r, t, &h, &ofx, &ofy) && invert_camera(r, inverse);
}
/* The game camera's inverse and projection (GTE drawing as it happens:
 * packets are unprojected with the camera in effect). */
int pc_scene_game_inverse(float inverse[3][3]) { return camera_valid && invert_camera(camera_rotation, inverse); }
int pc_scene_game_projection(float* h, float* ofx, float* ofy)
{
    if (!camera_valid || camera_h <= 0)
        return 0;
    *h = camera_h, *ofx = camera_ofx, *ofy = camera_ofy;
    return 1;
}
int pc_scene_projection(float* h, float* ofx, float* ofy)
{
    float r[3][3], t[3];
    if (!camera_valid || camera_h <= 0)
        return 0;
    return pc_scene_camera_get(r, t, h, ofx, ofy);
}
int pc_scene_serial(void) { return scene_serial; }
void pc_scene_bounds(float lo[3], float hi[3])
{
    /* Cached per capture: the present passes ask every frame. */
    static float cached_lo[3], cached_hi[3];
    static int cached_serial = -1, cached_count = -1;
    if (cached_serial != scene_serial || cached_count != triangle_count) {
        for (int k = 0; k < 3; ++k)
            cached_lo[k] = triangle_count ? 1e30f : 0, cached_hi[k] = triangle_count ? -1e30f : 0;
        for (int i = 0; i < triangle_count; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) {
                    float p = triangles[i].vertex[j].position[k];
                    cached_lo[k] = p < cached_lo[k] ? p : cached_lo[k];
                    cached_hi[k] = p > cached_hi[k] ? p : cached_hi[k];
                }
        cached_serial = scene_serial, cached_count = triangle_count;
    }
    for (int k = 0; k < 3; ++k)
        lo[k] = cached_lo[k], hi[k] = cached_hi[k];
}
/* The room's main floor height (world y, y down): of its level faces, the
 * height (in 8-unit bins) that has the most area. The ground fog lies on
 * it (atmosphere.cpp). Returns 0 without a captured room. */
int pc_scene_floor(float* y)
{
    static float cached;
    static int cached_serial = -1, cached_count = -1, cached_ok;
    if (cached_serial != scene_serial || cached_count != triangle_count) {
        enum { BINS = 1024 };
        static float area[BINS];
        float lo[3], hi[3];
        pc_scene_bounds(lo, hi);
        memset(area, 0, sizeof area);
        float bin = 8.0f;
        for (int i = 0; i < triangle_count; ++i) {
            const float *a = triangles[i].vertex[0].position, *b = triangles[i].vertex[1].position,
                        *c = triangles[i].vertex[2].position;
            float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            float n[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len < 1e-3f || fabsf(n[1]) < 0.9f * len)
                continue; /* not level */
            int k = (int)(((a[1] + b[1] + c[1]) / 3.0f - lo[1]) / bin);
            if (k >= 0 && k < BINS)
                area[k] += len * 0.5f;
        }
        int best = -1;
        for (int k = 0; k < BINS; ++k)
            if (area[k] > 0 && (best < 0 || area[k] > area[best]))
                best = k;
        cached_ok = best >= 0;
        cached = cached_ok ? fminf(lo[1] + (best + 1) * bin, hi[1]) : 0; /* the bin's lower edge (larger y) */
        cached_serial = scene_serial, cached_count = triangle_count;
    }
    *y = cached;
    return cached_ok;
}
/* The floor seen from above (scene_capture.h). Up-facing: the room's
 * polygons wind one way seen from their front (b - a) x (c - a) has +y (y
 * down) on a floor seen from above, -y on a ceiling or an arch's underside;
 * a room whose level faces all wind the other way takes them all. */
int pc_scene_floor_grid(float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID], float corner[2], float cell[2])
{
    enum { N = PC_SCENE_FLOOR_GRID };
    static float cached[N * N], cached_corner[2], cached_cell[2];
    static int cached_serial = -1, cached_count = -1, cached_ok;
    if (cached_serial != scene_serial || cached_count != triangle_count) {
        cached_serial = scene_serial, cached_count = triangle_count, cached_ok = 0;
        float lo[3], hi[3];
        pc_scene_bounds(lo, hi);
        cached_corner[0] = lo[0], cached_corner[1] = lo[2];
        cached_cell[0] = fmaxf((hi[0] - lo[0]) / N, 1.0f), cached_cell[1] = fmaxf((hi[2] - lo[2]) / N, 1.0f);
        static float lowest[N * N], grown[N * N];
        static unsigned char has[N * N], next[N * N];
        float up_area = 0, all_area = 0;
        for (int pass = 0; pass < 2; ++pass) {
            /* pass 0 counts; pass 1 rasterises (up-facing only, unless almost none are) */
            int up_only = pass == 1 && up_area > all_area * 0.05f;
            memset(has, 0, sizeof has);
            for (int t = 0; t < triangle_count; ++t) {
                const float *a = triangles[t].vertex[0].position, *b = triangles[t].vertex[1].position,
                            *c = triangles[t].vertex[2].position;
                float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
                float n[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
                float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len < 1e-3f || fabsf(n[1]) < 0.8f * len)
                    continue; /* not level (stairs' treads and gentle slopes are) */
                if (pass == 0) {
                    all_area += len, up_area += n[1] > 0 ? len : 0;
                    continue;
                }
                if (up_only && n[1] <= 0)
                    continue;
                /* the cells whose centre it covers, and the cells of its corners and centroid */
                float y = (a[1] + b[1] + c[1]) / 3.0f;
                float px[4] = { a[0], b[0], c[0], (a[0] + b[0] + c[0]) / 3 }, pz[4] = { a[2], b[2], c[2], (a[2] + b[2] + c[2]) / 3 };
                int i0 = N, i1 = -1, j0 = N, j1 = -1;
                for (int k = 0; k < 4; ++k) {
                    int i = (int)((px[k] - lo[0]) / cached_cell[0]), j = (int)((pz[k] - lo[2]) / cached_cell[1]);
                    i = i < 0 ? 0 : i >= N ? N - 1 : i, j = j < 0 ? 0 : j >= N ? N - 1 : j;
                    i0 = i < i0 ? i : i0, i1 = i > i1 ? i : i1, j0 = j < j0 ? j : j0, j1 = j > j1 ? j : j1;
                    int at = j * N + i;
                    if (!has[at] || y > lowest[at])
                        lowest[at] = y, has[at] = 1;
                }
                float area2 = u[0] * v[2] - u[2] * v[0]; /* twice the signed area in x, z */
                if (fabsf(area2) < 1e-6f)
                    continue;
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i) {
                        float x = lo[0] + (i + 0.5f) * cached_cell[0] - a[0], z = lo[2] + (j + 0.5f) * cached_cell[1] - a[2];
                        float s = (x * v[2] - z * v[0]) / area2, r = (u[0] * z - u[2] * x) / area2;
                        if (s < 0 || r < 0 || s + r > 1)
                            continue;
                        int at = j * N + i;
                        if (!has[at] || y > lowest[at])
                            lowest[at] = y, has[at] = 1;
                    }
            }
        }
        int any = 0;
        for (int k = 0; k < N * N; ++k)
            any |= has[k];
        if (any) {
            /* each cell the lowest of its 3 x 3 (cells with a floor) */
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N; ++i) {
                    int at = j * N + i, found = 0;
                    float y = 0;
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di) {
                            int x = i + di, z = j + dj;
                            if (x < 0 || z < 0 || x >= N || z >= N || !has[z * N + x])
                                continue;
                            float h = lowest[z * N + x];
                            if (!found || h > y)
                                y = h, found = 1;
                        }
                    grown[at] = y, next[at] = (unsigned char)found;
                }
            /* cells still without one: the mean of their neighbours', growing outward */
            for (int round = 0; round < 2 * N; ++round) {
                int missing = 0;
                memcpy(has, next, sizeof has);
                for (int j = 0; j < N; ++j)
                    for (int i = 0; i < N; ++i) {
                        int at = j * N + i;
                        if (has[at])
                            continue;
                        float sum = 0;
                        int n = 0;
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di) {
                                int x = i + di, z = j + dj;
                                if (x >= 0 && z >= 0 && x < N && z < N && has[z * N + x])
                                    sum += grown[z * N + x], n++;
                            }
                        if (n)
                            grown[at] = sum / n, next[at] = 1;
                        else
                            missing++;
                    }
                if (!missing)
                    break;
            }
            memcpy(cached, grown, sizeof cached);
            cached_ok = 1;
        }
    }
    if (cached_ok) {
        memcpy(heights, cached, sizeof cached);
        corner[0] = cached_corner[0], corner[1] = cached_corner[1];
        cell[0] = cached_cell[0], cell[1] = cached_cell[1];
    }
    return cached_ok;
}
/* Wavefront OBJ with per-vertex colours (x y z r g b) and a comment header
 * carrying the camera; texture page and CLUT per face as comments. */
int pc_scene_dump(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# rood-boi scene capture serial %d, %d triangles\n", scene_serial, triangle_count);
    if (camera_valid)
        fprintf(f,
            "# camera R %g %g %g %g %g %g %g %g %g T %g %g %g H %g OF %g %g\n",
            camera_rotation[0][0], camera_rotation[0][1], camera_rotation[0][2],
            camera_rotation[1][0], camera_rotation[1][1], camera_rotation[1][2],
            camera_rotation[2][0], camera_rotation[2][1], camera_rotation[2][2],
            camera_translation[0], camera_translation[1], camera_translation[2], camera_h,
            camera_ofx, camera_ofy);
    for (int i = 0; i < triangle_count; ++i)
        for (int j = 0; j < 3; ++j) {
            const scene_vertex* v = &triangles[i].vertex[j];
            fprintf(f, "v %g %g %g %.4f %.4f %.4f\n", v->position[0], v->position[1],
                v->position[2], v->rgb[0] / 255.0, v->rgb[1] / 255.0, v->rgb[2] / 255.0);
        }
    for (int i = 0; i < triangle_count; ++i)
        fprintf(f, "f %d %d %d # tpage %04x clut %04x group %d flags %08x\n", i * 3 + 1, i * 3 + 2,
            i * 3 + 3, triangles[i].tpage, triangles[i].clut, triangles[i].group, triangles[i].flags);
    return fclose(f) ? -1 : 0;
}
