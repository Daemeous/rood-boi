/* The room's particle emitters for the editor (particles.cpp): as lighting
 * items ("emitter:<id>"), by index, for the scene (editor_scene.cpp), the
 * schema (particles_editor.cpp), the clipboard and the room state
 * (environment.cpp). Editor internals. */
#ifndef ROOD_PARTICLES_INTERNAL_H
#define ROOD_PARTICLES_INTERNAL_H
#include <string>

#include "lighting_layers.h"

namespace rood_particles {
/* The room state (environment.cpp rood_env::reset / apply_item / add_items call these). */
void reset();
bool apply_item(const lighting::Item& it); /* an `emitter` item; false for others */
void add_items(lighting::State& s);
/* Read the emitter assets again (Details' Reload assets, RPC particles {reload_assets}) and
 * resolve every emitter from them again: its own values stay, the rest follow the assets. */
void reload();

int emitter_count();
std::string emitter_id(int index); /* "emitter:<id>" ("" out of range; an id assigned on the spot) */
int emitter_index(const std::string& item_id); /* -1: none */
std::string emitter_label(int index); /* "Emitter 2: Dust motes" */
bool emitter_centre(int index, float out[3], float* radius);
bool emitter_size(int index, float out[3]); /* its box; false out of range */
void set_emitter_size(int index, const float size[3]); /* the room unsaved */
lighting::Item emitter_item(int index);
/* A copy of `item` with a new id, offset: its index, or -1. */
int add_emitter_item(const lighting::Item& item, const float offset[3]);
void remove_emitter(int index);
/* A new emitter of `asset` at `pos`, selected; its index (the Atmosphere tab, the library). */
int add_emitter(const std::string& asset, const float pos[3]);
/* Details > Atmosphere's Add emitter: one of `asset` on the surface under the view's centre,
 * its box's bottom on it, selected (one undo step); its index, or -1 (no such asset, no surface). */
int add_here(const std::string& asset);
/* One of `asset` with its box's bottom on the surface point `surface` (a library drop), selected;
 * its index, or -1 (no such asset). The caller names the undo step. */
int add_on_surface(const std::string& asset, const float surface[3]);
/* Its value of a lighting field (as written, else its asset's, else the default); setting one
 * writes it (equal to its asset's: it follows the asset again). */
std::string field(int index, const char* key);
void set_field(int index, const char* key, const std::string& value);
void clear_field(int index, const char* key); /* it follows its asset again (the reset arrow) */
void outliner(const char* filter); /* the Outliner's Emitters rows (particles_editor.cpp) */
void details_extra(int index); /* Details: an emitter's tools under its fields (particles_editor.cpp) */
/* Its asset's value of a field (the reset arrow's), and whether it writes its own. */
std::string asset_value(int index, const char* key);
bool overrides(int index, const char* key);
/* The emitter's own fields as members, for the editor and the RPC. */
struct Own {
    std::string name, asset;
    bool enabled;
    float pos[3], yaw;
    int seed;
};
Own own(int index);
void set_own(int index, const Own& o);
bool emitters_shown(); /* Show > Emitters */
bool* emitters_shown_flag();
int drawn(int index); /* its motes last frame */
} // namespace rood_particles
#endif
