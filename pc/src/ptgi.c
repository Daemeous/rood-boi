/* Path-traced global illumination (PTGI) for the game view.
 *
 * The scene is the captured room (scene_capture.c) plus this frame's
 * characters and props (the hardware path's casters), in world space, in a
 * BVH built on the CPU when either changes. A compute pass traces, from each
 * lit surface of the G-buffer, cosine-distributed paths through the scene;
 * at every hit the dynamic lights are sampled with shadow rays and the
 * surface's PS1 texel (decoded from native VRAM) colours the bounce. The
 * noisy result is accumulated over time (reprojected with the previous
 * camera, rejected where the surface differs) and smoothed with an
 * edge-aware a-trous filter; the present pass adds it to the direct light
 * as indirect irradiance.
 *
 * Units follow the present pass: a surface shows albedo * E, with E the sum
 * of colour * falloff * cosine over the lights; the indirect E at a point is
 * the mean of albedo * E over the surfaces its cosine-weighted rays reach.
 * Needs OpenGL 4.3 compute (pc_gl_compute); the game view simply has no
 * indirect light without it. */
#define _POSIX_C_SOURCE 200809L
#include "gl_api.h"
#include "scene_capture.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "glow_overrides.h"
#include "light_shape.h"
#include "emissive.h" /* emissive texels that are lights: room glows */
#include "lighting_quality.h"
#include "environment.h"
#include "atmosphere.h" /* the haze, ground fog and fog volumes (pc_medium_gpu) */
extern GLuint pc_gl_medium_texture(void); /* gl_present.c: the medium's texture */
#include "gl_shaders.h" /* PC_GLSL_ENVIRONMENT: the room's sun and sky (pc/shaders/vulkan/environment.glsl) */

extern const float* pc_hw_casters(int* count, int* serial);
extern const void* pc_hw_caster_surfaces(void);
extern GLuint pc_hw_native_texture(void);
extern GLuint pc_hw_depth_texture(void);
extern void pc_hw_gbuffer(GLuint* normal, GLuint* albedo);
extern int pc_hw_scale;
extern int pc_gte_widescreen;
extern int pc_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4],
    float extent_up[][4], int shadows[], int max);
extern int pc_lights_enabled;

/* Settings (developer overlay, RPC). */
int pc_gi_enabled = 1;
int pc_rt_shadows = 1; /* ray-traced soft shadows (else the cube maps) */
int pc_baked_shadows = 1; /* characters shadow the baked light (contact + emitters) */
/* Shadow-only lights guessed from the room's vertex-colour hotspots: off,
 * as their guessed positions threw shadows the wrong way (beside a candle,
 * away from some patch of light). Shadows come from the glows, plus contact. */
int pc_baked_estimate;
int pc_baked_shadows_active; /* ...and did last frame (the drop shadow steps aside) */
float pc_contact_radius = 64.0f;
float pc_baked_ambient = 0.35f;
int pc_shadow_samples = 2; /* shadow rays per light per pixel per frame */
int pc_glow_shadows; /* the game's glows cast shadows (off: their light still adds, unshadowed) */
float pc_shadow_history = 48.0f; /* frames of history (the lighting signature drops it where shadows move) */
float pc_light_size = 4.0f; /* emitting sphere radius of traced point and spot lights */
/* Volumetric haze: scattering per world unit (0: off), march steps, phase
 * anisotropy, emitters' share. */
float pc_fog_density = 0.0002f;
int pc_fog_steps = 4;
float pc_fog_anisotropy = 0.35f;
float pc_fog_glow = 0.3f;
/* The captured room's bounds (the haze fills them only). */
static float room_lo[3], room_hi[3];
void pc_scene_bounds(float lo[3], float hi[3]);
void pc_gi_room_bounds(float lo[3], float hi[3])
{
    memcpy(lo, room_lo, sizeof room_lo);
    memcpy(hi, room_hi, sizeof room_hi);
} /* frames of shadow history (fewer: sharper motion, more noise) */
float pc_gi_strength = 1.0f; /* indirect light scale */
float pc_gi_resolution = 3.0f; /* GI pixels per native display pixel (3: ~670 lines) */
int pc_gi_bounces = 1;
int pc_gi_rays = 1; /* paths per GI pixel per frame */
float pc_gi_baked_bounce = 0.0f; /* let the baked vertex lighting bounce too */
/* gi_denoiser: 0 raw samples, 1 our temporal + a-trous filter, 2 NVIDIA NRD
 * RELAX for the indirect light (Vulkan built with NRD; elsewhere the filter)
 * with our filter for the shadow visibility signals. */
int pc_gi_denoise = 2;
int pc_gi_debug; /* 1: show the indirect light alone, 2: the shadow visibility */
int pc_gi_trace_debug; /* trace shader debug output (see trace_debug) */

/* ---- scene and BVH ---- */

typedef struct {
    float a[4], b[4], c[4]; /* xyz; w = vertex colour (r | g << 8 | b << 16) as float bits */
    uint32_t info[4]; /* uv0 | uv1 << 16, uv2 | tpage << 16, clut | code << 16, flags (1 textured, 2 raw, 4 semi,
                       * 8 moving: a character or prop) */
} gpu_triangle;
typedef struct {
    float lo[4], hi[4]; /* bounds; lo.w = first (child or triangle) as int bits, hi.w = triangle count (0: interior) */
} gpu_node;

static gpu_triangle* triangles;
static gpu_node* nodes;
static int triangle_count, node_count, capacity;
static float* centroids;
static int* order;

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static float bits_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static void grow(int n)
{
    if (n <= capacity)
        return;
    capacity = n + n / 2 + 256;
    triangles = realloc(triangles, (size_t)capacity * sizeof *triangles);
    nodes = realloc(nodes, (size_t)capacity * 2 * sizeof *nodes);
    centroids = realloc(centroids, (size_t)capacity * 3 * sizeof *centroids);
    order = realloc(order, (size_t)capacity * sizeof *order);
}
static void add_triangle(const float p[3][3], const uint8_t rgb[3][3], const uint8_t uv[3][2], unsigned tpage,
    unsigned clut, unsigned code, unsigned flags)
{
    grow(triangle_count + 1);
    gpu_triangle* t = &triangles[triangle_count++];
    float* corner[3] = { t->a, t->b, t->c };
    for (int i = 0; i < 3; i++) {
        memcpy(corner[i], p[i], 3 * sizeof(float));
        corner[i][3] = bits_float(rgb[i][0] | (uint32_t)rgb[i][1] << 8 | (uint32_t)rgb[i][2] << 16);
    }
    t->info[0] = (uint32_t)(uv[0][0] | uv[0][1] << 8 | uv[1][0] << 16 | uv[1][1] << 24);
    t->info[1] = (uint32_t)(uv[2][0] | uv[2][1] << 8) | (tpage & 0xffff) << 16;
    t->info[2] = (clut & 0xffff) | (code & 0xff) << 16;
    t->info[3] = flags;
}

/* Binned SAH over triangle centroids; leaves of up to four triangles. */
static void bounds_of(int first, int count, float lo[3], float hi[3])
{
    for (int k = 0; k < 3; k++)
        lo[k] = 1e30f, hi[k] = -1e30f;
    for (int i = first; i < first + count; i++) {
        const gpu_triangle* t = &triangles[order[i]];
        const float* p[3] = { t->a, t->b, t->c };
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                lo[k] = fminf(lo[k], p[j][k]);
                hi[k] = fmaxf(hi[k], p[j][k]);
            }
    }
}
static float area(const float lo[3], const float hi[3])
{
    float d[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
    return d[0] * d[1] + d[1] * d[2] + d[2] * d[0];
}
static void build(int node, int first, int count)
{
    gpu_node* n = &nodes[node];
    float lo[3], hi[3];
    bounds_of(first, count, lo, hi);
    memcpy(n->lo, lo, sizeof lo);
    memcpy(n->hi, hi, sizeof hi);
    if (count <= 4) {
        n->lo[3] = bits_float((uint32_t)first);
        n->hi[3] = bits_float((uint32_t)count);
        return;
    }
    float clo[3] = { 1e30f, 1e30f, 1e30f }, chi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = first; i < first + count; i++)
        for (int k = 0; k < 3; k++) {
            clo[k] = fminf(clo[k], centroids[order[i] * 3 + k]);
            chi[k] = fmaxf(chi[k], centroids[order[i] * 3 + k]);
        }
    enum { BINS = 12 };
    int best_axis = -1, best_split = 0;
    float best_cost = area(lo, hi) * (float)count; /* cost of a leaf (never chosen beyond 4 below) */
    best_cost = 1e30f;
    for (int axis = 0; axis < 3; axis++) {
        float extent = chi[axis] - clo[axis];
        if (extent <= 1e-6f)
            continue;
        int bin_count[BINS] = { 0 };
        float bin_lo[BINS][3], bin_hi[BINS][3];
        for (int b = 0; b < BINS; b++)
            for (int k = 0; k < 3; k++)
                bin_lo[b][k] = 1e30f, bin_hi[b][k] = -1e30f;
        for (int i = first; i < first + count; i++) {
            int b = (int)((centroids[order[i] * 3 + axis] - clo[axis]) / extent * BINS);
            b = b < 0 ? 0 : b >= BINS ? BINS - 1 : b;
            bin_count[b]++;
            const gpu_triangle* t = &triangles[order[i]];
            const float* p[3] = { t->a, t->b, t->c };
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) {
                    bin_lo[b][k] = fminf(bin_lo[b][k], p[j][k]);
                    bin_hi[b][k] = fmaxf(bin_hi[b][k], p[j][k]);
                }
        }
        for (int split = 1; split < BINS; split++) {
            float llo[3] = { 1e30f, 1e30f, 1e30f }, lhi[3] = { -1e30f, -1e30f, -1e30f };
            float rlo[3] = { 1e30f, 1e30f, 1e30f }, rhi[3] = { -1e30f, -1e30f, -1e30f };
            int lc = 0, rc = 0;
            for (int b = 0; b < BINS; b++) {
                if (!bin_count[b])
                    continue;
                float* tl = b < split ? llo : rlo;
                float* th = b < split ? lhi : rhi;
                for (int k = 0; k < 3; k++) {
                    tl[k] = fminf(tl[k], bin_lo[b][k]);
                    th[k] = fmaxf(th[k], bin_hi[b][k]);
                }
                if (b < split)
                    lc += bin_count[b];
                else
                    rc += bin_count[b];
            }
            if (!lc || !rc)
                continue;
            float cost = area(llo, lhi) * (float)lc + area(rlo, rhi) * (float)rc;
            if (cost < best_cost)
                best_cost = cost, best_axis = axis, best_split = split;
        }
    }
    int mid;
    if (best_axis < 0) {
        mid = first + count / 2; /* all centroids coincide: split the list */
    } else {
        float extent = chi[best_axis] - clo[best_axis];
        int i = first, j = first + count - 1;
        while (i <= j) {
            int b = (int)((centroids[order[i] * 3 + best_axis] - clo[best_axis]) / extent * BINS);
            b = b < 0 ? 0 : b >= BINS ? BINS - 1 : b;
            if (b < best_split)
                i++;
            else {
                int swap = order[i];
                order[i] = order[j];
                order[j--] = swap;
            }
        }
        mid = i;
        if (mid == first || mid == first + count)
            mid = first + count / 2;
    }
    int left = node_count;
    node_count += 2;
    n = &nodes[node];
    n->lo[3] = bits_float((uint32_t)left);
    n->hi[3] = bits_float(0);
    build(left, first, mid - first);
    build(left + 1, mid, first + count - mid);
}

typedef struct {
    uint8_t rgb[9], uv[6], code;
    uint16_t tpage, clut;
} caster_surface; /* gpu_hw.c pc_hw_surface */

static GLuint triangle_buffer, node_buffer;
static int built_scene = -1, built_casters = -1;
static int uploaded; /* the GPU buffers hold the current build (pc_gi_scene_cpu builds without them) */

/* Gather the room and this frame's casters; rebuild the BVH when either
 * changed. Returns the triangle count. */
/* Two-level scene: the room's triangles and BVH are built once per room
 * (nodes from 0, room_root); this frame's casters get their own small BVH
 * appended after them (caster_root), rebuilt only when they change. The
 * trace starts from both roots. Rebuilding everything every frame cost
 * 10+ ms of CPU. */
static int room_triangles, room_nodes, room_root = -1, caster_root = -1;
static gpu_triangle* room_sorted;
/* Animated materials (room texture effects): each room triangle's record in
 * room_sorted (and the triangle buffer), and the scene's UV serial they
 * follow; the animated triangles' UVs are rewritten in place. */
static int* room_position;
static int room_uv_serial = -1;
int pc_rt_uv_updates; /* triangle records whose UVs were rewritten (RPC uv_effects) */
static void update_room_uvs(const scene_triangle* room, int room_count)
{
    int serial = pc_scene_uv_serial();
    if (serial == room_uv_serial || !room_position || room_count != room_triangles || !room_sorted)
        return;
    room_uv_serial = serial;
    int count;
    const int* animated = pc_scene_animated(&count);
    for (int n = 0; n < count; n++) {
        int i = animated[n];
        if (i < 0 || i >= room_triangles)
            continue;
        const scene_triangle* t = &room[i];
        uint32_t info[2] = { (uint32_t)(t->vertex[0].u | t->vertex[0].v << 8 | t->vertex[1].u << 16 | t->vertex[1].v << 24),
            (uint32_t)(t->vertex[2].u | t->vertex[2].v << 8) | (uint32_t)t->tpage << 16 };
        int p = room_position[i];
        if (!memcmp(room_sorted[p].info, info, sizeof info))
            continue;
        memcpy(room_sorted[p].info, info, sizeof info);
        memcpy(triangles[i].info, info, sizeof info);
        ++pc_rt_uv_updates;
        if (triangle_buffer) {
            gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, triangle_buffer);
            gl_BufferSubData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)((size_t)p * sizeof *room_sorted + offsetof(gpu_triangle, info)),
                (GLsizeiptr)sizeof info, info);
            gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }
}
static int update_scene(void)
{
    int room_count, caster_count, caster_serial;
    const scene_triangle* room = pc_scene_triangle_data(&room_count);
    const float* casters = pc_hw_casters(&caster_count, &caster_serial);
    const caster_surface* surfaces = pc_hw_caster_surfaces();
    int serial = pc_scene_serial();
    if (serial == built_scene)
        update_room_uvs(room, room_count);
    if (serial == built_scene && caster_serial == built_casters && (uploaded || !triangle_buffer))
        return triangle_count;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (serial != built_scene) {
        triangle_count = 0;
        for (int i = 0; i < room_count; i++) {
            const scene_triangle* t = &room[i];
            float p[3][3];
            uint8_t rgb[3][3], uv[3][2];
            for (int j = 0; j < 3; j++) {
                memcpy(p[j], t->vertex[j].position, sizeof p[j]);
                memcpy(rgb[j], t->vertex[j].rgb, 3);
                uv[j][0] = t->vertex[j].u, uv[j][1] = t->vertex[j].v;
            }
            add_triangle(p, rgb, uv, t->tpage, t->clut, t->code, 1u | (t->code & 1 ? 2u : 0u) | (t->code & 2 ? 4u : 0u));
        }
        room_triangles = triangle_count;
        for (int i = 0; i < room_triangles; i++) {
            order[i] = i;
            for (int k = 0; k < 3; k++)
                centroids[i * 3 + k] = (triangles[i].a[k] + triangles[i].b[k] + triangles[i].c[k]) / 3.0f;
        }
        node_count = 0;
        room_root = -1;
        if (room_triangles) {
            room_root = node_count++;
            build(room_root, 0, room_triangles);
        }
        room_nodes = node_count;
        room_sorted = realloc(room_sorted, (size_t)(room_triangles ? room_triangles : 1) * sizeof *room_sorted);
        room_position = realloc(room_position, (size_t)(room_triangles ? room_triangles : 1) * sizeof *room_position);
        for (int i = 0; i < room_triangles; i++) {
            room_sorted[i] = triangles[order[i]];
            if (room_position)
                room_position[order[i]] = i;
        }
        built_scene = serial;
        room_uv_serial = pc_scene_uv_serial(); /* (the capture's UVs are this serial's) */
    }
    built_casters = caster_serial;
    triangle_count = room_triangles;
    for (int i = 0; i < caster_count; i++) {
        const caster_surface* s = &surfaces[i];
        float p[3][3];
        uint8_t rgb[3][3], uv[3][2];
        for (int j = 0; j < 3; j++) {
            memcpy(p[j], casters + i * 9 + j * 3, sizeof p[j]);
            memcpy(rgb[j], s->rgb + j * 3, 3);
            uv[j][0] = s->uv[j * 2], uv[j][1] = s->uv[j * 2 + 1];
        }
        add_triangle(p, rgb, uv, s->tpage, s->clut, s->code, (s->code & 4 ? 1u : 0u) | (s->code & 1 ? 2u : 0u) | 8u);
    }
    for (int i = room_triangles; i < triangle_count; i++) {
        order[i] = i;
        for (int k = 0; k < 3; k++)
            centroids[i * 3 + k] = (triangles[i].a[k] + triangles[i].b[k] + triangles[i].c[k]) / 3.0f;
    }
    node_count = room_nodes;
    caster_root = -1;
    if (triangle_count > room_triangles) {
        caster_root = node_count++;
        build(caster_root, room_triangles, triangle_count - room_triangles);
    }
    if (!triangle_count)
        return 0;
    /* Triangles in leaf order, so leaves index a contiguous range. */
    uploaded = triangle_buffer != 0;
    if (uploaded) {
        gpu_triangle* sorted = malloc((size_t)triangle_count * sizeof *sorted);
        memcpy(sorted, room_sorted, (size_t)room_triangles * sizeof *sorted);
        for (int i = room_triangles; i < triangle_count; i++)
            sorted[i] = triangles[order[i]];
        gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, triangle_buffer);
        gl_BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)triangle_count * (GLsizeiptr)sizeof *sorted, sorted,
            GL_DYNAMIC_DRAW);
        gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, node_buffer);
        gl_BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)node_count * (GLsizeiptr)sizeof *nodes, nodes,
            GL_DYNAMIC_DRAW);
        gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        free(sorted);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    static double build_ms;
    static int builds;
    build_ms += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    if (++builds == 300 && getenv("ROOD_PERF")) {
        fprintf(stderr, "[perf] scene update avg %.2f ms (%d room + %d caster triangles)\n", build_ms / builds,
            room_triangles, triangle_count - room_triangles);
        build_ms = 0, builds = 0;
    }
    return triangle_count;
}

/* ---- emitters ----
 *
 * Glowing things are additive translucent triangles: the room's (lamps,
 * the save point) and this frame's effects (flames). glows.c groups them by
 * their source into glows with a stable identity and smooths them; their
 * colour is that of their texels (decoded from native VRAM) times their
 * vertex colour. Their light is already in the baked vertex colours; the
 * path tracer uses them to shadow that baked light where characters block
 * it, and can add their light for real. */
#define MAX_EMITTERS 32
float pc_emitter_light = 0.25f; /* real light added by emitters */
float pc_emitter_radius = 700.0f;
static float emitter_pos[MAX_EMITTERS][4], emitter_col[MAX_EMITTERS][4];
static float emitter_shape[MAX_EMITTERS][4]; /* a column glow's half axis and radius (0: a sphere; glows.c) */
static float emitter_shadow[MAX_EMITTERS][4]; /* a column's shadows: core radius, baked weight, clear radius (glows.c) */
static int emitter_count;
extern const uint16_t* pc_native_vram(void);
extern const float* pc_hw_emissives(int* count, const void** surfaces);
static uint16_t vram_texel(unsigned u, unsigned v, unsigned tpage, unsigned clut)
{
    const uint16_t* vram = pc_native_vram();
    unsigned tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    unsigned cx = (clut & 63) * 16, cy = (clut >> 6) & 511;
    u &= 255, v &= 255;
#define AT(x, y) vram[((y) & 511) * 1024 + ((x) & 1023)]
    if (depth == 0)
        return AT(cx + ((AT(tx + (u >> 2), ty + v) >> ((u & 3) * 4)) & 15), cy);
    if (depth == 1)
        return AT(cx + ((AT(tx + (u >> 1), ty + v) >> ((u & 1) * 8)) & 255), cy);
    return AT(tx + u, ty + v);
#undef AT
}
typedef pc_glow_fragment glow;
static void add_glow(glow* g, int* n, int max, const float p[3][3], const uint8_t rgb[3][3], const uint8_t uv[3][2],
    unsigned tpage, unsigned clut, unsigned code, uint32_t source)
{
    if (*n >= max)
        return;
    float e1[3], e2[3], c[3];
    for (int k = 0; k < 3; k++)
        e1[k] = p[1][k] - p[0][k], e2[k] = p[2][k] - p[0][k];
    c[0] = e1[1] * e2[2] - e1[2] * e2[1], c[1] = e1[2] * e2[0] - e1[0] * e2[2], c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    float area = 0.5f * sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    float sum[3] = { 0, 0, 0 };
    int samples = 0;
    /* The corners and the centre; room glows (few, and their textures
     * scroll) on a finer grid, so their colour is the texture's mean rather
     * than whatever four texels pass under the corners this frame. */
    int room = (int)(source >> 28) == PC_GLOW_ORIGIN_ROOM, points = room ? 15 : 4;
    for (int j = 0; j < points; j++) {
        float w[3] = { j == 0, j == 1, j == 2 };
        if (room) {
            static const unsigned char grid[15][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 1, 0 }, { 1, 1 },
                { 1, 2 }, { 1, 3 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 3, 0 }, { 3, 1 }, { 4, 0 } };
            w[0] = grid[j][0] / 4.0f, w[1] = grid[j][1] / 4.0f, w[2] = 1.0f - w[0] - w[1];
        } else if (j == 3)
            w[0] = w[1] = w[2] = 1.0f / 3.0f;
        float u = w[0] * uv[0][0] + w[1] * uv[1][0] + w[2] * uv[2][0];
        float v = w[0] * uv[0][1] + w[1] * uv[1][1] + w[2] * uv[2][1];
        float texel[3] = { 1, 1, 1 };
        if (code & 4) {
            uint16_t t = vram_texel((unsigned)u, (unsigned)v, tpage, clut);
            if (!t)
                continue;
            texel[0] = (t & 31) / 31.0f, texel[1] = ((t >> 5) & 31) / 31.0f, texel[2] = ((t >> 10) & 31) / 31.0f;
        }
        for (int k = 0; k < 3; k++) {
            float vc = (w[0] * rgb[0][k] + w[1] * rgb[1][k] + w[2] * rgb[2][k]) / 255.0f;
            sum[k] += (code & 4) && !(code & 1) ? fminf(texel[k] * vc * 2.0f, 1.0f) : (code & 4) ? texel[k] : vc;
        }
        samples++;
    }
    if (!samples)
        return;
    glow* out = &g[(*n)++];
    for (int k = 0; k < 3; k++) {
        out->p[k] = (p[0][k] + p[1][k] + p[2][k]) / 3.0f;
        out->rgb[k] = sum[k] / samples;
    }
    out->area = area;
    out->weight = area * (0.299f * out->rgb[0] + 0.587f * out->rgb[1] + 0.114f * out->rgb[2]);
    out->reach = 0;
    out->source = source;
}
/* Baked light sources, estimated once per room from the vertex colours:
 * hotspots well above the room's ambient level, clustered, with a light
 * just off the surface on the side facing into the room. They only shadow
 * the baked light (their light is already in it): emitter size < 0. */
#define MAX_BAKED 12
static float baked_pos[MAX_BAKED][4], baked_col[MAX_BAKED][4], baked_spot[MAX_BAKED][3]; /* spot: the hotspot itself */
static int baked_count, baked_serial = -1;
static int by_float_desc(const void* a, const void* b)
{
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? 1 : x > y ? -1 : 0;
}
static void estimate_baked_lights(void)
{
    int serial = pc_scene_serial(), count;
    if (serial == baked_serial)
        return;
    baked_serial = serial;
    baked_count = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    if (!count)
        return;
    float* lum = malloc((size_t)count * 3 * sizeof *lum);
    float lo[3], hi[3], centre[3];
    for (int k = 0; k < 3; k++)
        lo[k] = 1e30f, hi[k] = -1e30f;
    int n = 0;
    for (int i = 0; i < count; i++)
        for (int j = 0; j < 3; j++) {
            const scene_vertex* v = &tris[i].vertex[j];
            lum[n++] = (0.299f * v->rgb[0] + 0.587f * v->rgb[1] + 0.114f * v->rgb[2]) / 128.0f;
            for (int k = 0; k < 3; k++)
                lo[k] = fminf(lo[k], v->position[k]), hi[k] = fmaxf(hi[k], v->position[k]);
        }
    for (int k = 0; k < 3; k++)
        centre[k] = 0.5f * (lo[k] + hi[k]);
    /* Percentiles over lit vertices (black ones are voids and cut-away backs). */
    float* sorted = malloc((size_t)n * sizeof *sorted);
    int lit = 0;
    for (int i = 0; i < n; i++)
        if (lum[i] > 0.04f)
            sorted[lit++] = lum[i];
    if (lit < 16) {
        free(sorted);
        free(lum);
        return;
    }
    qsort(sorted, (size_t)lit, sizeof *sorted, by_float_desc);
    float peak = sorted[lit / 50], ambient = sorted[lit * 4 / 5]; /* 98th and 20th percentiles */
    free(sorted);
    float threshold = ambient + fmaxf(0.12f, 0.45f * (peak - ambient));
    float sum_w[MAX_BAKED] = { 0 }, extent[MAX_BAKED] = { 0 }, normal[MAX_BAKED][3];
    memset(normal, 0, sizeof normal);
    n = 0;
    for (int i = 0; i < count; i++) {
        const scene_triangle* t = &tris[i];
        float e1[3], e2[3], fn[3], to_centre[3];
        for (int k = 0; k < 3; k++)
            e1[k] = t->vertex[1].position[k] - t->vertex[0].position[k],
            e2[k] = t->vertex[2].position[k] - t->vertex[0].position[k];
        fn[0] = e1[1] * e2[2] - e1[2] * e2[1], fn[1] = e1[2] * e2[0] - e1[0] * e2[2], fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
        float l = sqrtf(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
        for (int k = 0; k < 3; k++)
            fn[k] = l > 1e-6f ? fn[k] / l : 0, to_centre[k] = centre[k] - t->vertex[0].position[k];
        if (fn[0] * to_centre[0] + fn[1] * to_centre[1] + fn[2] * to_centre[2] < 0)
            for (int k = 0; k < 3; k++)
                fn[k] = -fn[k]; /* face into the room */
        for (int j = 0; j < 3; j++, n++) {
            float excess = lum[n] - threshold;
            if (excess <= 0 || (t->code & 2))
                continue;
            const scene_vertex* v = &t->vertex[j];
            int b = 0;
            for (; b < baked_count; b++) {
                float d[3] = { baked_pos[b][0] - v->position[0], baked_pos[b][1] - v->position[1],
                    baked_pos[b][2] - v->position[2] };
                if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 384.0f * 384.0f)
                    break; /* one lit area is one source, not several (doubled shadows) */
            }
            if (b == baked_count) {
                if (baked_count == MAX_BAKED)
                    continue;
                memset(baked_pos[b], 0, sizeof baked_pos[b]);
                memset(baked_col[b], 0, sizeof baked_col[b]);
                baked_count++;
            }
            float total = sum_w[b] + excess;
            for (int k = 0; k < 3; k++) {
                baked_pos[b][k] = (baked_pos[b][k] * sum_w[b] + v->position[k] * excess) / total;
                baked_col[b][k] = (baked_col[b][k] * sum_w[b] + v->rgb[k] / 128.0f * excess) / total;
                normal[b][k] += fn[k] * excess;
            }
            sum_w[b] = total;
        }
    }
    /* Reach: how far the hotspot spreads; the light sits off the surface. */
    n = 0;
    for (int i = 0; i < count; i++)
        for (int j = 0; j < 3; j++, n++) {
            if (lum[n] <= threshold)
                continue;
            const float* p = tris[i].vertex[j].position;
            for (int b = 0; b < baked_count; b++) {
                float d[3] = { baked_pos[b][0] - p[0], baked_pos[b][1] - p[1], baked_pos[b][2] - p[2] };
                float r = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (r < 384)
                    extent[b] = fmaxf(extent[b], r);
            }
        }
    free(lum);
    int kept = 0;
    for (int b = 0; b < baked_count; b++) {
        if (sum_w[b] < 1.0f)
            continue; /* a stray bright vertex or two */
        float l = sqrtf(normal[b][0] * normal[b][0] + normal[b][1] * normal[b][1] + normal[b][2] * normal[b][2]);
        float peak_c = fmaxf(baked_col[b][0], fmaxf(baked_col[b][1], baked_col[b][2]));
        /* A pool of light that spreads wide comes from high above it (a grate,
         * a lamp overhead); one just off the surface threw long shadows that
         * swung with every step. */
        float height = fminf(fmaxf(extent[b] * 1.5f, 192.0f), 768.0f);
        for (int k = 0; k < 3; k++) {
            baked_spot[kept][k] = baked_pos[b][k];
            baked_pos[kept][k] = baked_pos[b][k] + (l > 1e-6f ? normal[b][k] / l : 0) * height;
            baked_col[kept][k] = peak_c > 0 ? baked_col[b][k] / peak_c : 1;
        }
        /* Reach covers the pool from up there (only characters and props
         * block it, so it may sit above the ceiling). */
        float reach = fmaxf(extent[b] * 2.5f, sqrtf(height * height + extent[b] * extent[b]) * 1.5f);
        baked_pos[kept][3] = reach < 300 ? 300 : reach > 1600 ? 1600 : reach;
        baked_col[kept][3] = -16.0f; /* shadow only; emitting sphere radius 16 (soft edges) */
        kept++;
    }
    baked_count = kept;
    fprintf(stderr, "[gi] %d baked light sources estimated (ambient %.2f, peak %.2f)\n", baked_count, ambient, peak);
    for (int b = 0; b < baked_count && getenv("ROOD_GI_DEBUG"); b++)
        fprintf(stderr, "[gi]   baked light at %.0f %.0f %.0f colour %.2f %.2f %.2f reach %.0f\n", baked_pos[b][0],
            baked_pos[b][1], baked_pos[b][2], baked_col[b][0], baked_col[b][1], baked_col[b][2], baked_pos[b][3]);
}

/* ---- light fixtures (the editor's "Light this fixture") ----
 *
 * Lanterns, torches and candles are room geometry with a flame texture;
 * the flame is where their texels are bright and warm (or where an additive
 * glow polygon is). Samples at a triangle's corners, edge middles and
 * centre, decoded from native VRAM and modulated as drawn. */
static float flame_weight(const float c[3])
{
    /* Flame colours: near-white yellow to orange, bright and saturated
     * (lit sandstone is warm too, but duller). */
    float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    if (lum < 0.6f || c[0] < 0.8f || c[0] - c[2] < 0.25f || c[1] < 0.4f)
        return 0;
    return (lum - 0.62f) * (lum - 0.62f) * 10.0f;
}
/* Flame samples of one room triangle on a barycentric grid of `steps`
 * (1: corners, edge middles and centre): positions and weights; returns
 * the count. `relative`: every lit texel with a flame score (brightness
 * and warmth, no threshold) for picking the brightest part of a clicked
 * fixture; else only texels that pass flame_weight. */
#define MAX_TRI_SAMPLES 45
static int flame_samples(const scene_triangle* t, int steps, int relative, float pos[MAX_TRI_SAMPLES][3],
    float weight[MAX_TRI_SAMPLES])
{
    float bary[MAX_TRI_SAMPLES][3];
    int nb = 0;
    if (steps <= 1) {
        static const float fixed[7][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.5f, 0.5f, 0 }, { 0, 0.5f, 0.5f },
            { 0.5f, 0, 0.5f }, { 1 / 3.0f, 1 / 3.0f, 1 / 3.0f } };
        memcpy(bary, fixed, sizeof fixed);
        nb = 7;
    } else {
        for (int a = 0; a <= steps; a++)
            for (int b = 0; a + b <= steps && nb < MAX_TRI_SAMPLES; b++) {
                bary[nb][0] = (float)a / steps, bary[nb][1] = (float)b / steps;
                bary[nb][2] = 1.0f - bary[nb][0] - bary[nb][1];
                ++nb;
            }
    }
    unsigned mode = (t->tpage >> 5) & 3;
    if ((t->code & 2) && (mode == 1 || mode == 3)) /* additive polygons are glows (emitters) already */
        return 0;
    /* The texels as drawn (modulated by the vertex colour), and their mean
     * brightness: a flame is much brighter than the rest of its polygon (a
     * flame painted on a candle or lantern), or a small polygon of flame. */
    float colour[MAX_TRI_SAMPLES][3], lums[MAX_TRI_SAMPLES], mean = 0;
    int valid[MAX_TRI_SAMPLES], opaque = 0;
    for (int j = 0; j < nb; j++) {
        const float* w = bary[j];
        float u = w[0] * t->vertex[0].u + w[1] * t->vertex[1].u + w[2] * t->vertex[2].u;
        float v = w[0] * t->vertex[0].v + w[1] * t->vertex[1].v + w[2] * t->vertex[2].v;
        uint16_t texel = vram_texel((unsigned)(u + 0.5f), (unsigned)(v + 0.5f), t->tpage, t->clut);
        valid[j] = texel != 0;
        if (!texel)
            continue;
        float* c = colour[j];
        c[0] = (texel & 31) / 31.0f, c[1] = ((texel >> 5) & 31) / 31.0f, c[2] = ((texel >> 10) & 31) / 31.0f;
        for (int k = 0; k < 3; k++) {
            float vc = (w[0] * t->vertex[0].rgb[k] + w[1] * t->vertex[1].rgb[k] + w[2] * t->vertex[2].rgb[k]) / 255.0f;
            if (!(t->code & 1))
                c[k] = fminf(c[k] * vc * 2.0f, 1.0f);
        }
        lums[j] = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
        mean += lums[j];
        ++opaque;
    }
    if (!opaque)
        return 0;
    mean /= opaque;
    float e1[3], e2[3], x[3];
    for (int k = 0; k < 3; k++)
        e1[k] = t->vertex[1].position[k] - t->vertex[0].position[k], e2[k] = t->vertex[2].position[k] - t->vertex[0].position[k];
    x[0] = e1[1] * e2[2] - e1[2] * e2[1], x[1] = e1[2] * e2[0] - e1[0] * e2[2], x[2] = e1[0] * e2[1] - e1[1] * e2[0];
    float area = 0.5f * sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    int n = 0;
    for (int j = 0; j < nb; j++) {
        if (!valid[j])
            continue;
        const float* c = colour[j];
        float lum = lums[j], f;
        if (relative) {
            f = lum * lum * (0.3f + fmaxf(c[0] - c[2], 0.0f));
        } else {
            /* Room-wide: only small polygons of flame colour (a flame of its
             * own). Flames painted into larger textures are found by
             * clicking the fixture (relative); a room-wide contrast test
             * caught too much patterned sandstone. */
            f = area < 600.0f ? flame_weight(c) : 0;
        }
        if (f <= 0)
            continue;
        const float* w = bary[j];
        for (int k = 0; k < 3; k++)
            pos[n][k] = w[0] * t->vertex[0].position[k] + w[1] * t->vertex[1].position[k] + w[2] * t->vertex[2].position[k];
        weight[n++] = f;
    }
    return n;
}
/* The flame of the fixture around p (a click on it): the brightest, warmest
 * texels within `radius` (those scoring at least 3/4 of the best), their
 * centre; out[3] the best score. 0 when nothing is lit there. */
int pc_gi_flame_near(const float p[3], float radius, float out[4])
{
    int count;
    const scene_triangle* room = pc_scene_triangle_data(&count);
    enum { MAX_NEAR = 8192 };
    static float near[MAX_NEAR][4];
    int n = 0;
    for (int i = 0; i < count && n < MAX_NEAR - MAX_TRI_SAMPLES; i++) {
        int close = 0;
        for (int j = 0; j < 3; j++) {
            const float* q = room[i].vertex[j].position;
            float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
            close |= d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= 4 * radius * radius;
        }
        if (!close)
            continue;
        float pos[MAX_TRI_SAMPLES][3], weight[MAX_TRI_SAMPLES];
        int m = flame_samples(&room[i], 8, 1, pos, weight);
        for (int j = 0; j < m; j++) {
            float d[3] = { pos[j][0] - p[0], pos[j][1] - p[1], pos[j][2] - p[2] };
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > radius * radius)
                continue;
            memcpy(near[n], pos[j], 3 * sizeof(float));
            near[n++][3] = weight[j];
        }
    }
    float best = 0;
    for (int i = 0; i < n; i++)
        best = fmaxf(best, near[i][3]);
    if (best < 0.08f) /* nothing bright there */
        return 0;
    double sum[3] = { 0, 0, 0 }, total = 0;
    for (int i = 0; i < n; i++) {
        if (near[i][3] < best * 0.75f)
            continue;
        for (int k = 0; k < 3; k++)
            sum[k] += near[i][k] * near[i][3];
        total += near[i][3];
    }
    for (int k = 0; k < 3; k++)
        out[k] = (float)(sum[k] / total);
    out[3] = best;
    return 1;
}
/* Every flame in the room: clusters of flame samples (within 40 units of
 * the cluster's strongest sample), strongest first: xyz and weight. */
int pc_gi_flames(float out[][4], int max)
{
    int count;
    const scene_triangle* room = pc_scene_triangle_data(&count);
    enum { MAX_SAMPLES = 4096 };
    static float sp[MAX_SAMPLES][4];
    int n = 0;
    for (int i = 0; i < count && n < MAX_SAMPLES - MAX_TRI_SAMPLES; i++) {
        float pos[MAX_TRI_SAMPLES][3], weight[MAX_TRI_SAMPLES];
        int m = flame_samples(&room[i], 4, 0, pos, weight);
        for (int j = 0; j < m; j++, n++)
            sp[n][0] = pos[j][0], sp[n][1] = pos[j][1], sp[n][2] = pos[j][2], sp[n][3] = weight[j];
    }
    int clusters = 0;
    static int used[MAX_SAMPLES];
    memset(used, 0, sizeof used);
    while (clusters < max) {
        int best = -1;
        for (int i = 0; i < n; i++)
            if (!used[i] && (best < 0 || sp[i][3] > sp[best][3]))
                best = i;
        if (best < 0)
            break;
        double sum[3] = { 0, 0, 0 }, total = 0;
        for (int i = 0; i < n; i++) {
            float d[3] = { sp[i][0] - sp[best][0], sp[i][1] - sp[best][1], sp[i][2] - sp[best][2] };
            if (used[i] || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 40.0f * 40.0f)
                continue;
            used[i] = 1;
            for (int k = 0; k < 3; k++)
                sum[k] += sp[i][k] * sp[i][3];
            total += sp[i][3];
        }
        if (total < 0.12) /* a stray bright texel */
            continue;
        for (int k = 0; k < 3; k++)
            out[clusters][k] = (float)(sum[k] / total);
        out[clusters++][3] = (float)total;
    }
    return clusters;
}
/* The fixture's polygons around a flame: room triangles with a corner
 * within `radius` (editor highlight). Returns the count (up to max). */
int pc_gi_fixture_triangles(const float p[3], float radius, float (*out)[9], int max)
{
    int count, n = 0;
    const scene_triangle* room = pc_scene_triangle_data(&count);
    for (int i = 0; i < count && n < max; i++) {
        int near = 0, far = 0; /* a corner close by, none beyond 3 radii (not the wall around it) */
        for (int j = 0; j < 3; j++) {
            const float* q = room[i].vertex[j].position;
            float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
            float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            near |= dd <= radius * radius;
            far |= dd > 9 * radius * radius;
        }
        if (!near || far)
            continue;
        for (int j = 0; j < 3; j++)
            memcpy(out[n] + 3 * j, room[i].vertex[j].position, 3 * sizeof(float));
        ++n;
    }
    return n;
}

extern const uint32_t* pc_hw_emissive_sources(void);
extern unsigned pc_presents; /* gl_present.c: images presented */
extern unsigned pc_hw_history_cut; /* gpu_hw.c: unpresented stretches ended (the GI history restarts) */
static unsigned emitters_present = ~0u; /* the present the emitters were last gathered in */
static void update_emitters(void)
{
    emitters_present = pc_presents;
    enum { MAX_GLOWS = 8192 };
    static glow glows[MAX_GLOWS];
    int n = 0, room_count, effect_count;
    const scene_triangle* room = pc_scene_triangle_data(&room_count);
    for (int i = 0; i < room_count; i++) {
        const scene_triangle* t = &room[i];
        unsigned mode = (t->tpage >> 5) & 3;
        if (!(t->code & 2) || (mode != 1 && mode != 3))
            continue;
        float p[3][3];
        uint8_t rgb[3][3], uv[3][2];
        for (int j = 0; j < 3; j++) {
            memcpy(p[j], t->vertex[j].position, sizeof p[j]);
            memcpy(rgb[j], t->vertex[j].rgb, 3);
            uv[j][0] = t->vertex[j].u, uv[j][1] = t->vertex[j].v;
        }
        add_glow(glows, &n, MAX_GLOWS, p, rgb, uv, t->tpage, t->clut, t->code | 4,
            (uint32_t)PC_GLOW_ORIGIN_ROOM << 28 | (uint32_t)i);
    }
    /* Emissive texels marked as lights (emissive.h): their room triangles
     * glow with the texels' emission (zero where none shows this frame),
     * grouped as the additive ones are (glows.c build_groups). */
    if (pc_emissive_light_serial())
        for (int i = 0; i < room_count && n < MAX_GLOWS; i++) {
            float e[3];
            if (!pc_emissive_room_light(i, e))
                continue;
            const scene_triangle* t = &room[i];
            float e1[3], e2[3], c[3];
            glow* out = &glows[n++];
            for (int k = 0; k < 3; k++) {
                e1[k] = t->vertex[1].position[k] - t->vertex[0].position[k];
                e2[k] = t->vertex[2].position[k] - t->vertex[0].position[k];
                out->p[k] = (t->vertex[0].position[k] + t->vertex[1].position[k] + t->vertex[2].position[k]) / 3.0f;
                out->rgb[k] = e[k];
            }
            c[0] = e1[1] * e2[2] - e1[2] * e2[1], c[1] = e1[2] * e2[0] - e1[0] * e2[2], c[2] = e1[0] * e2[1] - e1[1] * e2[0];
            out->area = 0.5f * sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
            out->weight = out->area * (0.299f * e[0] + 0.587f * e[1] + 0.114f * e[2]);
            out->source = (uint32_t)PC_GLOW_ORIGIN_ROOM << 28 | (uint32_t)i;
            out->reach = 0;
            pc_emissive_room_light_shape(i, &out->reach, NULL);
        }
    /* ... and on characters, props and other room blocks (object glows). */
    if (pc_emissive_light_serial() || pc_emissive_active())
        n += pc_emissive_object_lights(glows + n, MAX_GLOWS - n);
    const caster_surface* surfaces;
    const float* effects = pc_hw_emissives(&effect_count, (const void**)&surfaces);
    const uint32_t* sources = pc_hw_emissive_sources();
    for (int i = 0; i < effect_count; i++) {
        float p[3][3];
        uint8_t rgb[3][3], uv[3][2];
        for (int j = 0; j < 3; j++) {
            memcpy(p[j], effects + i * 9 + j * 3, sizeof p[j]);
            memcpy(rgb[j], surfaces[i].rgb + j * 3, 3);
            uv[j][0] = surfaces[i].uv[j * 2], uv[j][1] = surfaces[i].uv[j * 2 + 1];
        }
        add_glow(glows, &n, MAX_GLOWS, p, rgb, uv, surfaces[i].tpage, surfaces[i].clut, surfaces[i].code, sources[i]);
    }
    if (pc_baked_estimate)
        estimate_baked_lights();
    else
        baked_count = 0, baked_serial = -1;
    /* A hotspot with a glow (candle, fire) near it is that glow's light: the
     * glow shadows it, and a second, guessed source beside it only threw a
     * shadow the wrong way. */
    float bp[MAX_BAKED][4], bc[MAX_BAKED][4];
    int unexplained = 0;
    for (int b = 0; b < baked_count; b++) {
        int explained = 0;
        for (int i = 0; i < n && !explained; i++)
            explained = glows[i].weight > 1e-3f
                     && (glows[i].p[0] - baked_spot[b][0]) * (glows[i].p[0] - baked_spot[b][0])
                                + (glows[i].p[1] - baked_spot[b][1]) * (glows[i].p[1] - baked_spot[b][1])
                                + (glows[i].p[2] - baked_spot[b][2]) * (glows[i].p[2] - baked_spot[b][2])
                            < 512.0f * 512.0f;
        if (explained)
            continue;
        memcpy(bp[unexplained], baked_pos[b], sizeof bp[0]);
        memcpy(bc[unexplained++], baked_col[b], sizeof bc[0]);
    }
    /* Sources into glows (stable ids, smoothed), with the editor's
     * overrides, mutes, solo and linked light actors applied. */
    emitter_count = pc_glows_update(glows, n, (const float (*)[4])bp, (const float (*)[4])bc, unexplained, emitter_pos,
        emitter_col, emitter_shape, emitter_shadow, MAX_EMITTERS);
}

/* The glows for the fog volumes' lights (atmosphere.h): this present's,
 * gathered here when nothing else (the path tracer, the probes) did. */
int pc_fog_glows(float (*pos)[4], float (*col)[4], float (*shape)[4], int max)
{
    if (emitters_present != pc_presents)
        update_emitters();
    int n = emitter_count < max ? emitter_count : max;
    memcpy(pos, emitter_pos, (size_t)n * sizeof emitter_pos[0]);
    memcpy(col, emitter_col, (size_t)n * sizeof emitter_col[0]);
    memcpy(shape, emitter_shape, (size_t)n * sizeof emitter_shape[0]);
    return n;
}

/* ---- shaders ---- */

#define SHADER_COMMON                                                                              \
    "#version 430 core\n"                                                                          \
    "layout(local_size_x = 8, local_size_y = 8) in;\n"                                             \
    "struct Tri { vec4 a, b, c; uvec4 info; };\n"                                                  \
    "struct Node { vec4 lo, hi; };\n"                                                              \
    "layout(std430, binding = 0) readonly buffer Tris { Tri tris[]; };\n"                          \
    "layout(std430, binding = 1) readonly buffer Nodes { Node nodes[]; };\n"                      \
    "uniform int room_root, caster_root;\n" /* BVH roots: the room's, this frame's casters' (-1: none) */

static const char* trace_source = SHADER_COMMON
    "layout(rgba16f, binding = 0) uniform writeonly image2D gi_out;\n"
    "layout(rgba16f, binding = 3) uniform writeonly image2D extra_out;\n" /* r: baked light visibility */
    "layout(r32ui, binding = 4) uniform writeonly uimage2D signature_out;\n" /* lighting signature */
    "layout(rgba32f, binding = 1) uniform readonly image2D position_in;\n" /* world xyz, w 1 = surface */
    "layout(rgba16f, binding = 2) uniform readonly image2D normal_in;\n"
    "uniform usampler2D vram;\n"
    "uniform vec3 eye;\n"
    "uniform int light_count;\n"
    "uniform vec4 light_pos[16];\n"   /* world xyz, radius */
    "uniform vec4 light_col[16];\n"   /* rgb * intensity, type */
    "uniform vec4 light_dir[16];\n"   /* direction, cos(half cone) */
    "uniform vec4 light_right[16];\n"
    "uniform vec4 light_up[16];\n"
    "uniform int light_shadow[16];\n" /* casts shadows */
    "uniform int indirect;\n"        /* trace bounce light */
    "uniform int shadows;\n"         /* trace shadow visibility */
    "uniform float light_size;\n"    /* emitting sphere radius of point and spot lights (world units) */
    "uniform int bounces;\n"
    "uniform int rays;\n"
    "uniform uint frame;\n"
    "uniform float baked_bounce;\n"
    "uniform int emitter_count;\n"
    "uniform vec4 emitter_pos[32];\n" /* xyz, radius of influence */
    "uniform vec4 emitter_col[32];\n" /* rgb, emitting sphere radius */
    "uniform vec4 emitter_shape[32];\n" /* a column glow: half axis, radius (> 0 its side, < 0 solid); 0: the sphere */
    "uniform vec4 emitter_shadow[32];\n" /* a column's shadows: x core radius, y baked weight, z clear radius */
    "uniform float emitter_light;\n"  /* real light added by emitters */
    "uniform int baked_shadows;\n"    /* characters shadow the baked light */
    "uniform float contact_radius;\n"
    "uniform float baked_ambient;\n"  /* share of baked light not from emitters (never shadowed by them) */
    "uniform int shadow_samples;\n"
    "uniform int glow_shadows;\n" /* glows cast shadows (characters and props block them) */
    "uniform int glow_casts;\n" /* emitters that may cast shadows (bit i; glows.c: never transient effects) */
    "uniform float fog_density;\n"    /* 1: the haze is traced (its medium: medium_) */
    "uniform int fog_steps;\n"
    "uniform float fog_anisotropy;\n" /* Henyey-Greenstein g: > 0 scatters forward */
    "uniform float fog_glow;\n"       /* emitters' share in the haze (glows and baked lights) */
    "uniform vec3 room_lo, room_hi;\n" /* the haze fills the room's bounds only */
    "uniform int trace_debug;\n" /* 1 start normal, 2 first-hit distance (red: inside 8 units), 3 start position */
    /* The room's sun and sky (environment.h pc_env_gpu; environment.glsl). */
    "uniform vec4 env_[5];\n"
    "#define env_sun_dir env_[0]\n"
    "#define env_sun_col env_[1]\n"
    "#define env_zenith env_[2]\n"
    "#define env_horizon env_[3]\n"
    "#define env_ground env_[4]\n" PC_GLSL_ENVIRONMENT
    /* The room's medium (atmosphere.h pc_medium_gpu; medium.glsl). */
    "uniform vec4 medium_[166];\n"
    "#define MEDIUM(i) medium_[i]\n"
    "uniform sampler3D medium_tex;\n" /* atmosphere.h pc_medium_texture (gl_present.c pc_gl_medium_texture) */
    "#define MEDIUM_TEX medium_tex\n" PC_GLSL_MEDIUM
    "uint rng = 0u;\n"
    "bool moving_only = false;\n"
    "vec3 cosine_direction(vec3 n);\n"
    "vec3 emitted(vec3 p, vec3 n);\n" /* trace characters and props only */
    "float rand() { rng = rng * 747796405u + 2891336453u; uint w = ((rng >> ((rng >> 28u) + 4u)) ^ rng) * 277803737u;\n"
    "    return float((w >> 22u) ^ w) / 4294967296.0; }\n"
    "uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "uint texel_at(uint u, uint v, uint tpage, uint clut) {\n"
    "    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;\n"
    "    int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);\n"
    "    uint depth = (tpage >> 7) & 3u;\n"
    "    if (depth == 0u) return fetch(cx + int((fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u), cy);\n"
    "    if (depth == 1u) return fetch(cx + int((fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u), cy);\n"
    "    return fetch(tx + int(u), ty + int(v));\n"
    "}\n"
    /* The surface texel at barycentric (u, v); 0 = transparent. */
    "uint surface_texel(Tri t, vec2 bc) {\n"
    "    vec2 uv0 = vec2(t.info.x & 255u, (t.info.x >> 8) & 255u);\n"
    "    vec2 uv1 = vec2((t.info.x >> 16) & 255u, t.info.x >> 24);\n"
    "    vec2 uv2 = vec2(t.info.y & 255u, (t.info.y >> 8) & 255u);\n"
    "    vec2 uv = uv0 * (1.0 - bc.x - bc.y) + uv1 * bc.x + uv2 * bc.y;\n"
    "    return texel_at(uint(uv.x) & 255u, uint(uv.y) & 255u, t.info.y >> 16, t.info.z & 0xffffu);\n"
    "}\n"
    "vec3 vertex_rgb(float w) { uint c = floatBitsToUint(w); return vec3(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u) / 255.0; }\n"
    /* Nearest hit (or any hit, for shadows) along o + d t, t < t_max;
     * transparent texels let rays through. */
    /* Layers within skip_t that lie nearly parallel to skip_n are ignored: the
     * game floats decals (stains, dirt) a few units off walls and floors. */
    "bool trace(vec3 o, vec3 d, float t_max, bool any_hit, vec3 skip_n, float skip_t, out float t_hit, out int hit, out vec2 bc_hit) {\n"
    "    vec3 inv = 1.0 / d;\n"
    "    int stack[32]; int sp = 0;\n"
    "    if (room_root >= 0) stack[sp++] = room_root;\n"
    "    if (caster_root >= 0) stack[sp++] = caster_root;\n"
    "    t_hit = t_max; hit = -1;\n"
    "    while (sp > 0) {\n"
    "        Node n = nodes[stack[--sp]];\n"
    "        vec3 t0 = (n.lo.xyz - o) * inv, t1 = (n.hi.xyz - o) * inv;\n"
    "        vec3 tn = min(t0, t1), tf = max(t0, t1);\n"
    "        float enter = max(max(tn.x, tn.y), max(tn.z, 0.0)), leave = min(min(tf.x, tf.y), min(tf.z, t_hit));\n"
    "        if (enter > leave) continue;\n"
    "        int first = floatBitsToInt(n.lo.w), count = floatBitsToInt(n.hi.w);\n"
    "        if (count == 0) {\n" /* push the farther child first: the nearer is visited first */
    "            if (sp < 31) {\n"
    "                Node l = nodes[first], r = nodes[first + 1];\n"
    "                float dl = dot(0.5 * (l.lo.xyz + l.hi.xyz) - o, d), dr = dot(0.5 * (r.lo.xyz + r.hi.xyz) - o, d);\n"
    "                if (dl < dr) { stack[sp++] = first + 1; stack[sp++] = first; }\n"
    "                else { stack[sp++] = first; stack[sp++] = first + 1; }\n"
    "            }\n"
    "            continue;\n"
    "        }\n"
    "        for (int i = first; i < first + count; i++) {\n"
    "            Tri tri = tris[i];\n"
    "            if (moving_only && (tri.info.w & 8u) == 0u) continue;\n"
    "            vec3 e1 = tri.b.xyz - tri.a.xyz, e2 = tri.c.xyz - tri.a.xyz, p = cross(d, e2);\n"
    "            float det = dot(e1, p);\n"
    "            if (abs(det) < 1e-9) continue;\n"
    "            float f = 1.0 / det; vec3 s = o - tri.a.xyz;\n"
    "            float u = f * dot(s, p); if (u < 0.0 || u > 1.0) continue;\n"
    "            vec3 q = cross(s, e1); float v = f * dot(d, q); if (v < 0.0 || u + v > 1.0) continue;\n"
    "            float t = f * dot(e2, q); if (t <= 0.0 || t >= t_hit) continue;\n"
    "            if (t < skip_t && abs(dot(normalize(cross(e1, e2)), skip_n)) > 0.98) continue;\n"
    "            if ((tri.info.w & 1u) != 0u) {\n" /* the PS1's per-texel rule: clear and translucent texels let rays through */
    "                uint c = surface_texel(tri, vec2(u, v));\n"
    "                if (c == 0u || ((tri.info.w & 4u) != 0u && (c & 0x8000u) != 0u)) continue;\n"
    "            }\n"
    "            t_hit = t; hit = i; bc_hit = vec2(u, v);\n"
    "            if (any_hit) return true;\n"
    "        }\n"
    "    }\n"
    "    return hit >= 0;\n"
    "}\n"
    PC_LIGHT_SHAPE_GLSL /* a spot's edge, a rect's face and barn doors (light_shape.h) */
    /* Direct light from the dynamic lights at a world point, with shadow rays. */
    "vec3 direct(vec3 p, vec3 n) {\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int i = 0; i < light_count; i++) {\n"
    "        vec3 centre = light_pos[i].xyz;\n"
    "        if (light_col[i].w > 1.5) {\n"
    "            vec3 rr = light_right[i].xyz, ru = light_up[i].xyz;\n"
    "            centre += rr * (rand() * 2.0 - 1.0) + ru * (rand() * 2.0 - 1.0);\n" /* a point on the emitter */
    "        } else {\n"
    "            centre += (vec3(rand(), rand(), rand()) - 0.5) * 2.0 * (light_right[i].w > 0.0 ? light_right[i].w : light_size);\n" /* soft shadows; .w: the light's own size */
    "        }\n"
    "        vec3 to = centre - p; float d = length(to); vec3 l = to / max(d, 1e-3);\n"
    "        float x = clamp(d / light_pos[i].w, 0.0, 1.0);\n"
    "        float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);\n"
    "        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], p, l);\n"
    "        if (a <= 0.0) continue;\n"
    "        float th; int hit; vec2 bc;\n"
    "        if (trace(p + n * 1.5, l, d - max(3.0, light_up[i].w), true, n, 0.0, th, hit, bc)) continue;\n"
    "        sum += light_col[i].rgb * a;\n"
    "    }\n"
    "    return sum + emitted(p, n);\n"
    "}\n"
    /* Shadow visibility of the shadow-casting lights at a G-buffer surface:
     * each light's unshadowed contribution weights its traced visibility
     * (one ray to a random point on the emitter). 1 = fully lit. */
    "float visibility(vec3 p, vec3 n) {\n"
    "    float lit = 0.0, all_ = 0.0;\n"
    "    for (int j = 0; j < light_count * shadow_samples; j++) {\n"
    "        int i = j / shadow_samples;\n"
    "        if (light_shadow[i] == 0) continue;\n"
    "        vec3 centre = light_pos[i].xyz;\n"
    "        vec3 to = centre - p; float d = length(to); vec3 l = to / max(d, 1e-3);\n"
    "        float x = clamp(d / light_pos[i].w, 0.0, 1.0);\n"
    "        float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);\n"
    "        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], p, l);\n"
    "        float w = a * dot(light_col[i].rgb, vec3(0.299, 0.587, 0.114));\n"
    "        if (w <= 0.0) continue;\n"
    "        if (light_col[i].w > 1.5) centre += light_right[i].xyz * (rand() * 2.0 - 1.0) + light_up[i].xyz * (rand() * 2.0 - 1.0);\n"
    "        else {\n" /* a point in a sphere of the emitter's size */
    "            vec3 r = vec3(rand(), rand(), rand()) * 2.0 - 1.0;\n"
    "            centre += normalize(r + 1e-4) * pow(rand(), 0.333) * (light_right[i].w > 0.0 ? light_right[i].w : light_size);\n"
    "        }\n"
    "        to = centre - p; d = length(to); l = to / max(d, 1e-3);\n"
    "        float th; int hit; vec2 bc;\n"
    "        bool blocked = dot(n, l) <= 0.0 || trace(p, l, d - max(3.0, light_up[i].w), true, n, 16.0, th, hit, bc);\n"
    "        all_ += w; lit += blocked ? 0.0 : w;\n"
    "    }\n"
    "    return all_ > 0.0 ? lit / all_ : 1.0;\n"
    "}\n"
    /* A point of emitter i's shape (rt_lights.glsl emitter_sample the same):
     * its small sphere, or a column around it (glows.c: the glowing side of
     * a column a character can stand in, the save point's; w < 0 solid). */
    "vec3 emitter_sample(int i, vec4 r) {\n"
    "    vec4 shape = emitter_shape[i];\n"
    "    if (shape.w == 0.0) {\n"
    "        vec3 v = r.xyz * 2.0 - 1.0;\n"
    "        return emitter_pos[i].xyz + normalize(v + 1e-4) * pow(r.w, 0.333) * abs(emitter_col[i].w);\n"
    "    }\n"
    "    float h = length(shape.xyz);\n"
    "    vec3 a = h > 1e-3 ? shape.xyz / h : vec3(0.0, 1.0, 0.0);\n"
    "    vec3 t = normalize(cross(abs(a.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), a)), b = cross(a, t);\n"
    "    float phi = 6.2831853 * r.x, radius = shape.w > 0.0 ? shape.w : -shape.w * sqrt(r.y);\n"
    "    return emitter_pos[i].xyz + shape.xyz * (2.0 * r.z - 1.0) + (t * cos(phi) + b * sin(phi)) * radius;\n"
    "}\n"
    "vec4 emitter_random() { float a = rand(), b = rand(), c = rand(); return vec4(a, b, c, rand()); }\n"
    /* Shadows of a column's side (rt_lights.glsl the same): rays aim at a
     * thinner core and end where they enter the column, so a character
     * standing in it casts no shadow from it and one outside a clear soft one. */
    "float emitter_column_entry(int i, vec3 p, vec3 dir) {\n"
    "    vec4 shape = emitter_shape[i];\n"
    "    float h = length(shape.xyz);\n"
    "    vec3 a = h > 1e-3 ? shape.xyz / h : vec3(0.0, 1.0, 0.0), v = p - emitter_pos[i].xyz;\n"
    "    vec3 pp = v - a * dot(v, a), dd = dir - a * dot(dir, a);\n"
    "    float A = dot(dd, dd), B = dot(pp, dd), C = dot(pp, pp) - emitter_shadow[i].z * emitter_shadow[i].z;\n"
    "    if (C <= 0.0) return 0.0;\n"
    "    float disc = B * B - A * C;\n"
    "    if (A < 1e-8 || B >= 0.0 || disc < 0.0) return 1e30;\n"
    "    return (-B - sqrt(disc)) / A;\n"
    "}\n"
    "float emitter_shadow_ray(int i, vec3 p, vec4 r, out vec3 dir) {\n"
    "    vec4 shape = emitter_shape[i];\n"
    "    if (shape.w <= 0.0) {\n"
    "        vec3 to = emitter_sample(i, r) - p; float d = length(to); dir = to / max(d, 1e-3);\n"
    "        return d - (shape.w == 0.0 ? abs(emitter_col[i].w) : 2.0);\n"
    "    }\n"
    "    float h = length(shape.xyz);\n"
    "    vec3 a = h > 1e-3 ? shape.xyz / h : vec3(0.0, 1.0, 0.0);\n"
    "    vec3 t = normalize(cross(abs(a.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), a)), b = cross(a, t);\n"
    "    float phi = 6.2831853 * r.x;\n"
    "    vec3 to = emitter_pos[i].xyz + shape.xyz * (2.0 * r.z - 1.0) + (t * cos(phi) + b * sin(phi)) * emitter_shadow[i].x - p;\n"
    "    float d = length(to); dir = to / max(d, 1e-3);\n"
    "    return min(d, emitter_column_entry(i, p, dir)) - 2.0;\n"
    "}\n"
    /* The same, noise-free (the signature's rays): the sphere's centre, the core's nearest point. */
    "float emitter_shadow_nearest(int i, vec3 p, out vec3 dir) {\n"
    "    vec4 shape = emitter_shape[i];\n"
    "    vec3 target = emitter_pos[i].xyz;\n"
    "    if (shape.w > 0.0) {\n"
    "        vec3 v = p - target; float h2 = dot(shape.xyz, shape.xyz);\n"
    "        vec3 on_axis = target + shape.xyz * (h2 > 1e-6 ? clamp(dot(v, shape.xyz) / h2, -1.0, 1.0) : 0.0);\n"
    "        vec3 out_ = p - on_axis; float r = length(out_);\n"
    "        target = r > 1e-3 ? on_axis + out_ / r * min(emitter_shadow[i].x, r) : on_axis;\n"
    "    }\n"
    "    vec3 to = target - p; float d = length(to); dir = to / max(d, 1e-3);\n"
    "    if (shape.w > 0.0) return min(d, emitter_column_entry(i, p, dir)) - 2.0;\n"
    "    return d - (shape.w == 0.0 ? abs(emitter_col[i].w) : 2.0);\n"
    "}\n"
    "vec3 shadow_dir; float shadow_reach;\n" /* the last emitter_falloff's shadow ray */
    "float emitter_falloff(int i, vec3 p, vec3 n, out vec3 l, out float d) {\n"
    "    vec4 r = emitter_random();\n"
    "    vec3 to = emitter_sample(i, r) - p; d = length(to); l = to / max(d, 1e-3);\n"
    "    shadow_reach = emitter_shadow_ray(i, p, r, shadow_dir);\n"
    "    float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);\n"
    "    return (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);\n"
    "}\n"
    /* Light from the emitters, shadowed by everything. */
    "vec3 emitted(vec3 p, vec3 n) {\n"
    "    if (emitter_light <= 0.0 || emitter_count == 0) return vec3(0.0);\n"
    /* One glow, picked in proportion to its unshadowed light (no rays). */
    "    float weights[32]; float all_ = 0.0;\n"
    "    for (int i = 0; i < emitter_count; i++) {\n"
    "        weights[i] = 0.0;\n"
    "        if (emitter_col[i].w < 0.0) continue;\n" /* a baked light: already in the vertex colours */
    "        vec3 to = emitter_pos[i].xyz - p; float d = length(to);\n"
    "        float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);\n"
    "        weights[i] = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, to / max(d, 1e-3)), 0.0) * dot(emitter_col[i].rgb, vec3(0.299, 0.587, 0.114));\n"
    "        all_ += weights[i];\n"
    "    }\n"
    "    if (all_ <= 0.0) return vec3(0.0);\n"
    "    float pick = rand() * all_; int i = 0;\n"
    "    for (; i < emitter_count - 1; i++) { pick -= weights[i]; if (pick <= 0.0 && weights[i] > 0.0) break; }\n"
    "    if (weights[i] <= 0.0) return vec3(0.0);\n"
    "    vec3 l; float d, th; int hit; vec2 bc;\n"
    "    float a = emitter_falloff(i, p, n, l, d);\n"
    "    if (a <= 0.0 || (glow_shadows != 0 && ((glow_casts >> i) & 1) != 0 && shadow_reach > 2.0 && trace(p + n * 1.5, shadow_dir, shadow_reach - 2.0, true, n, 0.0, th, hit, bc))) return vec3(0.0);\n"
    "    return emitter_col[i].rgb * a * emitter_light * all_ / weights[i];\n" /* divided by the pick probability */
    "}\n"
    /* How much of the baked light reaches p past characters and props: their
     * contact occlusion, and their shadows from the emitters (the room's own
     * shadowing is already baked). 1 = all of it. */
    "float baked_visibility(vec3 p, vec3 n, vec3 skip_n) {\n"
    "    moving_only = true;\n"
    "    float occlusion = 0.0;\n"
    "    for (int k = 0; k < 2; k++) {\n"
    "        float t; int hit; vec2 bc;\n"
    "        if (contact_radius > 0.0 && trace(p, cosine_direction(n), contact_radius, false, skip_n, 0.0, t, hit, bc))\n"
    "            occlusion += 1.0 - 0.5 * t / contact_radius;\n"
    "    }\n"
    "    float ao = 1.0 - 0.5 * occlusion;\n"
    /* Emitter shadows: the unshadowed weights of all emitters (no rays),
     * then shadow rays to emitters picked in proportion to them. */
    "    float weights[32]; float all_ = 0.0;\n"
    "    for (int i = 0; i < emitter_count; i++) {\n"
    "        vec3 to = emitter_pos[i].xyz - p; float d = length(to);\n"
    "        if (emitter_col[i].w >= 0.0 && (glow_shadows == 0 || ((glow_casts >> i) & 1) == 0)) { weights[i] = 0.0; continue; }\n" /* glows: no shadows unless asked */
    "        float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);\n"
    "        weights[i] = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, to / max(d, 1e-3)), 0.0) * (emitter_shadow[i].y > 0.0 ? emitter_shadow[i].y : emitter_shadow[i].y < 0.0 ? 0.0 : dot(emitter_col[i].rgb, vec3(0.299, 0.587, 0.114)));\n" /* y < 0: not the baked light's (an emissive object, glows.c) */
    "        all_ += weights[i];\n"
    "    }\n"
    "    float shadowed = 0.0;\n"
    "    if (all_ > 0.0) {\n"
    "        for (int k = 0; k < shadow_samples; k++) {\n"
    "            float pick = rand() * all_; int i = 0;\n"
    "            for (; i < emitter_count - 1; i++) { pick -= weights[i]; if (pick <= 0.0) break; }\n"
    "            vec3 l; float d, th; int hit; vec2 bc;\n"
    "            if (emitter_falloff(i, p, n, l, d) > 0.0 && shadow_reach > 0.0 && trace(p, shadow_dir, shadow_reach, true, skip_n, 0.0, th, hit, bc))\n"
    "                shadowed += 1.0;\n"
    "        }\n"
    "        shadowed /= float(shadow_samples);\n"
    "    }\n"
    "    moving_only = false;\n"
    "    float near = all_ / (all_ + 0.1);\n" /* fades out away from emitters */
    "    return ao * (1.0 - (1.0 - baked_ambient) * near * shadowed);\n"
    "}\n"
    /* Light in the haze along the view ray from the eye to p: at each
     * jittered step one source (dynamic light or emitter) is picked in
     * proportion to its unshadowed contribution and checked with a shadow
     * ray; attenuated by the haze between the step and the eye. */
    "float haze_reach; vec3 haze_dir;\n" /* a column glow's shadow ray (< 0: to the sample) */
    "vec3 source_light(int i, vec3 x, out vec3 l, out float d) {\n"
    "    vec3 centre; vec4 pos; vec3 col;\n"
    "    haze_reach = -1.0;\n"
    "    if (i < light_count) {\n"
    "        centre = light_pos[i].xyz; pos = light_pos[i]; col = light_col[i].rgb;\n"
    "        if (light_col[i].w > 1.5) centre += light_right[i].xyz * (rand() * 2.0 - 1.0) + light_up[i].xyz * (rand() * 2.0 - 1.0);\n"
    "    } else {\n"
    "        int e = i - light_count; vec4 r = emitter_random(); centre = emitter_sample(e, r); pos = emitter_pos[e]; col = emitter_col[e].rgb * fog_glow;\n"
    "        if (emitter_shape[e].w > 0.0) haze_reach = max(emitter_shadow_ray(e, x, r, haze_dir), 0.0);\n"
    "    }\n"
    "    vec3 to = centre - x; d = length(to); l = to / max(d, 1e-3);\n"
    "    float f = clamp(d / pos.w, 0.0, 1.0);\n"
    "    float a = (1.0 - f * f) * (1.0 - f * f);\n"
    "    if (i < light_count) a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], x, l);\n"
    "    return col * a;\n"
    "}\n"
    /* The medium (medium.glsl: haze, ground fog, volumes) along the part of
     * the ray it can be in; at each step its scattering colour times the
     * light (one picked source, the sun with its own ray) plus what it adds
     * by itself, Beer-Lambert between the steps. */
    "vec3 haze(vec3 p) {\n"
    "    vec3 v = p - eye; float length_ = length(v); vec3 dir = v / max(length_, 1e-3);\n"
    "    vec2 span = medium_span(eye, dir, length_);\n"
    "    if (span.y <= span.x) return vec3(0.0);\n"
    "    float step_ = (span.y - span.x) / float(fog_steps);\n"
    "    int sources = light_count + emitter_count;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    float g = medium_g(), transmittance = 1.0;\n"
    "    for (int k = 0; k < fog_steps; k++) {\n"
    "        vec3 x = eye + dir * (span.x + (float(k) + rand()) * step_);\n"
    "        vec3 scatter, emit;\n"
    "        float extinction = medium_at(x, scatter, emit);\n"
    "        if (extinction <= 0.0) continue;\n"
    "        vec3 here = vec3(0.0);\n"
    "        if (env_sun_on()) {\n" /* the sun's shafts */
    "            vec3 l = env_sun_sample(rand(), rand()); float th; int hit; vec2 bc;\n"
    "            bool lit = env_sun_col.w < 0.5 || !trace(x, l, ENV_SUN_DISTANCE, true, l, 0.0, th, hit, bc);\n"
    "            here += env_sun_col.rgb * medium_phase(dot(dir, l), g) * medium_sun_visibility(lit ? 1.0 : 0.0);\n"
    "        }\n"
    "        if (env_sky_on() && MEDIUM(5).w > 0.0) {\n" /* the sky, occluded (rt_haze.comp the same) */
    "            float z = 1.0 - 2.0 * rand(), s = sqrt(max(1.0 - z * z, 0.0)), a = 6.2831853 * rand();\n"
    "            vec3 d = vec3(s * cos(a), z, s * sin(a)); float th; int hit; vec2 bc;\n"
    "            if (!trace(x, d, ENV_SUN_DISTANCE, true, d, 0.0, th, hit, bc))\n"
    "                here += env_sky(d) * MEDIUM(5).w * medium_phase(dot(dir, d), g);\n"
    "        }\n"
    "        float a_ = medium_step(extinction, step_, transmittance);\n"
    "        float weights[48]; float all_ = 0.0;\n"
    "        for (int i = 0; i < sources && i < 48; i++) {\n"
    "            vec3 pos = i < light_count ? light_pos[i].xyz : emitter_pos[i - light_count].xyz;\n"
    "            float r = i < light_count ? light_pos[i].w : emitter_pos[i - light_count].w;\n"
    "            vec3 col = i < light_count ? light_col[i].rgb : emitter_col[i - light_count].rgb * fog_glow;\n"
    "            float f = clamp(distance(pos, x) / r, 0.0, 1.0);\n"
    "            weights[i] = (1.0 - f * f) * (1.0 - f * f) * dot(col, vec3(0.299, 0.587, 0.114));\n"
    "            all_ += weights[i];\n"
    "        }\n"
    "        if (all_ > 0.0) {\n"
    "            float pick = rand() * all_; int i = 0;\n"
    "            for (; i < sources - 1; i++) { pick -= weights[i]; if (pick <= 0.0) break; }\n"
    "            vec3 l; float d, th; int hit; vec2 bc;\n"
    "            vec3 c = source_light(i, x, l, d);\n"
    "            bool column = haze_reach >= 0.0; vec3 sl = column ? haze_dir : l;\n"
    "            if (weights[i] > 0.0 && !((i < light_count || ((glow_casts >> (i - light_count)) & 1) != 0)\n"
    "                    && (column ? haze_reach > 0.0 && trace(x, sl, haze_reach, true, sl, 0.0, th, hit, bc) : trace(x, l, d - 2.0, true, l, 0.0, th, hit, bc))))\n"
    "                here += c * (all_ / weights[i]) * medium_phase(dot(dir, l), g);\n" /* phase x 4 pi: isotropic 1 */
    "        }\n"
    "        sum += transmittance * (scatter * here + emit) * ((1.0 - a_) / extinction);\n"
    "        transmittance *= a_;\n"
    "    }\n"
    "    return medium_limit_light(sum);\n"
    "}\n"
    /* The lighting signature: noise-free centre rays to the shadow-casting
     * lights and (for the baked light) the emitters, one bit each. History
     * survives only where it is unchanged (a shadow edge passing drops it). */
    "uint lighting_signature(vec3 p, vec3 n) {\n"
    "    uint sig = 0u; int bit = 0;\n"
    "    for (int i = 0; i < light_count && bit < 12; i++) {\n"
    "        if (light_shadow[i] == 0) continue;\n"
    "        vec3 to = light_pos[i].xyz - p; float d = length(to);\n"
    "        float th; int hit; vec2 bc;\n"
    "        if (d < light_pos[i].w && dot(n, to) > 0.0 && !trace(p, to / max(d, 1e-3), d - max(3.0, light_up[i].w), true, n, 16.0, th, hit, bc))\n"
    "            sig |= 1u << bit;\n"
    "        bit++;\n"
    "    }\n"
    "    if (baked_shadows != 0) {\n"
    "        moving_only = true;\n"
    "        for (int i = 0; i < emitter_count && bit < 18; i++) {\n"
    "            vec3 to = emitter_pos[i].xyz - p; float d = length(to);\n"
    "            if (d >= emitter_pos[i].w || dot(n, to) <= 0.0 || ((glow_casts >> i) & 1) == 0) continue;\n" /* out of reach: no bit (cheap) */
    "            bit++;\n"
    "            float th; int hit; vec2 bc;\n"
    "            vec3 dir; float reach = emitter_shadow_nearest(i, p, dir);\n"
    "            if (reach <= 0.0 || !trace(p, dir, reach, true, n, 0.0, th, hit, bc))\n"
    "                sig |= 1u << bit;\n"
    "        }\n"
    "        moving_only = false;\n"
    "    }\n"
    "    return sig;\n"
    "}\n"
    /* The room's sun and sky (rt_trace.comp the same): the sun's own rays,
     * never competing with the lights; the sky for GI rays leaving the room. */
    "vec3 sun_light(vec3 p, vec3 n) {\n"
    "    if (!env_sun_on()) return vec3(0.0);\n"
    "    vec3 l = env_sun_sample(rand(), rand());\n"
    "    float c = dot(n, l);\n"
    "    if (c <= 0.0) return vec3(0.0);\n"
    "    float th; int hit; vec2 bc;\n"
    "    if (env_sun_col.w > 0.5 && trace(p + n * 1.5, l, ENV_SUN_DISTANCE, true, n, 0.0, th, hit, bc)) return vec3(0.0);\n"
    "    return env_sun_col.rgb * c;\n"
    "}\n"
    "float visibility_with_sun(vec3 p, vec3 n) {\n"
    "    float v = visibility(p, n);\n"
    "    float ws = env_sun_col.w > 0.5 ? max(dot(n, env_sun_dir.xyz), 0.0) * dot(env_sun_col.rgb, vec3(0.299, 0.587, 0.114)) : 0.0;\n"
    "    if (ws <= 0.0) return v;\n"
    "    float all_ = 0.0;\n" /* visibility()'s weights */
    "    for (int i = 0; i < light_count; i++) {\n"
    "        if (light_shadow[i] == 0) continue;\n"
    "        vec3 to = light_pos[i].xyz - p; float d = length(to); vec3 l = to / max(d, 1e-3);\n"
    "        float x = clamp(d / light_pos[i].w, 0.0, 1.0);\n"
    "        float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);\n"
    "        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], p, l);\n"
    "        all_ += max(a * dot(light_col[i].rgb, vec3(0.299, 0.587, 0.114)), 0.0) * float(shadow_samples);\n"
    "    }\n"
    "    float lit = 0.0;\n"
    "    for (int s = 0; s < shadow_samples; s++) {\n"
    "        vec3 l = env_sun_sample(rand(), rand());\n"
    "        float th; int hit; vec2 bc;\n"
    "        if (dot(n, l) > 0.0 && !trace(p, l, ENV_SUN_DISTANCE, true, n, 16.0, th, hit, bc)) lit += 1.0;\n"
    "    }\n"
    "    ws *= float(shadow_samples);\n"
    "    return (v * all_ + lit / float(shadow_samples) * ws) / (all_ + ws);\n"
    "}\n"
    "uint sun_signature(vec3 p, vec3 n) {\n" /* bit 19: the sun's centre ray */
    "    if (env_sun_col.w < 0.5 || !env_sun_on() || dot(n, env_sun_dir.xyz) <= 0.0) return 0u;\n"
    "    float th; int hit; vec2 bc;\n"
    "    return trace(p, env_sun_dir.xyz, ENV_SUN_DISTANCE, true, n, 16.0, th, hit, bc) ? 0u : 1u << 19;\n"
    "}\n"
    "vec3 cosine_direction(vec3 n) {\n"
    "    float r1 = rand(), r2 = rand(), phi = 6.2831853 * r1, s = sqrt(r2);\n"
    "    vec3 t = normalize(cross(abs(n.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), n)), b = cross(n, t);\n"
    "    return normalize(t * cos(phi) * s + b * sin(phi) * s + n * sqrt(1.0 - r2));\n"
    "}\n"
    "void main() {\n"
    "    ivec2 g = ivec2(gl_GlobalInvocationID.xy), size = imageSize(gi_out);\n"
    "    if (any(greaterThanEqual(g, size))) return;\n"
    "    vec4 pw = imageLoad(position_in, g);\n"
    "    if (pw.w < 0.5) { imageStore(gi_out, g, vec4(0.0, 0.0, 0.0, 1.0)); imageStore(extra_out, g, vec4(1.0, 0.0, 0.0, 0.0)); return; }\n"
    "    vec3 p = pw.xyz, n = imageLoad(normal_in, g).xyz;\n"
    "    float z = distance(p, eye);\n"
    "    rng = uint(g.x) * 1973u + uint(g.y) * 9277u + frame * 26699u;\n"
    "    float lift = 2.0 + z * 0.002;\n" /* off the surface, more with distance (depth precision) */
    "    if (trace_debug == 1) { imageStore(gi_out, g, vec4(n * 0.5 + 0.5, 1.0)); return; }\n"
    "    if (trace_debug == 3) { imageStore(gi_out, g, vec4(fract(p / 256.0), 1.0)); return; }\n"
    "    if (trace_debug == 2) {\n"
    "        float t; int hit; vec2 bc;\n"
    "        bool any = trace(p + n * lift, cosine_direction(n), 8192.0, false, n, 16.0, t, hit, bc);\n"
    "        imageStore(gi_out, g, any ? (t < 8.0 ? vec4(1, 0, 0, 1) : vec4(vec3(t / 1024.0), 1.0)) : vec4(0, 0, 1, 1));\n"
    "        return;\n"
    "    }\n"
    "    vec3 total = vec3(0.0);\n"
    "    float vis = shadows != 0 ? visibility_with_sun(p + n * lift, n) : 1.0;\n"
    "    for (int r = 0; r < (indirect != 0 ? rays : 0); r++) {\n"
    "        vec3 o = p + n * lift, nrm = n, throughput = vec3(1.0);\n"
    "        for (int b = 0; b < bounces; b++) {\n"
    "            vec3 d = cosine_direction(nrm);\n"
    "            float t; int hit; vec2 bc;\n"
    "            if (!trace(o, d, 8192.0, false, nrm, b == 0 ? 16.0 : 0.0, t, hit, bc)) {\n"
    "                total += throughput * env_sky(d);\n" /* left the room: the sky (none indoors) */
    "                break;\n"
    "            }\n"
    "            Tri tri = tris[hit];\n"
    "            vec3 y = o + d * t;\n"
    "            vec3 ny = normalize(cross(tri.b.xyz - tri.a.xyz, tri.c.xyz - tri.a.xyz));\n"
    "            if (dot(ny, d) > 0.0) ny = -ny;\n"
    "            vec3 vc = vertex_rgb(tri.a.w) * (1.0 - bc.x - bc.y) + vertex_rgb(tri.b.w) * bc.x + vertex_rgb(tri.c.w) * bc.y;\n"
    "            vec3 albedo = vc;\n"
    "            if ((tri.info.w & 1u) != 0u) {\n"
    "                uint c = surface_texel(tri, bc);\n"
    "                albedo = vec3(c & 31u, (c >> 5) & 31u, (c >> 10) & 31u) / 31.0;\n"
    "            }\n"
    /* Light leaving y toward us: its albedo times the light reaching it,
     * plus (optionally) its baked lighting as if emitted. */
    "            vec3 baked = (tri.info.w & 2u) != 0u ? albedo : min(albedo * vc * 2.0, 1.0);\n"
    "            total += throughput * (albedo * (direct(y + ny * 0.5, ny) + sun_light(y + ny * 0.5, ny)) + baked * baked_bounce);\n"
    "            throughput *= albedo;\n"
    "            o = y + ny * 1.0; nrm = ny;\n"
    "        }\n"
    "    }\n"
    "    vec3 gi = total / float(rays) + emitted(p + n * lift, n);\n"
    /* Never let a NaN or infinity in: it would live on in the history and
     * spread through the filters (black blotches on some GPUs). */
    "    if (any(isnan(gi)) || any(isinf(gi))) gi = vec3(0.0);\n"
    "    if (isnan(vis) || isinf(vis)) vis = 1.0;\n"
    "    imageStore(gi_out, g, vec4(gi, vis));\n"
    /* The haze is marched for every pixel each frame: marching a rotating
     * quarter of the screen left the rest to history, which a moving
     * camera drops, and the haze shook in workgroup-sized blocks. */
    "    vec3 fog = fog_density > 0.0 ? haze(p + n * lift) : vec3(0.0);\n"
    /* Characters and props carry the game's own per-frame lighting, not the
     * room's baked light: only room surfaces take its visibility. */
    "    bool moving_surface = pw.w > 1.5;\n"
    "    float bvis = baked_shadows != 0 && !moving_surface ? baked_visibility(p + n * lift, n, n) : 1.0;\n"
    "    if (isnan(bvis) || isinf(bvis)) bvis = 1.0;\n"
    "    if (any(isnan(fog)) || any(isinf(fog))) fog = vec3(-1.0);\n"
    "    imageStore(extra_out, g, vec4(bvis, fog));\n"
    "    imageStore(signature_out, g, uvec4(lighting_signature(p + n * lift, n) | sun_signature(p + n * lift, n)));\n"
    "}\n";

/* Surfaces from the game view's G-buffer: world position and normal of
 * each GI pixel, reconstructed from GTE depth through the camera. */
static const char* game_surface_source =
    "#version 430 core\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(rgba32f, binding = 1) uniform writeonly image2D position_out;\n" /* world xyz, w 1 = surface */
    "layout(rgba16f, binding = 2) uniform writeonly image2D normal_out;\n"
    "uniform sampler2D normal_tex;\n" /* G-buffer: view normal * 0.5 + 0.5, a = lit surface */
    "uniform sampler2D depth_tex;\n"  /* SZ / 65536 */
    "uniform ivec4 rect;\n"           /* display x, y, w, h in VRAM pixels */
    "uniform int scale;\n"            /* target pixels per VRAM pixel */
    "uniform vec2 gi_scale;\n"        /* GI pixels per display pixel */
    "uniform vec3 projection;\n"      /* H (narrowed for widescreen), H */
    "uniform vec2 screen_offset;\n"
    "uniform mat3 inverse_rotation;\n"
    "uniform mat3 normal_to_world;\n"
    "uniform vec3 camera_translation;\n"
    "void main() {\n"
    "    ivec2 g = ivec2(gl_GlobalInvocationID.xy), size = imageSize(position_out);\n"
    "    if (any(greaterThanEqual(g, size))) return;\n"
    "    vec2 display = (vec2(g) + 0.5) / gi_scale;\n" /* display pixels */
    "    ivec2 q = ivec2((vec2(rect.xy) + display) * float(scale));\n"
    "    q = ivec2(q.x % (1024 * scale), q.y % (512 * scale));\n"
    "    vec4 nn = texelFetch(normal_tex, q, 0);\n"
    "    float z = texelFetch(depth_tex, q, 0).r * 65536.0;\n"
    "    if (nn.a < 0.5 || z < 0.5 || z >= 65535.0) { imageStore(position_out, g, vec4(0.0)); imageStore(normal_out, g, vec4(0.0)); return; }\n"
    "    vec2 s = display - screen_offset;\n"
    "    vec3 view = vec3(s.x * z / projection.x, s.y * z / projection.y, z);\n"
    "    imageStore(position_out, g, vec4(inverse_rotation * (view - camera_translation), nn.a > 0.9 ? 1.0 : 2.0));\n" /* w 2: moving */
    "    imageStore(normal_out, g, vec4(normalize(normal_to_world * normalize(nn.xyz * 2.0 - 1.0)), 0.0));\n"
    "}\n";

/* Surfaces from world-space position and normal textures (the editor view
 * renders them directly). */
static const char* world_surface_source =
    "#version 430 core\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(rgba32f, binding = 1) uniform writeonly image2D position_out;\n"
    "layout(rgba16f, binding = 2) uniform writeonly image2D normal_out;\n"
    "uniform sampler2D world_position;\n" /* xyz, w 1 = surface */
    "uniform sampler2D world_normal;\n"
    "void main() {\n"
    "    ivec2 g = ivec2(gl_GlobalInvocationID.xy), size = imageSize(position_out);\n"
    "    if (any(greaterThanEqual(g, size))) return;\n"
    "    ivec2 source = textureSize(world_position, 0);\n"
    "    ivec2 q = min(ivec2((vec2(g) + 0.5) / vec2(size) * vec2(source)), source - 1);\n"
    "    vec4 p = texelFetch(world_position, q, 0);\n"
    "    imageStore(position_out, g, p.w > 0.5 ? vec4(p.xyz, texelFetch(world_normal, q, 0).w > 1.5 ? 2.0 : 1.0) : vec4(0.0));\n"
    "    imageStore(normal_out, g, vec4(normalize(texelFetch(world_normal, q, 0).xyz + 1e-6), 0.0));\n"
    "}\n";

/* Temporal accumulation (strict history): each surface takes the one
 * previous GI pixel it reprojects to, and only if that pixel saw the same
 * surface (normal within ~18 degrees, plane and position within a pixel
 * footprint, same static/moving class) lit the same way: the lighting
 * signature, noise-free centre rays to the main lights, must match. A
 * shadow moving onto or off a surface changes its signature and drops its
 * history at once, so history can be long elsewhere without ghosting. */
static const char* temporal_source =
    "#version 430 core\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(rgba16f, binding = 0) uniform readonly image2D current;\n"
    "layout(rgba32f, binding = 1) uniform readonly image2D position;\n"
    "layout(rgba16f, binding = 2) uniform image2D normal;\n" /* xyz; w = history length (written here) */
    "layout(rgba16f, binding = 3) uniform writeonly image2D accumulated;\n" /* rgb indirect, a shadow visibility */
    "layout(rgba16f, binding = 4) uniform readonly image2D current_extra;\n" /* r baked light visibility, gba haze */
    "layout(rgba16f, binding = 5) uniform writeonly image2D accumulated_extra;\n"
    "layout(r32ui, binding = 6) uniform readonly uimage2D signature;\n"
    "uniform sampler2D history_extra;\n"
    "uniform sampler2D history;\n"
    "uniform sampler2D history_position;\n"
    "uniform sampler2D history_normal;\n"
    "uniform usampler2D history_signature;\n"
    "uniform mat4 previous_to_image;\n" /* world -> previous GI image (x, y in 0..1) * w, w = view depth */
    "uniform float footprint;\n"        /* world units per GI pixel per unit of depth */
    "uniform float max_history;\n"
    "uniform int valid_history;\n"
    "void main() {\n"
    "    ivec2 g = ivec2(gl_GlobalInvocationID.xy), size = imageSize(accumulated);\n"
    "    if (any(greaterThanEqual(g, size))) return;\n"
    "    vec4 c = imageLoad(current, g), p = imageLoad(position, g);\n"
    "    vec4 e = imageLoad(current_extra, g);\n"
    /* No surface: neutral (no indirect light, fully visible), never "black". */
    "    if (p.w < 0.5) { imageStore(accumulated, g, vec4(0.0, 0.0, 0.0, 1.0)); imageStore(accumulated_extra, g, vec4(1.0, 0.0, 0.0, 0.0)); return; }\n"
    "    vec3 n = imageLoad(normal, g).xyz;\n"
    "    uint sig = imageLoad(signature, g).r;\n"
    "    bool reuse = false; vec4 hc, hx; float len = 1.0;\n"
    "    if (valid_history != 0) {\n"
    "        vec4 v = previous_to_image * vec4(p.xyz, 1.0);\n"
    "        if (v.w > 1.0) {\n"
    "            ivec2 h = ivec2(floor(v.xy / v.w * vec2(size)));\n"
    "            if (all(greaterThanEqual(h, ivec2(0))) && all(lessThan(h, size))) {\n"
    "                vec4 hp = texelFetch(history_position, h, 0), hn = texelFetch(history_normal, h, 0);\n"
    "                float fp = footprint * v.w;\n"
    "                hc = texelFetch(history, h, 0); hx = texelFetch(history_extra, h, 0);\n"
    "                reuse = hp.w > 0.5 && abs(hp.w - p.w) < 0.5 && dot(hn.xyz, n) >= 0.95\n"
    "                    && abs(dot(hp.xyz - p.xyz, n)) < 0.5 * fp + 1.0 && length(hp.xyz - p.xyz) < 2.0 * fp + 2.0\n"
    "                    && texelFetch(history_signature, h, 0).r == sig\n"
    "                    && !any(isnan(hc)) && !any(isnan(hx)) && !any(isinf(hc)) && !any(isinf(hx));\n"
    "                if (reuse) len = min(hn.w + 1.0, max_history);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    vec4 result = c, result_extra = e;\n"
    "    if (reuse) {\n"
    "        float a = 1.0 / len;\n"
    "        result = mix(hc, c, a);\n"
    "        vec3 haze = e.g < 0.0 ? hx.gba : hx.g < 0.0 ? e.gba : mix(hx.gba, e.gba, max(a, 1.0 / 8.0));\n"
    "        result_extra = vec4(mix(hx.r, e.r, a), haze);\n"
    "    }\n"
    "    imageStore(accumulated, g, result);\n"
    "    imageStore(accumulated_extra, g, result_extra);\n"
    "    imageStore(normal, g, vec4(n, len));\n"
    "}\n";

/* One a-trous wavelet iteration (5x5 B3 kernel at `step`), edge-stopping on
 * normal and plane distance; the luminance stop loosens with history. */
static const char* atrous_source =
    "#version 430 core\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(rgba16f, binding = 0) uniform readonly image2D source;\n"
    "layout(rgba32f, binding = 1) uniform readonly image2D position;\n"
    "layout(rgba16f, binding = 2) uniform readonly image2D normal;\n"
    "layout(rgba16f, binding = 3) uniform writeonly image2D target;\n"
    "layout(rgba16f, binding = 4) uniform readonly image2D source_extra;\n"
    "layout(rgba16f, binding = 5) uniform writeonly image2D target_extra;\n"
    "uniform int step;\n"
    "uniform int filter_visibility;\n" /* the shadow term only gets the finest passes */
    "uniform int final_pass;\n" /* blend back toward the unfiltered history as it converges */
    "layout(rgba16f, binding = 6) uniform readonly image2D unfiltered;\n"
    "layout(rgba16f, binding = 7) uniform readonly image2D unfiltered_extra;\n"
    "void main() {\n"
    "    ivec2 g = ivec2(gl_GlobalInvocationID.xy), size = imageSize(target);\n"
    "    if (any(greaterThanEqual(g, size))) return;\n"
    "    vec4 c = imageLoad(source, g), p = imageLoad(position, g);\n"
    "    vec4 ce = imageLoad(source_extra, g);\n"
    "    if (p.w < 0.5) { imageStore(target, g, c); imageStore(target_extra, g, ce); return; }\n"
    "    vec4 nl = imageLoad(normal, g); vec3 n = nl.xyz;\n"
    "    float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    float sigma_l = 4.0 / sqrt(max(nl.w, 1.0)) * max(lum, 0.05);\n"
    "    float sigma_v = 2.0 / sqrt(max(nl.w, 1.0));\n"
    "    const float kernel[3] = float[](3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);\n"
    "    vec3 sum = vec3(0.0), hsum = vec3(0.0); float weight = 0.0, vsum = 0.0, vweight = 0.0, esum = 0.0, eweight = 0.0, hweight = 0.0;\n"
    "    for (int y = -2; y <= 2; y++)\n"
    "        for (int x = -2; x <= 2; x++) {\n"
    "            ivec2 q = g + ivec2(x, y) * step;\n"
    "            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size))) continue;\n"
    "            vec4 qp = imageLoad(position, q);\n"
    "            if (qp.w < 0.5) continue;\n"
    "            vec3 qn = imageLoad(normal, q).xyz;\n"
    "            vec4 qc = imageLoad(source, q);\n"
    "            float w = kernel[abs(x)] * kernel[abs(y)];\n"
    /* The haze is an integral along the view ray, not a property of the
     * surface: it blurs across polygons whatever their normals (normal
     * weights left it faceted, one shade per polygon), only not across a
     * big jump in distance (a silhouette). */
    "            float wh = w * exp(-length(qp.xyz - p.xyz) / 240.0);\n"
    "            w *= pow(max(dot(n, qn), 0.0), 32.0);\n"
    "            w *= exp(-abs(dot(qp.xyz - p.xyz, n)) / 4.0);\n"
    "            float wv = w * exp(-abs(qc.a - c.a) / sigma_v);\n"
    "            vec4 qx = imageLoad(source_extra, q); float qe = qx.r;\n"
    "            float we = w * exp(-abs(qe - ce.r) / sigma_v);\n"
    "            esum += qe * we; eweight += we;\n"
    "            if (qx.g >= 0.0) { hsum += qx.gba * wh; hweight += wh; }\n" /* -1: no haze sample yet */
    "            w *= exp(-abs(dot(qc.rgb, vec3(0.299, 0.587, 0.114)) - lum) / sigma_l);\n"
    "            sum += qc.rgb * w; weight += w; vsum += qc.a * wv; vweight += wv;\n"
    "        }\n"
    "    float v = filter_visibility != 0 && vweight > 0.0 ? vsum / vweight : c.a;\n"
    "    vec4 out_ = vec4(weight > 0.0 ? sum / weight : c.rgb, v);\n"
    "    vec4 out_extra = vec4(filter_visibility != 0 && eweight > 0.0 ? esum / eweight : ce.r, hweight > 0.0 ? hsum / hweight : ce.gba);\n"
    "    if (final_pass != 0) out_extra.gba = max(out_extra.gba, vec3(0.0));\n"
    "    if (final_pass != 0) {\n" /* converged pixels keep their own detail (up to 3/4) */
    "        float keep = clamp((nl.w - 1.0) / 32.0, 0.0, 0.75);\n"
    "        out_ = mix(out_, imageLoad(unfiltered, g), keep);\n"
    "        out_extra.r = mix(out_extra.r, imageLoad(unfiltered_extra, g).r, keep);\n"
    "    }\n"
    "    imageStore(target, g, out_);\n"
    "    imageStore(target_extra, g, out_extra);\n"
    "}\n";

/* ---- passes ---- */

static GLuint trace_program, temporal_program, atrous_program, game_surface_program, world_surface_program;
static int state; /* 0 untried, 1 ready, -1 unavailable */
enum {
    RAW, POSITION, NORMAL, HISTORY_A, HISTORY_B, POSITION_OLD, NORMAL_OLD, FILTER_A, FILTER_B,
    RAW_EXTRA, HISTORY_EXTRA_A, HISTORY_EXTRA_B, FILTER_EXTRA_A, FILTER_EXTRA_B, SIGNATURE, SIGNATURE_OLD, IMAGES
};
/* One per view (the game, the editor): its images and its history. */
typedef struct {
    GLuint images[IMAGES];
    int size[2], history_current, history_valid;
    float previous_to_image[16];
    unsigned frame;
    GLuint extra; /* the last result's baked light visibility (r) */
    GLuint result; /* the last result (game view; the editor inspector reads it) */
    int result_scene; /* the room (pc_scene_serial) the result was traced in */
} gi_context;
static gi_context game_context, editor_context;

static int init(void)
{
    static int checked;
    const char* setting = getenv("ROOD_GI");
    if (!checked++ && setting && *setting == '0') {
        /* ROOD_GI=0: off from the start (long scripted tests on software
         * GL); the overlay can still turn it on. */
        pc_gi_enabled = pc_rt_shadows = pc_baked_shadows = 0;
        return 0;
    }
    if (state)
        return state > 0;
    state = -1;
    if (!pc_gl_compute)
        return 0;
    trace_program = pc_gl_compute_program(trace_source);
    temporal_program = pc_gl_compute_program(temporal_source);
    atrous_program = pc_gl_compute_program(atrous_source);
    game_surface_program = pc_gl_compute_program(game_surface_source);
    world_surface_program = pc_gl_compute_program(world_surface_source);
    if (!trace_program || !temporal_program || !atrous_program || !game_surface_program || !world_surface_program)
        return 0;
    gl_GenBuffers(1, &triangle_buffer);
    gl_GenBuffers(1, &node_buffer);
    state = 1;
    fprintf(stderr, "[gi] path-traced global illumination ready\n");
    return 1;
}
static void ensure_images(gi_context* c, int w, int h)
{
    if (c->size[0] == w && c->size[1] == h)
        return;
    if (c->size[0])
        gl_DeleteTextures(IMAGES, c->images);
    gl_GenTextures(IMAGES, c->images);
    for (int i = 0; i < IMAGES; i++) {
        GLenum format = i == POSITION || i == POSITION_OLD ? GL_RGBA32F
            : i == SIGNATURE || i == SIGNATURE_OLD         ? GL_R32UI
                                                           : GL_RGBA16F;
        gl_BindTexture(GL_TEXTURE_2D, c->images[i]);
        gl_TexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    c->size[0] = w, c->size[1] = h;
    c->history_valid = 0;
    c->result = c->extra = 0; /* the old images are gone */
}
#define U(program, name) gl_GetUniformLocation(program, name)
static void dispatch(int w, int h)
{
    gl_DispatchCompute((GLuint)(w + 7) / 8, (GLuint)(h + 7) / 8, 1);
    gl_MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

/* Lights for the passes (world space). */
static float lp[16][4], lc[16][4], ld[16][4], lr[16][4], lu[16][4];
static int light_shadows[16], light_count;
/* The room's sun and sky this frame (environment.h): the passes run for them
 * alone too (an outdoor room without lights). */
static pc_env_gpu env_now;
static int env_active;
static int gather_env(void)
{
    env_active = pc_env_gpu_get(&env_now);
    return env_active;
}
static int gather_lights(void)
{
    memset(light_shadows, 0, sizeof light_shadows);
    light_count = pc_lights_world(lp, lc, ld, lr, lu, light_shadows, 16);
    return light_count || (pc_gi_baked_bounce > 0 && pc_gi_enabled);
}

/* Trace, accumulate and filter for a context whose POSITION and NORMAL
 * images hold this frame's surfaces. `to_image` maps world points to this
 * frame's GI image (x, y in 0..1, times w; w = view depth), kept for the
 * next frame's reprojection. Returns the result (rgb indirect, a shadow
 * visibility). */
static GLuint run(gi_context* c, const float to_image[16], const float eye[3], float footprint)
{
    int gw = c->size[0], gh = c->size[1];
    GLuint* images = c->images;
    gl_BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangle_buffer);
    gl_BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, node_buffer);
    /* 1: trace. */
    GLuint pr = trace_program;
    gl_UseProgram(pr);
    gl_BindImageTexture(0, images[RAW], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl_BindImageTexture(1, images[POSITION], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    gl_BindImageTexture(2, images[NORMAL], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    gl_ActiveTexture(GL_TEXTURE2);
    gl_BindTexture(GL_TEXTURE_2D, pc_hw_native_texture());
    gl_Uniform1i(U(pr, "vram"), 2);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_Uniform3f(U(pr, "eye"), eye[0], eye[1], eye[2]);
    gl_Uniform1i(U(pr, "light_count"), light_count);
    gl_Uniform4fv(U(pr, "light_pos"), 16, &lp[0][0]);
    gl_Uniform4fv(U(pr, "light_col"), 16, &lc[0][0]);
    gl_Uniform4fv(U(pr, "light_dir"), 16, &ld[0][0]);
    gl_Uniform4fv(U(pr, "light_right"), 16, &lr[0][0]);
    gl_Uniform4fv(U(pr, "light_up"), 16, &lu[0][0]);
    gl_Uniform1iv(U(pr, "light_shadow"), 16, light_shadows);
    gl_Uniform1i(U(pr, "indirect"), pc_gi_enabled); /* only with gi_method path (probes: probe_gi.inc) */
    gl_Uniform1i(U(pr, "shadows"), pc_rt_shadows);
    gl_Uniform1f(U(pr, "light_size"), pc_light_size);
    gl_Uniform1i(U(pr, "bounces"), pc_gi_bounces < 1 ? 1 : pc_gi_bounces > 4 ? 4 : pc_gi_bounces);
    gl_Uniform1i(U(pr, "rays"), pc_gi_rays < 1 ? 1 : pc_gi_rays > 16 ? 16 : pc_gi_rays);
    gl_Uniform1ui(U(pr, "frame"), ++c->frame);
    gl_Uniform1f(U(pr, "baked_bounce"), pc_gi_baked_bounce);
    gl_Uniform1i(U(pr, "trace_debug"), pc_gi_trace_debug);
    gl_Uniform1i(U(pr, "emitter_count"), emitter_count);
    gl_Uniform1i(U(pr, "room_root"), room_root);
    gl_Uniform1i(U(pr, "caster_root"), caster_root);
    gl_Uniform4fv(U(pr, "emitter_pos"), MAX_EMITTERS, &emitter_pos[0][0]);
    gl_Uniform4fv(U(pr, "emitter_col"), MAX_EMITTERS, &emitter_col[0][0]);
    gl_Uniform4fv(U(pr, "emitter_shape"), MAX_EMITTERS, &emitter_shape[0][0]);
    gl_Uniform4fv(U(pr, "emitter_shadow"), MAX_EMITTERS, &emitter_shadow[0][0]);
    gl_Uniform1f(U(pr, "emitter_light"), pc_emitter_light);
    gl_Uniform1i(U(pr, "baked_shadows"), pc_baked_shadows);
    gl_Uniform1f(U(pr, "contact_radius"), pc_contact_radius);
    gl_Uniform1f(U(pr, "baked_ambient"), pc_baked_ambient);
    gl_Uniform1i(U(pr, "shadow_samples"), pc_shadow_samples < 1 ? 1 : pc_shadow_samples > 8 ? 8 : pc_shadow_samples);
    gl_Uniform1i(U(pr, "glow_shadows"), pc_glow_shadows);
    gl_Uniform1i(U(pr, "glow_casts"), (int)pc_glow_casts);
    {
        /* The room's medium (atmosphere.h): haze High only. */
        static pc_medium_gpu medium;
        int on = pc_haze_quality == PC_HAZE_HIGH
            && pc_medium_gpu_get(&medium, pc_lighting_traced_haze_density(), room_lo, room_hi);
        medium.v[5][0] = medium.v[5][1] = medium.v[5][2] = 0; /* the march traces the sky */
        gl_Uniform1f(U(pr, "fog_density"), on ? 1.0f : 0.0f);
        gl_Uniform4fv(U(pr, "medium_"), PC_MEDIUM_VEC4S, &medium.v[0][0]);
        gl_ActiveTexture(GL_TEXTURE3);
        gl_BindTexture(GL_TEXTURE_3D, pc_gl_medium_texture());
        gl_Uniform1i(U(pr, "medium_tex"), 3);
        gl_ActiveTexture(GL_TEXTURE0);
    }
    gl_Uniform1i(U(pr, "fog_steps"), pc_fog_steps < 2 ? 2 : pc_fog_steps > 64 ? 64 : pc_fog_steps);
    gl_Uniform1f(U(pr, "fog_anisotropy"), pc_fog_anisotropy);
    gl_Uniform1f(U(pr, "fog_glow"), pc_fog_glow);
    gl_Uniform3fv(U(pr, "room_lo"), 1, room_lo);
    gl_Uniform3fv(U(pr, "room_hi"), 1, room_hi);
    gl_Uniform4fv(U(pr, "env_"), 5, &env_now.sun_dir[0]);
    gl_BindImageTexture(3, images[RAW_EXTRA], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl_BindImageTexture(4, images[SIGNATURE], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
    dispatch(gw, gh);
    /* ROOD_GI_SLOW=n (testing): trace n more times, as a slow GPU would take
     * (the result is the same; tests/gi_lag_smoke.py). */
    static int slow = -1;
    if (slow < 0)
        slow = getenv("ROOD_GI_SLOW") ? atoi(getenv("ROOD_GI_SLOW")) : 0;
    for (int i = 0; i < slow && i < 200; i++)
        dispatch(gw, gh);

    /* 2: temporal accumulation into the other history image. */
    int old = HISTORY_A + c->history_current, out = HISTORY_A + (c->history_current ^ 1);
    int old_extra = HISTORY_EXTRA_A + c->history_current, out_extra = HISTORY_EXTRA_A + (c->history_current ^ 1);
    pr = temporal_program;
    gl_UseProgram(pr);
    gl_BindImageTexture(0, images[RAW], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    gl_BindImageTexture(1, images[POSITION], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    gl_BindImageTexture(2, images[NORMAL], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    gl_BindImageTexture(3, images[out], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl_BindImageTexture(4, images[RAW_EXTRA], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    gl_BindImageTexture(5, images[out_extra], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl_BindImageTexture(6, images[SIGNATURE], 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
    GLuint history_textures[5] = { images[old], images[POSITION_OLD], images[NORMAL_OLD], images[old_extra],
        images[SIGNATURE_OLD] };
    const char* history_names[5] = { "history", "history_position", "history_normal", "history_extra",
        "history_signature" };
    for (int i = 0; i < 5; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + i);
        gl_BindTexture(GL_TEXTURE_2D, history_textures[i]);
        gl_Uniform1i(U(pr, history_names[i]), i);
    }
    gl_ActiveTexture(GL_TEXTURE0);
    gl_UniformMatrix4fv(U(pr, "previous_to_image"), 1, GL_FALSE, c->previous_to_image);
    gl_Uniform1f(U(pr, "max_history"), pc_gi_denoise ? pc_shadow_history : 1.0f);
    gl_Uniform1f(U(pr, "footprint"), footprint);
    gl_Uniform1i(U(pr, "valid_history"), c->history_valid);
    dispatch(gw, gh);
    /* The current surface becomes next frame's history surface. */
    gl_CopyImageSubData(images[POSITION], GL_TEXTURE_2D, 0, 0, 0, 0, images[POSITION_OLD], GL_TEXTURE_2D, 0, 0, 0, 0,
        gw, gh, 1);
    gl_CopyImageSubData(images[NORMAL], GL_TEXTURE_2D, 0, 0, 0, 0, images[NORMAL_OLD], GL_TEXTURE_2D, 0, 0, 0, 0, gw,
        gh, 1);
    gl_CopyImageSubData(images[SIGNATURE], GL_TEXTURE_2D, 0, 0, 0, 0, images[SIGNATURE_OLD], GL_TEXTURE_2D, 0, 0, 0, 0,
        gw, gh, 1);
    memcpy(c->previous_to_image, to_image, sizeof c->previous_to_image);
    c->history_current ^= 1;
    c->history_valid = 1;

    /* 3: a-trous filtering of the accumulated light (display only). */
    GLuint result = images[out];
    c->extra = images[out_extra];
    if (pc_gi_denoise) {
        pr = atrous_program;
        gl_UseProgram(pr);
        gl_BindImageTexture(1, images[POSITION], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        gl_BindImageTexture(2, images[NORMAL], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
        GLuint source = images[out], source_extra = images[out_extra];
        for (int i = 0; i < 4; i++) {
            GLuint target = images[i & 1 ? FILTER_B : FILTER_A];
            GLuint target_extra = images[i & 1 ? FILTER_EXTRA_B : FILTER_EXTRA_A];
            gl_BindImageTexture(0, source, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            gl_BindImageTexture(3, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            gl_BindImageTexture(4, source_extra, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            gl_BindImageTexture(5, target_extra, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            gl_Uniform1i(U(pr, "step"), 1 << i);
            gl_Uniform1i(U(pr, "filter_visibility"), i < 2);
            gl_Uniform1i(U(pr, "final_pass"), i == 3);
            gl_BindImageTexture(6, images[out], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            gl_BindImageTexture(7, images[out_extra], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            dispatch(gw, gh);
            source = target;
            source_extra = target_extra;
        }
        result = source;
        c->extra = source_extra;
    }
    gl_UseProgram(0);
    return result;
}
static int ready(void)
{
    pc_baked_shadows_active = 0; /* until this frame's trace (the drop shadow steps aside for it) */
    if (!(pc_gi_enabled || pc_rt_shadows || pc_baked_shadows) || !pc_lights_enabled || !init() || !update_scene())
        return 0;
    update_emitters();
    pc_scene_bounds(room_lo, room_hi);
    int lights = gather_lights();
    gather_env();
    pc_baked_shadows_active = pc_baked_shadows && (emitter_count || pc_contact_radius > 0);
    return lights || pc_baked_shadows_active || (emitter_count && pc_emitter_light > 0) || env_active;
}

static void probe_gl_update(void); /* probe_gi.inc */

/* ---- cadence: the path tracer on a GPU too slow for it ----
 * The game's GPU time per present is the path tracer's plus the rest. When
 * the tracer alone takes longer than its budget, it runs on every n-th
 * present only (n = its GPU time / the budget, at most 8) and the presents
 * between show its last result: the light, shadow visibility and the
 * surfaces they were traced for (pc_gi_surface), so the present pass's
 * edge-aware lookup stays consistent and falls back to neutral where the
 * view moved on. Presentation keeps its rate; the lighting updates less
 * often. The tracer's GPU time comes from timestamp queries read a few
 * frames later (never waited for). A new room, a new GI size or no result
 * yet always trace.
 *
 * pc_gi_budget_ms (RPC graphics gi_budget, ROOD_GI_BUDGET): < 0 auto, 0
 * off (trace every present), else the budget. Auto is 16 ms (a 60 Hz
 * frame: a tracer that fits keeps running every present; Ultra here takes
 * ~15 ms on an RTX PRO 6000), and 50 ms in fast mode (tests, no vsync
 * pacing): unloaded runs trace every present as before, a GPU shared with
 * other runs still finishes its steps in time. */
float pc_gi_budget_ms = -1.0f;
extern int pc_window_fast(void) __attribute__((weak));
float pc_gi_budget_effective(void)
{
    static int env = -1;
    static float env_budget;
    if (env < 0) {
        const char* v = getenv("ROOD_GI_BUDGET");
        env = v != NULL;
        env_budget = v ? (float)atof(v) : 0;
    }
    if (pc_gi_budget_ms >= 0)
        return pc_gi_budget_ms;
    if (env)
        return env_budget;
    return pc_window_fast && pc_window_fast() ? 50.0f : 16.0f;
}
#define CADENCE_QUERIES 4
static GLuint cadence_queries[CADENCE_QUERIES][2];
static int cadence_pending[CADENCE_QUERIES], cadence_next;
static double cadence_ms;      /* the tracer's GPU time (smoothed) */
static unsigned cadence_since; /* presents since the last trace */
static unsigned cadence_traced, cadence_reused;
static void cadence_read(void)
{
    for (int i = 0; i < CADENCE_QUERIES; i++) {
        GLint available = 0;
        if (!cadence_pending[i])
            continue;
        gl_GetQueryObjectiv(cadence_queries[i][1], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            continue;
        GLuint64 begin = 0, end = 0;
        gl_GetQueryObjectui64v(cadence_queries[i][0], GL_QUERY_RESULT, &begin);
        gl_GetQueryObjectui64v(cadence_queries[i][1], GL_QUERY_RESULT, &end);
        double ms = end > begin ? (end - begin) * 1e-6 : 0;
        /* Up at once (a GPU falling behind), down smoothly. */
        cadence_ms = ms > cadence_ms ? ms : cadence_ms * 0.8 + ms * 0.2;
        cadence_pending[i] = 0;
    }
}
/* Timestamps around one trace (begin 1, end 0); none while all are in flight. */
static int cadence_slot = -1;
static void cadence_mark(int begin)
{
    if (begin) {
        if (!cadence_queries[0][0])
            gl_GenQueries(CADENCE_QUERIES * 2, &cadence_queries[0][0]);
        cadence_slot = cadence_pending[cadence_next] ? -1 : cadence_next;
        if (cadence_slot >= 0)
            gl_QueryCounter(cadence_queries[cadence_slot][0], GL_TIMESTAMP);
        return;
    }
    if (cadence_slot < 0)
        return;
    gl_QueryCounter(cadence_queries[cadence_slot][1], GL_TIMESTAMP);
    cadence_pending[cadence_slot] = 1;
    cadence_next = (cadence_slot + 1) % CADENCE_QUERIES;
    cadence_slot = -1;
}
/* Presents between traces now (1: every present). */
int pc_gi_cadence_interval(void)
{
    float budget = pc_gi_budget_effective();
    if (budget <= 0 || cadence_ms <= budget)
        return 1;
    int n = (int)ceil(cadence_ms / budget);
    return n > 8 ? 8 : n;
}
void pc_gi_cadence_stats(unsigned* traced, unsigned* reused, double* gi_ms, int reset)
{
    *traced = cadence_traced, *reused = cadence_reused, *gi_ms = cadence_ms;
    if (reset)
        cadence_traced = cadence_reused = 0;
}

/* Game view: this frame's indirect light and shadow visibility for the
 * display area (x, y, w, h in VRAM), at GI resolution. Returns the texture
 * (rgb indirect irradiance, a visibility) or 0. */
/* The display area's size of the last game-view GI update (GL or Vulkan):
 * pc_gi_probe_world maps world points into it. */
static int gi_display_size[2];
GLuint pc_gi_update(int x, int y, int w, int h)
{
    float r[3][3], t[3], hh, ox, oy, inverse[3][3];
    if (w <= 0 || h <= 0 || !ready() || !pc_scene_camera_get(r, t, &hh, &ox, &oy) || !pc_scene_camera_inverse(inverse))
        return 0;
    gi_display_size[0] = w, gi_display_size[1] = h;
    gi_context* c = &game_context;
    float res = pc_gi_resolution < 0.25f ? 0.25f : pc_gi_resolution > 4 ? 4 : pc_gi_resolution;
    int gw = (int)(w * res + 0.5f), gh = (int)(h * res + 0.5f);
    ensure_images(c, gw, gh);
    static unsigned cut_seen;
    if (cut_seen != pc_hw_history_cut) {
        /* Frames went unpresented (battle charging's skip): restart the history (GL; vrt_run for Vulkan). */
        cut_seen = pc_hw_history_cut;
        c->history_valid = 0;
        cadence_since = (unsigned)pc_gi_cadence_interval(); /* traced now, not the last result */
    }
    cadence_read();
    if (c->result && c->result_scene == pc_scene_serial() && (int)++cadence_since < pc_gi_cadence_interval()) {
        ++cadence_reused;
        probe_gl_update();
        return c->result; /* the last result, with the surfaces it was traced for */
    }
    cadence_since = 0;
    ++cadence_traced;
    cadence_mark(1);
    float hx = hh * (pc_gte_widescreen ? 0.75f : 1.0f);
    GLuint normal, albedo;
    pc_hw_gbuffer(&normal, &albedo);
    /* Surfaces from the G-buffer. */
    GLuint pr = game_surface_program;
    gl_UseProgram(pr);
    gl_BindImageTexture(1, c->images[POSITION], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    gl_BindImageTexture(2, c->images[NORMAL], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, normal);
    gl_Uniform1i(U(pr, "normal_tex"), 0);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, pc_hw_depth_texture());
    gl_Uniform1i(U(pr, "depth_tex"), 1);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_Uniform4i(U(pr, "rect"), x, y, w, h);
    gl_Uniform1i(U(pr, "scale"), pc_hw_scale);
    gl_Uniform2f(U(pr, "gi_scale"), (float)gw / w, (float)gh / h);
    gl_Uniform3f(U(pr, "projection"), hx, hh, 0);
    gl_Uniform2f(U(pr, "screen_offset"), ox, oy);
    float m[9], nt[9];
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) {
            m[col * 3 + row] = inverse[row][col];
            nt[col * 3 + row] = r[col][row];
        }
    gl_UniformMatrix3fv(U(pr, "inverse_rotation"), 1, GL_FALSE, m);
    gl_UniformMatrix3fv(U(pr, "normal_to_world"), 1, GL_FALSE, nt);
    gl_Uniform3f(U(pr, "camera_translation"), t[0], t[1], t[2]);
    dispatch(gw, gh);
    /* World -> GI image: x = (OFX + Hx X / Z) / w, y = (OFY + H Y / Z) / h,
     * with (X, Y, Z) = R p + t; column-major, w = Z. */
    float to_image[16];
    for (int k = 0; k < 4; k++) {
        float r0 = k < 3 ? r[0][k] : t[0], r1 = k < 3 ? r[1][k] : t[1], r2 = k < 3 ? r[2][k] : t[2];
        to_image[k * 4 + 0] = (hx * r0 + ox * r2) / w;
        to_image[k * 4 + 1] = (hh * r1 + oy * r2) / h;
        to_image[k * 4 + 2] = 0;
        to_image[k * 4 + 3] = r2;
    }
    float eye[3];
    for (int k = 0; k < 3; k++)
        eye[k] = -(inverse[k][0] * t[0] + inverse[k][1] * t[1] + inverse[k][2] * t[2]);
    c->result = run(c, to_image, eye, 1.0f / (hh * (float)gh / (float)h)); /* GI pixels per unit of X/Z: H scaled */
    c->result_scene = pc_scene_serial();
    cadence_mark(0);
    probe_gl_update(); /* gi_method probes: the probe field (probe_gi.inc) */
    return c->result;
}

/* Editor inspector: the game view's last GI result at (u, v) in [0, 1] of
 * the display area: rgba (indirect light, shadow visibility) and extra
 * (baked light visibility, haze rgb). Returns 0 without a result. */
#ifdef ROOD_VULKAN
#include "vk_hooks.h"
static int vrt_probe(float u, float v, float out[8]);
static int vrt_trace_cpu(const float o[3], const float d[3], float tmax, float* t_out, float centre[3]);
#endif
int pc_gi_probe(float u, float v, float out[8])
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return vrt_probe(u, v, out);
#endif
    gi_context* c = &game_context;
    if (!c->result || !c->size[0] || u < 0 || v < 0 || u >= 1 || v >= 1)
        return 0;
    int x = (int)(u * c->size[0]), y = (int)(v * c->size[1]);
    static GLuint fbo;
    if (!fbo)
        gl_GenFramebuffers(1, &fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->result, 0);
    gl_ReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, out);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->extra, 0);
    gl_ReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, out + 4);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

/* RPC gi_probe {x, y, z}: the game view's GI result where world point p is
 * on screen (the scene camera, as the surfaces are reconstructed: view =
 * R p + T, display = offset + H view.xy / view.z), its (u, v) too. Tests
 * read the shadows on the floor around a save point with it (GL and Vulkan
 * alike). Returns 0 when p is behind the camera, off screen or without a
 * result. */
int pc_gi_probe_world(const float p[3], float out[8], float uv[2])
{
    float r[3][3], t[3], hh, ox, oy, v[3];
    if (gi_display_size[0] <= 0 || !pc_scene_camera_get(r, t, &hh, &ox, &oy))
        return 0;
    for (int i = 0; i < 3; i++)
        v[i] = r[i][0] * p[0] + r[i][1] * p[1] + r[i][2] * p[2] + t[i];
    if (v[2] < 1.0f)
        return 0;
    float hx = hh * (pc_gte_widescreen ? 0.75f : 1.0f);
    uv[0] = (ox + hx * v[0] / v[2]) / (float)gi_display_size[0];
    uv[1] = (oy + hh * v[1] / v[2]) / (float)gi_display_size[1];
    return pc_gi_probe(uv[0], uv[1], out);
}

/* OpenGL: the scene pc_gi_trace_cpu traces (this room and this frame's
 * casters), built now on the CPU when the path tracer has not (GI and the
 * traced shadows off; its GPU buffers follow once it runs): the particles'
 * shadow rays (particles.cpp). Returns 1 when there is one (Vulkan: 0, its
 * particles trace on the GPU). */
int pc_gi_scene_cpu(void)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return 0;
#endif
    if (!update_scene())
        return 0;
    return triangle_count > 0 && node_count > 0 && built_scene == pc_scene_serial(); /* this room's */
}
/* Editor inspector: the nearest triangle a ray from o along d (unit) hits
 * before tmax, against the scene the path tracer uses (texture cut-outs
 * not tested). Returns its flags (1 textured .. 8 moving) or -1, the
 * distance and the triangle's centre. */
int pc_gi_trace_cpu(const float o[3], const float d[3], float tmax, float* t_out, float centre[3])
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return vrt_trace_cpu(o, d, tmax, t_out, centre);
#endif
    if (!triangle_count || !node_count)
        return -1;
    int stack[64], sp = 0, best = -1;
    float best_t = tmax;
    if (room_root >= 0)
        stack[sp++] = room_root;
    if (caster_root >= 0)
        stack[sp++] = caster_root;
    while (sp) {
        const gpu_node* n = &nodes[stack[--sp]];
        float t0 = 0, t1 = best_t;
        for (int k = 0; k < 3; k++) {
            float inv = 1.0f / (fabsf(d[k]) > 1e-9f ? d[k] : 1e-9f);
            float a = (n->lo[k] - o[k]) * inv, b = (n->hi[k] - o[k]) * inv;
            if (a > b) { float tmp = a; a = b; b = tmp; }
            t0 = a > t0 ? a : t0;
            t1 = b < t1 ? b : t1;
        }
        if (t0 > t1)
            continue;
        int first = (int)float_bits(n->lo[3]), count = (int)float_bits(n->hi[3]);
        if (!count) {
            if (sp < 62)
                stack[sp++] = first, stack[sp++] = first + 1;
            continue;
        }
        for (int i = first; i < first + count; i++) {
            const gpu_triangle* tri = &triangles[order[i]];
            float e1[3], e2[3], pv[3], tv[3], qv[3];
            for (int k = 0; k < 3; k++)
                e1[k] = tri->b[k] - tri->a[k], e2[k] = tri->c[k] - tri->a[k], tv[k] = o[k] - tri->a[k];
            pv[0] = d[1] * e2[2] - d[2] * e2[1], pv[1] = d[2] * e2[0] - d[0] * e2[2], pv[2] = d[0] * e2[1] - d[1] * e2[0];
            float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
            if (fabsf(det) < 1e-8f)
                continue;
            float inv = 1.0f / det, bu = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
            if (bu < 0 || bu > 1)
                continue;
            qv[0] = tv[1] * e1[2] - tv[2] * e1[1], qv[1] = tv[2] * e1[0] - tv[0] * e1[2], qv[2] = tv[0] * e1[1] - tv[1] * e1[0];
            float bv = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * inv;
            if (bv < 0 || bu + bv > 1)
                continue;
            float t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
            if (t > 1e-3f && t < best_t)
                best_t = t, best = order[i];
        }
    }
    if (best < 0)
        return -1;
    *t_out = best_t;
    for (int k = 0; k < 3; k++)
        centre[k] = (triangles[best].a[k] + triangles[best].b[k] + triangles[best].c[k]) / 3.0f;
    return (int)triangles[best].info[3];
}
/* Game view: the GI pixels' world positions and normals, for edge-aware
 * upsampling. */
void pc_gi_surface(GLuint* position, GLuint* normal, int* w, int* h)
{
    *position = game_context.images[POSITION], *normal = game_context.images[NORMAL];
    *w = game_context.size[0], *h = game_context.size[1];
}
/* The last result's baked light visibility (r) for the game or editor view. */
GLuint pc_gi_extra(int editor) { return editor ? editor_context.extra : game_context.extra; }

/* Editor view: surfaces from its world position and normal textures (GL
 * rows, bottom first), seen through `view_projection` (column-major GL
 * clip) from `eye`; the GI image is `scale` of the texture size. Returns
 * the result and its surface images. */
GLuint pc_gi_update_world(GLuint world_position, GLuint world_normal, int w, int h, float scale,
    const float view_projection[16], const float eye[3], GLuint* position, GLuint* normal, int size[2])
{
    if (w <= 0 || h <= 0 || !ready())
        return 0;
    gi_context* c = &editor_context;
    int gw = (int)(w * scale + 0.5f), gh = (int)(h * scale + 0.5f);
    ensure_images(c, gw < 1 ? 1 : gw, gh < 1 ? 1 : gh);
    GLuint pr = world_surface_program;
    gl_UseProgram(pr);
    gl_BindImageTexture(1, c->images[POSITION], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    gl_BindImageTexture(2, c->images[NORMAL], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, world_position);
    gl_Uniform1i(U(pr, "world_position"), 0);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, world_normal);
    gl_Uniform1i(U(pr, "world_normal"), 1);
    gl_ActiveTexture(GL_TEXTURE0);
    dispatch(c->size[0], c->size[1]);
    /* GL clip -> image 0..1 (rows bottom first, like the source textures). */
    float to_image[16];
    for (int k = 0; k < 4; k++) {
        const float* col = view_projection + k * 4;
        to_image[k * 4 + 0] = 0.5f * (col[0] + col[3]);
        to_image[k * 4 + 1] = 0.5f * (col[1] + col[3]);
        to_image[k * 4 + 2] = 0;
        to_image[k * 4 + 3] = col[3];
    }
    /* World units per GI pixel per unit of depth: 2 tan(fov/2) / height. */
    float footprint = 2.0f / (view_projection[5] * (float)c->size[1]);
    GLuint result = run(c, to_image, eye, footprint);
    *position = c->images[POSITION], *normal = c->images[NORMAL];
    size[0] = c->size[0], size[1] = c->size[1];
    return result;
}
#include "probe_gi.inc" /* gi_method probes: the probe field (DDGI) */
#ifdef ROOD_VULKAN
#include "light_list.h"
#include "vk_rt.inc" /* ROOD_RENDERER=vulkan: the same passes with ray queries */
#endif
/* The denoiser in effect (pc_gi_denoise without NRD where it cannot run). */
int pc_gi_denoiser_active(void)
{
#ifdef ROOD_VULKAN
    if (pc_gi_denoise >= 2 && vrt_nrd_usable())
        return 2;
#endif
    return pc_gi_denoise ? 1 : 0;
}
