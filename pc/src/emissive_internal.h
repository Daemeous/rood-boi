// Emissive texels: what emissive.cpp shares with its editor and RPC
// (emissive_editor.cpp). Design: pc/docs/Emissive.md.
#ifndef ROOD_EMISSIVE_INTERNAL_H
#define ROOD_EMISSIVE_INTERNAL_H
#include <stdint.h>
#include <array>
#include <string>
#include <vector>
#include "light_prefabs.h" /* prefab::Json */

namespace emissive {

enum { ORIGIN_CURATED = 0, ORIGIN_OVERRIDE = 1, ORIGIN_USER = 2 };
enum { MAX_SLOTS = 256 };

/* A texture the entry applies to: the content key of a polygon's UV box
 * (fixture_lib.py ikey), its size in texels, optionally its CLUT's colours
 * (clut key), and where that box sits in the entry's texture space. */
struct Match {
    std::string key, clut;
    int w = 0, h = 0;
    int ox = 0, oy = 0;
};
/* Which texels glow, in texture space (texels; x right, y down from the
 * first match's box origin). No rects and no polygons: the whole texture.
 * The filters apply per drawn texel. */
struct Mask {
    std::vector<std::array<int, 4>> rects; /* x0, y0, x1, y1 (x1, y1 excluded) */
    std::vector<std::vector<std::array<float, 2>>> polygons;
    bool has_indices = false;
    uint8_t indices[32] = {}; /* CLUT index set (bits) */
    bool has_color = false;
    int cmin[3] = { 0, 0, 0 }, cmax[3] = { 31, 31, 31 }; /* 5-bit */
    float lmin = 0, lmax = 1; /* luminance 0..1 */
    /* A painted map (materials.h): the channel ("emissive") of each matched
     * texture, by its key, gives every texel's amount (0: it does not glow;
     * 255: fully). Empty: every texel the shapes cover, fully. */
    std::string map;
};
/* Where an entry applies (pc/docs/Emissive.md, Scope). A mask is in
 * texture space, so every surface sampling its texels would glow; a scope
 * limits that. Empty: everywhere (the default). `rooms` limits the entry to
 * those rooms; `surfaces` and `models` (either) to those room polygons and
 * those characters' or props' models. All identifiers are the game data's
 * own, the same every visit: a room is (zone, room) as the state RPC names
 * it; a surface is a room polygon by its byte offset in the room's geometry
 * data (scene_capture.h scene_triangle.polygon); a model is the content
 * hash of its data (actor_native.c: kind, groups, vertices, records). */
struct Surface {
    int zone = -1, room = -1;
    uint32_t polygon = 0;
    bool operator==(const Surface& o) const { return zone == o.zone && room == o.room && polygon == o.polygon; }
};
struct Scope {
    std::vector<std::array<int, 2>> rooms;
    std::vector<Surface> surfaces;
    std::vector<uint32_t> models;
    bool empty() const { return rooms.empty() && surfaces.empty() && models.empty(); }
    /* Limited to some surfaces or models: drawn with a scope bit (emissive.glsl). */
    bool per_surface() const { return !surfaces.empty() || !models.empty(); }
};
/* What a drawn polygon is, for scopes: a polygon of the current room's
 * geometry (its byte offset), a polygon of a model (its hash), or anything
 * else (other room blocks, effects: only unscoped and room-scoped entries). */
enum { SURFACE_ROOM = 0, SURFACE_MODEL = 1, SURFACE_OTHER = 2 };
struct Entry {
    std::string id, name, where, note, review;
    std::vector<Match> match;
    Mask mask;
    Scope scope;
    float color[3] = { 1, 1, 1 }; /* tint */
    float strength = 1; /* x the texel's colour (display light) */
    float light = 0; /* > 0: its room triangles are game glows, this much */
    bool enabled = true;
    int origin = ORIGIN_CURATED;
};
struct Catalog {
    std::vector<Entry> entries;
};
struct UseInfo {
    unsigned page, clut;
    uint8_t box[4];
    std::string key, entry;
    int refs;
};

/* Catalog */
std::string catalog_path();
std::string user_path();
const Catalog& catalog(); /* merged: curated, then the user's (same id replaces) */
const Catalog& curated_catalog();
uint32_t generation();
void reload();
const std::string& status();
const Entry* find(const std::string& id);
int index_of(const std::string& id);
void put(const Entry& e); /* into the user layer */
bool remove(const std::string& id); /* from the user layer */
std::string new_id();
bool user_dirty();
bool save_user(std::string* error);
bool entry_from_json(const prefab::Json& j, Entry& e, std::string* error);
prefab::Json entry_to_json(const Entry& e);
std::string catalog_text(const Catalog& c, const char* comment);

/* Textures and the map */
uint16_t vram_at(int x, int y);
uint16_t texel(unsigned u, unsigned v, unsigned tpage, unsigned clut, unsigned* index);
std::string content_key(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1);
std::string clut_key(unsigned tpage, unsigned clut);
const std::vector<UseInfo> use_list(bool compute_keys);
int marked_use_count();
int marked_cell_count();
int scope_overflow_count(); /* entries limited to surfaces left unmarked (over 16 at once) */
int light_triangle_count();
/* The room triangles that are lights now: their glow colour (light_rgb), the
 * triangle's texels and the glowing texels' amount (RPC emissive lights). */
struct LightInfo {
    int index = 0; /* scene triangle */
    uint32_t polygon = 0;
    float rgb[3] = { 0, 0, 0 };
    int texels = 0;
    float amount = 0;
};
std::vector<LightInfo> light_list();
int slot_at(unsigned u, unsigned v, unsigned tpage);
bool mask_covers(const Mask& m, int x, int y);
bool filters_pass(const Mask& m, unsigned index, uint16_t t, unsigned depth);
uint32_t map_serial();
/* The amount (0..255) of a marked texel: its painted map's value, else 255. */
int amount_at(unsigned u, unsigned v, unsigned tpage);
/* A matched use of an entry with a painted map whose channel is missing or
 * of another size (the editor lists them). */
struct MapProblem {
    std::string entry, key, problem;
};
std::vector<MapProblem> map_problems();

/* Scopes */
bool current_room(int* zone, int* room); /* the room the player is in (dev_state.c) */
bool in_room(const Scope& s, int zone, int room); /* no rooms: every room */
/* Whether the entry applies to a polygon of that kind (SURFACE_*: room
 * polygon byte offset, model hash) in room (zone, room). */
bool scope_allows(const Entry& e, int zone, int room, int kind, uint32_t polygon, uint32_t model);
/* A native mesh's kind (SURFACE_*) and, for room meshes, the byte offset of
 * its polygon `index` in the room data; for models, the model hash. */
int mesh_surface(uint32_t mesh, int index, uint32_t* polygon, uint32_t* model);
/* A model hash as the catalog writes it (8 hex digits). */
std::string model_key(uint32_t model);
bool parse_model_key(const std::string& s, uint32_t* model);

/* Surfaces drawn now that sample an entry's marked texels (the editor's
 * "Show other uses", RPC emissive surfaces): one per room polygon (by its
 * offset) or per mesh (models, other room blocks), with its triangles (world
 * space) to draw. */
struct SurfaceUse {
    int kind = SURFACE_OTHER; /* SURFACE_* */
    uint32_t polygon = 0, model = 0, mesh = 0;
    std::vector<std::array<float, 9>> triangles; /* world space (at most 64) */
    float centre[3] = { 0, 0, 0 }; /* of its first triangle */
    int texels = 0, covered = 0, glowing = 0; /* its texels, under the entry's shapes, glowing now (filters) */
    bool allowed = false; /* the entry's scope lets it glow */
    bool listed = false; /* named by the scope (surfaces, models) */
    bool affected() const { return allowed && glowing > 0; }
};
std::vector<SurfaceUse> surface_uses(const std::string& id);
/* The emission of texel (u, v) under slot s, as emissive.glsl computes it
 * (the entry's filters; the scope not tested): 1 when it glows. */
int emission_at(unsigned u, unsigned v, unsigned tpage, unsigned clut, int s, float rgb[3]);

/* Light texels integrated over a triangle's texture area (UV space, texel
 * centres): the summed emission (display light x the light weight) of the
 * texels that glow, the glowing share (the painted / mask amount counted),
 * and the triangle's texel count. `scope_ok(entry)` says whether an entry
 * applies to this polygon. Returns whether any texel of a light entry lies
 * under it (its shapes, before the per-texel filters). */
struct TriangleLight {
    float sum[3] = { 0, 0, 0 }; /* sum of emission x light weight */
    float amount = 0; /* sum of the glowing texels' amounts (0..1 each) */
    int texels = 0;
    bool candidate = false;
};
/* Of a light triangle's emission, what reaches its glow: the glowing
 * texels' mean emission x light weight x max(their share, LIGHT_MIN_SHARE). */
#define LIGHT_MIN_SHARE 0.25f
void light_rgb(const TriangleLight& t, float rgb[3]);

} // namespace emissive
#endif
