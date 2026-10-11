// Lighting packs: per-room lighting data merged from layers (design and file
// format: lighting/README.md).
//
// A room's lighting is a set of items with stable ids: lights
// ("light:<id>"), light actors ("actor:<id>"), glow overrides
// ("glow:<glow key>"), texture effect overrides ("uv:<effect>") and room
// setting overrides ("set:<key>"). Layers, lowest first:
//   for each pack in lighting_packs (settings key, default "default"):
//     lighting/<pack>/generated/zoneZZ_roomRR.txt   generated (never hand-edited)
//     lighting/<pack>/rooms/zoneZZ_roomRR.txt       authored (hand-edited, PRs)
//   pc/user/lighting/zoneZZ_roomRR.txt             the user's own (untracked)
// A layer's file defines items, overrides fields of items from the layers
// below ("override <id> key=value ... !key") or deletes them ("delete <id>").
// The effective room is the merge; an override or delete whose id no layer
// below defines is a problem (reported, and kept in its file, never dropped).
//
// The editor works on the effective room; saving writes the user layer as
// the difference from the layers below it; "Promote" moves user-layer
// entries into the authored layer of the active pack.
//
// Environment: ROOD_LIGHTING names the packs' root (default "lighting");
// ROOD_LIGHTS names the user layer's directory (tests); when ROOD_LIGHTS is
// set and ROOD_LIGHTING is not, no packs load (tests stay isolated). Without
// ROOD_LIGHTS the user layer is pc/user/lighting, and a room with no file
// there reads the old pc/lights/zoneZZ_roomRR.txt (migrated on save).
//
// No Dear ImGui here (pc/tests/lighting_probe.cpp tests it).
#ifndef ROOD_LIGHTING_LAYERS_H
#define ROOD_LIGHTING_LAYERS_H
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "lights_internal.h"

namespace lighting {

constexpr int FORMAT_VERSION = 2;

using Fields = std::vector<std::pair<std::string, std::string>>;

struct Item {
    std::string kind, name; /* id = kind + ":" + name */
    Fields fields; /* canonical: known fields in schema order, then others by name; defaults left out */
    std::string id() const { return kind + ":" + name; }
    const std::string* get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);
    void erase(const std::string& key);
    bool operator==(const Item& o) const { return kind == o.kind && name == o.name && fields == o.fields; }
};

enum Verb { DEFINE, OVERRIDE, REMOVE };
struct Entry {
    Verb verb = DEFINE;
    std::string id; /* the item's (DEFINE) or the target's id */
    Item item; /* DEFINE */
    Fields set; /* OVERRIDE: fields set */
    std::vector<std::string> unset; /* OVERRIDE: fields back to their defaults */
    int line = 0; /* in its file (1-based; 0: new) */
};

struct File {
    std::string path;
    bool exists = false;
    int version = 0; /* 0: the old one-line-per-light format (pc/lights) */
    std::vector<Entry> entries;
    std::vector<std::string> errors; /* "line N: ..." */
    long long mtime = 0;
};

/* ---- values ---- */
/* Decimals a field's numbers keep (-1: integers kept as written). */
int field_decimals(const std::string& kind, const std::string& key);
/* Fixed formatting: rounded to `decimals`, trailing zeros dropped, no -0. */
std::string format_number(double v, int decimals);
/* A field value in canonical form (numbers and comma vectors reformatted;
 * anything else unchanged). */
std::string canonical_value(const std::string& kind, const std::string& key, const std::string& value);
/* The default of a field ("" when it has none). */
const char* field_default(const std::string& kind, const std::string& key);
/* Whether the kind knows this field (actors: any other key is a prefab
 * parameter). */
bool known_field(const std::string& kind, const std::string& key);
bool known_kind(const std::string& kind);
/* Canonical field order, values and defaults dropped. */
void canonicalize(Item& item);
/* A fresh id name: 14 hex digits, time-ordered (seconds, counter, random). */
std::string new_name();

/* ---- files ---- */
bool parse_text(const std::string& text, File& out);
bool read_file(const std::string& path, File& out);
std::string format_entry(const Entry& e);
/* The whole file: header, comment, entries sorted by id. */
std::string format_file(const std::vector<Entry>& entries, const std::string& comment);
bool write_file(const std::string& path, const std::string& text, std::string* error);
/* Rewrite a hand-edited file: the entries for `ids` replaced by `entries`
 * (missing ones removed, new ones inserted in id order), every other line
 * and comment kept as it is. */
std::string patch_text(const std::string& old_text, const std::vector<std::string>& ids,
    const std::vector<Entry>& entries, const std::string& comment);
long long file_mtime(const std::string& path);

/* ---- merging ---- */
using State = std::map<std::string, Item>; /* by id */
/* Apply one entry; false (and *problem) when it refers to a missing item. */
bool apply_entry(State& state, const Entry& e, std::string* problem);
/* The entries that turn `below` into `desired` (sorted by id): new items
 * defined, changed lights and actors overridden field by field (other
 * kinds redefined), missing ones deleted. */
std::vector<Entry> diff(const State& below, const State& desired);
/* The one entry for `id` (verb DEFINE with an empty id when nothing). */
bool diff_one(const State& below, const State& desired, const std::string& id, Entry& out);

/* ---- layers ---- */
enum LayerKind { GENERATED, AUTHORED, USER };
struct Layer {
    LayerKind kind = USER;
    std::string pack; /* "" for the user layer */
    std::string path; /* the file (may not exist) */
    std::string legacy; /* user layer: the old pc/lights file read when `path` is missing */
    File file;
    std::string label() const; /* "default/generated", "default/rooms", "user" */
    char badge() const; /* G, P, U */
};
struct Provenance {
    int defined = -1; /* layer index */
    std::vector<int> changed; /* layers that override it (or redefine it) above */
};
struct Problem {
    int layer = -1, line = 0;
    std::string id, message;
};
struct Room {
    int zone = -1, room = -1;
    std::vector<Layer> layers; /* lowest first; the last is the user layer */
    State base; /* every layer below the user's */
    State loaded; /* base + the user layer as read */
    std::map<std::string, Provenance> provenance; /* of the base (the user layer is compared live) */
    std::vector<Problem> problems;
    std::vector<Entry> user_dangling; /* user-layer entries with nothing to apply to: kept on save */
    std::vector<std::string> deleted; /* ids the layers deleted (for the Outliner) */
    int active = -1; /* the authored layer Promote writes (-1: no packs) */
};

std::string packs_root(); /* "" when packs are off */
std::string user_dir();
std::string legacy_dir(); /* "" when the user dir is ROOD_LIGHTS itself */
/* The configured packs, lowest first (settings key lighting_packs). */
std::vector<std::string> packs();
void set_packs(const std::string& comma_list); /* settings */
std::string packs_setting(); /* the comma list as set */
std::string active_pack_setting();
void set_active_pack(const std::string& name); /* "" = the highest */
/* Bumps whenever the pack list or root changes (the editor reloads). */
int config_serial();
std::string room_file_name(int zone, int room);

/* Read every layer of the room and merge them. */
void load_room(Room& r, int zone, int room);
/* Merge `upto` layers (exclusive) into a state, recording problems. */
State merge(const Room& r, int upto, std::vector<Problem>* problems = nullptr,
    std::map<std::string, Provenance>* provenance = nullptr, std::vector<Entry>* dangling_of_last = nullptr,
    std::vector<std::string>* deleted = nullptr);
/* Whether any layer file changed (or appeared, or went) since it was read. */
bool changed_on_disk(const Room& r);
/* The user layer for `current`: the diff from the base, plus the dangling
 * entries kept; the file text (empty when there is nothing to keep). */
std::string user_layer_text(const Room& r, const State& current);
/* Write the user layer (removes the file when empty, unless an old
 * pc/lights file would then be read instead). */
bool save_user(Room& r, const State& current, std::string* message);
/* Move the user layer's entries for `ids` (all of them when empty) into
 * the active pack's authored layer; then rewrites the user layer. Reloads
 * the layers (r.base, provenance) without touching `current`. */
bool promote(Room& r, const State& current, const std::vector<std::string>& ids, std::string* message);
/* "G", "P", "U", "G>P>U": where an item comes from and what changes it,
 * comparing `current` with the base for the user layer. */
std::string badge(const Room& r, const State& current, const std::string& id);

/* ---- conversion to and from the editor's lights and actors ---- */
Item item_from_light(const rood_lights::Light& l);
bool light_from_item(const Item& it, rood_lights::Light& l);
Item item_from_actor(const prefab::Actor& a);
bool actor_from_item(const Item& it, prefab::Actor& a);
/* Old-format lines (pc/lights) as items; lights and actors get new ids. */
void legacy_items(const std::string& text, std::vector<Item>& out);

/* The schema as JSON (tests compare it with pc/tools/lighting_format.py). */
std::string schema_json();

} // namespace lighting
#endif
