/* The editor's selection (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Selection model"): one set of typed references to the room's objects,
 * shared by the viewport, the Outliner, Details, undo and the RPC. A
 * reference is the object's lighting item id ("light:<id>", "actor:<id>",
 * "fog:<id>", "emitter:<id>", "glow:<key>"; lighting_layers.h), so it survives edits, undo
 * and reloads where list indices would not. The last reference is the
 * primary: the gizmo's and Details' object.
 *
 * Also the objects hidden in the editor (H, Shift+H, Ctrl+H): editor state
 * per room, kept in the user settings (keys editor_hidden_<zone>_<room>),
 * never in the lighting data.
 *
 * No Dear ImGui and no editor objects here (pc/tests/selection_probe.cpp
 * tests it); editor_scene.cpp connects it to the room's objects. */
#ifndef ROOD_EDITOR_SELECTION_H
#define ROOD_EDITOR_SELECTION_H
#include <stdio.h>
#include <functional>
#include <string>
#include <vector>

namespace rood_selection {

enum Kind { KIND_NONE, KIND_LIGHT, KIND_ACTOR, KIND_GLOW, KIND_VOLUME, KIND_SUN, KIND_EMITTER };
struct Ref {
    Kind kind = KIND_NONE;
    std::string id; /* the lighting item id, its prefix included */
    bool operator==(const Ref& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const Ref& o) const { return !(*this == o); }
    explicit operator bool() const { return kind != KIND_NONE; }
};
/* A reference from an item id, by its prefix ("env:sun": the room's sun or moon; KIND_NONE when unknown). */
Ref ref_of(const std::string& id);
const char* kind_name(Kind k); /* "light", "actor", "glow", "fog", "sun", "emitter", "none" */

/* Where a change came from: the Outliner scrolls to the new primary unless
 * the change came from it; Details switches to it unless the change came
 * from Details itself or from undo. */
enum Source { FROM_VIEWPORT, FROM_OUTLINER, FROM_DETAILS, FROM_RPC, FROM_UNDO, FROM_CODE };
const char* source_name(Source s); /* "viewport", "outliner", "details", "rpc", "undo", "code" */

const std::vector<Ref>& items(); /* in the order chosen; the last is the primary */
Ref primary();
bool contains(const Ref& r);
bool empty();
void set(const Ref& r, Source s); /* only r (an empty ref clears) */
void add(const Ref& r, Source s); /* r too, as the primary */
void toggle(const Ref& r, Source s); /* r in or out */
void remove(const Ref& r, Source s);
void clear(Source s);
void set_all(const std::vector<Ref>& refs, Source s); /* the last is the primary */
/* A click on r: plain replaces, Shift adds, Ctrl toggles (Unreal). */
void click(const Ref& r, bool shift, bool ctrl, Source s);
/* A box around refs: plain replaces, Shift adds them, Ctrl toggles each. */
void box(const std::vector<Ref>& refs, bool shift, bool ctrl, Source s);
/* Keep the refs `alive` accepts (objects deleted or reloaded away); true
 * when any went (a change from `s`). */
bool retain(const std::function<bool(const Ref&)>& alive, Source s);
unsigned serial(); /* bumps on every change */
Source last_source();

/* Details' lock: pinned to the primary at the time (lock(false): off). */
void lock(bool on);
Ref locked();

/* ---- hidden in the editor ---- */
void set_room(int zone, int room); /* the room hiding applies to */
bool hidden(const Ref& r);
bool any_hidden(); /* in this room */
void hide(const std::vector<Ref>& refs);
void unhide_all(); /* this room's */
std::vector<Ref> hidden_items(); /* this room's */
/* Settings lines (project_settings.cpp): "editor_hidden_<zone>_<room> <id> ...",
 * one per room with hidden objects. */
std::string hidden_line(int zone, int room); /* "" when none */
void write_hidden(FILE* f);
/* A settings key and the rest of its line: true when it is a hidden set
 * (its room's set replaced). */
bool parse_hidden(const char* key, const char* rest);
void clear_hidden(); /* every room's (the settings file is read again) */

} // namespace rood_selection
#endif
