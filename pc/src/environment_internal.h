/* The editor's side of the room environment (environment.cpp): its items
 * in the lighting layers and undo snapshots. Editor internals. */
#ifndef ROOD_ENVIRONMENT_INTERNAL_H
#define ROOD_ENVIRONMENT_INTERNAL_H
#include <string>
#include <vector>

#include "lighting_layers.h"

namespace rood_schema {
struct Kind;
}
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
/* The room's sun (or moon) for the editor's handles (editor_light_handles.cpp). */
struct SunState {
    bool defined = false, enabled = true;
    float azimuth = 180, elevation = 45; /* degrees (rood_math::sun_direction) */
    float color[3] = { 1, 1, 1 };
    float intensity = 1, angle = 0.53f;
    bool shadows = true;
};
/* The sun's intensity range in every editor (Details, Atmosphere, its ring in the view). */
const float SUN_INTENSITY_MAX = 8;
SunState sun_state();
void set_sun_state(const SunState& s); /* the room unsaved */
bool sun_icon_shown(); /* Atmosphere > "Sun in the view" */
/* The sun as a schema kind (editor_schema.h: "sun", object KIND_SUN, index 0;
 * Details, RPC editor_schema {id "env:sun"}) and its tools under its fields. */
void sun_schema_kind(rood_schema::Kind& out);
void sun_details_extra(int index);
} // namespace rood_env

/* The ground fog (`env fog`) and fog volumes (`fog <id>`): atmosphere.cpp.
 * rood_env's functions above include them. */
namespace prefab {
struct Json;
}
namespace rood_thumbs {
struct Fog;
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
bool volume_size(int index, float out[3]); /* its full extents; false out of range */
void set_volume_size(int index, const float size[3]); /* the room unsaved */
void outliner(const char* filter); /* the Outliner's Fog volumes rows */
void schema_kind(rood_schema::Kind& out); /* the fog volume's fields (editor_schema.h) */
void details_extra(int index); /* Details: a fog volume's tools under its fields */
void details_header(int index); /* Details: a fog volume's preset row, above its fields */
/* Fog presets for the library (editor_library.cpp): the built-in ones
 * (fog_presets.json) and the user's (<user prefab dir>/fog), in file order. */
struct FogPreset {
    std::string id, name, why;
    bool user = false;
};
std::vector<FogPreset> fog_presets();
unsigned fog_presets_serial(); /* bumps on every (re)load */
/* A preset's look for its thumbnail ("": a new volume's defaults); false when there is no such preset. */
bool preset_look(const std::string& id, rood_thumbs::Fog& out);
/* A fog volume of preset `preset` ("": the defaults) dropped on a surface: it
 * rests on the floor there (a wall's or ceiling's: the floor below), its
 * footprint inside the floor's area. volume_drop: where (pos, full size,
 * its shape); place_volume: added and selected, its index or -1. */
bool volume_drop(const std::string& preset, const float surface[3], const float normal[3], float pos[3], float size[3],
    bool* sphere);
int place_volume(const std::string& preset, const float surface[3], const float normal[3]);
/* The volume's look as a preset of the user's (<user prefab dir>/fog/<id>.json,
 * the id from the name, unique); the volume then uses it. */
bool save_user_preset(int index, const std::string& name, std::string* id, std::string* message);
/* The library's management of the user's presets (Mine): its file ("" when not
 * the user's), a new display name (the id stays: rooms naming it keep it), or
 * deleted (the file moved aside to <id>.json.bak; volumes using it keep their
 * values, written in full). */
std::string user_preset_file(const std::string& id);
bool rename_user_preset(const std::string& id, const std::string& name, std::string* message);
bool delete_user_preset(const std::string& id, std::string* message);
int volumes_using(const std::string& preset); /* the room's volumes naming it */
/* The level floor around (x, z) a drop fits a fog volume into: its x, z bounds and height (false: none known). */
bool floor_area_at(float x, float z, float lo[2], float hi[2], float* height);
} // namespace rood_atmo

/* The room's post-processing profiles (`post room`, `post cutscene`,
 * `post event.<id>`): post_profile.cpp. rood_env's functions include them. */
namespace rood_post {
void reset();
bool apply_item(const lighting::Item& it);
void add_items(lighting::State& s);
} // namespace rood_post
#endif
