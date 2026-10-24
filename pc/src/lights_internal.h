/* The room's lights and light actors as the light editor's files share them:
 * lights.cpp (room files, the viewport: icons, selection, gizmo, placement,
 * the lights handed to the renderers) and light_actors.cpp (the Place
 * Actors panel, actor rows and details, the prefab editor, glow
 * suggestions, the actors/prefabs RPC). Editor internals; nothing else
 * includes this. */
#ifndef ROOD_LIGHTS_INTERNAL_H
#define ROOD_LIGHTS_INTERNAL_H
#include <string>
#include <vector>

#include "light_prefabs.h"

namespace rood_lights {

struct Light {
    int type = 0; // 0 point, 1 spot, 2 rect
    float position[3] = { 0, 0, 0 };
    float color[3] = { 1.0f, 0.75f, 0.45f };
    float intensity = 1.0f;
    float radius = 600;
    float direction[3] = { 0, 1, 0 }; // spot: world direction (y is down)
    float cone = 45;
    float inner = 0; // spot: full brightness inside this full angle, fading to the cone (0: the outer quarter fades)
    float size[2] = { 128, 128 }; // rect: width, height (world units)
    int pattern = 0, pattern_grid = 16, pattern_mode = 0; // mode: 0 diffuse source, 1 directional aperture
    float pattern_elevation = 35, pattern_spread = 2; // travel below pane normal; full angular width (degrees)
    std::string pattern_faces; // texture key and three rect-XY/texture-UV vertices per triangle
    std::string pattern_entry; // live per-entry rect_light overrides, including user-layer pattern switch
    float barn_angle = 45; // rect: its barn doors' angle from its facing (0 straight ahead .. 90 folded flat)
    float barn_length = 0; // rect: how far its doors reach out from each edge (world units; 0: none)
    bool shadows = false;
    bool enabled = true;
    /* Stable id (lighting_layers.h: "light:<id>"; assigned when first
     * saved or listed) and an optional display name. */
    std::string id, name;
};

/* The current room (lights.cpp). */
extern std::vector<Light> lights;
extern std::vector<prefab::Actor> actors;
extern int zone, room;
extern int selected; /* light index, or -1 */
extern int selected_actor; /* actor index, or -1 */
extern int solo, solo_actor; /* only this light / actor lights the scene (-1: none) */
extern std::vector<int> multi; /* more selected lights (Outliner Ctrl+click), for Create prefab */
extern bool glow_selected;
extern uint32_t glow_id; /* the selected glow (glows.c id; 0: none found) */
extern float glow_at[3];
extern bool dirty;
extern char status[256];
extern int gizmo_operation; /* 0 move (W), 1 rotate (E), 2 scale (R; editor_transform.h) */

void track_room();
void select_none();
void select_light(int index);
void select_actor(int index);
void select_glow(const float position[3]); /* the glow nearest the position */
void select_glow_id(uint32_t id);
/* This presented frame's animation time in seconds (fixed by RPC actors
 * {time}), and whether lights animate (settings key light_animation). */
double light_time();
extern int animate;
/* The actor's lights now (nothing when its prefab is missing or it is off). */
void actor_lights(const prefab::Actor& a, std::vector<prefab::EvalLight>& out, bool animated = true);
const char* light_type_name(int type); /* "Point light", ... */

/* Placement (Place Actors panel, drag and drop, double-click, Ctrl+click,
 * RPC): an item is "light:point", "light:spot", "light:rect" or
 * "prefab:<id>". */
struct Placement {
    bool ok = false;
    int snap = 0; /* PC_SNAP_* the cursor found */
    float position[3] = { 0, 0, 0 }; /* light position or actor pivot */
    float rotation[3] = { 0, 0, 0 }; /* actor */
    float surface[3] = { 0, 0, 0 }, normal[3] = { 0, 0, 0 };
};
/* Where `item` goes for the cursor at window pixel (x, y) with the snap
 * modes (0: the editor's, always falling back to the surface). */
bool place_compute(const std::string& item, float x, float y, unsigned modes, Placement& out);
/* Add `item` there, select it; returns the new light or actor index (-1). */
int place_commit(const std::string& item, const Placement& p);
/* Place at the surface under the view's centre (double-click). */
int place_at_centre(const std::string& item);
bool view_rect(float rect[4]);
/* Drag from the panel: begin, move (forced cursor for RPC; else the mouse),
 * drop and cancel. */
void place_drag_begin(const std::string& item, bool forced, float x, float y);
void place_drag_move(float x, float y);
int place_drag_drop(); /* the new index, or -1 when cancelled / nowhere */
bool place_drag_cancel();
bool place_drag_active(std::string* item = nullptr, Placement* where = nullptr, bool* over_view = nullptr);
/* The Place panel's current item: what Ctrl+click adds. */
extern std::string place_item;
std::string item_label(const std::string& item);

/* Ghosts drawn in the view and lit (drag preview, glow suggestions). */
struct Ghost {
    bool actor = false;
    Light light;
    prefab::Actor a;
};
extern std::vector<Ghost> ghosts;

/* light_actors.cpp */
void actors_outliner(const char* filter);
/* Details: a kind's own tools under its schema fields (editor_schema.cpp). */
void light_details_extra(int index); /* lights.cpp: Duplicate, Delete, Create prefab */
void actor_details_extra(int index); /* the prefab line, Reset all, fixture, glow, its lights, actions */
void glow_details_actions();
void glows_outliner_actions();
void prefab_editor_window();
/* The prefab editor's prefab: opened on one ("" closes it), and which one is open ("" none). */
void edit_prefab(const std::string& id);
const std::string& editing_prefab_id();
void suggestions_window();
/* Glow suggestions for the room: a prefab for each unlinked glow it fits. */
struct Suggestion {
    float glow[3]; /* the flame: the glow's position, or the texture flame's */
    float color[3];
    float size;
    std::string prefab;
    bool accept = true;
    bool flame = false; /* a flame painted in room textures (no glow to link) */
    float weight = 0; /* its strength (bright warm texels) */
};
/* Light all fixtures: an actor for each unlit glow and texture flame. */
std::vector<Suggestion> suggest_for_glows();
int attach_to_glow(const std::string& prefab_id, const float glow[3]);
/* An actor for a room fixture's flame: its first light on the flame, the
 * fixture recorded (its polygons cast no shadow from it), linked to the
 * glow when there is one. */
int create_fixture_actor(const std::string& prefab_id, const float flame[3], const float* glow, const float* normal);
/* "Light this fixture" at a surface point (the click, L): finds the flame
 * (a glow nearby, else the fixture's bright warm texels), chooses the
 * prefab (empty: by glow colour/size or flame strength) and adds the actor.
 * Returns its index, or -1 (status says why). */
int light_fixture_at(const float hit[3], const float normal[3], const std::string& prefab_id);
int light_fixture_at_pixel(float x, float y, const std::string& prefab_id);
extern bool fixture_mode; /* toolbar "Light fixture": clicks in the view light fixtures */
extern "C" int pc_gi_flame_near(const float p[3], float radius, float out[4]);
extern "C" int pc_gi_flames(float out[][4], int max);
extern "C" int pc_gi_fixture_triangles(const float p[3], float radius, float (*out)[9], int max);
/* Prefab file operations (status set on failure). */
bool create_prefab_from_selection(const std::string& name);
/* A light prefab of the user's from lights and light actors (their lights at rest, baked: positions,
 * colours, animation), replaced here by one actor of it, selected. */
bool create_prefab_from(const std::vector<int>& light_indices, const std::vector<int>& actor_indices, const std::string& name);
bool save_actor_as_prefab(int actor, const std::string& name);
bool apply_actor_to_prefab(int actor);
void draw_icon(void* draw_list, float x, float y, float size, const std::string& item, const float color[3], float alpha);

/* lighting_editor.cpp: the room's lighting layers (lighting_layers.h). */
void ensure_ids(); /* every light and actor has a unique id */
void layers_load(int zone, int room); /* read the layers, fill the room's lights, actors, overrides */
/* Leaving a room with unsaved edits: keep them for this session; entering
 * it again puts them back on top of its layers (still unsaved). */
void layers_stash();
bool layers_save(); /* the user layer (status says what happened) */
void layers_poll(); /* live reload when a layer changed on disk */
unsigned layers_reload(); /* read the layers from disk now (RPC); returns layers_loads() */
unsigned layers_loads(); /* layer loads so far (room entries and reloads) */
/* Outliner: a row's layer badge (after the row's item) and its context
 * menu (Revert to the layers below, Promote to pack); `kind` light/actor/glow. */
void layer_row(const std::string& id);
void layers_outliner_header();
std::string light_item_id(int index);
std::string actor_item_id(int index);

} // namespace rood_lights
#endif
