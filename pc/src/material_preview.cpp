// The material preview (pc/docs/plans/2026-10-30-master-materials-plan.md, decision 6): a sphere
// shaded on the CPU with the shared GGX model (pc/docs/Materials.md "Shading": GGX D,
// height-correlated Smith, Schlick; F0 = 0.04 x specular mixed to the albedo by metal), textured
// nearest with the instance's texture where it is drawn now (else a swatch), with the relief and the
// per-texel values the material planes hold for it (its painted maps otherwise). Lit like the room
// (settings key material_preview_room, on by default): the strongest of the room's lights (or its sun)
// at Ashley, seen from the game camera, as the key light, and the game's own room light colour there
// as the ambient; else (or with no light reaching Ashley) its own key light and ambient, settings keys
// (material_preview_*). Drawn as rectangles, so it is the same picture on GL and Vulkan; reshaded
// only when something changed. Window > Material preview; RPC material_preview.
#define _USE_MATH_DEFINES /* M_PI: MinGW's <math.h> hides it under -std=c++17 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_shell.h"
#include "emissive.h"
#include "emissive_internal.h"
#include "environment.h"
#include "light_list.h"
#include "light_prefabs.h" /* prefab::Json */
#include "materials.h"
#include "dev_state.h"

using prefab::Json;

extern "C" {
extern float pc_specular_strength, pc_roughness_bias;
/* The look (settings keys, lighting_quality.cpp's table; RPC graphics): the sphere's pixels across,
 * the key light's strength and direction (degrees: yaw around the vertical, pitch above the
 * horizon, as seen from the viewer), the ambient's strength, the swatch's grey. Lit like the room
 * (`room` non-zero): the room's key light and ambient times `room_key` and `room_ambient`, taken at
 * `room_height` world units above Ashley's feet; `relief`: the relief's share on the sphere. */
float pc_material_preview_size = 96, pc_material_preview_light = 1.6f, pc_material_preview_yaw = -35,
      pc_material_preview_pitch = 35, pc_material_preview_ambient = 0.22f, pc_material_preview_swatch = 0.6f;
int pc_material_preview_room = 1;
float pc_material_preview_room_key = 1.0f, pc_material_preview_room_ambient = 0.35f, pc_material_preview_room_height = 40,
      pc_material_preview_relief = 2.0f;
/* The room's lights (lights.cpp), its camera (scene capture), the game's room light colour at a
 * position (room_lighting.c: BATTLE's actor lighting) and the environment's sun: weak, so a build
 * without them shows the preview's own light. */
int pc_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4],
    float extent_up[][4], int shadows[], int max) __attribute__((weak));
int pc_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy) __attribute__((weak));
int func_80098B38(unsigned position, int layer, unsigned* color) __attribute__((weak));
int pc_env_gpu_get(pc_env_gpu* out) __attribute__((weak));
}
bool pc_material_preview_open = false;

namespace {
std::string shown; /* the instance previewed (the Materials panel's selection follows it) */
std::vector<uint8_t> pixels; /* size x size RGB */
int pixels_size = 0;
std::string cache_key;
std::string texture_key_used;
int relief_used = 0; /* 0 none, 1 a height map, 2 estimated (emissive.cpp's MaterialUse::relief) */
bool planes_used = false; /* the per-texel values from the material planes (the instance is drawn here) */

/* The light the sphere is shaded with: the preview's own, or the room's. */
struct Lighting {
    bool room = false;          /* taken from the room */
    std::string key_from;       /* "light <n>", "sun", "preview" */
    float key[3] = { 0, 0, 0 }; /* rgb x strength */
    float dir[3] = { 0, 0, 1 }; /* toward the light, the preview's frame (x right, y up, z to the viewer) */
    float ambient[3] = { 0, 0, 0 };
};
float q(float v) { return roundf(v * 256.0f) / 256.0f; } /* (the cache key: a flicker below this reshades nothing) */
Lighting preview_lighting()
{
    Lighting l;
    float yaw = pc_material_preview_yaw * (float)M_PI / 180, pitch = pc_material_preview_pitch * (float)M_PI / 180;
    l.dir[0] = sinf(yaw) * cosf(pitch), l.dir[1] = sinf(pitch), l.dir[2] = cosf(yaw) * cosf(pitch);
    for (int c = 0; c < 3; c++) l.key[c] = pc_material_preview_light, l.ambient[c] = pc_material_preview_ambient;
    l.key_from = "preview";
    return l;
}
/* The room's light at Ashley (pc/docs/Materials.md "Masters and instances"): the key is the light that
 * delivers most there (its colour times the renderers' window (1 - x^2)^2, x = distance / radius; the
 * sun by its colour), its direction turned into the camera's view; the ambient is the game's room
 * light colour at Ashley's feet (128 = 1). No light reaching Ashley: the preview's own key light. */
Lighting lighting()
{
    Lighting l = preview_lighting();
    if (!pc_material_preview_room || !pc_dev_state) return l;
    struct pc_dev_state st;
    memset(&st, 0, sizeof st);
    pc_dev_state(&st);
    float rot[3][3], t[3], h, ox, oy;
    if (!st.valid || !st.has_player || !pc_scene_camera_get || !pc_scene_camera_get(rot, t, &h, &ox, &oy)) return l;
    l.room = true;
    float at[3] = { (float)st.x, (float)st.y - pc_material_preview_room_height, (float)st.z };
    unsigned colour = 0x808080;
    if (func_80098B38) func_80098B38((unsigned)(uint16_t)(int16_t)st.x | (unsigned)(uint16_t)(int16_t)st.z << 16, 0, &colour);
    for (int c = 0; c < 3; c++)
        l.ambient[c] = (float)((colour >> (8 * c)) & 255) / 128.0f * pc_material_preview_room_ambient;
    float best = 0, world_dir[3] = { 0, 0, 0 }, key[3] = { 0, 0, 0 };
    if (pc_lights_world) {
        static float p[PC_LIGHTS_MAX][4], col[PC_LIGHTS_MAX][4], d[PC_LIGHTS_MAX][4], r[PC_LIGHTS_MAX][4], u[PC_LIGHTS_MAX][4];
        static int sh[PC_LIGHTS_MAX];
        int n = pc_lights_world(p, col, d, r, u, sh, PC_LIGHTS_MAX);
        for (int i = 0; i < n; i++) {
            float to[3] = { p[i][0] - at[0], p[i][1] - at[1], p[i][2] - at[2] };
            float dist = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
            if (p[i][3] <= 0 || dist >= p[i][3] || dist < 1e-3f) continue;
            float x = dist / p[i][3], window = (1 - x * x) * (1 - x * x);
            float w = (0.299f * col[i][0] + 0.587f * col[i][1] + 0.114f * col[i][2]) * window;
            if (w <= best) continue;
            best = w;
            for (int c = 0; c < 3; c++) world_dir[c] = to[c] / dist, key[c] = col[i][c] * window;
            l.key_from = "light " + std::to_string(i);
        }
    }
    if (pc_env_gpu_get) {
        pc_env_gpu env;
        if (pc_env_gpu_get(&env) && env.sun_col[0] + env.sun_col[1] + env.sun_col[2] > 0) {
            float w = 0.299f * env.sun_col[0] + 0.587f * env.sun_col[1] + 0.114f * env.sun_col[2];
            if (w > best) {
                best = w;
                for (int c = 0; c < 3; c++) world_dir[c] = env.sun_dir[c], key[c] = env.sun_col[c];
                l.key_from = "sun";
            }
        }
    }
    if (best <= 0) {
        l.key_from = "preview"; /* (nothing reaches Ashley: the preview's own key light, the room's ambient) */
        return l;
    }
    /* World (y down) into the camera's view (the GTE rotation: x right, y down, z away from the viewer),
     * then the preview's frame (y up, z toward the viewer). */
    float v[3];
    for (int r = 0; r < 3; r++) v[r] = rot[r][0] * world_dir[0] + rot[r][1] * world_dir[1] + rot[r][2] * world_dir[2];
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-6f) return l;
    l.dir[0] = v[0] / len, l.dir[1] = -v[1] / len, l.dir[2] = -v[2] / len;
    for (int c = 0; c < 3; c++) l.key[c] = key[c] * pc_material_preview_room_key;
    return l;
}

std::string key_of(const materials::Instance* i, const std::map<std::string, float>& values, const Lighting& l)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d", i ? i->id.c_str() : "", (int)pc_material_preview_size,
        pc_material_preview_swatch, pc_specular_strength, pc_roughness_bias, pc_bump_strength, pc_bump_occlusion,
        pc_material_preview_relief, pc_auto_height_strength, pc_auto_height);
    std::string k = buf;
    for (const float* v : { l.key, l.dir, l.ambient }) {
        snprintf(buf, sizeof buf, "|%.4f,%.4f,%.4f", q(v[0]), q(v[1]), q(v[2]));
        k += buf;
    }
    for (const auto& p : values) k += "|" + p.first + "=" + std::to_string(p.second);
    if (i) k += "|" + i->key + "|" + i->clut;
    return k;
}
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* The texture's texels and, per texel, the values the sphere is shaded with: from the material
 * planes when the instance is what draws the texture here (its painted maps, its relief, as the room
 * shows them), else its resolved values with its painted maps (roughness, metal, height). */
struct Texels {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool per_texel = false;
    std::vector<float> rough, spec, metal, gx, gy, cavity; /* gx, gy: the height's slopes (height units per texel) */
    bool relief = false;
};
/* The height's slopes (Sobel, edges clamped), as emissive.cpp's make_stamp has them. */
void slopes(const std::vector<float>& height, int w, int h, Texels& t)
{
    auto at = [&](int x, int y) { return height[(size_t)std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)]; };
    t.gx.assign((size_t)w * h, 0), t.gy.assign((size_t)w * h, 0), t.cavity.assign((size_t)w * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            t.gx[i] = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x - 1, y) - at(x - 1, y + 1)) / 8.0f;
            t.gy[i] = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x, y - 1) - at(x + 1, y - 1)) / 8.0f;
            float around = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) around += dx || dy ? at(x + dx, y + dy) : 0.0f;
            t.cavity[i] = std::clamp((around / 8.0f - at(x, y)) * 2.0f, 0.0f, 1.0f);
        }
    t.relief = true;
}
Texels texels_of(const materials::Instance* i, const materials::Surface& s, std::string* digest)
{
    Texels t;
    texture_key_used.clear(), relief_used = 0, planes_used = false;
    if (!i) return t;
    /* (a palette instance: the first texture it draws here, in the planes' stable order) */
    for (const emissive::MaterialUse& u : emissive::material_use_list()) {
        if (u.entry != i->id || (!i->key.empty() && u.key != i->key)) continue;
        t.w = u.box[2] - u.box[0] + 1, t.h = u.box[3] - u.box[1] + 1;
        t.rgba = emissive::texture_rgba_box(u.page, u.clut, u.box);
        size_t n = (size_t)t.w * t.h;
        t.rough.assign(n, s.roughness), t.spec.assign(n, s.specular), t.metal.assign(n, s.metal);
        t.gx.assign(n, 0), t.gy.assign(n, 0), t.cavity.assign(n, 0);
        t.per_texel = true, t.relief = u.relief != 0;
        for (int y = 0; y < t.h; y++)
            for (int x = 0; x < t.w; x++) {
                int c[PC_MATERIAL_PLANES];
                if (!emissive::material_at(u.box[0] + x, u.box[1] + y, u.page, c)) continue;
                size_t k = (size_t)y * t.w + x;
                t.rough[k] = c[0] / 255.0f, t.spec[k] = c[1] / 255.0f, t.metal[k] = c[2] / 255.0f;
                if (c[3]) t.gx[k] = (c[3] - 128) / 63.5f, t.gy[k] = (c[4] - 128) / 63.5f, t.cavity[k] = c[5] / 255.0f;
                for (int p = 0; p < 6; p++) digest->push_back((char)c[p]); /* (the planes may follow an edit a frame later) */
            }
        texture_key_used = u.key, relief_used = u.relief, planes_used = true;
        return t;
    }
    if (i->key.empty()) return t;
    emissive::TexturePick pick;
    if (!emissive::texture_where(i->key, pick)) return t;
    t.w = pick.w(), t.h = pick.h();
    t.rgba = emissive::texture_rgba_box(pick.tpage, pick.clut, pick.box);
    texture_key_used = i->key;
    size_t n = (size_t)t.w * t.h;
    const materials::Channel* r = materials::channel(i->key, "roughness");
    const materials::Channel* m = materials::channel(i->key, "metal");
    const materials::Channel* hm = materials::channel(i->key, "height");
    auto fits = [&](const materials::Channel* c) { return c && c->w == t.w && c->h == t.h && c->values.size() >= n; };
    if (fits(r) || fits(m) || (fits(hm) && s.bump > 0)) {
        t.per_texel = true;
        t.rough.assign(n, s.roughness), t.spec.assign(n, s.specular), t.metal.assign(n, s.metal);
        t.gx.assign(n, 0), t.gy.assign(n, 0), t.cavity.assign(n, 0);
        for (size_t k = 0; k < n; k++) {
            if (fits(r)) t.rough[k] = r->values[k] / 255.0f;
            if (fits(m)) t.metal[k] = m->values[k] / 255.0f;
        }
        if (fits(hm) && s.bump > 0) {
            std::vector<float> height(n);
            for (size_t k = 0; k < n; k++) height[k] = hm->values[k] / 255.0f * s.bump;
            slopes(height, t.w, t.h, t);
            relief_used = 1;
        }
        *digest += (r ? r->path : "") + (m ? m->path : "") + (hm ? hm->path : "");
    }
    return t;
}

/* The sphere, reshaded when its inputs changed. */
void shade()
{
    const materials::Instance* i = shown.empty() ? nullptr : materials::find_instance(shown);
    std::map<std::string, float> values;
    if (i) materials::resolve(i->id, values, nullptr, nullptr);
    materials::Surface s = materials::surface_of(values);
    Lighting li = lighting();
    std::string planes;
    Texels tex = texels_of(i, s, &planes);
    std::string k = key_of(i, values, li) + "|" + std::to_string(materials::catalog_generation()) + "|"
        + std::to_string(std::hash<std::string>()(planes)) + "|" + texture_key_used;
    if (k == cache_key) return;
    cache_key = k;
    int n = (int)pc_material_preview_size;
    if (n < 8) n = 8;
    if (n > 256) n = 256;
    pixels_size = n;
    pixels.assign((size_t)n * n * 3, 0);
    int tw = tex.rgba.empty() ? 0 : tex.w, th = tex.h;
    const float* L = li.dir;
    float bump = std::max(pc_bump_strength, 0.0f) * std::max(pc_material_preview_relief, 0.0f);
    float occlusion = std::clamp(pc_bump_occlusion, 0.0f, 1.0f) * std::min(std::max(pc_bump_strength, 0.0f), 1.0f);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float px = ((x + 0.5f) / n) * 2 - 1, py = 1 - ((y + 0.5f) / n) * 2;
            float r2 = px * px + py * py;
            uint8_t* o = &pixels[((size_t)y * n + x) * 3];
            if (r2 > 1) {
                o[0] = 28, o[1] = 28, o[2] = 32; /* the surround */
                continue;
            }
            float N[3] = { px, py, sqrtf(1 - r2) };
            float albedo[3] = { pc_material_preview_swatch, pc_material_preview_swatch, pc_material_preview_swatch };
            float rough = s.roughness, spec_v = s.specular, metal = s.metal, cav = 0;
            if (tw > 0) {
                float u = 0.5f + atan2f(N[0], N[2]) / (2 * (float)M_PI), v = 0.5f - asinf(N[1]) / (float)M_PI;
                int tx = (int)(u * tw * 2) % tw, ty = (int)(v * th * 2) % th; /* the texture twice around */
                size_t at = (size_t)ty * tw + tx;
                const uint8_t* p = &tex.rgba[at * 4];
                if (p[3]) albedo[0] = p[0] / 255.0f, albedo[1] = p[1] / 255.0f, albedo[2] = p[2] / 255.0f;
                if (tex.per_texel) {
                    rough = tex.rough[at], spec_v = tex.spec[at], metal = tex.metal[at];
                    if (tex.relief && bump > 0) {
                        /* The relief bends the normal along the texture's directions on the sphere: u around
                         * (east), v down (toward the south pole), by the slopes times the relief strength, as
                         * scaled.frag bends its shading normal (n - tangential x bump_strength). */
                        float T[3] = { N[2], 0, -N[0] }, tl = sqrtf(T[0] * T[0] + T[2] * T[2]);
                        if (tl > 1e-4f) T[0] /= tl, T[2] /= tl;
                        float B[3] = { N[1] * N[0], -(1 - N[1] * N[1]), N[1] * N[2] }, bl = sqrtf(B[0] * B[0] + B[1] * B[1] + B[2] * B[2]);
                        if (bl > 1e-4f) B[0] /= bl, B[1] /= bl, B[2] /= bl;
                        float g0 = tex.gx[at] * bump, g1 = tex.gy[at] * bump;
                        for (int c = 0; c < 3; c++) N[c] -= T[c] * g0 + B[c] * g1;
                        float nl = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
                        for (int c = 0; c < 3; c++) N[c] /= nl;
                        if (N[2] < 0.02f) { /* (never turned away from the viewer) */
                            N[2] = 0.02f;
                            nl = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
                            for (int c = 0; c < 3; c++) N[c] /= nl;
                        }
                        cav = tex.cavity[at] * occlusion;
                    }
                }
            }
            float r_b = clamp01(rough + pc_roughness_bias);
            float a = r_b * r_b;
            if (a < 0.03f) a = 0.03f;
            float a2 = a * a;
            float H[3] = { L[0], L[1], L[2] + 1 }; /* (V = +z) */
            float hl = sqrtf(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
            if (hl > 1e-6f) H[0] /= hl, H[1] /= hl, H[2] /= hl;
            float NoL = N[0] * L[0] + N[1] * L[1] + N[2] * L[2];
            float NoV = N[2], NoH = N[0] * H[0] + N[1] * H[1] + N[2] * H[2], VoH = H[2];
            if (NoL < 0) NoL = 0;
            float d = NoH * NoH * (a2 - 1) + 1;
            float D = a2 / ((float)M_PI * d * d);
            float gv = NoL * sqrtf(NoV * NoV * (1 - a2) + a2), gl = NoV * sqrtf(NoL * NoL * (1 - a2) + a2);
            float Vis = gv + gl > 0 ? 0.5f / (gv + gl) : 0;
            float fres = powf(1 - VoH, 5);
            for (int c = 0; c < 3; c++) {
                float light = li.key[c], amb = li.ambient[c] * (1 - cav);
                float f0 = 0.04f * spec_v * (1 - metal) + albedo[c] * metal;
                float f90 = spec_v * (1 - metal) + metal;
                float F = f0 + (f90 - f0) * fres;
                float diffuse = albedo[c] * (1 - metal) * (amb + NoL * light);
                float spec = (float)M_PI * D * Vis * F * NoL * light * pc_specular_strength + albedo[c] * metal * amb * F;
                float v = diffuse + spec;
                v = v / (1 + v * 0.35f); /* a soft shoulder */
                o[c] = (uint8_t)(clamp01(v) * 255 + 0.5f);
            }
        }
}
int fail(const std::string& m, char* out, int size)
{
    snprintf(out, size, "%s", m.c_str());
    return -1;
}
Json state_json()
{
    shade();
    Json j = Json::object();
    j.set("id", Json::str(shown));
    j.set("size", Json::num(pixels_size));
    j.set("texture", Json::str(texture_key_used));
    uint32_t sum = 0;
    double mean = 0;
    for (uint8_t v : pixels) sum = sum * 31 + v, mean += v;
    j.set("digest", Json::num(sum));
    char hex[16];
    snprintf(hex, sizeof hex, "%08x", sum);
    j.set("digest_hex", Json::str(hex)); /* (every bit: the number prints rounded) */
    j.set("mean", Json::num(pixels.empty() ? 0 : mean / (double)pixels.size()));
    Json centre = Json::array(), edge = Json::array();
    if (pixels_size) {
        const uint8_t* c = &pixels[((size_t)(pixels_size / 2) * pixels_size + pixels_size / 2) * 3];
        const uint8_t* e = &pixels[((size_t)(pixels_size / 2) * pixels_size + pixels_size / 8) * 3];
        for (int k = 0; k < 3; k++) centre.items.push_back(Json::num(c[k])), edge.items.push_back(Json::num(e[k]));
    }
    j.set("centre", centre), j.set("edge", edge);
    j.set("relief", Json::num(relief_used));
    j.set("planes", Json::boolean_(planes_used));
    Lighting li = lighting();
    Json light = Json::object(), key = Json::array(), dir = Json::array(), amb = Json::array();
    for (int c = 0; c < 3; c++)
        key.items.push_back(Json::num(li.key[c])), dir.items.push_back(Json::num(li.dir[c])), amb.items.push_back(Json::num(li.ambient[c]));
    light.set("room", Json::boolean_(li.room)), light.set("key_from", Json::str(li.key_from));
    light.set("key", key), light.set("dir", dir), light.set("ambient", amb);
    j.set("lighting", light);
    Json settings = Json::object();
    settings.set("material_preview_size", Json::num(pc_material_preview_size));
    settings.set("material_preview_light", Json::num(pc_material_preview_light));
    settings.set("material_preview_yaw", Json::num(pc_material_preview_yaw));
    settings.set("material_preview_pitch", Json::num(pc_material_preview_pitch));
    settings.set("material_preview_ambient", Json::num(pc_material_preview_ambient));
    settings.set("material_preview_swatch", Json::num(pc_material_preview_swatch));
    settings.set("material_preview_room", Json::boolean_(pc_material_preview_room != 0));
    settings.set("material_preview_room_key", Json::num(pc_material_preview_room_key));
    settings.set("material_preview_room_ambient", Json::num(pc_material_preview_room_ambient));
    settings.set("material_preview_room_height", Json::num(pc_material_preview_room_height));
    settings.set("material_preview_relief", Json::num(pc_material_preview_relief));
    j.set("settings", settings);
    j.set("open", Json::boolean_(pc_material_preview_open));
    return j;
}
} // namespace

extern "C" void pc_material_preview_select(const char* id) { shown = id ? id : ""; }

extern "C" void pc_material_preview_window(void)
{
    if (!pc_material_preview_open) return;
    rood_shell::dock_tool_window("Material preview");
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material preview", &pc_material_preview_open)) {
        ImGui::End();
        return;
    }
    std::vector<materials::Instance> all = materials::instances();
    if (ImGui::BeginCombo("Instance", shown.empty() ? "(none)" : shown.c_str())) {
        for (const materials::Instance& i : all) {
            if (i.preview) continue;
            if (ImGui::Selectable(i.id.c_str(), shown == i.id)) shown = i.id;
        }
        ImGui::EndCombo();
    }
    shade();
    float px = ImGui::GetContentRegionAvail().x;
    if (px > 400) px = 400;
    if (pixels_size > 0) {
        float cell = px / pixels_size;
        ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int y = 0; y < pixels_size; y++)
            for (int x = 0; x < pixels_size; x++) {
                const uint8_t* p = &pixels[((size_t)y * pixels_size + x) * 3];
                ImVec2 a(o.x + x * cell, o.y + y * cell);
                dl->AddRectFilled(a, ImVec2(a.x + cell + 0.5f, a.y + cell + 0.5f), IM_COL32(p[0], p[1], p[2], 255));
            }
        ImGui::Dummy(ImVec2(px, px));
    }
    if (!shown.empty()) {
        std::map<std::string, float> values;
        if (materials::resolve(shown, values, nullptr, nullptr)) {
            materials::Surface s = materials::surface_of(values);
            ImGui::Text("rough %.2f  spec %.2f  metal %.2f  relief x%.2f%s", s.roughness, s.specular, s.metal, s.bump,
                s.auto_height ? " (estimated)" : "");
        }
        ImGui::TextDisabled("%s", texture_key_used.empty() ? "swatch (the texture is not drawn here)" : ("texture " + texture_key_used).c_str());
        if (!texture_key_used.empty())
            ImGui::TextDisabled("%s%s", relief_used == 1 ? "relief: height map" : relief_used == 2 ? "relief: estimated" : "no relief",
                planes_used ? ", as drawn here" : ", its painted maps");
    }
    Lighting li = lighting();
    bool room = pc_material_preview_room != 0;
    if (ImGui::Checkbox("Lit like the room", &room)) pc_material_preview_room = room;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The key light: the room's strongest light (or its sun) at Ashley, seen from the game camera;\n"
                          "the ambient: the game's own room light there. Off: the preview's own light below.");
    if (room)
        ImGui::TextDisabled("%s", !li.room ? "(no room here: the preview's own light)"
            : li.key_from == "preview" ? "key: no light reaches Ashley (the preview's own); ambient: the room's"
            : ("key: " + li.key_from + "; ambient: the room's").c_str());
    if (ImGui::TreeNode("Light")) {
        if (room) {
            ImGui::SliderFloat("Room key x", &pc_material_preview_room_key, 0, 4, "%.2f");
            ImGui::SliderFloat("Room ambient x", &pc_material_preview_room_ambient, 0, 2, "%.2f");
            ImGui::SliderFloat("Height above Ashley", &pc_material_preview_room_height, 0, 200, "%.0f");
        }
        ImGui::SliderFloat("Key light", &pc_material_preview_light, 0, 4, "%.2f");
        ImGui::SliderFloat("Yaw", &pc_material_preview_yaw, -180, 180, "%.0f deg");
        ImGui::SliderFloat("Pitch", &pc_material_preview_pitch, -89, 89, "%.0f deg");
        ImGui::SliderFloat("Ambient", &pc_material_preview_ambient, 0, 1, "%.2f");
        ImGui::SliderFloat("Relief", &pc_material_preview_relief, 0, 4, "%.2f");
        ImGui::SliderFloat("Swatch grey", &pc_material_preview_swatch, 0, 1, "%.2f");
        ImGui::SliderFloat("Pixels", &pc_material_preview_size, 16, 256, "%.0f");
        ImGui::TextDisabled("Settings keys material_preview_* (RPC graphics)");
        ImGui::TreePop();
    }
    ImGui::End();
}

/* RPC material_preview: {action: state (default) | select {id} | open {open} | save {path} (a PNG)}; the look is set by RPC
 * graphics (material_preview_size, _light, _yaw, _pitch, _ambient, _swatch, _room, _room_key,
 * _room_ambient, _room_height, _relief). */
extern "C" int pc_material_preview_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "state";
    if (action == "select") {
        const Json* id = params.get("id");
        std::string s = id && id->type == Json::String ? id->string : "";
        if (!s.empty() && !materials::find_instance(s)) return fail("unknown instance " + s, out, size);
        shown = s;
    } else if (action == "open") {
        const Json* o = params.get("open");
        pc_material_preview_open = !o || o->type != Json::Bool || o->boolean;
    } else if (action == "save") {
        /* The sphere as a PNG (a path the caller names: a picture of game art, never committed). */
        const Json* path = params.get("path");
        if (!path || path->type != Json::String || path->string.empty()) return fail("save needs path", out, size);
        shade();
        std::vector<uint8_t> rgba((size_t)pixels_size * pixels_size * 4, 255);
        for (size_t k = 0; k < (size_t)pixels_size * pixels_size; k++) memcpy(&rgba[k * 4], &pixels[k * 3], 3);
        if (!materials::write_png(path->string, pixels_size, pixels_size, 4, rgba.data(), &error)) return fail(error, out, size);
    } else if (action != "state")
        return fail("unknown action " + action, out, size);
    std::string text = prefab::json_write(state_json(), 0);
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end()); /* one line: the RPC is line-based */
    if ((int)text.size() >= size) return fail("reply too large", out, size);
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}
