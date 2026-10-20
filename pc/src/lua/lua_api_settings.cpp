// rood.settings (pc/docs/Extensions.md, "Settings"): one namespace over the
// game's settings registries, validated by their own metadata, so a setting
// a plugin reads or changes is one the menus and the dev tools show:
//   - the System pages' rows (pc/src/ui/ui_settings.h): ids such as
//     "widescreen" or "qol_skip_charging"; a toggle is a boolean, a choice
//     its choice's id (the label in lower case: "fast"), a slider a number;
//     actions run with rood.settings.run; greyed rows refuse, saying why;
//   - standalone kinds of the editor's property schema (editor_schema.h
//     add_kind) with one object: "<kind>/<field>";
//   - settings.cfg's keys without a row, read only: the plain numeric ones
//     (qol_fast_travel_prompt_size; a colour a table of four) and ui_text's
//     (ui_text_status_size, ui_text_labels_font);
//   - a plugin's own settings (rood.settings.define): the schema kind
//     "plugin:<plugin id>" (so the dev tools' Details can draw them), the
//     values in the plugin's data folder (settings.json).
// Generated from that metadata (ids, kinds, ranges, choices, availability):
// nothing here names a single setting. System changes reach settings.cfg
// through ui_settings' own save. Game side: not in lua_probe.
#include "rl_internal.h"
#include "editor_schema.h"
#include "light_prefabs.h"
#include "ui/ui_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int pc_settings_numbers(const char* key, double* values, int max) __attribute__((weak));
extern "C" int pc_ui_text_setting_value(const char* key, char* out, int size) __attribute__((weak));

namespace rl {
namespace {
using rood_schema::Field;
using rood_schema::Kind;

const char* const ROW_KINDS[] = { "choice", "toggle", "slider", "action", "binding", "info" }; /* PC_UI_SET_* */

std::string slug(const char* label)
{
    std::string out;
    for (const char* s = label; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (std::isalnum(c))
            out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

/* luaL_error with printf's formats (lua_pushfstring has no %g). */
[[noreturn]] __attribute__((format(printf, 2, 3))) void raisef(lua_State* L, const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    luaL_error(L, "%s", text);
    std::abort(); /* not reached: luaL_error raises */
}

void set_text(lua_State* L, const char* key, const std::string& value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}
void set_number(lua_State* L, const char* key, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}
void set_flag(lua_State* L, const char* key, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

/* ---- System rows: the settings catalog (ui_settings.h), whichever pages show them ---- */
struct RowRef {
    int index = -1; /* the catalog's */
    pc_ui_setting_row r;
};
/* 1: shown (ref filled), 2: exists but not shown now, 0: no such row. Only the
 * catalog's own rows: its provider's (plugin settings, schema fields) are the
 * schema's below. */
int find_row(const std::string& id, RowRef& ref)
{
    int i = pc_ui_settings_catalog_index(id.c_str());
    if (i < 0 || i >= pc_ui_settings_catalog_count())
        return 0;
    ref.index = i;
    if (!pc_ui_settings_catalog_shown(i))
        return 2;
    return pc_ui_settings_catalog_row(i, &ref.r) ? 1 : 0;
}
/* A home's rood.settings page name: "display" for rood.options/display. */
std::string home_page(const char* home)
{
    const char* slash = home ? std::strrchr(home, '/') : nullptr;
    return slash ? slash + 1 : home ? home : "";
}

std::string choices_text(const RowRef& ref)
{
    std::string s;
    for (int c = 0; c < ref.r.count; c++) {
        const char* name = nullptr;
        if (pc_ui_settings_catalog_choice(ref.index, c, &name, nullptr) && name)
            s += (s.empty() ? "" : ", ") + slug(name);
    }
    return s;
}

void push_row_value(lua_State* L, const RowRef& ref)
{
    const pc_ui_setting_row& r = ref.r;
    switch (r.kind) {
    case PC_UI_SET_TOGGLE:
        lua_pushboolean(L, r.index == 1);
        return;
    case PC_UI_SET_CHOICE: {
        const char* name = nullptr;
        if (r.index >= 0 && pc_ui_settings_catalog_choice(ref.index, r.index, &name, nullptr) && name)
            lua_pushstring(L, slug(name).c_str());
        else
            lua_pushnil(L);
        return;
    }
    case PC_UI_SET_SLIDER:
        if (r.step >= 1 && std::floor(r.step) == r.step && std::floor(r.min) == r.min)
            lua_pushinteger(L, (lua_Integer)std::lround(r.number));
        else
            lua_pushnumber(L, r.number);
        return;
    case PC_UI_SET_ACTION:
        lua_pushnil(L);
        return;
    default:
        lua_pushstring(L, r.value);
        return;
    }
}

/* The Lua value at idx for a row, validated (raises with what is allowed). */
double row_value(lua_State* L, int idx, const RowRef& ref, const char* id)
{
    const pc_ui_setting_row& r = ref.r;
    if (r.kind == PC_UI_SET_ACTION)
        luaL_error(L, "'%s' is an action: run it with rood.settings.run", id);
    if (r.kind == PC_UI_SET_BINDING || r.kind == PC_UI_SET_INFO)
        luaL_error(L, "'%s' is read-only here", id);
    if (!r.enabled)
        luaL_error(L, "'%s' cannot change now: %s", id, r.reason);
    if (r.kind == PC_UI_SET_TOGGLE) {
        if (!lua_isboolean(L, idx))
            luaL_error(L, "'%s' is a boolean (true or false), got a %s", id, luaL_typename(L, idx));
        return lua_toboolean(L, idx) ? 1 : 0;
    }
    if (r.kind == PC_UI_SET_SLIDER) {
        if (lua_type(L, idx) != LUA_TNUMBER)
            luaL_error(L, "'%s' is a number, got a %s", id, luaL_typename(L, idx));
        double v = lua_tonumber(L, idx);
        if (!(v >= r.min - 1e-6 && v <= r.max + 1e-6))
            raisef(L, "'%s' must be between %g and %g (got %g)", id, (double)r.min, (double)r.max, v);
        return v;
    }
    if (lua_type(L, idx) != LUA_TSTRING)
        luaL_error(L, "'%s' is one of: %s (got a %s)", id, choices_text(ref).c_str(), luaL_typename(L, idx));
    const char* want = lua_tostring(L, idx);
    for (int c = 0; c < r.count; c++) {
        const char* name = nullptr;
        const char* reason = nullptr;
        if (!pc_ui_settings_catalog_choice(ref.index, c, &name, &reason) || !name)
            continue;
        if (slug(name) == want || !std::strcmp(name, want)) {
            if (reason)
                luaL_error(L, "'%s' cannot be %s here: %s", id, want, reason);
            return c;
        }
    }
    luaL_error(L, "'%s' must be one of: %s (got '%s')", id, choices_text(ref).c_str(), want);
    return 0;
}

void push_row_info(lua_State* L, const RowRef& ref, const char* page)
{
    const pc_ui_setting_row& r = ref.r;
    lua_createtable(L, 0, 16);
    set_text(L, "id", r.id);
    set_text(L, "label", r.label);
    set_text(L, "help", r.help ? r.help : "");
    set_text(L, "kind", ROW_KINDS[r.kind]);
    set_text(L, "page", page);
    set_text(L, "source", "system");
    push_row_value(L, ref);
    lua_setfield(L, -2, "value");
    set_text(L, "text", r.value);
    set_flag(L, "enabled", r.enabled != 0);
    if (r.reason[0])
        set_text(L, "reason", r.reason);
    set_flag(L, "game_option", r.game_option != 0);
    if (r.kind == PC_UI_SET_SLIDER) {
        set_number(L, "min", r.min);
        set_number(L, "max", r.max);
        set_number(L, "step", r.step);
    }
    if (r.kind == PC_UI_SET_CHOICE || r.kind == PC_UI_SET_TOGGLE) {
        lua_createtable(L, r.count, 0);
        for (int c = 0; c < r.count; c++) {
            const char* name = nullptr;
            const char* why = nullptr;
            pc_ui_settings_catalog_choice(ref.index, c, &name, &why);
            lua_createtable(L, 0, 4);
            set_text(L, "id", slug(name ? name : "?"));
            set_text(L, "label", name ? name : "?");
            set_flag(L, "available", why == nullptr);
            if (why)
                set_text(L, "reason", why);
            lua_rawseti(L, -2, c + 1);
        }
        lua_setfield(L, -2, "choices");
    }
}

/* ---- Schema fields ---- */
/* "<kind>/<field>" of a standalone kind with one object. */
const Field* schema_field(const std::string& id, const Kind** kind)
{
    size_t slash = id.rfind('/');
    if (slash == std::string::npos)
        return nullptr;
    const Kind* k = rood_schema::find_kind(id.substr(0, slash));
    if (!k || k->object != rood_selection::KIND_NONE || k->count != 1)
        return nullptr;
    const Field* f = k->field(id.substr(slash + 1));
    if (f && kind)
        *kind = k;
    return f;
}

std::string options_text(const Field& f)
{
    std::string s;
    for (const std::string& o : f.options)
        s += (s.empty() ? "" : ", ") + o;
    return s;
}

void push_field_value(lua_State* L, const Field& f, const prefab::Value& v)
{
    switch (f.type) {
    case rood_schema::TYPE_BOOL:
        lua_pushboolean(L, v.x() != 0);
        return;
    case rood_schema::TYPE_INT:
        lua_pushinteger(L, (lua_Integer)std::lround(v.x()));
        return;
    case rood_schema::TYPE_ENUM: {
        long i = std::lround(v.x());
        if (i >= 0 && i < (long)f.options.size())
            lua_pushstring(L, f.options[(size_t)i].c_str());
        else
            lua_pushnil(L);
        return;
    }
    case rood_schema::TYPE_COLOUR:
    case rood_schema::TYPE_VEC3:
        lua_createtable(L, 3, 0);
        for (int c = 0; c < 3; c++) {
            lua_pushnumber(L, v.at(c));
            lua_rawseti(L, -2, c + 1);
        }
        return;
    default:
        lua_pushnumber(L, v.x());
        return;
    }
}

/* The Lua value at idx (absolute) for a field, validated (raises with what is allowed). */
prefab::Value field_value(lua_State* L, int idx, const Field& f, const char* id)
{
    auto in_range = [&](double x) {
        if (!(x >= f.min - 1e-6 && x <= f.max + 1e-6))
            raisef(L, "'%s' must be between %g and %g (got %g)", id, (double)f.min, (double)f.max, x);
    };
    switch (f.type) {
    case rood_schema::TYPE_BOOL:
        if (!lua_isboolean(L, idx))
            luaL_error(L, "'%s' is a boolean (true or false), got a %s", id, luaL_typename(L, idx));
        return prefab::Value::scalar(lua_toboolean(L, idx) ? 1.0f : 0.0f);
    case rood_schema::TYPE_ENUM: {
        if (lua_type(L, idx) != LUA_TSTRING)
            luaL_error(L, "'%s' is one of: %s (got a %s)", id, options_text(f).c_str(), luaL_typename(L, idx));
        const char* want = lua_tostring(L, idx);
        for (size_t k = 0; k < f.options.size(); k++)
            if (f.options[k] == want || slug(f.options[k].c_str()) == want)
                return prefab::Value::scalar((float)k);
        luaL_error(L, "'%s' must be one of: %s (got '%s')", id, options_text(f).c_str(), want);
        return prefab::Value();
    }
    case rood_schema::TYPE_COLOUR:
    case rood_schema::TYPE_VEC3: {
        if (!lua_istable(L, idx) || lua_rawlen(L, idx) != 3)
            luaL_error(L, "'%s' is a table of 3 numbers", id);
        float c[3];
        for (int k = 0; k < 3; k++) {
            lua_rawgeti(L, idx, k + 1);
            if (lua_type(L, -1) != LUA_TNUMBER)
                luaL_error(L, "'%s' is a table of 3 numbers", id);
            c[k] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
            in_range(c[k]);
        }
        return prefab::Value::vec(c[0], c[1], c[2]);
    }
    default: {
        if (lua_type(L, idx) != LUA_TNUMBER)
            luaL_error(L, "'%s' is a number, got a %s", id, luaL_typename(L, idx));
        double x = lua_tonumber(L, idx);
        if (f.type == rood_schema::TYPE_INT && std::floor(x) != x)
            raisef(L, "'%s' is an integer (got %g)", id, x);
        in_range(x);
        return prefab::Value::scalar((float)x);
    }
    }
}

void push_field_info(lua_State* L, const Kind& k, const Field& f)
{
    lua_createtable(L, 0, 16);
    set_text(L, "id", k.name + "/" + f.name);
    set_text(L, "label", f.label);
    set_text(L, "help", f.tooltip);
    set_text(L, "kind", rood_schema::type_name(f.type));
    set_text(L, "page", k.name);
    set_text(L, "category", f.category);
    set_text(L, "source", k.name.compare(0, 7, "plugin:") == 0 ? "plugin" : "schema");
    push_field_value(L, f, f.get(0));
    lua_setfield(L, -2, "value");
    set_text(L, "text", rood_schema::format(f, f.get(0)));
    set_flag(L, "enabled", true);
    set_number(L, "min", f.min);
    set_number(L, "max", f.max);
    set_number(L, "step", f.step);
    push_field_value(L, f, f.def);
    lua_setfield(L, -2, "default");
    if (f.type == rood_schema::TYPE_ENUM) {
        lua_createtable(L, (int)f.options.size(), 0);
        for (size_t c = 0; c < f.options.size(); c++) {
            lua_createtable(L, 0, 3);
            set_text(L, "id", slug(f.options[c].c_str()));
            set_text(L, "label", f.options[c]);
            set_flag(L, "available", true);
            lua_rawseti(L, -2, (lua_Integer)c + 1);
        }
        lua_setfield(L, -2, "choices");
    }
}

/* Pushes a setting's value; false when there is none shown. */
/* ---- settings.cfg keys without a row (read only): the plain numeric ones
 * (project_settings.cpp's table: qol_fast_travel_prompt_size, ui_prompt_icon, ...; a
 * number, or a table of numbers for a key with several: colours) and ui_text's
 * (ui_text_<style>_<field>: a number, or the font's path). Changed by settings.cfg,
 * the dev tools and the RPC; plugins read and watch them. ---- */
#define CONFIG_MAX 8
struct ConfigKey {
    int count = 0;           /* numbers; -1: text */
    double numbers[CONFIG_MAX];
    char text[256];
};
bool config_value(const std::string& id, ConfigKey& c)
{
    c.count = pc_settings_numbers ? pc_settings_numbers(id.c_str(), c.numbers, CONFIG_MAX) : 0;
    if (c.count > 0)
        return true;
    if (pc_ui_text_setting_value && pc_ui_text_setting_value(id.c_str(), c.text, (int)sizeof c.text)) {
        char* end;
        double v = std::strtod(c.text, &end);
        if (end != c.text && !*end)
            c.count = 1, c.numbers[0] = v;
        else
            c.count = -1;
        return true;
    }
    return false;
}
void push_number(lua_State* L, double v)
{
    if (std::floor(v) == v && std::fabs(v) < 1e15)
        lua_pushinteger(L, (lua_Integer)v);
    else
        lua_pushnumber(L, v);
}

bool push_setting(lua_State* L, const std::string& id)
{
    RowRef ref;
    if (find_row(id, ref) == 1) {
        push_row_value(L, ref);
        return true;
    }
    const Kind* k = nullptr;
    if (const Field* f = schema_field(id, &k)) {
        push_field_value(L, *f, f->get(0));
        return true;
    }
    ConfigKey c;
    if (!find_row(id, ref) && config_value(id, c)) {
        if (c.count < 0) {
            lua_pushstring(L, c.text);
        } else if (c.count == 1) {
            push_number(L, c.numbers[0]);
        } else {
            lua_createtable(L, c.count, 0);
            for (int i = 0; i < c.count; i++) {
                push_number(L, c.numbers[i]);
                lua_rawseti(L, -2, i + 1);
            }
        }
        return true;
    }
    return false;
}

/* What a watch compares; "" when unknown or not shown. */
std::string current_text(const std::string& id)
{
    RowRef ref;
    if (find_row(id, ref) == 1)
        return std::to_string(ref.r.index) + "|" + ref.r.value + "|" + std::to_string(ref.r.number);
    const Kind* k = nullptr;
    if (const Field* f = schema_field(id, &k))
        return rood_schema::to_json(*f, f->get(0));
    ConfigKey c;
    if (!find_row(id, ref) && config_value(id, c)) {
        if (c.count < 0)
            return c.text;
        std::string text;
        for (int i = 0; i < c.count; i++)
            text += std::to_string(c.numbers[i]) + " ";
        return text;
    }
    return "";
}

/* ---- A plugin's own settings ---- */
struct PluginSettings {
    std::map<std::string, prefab::Value> values;
    std::map<std::string, std::string> specs; /* name -> its spec as JSON (a redefinition must match) */
    prefab::Json saved;                       /* settings.json as last read or written */
    bool loaded = false, dirty = false;
};
std::map<std::string, PluginSettings>& plugin_settings()
{
    static auto* map = new std::map<std::string, PluginSettings>();
    return *map;
}
std::map<std::string, Kind>& plugin_kinds()
{
    static auto* map = new std::map<std::string, Kind>();
    return *map;
}

PluginSettings& settings_of(Plugin* p)
{
    PluginSettings& s = plugin_settings()[p->id()];
    if (!s.loaded) {
        s.loaded = true;
        s.saved = prefab::Json::object();
        std::string text, why;
        if (!p->data_dir.empty() && read_file(p->data_dir + "/settings.json", &text, RL_MAX_FILE, &why)) {
            prefab::Json j;
            if (prefab::json_parse(text.c_str(), j, &why) && j.type == prefab::Json::Object)
                s.saved = j;
            else
                log(LOG_WARN, p->id(), "settings.json is unreadable (" + why + "); using the defaults");
        }
    }
    return s;
}

void save_settings_of(const std::string& id)
{
    auto s = plugin_settings().find(id);
    auto k = plugin_kinds().find(id);
    Plugin* p = find(id);
    if (s == plugin_settings().end() || !s->second.dirty || k == plugin_kinds().end() || !p || p->data_dir.empty())
        return;
    prefab::Json out = s->second.saved; /* settings not defined in this load keep their values */
    for (const Field& f : k->second.fields) {
        auto v = s->second.values.find(f.name);
        prefab::Json j;
        if (v != s->second.values.end() && prefab::json_parse(rood_schema::to_json(f, v->second).c_str(), j, nullptr))
            out.set(f.name.c_str(), j);
    }
    std::string why;
    if (write_file(p->data_dir + "/settings.json", prefab::json_write(out) + "\n", &why)) {
        s->second.saved = out;
        s->second.dirty = false;
    } else
        log(LOG_ERROR, id, "settings: " + why);
}

Field make_field(lua_State* L, const std::string& name, const Value& spec)
{
    auto text = [&](const char* key, const std::string& fallback) {
        const Value* v = spec.get(key);
        if (!v || v->kind == Value::NIL)
            return fallback;
        if (v->kind != Value::STRING)
            luaL_error(L, "rood.settings.define: %s must be a string", key);
        return v->s;
    };
    auto number = [&](const char* key, bool required, double fallback) {
        const Value* v = spec.get(key);
        if (!v || v->kind == Value::NIL) {
            if (required)
                luaL_error(L, "rood.settings.define: '%s' needs %s", name.c_str(), key);
            return fallback;
        }
        if (v->kind != Value::INTEGER && v->kind != Value::NUMBER)
            luaL_error(L, "rood.settings.define: %s must be a number", key);
        return v->kind == Value::INTEGER ? (double)v->i : v->n;
    };
    std::string type = text("type", ""), label = text("label", name), category = text("category", "Settings"),
                help = text("help", ""), unit = text("unit", "");
    const Value* def = spec.get("default");
    if (type == "number" || type == "integer") {
        double min = number("min", true, 0), max = number("max", true, 1);
        if (!(min < max))
            luaL_error(L, "rood.settings.define: '%s' needs min < max", name.c_str());
        double step = number("step", false, type == "integer" ? 1 : (max - min) / 100);
        double d = number("default", false, min);
        if (d < min || d > max)
            raisef(L, "rood.settings.define: '%s' default %g is outside %g..%g", name.c_str(), d, min, max);
        Field f = rood_schema::number_field(name.c_str(), label.c_str(), category.c_str(), (float)min, (float)max,
            (float)step, (float)d, unit.c_str(), false, help.c_str());
        if (type == "integer")
            f.type = rood_schema::TYPE_INT;
        return f;
    }
    if (type == "boolean") {
        if (def && def->kind != Value::NIL && def->kind != Value::BOOLEAN)
            luaL_error(L, "rood.settings.define: '%s' default must be true or false", name.c_str());
        return rood_schema::bool_field(name.c_str(), label.c_str(), category.c_str(), def && def->kind == Value::BOOLEAN && def->b,
            help.c_str());
    }
    if (type == "enum") {
        std::vector<std::string> options;
        if (const Value* list = spec.get("options"))
            for (const auto& kv : list->table)
                if (kv.second.kind == Value::STRING)
                    options.push_back(kv.second.s);
        if (options.empty())
            luaL_error(L, "rood.settings.define: '%s' needs options (a list of strings)", name.c_str());
        int index = 0;
        if (def && def->kind == Value::STRING) {
            auto it = std::find(options.begin(), options.end(), def->s);
            if (it == options.end())
                luaL_error(L, "rood.settings.define: '%s' default '%s' is not one of its options", name.c_str(), def->s.c_str());
            index = (int)(it - options.begin());
        }
        return rood_schema::enum_field(name.c_str(), label.c_str(), category.c_str(), options, index, help.c_str());
    }
    if (type == "colour" || type == "color") {
        prefab::Value d = prefab::Value::vec(1, 1, 1);
        if (def && def->kind == Value::TABLE && def->length() == 3) {
            float c[3] = { 1, 1, 1 };
            for (const auto& kv : def->table)
                if (kv.first.kind == Value::INTEGER && kv.first.i >= 1 && kv.first.i <= 3)
                    c[kv.first.i - 1] = (float)(kv.second.kind == Value::INTEGER ? (double)kv.second.i : kv.second.n);
            d = prefab::Value::vec(c[0], c[1], c[2]);
        }
        return rood_schema::colour_field(name.c_str(), label.c_str(), category.c_str(), 1.0f, d, help.c_str());
    }
    luaL_error(L, "rood.settings.define: '%s' type must be number, integer, boolean, enum or colour", name.c_str());
    return Field();
}

/* ---- Watches ---- */
struct Watch {
    unsigned long long id;
    std::string plugin;
    unsigned generation;
    int ref;
    std::string setting, last;
};
std::vector<Watch>& watches()
{
    static auto* list = new std::vector<Watch>();
    return *list;
}
unsigned long long next_watch = 1;
bool flush_pending;
int flush_polls;
double flush_since;

/* ---- rood.settings ---- */
/* Another plugin's own settings are read and watched, never changed: they may
 * steer what that plugin does with its grants. */
void refuse_foreign(lua_State* L, const char* fn, const std::string& id)
{
    if (id.compare(0, 7, "plugin:") != 0)
        return;
    std::string kind = id.substr(0, id.rfind('/'));
    if (kind != "plugin:" + plugin_of(L)->id())
        luaL_error(L, "rood.settings.%s: '%s' belongs to plugin %s (a plugin changes only its own settings)", fn,
            id.c_str(), kind.c_str() + 7);
}

int api_get(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    RowRef ref;
    if (find_row(id, ref) == 2)
        return luaL_error(L, "rood.settings.get: '%s' is not shown now (it depends on another setting)", id);
    if (!push_setting(L, id))
        return luaL_error(L, "rood.settings.get: unknown setting '%s'", id);
    return 1;
}

int api_set(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    RowRef ref;
    int found = find_row(id, ref);
    if (found == 2)
        return luaL_error(L, "rood.settings.set: '%s' is not shown now (it depends on another setting)", id);
    if (found == 1) {
        double v = row_value(L, 2, ref, id);
        if (pc_ui_settings_catalog_set(ref.index, v) && !flush_pending) {
            flush_pending = true;
            flush_polls = 0;
            flush_since = now_seconds();
        }
        push_setting(L, id);
        return 1;
    }
    const Kind* k = nullptr;
    refuse_foreign(L, "set", id);
    if (const Field* f = schema_field(id, &k)) {
        prefab::Value v = field_value(L, 2, *f, id);
        rood_schema::apply_at(*k, *f, 0, v);
        push_setting(L, id);
        return 1;
    }
    ConfigKey c;
    if (config_value(id, c))
        return luaL_error(L, "rood.settings.set: '%s' is read-only here (settings.cfg, the dev tools)", id);
    return luaL_error(L, "rood.settings.set: unknown setting '%s'", id);
}

int api_reset(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    RowRef ref;
    if (find_row(id, ref))
        return luaL_error(L, "rood.settings.reset: '%s' (a System row) has no recorded default; set it instead", id);
    const Kind* k = nullptr;
    refuse_foreign(L, "reset", id);
    const Field* f = schema_field(id, &k);
    if (!f)
        return luaL_error(L, "rood.settings.reset: unknown setting '%s'", id);
    rood_schema::apply_reset_at(*k, *f, 0);
    push_setting(L, id);
    return 1;
}

int api_run(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    RowRef ref;
    if (find_row(id, ref) != 1 || ref.r.kind != PC_UI_SET_ACTION)
        return luaL_error(L, "rood.settings.run: '%s' is not an action shown now", id);
    if (!ref.r.enabled)
        return luaL_error(L, "rood.settings.run: '%s' cannot run now: %s", id, ref.r.reason);
    lua_pushboolean(L, pc_ui_settings_catalog_activate(ref.index));
    return 1;
}

int api_list(lua_State* L)
{
    std::string filter;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "page");
        if (lua_type(L, -1) == LUA_TSTRING)
            filter = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_newtable(L);
    lua_Integer n = 0;
    for (int i = 0, count = pc_ui_settings_catalog_count(); i < count; i++) {
        RowRef ref;
        ref.index = i;
        if (!pc_ui_settings_catalog_shown(i) || !pc_ui_settings_catalog_row(i, &ref.r))
            continue;
        std::string pid = home_page(ref.r.home);
        if (!filter.empty() && filter != pid)
            continue;
        push_row_info(L, ref, pid.c_str());
        lua_rawseti(L, -2, ++n);
    }
    for (const Kind* k : rood_schema::all_kinds()) {
        if (k->object != rood_selection::KIND_NONE || k->count != 1 || (!filter.empty() && filter != k->name))
            continue;
        for (const Field& f : k->fields) {
            push_field_info(L, *k, f);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

int api_pages(lua_State* L)
{
    lua_createtable(L, pc_ui_settings_catalog_homes(), 0);
    for (int home = 0; home < pc_ui_settings_catalog_homes(); home++) {
        const char* title = nullptr;
        const char* id = pc_ui_settings_catalog_home(home, &title);
        lua_createtable(L, 0, 2);
        set_text(L, "id", home_page(id));
        set_text(L, "title", title ? title : "");
        lua_rawseti(L, -2, home + 1);
    }
    return 1;
}

int api_define(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    size_t length = std::strlen(name);
    bool ok = length > 0 && length <= 48;
    for (const char* s = name; *s; s++)
        ok &= std::isalnum((unsigned char)*s) || *s == '_';
    if (!ok)
        return luaL_error(L, "rood.settings.define: bad name '%s' (1 to 48 letters, digits and _)", name);
    Plugin* p = plugin_of(L);
    std::string kind_name = "plugin:" + p->id(), id = kind_name + "/" + name;
    Value spec = Value::from_lua(L, 2);
    std::string spec_text = to_json(spec);
    if (spec_text.size() > 64 * 1024)
        return luaL_error(L, "rood.settings.define: '%s' spec larger than 64 KB", name);
    PluginSettings& s = settings_of(p);
    if (s.specs.size() >= 256 && s.specs.find(name) == s.specs.end())
        return luaL_error(L, "rood.settings.define: more than 256 settings in one plugin");
    auto old = s.specs.find(name);
    if (old != s.specs.end()) {
        if (old->second != spec_text)
            return luaL_error(L, "rood.settings.define: '%s' is already defined differently", name);
        lua_pushstring(L, id.c_str());
        return 1;
    }
    Field f = make_field(L, name, spec);
    std::string pid = p->id(), key = name;
    f.get = [pid, key](int) { return plugin_settings()[pid].values[key]; };
    f.set = [pid, key](int, const prefab::Value& v) {
        PluginSettings& ps = plugin_settings()[pid];
        ps.values[key] = v;
        ps.dirty = true;
    };
    prefab::Value initial = f.def;
    if (const prefab::Json* saved = s.saved.get(name)) {
        prefab::Value v;
        std::string why;
        if (rood_schema::from_json(f, *saved, v, &why))
            initial = rood_schema::clamp(f, v);
    }
    s.values[key] = initial;
    s.specs[key] = spec_text;
    Kind& k = plugin_kinds()[pid];
    if (k.name.empty()) {
        k.name = kind_name;
        k.label = p->manifest.name + " (plugin)";
        k.object = rood_selection::KIND_NONE;
        k.count = 1;
    }
    k.fields.push_back(f);
    rood_schema::add_kind(k);
    lua_pushstring(L, id.c_str());
    return 1;
}

int api_on_change(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    RowRef ref;
    const Kind* k = nullptr;
    ConfigKey c;
    if (!find_row(id, ref) && !schema_field(id, &k) && !config_value(id, c))
        return luaL_error(L, "rood.settings.on_change: unknown setting '%s'", id);
    Plugin* p = plugin_of(L);
    lua_settop(L, 2);
    int fn = luaL_ref(L, LUA_REGISTRYINDEX);
    watches().push_back({ next_watch, p->id(), p->generation, fn, id, current_text(id) });
    lua_pushinteger(L, (lua_Integer)next_watch++);
    return 1;
}

int api_off_change(lua_State* L)
{
    lua_Integer handle = luaL_checkinteger(L, 1);
    Plugin* p = plugin_of(L);
    std::vector<Watch>& list = watches();
    for (auto it = list.begin(); it != list.end(); ++it)
        if ((lua_Integer)it->id == handle && it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            list.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

/* Every tick and frame: changed settings call their watchers; System changes reach
 * settings.cfg (ui_settings' save) after 30 polls or half a second; plugin settings
 * are written when they changed. */
void poll_settings()
{
    std::vector<Watch> snapshot = watches();
    for (const Watch& w : snapshot) {
        std::string now = current_text(w.setting);
        auto it = std::find_if(watches().begin(), watches().end(), [&](const Watch& x) { return x.id == w.id; });
        if (it == watches().end() || now == it->last)
            continue;
        it->last = now;
        Plugin* p = find(w.plugin);
        if (!p || p->state != PLUGIN_LOADED || p->generation != w.generation || !p->disable_reason.empty())
            continue;
        run(p, ("settings change " + w.setting).c_str(), BUDGET_CALL, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, w.ref);
            if (!push_setting(L, w.setting))
                lua_pushnil(L);
            lua_call(L, 1, 0);
        });
    }
    if (flush_pending && (++flush_polls >= 30 || now_seconds() - flush_since >= 0.5)) {
        flush_pending = false;
        pc_ui_settings_flush();
    }
    for (auto& kv : plugin_settings())
        if (kv.second.dirty)
            save_settings_of(kv.first);
}

void drop_settings(Plugin* p)
{
    save_settings_of(p->id());
    std::vector<Watch>& list = watches();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Watch& w) { return w.plugin == p->id(); }), list.end());
    if (plugin_kinds().erase(p->id()))
        rood_schema::remove_kind("plugin:" + p->id());
    plugin_settings().erase(p->id());
}

void flush_settings()
{
    for (auto& kv : plugin_settings())
        save_settings_of(kv.first);
    if (flush_pending)
        pc_ui_settings_flush();
}

/* ---- The settings catalog's provider (ui_settings.h): schema fields as rows ----
 * So an Option page (option_pages.h, rood.menu) can show a plugin's own setting
 * ("plugin:<id>/<name>") or a standalone schema kind's ("<kind>/<field>"):
 * booleans are toggles, enums choices, numbers sliders, colours read-only
 * text. Changes come from the player (the menus), never another plugin. */
std::vector<std::string>& provided()
{
    static auto* ids = new std::vector<std::string>(); /* handle -> id, kept: a handle stays its id's */
    return *ids;
}
int provider_find(const char* id)
{
    if (!id || !schema_field(id, nullptr))
        return -1;
    std::vector<std::string>& ids = provided();
    for (size_t k = 0; k < ids.size(); k++)
        if (ids[k] == id)
            return (int)k;
    ids.push_back(id);
    return (int)ids.size() - 1;
}
const Field* provided_field(int handle, const Kind** kind)
{
    if (handle < 0 || handle >= (int)provided().size())
        return nullptr;
    return schema_field(provided()[(size_t)handle], kind);
}
int provider_row(int handle, pc_ui_setting_row* out)
{
    const Kind* k = nullptr;
    const Field* f = provided_field(handle, &k);
    if (!f || !out)
        return 0;
    std::memset(out, 0, sizeof *out);
    out->id = provided()[(size_t)handle].c_str();
    out->label = f->label.c_str();
    out->help = f->tooltip.c_str();
    out->home = k->name.c_str();
    out->where = PC_UI_WHERE_ALL;
    out->enabled = 1;
    out->index = out->button = -1;
    prefab::Value v = f->get(0);
    std::snprintf(out->value, sizeof out->value, "%s", rood_schema::format(*f, v).c_str());
    switch (f->type) {
    case rood_schema::TYPE_BOOL:
        out->kind = PC_UI_SET_TOGGLE;
        out->count = 2;
        out->index = v.x() != 0;
        std::snprintf(out->value, sizeof out->value, "%s", out->index ? "On" : "Off");
        break;
    case rood_schema::TYPE_ENUM:
        out->kind = PC_UI_SET_CHOICE;
        out->count = (int)f->options.size();
        out->index = (int)std::lround(v.x());
        if (out->index < 0 || out->index >= out->count)
            out->index = -1;
        break;
    case rood_schema::TYPE_FLOAT:
    case rood_schema::TYPE_INT:
    case rood_schema::TYPE_ANGLE:
        out->kind = PC_UI_SET_SLIDER;
        out->min = f->min, out->max = f->max, out->number = v.x();
        out->step = f->type == rood_schema::TYPE_INT ? std::max(1.0f, std::round(f->step)) : f->step;
        break;
    default: /* a colour, a vector: shown, changed in the dev tools */
        out->kind = PC_UI_SET_INFO;
        break;
    }
    return 1;
}
int provider_choice(int handle, int choice, const char** name)
{
    static const char* const OFF_ON[2] = { "Off", "On" };
    const Field* f = provided_field(handle, nullptr);
    if (!f)
        return 0;
    if (f->type == rood_schema::TYPE_BOOL && choice >= 0 && choice < 2) {
        *name = OFF_ON[choice];
        return 1;
    }
    if (f->type != rood_schema::TYPE_ENUM || choice < 0 || choice >= (int)f->options.size())
        return 0;
    *name = f->options[(size_t)choice].c_str();
    return 1;
}
int provider_set(int handle, double value)
{
    const Kind* k = nullptr;
    const Field* f = provided_field(handle, &k);
    if (!f || f->type == rood_schema::TYPE_COLOUR || f->type == rood_schema::TYPE_VEC3)
        return 0;
    if (f->type == rood_schema::TYPE_INT)
        value = std::round(value);
    prefab::Value before = f->get(0), v = rood_schema::clamp(*f, prefab::Value::scalar((float)value));
    if (rood_schema::same(*f, before, v))
        return 0;
    rood_schema::apply_at(*k, *f, 0, v);
    return 1;
}

void open_settings(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "get", api_get);
    set_cfunction(L, -1, "set", api_set);
    set_cfunction(L, -1, "reset", api_reset);
    set_cfunction(L, -1, "run", api_run);
    set_cfunction(L, -1, "list", api_list);
    set_cfunction(L, -1, "pages", api_pages);
    set_cfunction(L, -1, "define", api_define);
    set_cfunction(L, -1, "on_change", api_on_change);
    set_cfunction(L, -1, "off_change", api_off_change);
    lock_table(L);
    lua_setfield(L, -2, "settings");
}

struct Register {
    Register()
    {
        static const pc_ui_settings_provider provider = { provider_find, provider_row, provider_choice, provider_set };
        pc_ui_settings_set_provider(&provider);
        api_openers().push_back(open_settings);
        unload_hooks().push_back(drop_settings);
        poll_hooks().push_back(poll_settings);
        flush_hooks().push_back(flush_settings);
    }
} register_settings;
} // namespace
} // namespace rl
