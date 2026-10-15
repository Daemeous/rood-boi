/* The editor's side of the room environment (environment.cpp): its items
 * in the lighting layers and undo snapshots. Editor internals. */
#ifndef ROOD_ENVIRONMENT_INTERNAL_H
#define ROOD_ENVIRONMENT_INTERNAL_H
#include <string>

#include "lighting_layers.h"

namespace rood_env {
/* No sun, no sky (a room's layers are applied after this). */
void reset();
/* An `env sun` / `env sky` item into the room; false for other items. */
bool apply_item(const lighting::Item& it);
/* The room's env items (those it has) into a state. */
void add_items(lighting::State& s);
/* The items as definition lines (undo snapshots), and back. */
std::string text();
void set_text(const std::string& text);
} // namespace rood_env

/* The ground fog (`env fog`) and fog volumes (`fog <id>`): atmosphere.cpp.
 * rood_env's functions above include them. */
namespace prefab {
struct Json;
}
namespace rood_schema {
struct Kind;
}
namespace rood_atmo {
void reset();
bool apply_item(const lighting::Item& it); /* env fog, fog volumes */
void add_items(lighting::State& s);
bool rpc(const prefab::Json& params); /* RPC environment {fog, volumes, ...}: true when the room changed */
std::string json(); /* "fog":{...},"volumes":[...] for its reply */
/* The room's fog volumes for the editor (editor_scene.cpp, editor_schema.cpp,
 * editor_clipboard.cpp): by index, as lighting items ("fog:<id>"), picked by
 * a ray, copied. */
int volume_count();
std::string volume_id(int index); /* "fog:<id>" (an id assigned on the spot; "" out of range) */
int volume_index(const std::string& item_id); /* -1: none */
std::string volume_label(int index); /* "Box 2: Cellar mist" */
void set_selected(int index); /* the selection's volume: its gizmo (editor_scene.cpp publish) */
bool volume_centre(int index, float out[3], float* radius);
float volume_hit(int index, const float origin[3], const float dir[3]); /* rood_pick::volume_hit; -1 */
bool volumes_shown(); /* Show > Fog volumes */
lighting::Item volume_item(int index);
int add_volume_item(const lighting::Item& item, const float offset[3]); /* a copy with a new id: its index, or -1 */
void remove_volume(int index);
void outliner(const char* filter); /* the Outliner's Fog volumes rows */
void schema_kind(rood_schema::Kind& out); /* the fog volume's fields (editor_schema.h) */
void details_extra(int index); /* Details: a fog volume's tools under its fields */
} // namespace rood_atmo

/* The room's post-processing profiles (`post room`, `post cutscene`,
 * `post event.<id>`): post_profile.cpp. rood_env's functions include them. */
namespace rood_post {
void reset();
bool apply_item(const lighting::Item& it);
void add_items(lighting::State& s);
} // namespace rood_post
#endif
