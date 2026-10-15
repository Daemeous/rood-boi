/* The editor's property schema (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Property schema"): each kind of object the editor edits registers its
 * fields once (name, label, category, type, range, unit, log scale, step,
 * default, accessors by the object's index, and what the per-value reset
 * restores), and Details (editor_details.cpp), undo names and the RPC
 * editor_schema use them. Kinds: the point, spot and rect light; a light
 * actor per prefab (its transform and the prefab's exposed parameters); the
 * fog volume (atmosphere.cpp registers it). Editor internals. */
#ifndef ROOD_EDITOR_SCHEMA_H
#define ROOD_EDITOR_SCHEMA_H
#include <functional>
#include <string>
#include <vector>

#include "editor_selection.h"
#include "light_prefabs.h"

namespace rood_schema {
using prefab::Value;

enum Type { TYPE_FLOAT, TYPE_INT, TYPE_BOOL, TYPE_COLOUR, TYPE_ENUM, TYPE_VEC3, TYPE_ANGLE };
const char* type_name(Type t); /* "float", "int", "bool", "colour", "enum", "vec3", "angle" */

struct Field {
    std::string name, label, category, unit, tooltip;
    Type type = TYPE_FLOAT;
    float min = 0, max = 1; /* every component's (bool 0..1, enum 0..options-1) */
    float step = 0.01f; /* drag speed per pixel; the value ladder opens on its nearest step */
    bool logarithmic = false;
    bool resettable = true; /* has a reset (a position has none) */
    bool shared = true; /* an edit goes to every selected object of the kind (a position: the edited one only) */
    std::vector<std::string> options; /* TYPE_ENUM */
    Value def; /* the schema's default */
    std::function<Value(int)> get; /* by the object's index */
    std::function<void(int, const Value&)> set;
    /* What reset restores when it is not `def` (a preset's or a prefab's
     * value), and where it comes from ("default", "preset", "prefab", "fixture type"). */
    std::function<Value(int)> base;
    std::function<const char*(int)> base_source;
    /* Whether reset has something to undo (an actor: it overrides the parameter); null: get != base. */
    std::function<bool(int)> overridden;
    std::function<void(int)> reset; /* null: set(base) */
};

struct Kind {
    std::string name, label; /* "point" / "Point light"; an actor's "actor:<prefab id>" / the prefab's name */
    rood_selection::Kind object = rood_selection::KIND_NONE;
    std::vector<Field> fields;
    std::vector<std::string> closed; /* categories Details shows closed at first */
    std::function<void(int)> extras; /* Details: the kind's own tools under its fields */
    const Field* field(const std::string& name) const;
};

/* Field factories (each kind then sets get / set). */
Field number_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* unit = "", bool logarithmic = false, const char* tooltip = "");
Field angle_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* tooltip = "");
Field bool_field(const char* name, const char* label, const char* category, bool def, const char* tooltip = "");
Field colour_field(const char* name, const char* label, const char* category, float max, Value def, const char* tooltip = "");
Field vec3_field(const char* name, const char* label, const char* category, float min, float max, float step, Value def,
    const char* unit = "", const char* tooltip = "");
Field enum_field(const char* name, const char* label, const char* category, const std::vector<std::string>& options,
    int def, const char* tooltip = "");

/* The kind of an object and its index (null for glows and what is gone). */
const Kind* kind_of(const rood_selection::Ref& r, int* index);
const Kind* find_kind(const std::string& name);
std::vector<const Kind*> all_kinds(); /* the lights', the fog volume's, one per prefab in the library */
std::vector<std::string> lint(); /* problems; none: every field is complete */

Value base_of(const Field& f, int index);
const char* base_source_of(const Field& f, int index);
bool is_overridden(const Field& f, int index);
void reset_field(const Field& f, int index);
Value clamp(const Field& f, const Value& v);
bool same(const Field& f, const Value& a, const Value& b);
std::string format(const Field& f, const Value& v); /* "1.25 u", "Spot", "on" */
/* A lighting value as text ("1.5", "1,0.5,0", "sphere"). */
Value from_text(Type type, const std::vector<std::string>& options, const std::string& text);
std::string to_json(const Field& f, const Value& v);
bool from_json(const Field& f, const prefab::Json& j, Value& out, std::string* error);

/* The objects an edit of `target`'s field goes to: it, and with a shared
 * field every other selected object of the same kind (when it is selected). */
std::vector<rood_selection::Ref> targets(const Kind& k, const Field& f, const rood_selection::Ref& target);
/* Edits (Details, the value ladder, the RPC): the value clamped to its
 * range, to every target; the room is unsaved; the undo step is named
 * "Edit <label>" / "Reset <label>". With a vec3 `component` (0-2), the other
 * targets take that component only (their other two stay their own). */
void apply(const Kind& k, const Field& f, const rood_selection::Ref& target, const Value& v, int component = -1);
void apply_reset(const Kind& k, const Field& f, const rood_selection::Ref& target);
} // namespace rood_schema

extern "C" int pc_editor_schema_rpc(const char* params, char* out, int size);
#endif
