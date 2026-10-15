// The fixture catalog (pc/assets/fixtures/catalog.json; format and workflow
// in pc/docs/Fixture_Catalog.md): every fixture model of the game's rooms (a
// candle, a wall torch, a lantern, a cobweb, a light shaft...) as a type,
// with how its instances are lit.
//
// pc/tools/generate_lighting.py places one light actor per flame of every
// catalogued fixture in the packs' generated layer; each actor names its
// type (fixture_type=T001, flame=N), and each game glow that belongs to a
// catalogued fixture is listed with its type. Here the game reads the
// catalog for those: a type's prefab parameters (its class's defaults, then
// its own), its light offset from the flame (in the fixture's frame), its
// fixture radius (shadow exclusion) and its glow treatment (leave, drive,
// suppress) apply to every instance, live: editing a type (the editor's
// "Edit type", or the file by hand, which reloads within half a second)
// changes all its instances at once. An actor's own overrides (authored or
// user layer, by id) still win over its type.
//
// No Dear ImGui here.
#ifndef ROOD_FIXTURE_CATALOG_H
#define ROOD_FIXTURE_CATALOG_H
#include <string>
#include <utility>
#include <vector>

#include "light_prefabs.h"

namespace fixtures {

enum GlowMode { GLOW_LEAVE = 0, GLOW_DRIVE = 1, GLOW_SUPPRESS = 2 };

struct Type {
    std::string id, cls, description, family, review, confidence;
    std::string prefab; /* "none": not a light */
    int glow = GLOW_LEAVE;
    int glow_shape = 0; /* its glow's shape: 0 auto, 1 sphere, 2 column (glow_overrides.h PC_GLOW_SHAPE_*) */
    float glow_shadow_core = -1; /* its column's shadows aim at a core this fraction of its radius (< 0: the default) */
    bool enabled = true, haze = false;
    float fixture_radius = 16;
    float offset[3] = { 0, 0, 0 }; /* light offset from the flame (x right, y down, z forward) */
    std::vector<std::pair<std::string, prefab::Value>> params; /* the class's, then the type's own */
    std::vector<std::string> own; /* fields the type sets itself (else its class's): "params.<key>", "offset", ... */
    int instances = 0, rooms = 0, flames = 0;
    bool owns(const std::string& field) const;
    const prefab::Value* param(const std::string& key) const;
};

struct Catalog {
    std::string path, error;
    std::vector<Type> types;
    prefab::Json json; /* the file as parsed (edits change it, save writes it) */
    long long stamp = -1;
    int serial = 0; /* bumps on every (re)load or edit */
    bool dirty = false; /* edited since loaded or saved */
};

/* The catalog (loaded on first use; ROOD_FIXTURES names another file). */
const Catalog& catalog();
const Type* find(const std::string& id);
/* Reload when the file changed (and it has no unsaved edits); true when it did. */
bool poll();
/* Edits (live: every instance follows at once). A null value removes the
 * type's own setting (back to its class's). */
bool set_param(const std::string& type, const std::string& key, const prefab::Value* value);
bool set_offset(const std::string& type, const float* offset);
bool set_fixture_radius(const std::string& type, const float* radius);
bool set_glow(const std::string& type, const int* mode);
bool set_enabled(const std::string& type, const bool* enabled);
bool set_review(const std::string& type, const std::string& review);
/* Write the catalog (the same layout pc/tools/fixture_lib.py writes). */
bool save(std::string* message);
/* Drop unsaved edits (reload the file). */
void revert();
const char* glow_name(int mode);

} // namespace fixtures
#endif
