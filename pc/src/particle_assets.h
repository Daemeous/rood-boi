/* Emitter assets (pc/assets/emitters/README.md; pc/docs/Particles.md):
 * pc/assets/emitters/<id>.json (ROOD_EMITTERS: another directory), each an
 * emitter's look as lighting field values (lighting/README.md "Emitters"):
 * its spawn, its behaviour modules (drift, curl, gravity, drag, fade; one
 * left out is neutral), its renderer and lighting, and what the library
 * lists (kind "emitter", name, category, tags, icon, description). An
 * emitter item names one with asset=<id>; every field it does not write
 * comes from it. No game dependencies (pc/tests/particles_probe.cpp). */
#ifndef ROOD_PARTICLE_ASSETS_H
#define ROOD_PARTICLE_ASSETS_H
#include <string>
#include <vector>

#include "lighting_layers.h"

namespace prefab {
struct Json;
}
namespace rood_particles {
struct Asset {
    std::string id, name, category, icon, description; /* id: the file's stem */
    std::vector<std::string> tags;
    lighting::Fields fields; /* lighting key, canonical value (kind "emitter") */
    const std::string* get(const std::string& key) const;
};
std::string assets_dir();
/* An asset from its JSON; false (and why) when it is not one. */
bool asset_from_json(const std::string& id, const prefab::Json& root, Asset& out, std::string* error);
/* Every asset of the directory, sorted by id (read once; reload_assets reads them again). */
const std::vector<Asset>& assets();
void reload_assets();
/* The directory's files changed since they were read (their names, times and sizes; false before the first
 * read): particles.cpp's poll then reloads them and resolves every emitter again. */
bool assets_changed();
const Asset* find_asset(const std::string& id);
const std::string& assets_status(); /* "1 asset (pc/assets/emitters)", or the problems */
} // namespace rood_particles
#endif
