// The library's thumbnails (library_thumbs.h).
#include "library_thumbs.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_math.h"
#include "light_shape.h"
#include "platform.h"

namespace rood_thumbs {
namespace {
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void normalize3(float* v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-9f)
        v[0] /= l, v[1] /= l, v[2] /= l;
}
struct Camera {
    float eye[3], forward[3], right[3], up[3], tan_half;
};
/* From the front and above (y down), looking at the item, far enough for its reach. */
Camera camera_for(const Scene& s)
{
    float extent = 96;
    for (const prefab::EvalLight& l : s.lights)
        extent = fmaxf(extent, fminf(l.radius * 0.6f, 900.0f));
    if (s.fog.on)
        extent = fmaxf(extent, 0.6f * fmaxf(s.fog.size[0], fmaxf(s.fog.size[1], s.fog.size[2])));
    extent = fminf(extent, 1200.0f);
    Camera c;
    c.forward[0] = 0, c.forward[1] = 0.45f, c.forward[2] = 1;
    normalize3(c.forward);
    float focus[3] = { 0, -extent * 0.25f, 0 }, distance = extent * 2.2f;
    for (int k = 0; k < 3; k++)
        c.eye[k] = focus[k] - c.forward[k] * distance;
    c.right[0] = 1, c.right[1] = 0, c.right[2] = 0;
    c.up[0] = 0, c.up[1] = -c.forward[2], c.up[2] = c.forward[1]; /* across the forward, toward -y */
    c.tan_half = tanf(look().fov * 0.5f * 3.14159265f / 180);
    return c;
}
/* One light's contribution at p (normal n; null: a fog sample, no N.L), as the renderers evaluate it. */
void add_light(const prefab::EvalLight& l, const float p[3], const float* n, float out[3])
{
    if (!l.enabled || l.intensity <= 0)
        return;
    float f[3], r[3], u[3], centre[3] = { l.position[0], l.position[1], l.position[2] };
    rood_math::light_frame(l.direction, f, r, u);
    float hr[3], hu[3];
    for (int k = 0; k < 3; k++)
        hr[k] = r[k] * l.size[0] * 0.5f, hu[k] = u[k] * l.size[1] * 0.5f;
    if (l.type == prefab::LIGHT_RECT) { /* the nearest point of its rectangle (editor_view.cpp) */
        float rel[3] = { p[0] - centre[0], p[1] - centre[1], p[2] - centre[2] };
        float a = fminf(fmaxf(dot3(rel, hr) / fmaxf(dot3(hr, hr), 1e-6f), -1.0f), 1.0f);
        float b = fminf(fmaxf(dot3(rel, hu) / fmaxf(dot3(hu, hu), 1e-6f), -1.0f), 1.0f);
        for (int k = 0; k < 3; k++)
            centre[k] += hr[k] * a + hu[k] * b;
    }
    float to[3] = { centre[0] - p[0], centre[1] - p[1], centre[2] - p[2] };
    float d = sqrtf(dot3(to, to));
    float ld[3] = { to[0] / fmaxf(d, 1e-3f), to[1] / fmaxf(d, 1e-3f), to[2] / fmaxf(d, 1e-3f) };
    float a = pc_light_falloff(d, l.radius) * (n ? fmaxf(dot3(n, ld), 0.0f) : 1.0f);
    float c = -dot3(ld, f);
    if (l.type == prefab::LIGHT_SPOT)
        a *= pc_smoothstep(cosf(l.cone * 0.5f * 3.14159265f / 180), pc_spot_cos_inner(l.cone, l.inner), c);
    if (l.type == prefab::LIGHT_RECT) {
        float ba, bb;
        pc_barn_pack(l.barn_angle, l.barn_length, &ba, &bb);
        a *= fmaxf(c, 0.0f) * pc_barn_doors(l.position, hr, hu, f, ba, bb, p);
    }
    for (int k = 0; k < 3; k++)
        out[k] += l.color[k] * l.intensity * a;
}
/* Henyey-Greenstein by Schlick's fit, x 4 pi (fog_volume.glsl fv_phase). */
float phase(float mu, float g)
{
    float k = 1.55f * g - 0.55f * g * g * g, d = 1 - k * mu;
    return fminf((1 - k * k) / fmaxf(d * d, 1e-4f), 12.0f);
}
/* Smooth value noise in 0..1 at a 96-unit period, squashed vertically (the volumes' wisps; deterministic). */
float lattice(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    h = prefab::hash32(h);
    return (h & 0xffffff) / 16777215.0f;
}
float wisp_noise(const float p[3])
{
    float x = p[0] / 96, y = p[1] * 2.2f / 96, z = p[2] / 96;
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy), fz = fz * fz * (3 - 2 * fz);
    float v = 0;
    for (int c = 0; c < 8; c++) {
        float w = (c & 1 ? fx : 1 - fx) * (c & 2 ? fy : 1 - fy) * (c & 4 ? fz : 1 - fz);
        v += w * lattice(ix + (c & 1), iy + (c >> 1 & 1), iz + (c >> 2 & 1));
    }
    return v;
}
float encode(float linear)
{
    float c = 1 - expf(-look().exposure * fmaxf(linear, 0.0f));
    c = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f;
    return fminf(fmaxf(c, 0.0f), 1.0f);
}
/* Where the ray enters and leaves the fog volume (it rests on the floor, centred on x = z = 0). */
bool fog_span(const Fog& fog, const float o[3], const float d[3], float* t0, float* t1)
{
    float half[3] = { fog.size[0] * 0.5f, fog.size[1] * 0.5f, fog.size[2] * 0.5f };
    float centre[3] = { 0, -half[1], 0 };
    float lo = 0, hi = 1e30f;
    if (fog.sphere) { /* the ellipsoid as a unit sphere */
        float q[3], v[3];
        for (int k = 0; k < 3; k++)
            q[k] = (o[k] - centre[k]) / half[k], v[k] = d[k] / half[k];
        float a = dot3(v, v), b = dot3(q, v), c = dot3(q, q) - 1, disc = b * b - a * c;
        if (disc <= 0)
            return false;
        float s = sqrtf(disc);
        lo = fmaxf((-b - s) / a, 0.0f), hi = (-b + s) / a;
    } else {
        for (int k = 0; k < 3; k++) {
            if (fabsf(d[k]) < 1e-9f) {
                if (fabsf(o[k] - centre[k]) > half[k])
                    return false;
                continue;
            }
            float a = (centre[k] - half[k] - o[k]) / d[k], b = (centre[k] + half[k] - o[k]) / d[k];
            lo = fmaxf(lo, fminf(a, b)), hi = fminf(hi, fmaxf(a, b));
        }
    }
    *t0 = lo, *t1 = hi;
    return hi > lo;
}
} // namespace

const std::vector<LookField>& look_fields()
{
    static const std::vector<LookField> f = {
        { "floor", "Floor albedo", &Look::floor, 0, 1 },
        { "wall", "Wall albedo", &Look::wall, 0, 1 },
        { "grid", "Grid lines (x floor)", &Look::grid, 0, 1 },
        { "ambient", "Ambient", &Look::ambient, 0, 1 },
        { "fog_ambient", "Fog ambient", &Look::fog_ambient, 0, 2 },
        { "exposure", "Exposure", &Look::exposure, 0.1f, 8 },
        { "fov", "Field of view", &Look::fov, 10, 90 },
    };
    return f;
}
std::string look_path()
{
    const char* e = getenv("ROOD_LIBRARY_THUMBS_LOOK");
    return e && *e ? e : "pc/assets/editor/library_thumbs.json";
}
namespace {
bool look_loaded;
}
Look& look()
{
    static Look l;
    if (!look_loaded) {
        look_loaded = true;
        std::string error;
        if (!load_look(&error))
            fprintf(stderr, "[library] %s\n", error.c_str());
    }
    return l;
}
bool load_look(std::string* error)
{
    look_loaded = true;
    Look& l = look();
    l = Look();
    FILE* f = fopen(look_path().c_str(), "rb");
    if (!f)
        return true; /* none: the defaults */
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    prefab::Json j;
    std::string e;
    if (!prefab::json_parse(text.c_str(), j, &e) || j.type != prefab::Json::Object) {
        if (error)
            *error = look_path() + ": " + (e.empty() ? "not an object" : e);
        return false;
    }
    for (const LookField& fd : look_fields())
        if (const prefab::Json* v = j.get(fd.key))
            if (v->type == prefab::Json::Number)
                l.*fd.value = fminf(fmaxf((float)v->number, fd.min), fd.max);
    if (const prefab::Json* b = j.get("background"))
        for (size_t k = 0; k < 3 && k < b->items.size(); k++)
            l.background[k] = fminf(fmaxf((float)b->items[k].number, 0.0f), 1.0f);
    return true;
}
bool save_look(std::string* error)
{
    const Look& l = look();
    prefab::Json j = prefab::Json::object();
    j.set("about", prefab::Json::str("The editor Library's thumbnail scene (pc/src/library_thumbs.h): albedos, light, exposure, "
                                     "field of view. Library > Thumbnails... edits it."));
    auto r4 = [](float v) { return prefab::Json::num(roundf(v * 10000) / 10000); };
    for (const LookField& fd : look_fields())
        j.set(fd.key, r4(l.*fd.value));
    prefab::Json b = prefab::Json::array();
    for (int k = 0; k < 3; k++)
        b.items.push_back(r4(l.background[k]));
    j.set("background", b);
    std::string text = prefab::json_write(j) + "\n", path = look_path(), temp = path + ".tmp";
    size_t slash = path.rfind('/');
    for (size_t i = 1; slash != std::string::npos && i <= slash; i++)
        if (i == slash || path[i] == '/')
            pc_mkdir(path.substr(0, i).c_str());
    FILE* f = fopen(temp.c_str(), "wb");
    bool ok = f && fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f)
        fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    return true;
}

void render(const Scene& s, int size, std::vector<uint8_t>& rgba)
{
    rgba.assign((size_t)size * size * 4, 255);
    const Look& lk = look();
    Camera cam = camera_for(s);
    const int steps = 40;
    for (int j = 0; j < size; j++)
        for (int i = 0; i < size; i++) {
            float u = ((i + 0.5f) / size * 2 - 1) * cam.tan_half, v = (1 - (j + 0.5f) / size * 2) * cam.tan_half;
            float d[3];
            for (int k = 0; k < 3; k++)
                d[k] = cam.forward[k] + cam.right[k] * u + cam.up[k] * v;
            normalize3(d);
            float t = 1e30f, n[3] = { 0, 0, 0 }, albedo = 0;
            if (d[1] > 1e-6f) { /* the floor, y = 0 (the camera is above it: y < 0) */
                float tf = -cam.eye[1] / d[1], z = cam.eye[2] + d[2] * tf;
                if (tf > 0 && z <= s.wall_z) {
                    t = tf, n[1] = -1;
                    float x = cam.eye[0] + d[0] * tf;
                    bool line = fabsf(fmodf(fabsf(x) + 2, 64.0f)) < 4 || fabsf(fmodf(fabsf(z) + 2, 64.0f)) < 4;
                    albedo = lk.floor * (line ? lk.grid : 1.0f);
                }
            }
            if (d[2] > 1e-6f) { /* the back wall, z = wall_z, facing -z */
                float tw = (s.wall_z - cam.eye[2]) / d[2], y = cam.eye[1] + d[1] * tw;
                if (tw > 0 && tw < t && y <= 0)
                    t = tw, n[0] = n[1] = 0, n[2] = -1, albedo = lk.wall;
            }
            float colour[3] = { lk.background[0], lk.background[1], lk.background[2] };
            if (t < 1e29f) {
                float p[3] = { cam.eye[0] + d[0] * t, cam.eye[1] + d[1] * t, cam.eye[2] + d[2] * t };
                float light[3] = { lk.ambient, lk.ambient, lk.ambient };
                for (const prefab::EvalLight& l : s.lights)
                    add_light(l, p, n, light);
                for (int k = 0; k < 3; k++)
                    colour[k] = albedo * light[k];
            }
            float t0, t1;
            if (s.fog.on && fog_span(s.fog, cam.eye, d, &t0, &t1) && t0 < t) {
                t1 = fminf(t1, t);
                const Fog& fg = s.fog;
                float dt = (t1 - t0) / steps, transmit = 1, gathered[3] = { 0, 0, 0 };
                float half[3] = { fg.size[0] * 0.5f, fg.size[1] * 0.5f, fg.size[2] * 0.5f };
                float least = 1 - fminf(fmaxf(fg.opacity, 0.0f), 1.0f); /* its opacity cap (fog_volume.glsl fv_march) */
                for (int k = 0; k < steps; k++) {
                    float tt = t0 + (k + 0.5f) * dt, p[3] = { cam.eye[0] + d[0] * tt, cam.eye[1] + d[1] * tt, cam.eye[2] + d[2] * tt };
                    float q[3] = { p[0] / half[0], (p[1] + half[1]) / half[1], p[2] / half[2] }; /* q[1]: -1 top, +1 base */
                    float r;
                    if (fg.sphere) {
                        r = sqrtf(dot3(q, q));
                    } else { /* a box's footprint with rounded corners (fv_density) */
                        float hx = q[0] * q[0], hz = q[2] * q[2];
                        hx = hx * hx * hx, hz = hz * hz * hz;
                        r = fmaxf(powf(hx + hz, 1.0f / 6.0f), fabsf(q[1]));
                    }
                    if (r >= 1)
                        continue;
                    float w = 1 - pc_smoothstep(1 - fminf(fmaxf(fg.edge, 0.001f), 1.0f), 1, r);
                    if (fg.taper > 0) { /* thinning toward its top */
                        float up = fminf(fmaxf(0.5f - 0.5f * q[1], 0.0f), 1.0f);
                        w *= 1 + ((1 - up) * (1 - up) - 1) * fg.taper;
                    }
                    if (fg.noise > 0) /* its wisps (medium_wisps) */
                        w *= 1 + (2 * pc_smoothstep(0.2f, 0.8f, wisp_noise(p)) - 1) * fg.noise;
                    float sigma = fg.density * w;
                    if (sigma <= 0)
                        continue;
                    /* Self-shadowing: the light reaching here through the fog above it (an estimate). */
                    float shade = fg.shadow > 0 ? expf(-fg.shadow * fg.density * (p[1] + fg.size[1]) * 0.5f) : 1.0f;
                    float lit[3] = { 0, 0, 0 };
                    for (const prefab::EvalLight& l : s.lights) {
                        float c[3] = { 0, 0, 0 };
                        add_light(l, p, nullptr, c);
                        float to[3] = { l.position[0] - p[0], l.position[1] - p[1], l.position[2] - p[2] };
                        normalize3(to);
                        float g = phase(dot3(d, to), fg.phase) * shade;
                        for (int k3 = 0; k3 < 3; k3++)
                            lit[k3] += c[k3] * g;
                    }
                    float glow = 1;
                    if (fg.glow_core > 0) { /* gathered at its base */
                        float b2 = q[0] * q[0] + (q[1] - 1) * (q[1] - 1) + q[2] * q[2];
                        glow = 1 + (2.2f * expf(-2 * b2) - 1) * fg.glow_core;
                    }
                    for (int c = 0; c < 3; c++) {
                        float in = lit[c] + lk.fog_ambient * fg.ambient * (0.5f + 0.5f * shade);
                        gathered[c] += transmit * (fg.color[c] * in + fg.fill[c] + fg.glow[c] * glow) * fg.brightness * sigma * dt;
                    }
                    float a = expf(-sigma * dt);
                    if (least > 0)
                        a = fmaxf(a, fminf(least / fmaxf(transmit, 1e-6f), 1.0f));
                    transmit *= a;
                }
                for (int c = 0; c < 3; c++)
                    colour[c] = colour[c] * transmit + gathered[c];
            }
            uint8_t* px = &rgba[((size_t)j * size + i) * 4];
            for (int c = 0; c < 3; c++)
                px[c] = (uint8_t)(encode(colour[c]) * 255 + 0.5f);
            px[3] = 255;
        }
}

std::string hash(const Scene& s, int size)
{
    const Look& lk = look();
    char text[512];
    std::string desc;
    snprintf(text, sizeof text, "v%d %d look %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g wall %.6g\n", VERSION, size,
        lk.floor, lk.wall, lk.grid, lk.ambient, lk.fog_ambient, lk.exposure, lk.background[0], lk.background[1],
        lk.background[2], lk.fov, s.wall_z);
    desc += text;
    for (const prefab::EvalLight& l : s.lights) {
        snprintf(text, sizeof text,
            "l %d %d %.6g %.6g %.6g c %.6g %.6g %.6g d %.6g %.6g %.6g i %.6g r %.6g %.6g %.6g %.6g %.6g %.6g %.6g\n", l.type,
            l.enabled ? 1 : 0, l.position[0], l.position[1], l.position[2], l.color[0], l.color[1], l.color[2],
            l.direction[0], l.direction[1], l.direction[2], l.intensity, l.radius, l.cone, l.inner, l.size[0], l.size[1],
            l.barn_angle, l.barn_length);
        desc += text;
    }
    if (s.fog.on) {
        const Fog& f = s.fog;
        snprintf(text, sizeof text, "f %d %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g\n",
            f.sphere ? 1 : 0, f.size[0], f.size[1], f.size[2], f.density, f.edge, f.color[0], f.color[1], f.color[2], f.fill[0],
            f.fill[1], f.fill[2], f.glow[0], f.glow[1], f.glow[2]);
        desc += text;
        snprintf(text, sizeof text, "g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g\n", f.noise, f.taper, f.glow_core, f.phase,
            f.opacity, f.brightness, f.ambient, f.shadow);
        desc += text;
    }
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : desc)
        h = (h ^ c) * 1099511628211ULL;
    snprintf(text, sizeof text, "%016llx", (unsigned long long)h);
    return text;
}

std::string cache_dir()
{
    const char* e = getenv("ROOD_LIBRARY_THUMBS");
    return e && *e ? e : "pc/build/library_thumbs";
}
std::string file_name(const std::string& kind, const std::string& id, const std::string& h)
{
    std::string safe;
    for (char c : id)
        safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return kind + "-" + (safe.empty() ? "default" : safe) + "-" + h + ".rgba";
}
namespace {
void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
uint32_t get32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void make_dirs(const std::string& dir)
{
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/')
            pc_mkdir(dir.substr(0, i).c_str());
}
} // namespace
bool load(const std::string& path, int size, std::vector<uint8_t>& rgba)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    uint8_t head[16];
    size_t want = (size_t)size * size * 4;
    bool ok = fread(head, 1, 16, f) == 16 && !memcmp(head, "RTHB", 4) && get32(head + 4) == (uint32_t)size
        && get32(head + 8) == (uint32_t)size && get32(head + 12) == (uint32_t)VERSION;
    if (ok) {
        rgba.resize(want);
        ok = fread(rgba.data(), 1, want, f) == want;
    }
    fclose(f);
    return ok;
}
bool save(const std::string& path, int size, const std::vector<uint8_t>& rgba, std::string* error)
{
    size_t slash = path.rfind('/');
    if (slash != std::string::npos)
        make_dirs(path.substr(0, slash));
    std::string temp = path + ".tmp";
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        if (error)
            *error = "cannot write " + path + ": " + strerror(errno);
        return false;
    }
    uint8_t head[16];
    memcpy(head, "RTHB", 4);
    put32(head + 4, (uint32_t)size), put32(head + 8, (uint32_t)size), put32(head + 12, (uint32_t)VERSION);
    bool ok = fwrite(head, 1, 16, f) == 16 && fwrite(rgba.data(), 1, rgba.size(), f) == rgba.size();
    fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    return true;
}
} // namespace rood_thumbs
