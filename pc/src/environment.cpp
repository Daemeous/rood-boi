// The room's environment: sun (or moon) and sky (environment.h). Per room,
// from the lighting layers' `env sun` and `env sky` items; the editor's
// Atmosphere tab edits them (with a sun direction gizmo in the view), RPC
// `environment` reads and sets them, the renderers read pc_env_gpu_get.
#include "vram_texels.h"
#include "environment.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "imgui.h"
#include "editor_schema.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lighting_layers.h"
#include "lighting_quality.h"
#include "lights_internal.h"
#include "particles_internal.h"
#include "scene_capture.h"
#include "present_guard.h" /* PC_PRESENT_GUARD (T1 R4a) */

extern "C" {
extern int pc_lights_enabled;
void pc_lights_mark_dirty(void);
/* Room weather (2842C.c); scripts can turn rain on/off mid-scene. */
int pc_weather_raining(void);
unsigned pc_frame_vsync_count(void);
/* The game's sky (BATTLE 146C.c; pc/docs/Vulkan_Renderer.md "The game's sky"): the
 * background sphere's five ring colours (0x00BBGGRR: zenith, 26.5 degrees up, the
 * horizon, 26.5 down, the nadir), set from the room's section B by vs_field_initializeRoomBackgroundColors and
 * animated by the scripts' background fades (main.c vs_render_transformBackgroundGradient); the flag of a
 * room whose five are equal (vs_field_isSolidBackground: a flat backdrop, one texel of VRAM tinted by
 * D_800F1BB0, instead of the sphere). Weak: builds without BATTLE have no sky. */
extern int vs_field_backgroundGradientColors[5] __attribute__((weak));
extern char vs_field_isSolidBackground __attribute__((weak));
typedef struct {
    signed char unk0;
    signed char unk1;
    signed char unk2;
    char unk3;
    short unk4;
    short unk6;
} vs_field_RoomTint;
extern vs_field_RoomTint D_800F1BB0 __attribute__((weak));
/* The sky panorama (2842C.h; vs_field_drawRoomBackgroundTiles), drawn instead of the sphere while a
 * script has it on (vs_field_isTiledBackgroundEnabled) and its layout is loaded (vs_field_loadedBackgroundLayout): tiles of 32 x
 * 32 screen pixels, each a texture rectangle of tpage vs_field_legacyBackgroundTexturePage | (colors[1].cd << 7)
 * through CLUT 0x3BB0, its corners' colours modulating it; a black TILE behind. */
typedef struct {
    short x, y, w, h; /* RECT uv */
    unsigned char colors[4][4]; /* CVECTOR r, g, b, cd */
} vs_battle_backgroundTile;
typedef struct {
    int width;
    int height;
    int unk8;
    vs_battle_backgroundTile tiles[];
} vs_battle_backgroundLayout;
extern char vs_field_legacyBackgroundTexturePage __attribute__((weak));
extern unsigned char vs_field_isTiledBackgroundEnabled __attribute__((weak));
extern vs_battle_backgroundLayout* vs_field_loadedBackgroundLayout __attribute__((weak));
const uint16_t* pc_renderer_vram(void);
}

int pc_env_global = 1;

namespace {
const float DEG = 3.14159265f / 180.0f;

/* Defaults: the schema's (lighting_layers.cpp kind "env"). */
struct Sun {
    bool defined = false, enabled = true;
    float azimuth = 180, elevation = 45; /* degrees: azimuth from +z toward +x, elevation above the horizon */
    float color[3] = { 1, 1, 1 };
    float intensity = 1;
    float angle = 0.53f; /* angular diameter, degrees (the sun's own: 0.53) */
    bool shadows = true;
};
struct Sky {
    bool defined = false, enabled = true;
    float zenith[3] = { 0.1f, 0.12f, 0.2f }, horizon[3] = { 0.25f, 0.25f, 0.3f }, ground[3] = { 0.05f, 0.05f, 0.05f };
    float intensity = 1;
    float sun_tint = 0;
    float ambient = 0.5f; /* the non-traced fallback's strength */
    bool manual = false; /* source=manual: the three colours above; game: the game's sky where the room has one */
    float game_scale = 0.25f; /* the game's colours (0..255 over 255) times this */
};
Sun sun;
Sky sky;
bool gizmo = true; /* the sun's icon in the editor's view (its handles: editor_light_handles.cpp) */

std::string num(double v, int d) { return lighting::format_number(v, d); }
std::string vec3(const float* v, int d) { return num(v[0], d) + "," + num(v[1], d) + "," + num(v[2], d); }
bool get3(const lighting::Item& it, const char* key, float* out)
{
    const std::string* v = it.get(key);
    std::string text = v ? *v : lighting::field_default(it.kind, key);
    float x[3];
    if (sscanf(text.c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) != 3)
        return false;
    memcpy(out, x, sizeof x);
    return true;
}
float get1(const lighting::Item& it, const char* key)
{
    const std::string* v = it.get(key);
    return (float)atof(v ? v->c_str() : lighting::field_default(it.kind, key));
}
void direction(float azimuth, float elevation, float out[3])
{
    float a = azimuth * DEG, e = elevation * DEG;
    out[0] = cosf(e) * sinf(a);
    out[1] = -sinf(e); /* y is down */
    out[2] = cosf(e) * cosf(a);
}
/* The game's own sky this frame (see the externs above). Colours 0..1 as the PS1
 * draws them (255 = full), before the sky's game_scale and intensity. */
struct GameSky {
    int kind = 0; /* 0 none (no data, or all black: the room draws no sky), 1 the sphere, 2 the flat backdrop,
                   * 3 the panorama */
    float ring[5][3] = {}; /* the sphere's rings, zenith to nadir (the flat backdrop: all five its colour; the
                            * panorama: its top, middle and bottom tile rows' means, then black) */
    float zenith[3] = {}, horizon[3] = {}, ground[3] = {};
};
GameSky game_sky_read()
{
    GameSky g;
    if (!&vs_field_backgroundGradientColors[0] || !&vs_field_isSolidBackground)
        return g;
    if (&vs_field_isTiledBackgroundEnabled && &vs_field_loadedBackgroundLayout && vs_field_isTiledBackgroundEnabled && vs_field_loadedBackgroundLayout) {
        /* The panorama (vs_field_drawRoomBackground draws it instead of the sphere): each tile row's mean
         * colour as drawn (texels through the CLUT times the corners' colours / 128). The top
         * row is the zenith's, the bottom row the horizon's; below it the black TILE. */
        const vs_battle_backgroundLayout* layout = vs_field_loadedBackgroundLayout;
        if ((unsigned)(layout->width - 1) >= 31 || (unsigned)(layout->height - 1) >= 31)
            return g;
        const uint16_t* vram = pc_renderer_vram();
        float rows[32][3] = {};
        int filled[32] = {};
        for (int r = 0; r < layout->height; r++) {
            double sum[3] = { 0, 0, 0 };
            int n = 0;
            for (int c = 0; c < layout->width; c++) {
                const vs_battle_backgroundTile& t = layout->tiles[r * layout->width + c];
                if (!t.w && !t.h)
                    continue;
                unsigned tpage = (unsigned char)vs_field_legacyBackgroundTexturePage | (t.colors[1][3] << 7);
                double tex[3] = { 0, 0, 0 };
                int texels = 0;
                for (int v = t.y; v < t.y + t.h; v += 2)
                    for (int u = t.x; u < t.x + t.w; u += 2) {
                        /* (the background's colours sit in the CLUT row at (768, 238): clut word 238 << 6 | 48) */
                        uint16_t col = (uint16_t)pc_tex_colour(vram, tpage, 238u << 6 | 48, u, v);
                        if (!col)
                            continue; /* transparent */
                        tex[0] += (col & 31) * 8, tex[1] += ((col >> 5) & 31) * 8, tex[2] += ((col >> 10) & 31) * 8;
                        ++texels;
                    }
                if (!texels)
                    continue;
                for (int k = 0; k < 3; k++) {
                    float corner = (t.colors[0][k] + t.colors[1][k] + t.colors[2][k] + t.colors[3][k]) / 4.0f;
                    sum[k] += fmin(tex[k] / texels * corner / 128.0, 255.0) / 255.0;
                }
                ++n;
            }
            if (n) {
                for (int k = 0; k < 3; k++)
                    rows[r][k] = (float)(sum[k] / n);
                filled[r] = 1;
            }
        }
        int top = -1, bottom = -1;
        for (int r = 0; r < layout->height; r++)
            if (filled[r]) {
                if (top < 0)
                    top = r;
                bottom = r;
            }
        if (top < 0)
            return g;
        int mid = (top + bottom) / 2;
        memcpy(g.ring[0], rows[top], sizeof rows[top]);
        memcpy(g.ring[1], rows[mid], sizeof rows[mid]);
        memcpy(g.ring[2], rows[bottom], sizeof rows[bottom]);
        g.kind = 3;
    } else if (vs_field_isSolidBackground) {
        /* vs_field_drawRoomBackground: a 320 x 240 POLY_FT4 at u,v 248..249 of tpage 0x11 (4-bit, texture
         * page x 64, y 256) through CLUT 0x38B1 (x 784, y 226), tinted 128 + 4 x D_800F1BB0. */
        const uint16_t* vram = pc_renderer_vram();
        float texel[3] = { 0, 0, 0 };
        int seen = 0;
        for (int v = 248; v < 250; v++)
            for (int u = 248; u < 250; u++) {
                /* tpage 0x11 (4-bit, page x 64, y 256) through CLUT 0x38B1 (x 784, y 226) */
                uint16_t c = (uint16_t)pc_tex_colour(vram, 0x11, 0x38B1, u, v);
                if (!c)
                    continue; /* transparent: not drawn */
                texel[0] += (c & 31) * 8, texel[1] += ((c >> 5) & 31) * 8, texel[2] += ((c >> 10) & 31) * 8;
                ++seen;
            }
        if (!seen)
            return g;
        int tint[3] = { 128, 128, 128 };
        if (&D_800F1BB0 && D_800F1BB0.unk6) {
            tint[0] = 128 + D_800F1BB0.unk0 * 4, tint[1] = 128 + D_800F1BB0.unk1 * 4, tint[2] = 128 + D_800F1BB0.unk2 * 4;
            for (int& t : tint)
                t = t < 0 ? 0 : t > 255 ? 255 : t;
        }
        float rgb[3];
        for (int k = 0; k < 3; k++)
            rgb[k] = fminf(texel[k] / seen * tint[k] / 128.0f, 255.0f) / 255.0f;
        for (int r = 0; r < 5; r++)
            memcpy(g.ring[r], rgb, sizeof rgb);
        g.kind = 2;
    } else {
        unsigned any = 0;
        for (int r = 0; r < 5; r++) {
            unsigned c = (unsigned)vs_field_backgroundGradientColors[r] & 0xffffff;
            any |= c;
            g.ring[r][0] = (c & 255) / 255.0f, g.ring[r][1] = ((c >> 8) & 255) / 255.0f, g.ring[r][2] = (c >> 16) / 255.0f;
        }
        if (!any)
            return g;
        g.kind = 1;
    }
    if (!(g.ring[0][0] + g.ring[0][1] + g.ring[0][2] + g.ring[1][0] + g.ring[1][1] + g.ring[1][2] + g.ring[2][0]
            + g.ring[2][1] + g.ring[2][2] + g.ring[3][0] + g.ring[3][1] + g.ring[3][2] + g.ring[4][0] + g.ring[4][1]
            + g.ring[4][2] > 0.0f)) {
        g.kind = 0; /* drawn black: no sky to light with */
        return g;
    }
    /* The gradient's three: the zenith ring, the horizon ring, and below it the mean of the
     * lower ring and the nadir (env_sky reaches the ground colour 14.5 degrees down). The
     * ring 26.5 degrees up is what the zenith -> horizon blend gives there (within ~10 %
     * in the town's sky); a directional map would keep it exactly (the doc's design). */
    for (int k = 0; k < 3; k++) {
        g.zenith[k] = g.ring[0][k];
        g.horizon[k] = g.ring[2][k];
        g.ground[k] = 0.5f * (g.ring[3][k] + g.ring[4][k]);
    }
    return g;
}
/* The sky is read from the renderer's VRAM and the game's tables, which move with how far the
 * render thread has got, not with the game: read once per game frame so every present of that
 * frame (paused redraws, interpolation images) lights with the same sky. */
GameSky game_sky()
{
    static GameSky cached;
    static unsigned cached_frame = ~0u;
    unsigned frame = pc_frame_vsync_count();
    if (frame != cached_frame) {
        cached = game_sky_read();
        cached_frame = frame;
    }
    return cached;
}
const char* game_sky_kind(int kind)
{
    return kind == 1 ? "sphere" : kind == 2 ? "flat" : kind == 3 ? "panorama" : "none";
}
/* The colours the sky uses (before intensity): the game's (times game_scale) or the room's own.
 * The ground is always the room's own: light from below the horizon is the terrain's, and the
 * game's lower rings are only what it draws past the set (the forest's flat grey lit shadowed
 * air from below and washed out its shafts). */
bool sky_colours(float zenith[3], float horizon[3], float ground[3], GameSky* game = nullptr)
{
    GameSky g;
    if (!sky.manual)
        g = game_sky();
    if (game)
        *game = g;
    for (int k = 0; k < 3; k++) {
        zenith[k] = g.kind ? g.zenith[k] * sky.game_scale : sky.zenith[k];
        horizon[k] = g.kind ? g.horizon[k] * sky.game_scale : sky.horizon[k];
        ground[k] = sky.ground[k];
    }
    return g.kind != 0;
}
} // namespace

namespace rood_env {

void reset()
{
    sun = Sun();
    sky = Sky();
    rood_atmo::reset();
    rood_particles::reset();
    rood_post::reset();
}

bool apply_item(const lighting::Item& it)
{
    if (rood_atmo::apply_item(it)) /* env fog, fog volumes */
        return true;
    if (rood_particles::apply_item(it)) /* particle emitters (particles.cpp) */
        return true;
    if (rood_post::apply_item(it)) /* post room, post cutscene, post event.<id> */
        return true;
    if (it.kind != "env")
        return false;
    if (it.name == "sun") {
        sun = Sun();
        sun.defined = true;
        sun.enabled = get1(it, "enabled") != 0;
        sun.azimuth = get1(it, "azimuth");
        sun.elevation = get1(it, "elevation");
        get3(it, "color", sun.color);
        sun.intensity = get1(it, "intensity");
        sun.angle = get1(it, "angle");
        sun.shadows = get1(it, "shadows") != 0;
        return true;
    }
    if (it.name == "sky") {
        sky = Sky();
        sky.defined = true;
        sky.enabled = get1(it, "enabled") != 0;
        get3(it, "zenith", sky.zenith);
        get3(it, "horizon", sky.horizon);
        get3(it, "ground", sky.ground);
        sky.intensity = get1(it, "intensity");
        sky.sun_tint = get1(it, "sun_tint");
        sky.ambient = get1(it, "ambient");
        const std::string* src = it.get("source");
        sky.manual = src && *src == "manual";
        sky.game_scale = get1(it, "game_scale");
        return true;
    }
    return false;
}

void add_items(lighting::State& s)
{
    if (sun.defined) {
        lighting::Item it;
        it.kind = "env", it.name = "sun";
        it.set("enabled", sun.enabled ? "1" : "0");
        it.set("azimuth", num(sun.azimuth, 2));
        it.set("elevation", num(sun.elevation, 2));
        it.set("color", vec3(sun.color, 3));
        it.set("intensity", num(sun.intensity, 3));
        it.set("angle", num(sun.angle, 2));
        it.set("shadows", sun.shadows ? "1" : "0");
        lighting::canonicalize(it);
        s[it.id()] = it;
    }
    if (sky.defined) {
        lighting::Item it;
        it.kind = "env", it.name = "sky";
        it.set("enabled", sky.enabled ? "1" : "0");
        it.set("zenith", vec3(sky.zenith, 3));
        it.set("horizon", vec3(sky.horizon, 3));
        it.set("ground", vec3(sky.ground, 3));
        it.set("intensity", num(sky.intensity, 3));
        it.set("sun_tint", num(sky.sun_tint, 3));
        it.set("ambient", num(sky.ambient, 3));
        it.set("source", sky.manual ? "manual" : "game");
        it.set("game_scale", num(sky.game_scale, 3));
        lighting::canonicalize(it);
        s[it.id()] = it;
    }
    rood_atmo::add_items(s);
    rood_particles::add_items(s);
    rood_post::add_items(s);
}

/* Undo snapshots (editor_undo.cpp): the items as lines, and back. */
std::string text()
{
    lighting::State s;
    add_items(s);
    std::string out;
    for (const auto& kv : s) {
        lighting::Entry e;
        e.verb = lighting::DEFINE;
        e.id = kv.first;
        e.item = kv.second;
        out += lighting::format_entry(e) + "\n";
    }
    return out;
}
void set_text(const std::string& t)
{
    reset();
    lighting::File f;
    if (!lighting::parse_text("rood-lighting 2\n" + t, f))
        return;
    for (const lighting::Entry& e : f.entries)
        if (e.verb == lighting::DEFINE)
            apply_item(e.item);
}

} // namespace rood_env

rood_env::SunState rood_env::sun_state()
{
    SunState s;
    s.defined = sun.defined, s.enabled = sun.enabled, s.azimuth = sun.azimuth, s.elevation = sun.elevation;
    memcpy(s.color, sun.color, sizeof s.color);
    s.intensity = sun.intensity, s.angle = sun.angle, s.shadows = sun.shadows;
    return s;
}
void rood_env::set_sun_state(const SunState& s)
{
    sun.defined = s.defined, sun.enabled = s.enabled, sun.azimuth = s.azimuth, sun.elevation = s.elevation;
    memcpy(sun.color, s.color, sizeof sun.color);
    sun.intensity = s.intensity, sun.angle = s.angle, sun.shadows = s.shadows;
    pc_lights_mark_dirty();
}
bool rood_env::sun_icon_shown() { return gizmo; }

/* ---- renderers ---- */

/* The room is open to the sky: its environment defines a sun (or moon) or a sky, on or off for the
 * weather (the baked shadow, lights.cpp, applies only there). */
extern "C" int pc_env_open_sky(void)
{
    rood_lights::track_room();
    return (sun.defined && sun.enabled) || (sky.defined && sky.enabled);
}

extern "C" int pc_env_gpu_get(pc_env_gpu* out)
{
    PC_PRESENT_GUARD("pc_env_gpu_get");
    memset(out, 0, sizeof *out);
    rood_lights::track_room(); /* the current room's items */
    if (!pc_env_global || !pc_lights_enabled)
        return 0;
    int on = 0;
    float d[3];
    direction(sun.azimuth, sun.elevation, d);
    memcpy(out->sun_dir, d, sizeof d);
    out->sun_dir[3] = fmaxf(sun.angle, 0.0f) * 0.5f * DEG;
    /* The generated outdoor directional light is moonlight. Rain is an
     * overcast night, not a clear sky: retain the sky fill but suppress the
     * moon and its shadows while the original weather is raining. */
    if (sun.defined && sun.enabled && sun.intensity > 0 && !pc_weather_raining()) {
        for (int k = 0; k < 3; k++)
            out->sun_col[k] = fmaxf(sun.color[k], 0.0f) * sun.intensity;
        out->sun_col[3] = sun.shadows && pc_shadow_mode != PC_SHADOWS_OFF ? 1.0f : 0.0f; /* quality: Shadows Off */
        on = 1;
    }
    if (sky.defined && sky.enabled && sky.intensity > 0) {
        /* The game's sky where the room has one (source=game), else the manual colours: a
         * room without game sky data gives exactly the record it gave before. */
        float zenith[3], horizon[3], ground[3];
        sky_colours(zenith, horizon, ground);
        for (int k = 0; k < 3; k++) {
            out->zenith[k] = fmaxf(zenith[k], 0.0f) * sky.intensity;
            out->horizon[k] = fmaxf(horizon[k], 0.0f) * sky.intensity;
            out->ground[k] = fmaxf(ground[k], 0.0f) * sky.intensity;
        }
        out->zenith[3] = 1;
        out->horizon[3] = fmaxf(sky.sun_tint, 0.0f);
        out->ground[3] = fmaxf(sky.ambient, 0.0f);
        on = 1;
    }
    return on;
}

/* The game's rain is projected PS1 lines and splashes (146C.c func_8008F440),
 * not port emitters. A drop is left out of the picture (never out of the
 * game's state) when solid room geometry hides it:
 * - a roof above it: the original drew drops indoors too (its rooms rarely
 *   show them at its cameras; a free camera does);
 * - a surface between the eye and it: the PS1's ordering table put a drop
 *   behind the nearer wall, but the port draws its lines as screen-space
 *   furniture after the room's native meshes, so rain outside a room showed
 *   through its walls.
 * Room triangles already have the game's transforms applied by scene_capture;
 * translucent ones (glass, glows) hide nothing. */
extern "C" {
long pc_weather_drops, pc_weather_drops_hidden; /* asked, hidden (RPC render_stats rain_drops*) */
}
static int drop_hidden(int x, int y, int z);
/* The game still draws every drop (native VRAM, the game's state and its replays stay the PS1's); the hardware picture
 * leaves the hidden ones out (the drop's item is marked PC_ITEM_HW_HIDDEN by 146C.c func_8008F440). */
extern "C" int pc_weather_drop_hidden(int x, int y, int z)
{
    ++pc_weather_drops;
    if (!drop_hidden(x, y, z))
        return 0;
    ++pc_weather_drops_hidden;
    return 1;
}
static int drop_hidden(int x, int y, int z)
{
    if (!pc_scene_room_data()) /* a room transition, not this frame's geometry */
        return 0;
    struct Solid {
        float a[3], ab[3], ac[3];
        float lo[3], hi[3];
        float inv_det; /* the (x, z) projection's, 0 for walls: roofs only where it is not */
    };
    static int serial = -1;
    static std::vector<Solid> solids;
    if (serial != pc_scene_serial()) {
        serial = pc_scene_serial();
        solids.clear();
        int count = 0;
        const scene_triangle* tris = pc_scene_triangle_data(&count);
        for (int i = 0; i < count && tris; ++i) {
            const scene_triangle& t = tris[i];
            if (t.code & 2)
                continue;
            const float *a = t.vertex[0].position, *b = t.vertex[1].position, *c = t.vertex[2].position;
            Solid r;
            for (int k = 0; k < 3; k++) {
                r.a[k] = a[k], r.ab[k] = b[k] - a[k], r.ac[k] = c[k] - a[k];
                r.lo[k] = fminf(a[k], fminf(b[k], c[k])), r.hi[k] = fmaxf(a[k], fmaxf(b[k], c[k]));
            }
            float det = r.ab[0] * r.ac[2] - r.ab[2] * r.ac[0];
            r.inv_det = fabsf(det) < 1e-3f ? 0.0f : 1.0f / det;
            solids.push_back(r);
        }
    }
    const float p[3] = { (float)x, (float)y, (float)z };
    /* A roof: a surface above the drop (negative y is up). */
    for (const Solid& r : solids) {
        if (!r.inv_det || p[0] < r.lo[0] || p[0] > r.hi[0] || p[2] < r.lo[2] || p[2] > r.hi[2] || r.lo[1] >= p[1] - 2)
            continue;
        const float px = p[0] - r.a[0], pz = p[2] - r.a[2];
        const float u = (px * r.ac[2] - pz * r.ac[0]) * r.inv_det;
        const float v = (r.ab[0] * pz - r.ab[2] * px) * r.inv_det;
        if (u >= 0 && v >= 0 && u + v <= 1 && r.a[1] + u * r.ab[1] + v * r.ac[1] < p[1] - 2)
            return 1;
    }
    /* A surface between the eye (the GTE camera the frame is built with:
     * the free camera's while one flies) and the drop. */
    float rot[3][3], tr[3], inv[3][3], h, ofx, ofy;
    if (!pc_scene_game_camera(rot, tr, &h, &ofx, &ofy) || !pc_scene_game_inverse(inv))
        return 0;
    float eye[3], d[3], lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        eye[k] = -(inv[k][0] * tr[0] + inv[k][1] * tr[1] + inv[k][2] * tr[2]);
        d[k] = p[k] - eye[k];
        lo[k] = fminf(eye[k], p[k]), hi[k] = fmaxf(eye[k], p[k]);
    }
    for (const Solid& r : solids) {
        if (r.hi[0] < lo[0] || r.lo[0] > hi[0] || r.hi[1] < lo[1] || r.lo[1] > hi[1] || r.hi[2] < lo[2]
            || r.lo[2] > hi[2])
            continue;
        /* Moller-Trumbore, either facing; the drop's own end excluded. */
        const float q[3] = { d[1] * r.ac[2] - d[2] * r.ac[1], d[2] * r.ac[0] - d[0] * r.ac[2],
            d[0] * r.ac[1] - d[1] * r.ac[0] };
        const float det = r.ab[0] * q[0] + r.ab[1] * q[1] + r.ab[2] * q[2];
        if (fabsf(det) < 1e-6f)
            continue;
        const float f = 1.0f / det;
        const float s[3] = { eye[0] - r.a[0], eye[1] - r.a[1], eye[2] - r.a[2] };
        const float u = f * (s[0] * q[0] + s[1] * q[1] + s[2] * q[2]);
        if (u < 0 || u > 1)
            continue;
        const float w[3] = { s[1] * r.ab[2] - s[2] * r.ab[1], s[2] * r.ab[0] - s[0] * r.ab[2],
            s[0] * r.ab[1] - s[1] * r.ab[0] };
        const float v = f * (d[0] * w[0] + d[1] * w[1] + d[2] * w[2]);
        if (v < 0 || u + v > 1)
            continue;
        const float t = f * (r.ac[0] * w[0] + r.ac[1] * w[1] + r.ac[2] * w[2]);
        if (t > 1e-3f && t < 0.98f)
            return 1;
    }
    return 0; /* open sky, in plain view */
}

extern "C" unsigned pc_env_hash(void)
{
    PC_PRESENT_GUARD("pc_env_hash");
    pc_env_gpu e;
    pc_env_gpu_get(&e);
    const unsigned char* b = (const unsigned char*)&e;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < sizeof e; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

/* ---- editor: the Atmosphere tab ---- */

namespace {
/* A colour edit plus its intensity as one row. */
bool color_row(const char* label, float* rgb)
{
    return ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}
void tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
} // namespace

bool pc_atmosphere_page(void); /* atmosphere.cpp */
bool pc_particles_page(void); /* particles_editor.cpp */
/* Returns true when something changed (the room has unsaved edits). */
extern "C" bool pc_environment_page(void)
{
    rood_lights::track_room();
    bool changed = false;
    ImGui::PushID("environment");
    ImGui::SeparatorText("Sun and sky (this room)");
    if (!sun.defined && !sky.defined) {
        ImGui::TextDisabled("No sun or sky here (a roofed room; the generator gives\n"
                            "rooms open to the sky a moon and an overcast night sky).");
        if (ImGui::Button("Add sun and sky")) {
            sun = Sun();
            sky = Sky();
            sun.defined = sky.defined = true;
            sun.azimuth = 205, sun.elevation = 38, sun.intensity = 0.55f, sun.angle = 1.5f;
            sun.color[0] = 0.62f, sun.color[1] = 0.70f, sun.color[2] = 0.95f;
            changed = true;
            pc_undo_hint("Add sun and sky");
        }
    } else {
        if (!sun.defined) {
            if (ImGui::Button("Add a sun")) {
                sun = Sun();
                sun.defined = changed = true;
            }
        } else {
            ImGui::PushID("sun");
            changed |= ImGui::Checkbox("Sun / moon", &sun.enabled);
            tooltip("A directional light from far away: parallel rays, soft shadows from its angular size.\n"
                    "Ray-traced shadows (Vulkan ray tracing, OpenGL compute); where nothing is traced it lights\n"
                    "unshadowed (pc/docs/Vulkan_Renderer.md, \"Sun and sky\").");
            ImGui::BeginDisabled(!sun.enabled);
            changed |= ImGui::SliderFloat("Azimuth", &sun.azimuth, 0, 360, "%.1f deg");
            tooltip("Compass direction it shines from: 0 = from +z, 90 = from +x. Drag the sun in the view too.");
            changed |= ImGui::SliderFloat("Elevation", &sun.elevation, 0, 90, "%.1f deg");
            changed |= color_row("Colour##sun", sun.color);
            changed |= ImGui::SliderFloat("Intensity##sun", &sun.intensity, 0, rood_env::SUN_INTENSITY_MAX, "%.3f");
            changed |= ImGui::SliderFloat("Angular size", &sun.angle, 0, 10, "%.2f deg");
            tooltip("Angular diameter: 0.53 is the real sun's; larger gives softer shadow edges (an overcast moon).");
            changed |= ImGui::Checkbox("Casts shadows", &sun.shadows);
            ImGui::Checkbox("Sun in the view", &gizmo);
            tooltip("Its disc on a sky dome in the view: click it, then drag it (azimuth and elevation), its ring\n"
                    "(intensity) or its swatch (colour).");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (!sky.defined) {
            if (ImGui::Button("Add a sky")) {
                sky = Sky();
                sky.defined = changed = true;
            }
        } else {
            ImGui::PushID("sky");
            changed |= ImGui::Checkbox("Sky", &sky.enabled);
            tooltip("Light from the open sky: rays of the indirect light that leave the room bring the sky's\n"
                    "radiance (zenith -> horizon -> ground gradient); what blocks it occludes it.");
            ImGui::BeginDisabled(!sky.enabled);
            int source = sky.manual ? 1 : 0;
            if (ImGui::Combo("Colours from", &source, "The game's sky\0Manual\0")) {
                sky.manual = source == 1;
                changed = true;
            }
            tooltip("The game's sky: the colours of the background the game draws behind this room (its gradient\n"
                    "sphere, or the flat backdrop of the forest), live (the scripts' fades change them), times\n"
                    "Game scale; the ground stays the room's own. Where the room draws no sky (black) the\n"
                    "manual colours are used. Manual: the three colours below.");
            float gz[3], gh[3], gg[3];
            GameSky game;
            bool from_game = sky_colours(gz, gh, gg, &game);
            if (!sky.manual) {
                if (from_game) {
                    ImGui::Text("Game sky: %s", game.kind == 1 ? "gradient sphere" : game.kind == 2 ? "flat backdrop" : "panorama");
                    ImGui::SameLine();
                    for (int r = 0; r < 5; r++) {
                        char id[16];
                        snprintf(id, sizeof id, "##ring%d", r);
                        ImGui::ColorButton(id, ImVec4(game.ring[r][0], game.ring[r][1], game.ring[r][2], 1),
                            ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
                        if (r < 4)
                            ImGui::SameLine(0, 2);
                    }
                    tooltip("As the game draws it: the sphere's five rings (zenith, 26.5 degrees up, horizon, 26.5 down, nadir);\n"
                            "the panorama's top, middle and bottom tile rows (their mean colours), then black.");
                    changed |= ImGui::SliderFloat("Game scale", &sky.game_scale, 0, 2, "%.3f");
                    tooltip("The game's colours (0..1) times this are the sky's radiance (then times Intensity).");
                    if (ImGui::SmallButton("Copy to manual")) {
                        memcpy(sky.zenith, gz, sizeof gz);
                        memcpy(sky.horizon, gh, sizeof gh);
                        memcpy(sky.ground, gg, sizeof gg);
                        sky.manual = changed = true;
                        pc_undo_hint("Sky colours from the game");
                    }
                    tooltip("Sets the manual colours to these and switches to them (to edit from here).");
                } else
                    ImGui::TextDisabled("No game sky here (the room draws none): the manual colours.");
            }
            ImGui::BeginDisabled(from_game);
            changed |= color_row("Zenith", sky.zenith);
            changed |= color_row("Horizon", sky.horizon);
            ImGui::EndDisabled();
            changed |= color_row("Ground", sky.ground);
            tooltip("Light from below the horizon (the terrain's): the room's own, with either source.");
            changed |= ImGui::SliderFloat("Intensity##sky", &sky.intensity, 0, 4, "%.3f");
            changed |= ImGui::SliderFloat("Sun tint", &sky.sun_tint, 0, 1, "%.2f");
            tooltip("How much the sky around the sun takes its colour.");
            changed |= ImGui::SliderFloat("Ambient (no ray tracing)", &sky.ambient, 0, 1, "%.2f");
            tooltip("Where nothing traces the sky (shadow maps without GI), a hemisphere ambient term of this\n"
                    "strength stands in for it: upward faces get the sky, downward ones the ground.");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Remove sun and sky")) {
            sun = Sun();
            sky = Sky();
            changed = true;
            pc_undo_hint("Remove sun and sky");
        }
        tooltip("Deletes them from this room (saved as delete lines when a pack defines them).");
    }
    bool global = pc_env_global != 0;
    if (ImGui::Checkbox("Sun and sky on (all rooms)", &global))
        pc_env_global = global;
    tooltip("Switches every room's sun and sky off for comparison (not saved; RPC environment {global}).");
    ImGui::PopID();
    changed |= pc_atmosphere_page(); /* ground fog and fog volumes (atmosphere.cpp) */
    changed |= pc_particles_page(); /* dust and particles */
    if (changed)
        pc_lights_mark_dirty();
    return changed;
}

/* ---- RPC environment ---- */

namespace {
std::string jbool(bool b) { return b ? "true" : "false"; }
std::string jvec(const float* v, int n)
{
    std::string s = "[";
    for (int i = 0; i < n; i++)
        s += (i ? "," : "") + lighting::format_number(v[i], 4);
    return s + "]";
}
void read_vec(const prefab::Json* j, float* out, int n)
{
    if (!j || j->type != prefab::Json::Array || (int)j->items.size() != n)
        return;
    for (int i = 0; i < n; i++)
        out[i] = (float)j->items[i].number;
}
void read_num(const prefab::Json& o, const char* key, float* out)
{
    if (const prefab::Json* j = o.get(key))
        *out = j->type == prefab::Json::Bool ? (j->boolean ? 1.0f : 0.0f) : (float)j->number;
}
void read_bool(const prefab::Json& o, const char* key, bool* out)
{
    if (const prefab::Json* j = o.get(key))
        *out = j->type == prefab::Json::Bool ? j->boolean : j->number != 0;
}
} // namespace

/* params: {sun: {enabled, azimuth, elevation, color, intensity, angle,
 * shadows}, sky: {enabled, zenith, horizon, ground, intensity, sun_tint,
 * ambient, source: "game" | "manual", game_scale}, remove: "sun" | "sky" |
 * "all", global: bool}. Setting a field of a sun or sky the room does not have
 * adds it (from the defaults). The reply: the room's sun and sky (sky.game:
 * the game's sky this frame, kind "sphere" | "flat" | "panorama" | "none", its five rings
 * and the zenith/horizon/ground they give, 0..1; sky.using: "game" when the
 * record takes them) and the renderers' packed record. Save with
 * lighting {action: "save"}. */
extern "C" int pc_environment_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_lights::track_room();
    bool changed = false;
    if (const prefab::Json* g = params.get("global"))
        pc_env_global = g->type == prefab::Json::Bool ? g->boolean : g->number != 0;
    if (const prefab::Json* r = params.get("remove")) {
        if (r->string == "sun" || r->string == "all")
            sun = Sun(), changed = true;
        if (r->string == "sky" || r->string == "all")
            sky = Sky(), changed = true;
    }
    if (const prefab::Json* s = params.get("sun")) {
        if (!sun.defined)
            sun = Sun(), sun.defined = true;
        read_bool(*s, "enabled", &sun.enabled);
        read_num(*s, "azimuth", &sun.azimuth);
        read_num(*s, "elevation", &sun.elevation);
        read_vec(s->get("color"), sun.color, 3);
        read_num(*s, "intensity", &sun.intensity);
        read_num(*s, "angle", &sun.angle);
        read_bool(*s, "shadows", &sun.shadows);
        changed = true;
    }
    if (const prefab::Json* s = params.get("sky")) {
        if (!sky.defined)
            sky = Sky(), sky.defined = true;
        read_bool(*s, "enabled", &sky.enabled);
        read_vec(s->get("zenith"), sky.zenith, 3);
        read_vec(s->get("horizon"), sky.horizon, 3);
        read_vec(s->get("ground"), sky.ground, 3);
        read_num(*s, "intensity", &sky.intensity);
        read_num(*s, "sun_tint", &sky.sun_tint);
        read_num(*s, "ambient", &sky.ambient);
        if (const prefab::Json* src = s->get("source"))
            sky.manual = src->string == "manual";
        read_num(*s, "game_scale", &sky.game_scale);
        changed = true;
    }
    changed |= rood_atmo::rpc(params); /* fog, volumes, fog_time (atmosphere.cpp) */
    if (changed)
        pc_lights_mark_dirty();
    /* rain_probe [x, y, z]: whether a rain drop there is left out of the picture (tests). */
    std::string probe;
    if (const prefab::Json* r = params.get("rain_probe"))
        if (r->type == prefab::Json::Array && r->items.size() == 3)
            probe = ",\"rain_hidden\":" + jbool(drop_hidden((int)r->items[0].number, (int)r->items[1].number,
                (int)r->items[2].number));
    pc_env_gpu g;
    int active = pc_env_gpu_get(&g);
    /* The game's sky (whatever the source): its kind, rings (0..1) and the three it gives. */
    GameSky game = game_sky();
    std::string rings;
    for (int r = 0; r < 5; r++)
        rings += (r ? "," : "") + jvec(game.ring[r], 3);
    float uz[3], uh[3], ug[3];
    bool using_game = sky_colours(uz, uh, ug);
    std::string game_json = ",\"game\":{\"kind\":\"" + std::string(game_sky_kind(game.kind)) + "\",\"rings\":[" + rings
        + "],\"zenith\":" + jvec(game.zenith, 3) + ",\"horizon\":" + jvec(game.horizon, 3) + ",\"ground\":"
        + jvec(game.ground, 3) + "},\"using\":\"" + (using_game ? "game" : "manual") + "\"";
    std::string s = "{\"zone\":" + std::to_string(rood_lights::zone) + ",\"room\":" + std::to_string(rood_lights::room)
        + ",\"global\":" + jbool(pc_env_global) + ",\"active\":" + jbool(active) + ",\"sun\":{\"defined\":"
        + jbool(sun.defined) + ",\"enabled\":" + jbool(sun.enabled) + ",\"azimuth\":" + num(sun.azimuth, 2)
        + ",\"elevation\":" + num(sun.elevation, 2) + ",\"color\":" + jvec(sun.color, 3)
        + ",\"intensity\":" + num(sun.intensity, 3) + ",\"angle\":" + num(sun.angle, 2)
        + ",\"shadows\":" + jbool(sun.shadows) + "},\"sky\":{\"defined\":" + jbool(sky.defined)
        + ",\"enabled\":" + jbool(sky.enabled) + ",\"zenith\":" + jvec(sky.zenith, 3) + ",\"horizon\":"
        + jvec(sky.horizon, 3) + ",\"ground\":" + jvec(sky.ground, 3) + ",\"intensity\":" + num(sky.intensity, 3)
        + ",\"sun_tint\":" + num(sky.sun_tint, 3) + ",\"ambient\":" + num(sky.ambient, 3) + ",\"source\":\""
        + (sky.manual ? "manual" : "game") + "\",\"game_scale\":" + num(sky.game_scale, 3) + game_json + "},\"gpu\":{\"sun_dir\":"
        + jvec(g.sun_dir, 4) + ",\"sun_col\":" + jvec(g.sun_col, 4) + ",\"zenith\":" + jvec(g.zenith, 4)
        + ",\"horizon\":" + jvec(g.horizon, 4) + ",\"ground\":" + jvec(g.ground, 4) + "},\"hash\":"
        + std::to_string(pc_env_hash()) + ",\"raining\":" + jbool(pc_weather_raining()) + probe + ","
        + rood_atmo::json() + "}";
    snprintf(out, size, "%s", s.c_str());
    return (int)s.size() < size ? 0 : -1;
}

/* ---- the sun's schema kind (editor_schema.h) ---- */

extern "C" int pc_overlay_set_details_tab(const char* name) __attribute__((weak)); /* dev_overlay.cpp */

void rood_env::sun_schema_kind(rood_schema::Kind& k)
{
    namespace rs = rood_schema;
    using rs::Field;
    using rs::Value;
    k = rs::Kind();
    k.name = "sun";
    k.label = "Sun";
    k.object = rood_selection::KIND_SUN;
    k.extras = [](int i) { rood_env::sun_details_extra(i); };
    const Sun d = Sun();
    Field f = rs::bool_field("enabled", "Enabled", "Sun", d.enabled, "Off: no sunlight (the sky stays)");
    f.get = [](int) { return Value::scalar(sun.enabled ? 1.0f : 0.0f); };
    f.set = [](int, const Value& v) { sun.enabled = v.x() > 0.5f, pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::number_field("intensity", "Intensity", "Sun", 0, rood_env::SUN_INTENSITY_MAX, 0.01f, d.intensity, "", false, "Scale on its colour");
    f.get = [](int) { return Value::scalar(sun.intensity); };
    f.set = [](int, const Value& v) { sun.intensity = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::colour_field("color", "Colour", "Sun", 1, Value::vec(d.color[0], d.color[1], d.color[2]), "Linear colour");
    f.get = [](int) { return Value::vec(sun.color[0], sun.color[1], sun.color[2]); };
    f.set = [](int, const Value& v) {
        for (int c = 0; c < 3; c++)
            sun.color[c] = v.at(c);
        pc_lights_mark_dirty();
    };
    k.fields.push_back(f);
    f = rs::bool_field("shadows", "Casts shadows", "Sun", d.shadows);
    f.get = [](int) { return Value::scalar(sun.shadows ? 1.0f : 0.0f); };
    f.set = [](int, const Value& v) { sun.shadows = v.x() > 0.5f, pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::angle_field("azimuth", "Azimuth", "Direction", 0, 360, 0.5f, d.azimuth,
        "Compass direction it shines from: 0 = from +z, 90 = from +x (drag its disc in the view)");
    f.get = [](int) { return Value::scalar(sun.azimuth); };
    f.set = [](int, const Value& v) { sun.azimuth = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::angle_field("elevation", "Elevation", "Direction", 0, 90, 0.5f, d.elevation, "Above the horizon");
    f.get = [](int) { return Value::scalar(sun.elevation); };
    f.set = [](int, const Value& v) { sun.elevation = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::angle_field("angle", "Angular size", "Direction", 0, 10, 0.01f, d.angle,
        "Its disc's diameter: the softness of its shadows (the sun's own: 0.53)");
    f.get = [](int) { return Value::scalar(sun.angle); };
    f.set = [](int, const Value& v) { sun.angle = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
}
void rood_env::sun_details_extra(int)
{
    ImGui::Spacing();
    ImGui::TextWrapped("In the view: drag its disc across the sky dome (azimuth and elevation), its ring "
                       "(intensity), its swatch (colour).");
    if (ImGui::SmallButton("The sky: Atmosphere tab") && pc_overlay_set_details_tab)
        pc_overlay_set_details_tab("atmosphere");
}
