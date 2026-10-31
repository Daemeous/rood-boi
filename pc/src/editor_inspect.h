// Click-to-inspect (pc/docs/plans/2026-10-27-materials-and-inspection-design.md part 2, plan
// 2026-10-30-click-to-inspect-plan.md): a click on a room surface or a drawn model (a character,
// weapon or prop) in the Game panel selects it (selection kinds KIND_SURFACE / KIND_MODEL,
// editor_selection.h), outlined in the view; Details shows what it is, its mesh, the texture and
// palette under the click (nearest-sampled preview), the material entry and the layer it came from,
// the shader; where the texture is used (the rollout survey's rooms, with Go); the Outliner lists
// the models drawn now and the surfaces inspected. View modes by name (the design's list) are
// commands and a View menu. Everything is reachable over RPC editor_inspect.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "editor_overlays.h"
#include "editor_selection.h"
#include "emissive_internal.h"

namespace rood_inspect {
using rood_selection::Ref;

struct Item {
    Ref ref;
    emissive::TexturePick pick; /* the texture and surface under the click */
    int actor_slot = -1; /* a model drawn for an actor (0: Ashley), else -1 */
    unsigned tris = 0, total = 0; /* the mesh's triangles / polygons (room mesh: the group's) */
    int list = 0; /* render_list_objects of the list that drew it: 0 room, 1 object, 2 layered block */
    std::vector<float> outline; /* the picked polygon's triangles (world, 9 floats each) */
    std::string entry, entry_source, entry_name, entry_preset; /* the material entry and its layer */
};

/* Pick at a window pixel / world point: what is there (false: nothing textured). The item is
 * remembered under its ref (exists / label / position / details answer for it). */
bool pick(float x, float y, Item& out);
bool pick_world(const float p[3], Item& out);
/* A plain click on empty space (lights.cpp, editor_scene.cpp click_at): picks and selects; the
 * ref selected, or empty when nothing was there. */
Ref click(float x, float y, rood_selection::Source s);
const Item* item(const Ref& r); /* remembered (null: unknown) */
bool exists(const Ref& r);
std::string label(const Ref& r); /* "Ashley", "Actor 2", "Model 1a2b3c4d", "Surface 0x1f40" */
bool position(const Ref& r, float out[3], float* radius); /* the picked triangle's centroid */
std::vector<Ref> refs(); /* every remembered item (for editor_select's objects) */
/* Escape with an inspected surface or model as the primary: the selection is cleared (true); else
 * Escape keeps its meaning (closing the editor). */
bool escape_clears();
/* Each overlay frame (rood_scene::frame): a room change drops the items; the models drawn now. */
void frame(int zone, int room);
/* The outline of the primary (or locked) surface / model, over the view. */
void draw_highlight(const pc_overlay_view& v);
/* Details' page for a surface or model subject. */
void details(const Ref& subject);
/* The Outliner section's rows: the models drawn now, the surfaces inspected. */
void outliner(const char* filter);
/* Details > Material on the item's texture (its entry, or a new one). */
bool open_material(const Ref& r, std::string* error);

/* ---- where used ---- */
struct Use {
    int zone = -1, room = -1;
    std::string name; /* the room's name from rooms.json ("" unknown) */
    float share = 0; /* of the surveyed views' textured area */
    bool live = false; /* drawn in the current frame */
};
/* The rooms a texture key (or, with an empty key, a palette key) is used in: the rollout
 * survey's data ($ROOD_MATERIAL_ROLLOUT, else beside the review's proposals file) plus the live
 * room; `status` says when the survey data is missing. */
std::vector<Use> where_used(const std::string& key, const std::string& clut, std::string* status);
bool go(int zone, int room, std::string* why); /* warp there (the review's Go) */

/* ---- view modes (the design's list, by name) ---- */
struct ViewMode {
    const char* name; /* "lit", "unlit", ... (command view.mode.<name>, RPC) */
    const char* title;
    bool vulkan_only; /* shows the lit picture on GL */
};
const std::vector<ViewMode>& view_modes();
std::string view_mode(); /* the current one's name ("" when a debug mode outside the list is on) */
bool set_view_mode(const std::string& name);
void register_commands(); /* view.mode.<name> (editor_commands.h) */
void view_mode_menu(); /* View > View mode */
} // namespace rood_inspect

extern "C" {
/* RPC editor_inspect (dev_rpc.c): {action "pick" | "state" | "items" | "where_used" | "go" |
 * "view_mode" | "view_modes" | "outliner" | "open_material", x, y | u, v | world, select, id, key,
 * clut, zone, room, name}. */
int pc_editor_inspect_rpc(const char* params, char* out, int size);
/* actor_native.c: a model mesh drawn for actor slot `slot` (-1: none). */
void pc_inspect_model_actor(uint32_t mesh, int slot);
}
