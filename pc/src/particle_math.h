/* The port's particle system (pc/docs/Particles.md; not the game's effect
 * particles): a mote as a closed-form function of its emitter's record, its
 * index and the time, so the simulation keeps no state, runs the same at any
 * frame rate, on the CPU (OpenGL: particles.cpp) and the GPU (Vulkan:
 * pc/shaders/vulkan/particles_sim.glsl, its GLSL copy: keep the two the
 * same), stops with the game and is deterministic for tests.
 *
 * Each mote i of an emitter lives `life` seconds (lifetime, less up to
 * `jitter` of it, per mote), offset by a random phase; its cycle k =
 * floor((t + phase) / life) reseeds its spawn point; its age runs 0..life.
 * From the spawn point it drifts (the drift velocity is its terminal
 * velocity), falls under gravity (y down; negative rises) slowed by drag to
 * g / drag, and the curl of a smooth vector field (divergence-free: swirls
 * without clumping) displaces it by `curl` world units, the field evolving
 * with the time. It fades in and out (smoothstep over fade_in, fade_out),
 * shines `gain` (1 - variation x a random) and is `size` whole game pixels.
 *
 * World space, y down. Plain C (pc/tests/particles_probe.cpp). */
#ifndef ROOD_PARTICLE_MATH_H
#define ROOD_PARTICLE_MATH_H
#include <math.h>
#include <stdint.h>

#define PC_PARTICLES_MAX 16384 /* motes per room: the key's low 16 bits; 64 rows of 256 colours */
#define PC_PARTICLE_EMITTERS 16 /* emitters per room the renderers take */
#define PC_PARTICLE_COLOUR_W 256 /* the colour image's width (its height PC_PARTICLES_MAX / 256) */
#define PC_PARTICLE_GLOWS 32 /* glows a mote may be lit by */
#define PC_PARTICLE_FLOOR_N 24 /* scene_capture.h PC_SCENE_FLOOR_GRID */
#define PC_PARTICLE_NEAR 16.0f /* nearer than this (view z): not drawn */
#define PC_PARTICLE_NONE 0xffffffffu /* a key image texel without a mote */

/* Spawn shapes (the item field `shape`; `room` is packed as a box). */
enum { PC_SPAWN_BOX = 0, PC_SPAWN_POINT = 1, PC_SPAWN_DISC = 2, PC_SPAWN_SURFACE = 3 };

/* One emitter as the renderers read it (particles_sim.glsl PARTICLE_EMITTER(e, k)):
 *  0 pos xyz, yaw (radians)
 *  1 size xyz (full extents; point and disc: x the radius), shape (PC_SPAWN_*)
 *  2 drift xyz (u/s), gravity (u/s^2, y down)
 *  3 curl amplitude (u), curl scale (u), curl speed (1/s), drag (1/s)
 *  4 lifetime (s), lifetime jitter (0..0.95), fade in (s), fade out (s)
 *  5 colour rgb, brightness
 *  6 pixel size min, max (1..4), variation (0..1), lit (0 in light only, 1 lit)
 *  7 seed (an integer below 2^24), ambient, first mote (index into the frame's), motes */
typedef struct {
    float v[8][4];
} pc_emitter_gpu;

/* The frame's motes for the Vulkan pass (particles_main.glsl `Particles`, std140). */
typedef struct {
    int info[4]; /* motes, emitters, display w, display h */
    int info2[4]; /* lights, glows, lights per mote (1..4), shadow rays (1: traced) */
    float time[4]; /* t (s), glow scale, debug (1: every mote white, unlit), glow shadows (1) */
    float projection[4]; /* hx (widescreen-narrowed), hy, ofx, ofy */
    float rotation[3][4]; /* the GTE camera: rows of R, w the translation (view = R p + T) */
    float env[5][4]; /* the room's sun and sky (environment.h pc_env_gpu) */
    float floor_info[4]; /* the floor grid: corner x, z, 1 / cell x, 1 / cell z */
    float floor_n[4]; /* cells per side (0: none) */
    float floor[PC_PARTICLE_FLOOR_N * PC_PARTICLE_FLOOR_N / 4][4]; /* heights, cell i + j n */
    float glow_pos[PC_PARTICLE_GLOWS][4]; /* xyz, radius of influence */
    float glow_col[PC_PARTICLE_GLOWS][4]; /* rgb, emitting radius */
    pc_emitter_gpu emitters[PC_PARTICLE_EMITTERS];
} pc_particles_gpu;

/* A mote now. */
typedef struct {
    float pos[3];
    float age, life, fade, gain;
    int size; /* game pixels */
    int cycle;
} pc_mote;

static inline uint32_t pc_particle_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
/* The next uniform number in 0..1 of a hash stream. */
static inline float pc_particle_unit(uint32_t* s)
{
    *s = pc_particle_hash(*s + 0x9e3779b9u);
    return (float)(*s >> 8) * (1.0f / 16777216.0f);
}
static inline float pc_particle_smooth(float x)
{
    x = fminf(fmaxf(x, 0.0f), 1.0f);
    return x * x * (3 - 2 * x);
}

/* The floor grid's height (world y) under (x, z): bilinear between cell
 * centres, clamped at its edges (medium.glsl medium_floor). */
static inline float pc_particle_floor(const float* heights, const float info[4], int n, float x, float z)
{
    float cx = (x - info[0]) * info[2] - 0.5f, cz = (z - info[1]) * info[3] - 0.5f;
    int i = (int)floorf(cx), j = (int)floorf(cz);
    float fx = cx - (float)i, fz = cz - (float)j;
    int i0 = i < 0 ? 0 : i >= n ? n - 1 : i, i1 = i + 1 < 0 ? 0 : i + 1 >= n ? n - 1 : i + 1;
    int j0 = j < 0 ? 0 : j >= n ? n - 1 : j, j1 = j + 1 < 0 ? 0 : j + 1 >= n ? n - 1 : j + 1;
    float a = heights[j0 * n + i0] + (heights[j0 * n + i1] - heights[j0 * n + i0]) * fx;
    float b = heights[j1 * n + i0] + (heights[j1 * n + i1] - heights[j1 * n + i0]) * fx;
    return a + (b - a) * fz;
}

/* The curl of psi, a sum of six plane waves (two per component; wave vector,
 * angular speed, phase): smooth, divergence-free, about unit strength. */
static const float PC_CURL_WAVES[6][5] = {
    { 0.83f, 1.31f, -0.47f, 0.9f, 0.0f }, { -1.62f, 0.38f, 1.17f, 1.3f, 1.3f },
    { 1.12f, -0.71f, 0.95f, 0.7f, 2.1f }, { 0.41f, 1.77f, -1.08f, 1.1f, 0.7f },
    { -0.92f, 0.56f, 1.43f, 0.8f, 4.2f }, { 1.36f, -1.21f, -0.33f, 1.5f, 3.3f },
};
static inline void pc_particle_curl(const float q[3], float tau, float out[3])
{
    float g[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }; /* the gradient of each component of psi */
    for (int j = 0; j < 6; j++) {
        const float* k = PC_CURL_WAVES[j];
        float w = ((j & 1) ? 0.5f : 1.0f) * cosf(k[0] * q[0] + k[1] * q[1] + k[2] * q[2] + tau * k[3] + k[4]);
        g[j >> 1][0] += w * k[0], g[j >> 1][1] += w * k[1], g[j >> 1][2] += w * k[2];
    }
    out[0] = 0.5f * (g[2][1] - g[1][2]);
    out[1] = 0.5f * (g[0][2] - g[2][0]);
    out[2] = 0.5f * (g[1][0] - g[0][1]);
}

/* Mote i of emitter e at time t (seconds, >= 0). `heights` (n x n, info:
 * pc_particles_gpu floor_info) is the room's floor grid for surface
 * emitters (NULL: none). Returns 1. */
static inline int pc_particle_mote(const pc_emitter_gpu* e, int i, float t, const float* heights, const float info[4], int n,
    pc_mote* m)
{
    const float(*v)[4] = e->v;
    uint32_t s = pc_particle_hash((uint32_t)v[7][0] * 0x9e3779b1u ^ (uint32_t)i * 0x85ebca77u);
    float jitter = fminf(fmaxf(v[4][1], 0.0f), 0.95f);
    float life = fmaxf(v[4][0], 0.05f) * (1.0f - jitter * pc_particle_unit(&s));
    float tt = fmaxf(t, 0.0f) + pc_particle_unit(&s) * life;
    float k = floorf(tt / life);
    float age = fminf(fmaxf(tt - k * life, 0.0f), life);
    uint32_t c = pc_particle_hash(s ^ ((uint32_t)(int32_t)k * 0xc2b2ae35u));
    float u0 = pc_particle_unit(&c), u1 = pc_particle_unit(&c), u2 = pc_particle_unit(&c);
    int shape = (int)v[1][3];
    float p[3];
    if (shape == PC_SPAWN_POINT) {
        float z = 1 - 2 * u0, r = sqrtf(fmaxf(1 - z * z, 0.0f)), a = 6.2831853f * u1, rr = v[1][0] * cbrtf(u2);
        p[0] = v[0][0] + rr * r * cosf(a), p[1] = v[0][1] + rr * z, p[2] = v[0][2] + rr * r * sinf(a);
    } else if (shape == PC_SPAWN_DISC) {
        float rr = v[1][0] * sqrtf(u0), a = 6.2831853f * u1;
        p[0] = v[0][0] + rr * cosf(a), p[1] = v[0][1], p[2] = v[0][2] + rr * sinf(a);
    } else { /* a box (the room's bounds come packed as one), or its footprint on the floor */
        float lx = (u0 - 0.5f) * v[1][0], ly = (u1 - 0.5f) * v[1][1], lz = (u2 - 0.5f) * v[1][2];
        float cs = cosf(v[0][3]), sn = sinf(v[0][3]);
        p[0] = v[0][0] + cs * lx + sn * lz, p[1] = v[0][1] + ly, p[2] = v[0][2] - sn * lx + cs * lz;
        if (shape == PC_SPAWN_SURFACE)
            p[1] = (heights && n > 0 ? pc_particle_floor(heights, info, n, p[0], p[2]) : v[0][1] + v[1][1] * 0.5f) - 1.0f;
    }
    float g = v[2][3], drag = v[3][3];
    float fall = drag > 1e-4f ? g * (age / drag - (1.0f - expf(-drag * age)) / (drag * drag)) : 0.5f * g * age * age;
    for (int a = 0; a < 3; a++)
        p[a] += v[2][a] * age;
    p[1] += fall;
    if (v[3][0] > 0) {
        float sc = fmaxf(v[3][1], 1.0f), q[3] = { p[0] / sc, p[1] / sc, p[2] / sc }, d[3];
        pc_particle_curl(q, t * v[3][2], d);
        for (int a = 0; a < 3; a++)
            p[a] += v[3][0] * d[a];
    }
    for (int a = 0; a < 3; a++)
        m->pos[a] = p[a];
    m->age = age, m->life = life, m->cycle = (int)k;
    m->fade = (v[4][2] > 0 ? pc_particle_smooth(age / v[4][2]) : 1.0f) * (v[4][3] > 0 ? pc_particle_smooth((life - age) / v[4][3]) : 1.0f);
    m->gain = 1.0f - fminf(fmaxf(v[6][2], 0.0f), 1.0f) * pc_particle_unit(&c);
    int lo = (int)v[6][0] < 1 ? 1 : (int)v[6][0] > 4 ? 4 : (int)v[6][0];
    int hi = (int)v[6][1] < lo ? lo : (int)v[6][1] > 4 ? 4 : (int)v[6][1];
    int size = lo + (int)(pc_particle_unit(&c) * (float)(hi - lo + 1));
    m->size = size > hi ? hi : size;
    return 1;
}

/* A world point in the display (the GTE camera: view = R p + T; display =
 * offset + H view.xy / view.z): out x, y (game pixels), z (SZ). 0 when
 * nearer than PC_PARTICLE_NEAR. */
static inline int pc_particle_project(const float rot[3][4], const float proj[4], const float p[3], float out[3])
{
    float vz = rot[2][0] * p[0] + rot[2][1] * p[1] + rot[2][2] * p[2] + rot[2][3];
    if (!(vz >= PC_PARTICLE_NEAR))
        return 0;
    float vx = rot[0][0] * p[0] + rot[0][1] * p[1] + rot[0][2] * p[2] + rot[0][3];
    float vy = rot[1][0] * p[0] + rot[1][1] * p[1] + rot[1][2] * p[2] + rot[1][3];
    out[0] = proj[2] + proj[0] * vx / vz, out[1] = proj[3] + proj[1] * vy / vz, out[2] = vz;
    return 1;
}
/* A key image texel: the nearer mote is the smaller key. */
static inline uint32_t pc_particle_key(float sz, int index)
{
    uint32_t z = sz >= 65535.0f ? 65535u : sz <= 1.0f ? 1u : (uint32_t)sz;
    return z << 16 | ((uint32_t)index & 0xffffu);
}
/* The first game pixel a mote of `size` pixels covers along one axis. */
static inline int pc_particle_anchor(float s, int size) { return (int)floorf(s - 0.5f * (float)(size - 1)); }
#endif
