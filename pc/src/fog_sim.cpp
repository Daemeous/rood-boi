// Interactive fog (fog_sim.h): fog volumes' top-down density fields, pushed
// by the room's characters, their attacks and spells. Off unless a volume
// has sim=1.
#include "fog_sim.h"

/* 32-bit builds: SSE maths here (x87 is several times slower for this loop). */
#if defined(__i386__) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC target("sse2,fpmath=sse")
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "dev_state.h"

namespace {
using namespace rood_fog_sim;
const int N = PC_FOG_SIM_GRID;
const int SLOTS = 17; /* the game's actor slots (Ashley first) */
const float VSYNC_DT = 1.0f / 59.94f;
/* Tuning: subtle by default (a soft clearing at the feet, a short wake). */
const float FOOT = 0.9f; /* an actor's footprint: its drop shadow's radius x this (Ashley: 48 -> 43 units) */
const float FOOT_DEFAULT = 44; /* world units, when the game gives no radius */
const float HOLE = 0.2f; /* the density an actor standing still settles its footprint to (strength 1) */
const float RING_SHARE = 0.6f; /* the share of the displaced fog piled around the hole */
const float PILE = 1.5f; /* the most a cell piles to */
const float WAKE = 0.25f; /* how much of an actor's velocity the fog takes (trail 1; a little: the cleared trail stays behind) */
const float DAMP = 1.8f; /* per second: the fog's motion dies out (trail 1) */
const float DIFFUSE = 0.6f; /* per second */
const float REACH_Y = 160; /* above and below the volume a source still touches it */
const float REST = 0.01f; /* a field within this of rest (1 %: invisible; its motion under REST_SPEED) sleeps */
const float REST_SPEED = 0.02f; /* cells per second */
const unsigned FORGET = 600; /* vsyncs: a volume not stepped for this long is forgotten; a longer gap restarts it */

/* One disturbance this vsync: an actor or the ghost (velocity from its
 * moves), in world units. */
struct Body {
    int source;
    float pos[3], vel[2], radius;
};
/* An actor slot's motion (the game moves actors on game frames, 1-4 vsyncs
 * apart: the velocity is the last move over the time it took). */
struct Track {
    float last[3] = { 0, 0, 0 }, velocity[2] = { 0, 0 };
    unsigned last_moved = 0;
    bool seen = false;
};
struct Live {
    Event e;
    unsigned start;
};

struct Sim {
    std::vector<float> d, u, v, scratch; /* density multiplier; velocity in cells per second */
    unsigned vsync = 0; /* the last vsync it stepped to */
    bool started = false, awake = false;
    Stats stats;
    void rest()
    {
        d.assign(N * N, 1.0f), u.assign(N * N, 0.0f), v.assign(N * N, 0.0f), scratch.assign(N * N, 0.0f);
        awake = false;
    }
};

struct World {
    std::map<std::string, Sim> sims;
    Track tracks[SLOTS + 1]; /* + the ghost */
    std::vector<Body> bodies; /* this vsync's */
    std::vector<Live> events;
    std::vector<Event> pending; /* emitted since the last vsync */
    bool ghost_on = false;
    float ghost_pos[3] = { 0, 0, 0 };
    unsigned vsync = 0;
    unsigned offset = 0; /* preview ticks while the game was paused (clock) */
    unsigned attacks = 0, spells = 0; /* the game's events seen */
    int last_phase = 0, last_chain = -1, last_type = 0; /* the battle action last vsync */
    bool events_seen = false;
} world;

float sample(const std::vector<float>& f, float x, float z, float outside)
{
    /* cell centres at i + 0.5; outside the grid: the rest value */
    x -= 0.5f, z -= 0.5f;
    if (x < -1 || z < -1 || x > N || z > N)
        return outside;
    int i = (int)floorf(x), j = (int)floorf(z);
    float fx = x - i, fz = z - j;
    auto at = [&](int a, int b) { return a < 0 || b < 0 || a >= N || b >= N ? outside : f[b * N + a]; };
    return (at(i, j) * (1 - fx) + at(i + 1, j) * fx) * (1 - fz) + (at(i, j + 1) * (1 - fx) + at(i + 1, j + 1) * fx) * fz;
}
/* World -> the volume's unit frame (medium.glsl medium_volume), then cells. */
void to_cells(const Frame& f, float wx, float wz, float* cx, float* cz)
{
    float rx = wx - f.pos[0], rz = wz - f.pos[2];
    float lx = (f.yaw_cos * rx - f.yaw_sin * rz) / f.half[0], lz = (f.yaw_sin * rx + f.yaw_cos * rz) / f.half[2];
    *cx = (lx * 0.5f + 0.5f) * N, *cz = (lz * 0.5f + 0.5f) * N;
}
void to_cell_velocity(const Frame& f, float vx, float vz, float* cu, float* cv)
{
    *cu = (f.yaw_cos * vx - f.yaw_sin * vz) / f.half[0] * 0.5f * N;
    *cv = (f.yaw_sin * vx + f.yaw_cos * vz) / f.half[2] * 0.5f * N;
}
float cells_per_unit(const Frame& f) { return N / (2.0f * fmaxf(f.half[0], f.half[2])); }
/* Whether a world point within `radius` (world units) can touch the
 * volume's footprint. */
bool touches(const Frame& f, const float* p, float radius, float* cx, float* cz, float* rc)
{
    if (fabsf(p[1] - f.pos[1]) > f.half[1] + REACH_Y)
        return false;
    to_cells(f, p[0], p[2], cx, cz);
    float cell_x = f.half[0] * 2 / N, cell_z = f.half[2] * 2 / N;
    *rc = radius / fminf(cell_x, cell_z);
    float m = 2.4f * *rc + 1;
    return !(*cx < -m || *cz < -m || *cx > N + m || *cz > N + m);
}

/* An actor's footprint: clears toward the hole floor (faster when moving),
 * takes its motion (the wake) and some outward flow, piles a ring. */
void splat_body(Sim& s, const Frame& f, float dt, float cx, float cz, float radius, float au, float av, float clear,
    bool player)
{
    float speed = sqrtf(au * au + av * av);
    float floor_ = fmaxf(1.0f - clear * (1.0f - HOLE), 0.0f);
    float rate = 1.0f - expf(-dt * (6.0f + speed * 0.4f) * fminf(clear, 1.0f)); /* walking clears faster */
    float wake = WAKE * fmaxf(f.trail, 0.0f);
    float removed = 0, ring_weight = 0;
    int r = (int)ceilf(radius * 2.4f);
    int i0 = (int)floorf(cx), j0 = (int)floorf(cz);
    if (player)
        s.stats.at_player = sample(s.d, cx, cz, 1.0f);
    for (int j = j0 - r; j <= j0 + r; j++)
        for (int i = i0 - r; i <= i0 + r; i++) {
            if (i < 0 || j < 0 || i >= N || j >= N)
                continue;
            float dx = i + 0.5f - cx, dz = j + 0.5f - cz, dist = sqrtf(dx * dx + dz * dz) / radius;
            int k = j * N + i;
            float w = expf(-dist * dist * 2.0f);
            if (w > 0.01f) {
                float drop = (s.d[k] - floor_) * w * rate;
                if (drop > 0)
                    s.d[k] -= drop, removed += drop;
                float push = w * fminf(1.0f, dt * 8.0f) * fminf(clear, 1.0f);
                float out = dist > 1e-3f ? (0.6f + speed * 0.05f) / dist : 0;
                float tu = 0, tv = 0;
                if (f.swirl > 0 && speed > 0.5f && dist > 1e-3f) {
                    /* a pair of counter-rotating eddies shed at its sides: the wake curls */
                    float side = au * dz - av * dx > 0 ? 1.0f : -1.0f, ring = expf(-(dist - 1.1f) * (dist - 1.1f) * 3.0f);
                    tu = -dz / dist * side * speed * f.swirl * 0.3f * ring;
                    tv = dx / dist * side * speed * f.swirl * 0.3f * ring;
                }
                s.u[k] += (au * wake + dx * out * 0.5f + tu - s.u[k]) * push;
                s.v[k] += (av * wake + dz * out * 0.5f + tv - s.v[k]) * push;
            }
            if (dist > 1.0f && dist < 2.4f)
                ring_weight += 1.0f - fabsf(dist - 1.6f) / 0.8f;
        }
    if (removed > 0 && ring_weight > 0)
        for (int j = j0 - r; j <= j0 + r; j++)
            for (int i = i0 - r; i <= i0 + r; i++) {
                if (i < 0 || j < 0 || i >= N || j >= N)
                    continue;
                float dx = i + 0.5f - cx, dz = j + 0.5f - cz, dist = sqrtf(dx * dx + dz * dz) / radius;
                if (dist > 1.0f && dist < 2.4f) {
                    int k = j * N + i;
                    s.d[k] = fminf(s.d[k] + removed * RING_SHARE * (1.0f - fabsf(dist - 1.6f) / 0.8f) / ring_weight, PILE);
                }
            }
}

/* A stamp, one vsync of its life: `rate` of its clearing, its push. */
void splat_event(Sim& s, const Frame& f, float dt, const Event& e, float weight, float cx, float cz, float radius)
{
    float k_clear = fminf(fmaxf(e.strength * weight * f.strength, 0.0f), 2.0f);
    if (k_clear <= 0)
        return;
    float floor_ = fmaxf(1.0f - k_clear * (1.0f - HOLE), 0.0f);
    float rate = 1.0f - expf(-dt * 10.0f * fminf(k_clear, 1.0f));
    /* the facing in the volume's frame (cells) */
    float fu, fv;
    to_cell_velocity(f, e.dir[0], e.dir[1], &fu, &fv);
    float fl = sqrtf(fu * fu + fv * fv);
    if (fl > 1e-6f)
        fu /= fl, fv /= fl;
    else
        fu = 0, fv = 1;
    float speed = radius * 3.0f * fminf(k_clear, 1.5f) * fmaxf(f.trail, 0.2f); /* cells per second at the rim */
    float removed = 0, ring_weight = 0;
    int r = (int)ceilf(radius * 2.0f);
    int i0 = (int)floorf(cx), j0 = (int)floorf(cz);
    for (int j = j0 - r; j <= j0 + r; j++)
        for (int i = i0 - r; i <= i0 + r; i++) {
            if (i < 0 || j < 0 || i >= N || j >= N)
                continue;
            float dx = i + 0.5f - cx, dz = j + 0.5f - cz, dl = sqrtf(dx * dx + dz * dz), dist = dl / radius;
            float nx = dl > 1e-3f ? dx / dl : 0, nz = dl > 1e-3f ? dz / dl : 0;
            int k = j * N + i;
            float clear = 0, pu = 0, pv = 0; /* how much it clears here (0..1), the velocity it sets */
            switch (e.shape) {
            case BURST: /* a disc cleared, blown outward */
                clear = expf(-dist * dist * 1.5f);
                pu = nx * speed * clear * 0.6f, pv = nz * speed * clear * 0.6f;
                break;
            case RING: { /* a thin ring pushed outward, the centre barely touched */
                float g = expf(-(dist - 0.8f) * (dist - 0.8f) * 12.0f);
                clear = g * 0.6f;
                pu = nx * speed * g * 0.5f, pv = nz * speed * g * 0.5f;
                break;
            }
            case SWING: { /* an arc in front (+-80 degrees), swirled around the swinger */
                float front = nx * fu + nz * fv;
                float arc = dl < 1e-3f ? 1.0f : fmaxf((front + 0.17f) / 1.17f, 0.0f);
                float g = expf(-(dist - 0.7f) * (dist - 0.7f) * 4.0f) * arc;
                clear = g * 0.8f;
                pu = (-nz * speed * 0.8f + nx * speed * 0.2f) * g, pv = (nx * speed * 0.8f + nz * speed * 0.2f) * g;
                break;
            }
            default: { /* VORTEX: a disc swirled in place */
                float g = expf(-dist * dist * 1.5f);
                clear = g * 0.5f;
                pu = -nz * speed * g * 0.6f, pv = nx * speed * g * 0.6f;
                break;
            }
            }
            if (clear > 0.01f) {
                float drop = (s.d[k] - floor_) * clear * rate;
                if (drop > 0)
                    s.d[k] -= drop, removed += drop;
                float push = fminf(1.0f, dt * 10.0f) * fminf(k_clear, 1.0f);
                s.u[k] += (pu - s.u[k]) * push * fminf(clear * 2, 1.0f);
                s.v[k] += (pv - s.v[k]) * push * fminf(clear * 2, 1.0f);
            }
            if (dist > 1.1f && dist < 2.0f)
                ring_weight += 1.0f - fabsf(dist - 1.5f) / 0.5f;
        }
    if (removed > 0 && ring_weight > 0)
        for (int j = j0 - r; j <= j0 + r; j++)
            for (int i = i0 - r; i <= i0 + r; i++) {
                if (i < 0 || j < 0 || i >= N || j >= N)
                    continue;
                float dx = i + 0.5f - cx, dz = j + 0.5f - cz, dist = sqrtf(dx * dx + dz * dz) / radius;
                if (dist > 1.1f && dist < 2.0f) {
                    int k = j * N + i;
                    s.d[k] = fminf(s.d[k] + removed * RING_SHARE * (1.0f - fabsf(dist - 1.5f) / 0.5f) / ring_weight, PILE);
                }
            }
}

/* Swirls (sim_swirl > 0): vorticity confinement puts back the small
 * eddies the coarse grid and the damping lose (a force along N x w, N the
 * unit gradient of |curl|), then the flow is made incompressible (a
 * pressure solve, Jacobi, and its gradient subtracted), so pushes turn
 * into curls and rolls instead of spreading out. */
void swirl(Sim& s, const Frame& f, float dt)
{
    std::vector<float>& w = s.scratch;
    auto U = [&](int i, int j) { return i < 0 || j < 0 || i >= N || j >= N ? 0.0f : s.u[j * N + i]; };
    auto V = [&](int i, int j) { return i < 0 || j < 0 || i >= N || j >= N ? 0.0f : s.v[j * N + i]; };
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
            w[j * N + i] = (V(i + 1, j) - V(i - 1, j) - U(i, j + 1) + U(i, j - 1)) * 0.5f;
    auto W = [&](int i, int j) { return fabsf(w[std::min(std::max(j, 0), N - 1) * N + std::min(std::max(i, 0), N - 1)]); };
    float eps = f.swirl * 1.0f;
    for (int j = 1; j < N - 1; j++)
        for (int i = 1; i < N - 1; i++) {
            float gx = (W(i + 1, j) - W(i - 1, j)) * 0.5f, gz = (W(i, j + 1) - W(i, j - 1)) * 0.5f;
            float l = sqrtf(gx * gx + gz * gz) + 1e-5f;
            /* only where the fog is disturbed: settled fog stops curling (and sleeps) */
            float vort = w[j * N + i] * fminf(fabsf(s.d[j * N + i] - 1.0f) * 6.0f, 1.0f);
            s.u[j * N + i] += eps * (gz / l) * vort * dt * 60.0f;
            s.v[j * N + i] -= eps * (gx / l) * vort * dt * 60.0f;
        }
    /* pressure: div u = lap p (open borders: p 0 outside) */
    /* padded by a cell of 0 all round: no bounds tests in the solve */
    const int M = N + 2;
    static std::vector<float> div, p, q;
    div.assign(M * M, 0.0f), p.assign(M * M, 0.0f), q.assign(M * M, 0.0f);
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
            div[(j + 1) * M + i + 1] = (U(i + 1, j) - U(i - 1, j) + V(i, j + 1) - V(i, j - 1)) * 0.5f;
    for (int it = 0; it < 16; it++) {
        for (int j = 1; j <= N; j++) {
            const float *a = &p[j * M], *up = &p[(j - 1) * M], *dn = &p[(j + 1) * M], *dv = &div[j * M];
            float* o = &q[j * M];
            for (int i = 1; i <= N; i++)
                o[i] = (a[i - 1] + a[i + 1] + up[i] + dn[i] - dv[i]) * 0.25f;
        }
        p.swap(q);
    }
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            const float* c = &p[(j + 1) * M + i + 1];
            s.u[j * N + i] -= (c[1] - c[-1]) * 0.5f;
            s.v[j * N + i] -= (c[M] - c[-M]) * 0.5f;
        }
}

/* One vsync of a volume: its sources, then advect, diffuse, damp, relax.
 * Returns whether anything touched it. */
bool advance(Sim& s, const Frame& f, float dt)
{
    s.stats.actors = 0;
    s.stats.at_player = -1;
    float cpu = cells_per_unit(f);
    (void)cpu;
    for (const Body& b : world.bodies) {
        float w = f.weight[b.source];
        if (w <= 0)
            continue;
        float radius = b.radius * fmaxf(f.footprint, 0.05f), cx, cz, rc;
        if (!touches(f, b.pos, radius, &cx, &cz, &rc))
            continue;
        s.stats.actors++;
        float au, av;
        to_cell_velocity(f, b.vel[0], b.vel[1], &au, &av);
        splat_body(s, f, dt, cx, cz, rc, au, av, fminf(fmaxf(f.strength * w, 0.0f), 2.0f), b.source == PLAYER);
    }
    for (const Live& l : world.events) {
        float w = f.weight[l.e.source];
        if (w <= 0)
            continue;
        float cx, cz, rc;
        if (!touches(f, l.e.pos, l.e.radius * fmaxf(f.footprint, 0.05f), &cx, &cz, &rc))
            continue;
        s.stats.actors++;
        splat_event(s, f, dt, l.e, w, cx, cz, rc);
    }
    bool touched = s.stats.actors > 0;
    if (f.swirl > 0)
        swirl(s, f, dt);
    /* advect the density and the velocity (semi-Lagrangian, with the wind) */
    float wu, wv;
    to_cell_velocity(f, f.wind[0], f.wind[2], &wu, &wv);
    /* the three fields at once, one back-trace per cell (outside the grid: rest) */
    static std::vector<float> nu, nv;
    nu.resize(N * N), nv.resize(N * N);
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            int k = j * N + i;
            float x = i - (s.u[k] + wu) * dt, z = j - (s.v[k] + wv) * dt;
            float d = 0, u = 0, v = 0, wsum_out = 0;
            if (x < -1 || z < -1 || x > N || z > N) {
                s.scratch[k] = 1, nu[k] = 0, nv[k] = 0;
                continue;
            }
            int i0 = (int)(x + 1) - 1, j0 = (int)(z + 1) - 1; /* floor (x, z > -1): no libm call */
            float fx = x - i0, fz = z - j0;
            const float wt[4] = { (1 - fx) * (1 - fz), fx * (1 - fz), (1 - fx) * fz, fx * fz };
            const int di[4] = { 0, 1, 0, 1 }, dj[4] = { 0, 0, 1, 1 };
            for (int t = 0; t < 4; t++) {
                int a = i0 + di[t], b = j0 + dj[t];
                if (a < 0 || b < 0 || a >= N || b >= N) {
                    wsum_out += wt[t];
                    continue;
                }
                int q = b * N + a;
                d += s.d[q] * wt[t], u += s.u[q] * wt[t], v += s.v[q] * wt[t];
            }
            s.scratch[k] = d + wsum_out, nu[k] = u, nv[k] = v;
        }
    s.d.swap(s.scratch);
    s.u.swap(nu), s.v.swap(nv);
    /* diffuse, damp, relax toward rest */
    float k_diffuse = fminf(DIFFUSE * (N / 32.0f) * (N / 32.0f) * dt, 0.25f), /* the same in world units at any grid */ damp = expf(-DAMP / fmaxf(f.trail, 0.25f) * dt);
    float relax = 1.0f - expf(-dt / fmaxf(f.recover, 0.1f));
    float deviation = 0, motion = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            int k = j * N + i;
            float l = i > 0 ? s.d[k - 1] : 1.0f, r_ = i < N - 1 ? s.d[k + 1] : 1.0f;
            float t = j > 0 ? s.d[k - N] : 1.0f, b = j < N - 1 ? s.d[k + N] : 1.0f;
            float value = s.d[k] + k_diffuse * ((l + r_ + t + b) * 0.25f - s.d[k]);
            value += (1.0f - value) * relax;
            s.scratch[k] = value;
            s.u[k] *= damp, s.v[k] *= damp;
            if (fabsf(s.u[k]) < 1e-5f) /* no denormals: x87 (32-bit builds) slows down 20-fold on them */
                s.u[k] = 0;
            if (fabsf(s.v[k]) < 1e-5f)
                s.v[k] = 0;
            deviation = fmaxf(deviation, fabsf(value - 1.0f));
            motion = fmaxf(motion, fabsf(s.u[k]) + fabsf(s.v[k]));
        }
    s.d.swap(s.scratch);
    s.awake = touched || deviation > REST || motion > REST_SPEED;
    if (!s.awake) { /* settled: exactly at rest, and it sleeps */
        std::fill(s.d.begin(), s.d.end(), 1.0f);
        std::fill(s.u.begin(), s.u.end(), 0.0f);
        std::fill(s.v.begin(), s.v.end(), 0.0f);
    }
    s.stats.steps++;
    return true;
}

/* Whether any source could touch the volume this vsync. */
bool any_source(const Frame& f)
{
    float cx, cz, rc;
    for (const Body& b : world.bodies)
        if (f.weight[b.source] > 0 && touches(f, b.pos, b.radius * fmaxf(f.footprint, 0.05f), &cx, &cz, &rc))
            return true;
    for (const Live& l : world.events)
        if (f.weight[l.e.source] > 0 && touches(f, l.e.pos, l.e.radius * fmaxf(f.footprint, 0.05f), &cx, &cz, &rc))
            return true;
    return false;
}

void track(Track& t, const float* p, unsigned vsync)
{
    if (!t.seen) {
        memcpy(t.last, p, sizeof t.last);
        t.last_moved = vsync, t.seen = true;
        t.velocity[0] = t.velocity[1] = 0;
    } else if (p[0] != t.last[0] || p[2] != t.last[2]) {
        float dt = (vsync - t.last_moved) / 59.94f;
        if (dt > 0 && dt < 0.5f) {
            t.velocity[0] = (p[0] - t.last[0]) / dt;
            t.velocity[1] = (p[2] - t.last[2]) / dt;
        } else
            t.velocity[0] = t.velocity[1] = 0; /* a jump (a warp, a cut): no wake */
        memcpy(t.last, p, sizeof t.last);
        t.last_moved = vsync;
    } else if (vsync - t.last_moved > 6) {
        t.velocity[0] = t.velocity[1] = 0; /* standing */
    }
}
} // namespace

namespace rood_fog_sim {

const char* shape_name(int shape)
{
    static const char* names[SHAPES] = { "burst", "ring", "swing", "vortex" };
    return shape >= 0 && shape < SHAPES ? names[shape] : "burst";
}
int shape_from_name(const char* name)
{
    for (int i = 0; i < SHAPES; i++)
        if (!strcmp(name, shape_name(i)))
            return i;
    return -1;
}

unsigned clock(unsigned game_vsync) { return game_vsync + world.offset; }
void preview_tick() { world.offset++; }

void begin_vsync(unsigned vsync)
{
    world.vsync = vsync;
    world.bodies.clear();
    /* the characters (battle builds) */
    pc_dev_actor actors[SLOTS];
    int count = pc_dev_actors ? pc_dev_actors(actors, SLOTS) : 0;
    bool present[SLOTS] = {};
    int body_of[SLOTS], actor_of[SLOTS];
    for (int a = 0; a < count; a++) {
        const pc_dev_actor& x = actors[a];
        if (x.slot < 0 || x.slot >= SLOTS || present[x.slot])
            continue;
        present[x.slot] = true;
        body_of[x.slot] = (int)world.bodies.size(), actor_of[x.slot] = a;
        Track& t = world.tracks[x.slot];
        track(t, x.pos, vsync);
        Body b;
        b.source = x.slot == 0 ? PLAYER : rood_fog_sim::ACTORS;
        memcpy(b.pos, x.pos, sizeof b.pos);
        b.vel[0] = t.velocity[0], b.vel[1] = t.velocity[1];
        b.radius = x.radius > 4 && x.radius < 512 ? x.radius * FOOT : FOOT_DEFAULT;
        world.bodies.push_back(b);
    }
    for (int a = 0; a < SLOTS; a++)
        if (!present[a])
            world.tracks[a].seen = false;
    /* The game's events (one battle action at a time, any actor): a swing or
     * an effect's release starts (phase 7: once per swing and chain step), an
     * effect action starts casting (phase 5). Plain attacks, abilities and
     * break arts are attacks; spells, items and traps are spells. */
    pc_dev_action act;
    int phase = 0, chain = -1;
    if (pc_dev_battle_action && pc_dev_battle_action(&act) && act.actor >= 0 && act.actor < SLOTS
        && present[act.actor]) {
        phase = act.phase, chain = act.chain;
        const Body* who = &world.bodies[body_of[act.actor]];
        bool spell = act.type == 1 || act.type == 5 || act.type == 7;
        bool starts = (phase == 7 && (world.last_phase != 7 || chain != world.last_chain))
            || (phase == 5 && world.last_phase != 5);
        if (who && starts && world.events_seen) {
            Event e;
            e.source = spell ? SPELLS : ATTACKS;
            float dx = act.aim[0] - who->pos[0], dz = act.aim[2] - who->pos[2], l = sqrtf(dx * dx + dz * dz);
            if (l > 1) {
                e.dir[0] = dx / l, e.dir[1] = dz / l;
            } else { /* the facing: (dx, dz) = (-sin, -cos) of the game's angle */
                const pc_dev_actor& x = actors[actor_of[act.actor]];
                e.dir[0] = -sinf(x.facing * (6.2831853f / 4096)), e.dir[1] = -cosf(x.facing * (6.2831853f / 4096));
            }
            if (phase == 5) { /* casting: a ring around the caster */
                memcpy(e.pos, who->pos, sizeof e.pos);
                e.shape = RING, e.radius = who->radius * 2.5f, e.strength = 0.6f, e.frames = 20;
            } else if (spell || !act.plain) { /* a spell's or an art's release: a burst where it lands */
                if (l > 1 && l < 4000)
                    memcpy(e.pos, act.aim, sizeof e.pos);
                else
                    memcpy(e.pos, who->pos, sizeof e.pos);
                e.shape = BURST, e.radius = 130, e.strength = 1, e.frames = 18;
            } else { /* a swing: an arc in front of the swinger */
                memcpy(e.pos, who->pos, sizeof e.pos);
                e.shape = SWING, e.radius = who->radius * 2.2f, e.strength = 1, e.frames = 12;
            }
            emit(e);
            (spell ? world.spells : world.attacks)++;
        }
    }
    world.last_phase = phase, world.last_chain = chain, world.last_type = phase ? act.type : 0;
    world.events_seen = true;
    if (world.ghost_on) {
        Track& t = world.tracks[SLOTS];
        track(t, world.ghost_pos, vsync);
        Body b;
        b.source = GHOST;
        memcpy(b.pos, world.ghost_pos, sizeof b.pos);
        b.vel[0] = t.velocity[0], b.vel[1] = t.velocity[1];
        b.radius = FOOT_DEFAULT;
        world.bodies.push_back(b);
    } else
        world.tracks[SLOTS].seen = false;
    for (const Event& e : world.pending)
        world.events.push_back(Live { e, vsync });
    world.pending.clear();
}

bool step(const char* id, const Frame& frame, unsigned vsync)
{
    Sim& s = world.sims[id];
    if (s.d.empty())
        s.rest();
    if (!s.started || vsync < s.vsync || vsync - s.vsync > FORGET) {
        s.rest();
        s.vsync = vsync, s.started = true;
    }
    unsigned steps = vsync - s.vsync;
    s.vsync = vsync;
    if (!steps)
        return false;
    if (!s.awake && !any_source(frame))
        return false; /* at rest, nothing near: idle */
    s.awake = true;
    /* one step per vsync since its last (normally one) */
    if (steps > 8)
        steps = 8;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (unsigned k = 0; k < steps; k++)
        advance(s, frame, VSYNC_DT);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    s.stats.ms = (float)((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6);
    return true;
}

void end_vsync(unsigned vsync)
{
    for (size_t i = 0; i < world.events.size();)
        if (vsync - world.events[i].start + 1 >= (unsigned)world.events[i].e.frames)
            world.events.erase(world.events.begin() + i);
        else
            i++;
    for (auto it = world.sims.begin(); it != world.sims.end();)
        if (vsync - it->second.vsync > FORGET)
            it = world.sims.erase(it);
        else
            ++it;
}

const float* field(const char* id)
{
    auto it = world.sims.find(id);
    return it != world.sims.end() && !it->second.d.empty() ? it->second.d.data() : nullptr;
}
void velocity_world(const char* id, const Frame& f, float* out)
{
    auto it = world.sims.find(id);
    if (it == world.sims.end() || it->second.u.empty())
        return;
    const Sim& s = it->second;
    /* cells per second in the volume's frame -> its unit frame -> world (the inverse turn) */
    float sx = f.half[0] * 2 / N, sz = f.half[2] * 2 / N;
    for (int k = 0; k < N * N; k++) {
        float lx = s.u[k] * sx, lz = s.v[k] * sz;
        out[k * 2] = f.yaw_cos * lx + f.yaw_sin * lz;
        out[k * 2 + 1] = -f.yaw_sin * lx + f.yaw_cos * lz;
    }
}
void emit(const Event& e) { world.pending.push_back(e); }
void ghost(bool on, const float pos[3])
{
    world.ghost_on = on;
    if (on)
        memcpy(world.ghost_pos, pos, sizeof world.ghost_pos);
}
void reset()
{
    for (auto& kv : world.sims)
        kv.second.rest();
    world.events.clear();
    world.pending.clear();
}
bool stats(const char* id, Stats* out)
{
    auto it = world.sims.find(id);
    if (it == world.sims.end() || it->second.d.empty())
        return false;
    *out = it->second.stats;
    float lo = 1, sum = 0;
    for (float x : it->second.d)
        lo = fminf(lo, x), sum += x;
    out->least = lo, out->mean = sum / it->second.d.size();
    out->awake = it->second.awake;
    return true;
}
void event_counts(unsigned* attacks, unsigned* spells) { *attacks = world.attacks, *spells = world.spells; }
void sources(int* bodies, int* phase, int* type)
{
    *bodies = (int)world.bodies.size(), *phase = world.last_phase, *type = world.last_type;
}

/* ---- save states: a small versioned byte stream ---- */
namespace {
const unsigned STATE_VERSION = 3;
struct Writer {
    std::string out;
    template <class T> void put(const T& v) { out.append((const char*)&v, sizeof v); }
    void floats(const std::vector<float>& v)
    {
        put((unsigned)v.size());
        out.append((const char*)v.data(), v.size() * sizeof(float));
    }
    void str(const std::string& s)
    {
        put((unsigned)s.size());
        out += s;
    }
};
struct Reader {
    const char* p;
    const char* end;
    bool ok = true;
    template <class T> void get(T& v)
    {
        if (end - p < (long)sizeof v) {
            ok = false;
            return;
        }
        memcpy(&v, p, sizeof v);
        p += sizeof v;
    }
    void floats(std::vector<float>& v)
    {
        unsigned n = 0;
        get(n);
        if (!ok || n > (unsigned)(N * N) || (size_t)(end - p) < n * sizeof(float)) {
            ok = false;
            return;
        }
        v.assign((const float*)p, (const float*)p + n);
        p += n * sizeof(float);
    }
    void str(std::string& s)
    {
        unsigned n = 0;
        get(n);
        if (!ok || n > 256 || (size_t)(end - p) < n) {
            ok = false;
            return;
        }
        s.assign(p, n);
        p += n;
    }
};
} // namespace

std::string state_save()
{
    Writer w;
    w.put(STATE_VERSION);
    w.put(world.vsync);
    w.put(world.offset);
    w.put(world.attacks), w.put(world.spells);
    w.put(world.events_seen);
    w.put(world.ghost_on);
    w.put(world.ghost_pos);
    w.put(world.last_phase), w.put(world.last_chain);
    for (const Track& t : world.tracks)
        w.put(t);
    w.put((unsigned)world.events.size());
    for (const Live& l : world.events)
        w.put(l);
    w.put((unsigned)world.pending.size());
    for (const Event& e : world.pending)
        w.put(e);
    w.put((unsigned)world.sims.size());
    for (const auto& kv : world.sims) {
        const Sim& s = kv.second;
        w.str(kv.first);
        w.put(s.vsync), w.put(s.started), w.put(s.awake), w.put(s.stats);
        w.floats(s.d), w.floats(s.u), w.floats(s.v);
    }
    return w.out;
}

bool state_load(const void* data, size_t size)
{
    Reader r { (const char*)data, (const char*)data + size };
    unsigned version = 0;
    r.get(version);
    if (!r.ok || version != STATE_VERSION)
        return false;
    World x;
    r.get(x.vsync);
    r.get(x.offset);
    r.get(x.attacks), r.get(x.spells);
    r.get(x.events_seen);
    r.get(x.ghost_on);
    r.get(x.ghost_pos);
    r.get(x.last_phase), r.get(x.last_chain);
    for (Track& t : x.tracks)
        r.get(t);
    unsigned n = 0;
    r.get(n);
    for (unsigned i = 0; r.ok && i < n && i < 4096; i++) {
        Live l;
        r.get(l);
        x.events.push_back(l);
    }
    r.get(n);
    for (unsigned i = 0; r.ok && i < n && i < 4096; i++) {
        Event e;
        r.get(e);
        x.pending.push_back(e);
    }
    r.get(n);
    for (unsigned i = 0; r.ok && i < n && i < 64; i++) {
        std::string id;
        r.str(id);
        Sim s;
        r.get(s.vsync), r.get(s.started), r.get(s.awake), r.get(s.stats);
        r.floats(s.d), r.floats(s.u), r.floats(s.v);
        if (r.ok && (s.d.size() != (size_t)(N * N) || s.u.size() != s.d.size() || s.v.size() != s.d.size()))
            r.ok = false;
        s.scratch.assign(N * N, 0.0f);
        x.sims[id] = s;
    }
    if (!r.ok || r.p != r.end)
        return false;
    world = x;
    return true;
}

} // namespace rood_fog_sim

/* snapshot.c: the interactive fog's state in save states. */
extern "C" {
int pc_fog_state_size(void) { return (int)rood_fog_sim::state_save().size(); }
void pc_fog_state_save(void* out)
{
    std::string s = rood_fog_sim::state_save();
    memcpy(out, s.data(), s.size());
}
int pc_fog_state_load(const void* in, int size) { return rood_fog_sim::state_load(in, (size_t)size); }
}
