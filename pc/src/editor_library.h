/* The editor's library (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Library"; pc/docs/plans/2026-10-16-artist-tools-de-plan.md decisions 5-9):
 * one panel for everything one places: basic lights, light prefabs,
 * fixtures, fog volume presets and whatever else registers a source (the
 * emitters). Each source is a kind: it lists entries (library_model.h),
 * places one where a drop lands, and describes its thumbnail's scene
 * (library_thumbs.h). Drops use the lights' drag and drop (lights_internal.h
 * place_*): rood_lights::place_commit hands every item that is not a basic
 * light or a light prefab to place() here. Editor internals; RPC editor_library. */
#ifndef ROOD_EDITOR_LIBRARY_H
#define ROOD_EDITOR_LIBRARY_H
#include <stdio.h>
#include <functional>
#include <string>
#include <vector>

#include "editor_overlays.h"
#include "library_model.h"
#include "library_thumbs.h"
#include "lights_internal.h"

namespace rood_library {
/* A source of entries: one per kind. An emitters source by hand, say:
 *
 *     rood_library::Source s;
 *     s.kind = "emitter";
 *     s.serial = [] { return emitters::serial(); };
 *     s.list = [](std::vector<rood_library::Entry>& out) { ... one per emitters::list() ... };
 *     s.place = [](const rood_library::Entry& e, const rood_lights::Placement& w) { return emitters::place(e.id, w.surface, w.normal); };
 *     rood_library::add_source(s);
 */
struct Source {
    std::string kind;
    std::function<unsigned()> serial; /* changes whenever its entries may have */
    std::function<void(std::vector<Entry>&)> list; /* its entries (kind, item "<kind>:<id>", id, name, category, ...) */
    /* Put `e` where a drop landed (rood_lights::place_compute's Placement):
     * the new object's index, selected; -1 when it cannot. The caller names
     * the undo step ("Place <name>"). */
    std::function<int(const Entry& e, const rood_lights::Placement& where)> place;
    /* Its thumbnail's scene; false (or null): the tile shows its type icon only. */
    std::function<bool(const Entry& e, rood_thumbs::Scene& out)> scene;
    /* Optional: what it looks like while dragged over the view (a fog volume's box). */
    std::function<void(const Entry& e, const pc_overlay_view& v, const rood_lights::Placement& where, void* draw_list)> ghost;
    /* Optional: the prefab whose placement (align, surface offset) and lit preview a drop uses (a fixture's). */
    std::function<const prefab::Prefab*(const Entry& e)> prefab;
    /* Optional, for the user's own entries (Mine; the tile's right-click menu, RPC): its file ("": none),
     * a new display name (its id, which rooms name, stays), deleted (moved aside, never erased). True when
     * done; `message` says what happened or why not. */
    std::function<std::string(const Entry& e)> file;
    std::function<bool(const Entry& e, const std::string& name, std::string* message)> rename;
    std::function<bool(const Entry& e, std::string* message)> remove;
};
void add_source(Source s); /* any time; replaces the source of the same kind */
/* A source listing a kind's JSON files: <builtin_dir>/*.json, then <user_dir>/*.json (Mine), one entry
 * each ("<kind>:<file name>"; its "name", "category", "description", "tags" members; a parse problem on
 * its tile). Placing (and a thumbnail) is the caller's to add: without them a drop says so. The
 * emitters (pc/assets/emitters, ROOD_EMITTERS; <user prefab dir>/emitters) are one, built in: the
 * emitters' own code completes it with
 *
 *     rood_library::Source s = rood_library::json_dir_source("emitter", dirs...);
 *     s.place = ...;
 *     rood_library::add_source(s);
 */
Source json_dir_source(const std::string& kind, const std::string& builtin_dir, const std::string& user_dir,
    const std::string& category);
std::string emitters_dir(); /* ROOD_EMITTERS, else pc/assets/emitters */
std::vector<std::string> source_kinds(); /* registered, in kind order */
const std::vector<Entry>& entries(); /* every source's, sorted by kind (rebuilt when a source's serial changes) */
const Entry* find(const std::string& item);
/* An item placing goes through here: one of the library's, not a basic light ("light:") or a light prefab ("prefab:"). */
bool is_item(const std::string& item);
const prefab::Prefab* item_prefab(const std::string& item); /* null: none */
std::string label(const std::string& item); /* its name, else the item */
int place(const std::string& item, const rood_lights::Placement& where);
void draw_ghost(const std::string& item, const pc_overlay_view& v, const rood_lights::Placement& where, void* draw_list);

/* The panel (Task 17), Save as prefab (Task 18). */
void panel(); /* the side column's Library tab, or the floating window's body */
bool docked(); /* in the side column (else a floating window) */
void window(); /* each overlay frame: the floating window when undocked, Save as prefab's dialog */
void request_save(); /* opens Save as prefab's dialog (next frame) */
bool save_menu_item(); /* "Save as prefab..." in a menu (disabled with nothing selected); true when picked */
std::string save_selection(const std::string& name, std::string* message); /* the new item, or "" and why */
/* An entry's management (its right-click menu; RPC editor_library edit_prefab, rename, delete, file). */
bool edit_prefab(const std::string& item, std::string* message); /* the prefab editor on its prefab */
bool rename(const std::string& item, const std::string& name, std::string* message);
bool remove(const std::string& item, std::string* message);
std::string file_of(const std::string& item); /* the user's file behind it, else "" */

/* Settings lines (project_settings.cpp): editor_library_view, editor_library_docked,
 * editor_library_window, editor_library_favourites. */
bool parse_setting(const char* key, const char* rest);
void write_settings(FILE* f);
} // namespace rood_library

extern "C" int pc_editor_library_rpc(const char* params, char* out, int size);
#endif
