// Light actors (prefabs): data-first lamps, candles, torches.
//
// A prefab is a JSON file: one or more lights placed relative to the actor's
// pivot, plus exposed parameters with ranges that the lights' fields refer
// to by name in small expressions ("kelvin(temperature) * tint",
// [0, "-flame_height", 0]). Built-in prefabs live in pc/assets/prefabs
// (authored for this port, committed); the user's own and edited ones in
// pc/user/prefabs (ROOD_PREFABS), overriding built-ins with the same id (the
// file name without .json). Both directories reload live when files change.
//
// An actor (an instance, saved in the room files as an "actor" line) names
// its prefab and keeps its transform and only the parameters it overrides,
// so editing a prefab updates every instance. Lights are evaluated from
// time every presented frame: flicker (intensity noise and position jitter)
// and pulse are deterministic functions of the time and the actor's seed.
//
// This file has no Dear ImGui dependency (pc/tests/prefab_probe.cpp tests it);
// the editor is light_actors.cpp. Format reference: pc/assets/prefabs/README.md.
#ifndef ROOD_LIGHT_PREFABS_H
#define ROOD_LIGHT_PREFABS_H
#include <stdint.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace prefab {

/* ---- JSON (enough for prefab files and RPC parameters) ---- */
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> items; // Array
    std::vector<std::pair<std::string, Json>> members; // Object, in file order
    const Json* get(const char* key) const;
    Json* get(const char* key);
    Json& set(const char* key, const Json& value); // add or replace (keeps the position)
    void remove(const char* key);
    static Json num(double v);
    static Json str(const std::string& s);
    static Json boolean_(bool b);
    static Json array();
    static Json object();
};
bool json_parse(const char* text, Json& out, std::string* error = nullptr);
/* Pretty printed (two spaces; short arrays of numbers on one line). */
std::string json_write(const Json& value, int indent = 0);
std::string json_quote(const std::string& s);

/* ---- values and expressions ---- */
struct Value {
    float v[3] = { 0, 0, 0 };
    int n = 1; // 1 scalar, 3 vector
    static Value scalar(float x);
    static Value vec(float x, float y, float z);
    float x() const { return v[0]; }
    float at(int i) const { return n == 1 ? v[0] : v[i]; }
};
struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;
using Lookup = std::function<bool(const std::string& name, Value& out)>;
/* Numbers, names, + - * /, unary -, parentheses, and calls: vec(x, y, z),
 * kelvin(K) (a blackbody colour, display space, brightest channel 1),
 * min, max, clamp(x, lo, hi), mix(a, b, t), select(c, a, b) (c > 0.5 ? a : b),
 * pow, sqrt, abs, rgb(r, g, b) (0-255 to 0-1). Vectors and scalars mix
 * componentwise (a scalar broadcasts). */
ExprPtr expr_parse(const std::string& text, std::string* error = nullptr);
ExprPtr expr_constant(Value v);
/* A JSON field as an expression: a number, a string expression, or an array
 * of three numbers/expressions (a vector). Null when absent or invalid. */
ExprPtr expr_from_json(const Json* field, std::string* error = nullptr);
Value expr_eval(const ExprPtr& e, const Lookup& lookup, Value fallback = Value());

void kelvin(float temperature, float rgb[3]);

/* ---- deterministic animation noise ---- */
uint32_t hash32(uint32_t x);
/* Smooth value noise in [0, 1] of time t (seconds times frequency). */
float noise1(uint32_t seed, double t);
/* Flicker: a few octaves of noise1, in [0, 1], mostly around 0.3-0.7. */
float flicker_noise(uint32_t seed, double t);

/* ---- prefabs ---- */
enum ParamType { PARAM_FLOAT, PARAM_COLOR, PARAM_BOOL, PARAM_ENUM };
struct Param {
    std::string key, label, group, unit, tooltip;
    ParamType type = PARAM_FLOAT;
    Value def;
    float min = 0, max = 1;
    bool logarithmic = false;
    std::vector<std::string> options; // PARAM_ENUM
};
enum { LIGHT_POINT, LIGHT_SPOT, LIGHT_RECT };
struct LightTemplate {
    std::string name;
    int type = LIGHT_POINT;
    /* Fields; null ones take the defaults in evaluate(). Offsets and
     * directions are in the actor's frame (x right, y down as in the game,
     * z forward). */
    ExprPtr offset, direction, color, intensity, radius, cone, inner, size, barn_angle, barn_length, shadows, light_size, enabled;
    ExprPtr flicker, flicker_speed, jitter, pulse, pulse_speed;
    float phase = 0; // decorrelates the animation of lights in one actor
};
struct GlowSuggest { // which game glows this prefab is suggested for
    bool enabled = false;
    std::string hue; // "warm", "cool" or "any"
    float min_size = 0, max_size = 1e9f; // emitting radius the path tracer found (2-8)
    float min_brightness = 0.2f; // brightest colour channel (dim smoke and embers are left out)
    float merge = 0; // glows this close to one already suggested share its actor (a large sigil)
    /* Flames painted in room textures (no glow): suggested when "flames" is
     * on and the flame's strength (its bright warm texels) is in range. */
    bool flames = false;
    float flame_min = 0, flame_max = 1e9f;
    int priority = 0; // higher wins
};
struct Prefab {
    std::string id, name, category, description, icon, path;
    bool user = false; // from the user directory (overrides a built-in)
    bool overrides_builtin = false;
    std::vector<Param> params;
    std::vector<LightTemplate> lights;
    std::string align = "up"; // placement on a surface: "up" (keep upright) or "normal" (forward = surface normal)
    float surface_offset = 0; // the pivot's distance off the surface
    float fixture_radius = 16; // lighting a room fixture: its polygons this close to the flame cast no shadow from it
    ExprPtr glow_mode; // 0 leave the linked glow, 1 drive (its emission follows the actor), 2 suppress (mute it)
    ExprPtr glow_scale; // emission scale when driving
    GlowSuggest suggest;
    Json source; // the parsed file (editing writes it back)
    std::string error; // load problems (shown in the editor)
    const Param* param(const std::string& key) const;
};
/* Build a prefab from its JSON; problems go to prefab.error. */
Prefab prefab_from_json(const std::string& id, const Json& json);

struct Library {
    std::vector<Prefab> prefabs; // sorted by category, then name
    std::string builtin_dir, user_dir;
    long long stamp = -1; // directory contents (names and mtimes) as last loaded
    int serial = 0; // bumps on every (re)load
    const Prefab* find(const std::string& id) const;
    void load();
    /* Reload when either directory changed; true when it did. */
    bool poll();
    /* Write `json` as the user's prefab `id` (pc/user/prefabs/id.json). */
    bool save_user(const std::string& id, const Json& json, std::string* error);
    /* Move the user's file aside (id.json.bak): the built-in comes back. */
    bool revert_user(const std::string& id, std::string* error);
};
Library& library();
/* A file-name-safe id from a display name ("Wall torch" -> "wall_torch"). */
std::string make_id(const std::string& name);

/* ---- actors ---- */
struct Actor {
    std::string prefab; // id
    float position[3] = { 0, 0, 0 }; // pivot, world
    float rotation[3] = { 0, 0, 0 }; // yaw, pitch, roll (degrees): R = Ry(yaw) Rx(pitch) Rz(roll)
    float scale = 1; // its size (R): its lights' offsets, jitter, radii and sizes scale with it, never their intensity
    bool enabled = true;
    uint32_t seed = 1; // animation noise
    std::vector<std::pair<std::string, Value>> overrides;
    bool glow_linked = false;
    float glow[3] = { 0, 0, 0 }; // the linked glow, followed as it drifts
    std::string glow_key; // the linked glow's persistent key (glows.c; empty: the glow nearest `glow`)
    /* The room fixture (a lantern or candle made of room polygons) whose
     * flame it lights: the fixture's polygons around the flame cast no
     * shadow from its lights unless fixture_shadows (shadow rays stop
     * fixture_radius short of the light: UE's "ignore owner"). */
    bool fixture = false, fixture_shadows = false;
    float fixture_at[3] = { 0, 0, 0 };
    float fixture_radius = 16;
    bool fixture_radius_set = false; // its own radius (else its fixture type's, when it has one)
    /* A generated actor's fixture type (pc/assets/fixtures/catalog.json,
     * fixture_catalog.cpp: "T001") and which of the type's flames it lights:
     * the type's parameters (between the actor's own overrides and the
     * prefab's defaults), light offset and fixture radius apply to it. */
    std::string fixture_type;
    int flame = 0;
    /* Stable id (lighting_layers.h: "actor:<id>"; assigned when first saved
     * or listed) and an optional display name. */
    std::string id, name;
    const Value* override_of(const std::string& key) const;
    void set_override(const std::string& key, const Value& v);
    void clear_override(const std::string& key);
};
/* "actor ID x y z yaw pitch roll [key=value ...]"; values are numbers or
 * comma-separated vectors; keys enabled, seed, glow, fixture (x,y,z),
 * fixture_radius and fixture_shadows are the actor's own. */
bool actor_parse(const char* line, Actor& out);
std::string actor_format(const Actor& a);
/* The parameter's value for this actor (override, else its fixture type's,
 * else the prefab default). */
Value actor_param(const Prefab& p, const Actor& a, const std::string& key);
/* What the actor inherits for a parameter (its fixture type's value, else
 * the prefab default); *from_type tells which. */
Value inherited_param(const Prefab& p, const Actor& a, const std::string& key, bool* from_type = nullptr);
/* The fixture radius in effect (its own, else its type's, else 16). */
float actor_fixture_radius(const Actor& a);

/* Fixture types (fixture_catalog.cpp installs these; null: no catalog). */
struct TypeHooks {
    /* The type's value for a parameter (class defaults, then the type's). */
    bool (*param)(const std::string& type, const std::string& key, Value& out) = nullptr;
    /* The light offset from the flame (the actor's frame: x right, y down, z forward). */
    bool (*offset)(const std::string& type, int flame, float out[3]) = nullptr;
    bool (*fixture_radius)(const std::string& type, float* out) = nullptr;
};
TypeHooks& type_hooks();
/* Columns of R: right, down, forward. */
void rotation_basis(const float rotation[3], float right[3], float down[3], float forward[3]);
void rotation_from_basis(const float right[3], const float down[3], const float forward[3], float rotation[3]);
/* Rotation whose forward is `normal`, upright as far as it allows. */
void rotation_facing(const float normal[3], float rotation[3]);

struct EvalLight {
    std::string name;
    int type = LIGHT_POINT;
    float position[3] = { 0, 0, 0 }, color[3] = { 1, 1, 1 }, direction[3] = { 0, 0, 1 };
    float intensity = 1, radius = 600, cone = 45, size[2] = { 128, 128 };
    float inner = 0, barn_angle = 45, barn_length = 0; // spot inner cone; rect barn doors (lights_internal.h Light)
    float light_size = 0; // emitting radius for soft shadows (0: the room's light size)
    float shadow_skip = 0; // shadow rays stop this far short of the light (its fixture casts no shadow)
    bool shadows = false, enabled = true;
    float factor = 1; // this frame's animation factor on the intensity
};
/* The actor's lights at `time` (seconds); animated when `animate`. */
void evaluate(const Prefab& p, const Actor& a, double time, bool animate, std::vector<EvalLight>& out);
/* The linked glow's treatment at `time`: 0 none, 1 drive (scale set), 2 suppress. */
int glow_drive(const Prefab& p, const Actor& a, double time, bool animate, float* scale);

} // namespace prefab
#endif
