/* The editor's objects for its selection (editor_selection.h): the room's
 * lights, light actors, the game's glows and fog volumes as references,
 * their list indices and labels; the per-kind state the editor's older code
 * reads (lights_internal.h selected, selected_actor, multi, glow_selected,
 * glow_id; atmosphere.cpp's selected volume) kept in step with the
 * selection; the viewport's icons and picking (an icon, else a ray against
 * the fog volumes) and box select; the Outliner's rows following the
 * selection; hiding, deleting, copying and pasting the selection
 * (editor_clipboard.cpp); the RPC editor_select. Editor internals. */
#ifndef ROOD_EDITOR_SCENE_H
#define ROOD_EDITOR_SCENE_H
#include <stdint.h>
#include <string>
#include <vector>

#include "editor_overlays.h"
#include "editor_selection.h"

namespace rood_scene {
using rood_selection::Ref;

/* ---- references (light and actor ids are assigned on the spot) ---- */
Ref light_ref(int index);
Ref actor_ref(int index);
Ref volume_ref(int index);
Ref glow_ref(uint32_t id); /* by its key; "glow:@x,y,z" when it has none */
Ref glow_ref_key(const char* key, const float position[3]);
/* A light's, actor's or volume's list index, a glow's id; -1 when it is gone. */
int index_of(const Ref& r);
bool exists(const Ref& r); /* glows always: they come and go with the game */
std::string label(const Ref& r); /* "Point light 3", "Candle 2", "Box 1: Cellar mist", a glow's name */
bool position(const Ref& r, float out[3], float* radius); /* where F frames it */

/* ---- in step ---- */
void frame(); /* each overlay frame: the room, refs to what is gone dropped, publish */
void refresh(); /* refs to what is gone dropped, then publish */
void publish(); /* the per-kind state from the selection */

/* ---- the Outliner ---- */
void row_clicked(const Ref& r); /* a row's click: Shift adds, Ctrl toggles, else selects */
void row_drawn(const Ref& r); /* after a row's item: the primary's row is scrolled to; its visibility kept (RPC) */
/* Whether the section holding kind a (or b) must open: the primary is one and a scroll to it is due. */
bool scroll_due(rood_selection::Kind a, rood_selection::Kind b = rood_selection::KIND_NONE);

/* ---- the viewport ---- */
void icons_begin(); /* lights.cpp, each frame before drawing */
/* An object's icon at window pixel (x, y): pickable when radius > 0; the
 * anchor is where box select and the RPC see it. */
void icon(const Ref& r, float x, float y, float radius, bool anchor);
Ref object_at(float x, float y); /* the icon under the pixel: lights first, then actors, then glows */
/* The fog volume a ray from the pixel enters first, unless a room surface is in front of it. */
Ref volume_at(const pc_overlay_view& v, pc_overlay_pick pick, float x, float y);
std::vector<Ref> in_box(float x0, float y0, float x1, float y1); /* lights, actors, volumes (not glows) */
/* A click in the view at (x, y) as the RPC makes it: what is under it is
 * selected (Shift adds, Ctrl toggles); a plain click on nothing clears. */
Ref click_at(float x, float y, bool shift, bool ctrl, rood_selection::Source s);

/* ---- hidden in the editor: no icon, not pickable, and (while the editor
 * shows) their light and fog left out ---- */
bool light_hidden(int index);
bool actor_hidden(int index);
bool volume_hidden_id(const std::string& id); /* by a volume's bare id */
bool glow_hidden(const char* key, const float position[3]);
void hide_selection(); /* H */
void isolate_selection(); /* Shift+H: everything else */
void unhide_all(); /* Ctrl+H */

/* ---- operations on the selection ---- */
int delete_selection(); /* lights, actors and volumes; how many went */
/* editor_clipboard.cpp: the selection as lighting entries (lighting_layers.h), and back as new objects. */
std::string copy_text();
int copy_selection(); /* Ctrl+C: the editor's clipboard and the system's */
const std::string& clipboard();
int paste_text(const std::string& text, const float offset[3], const char* verb);
int paste_clipboard(); /* Ctrl+V: where they were copied */
int duplicate_selection(); /* Ctrl+D: 64 units along +x */
} // namespace rood_scene

extern "C" int pc_editor_select_rpc(const char* params, char* out, int size);
#endif
