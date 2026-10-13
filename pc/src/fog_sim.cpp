// Interactive fog prototype (fog_sim.h): one fog volume's top-down density
// field, pushed by the room's characters. Off unless a volume has sim=1.
#include "fog_sim.h"

#include <math.h>
#include <string.h>
#include <string>
#include <vector>

#include "dev_state.h"

namespace {
const int N = PC_FOG_SIM_GRID;
const int ACTORS = 17;
/* Tuning: subtle by default (a soft clearing at the feet, a short wake). */
const float RADIUS = 44; /* world units: an actor's footprint */
const float HOLE = 0.2f; /* the density an actor standing still settles its footprint to (strength 1) */
const float RING = 0.6f; /* the share of the displaced fog piled around the hole */
const float PILE = 1.5f; /* the most a cell piles to */
const float WAKE = 0.25f; /* how much of an actor's velocity the fog takes (a little: the cleared trail stays behind) */
const float DAMP = 1.8f; /* per second: the fog's motion dies out */
const float DIFFUSE = 0.6f; /* per second */
const float REACH_Y = 160; /* above and below the volume an actor still touches it */

struct Sim {
    std::string id;
    std::vector<float> d, u, v, scratch; /* density multiplier; velocity in cells per second */
    float last[ACTORS][3];
    unsigned last_moved[ACTORS];
    float velocity[ACTORS][2]; /* world units per second, x and z */
    bool seen[ACTORS];
    unsigned vsync = 0;
    bool started = false;
    float at_player = -1;
    int actors = 0;
    void rest()
    {
        d.assign(N * N, 1.0f), u.assign(N * N, 0.0f), v.assign(N * N, 0.0f), scratch.assign(N * N, 0.0f);
        memset(seen, 0, sizeof seen);
        memset(velocity, 0, sizeof velocity);
        started = false;
    }
};
Sim sim;

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
void to_cells(const rood_fog_sim::Frame& f, float wx, float wz, float* cx, float* cz)
{
    float rx = wx - f.pos[0], rz = wz - f.pos[2];
    float lx = (f.yaw_cos * rx - f.yaw_sin * rz) / f.half[0], lz = (f.yaw_sin * rx + f.yaw_cos * rz) / f.half[2];
    *cx = (lx * 0.5f + 0.5f) * N, *cz = (lz * 0.5f + 0.5f) * N;
}
void to_cell_velocity(const rood_fog_sim::Frame& f, float vx, float vz, float* cu, float* cv)
{
    *cu = (f.yaw_cos * vx - f.yaw_sin * vz) / f.half[0] * 0.5f * N;
    *cv = (f.yaw_sin * vx + f.yaw_cos * vz) / f.half[2] * 0.5f * N;
}

void advance(const rood_fog_sim::Frame& f, float dt, const float (*pos)[3], int count)
{
    /* the characters: velocity from their last move (they move on game frames, not every vsync) */
    sim.actors = 0;
    sim.at_player = -1;
    float wu, wv;
    to_cell_velocity(f, f.wind[0], f.wind[2], &wu, &wv);
    float cell_x = f.half[0] * 2 / N, cell_z = f.half[2] * 2 / N;
    float radius = RADIUS / fminf(cell_x, cell_z); /* in cells */
    for (int a = 0; a < count && a < ACTORS; a++) {
        const float* p = pos[a];
        if (!sim.seen[a]) {
            memcpy(sim.last[a], p, sizeof sim.last[a]);
            sim.last_moved[a] = sim.vsync, sim.seen[a] = true;
        } else if (p[0] != sim.last[a][0] || p[2] != sim.last[a][2]) {
            float t = (sim.vsync - sim.last_moved[a]) / 59.94f;
            if (t > 0 && t < 0.5f) {
                sim.velocity[a][0] = (p[0] - sim.last[a][0]) / t;
                sim.velocity[a][1] = (p[2] - sim.last[a][2]) / t;
            }
            memcpy(sim.last[a], p, sizeof sim.last[a]);
            sim.last_moved[a] = sim.vsync;
        } else if (sim.vsync - sim.last_moved[a] > 6) {
            sim.velocity[a][0] = sim.velocity[a][1] = 0; /* standing */
        }
        if (fabsf(p[1] - f.pos[1]) > f.half[1] + REACH_Y)
            continue;
        float cx, cz;
        to_cells(f, p[0], p[2], &cx, &cz);
        if (cx < -2 * radius || cz < -2 * radius || cx > N + 2 * radius || cz > N + 2 * radius)
            continue;
        sim.actors++;
        if (a == 0)
            sim.at_player = sample(sim.d, cx, cz, 1.0f);
        float au, av;
        to_cell_velocity(f, sim.velocity[a][0], sim.velocity[a][1], &au, &av);
        float speed = sqrtf(au * au + av * av);
        float clear = fminf(fmaxf(f.strength, 0.0f), 2.0f);
        float floor_ = fmaxf(1.0f - clear * (1.0f - HOLE), 0.0f);
        float rate = 1.0f - expf(-dt * (6.0f + speed * 0.4f)); /* walking clears faster */
        float removed = 0, ring_weight = 0;
        int r = (int)ceilf(radius * 2.4f);
        int i0 = (int)floorf(cx), j0 = (int)floorf(cz);
        for (int j = j0 - r; j <= j0 + r; j++)
            for (int i = i0 - r; i <= i0 + r; i++) {
                if (i < 0 || j < 0 || i >= N || j >= N)
                    continue;
                float dx = i + 0.5f - cx, dz = j + 0.5f - cz, dist = sqrtf(dx * dx + dz * dz) / radius;
                int k = j * N + i;
                float w = expf(-dist * dist * 2.0f);
                if (w > 0.01f) {
                    float drop = (sim.d[k] - floor_) * w * rate;
                    if (drop > 0)
                        sim.d[k] -= drop, removed += drop;
                    /* the wake: the fog takes the actor's motion, and flows out of its footprint */
                    float push = w * fminf(1.0f, dt * 8.0f);
                    float out = dist > 1e-3f ? (0.6f + speed * 0.05f) / dist : 0;
                    sim.u[k] += (au * WAKE + dx * out * 0.5f - sim.u[k]) * push;
                    sim.v[k] += (av * WAKE + dz * out * 0.5f - sim.v[k]) * push;
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
                        sim.d[k] = fminf(sim.d[k] + removed * RING * (1.0f - fabsf(dist - 1.6f) / 0.8f) / ring_weight, PILE);
                    }
                }
    }
    /* advect the density and the velocity (semi-Lagrangian, with the wind) */
    for (std::vector<float>* field : { &sim.d, &sim.u, &sim.v }) {
        float rest = field == &sim.d ? 1.0f : 0.0f;
        for (int j = 0; j < N; j++)
            for (int i = 0; i < N; i++) {
                int k = j * N + i;
                float x = i + 0.5f - (sim.u[k] + wu) * dt, z = j + 0.5f - (sim.v[k] + wv) * dt;
                sim.scratch[k] = sample(*field, x, z, rest);
            }
        field->swap(sim.scratch);
    }
    /* diffuse, damp, relax toward rest */
    float k_diffuse = fminf(DIFFUSE * dt, 0.25f), damp = expf(-DAMP * dt);
    float relax = 1.0f - expf(-dt / fmaxf(f.recover, 0.1f));
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            int k = j * N + i;
            float l = i > 0 ? sim.d[k - 1] : 1.0f, r_ = i < N - 1 ? sim.d[k + 1] : 1.0f;
            float t = j > 0 ? sim.d[k - N] : 1.0f, b = j < N - 1 ? sim.d[k + N] : 1.0f;
            float value = sim.d[k] + k_diffuse * ((l + r_ + t + b) * 0.25f - sim.d[k]);
            sim.scratch[k] = value + (1.0f - value) * relax;
            sim.u[k] *= damp, sim.v[k] *= damp;
        }
    sim.d.swap(sim.scratch);
}
} // namespace

namespace rood_fog_sim {
void step(const char* id, const Frame& frame, unsigned vsync)
{
    if (sim.id != id || sim.d.empty()) {
        sim.id = id;
        sim.rest();
    }
    if (!sim.started || vsync < sim.vsync || vsync - sim.vsync > 600) {
        sim.vsync = vsync, sim.started = true;
        return;
    }
    float pos[ACTORS][3];
    int count = pc_dev_actor_positions ? pc_dev_actor_positions(pos, ACTORS) : 0;
    /* one step per vsync (60 Hz, deterministic), at most 8 per call */
    unsigned steps = vsync - sim.vsync;
    if (steps > 8)
        sim.vsync = vsync - 8, steps = 8;
    for (unsigned s = 0; s < steps; s++) {
        sim.vsync++;
        advance(frame, 1.0f / 59.94f, pos, count);
    }
}
const float* field(const char* id) { return !sim.d.empty() && sim.id == id ? sim.d.data() : nullptr; }
void reset() { sim.rest(); }
void stats(float* least, float* mean, float* at_player, int* actors)
{
    float lo = 1, sum = 0;
    for (float x : sim.d)
        lo = fminf(lo, x), sum += x;
    *least = sim.d.empty() ? 1 : lo;
    *mean = sim.d.empty() ? 1 : sum / sim.d.size();
    *at_player = sim.at_player;
    *actors = sim.actors;
}
} // namespace rood_fog_sim
