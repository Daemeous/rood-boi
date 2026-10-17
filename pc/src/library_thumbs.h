/* The library's thumbnails (pc/docs/plans/2026-10-16-artist-tools-de-plan.md,
 * decision 7): an item lit in a neutral scene (a grey floor at y = 0 with a
 * 64-unit grid, a back wall), rendered on the CPU with our renderers' light
 * model (light_shape.h: the falloff, N.L, a spot's edge, a rect's front face
 * and barn doors) and a fog volume marched through as fog_volume.glsl shades
 * it (its rounded box or sphere, soft edge, taper, wisps, phase, glow at its
 * base, opacity cap, brightness, ambient, a self-shadow estimate), so OpenGL
 * and Vulkan show the same picture and the bytes are the same every run. Not
 * the GPU renderers themselves (the design's wording; an open question for
 * the user, pc/docs/plans/2026-10-15-artist-tools-design.md "Status"). Cached
 * as <kind>-<id>-<hash>.rgba under pc/build/library_thumbs (ROOD_LIBRARY_THUMBS);
 * never committed. No Dear ImGui (pc/tests/library_probe.cpp). */
#ifndef ROOD_LIBRARY_THUMBS_H
#define ROOD_LIBRARY_THUMBS_H
#include <stdint.h>
#include <string>
#include <vector>

#include "light_prefabs.h"

namespace rood_thumbs {
constexpr int SIZE = 96;
constexpr int VERSION = 2; /* bump whenever render() draws differently: every cached name changes */
/* The neutral scene's look: pc/assets/editor/library_thumbs.json (ROOD_LIBRARY_THUMBS_LOOK), the
 * Library's Thumbnails... popup, RPC editor_library {action "thumbs", look, save_look}; part of every
 * hash (a change renders them again). */
struct Look {
    float floor = 0.5f, wall = 0.42f, grid = 0.8f; /* albedos; the grid lines' factor on the floor */
    float ambient = 0.04f; /* light everywhere on the surfaces */
    float fog_ambient = 0.25f; /* light everywhere in a fog volume (the sky, the room) */
    float exposure = 1.8f; /* display = 1 - exp(-exposure x light), then sRGB */
    float background[3] = { 0.03f, 0.03f, 0.035f };
    float fov = 40; /* degrees, vertical */
};
Look& look(); /* read from look_path() the first time */
std::string look_path();
bool load_look(std::string* error); /* look_path() into look() (a missing file: the defaults) */
bool save_look(std::string* error); /* look() into look_path() */
struct LookField {
    const char* key;
    const char* label;
    float Look::*value;
    float min, max;
};
const std::vector<LookField>& look_fields(); /* the scalars, for the panel, RPC and the file */
struct Fog { /* a fog volume's look (atmosphere.cpp Volume; fog_volume.glsl) */
    bool on = false, sphere = false;
    float size[3] = { 256, 128, 256 }; /* full extents; it rests on the floor, centred on x = z = 0 */
    float density = 0.004f, edge = 0.5f;
    float color[3] = { 1, 1, 1 }, fill[3] = { 0, 0, 0 }, glow[3] = { 0, 0, 0 };
    float noise = 0.5f, taper = 0, glow_core = 0.5f, phase = 0.3f, opacity = 1, brightness = 1, ambient = 1, shadow = 0;
};
struct Scene {
    std::vector<prefab::EvalLight> lights; /* world: y down, the floor at y = 0, the item about x = z = 0 */
    Fog fog;
    float wall_z = 200; /* the back wall's plane (facing -z) */
};
/* size x size RGBA (sRGB, opaque). */
void render(const Scene& s, int size, std::vector<uint8_t>& rgba);
/* What the picture depends on (the scene, the look, the size, VERSION): 16 hex digits. */
std::string hash(const Scene& s, int size);
std::string cache_dir(); /* ROOD_LIBRARY_THUMBS, else pc/build/library_thumbs */
std::string file_name(const std::string& kind, const std::string& id, const std::string& hash);
/* The cache file: "RTHB", width, height, VERSION (uint32 each, little-endian), then the pixels. */
bool load(const std::string& path, int size, std::vector<uint8_t>& rgba);
bool save(const std::string& path, int size, const std::vector<uint8_t>& rgba, std::string* error);
} // namespace rood_thumbs
#endif
