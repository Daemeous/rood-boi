// The editor's property schema (editor_schema.h): the value helpers, the
// lights' and light actors' kinds, the lint and the RPC editor_schema.
#include "editor_schema.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "editor_details.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lights_internal.h"

using rood_selection::Ref;

namespace rood_schema {

const char* type_name(Type t)
{
    static const char* const names[] = { "float", "int", "bool", "colour", "enum", "vec3", "angle" };
    return names[t];
}
const Field* Kind::field(const std::string& name) const
{
    for (const Field& f : fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

/* ---- factories ---- */

Field number_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* unit, bool logarithmic, const char* tooltip)
{
    Field f;
    f.name = name, f.label = label, f.category = category, f.unit = unit, f.tooltip = tooltip;
    f.type = TYPE_FLOAT, f.min = min, f.max = max, f.step = step, f.logarithmic = logarithmic;
    f.def = Value::scalar(def);
    return f;
}
Field angle_field(const char* name, const char* label, const char* category, float min, float max, float step, float def,
    const char* tooltip)
{
    Field f = number_field(name, label, category, min, max, step, def, "deg", false, tooltip);
    f.type = TYPE_ANGLE;
    return f;
}
Field bool_field(const char* name, const char* label, const char* category, bool def, const char* tooltip)
{
    Field f = number_field(name, label, category, 0, 1, 1, def ? 1.0f : 0.0f, "", false, tooltip);
    f.type = TYPE_BOOL;
    return f;
}
Field colour_field(const char* name, const char* label, const char* category, float max, Value def, const char* tooltip)
{
    Field f = number_field(name, label, category, 0, max, 0.01f, 0, "", false, tooltip);
    f.type = TYPE_COLOUR;
    f.def = def;
    return f;
}
Field vec3_field(const char* name, const char* label, const char* category, float min, float max, float step, Value def,
    const char* unit, const char* tooltip)
{
    Field f = number_field(name, label, category, min, max, step, 0, unit, false, tooltip);
    f.type = TYPE_VEC3;
    f.def = def;
    return f;
}
Field enum_field(const char* name, const char* label, const char* category, const std::vector<std::string>& options,
    int def, const char* tooltip)
{
    Field f = number_field(name, label, category, 0, options.empty() ? 0.0f : (float)(options.size() - 1), 1, (float)def,
        "", false, tooltip);
    f.type = TYPE_ENUM;
    f.options = options;
    return f;
}

/* ---- values ---- */

namespace {
int components(const Field& f) { return f.type == TYPE_COLOUR || f.type == TYPE_VEC3 ? 3 : 1; }
int decimals(float step) { return step >= 1 ? 0 : step >= 0.1f ? 1 : step >= 0.01f ? 2 : step >= 0.001f ? 3 : 5; }
bool same_word(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}
} // namespace

Value base_of(const Field& f, int index) { return f.base ? f.base(index) : f.def; }
const char* base_source_of(const Field& f, int index) { return f.base_source ? f.base_source(index) : "default"; }
bool is_overridden(const Field& f, int index)
{
    return f.overridden ? f.overridden(index) : !same(f, f.get(index), base_of(f, index));
}
void reset_field(const Field& f, int index)
{
    if (f.reset)
        f.reset(index);
    else
        f.set(index, base_of(f, index));
}
Value clamp(const Field& f, const Value& in)
{
    int n = components(f);
    Value v = n == 3 ? Value::vec(in.at(0), in.at(1), in.at(2)) : Value::scalar(in.x());
    for (int c = 0; c < n; c++) {
        float x = v.v[c];
        if (!std::isfinite(x))
            x = f.def.at(c);
        x = fminf(fmaxf(x, f.min), f.max);
        if (f.type == TYPE_INT || f.type == TYPE_ENUM)
            x = roundf(x);
        if (f.type == TYPE_BOOL)
            x = x > 0.5f ? 1.0f : 0.0f;
        v.v[c] = x;
    }
    return v;
}
bool same(const Field& f, const Value& a, const Value& b)
{
    for (int c = 0; c < components(f); c++)
        if (fabsf(a.at(c) - b.at(c)) > 1e-5f * fmaxf(1.0f, fabsf(b.at(c))))
            return false;
    return true;
}
std::string format(const Field& f, const Value& v)
{
    char text[128];
    int d = decimals(f.step);
    switch (f.type) {
    case TYPE_BOOL:
        return v.x() > 0.5f ? "on" : "off";
    case TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        return i >= 0 && i < (int)f.options.size() ? f.options[i] : std::to_string(i);
    }
    case TYPE_COLOUR:
        snprintf(text, sizeof text, "%.3f, %.3f, %.3f", v.at(0), v.at(1), v.at(2));
        return text;
    case TYPE_VEC3:
        snprintf(text, sizeof text, "%.*f, %.*f, %.*f", d, v.at(0), d, v.at(1), d, v.at(2));
        break;
    default:
        snprintf(text, sizeof text, "%.*f", d, v.x());
        break;
    }
    return f.unit.empty() ? std::string(text) : std::string(text) + " " + f.unit;
}
Value from_text(Type type, const std::vector<std::string>& options, const std::string& text)
{
    if (type == TYPE_ENUM) {
        for (size_t i = 0; i < options.size(); i++)
            if (same_word(options[i], text))
                return Value::scalar((float)i);
        return Value::scalar((float)atof(text.c_str()));
    }
    if (type == TYPE_COLOUR || type == TYPE_VEC3) {
        float x[3];
        if (sscanf(text.c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) == 3)
            return Value::vec(x[0], x[1], x[2]);
        float s = (float)atof(text.c_str());
        return Value::vec(s, s, s);
    }
    return Value::scalar((float)atof(text.c_str()));
}
std::string to_json(const Field& f, const Value& v)
{
    char text[128];
    switch (f.type) {
    case TYPE_BOOL:
        return v.x() > 0.5f ? "true" : "false";
    case TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        return i >= 0 && i < (int)f.options.size() ? prefab::json_quote(f.options[i]) : std::to_string(i);
    }
    case TYPE_COLOUR:
    case TYPE_VEC3:
        snprintf(text, sizeof text, "[%.6g,%.6g,%.6g]", v.at(0), v.at(1), v.at(2));
        return text;
    default:
        snprintf(text, sizeof text, "%.6g", v.x());
        return text;
    }
}
bool from_json(const Field& f, const prefab::Json& j, Value& out, std::string* error)
{
    using prefab::Json;
    if (f.type == TYPE_BOOL && (j.type == Json::Bool || j.type == Json::Number)) {
        out = Value::scalar((j.type == Json::Bool ? j.boolean : j.number != 0) ? 1.0f : 0.0f);
        return true;
    }
    if (f.type == TYPE_ENUM && j.type == Json::String) {
        for (size_t i = 0; i < f.options.size(); i++)
            if (same_word(f.options[i], j.string)) {
                out = Value::scalar((float)i);
                return true;
            }
        if (error)
            *error = "no option " + j.string + " for " + f.name;
        return false;
    }
    if ((f.type == TYPE_COLOUR || f.type == TYPE_VEC3) && j.type == Json::Array && j.items.size() == 3
        && j.items[0].type == Json::Number && j.items[1].type == Json::Number && j.items[2].type == Json::Number) {
        out = Value::vec((float)j.items[0].number, (float)j.items[1].number, (float)j.items[2].number);
        return true;
    }
    if (f.type == TYPE_COLOUR && j.type == Json::Number) { /* a grey */
        out = Value::vec((float)j.number, (float)j.number, (float)j.number);
        return true;
    }
    if (f.type != TYPE_BOOL && f.type != TYPE_COLOUR && f.type != TYPE_VEC3 && j.type == Json::Number) {
        out = Value::scalar((float)j.number);
        return true;
    }
    if (error)
        *error = "bad value for " + f.name + " (a " + type_name(f.type) + ")";
    return false;
}

/* ---- kinds ---- */

namespace {
using rood_lights::actors;
using rood_lights::lights;

Value vec_of(const float* v) { return Value::vec(v[0], v[1], v[2]); }
void put(float* out, const Value& v)
{
    for (int k = 0; k < 3; k++)
        out[k] = v.at(k);
}

/* A point (0), spot (1) or rect (2) light; a new light's values are the defaults. */
Kind light_kind(int type)
{
    const rood_lights::Light d = rood_lights::Light();
    Kind k;
    k.object = rood_selection::KIND_LIGHT;
    k.name = type == 1 ? "spot" : type == 2 ? "rect" : "point";
    k.label = rood_lights::light_type_name(type);
    k.extras = [](int i) { rood_lights::light_details_extra(i); };
    Field f = enum_field("type", "Type", "Light", { "Point", "Spot", "Rect" }, type, "Point, spot or rect (its fields follow)");
    f.resettable = false;
    f.get = [](int i) { return Value::scalar((float)lights[i].type); };
    f.set = [](int i, const Value& v) { lights[i].type = (int)lroundf(v.x()); };
    k.fields.push_back(f);
    f = bool_field("enabled", "Enabled", "Light", d.enabled, "Off: it lights nothing (the Outliner's box)");
    f.get = [](int i) { return Value::scalar(lights[i].enabled ? 1.0f : 0.0f); };
    f.set = [](int i, const Value& v) { lights[i].enabled = v.x() > 0.5f; };
    k.fields.push_back(f);
    f = number_field("intensity", "Intensity", "Light", 0, 8, 0.01f, d.intensity, "", false, "Scale on its colour (linear light)");
    f.get = [](int i) { return Value::scalar(lights[i].intensity); };
    f.set = [](int i, const Value& v) { lights[i].intensity = v.x(); };
    k.fields.push_back(f);
    f = colour_field("color", "Colour", "Light", 1, vec_of(d.color), "Linear colour");
    f.get = [](int i) { return vec_of(lights[i].color); };
    f.set = [](int i, const Value& v) { put(lights[i].color, v); };
    k.fields.push_back(f);
    f = number_field("radius", "Radius", "Light", 32, 4096, 1, d.radius, "u", true, "Its reach: the light falls to nothing here");
    f.get = [](int i) { return Value::scalar(lights[i].radius); };
    f.set = [](int i, const Value& v) { lights[i].radius = v.x(); };
    k.fields.push_back(f);
    f = bool_field("shadows", "Casts shadows", "Light", d.shadows);
    f.get = [](int i) { return Value::scalar(lights[i].shadows ? 1.0f : 0.0f); };
    f.set = [](int i, const Value& v) { lights[i].shadows = v.x() > 0.5f; };
    k.fields.push_back(f);
    f = vec3_field("position", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "World units (y is down)");
    f.resettable = false, f.shared = false;
    f.get = [](int i) { return vec_of(lights[i].position); };
    f.set = [](int i, const Value& v) { put(lights[i].position, v); };
    k.fields.push_back(f);
    if (type != 0) {
        f = vec3_field("direction", "Direction", "Transform", -1, 1, 0.01f, vec_of(d.direction), "",
            "Where it points (world, y down; the rotate tool, E)");
        f.get = [](int i) { return vec_of(lights[i].direction); };
        f.set = [](int i, const Value& v) { put(lights[i].direction, v); };
        k.fields.push_back(f);
    }
    if (type == 1) {
        f = angle_field("cone", "Cone", "Shape", 5, 170, 0.5f, d.cone, "Its full opening angle");
        f.get = [](int i) { return Value::scalar(lights[i].cone); };
        f.set = [](int i, const Value& v) { lights[i].cone = v.x(); };
        k.fields.push_back(f);
        f = angle_field("inner", "Inner cone", "Shape", 0, 170, 0.5f, d.inner,
            "Full brightness inside this angle, fading out to the cone's edge; 0: the outer quarter fades");
        f.get = [](int i) { return Value::scalar(lights[i].inner); };
        f.set = [](int i, const Value& v) { lights[i].inner = v.x(); };
        k.fields.push_back(f);
    }
    if (type == 2) {
        f = number_field("width", "Width", "Shape", 8, 4096, 1, d.size[0], "u");
        f.get = [](int i) { return Value::scalar(lights[i].size[0]); };
        f.set = [](int i, const Value& v) { lights[i].size[0] = v.x(); };
        k.fields.push_back(f);
        f = number_field("height", "Height", "Shape", 8, 4096, 1, d.size[1], "u");
        f.get = [](int i) { return Value::scalar(lights[i].size[1]); };
        f.set = [](int i, const Value& v) { lights[i].size[1] = v.x(); };
        k.fields.push_back(f);
        f = angle_field("barn_angle", "Barn door angle", "Barn doors", 0, 90, 0.5f, d.barn_angle,
            "Its four doors' angle from its facing: 0 straight ahead (the tightest beam), 90 folded flat (no effect)");
        f.get = [](int i) { return Value::scalar(lights[i].barn_angle); };
        f.set = [](int i, const Value& v) { lights[i].barn_angle = v.x(); };
        k.fields.push_back(f);
        f = number_field("barn_length", "Barn door length", "Barn doors", 0, 1024, 1, d.barn_length, "u", false,
            "How far the doors reach out from each edge; 0: no doors");
        f.get = [](int i) { return Value::scalar(lights[i].barn_length); };
        f.set = [](int i, const Value& v) { lights[i].barn_length = v.x(); };
        k.fields.push_back(f);
    }
    return k;
}

/* A light actor of a prefab: its own transform and switch, then the prefab's
 * exposed parameters (a change is the actor's override; reset drops it). */
Kind actor_kind(const std::string& prefab_id)
{
    const prefab::Prefab* p = prefab::library().find(prefab_id);
    Kind k;
    k.object = rood_selection::KIND_ACTOR;
    k.name = "actor:" + prefab_id;
    k.label = p ? p->name : prefab_id + " (missing prefab)";
    k.extras = [](int i) { rood_lights::actor_details_extra(i); };
    Field f = bool_field("enabled", "Enabled", "Actor", true, "Off: none of its lights shine");
    f.get = [](int i) { return Value::scalar(actors[i].enabled ? 1.0f : 0.0f); };
    f.set = [](int i, const Value& v) { actors[i].enabled = v.x() > 0.5f; };
    k.fields.push_back(f);
    f = vec3_field("position", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its pivot (world units, y down)");
    f.resettable = false, f.shared = false;
    f.get = [](int i) { return vec_of(actors[i].position); };
    f.set = [](int i, const Value& v) { put(actors[i].position, v); };
    k.fields.push_back(f);
    f = vec3_field("rotation", "Rotation", "Transform", -360, 360, 0.5f, Value::vec(0, 0, 0), "deg",
        "Yaw, pitch, roll (the rotate tool, E)");
    f.get = [](int i) { return vec_of(actors[i].rotation); };
    f.set = [](int i, const Value& v) { put(actors[i].rotation, v); };
    k.fields.push_back(f);
    f = number_field("scale", "Scale", "Transform", 0.05f, 20, 0.01f, 1, "", true,
        "Its size (the scale tool, R): its lights' offsets, radii and sizes scale with it, never their brightness");
    f.get = [](int i) { return Value::scalar(actors[i].scale); };
    f.set = [](int i, const Value& v) { actors[i].scale = v.x(); };
    k.fields.push_back(f);
    if (!p)
        return k;
    for (const prefab::Param& q : p->params) {
        Field pf;
        pf.name = q.key;
        pf.label = q.label.empty() ? q.key : q.label;
        pf.category = q.group.empty() ? "Parameters" : q.group;
        pf.unit = q.unit, pf.tooltip = q.tooltip;
        pf.type = q.type == prefab::PARAM_COLOR ? TYPE_COLOUR
            : q.type == prefab::PARAM_BOOL      ? TYPE_BOOL
            : q.type == prefab::PARAM_ENUM      ? TYPE_ENUM
                                                : TYPE_FLOAT;
        pf.options = q.options;
        pf.min = q.min, pf.max = q.max, pf.logarithmic = q.logarithmic;
        float span = q.max - q.min;
        pf.step = pf.type != TYPE_FLOAT ? (pf.type == TYPE_COLOUR ? 0.01f : 1.0f) : span >= 200 ? 1.0f : span >= 20 ? 0.1f : 0.01f;
        pf.def = q.def;
        std::string key = q.key, id = prefab_id;
        pf.get = [key, id](int i) {
            const prefab::Prefab* pp = prefab::library().find(id);
            return pp ? prefab::actor_param(*pp, actors[i], key) : Value();
        };
        pf.set = [key](int i, const Value& v) { actors[i].set_override(key, v); };
        pf.base = [key, id](int i) {
            const prefab::Prefab* pp = prefab::library().find(id);
            return pp ? prefab::inherited_param(*pp, actors[i], key) : Value();
        };
        pf.base_source = [key, id](int i) -> const char* {
            const prefab::Prefab* pp = prefab::library().find(id);
            bool from_type = false;
            if (pp)
                prefab::inherited_param(*pp, actors[i], key, &from_type);
            return from_type ? "fixture type" : "prefab";
        };
        pf.overridden = [key](int i) { return actors[i].override_of(key) != nullptr; };
        pf.reset = [key](int i) { actors[i].clear_override(key); };
        k.fields.push_back(pf);
    }
    return k;
}

std::vector<Kind>& fixed_kinds()
{
    static std::vector<Kind> kinds;
    if (kinds.empty()) {
        for (int type = 0; type < 3; type++)
            kinds.push_back(light_kind(type));
        Kind fog;
        rood_atmo::schema_kind(fog);
        kinds.push_back(fog);
    }
    return kinds;
}
/* One kind per prefab, rebuilt when the library reloads (its parameters may change). */
std::map<std::string, Kind>& actor_kinds()
{
    static std::map<std::string, Kind> kinds;
    static int serial = -1;
    if (serial != prefab::library().serial) {
        kinds.clear();
        serial = prefab::library().serial;
    }
    return kinds;
}
/* add_kind's (a deque: the pointers handed out stay valid). */
std::deque<Kind>& added_kinds()
{
    static std::deque<Kind> kinds;
    return kinds;
}
const Kind* actor_kind_for(const std::string& prefab_id)
{
    std::map<std::string, Kind>& kinds = actor_kinds();
    auto it = kinds.find(prefab_id);
    if (it == kinds.end())
        it = kinds.emplace(prefab_id, actor_kind(prefab_id)).first;
    return &it->second;
}
} // namespace

void add_kind(Kind k)
{
    for (Kind& old : added_kinds())
        if (old.name == k.name) {
            old = std::move(k);
            return;
        }
    added_kinds().push_back(std::move(k));
}
const Kind* find_kind(const std::string& name)
{
    for (const Kind& k : fixed_kinds())
        if (k.name == name)
            return &k;
    for (const Kind& k : added_kinds())
        if (k.name == name)
            return &k;
    if (name.compare(0, 6, "actor:") == 0 && prefab::library().find(name.substr(6)))
        return actor_kind_for(name.substr(6));
    return nullptr;
}
const Kind* kind_of(const Ref& r, int* index)
{
    int i = r.kind == rood_selection::KIND_GLOW ? -1 : rood_scene::index_of(r);
    if (index)
        *index = i;
    if (i < 0)
        return nullptr;
    switch (r.kind) {
    case rood_selection::KIND_LIGHT: {
        int type = lights[i].type;
        return &fixed_kinds()[type == 1 ? 1 : type == 2 ? 2 : 0];
    }
    case rood_selection::KIND_ACTOR:
        return actor_kind_for(actors[i].prefab);
    default:
        return find_kind("fog"); /* atmosphere.cpp registers it */
    }
}
std::vector<const Kind*> all_kinds()
{
    std::vector<const Kind*> out;
    for (const Kind& k : fixed_kinds())
        out.push_back(&k);
    for (const Kind& k : added_kinds())
        out.push_back(&k);
    for (const prefab::Prefab& p : prefab::library().prefabs)
        out.push_back(actor_kind_for(p.id));
    return out;
}
std::vector<std::string> lint()
{
    std::vector<std::string> problems;
    for (const Kind* k : all_kinds()) {
        std::vector<std::string> names;
        if (k->fields.empty())
            problems.push_back(k->name + ": no fields");
        if (k->object == rood_selection::KIND_NONE && k->count < 1)
            problems.push_back(k->name + ": a standalone kind without objects (count " + std::to_string(k->count) + ")");
        for (const Field& f : k->fields) {
            std::string where = k->name + "." + f.name;
            if (f.name.empty() || f.label.empty() || f.category.empty())
                problems.push_back(where + ": needs a name, a label and a category");
            if (std::find(names.begin(), names.end(), f.name) != names.end())
                problems.push_back(where + ": the name twice");
            names.push_back(f.name);
            if (!(f.min < f.max))
                problems.push_back(where + ": no range (" + std::to_string(f.min) + " to " + std::to_string(f.max) + ")");
            if (!(f.step > 0))
                problems.push_back(where + ": no step");
            if (f.def.n != components(f))
                problems.push_back(where + ": no default with " + std::to_string(components(f)) + " components");
            else
                for (int c = 0; c < components(f); c++)
                    if (!std::isfinite(f.def.at(c)) || f.def.at(c) < f.min || f.def.at(c) > f.max)
                        problems.push_back(where + ": default " + format(f, f.def) + " outside its range");
            if (f.type == TYPE_ENUM && (f.options.size() < 2 || (int)f.max != (int)f.options.size() - 1))
                problems.push_back(where + ": an enum needs two options or more, its range their indices");
            if (!f.get || !f.set)
                problems.push_back(where + ": no accessors");
        }
    }
    return problems;
}

std::vector<Ref> targets(const Kind& k, const Field& f, const Ref& target)
{
    std::vector<Ref> out = { target };
    if (!f.shared || !rood_selection::contains(target))
        return out;
    for (const Ref& r : rood_selection::items()) {
        int i;
        if (r != target && kind_of(r, &i) == &k)
            out.push_back(r);
    }
    return out;
}
void apply(const Kind& k, const Field& f, const Ref& target, const Value& value, int component)
{
    Value v = clamp(f, value);
    bool one = f.type == TYPE_VEC3 && component >= 0 && component < 3;
    for (const Ref& r : targets(k, f, target)) {
        int i = rood_scene::index_of(r);
        if (i < 0)
            continue;
        if (one && r != target) { /* only the component edited (a size's x leaves y and z alone) */
            Value own = f.get(i);
            Value mixed = Value::vec(own.at(0), own.at(1), own.at(2));
            mixed.v[component] = v.at(component);
            f.set(i, mixed);
        } else {
            f.set(i, v);
        }
    }
    rood_lights::dirty = true;
    pc_undo_hint(("Edit " + f.label).c_str());
}
void apply_reset(const Kind& k, const Field& f, const Ref& target)
{
    for (const Ref& r : targets(k, f, target)) {
        int i = rood_scene::index_of(r);
        if (i >= 0)
            reset_field(f, i);
    }
    rood_lights::dirty = true;
    pc_undo_hint(("Reset " + f.label).c_str());
}
void apply_at(const Kind& k, const Field& f, int index, const Value& v)
{
    if (index < 0 || index >= k.count)
        return;
    f.set(index, clamp(f, v));
    pc_undo_hint(("Edit " + f.label).c_str());
}
void apply_reset_at(const Kind& k, const Field& f, int index)
{
    if (index < 0 || index >= k.count)
        return;
    reset_field(f, index);
    pc_undo_hint(("Reset " + f.label).c_str());
}

/* Lines of "<kind>\t<index>\t<field>\t<value as JSON>". */
std::string standalone_state()
{
    std::string s;
    for (const Kind& k : added_kinds())
        if (k.object == rood_selection::KIND_NONE)
            for (int i = 0; i < k.count; i++)
                for (const Field& f : k.fields)
                    s += k.name + "\t" + std::to_string(i) + "\t" + f.name + "\t" + to_json(f, f.get(i)) + "\n";
    return s;
}
void set_standalone_state(const std::string& text)
{
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1),
               c = b == std::string::npos ? b : line.find('\t', b + 1);
        if (c == std::string::npos)
            continue;
        const Kind* k = find_kind(line.substr(0, a));
        int index = atoi(line.substr(a + 1, b - a - 1).c_str());
        const Field* f = k ? k->field(line.substr(b + 1, c - b - 1)) : nullptr;
        prefab::Json j;
        Value v;
        if (!f || index < 0 || index >= k->count || !prefab::json_parse(line.substr(c + 1).c_str(), j)
            || !from_json(*f, j, v, nullptr))
            continue;
        if (!same(*f, f->get(index), v))
            f->set(index, v);
    }
}
} // namespace rood_schema

/* ---- RPC editor_schema ---- */

namespace {
using prefab::Json;
using rood_schema::Field;
using rood_schema::Kind;
std::string quote(const std::string& s) { return prefab::json_quote(s); }
std::string number(double v)
{
    char text[32];
    snprintf(text, sizeof text, "%.6g", v);
    return text;
}
int failure(const std::string& message, char* out, int size)
{
    snprintf(out, size, "%s", message.c_str());
    return -1;
}
std::string kind_json(const Kind& k)
{
    std::string s = "{\"name\":" + quote(k.name) + ",\"label\":" + quote(k.label) + ",\"object\":\""
        + rood_selection::kind_name(k.object) + "\",\"count\":"
        + std::to_string(k.object == rood_selection::KIND_NONE ? k.count : -1) + ",\"closed\":[";
    for (size_t i = 0; i < k.closed.size(); i++)
        s += (i ? "," : "") + quote(k.closed[i]);
    s += "],\"fields\":[";
    for (size_t i = 0; i < k.fields.size(); i++) {
        const Field& f = k.fields[i];
        s += std::string(i ? "," : "") + "{\"name\":" + quote(f.name) + ",\"label\":" + quote(f.label) + ",\"category\":"
            + quote(f.category) + ",\"type\":\"" + rood_schema::type_name(f.type) + "\",\"min\":" + number(f.min)
            + ",\"max\":" + number(f.max) + ",\"step\":" + number(f.step) + ",\"log\":" + (f.logarithmic ? "true" : "false")
            + ",\"unit\":" + quote(f.unit) + ",\"default\":" + rood_schema::to_json(f, f.def) + ",\"resettable\":"
            + (f.resettable ? "true" : "false") + ",\"shared\":" + (f.shared ? "true" : "false") + ",\"options\":[";
        for (size_t o = 0; o < f.options.size(); o++)
            s += (o ? "," : "") + quote(f.options[o]);
        s += "]}";
    }
    return s + "]}";
}
std::string object_json(const Kind& k, const std::string& id, const std::string& label, int index)
{
    std::string s = "{\"id\":" + quote(id) + ",\"kind\":" + quote(k.name) + ",\"label\":" + quote(label)
        + ",\"index\":" + std::to_string(index) + ",\"fields\":{";
    for (size_t i = 0; i < k.fields.size(); i++) {
        const Field& f = k.fields[i];
        s += std::string(i ? "," : "") + quote(f.name) + ":{\"value\":" + rood_schema::to_json(f, f.get(index))
            + ",\"base\":" + rood_schema::to_json(f, rood_schema::base_of(f, index)) + ",\"source\":\""
            + rood_schema::base_source_of(f, index) + "\",\"overridden\":"
            + (f.resettable && rood_schema::is_overridden(f, index) ? "true" : "false") + "}";
    }
    return s + "}}";
}
} // namespace

/* RPC editor_schema (dev_rpc.c hands over the params object's text):
 * action kinds (default; {kind}: one), lint, get {id}, set {id, values
 * {field: value}}, reset {id, fields [names] (default every resettable
 * one)}; without an id, the object Details shows (its lock, else the
 * selection's primary); {kind, index (default 0)} instead of an id: a
 * standalone kind's object (add_kind; "menu_model_light"); ui {search,
 * kind_search {kind: text}, open: kind (rood_details::kind_section)}. Values are numbers, [r, g, b] / [x, y, z],
 * true / false and enum option names; set clamps them to their range. */
extern "C" int pc_editor_schema_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return failure("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "kinds";
    std::string text;
    if (action == "lint") {
        std::vector<std::string> problems = rood_schema::lint();
        size_t fields = 0;
        std::vector<const Kind*> kinds = rood_schema::all_kinds();
        for (const Kind* k : kinds)
            fields += k->fields.size();
        text = "{\"kinds\":" + std::to_string(kinds.size()) + ",\"fields\":" + std::to_string(fields) + ",\"problems\":[";
        for (size_t i = 0; i < problems.size(); i++)
            text += (i ? "," : "") + quote(problems[i]);
        text += "]}";
    } else if (action == "kinds") {
        const Json* only = params.get("kind");
        text = "{\"kinds\":[";
        bool first = true;
        for (const Kind* k : rood_schema::all_kinds())
            if (!only || only->string == k->name) {
                text += (first ? "" : ",") + kind_json(*k);
                first = false;
            }
        text += "]}";
    } else if (action == "get" || action == "set" || action == "reset") {
        rood_selection::Ref target = rood_selection::locked() ? rood_selection::locked() : rood_selection::primary();
        if (const Json* id = params.get("id"))
            target = rood_selection::ref_of(id->string);
        int index;
        const Kind* k = nullptr;
        bool standalone = false;
        if (const Json* kn = params.get("kind")) {
            k = kn->type == Json::String ? rood_schema::find_kind(kn->string) : nullptr;
            const Json* ix = params.get("index");
            index = ix && ix->type == Json::Number ? (int)ix->number : 0;
            if (!k || k->object != rood_selection::KIND_NONE || index < 0 || index >= k->count)
                return failure("no standalone kind " + (kn->type == Json::String ? kn->string : std::string("?"))
                        + " with an object " + std::to_string(index) + " (a scene object: its id)",
                    out, size);
            standalone = true;
        } else {
            k = rood_schema::kind_of(target, &index);
        }
        auto edit = [&](const Field& f, const rood_schema::Value& v) {
            if (standalone)
                rood_schema::apply_at(*k, f, index, v);
            else
                rood_schema::apply(*k, f, target, v);
        };
        auto reset = [&](const Field& f) {
            if (standalone)
                rood_schema::apply_reset_at(*k, f, index);
            else
                rood_schema::apply_reset(*k, f, target);
        };
        if (!k)
            return failure("no object with fields: " + (target ? target.id : std::string("nothing selected"))
                    + " (glows have none yet)",
                out, size);
        if (action == "set") {
            const Json* values = params.get("values");
            if (!values || values->type != Json::Object)
                return failure("set needs values {field: value}", out, size);
            std::vector<std::pair<const Field*, rood_schema::Value>> edits;
            for (const auto& m : values->members) {
                const Field* f = k->field(m.first);
                if (!f)
                    return failure("no field " + m.first + " on " + k->name, out, size);
                rood_schema::Value v;
                if (!rood_schema::from_json(*f, m.second, v, &error))
                    return failure(error, out, size);
                edits.push_back({ f, v });
            }
            for (const auto& e : edits)
                edit(*e.first, e.second);
            if (edits.size() > 1)
                pc_undo_hint(("Edit " + std::to_string(edits.size()) + " fields").c_str());
        } else if (action == "reset") {
            const Json* names = params.get("fields");
            std::vector<const Field*> fields;
            if (names)
                for (const Json& n : names->items) {
                    const Field* f = k->field(n.string);
                    if (!f || !f->resettable)
                        return failure("no resettable field " + n.string + " on " + k->name, out, size);
                    fields.push_back(f);
                }
            else
                for (const Field& f : k->fields)
                    if (f.resettable)
                        fields.push_back(&f);
            for (const Field* f : fields)
                reset(*f);
            if (fields.size() > 1)
                pc_undo_hint(("Reset " + std::to_string(fields.size()) + " fields").c_str());
        }
        if (standalone) {
            text = object_json(*k, k->name, k->label, index);
        } else {
            k = rood_schema::kind_of(target, &index); /* a light's type may have changed it */
            text = object_json(*k, target.id, rood_scene::label(target), index);
        }
    } else if (action == "ui") {
        if (const Json* s = params.get("search"))
            rood_details::set_search(s->string);
        if (const Json* ks = params.get("kind_search")) /* {kind: text}: draw_kind's search boxes */
            for (const auto& m : ks->members)
                rood_details::set_kind_search(m.first, m.second.string);
        if (const Json* o = params.get("open")) /* a kind_section's header opens next frame */
            rood_details::open_section(o->string);
        text = rood_details::state_json();
    } else {
        return failure("action: kinds, lint, get, set, reset or ui", out, size);
    }
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
