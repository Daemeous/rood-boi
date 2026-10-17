// The particle system without the game (pc/docs/Particles.md): the mote's
// closed form (pc/src/particle_math.h: spawn shapes, drift, gravity and
// drag, curl-noise turbulence, fade, pixel size, cycles), projection and the
// key image's packing, the CPU light shape (light_shape.h pc_light_shape),
// and later the emitter assets (particle_assets.cpp).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "light_prefabs.h"
#include "light_shape.h"
#include "particle_assets.h"
#include "particle_math.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool close_to(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }

/* A box emitter at (100, -50, 200), 200 x 100 x 300, no motion, 10 s lives. */
static pc_emitter_gpu still_box()
{
    pc_emitter_gpu e;
    memset(&e, 0, sizeof e);
    e.v[0][0] = 100, e.v[0][1] = -50, e.v[0][2] = 200, e.v[0][3] = 0;
    e.v[1][0] = 200, e.v[1][1] = 100, e.v[1][2] = 300, e.v[1][3] = PC_SPAWN_BOX;
    e.v[4][0] = 10, e.v[4][1] = 0.5f;
    e.v[5][0] = e.v[5][1] = e.v[5][2] = 1, e.v[5][3] = 1;
    e.v[6][0] = 1, e.v[6][1] = 2, e.v[6][2] = 0.5f;
    e.v[7][0] = 7, e.v[7][3] = 64;
    return e;
}

static void spawn_and_cycles()
{
    pc_emitter_gpu e = still_box();
    pc_mote a, b;
    bool inside = true, sizes = true, same = true;
    int ones = 0;
    for (int i = 0; i < 64; i++) {
        pc_particle_mote(&e, i, 3.25f, nullptr, nullptr, 0, &a);
        pc_particle_mote(&e, i, 3.25f, nullptr, nullptr, 0, &b);
        same &= !memcmp(&a, &b, sizeof a);
        inside &= fabsf(a.pos[0] - 100) <= 100.01f && fabsf(a.pos[1] + 50) <= 50.01f && fabsf(a.pos[2] - 200) <= 150.01f;
        sizes &= a.size >= 1 && a.size <= 2 && a.age >= 0 && a.age < a.life && a.life <= 10 && a.life >= 5;
        ones += a.size == 1;
    }
    check(same, "a mote is a pure function of (emitter, index, time)");
    check(inside, "a still box emitter's motes are in its box");
    check(sizes && ones > 10 && ones < 54, "sizes 1..2 game pixels (both), ages within lives of 5..10 s");
    pc_particle_mote(&e, 5, 3.25f, nullptr, nullptr, 0, &a);
    pc_particle_mote(&e, 5, 3.25f + a.life, nullptr, nullptr, 0, &b);
    check(b.cycle == a.cycle + 1 && close_to(b.age, a.age, 1e-2f) && fabsf(b.pos[0] - a.pos[0]) + fabsf(b.pos[2] - a.pos[2]) > 1e-3f,
        "one life later: the next cycle, the same age, a new spawn point");
    pc_emitter_gpu turned = e;
    turned.v[0][3] = 3.14159265f * 0.5f; /* yaw 90: the box's x runs along -z */
    turned.v[1][0] = 200, turned.v[1][2] = 2; /* a thin slab along its x */
    bool along_z = true;
    for (int i = 0; i < 32; i++) {
        pc_particle_mote(&turned, i, 1, nullptr, nullptr, 0, &a);
        along_z &= fabsf(a.pos[0] - 100) <= 1.01f && fabsf(a.pos[2] - 200) <= 100.01f;
    }
    check(along_z, "yaw turns the box (atmosphere.cpp's corner(): x' = c x + s z, z' = -s x + c z)");
    pc_emitter_gpu disc = e;
    disc.v[1][3] = PC_SPAWN_DISC, disc.v[1][0] = 50;
    pc_emitter_gpu point = e;
    point.v[1][3] = PC_SPAWN_POINT, point.v[1][0] = 30;
    bool on_disc = true, in_ball = true;
    for (int i = 0; i < 64; i++) {
        pc_particle_mote(&disc, i, 1, nullptr, nullptr, 0, &a);
        on_disc &= close_to(a.pos[1], -50) && hypotf(a.pos[0] - 100, a.pos[2] - 200) <= 50.01f;
        pc_particle_mote(&point, i, 1, nullptr, nullptr, 0, &b);
        in_ball &= sqrtf((b.pos[0] - 100) * (b.pos[0] - 100) + (b.pos[1] + 50) * (b.pos[1] + 50)
                       + (b.pos[2] - 200) * (b.pos[2] - 200)) <= 30.01f;
    }
    check(on_disc, "a disc: a horizontal circle of its radius");
    check(in_ball, "a point: a ball of its radius");
    /* The floor under a surface emitter: a 2 x 2 grid sloping in x (y down: the floor at -10 .. -30). */
    float heights[4] = { -10, -30, -10, -30 }, info[4] = { 0, 100, 1.0f / 200, 1.0f / 200 };
    pc_emitter_gpu surface = e;
    surface.v[1][3] = PC_SPAWN_SURFACE;
    bool on_floor = true;
    for (int i = 0; i < 32; i++) {
        pc_particle_mote(&surface, i, 1, heights, info, 2, &a);
        float expect = pc_particle_floor(heights, info, 2, a.pos[0], a.pos[2]) - 1;
        on_floor &= close_to(a.pos[1], expect, 1e-3f) && a.pos[1] <= -11 + 1e-3f && a.pos[1] >= -31 - 1e-3f;
    }
    check(on_floor, "a surface emitter spawns just above the floor grid under it");
    pc_particle_mote(&surface, 3, 1, nullptr, nullptr, 0, &a);
    check(close_to(a.pos[1], -50 + 50 - 1), "without a floor grid: its box's bottom face (y down)");
}

/* The first mote of e at least 2 s old at t0 whose cycle is the same at t1 (no respawn between). */
static int steady(const pc_emitter_gpu& e, float t0, float t1)
{
    for (int i = 0; i < 64; i++) {
        pc_mote a, b;
        pc_particle_mote(&e, i, t0, nullptr, nullptr, 0, &a);
        pc_particle_mote(&e, i, t1, nullptr, nullptr, 0, &b);
        if (a.cycle == b.cycle && a.age >= 2)
            return i;
    }
    return 0;
}

static void motion()
{
    pc_emitter_gpu e = still_box();
    e.v[4][0] = 20, e.v[4][1] = 0; /* 20 s lives (small ages: float precision) */
    e.v[2][0] = 3, e.v[2][1] = -1, e.v[2][2] = 2; /* drift */
    pc_mote a, b;
    int i = steady(e, 10, 12);
    pc_particle_mote(&e, i, 10, nullptr, nullptr, 0, &a);
    pc_particle_mote(&e, i, 12, nullptr, nullptr, 0, &b);
    check(b.cycle == a.cycle && close_to(b.pos[0] - a.pos[0], 6) && close_to(b.pos[1] - a.pos[1], -2)
            && close_to(b.pos[2] - a.pos[2], 4),
        "drift: its velocity times the time");
    pc_emitter_gpu fall = still_box();
    fall.v[4][0] = 20, fall.v[4][1] = 0, fall.v[2][3] = 2; /* gravity 2 u/s^2, no drag */
    i = steady(fall, 1, 4);
    pc_particle_mote(&fall, i, 1, nullptr, nullptr, 0, &a);
    pc_particle_mote(&fall, i, 4, nullptr, nullptr, 0, &b);
    check(close_to(b.pos[1] - a.pos[1], b.age * b.age - a.age * a.age, 1e-2f),
        "gravity without drag: 1/2 g age^2 (y down: it falls)");
    fall.v[4][0] = 1000, fall.v[3][3] = 4; /* drag: the fall's speed tends to g / drag */
    i = steady(fall, 600, 601);
    pc_mote c, d;
    pc_particle_mote(&fall, i, 600, nullptr, nullptr, 0, &c);
    pc_particle_mote(&fall, i, 601, nullptr, nullptr, 0, &d);
    check(c.age >= 2 && close_to(d.pos[1] - c.pos[1], 0.5f, 1e-2f), "with drag the fall settles at g / drag units a second");
    pc_emitter_gpu fade = still_box();
    fade.v[4][1] = 0, fade.v[4][2] = 2, fade.v[4][3] = 3;
    bool shaped = true;
    for (int k = 0; k < 64; k++) {
        pc_particle_mote(&fade, k, 7.5f, nullptr, nullptr, 0, &a);
        float in = fminf(a.age / 2, 1), out = fminf((a.life - a.age) / 3, 1);
        float expect = in * in * (3 - 2 * in) * out * out * (3 - 2 * out);
        shaped &= close_to(a.fade, expect, 1e-4f);
    }
    check(shaped, "fade in over fade_in, out over fade_out (smoothstep), 1 between");
}

static void curl()
{
    float q[3] = { 0.3f, -1.2f, 2.5f }, c[3];
    double sum = 0, div_max = 0;
    for (int i = 0; i < 200; i++) {
        q[0] = 0.37f * i, q[1] = -0.21f * i + 1, q[2] = 0.11f * i - 3;
        pc_particle_curl(q, 0.5f, c);
        sum += sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        const float h = 1e-2f;
        float div = 0;
        for (int k = 0; k < 3; k++) {
            float a[3] = { q[0], q[1], q[2] }, b[3] = { q[0], q[1], q[2] }, ca[3], cb[3];
            a[k] += h, b[k] -= h;
            pc_particle_curl(a, 0.5f, ca);
            pc_particle_curl(b, 0.5f, cb);
            div += (ca[k] - cb[k]) / (2 * h);
        }
        div_max = fmax(div_max, fabs(div));
    }
    check(div_max < 2e-2, "curl noise is divergence-free (no clumping)");
    check(sum / 200 > 0.3 && sum / 200 < 1.5, "curl noise has about unit strength (the amplitude is in world units)");
    float c0[3], c1[3];
    pc_particle_curl(q, 0, c0);
    pc_particle_curl(q, 0.01f, c1);
    check(fabsf(c0[0] - c1[0]) + fabsf(c0[1] - c1[1]) + fabsf(c0[2] - c1[2]) < 0.05f, "it evolves smoothly in time");
}

static void projection_and_keys()
{
    /* Camera at the origin looking along +z (the GTE's: view = R p + T), H 256, offset (160, 120). */
    float rot[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } }, proj[4] = { 256, 256, 160, 120 };
    float p[3] = { 50, -25, 512 }, s[3];
    check(pc_particle_project(rot, proj, p, s) && close_to(s[0], 185) && close_to(s[1], 107.5f) && close_to(s[2], 512),
        "projection: offset + H x view / z, SZ = view z");
    float behind[3] = { 0, 0, 8 };
    check(!pc_particle_project(rot, proj, behind, s), "nearer than PC_PARTICLE_NEAR: not drawn");
    check(pc_particle_key(512.7f, 9) == ((512u << 16) | 9u) && pc_particle_key(1e6f, 3) == ((65535u << 16) | 3u)
            && pc_particle_key(1000, 1) < pc_particle_key(1001, 0),
        "keys: the nearer mote is the smaller key");
    check(pc_particle_anchor(10.9f, 1) == 10 && pc_particle_anchor(10.9f, 2) == 10 && pc_particle_anchor(10.2f, 2) == 9,
        "a 2-pixel mote covers the two game pixels nearest its centre");
}

static void light_shape()
{
    /* A spot at the origin aiming +y (down), cone 60 (cos 30 = 0.866), inner cos 0.95. */
    float centre[3] = { 0, 0, 0 }, dir[4] = { 0, 1, 0, 0.866f }, right[4] = { 0.95f, 0, 0, 0 }, up[4] = { 0, 0, 0, 0 };
    float below[3] = { 0, 100, 0 }, l[3] = { 0, -1, 0 };
    check(close_to(pc_light_shape(1, centre, dir, right, up, below, l), 1), "inside a spot's inner cone: 1");
    float side[3] = { 100, 10, 0 }, ls[3] = { -0.995f, -0.0995f, 0 };
    check(close_to(pc_light_shape(1, centre, dir, right, up, side, ls), 0), "outside its cone: 0");
    check(close_to(pc_light_shape(0, centre, dir, right, up, side, ls), 1), "a point light: 1");
}

/* Emitter assets (particle_assets.h): modules flattened into lighting fields. */
static void assets(const char* dir)
{
    if (dir)
        setenv("ROOD_EMITTERS", dir, 1);
    rood_particles::reload_assets();
    const rood_particles::Asset* dust = rood_particles::find_asset("dust_motes");
    check(dust && dust->name == "Dust motes" && dust->category == "Atmosphere" && !dust->tags.empty(),
        "dust_motes.json: its name, category and tags (the library's)");
    auto has = [&](const char* key, const char* value) {
        const std::string* v = dust ? dust->get(key) : nullptr;
        return v && *v == value;
    };
    check(dust && has("shape", "box") && has("count", "900") && has("drift", "1.2,0.4,0.8") && has("curl", "9")
            && has("curl_scale", "56") && has("fade_in", "2.5") && has("lit", "0") && has("quality", "dust")
            && has("pixel_max", "2") && !dust->get("gravity"),
        "its spawn, modules (drift, curl, fade) and renderer as lighting fields; an absent module writes nothing");
    prefab::Json root;
    std::string error;
    rood_particles::Asset a;
    prefab::json_parse("{\"kind\":\"emitter\",\"modules\":{\"wobble\":{}}}", root);
    check(!rood_particles::asset_from_json("x", root, a, &error) && error.find("wobble") != std::string::npos,
        "an unknown module is refused, by name");
    prefab::json_parse("{\"kind\":\"emitter\",\"lighting\":{\"mode\":\"lit\"},\"render\":{\"type\":\"pixel\"}}", root);
    check(rood_particles::asset_from_json("y", root, a, &error) && a.get("lit") && *a.get("lit") == "1",
        "lighting mode lit is the field lit=1");
    prefab::json_parse("{\"kind\":\"prefab\"}", root);
    check(!rood_particles::asset_from_json("z", root, a, &error), "only kind \"emitter\" files are emitter assets");
}

int main(int argc, char** argv)
{
    spawn_and_cycles();
    motion();
    curl();
    projection_and_keys();
    light_shape();
    assets(argc > 1 ? argv[1] : nullptr);
    printf("%s\n", failures ? "FAIL" : "PASS: particles");
    return failures ? 1 : 0;
}
