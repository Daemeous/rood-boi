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
};
struct Entry {
    std::string id, name, where, note, review;
    std::vector<Match> match;
    Mask mask;
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
int light_triangle_count();
int slot_at(unsigned u, unsigned v, unsigned tpage);
bool mask_covers(const Mask& m, int x, int y);
bool filters_pass(const Mask& m, unsigned index, uint16_t t, unsigned depth);
uint32_t map_serial();

} // namespace emissive
#endif
