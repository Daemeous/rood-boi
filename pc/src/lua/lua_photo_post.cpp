// rood.post (the post grant; pc/docs/Extensions.md "Photo mode foundation"; the host: post_profile.cpp's
// overrides, photo_ui.cpp's format): the post profile's live values by key, a plugin's overrides over
// every layer (never saved), and the photo mode's own keys "format" and "preset".
#include "lua_photo.h"
#include "post.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
void pc_photo_format_set(const char* owner, float aspect) __attribute__((weak));
float pc_photo_format(void) __attribute__((weak));
void pc_photo_format_colour_set(const char* owner, int rgb) __attribute__((weak));
int pc_photo_format_colour(void) __attribute__((weak));
int pc_post_field_count(void) __attribute__((weak));
int pc_post_field_info(int index, pc_post_field_desc* out) __attribute__((weak));
int pc_post_field_find(const char* key) __attribute__((weak));
int pc_post_value_text(const char* key, char* out, int size) __attribute__((weak));
int pc_post_override_set(const char* owner, const char* key, const char* value, char* why, int size)
    __attribute__((weak));
void pc_post_override_clear(const char* owner, const char* key) __attribute__((weak));
int pc_post_preset_names(char* out, int size) __attribute__((weak));
}

namespace rl {
namespace {
using photo::Callback;
using photo::fail;
using photo::owner_of;
using photo::number_field;
using photo::push_vector;
using photo::vector_field;
/* The post keys each plugin overrides (rood.post.overrides; given back at unload). */
std::map<std::string, std::map<std::string, std::string>> post_owned;

/* The photo mode's own key (photo.cpp): the format mask, an aspect ratio (0: the whole picture); and
 * "preset": a named post profile under the plugin's values (a filter). */
const char* const FORMAT = "format";
const char* const PRESET = "preset";
/* ... and the format bars' colour over the view, 0xRRGGBB (0: black). */
const char* const FORMAT_COLOUR = "format_color";

void push_names(lua_State* L, const char* list)
{
    lua_newtable(L);
    int i = 0;
    for (const char* n = list; n && *n; n += strlen(n) + 1) {
        lua_pushstring(L, n);
        lua_rawseti(L, -2, ++i);
    }
}
int post_list(lua_State* L)
{
    int n = pc_post_field_count ? pc_post_field_count() : 0;
    lua_createtable(L, n + 3, 0);
    pc_post_field_desc d;
    for (int i = 0; i < n && pc_post_field_info(i, &d); i++) {
        lua_createtable(L, 0, 12);
        lua_pushstring(L, d.key);
        lua_setfield(L, -2, "key");
        lua_pushstring(L, d.kind);
        lua_setfield(L, -2, "kind");
        lua_pushstring(L, d.group);
        lua_setfield(L, -2, "group");
        lua_pushstring(L, d.label);
        lua_setfield(L, -2, "label");
        lua_pushstring(L, d.tip ? d.tip : "");
        lua_setfield(L, -2, "tip");
        lua_pushinteger(L, d.count);
        lua_setfield(L, -2, "components");
        if (!strcmp(d.kind, "float") || !strcmp(d.kind, "color") || !strcmp(d.kind, "vec4")) {
            lua_pushnumber(L, d.lo);
            lua_setfield(L, -2, "min");
            lua_pushnumber(L, d.hi);
            lua_setfield(L, -2, "max");
            lua_pushboolean(L, d.log);
            lua_setfield(L, -2, "log");
            lua_pushboolean(L, d.integer);
            lua_setfield(L, -2, "integer");
        }
        if (!strcmp(d.kind, "enum")) {
            push_names(L, d.names);
            lua_setfield(L, -2, "choices");
            push_names(L, d.labels);
            lua_setfield(L, -2, "labels");
        }
        if (d.count == 1 && strcmp(d.kind, "enum"))
            lua_pushnumber(L, d.def[0]);
        else if (!strcmp(d.kind, "enum")) {
            const char* name = d.names;
            for (int k = 0; k < (int)d.def[0] && *name; k++)
                name += strlen(name) + 1;
            lua_pushstring(L, name);
        } else if (d.count > 1) {
            lua_createtable(L, d.count, 0);
            for (int k = 0; k < d.count; k++) {
                lua_pushnumber(L, d.def[k]);
                lua_rawseti(L, -2, k + 1);
            }
        } else
            lua_pushstring(L, "");
        if (!strcmp(d.kind, "bool")) {
            lua_pop(L, 1);
            lua_pushboolean(L, d.def[0] != 0);
        }
        lua_setfield(L, -2, "default");
        lua_rawseti(L, -2, i + 1);
    }
    /* A named profile (pc/assets/post, pc/user/post) under the plugin's values: a filter. */
    lua_createtable(L, 0, 10);
    lua_pushstring(L, PRESET);
    lua_setfield(L, -2, "key");
    lua_pushstring(L, "enum");
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, "Photo|Filter");
    lua_setfield(L, -2, "group");
    lua_pushstring(L, "Filter (post preset)");
    lua_setfield(L, -2, "label");
    lua_pushstring(L, "A named post profile under the values set here (\"\": none).");
    lua_setfield(L, -2, "tip");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "components");
    {
        char names[2048];
        if (!pc_post_preset_names || !pc_post_preset_names(names, (int)sizeof names))
            names[0] = names[1] = 0;
        lua_newtable(L);
        lua_pushstring(L, "");
        lua_rawseti(L, -2, 1);
        int k = 1;
        for (const char* n = names; *n; n += strlen(n) + 1) {
            lua_pushstring(L, n);
            lua_rawseti(L, -2, ++k);
        }
        lua_setfield(L, -2, "choices");
    }
    lua_pushstring(L, "");
    lua_setfield(L, -2, "default");
    lua_rawseti(L, -2, n + 1);
    /* The photo mode's format mask. */
    lua_createtable(L, 0, 10);
    lua_pushstring(L, FORMAT);
    lua_setfield(L, -2, "key");
    lua_pushstring(L, "float");
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, "Photo|Format");
    lua_setfield(L, -2, "group");
    lua_pushstring(L, "Format (aspect ratio)");
    lua_setfield(L, -2, "label");
    lua_pushstring(L, "The picture cropped to this width over height (2.39, 1.85, 1, 0.8, ...): bars over the view, "
                      "the photo cropped to it. 0: the whole picture.");
    lua_setfield(L, -2, "tip");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "components");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "min");
    lua_pushnumber(L, 5);
    lua_setfield(L, -2, "max");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "default");
    lua_rawseti(L, -2, n + 2);
    lua_createtable(L, 0, 10);
    lua_pushstring(L, FORMAT_COLOUR);
    lua_setfield(L, -2, "key");
    lua_pushstring(L, "float");
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, "Photo|Format");
    lua_setfield(L, -2, "group");
    lua_pushstring(L, "Format bars colour");
    lua_setfield(L, -2, "label");
    lua_pushstring(L, "The format's bars over the view, 0xRRGGBB (0: black); the photo is cropped to the format.");
    lua_setfield(L, -2, "tip");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "components");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "min");
    lua_pushnumber(L, 16777215);
    lua_setfield(L, -2, "max");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "default");
    lua_rawseti(L, -2, n + 3);
    return 1;
}
/* The text the settings write for a field, as Lua: a boolean, a number, a name, a list of numbers. */
void push_value(lua_State* L, const pc_post_field_desc& d, const char* text)
{
    if (!strcmp(d.kind, "bool")) {
        lua_pushboolean(L, atof(text) != 0);
    } else if (!strcmp(d.kind, "enum") || !strcmp(d.kind, "text")) {
        lua_pushstring(L, text);
    } else if (d.count > 1) {
        lua_createtable(L, d.count, 0);
        const char* s = text;
        for (int k = 0; k < d.count; k++) {
            char* end;
            lua_pushnumber(L, strtod(s, &end));
            lua_rawseti(L, -2, k + 1);
            s = *end == ',' ? end + 1 : end;
        }
    } else
        lua_pushnumber(L, atof(text));
}
int post_get(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    if (!strcmp(key, FORMAT)) {
        lua_pushnumber(L, pc_photo_format ? pc_photo_format() : 0);
        return 1;
    }
    if (!strcmp(key, FORMAT_COLOUR)) {
        lua_pushinteger(L, pc_photo_format_colour ? pc_photo_format_colour() : 0);
        return 1;
    }
    char text[512];
    if (!strcmp(key, PRESET)) {
        if (!pc_post_value_text || pc_post_value_text(key, text, (int)sizeof text))
            text[0] = 0;
        lua_pushstring(L, text);
        return 1;
    }
    int i = pc_post_field_find ? pc_post_field_find(key) : -1;
    pc_post_field_desc d;
    if (i < 0 || !pc_post_field_info(i, &d) || pc_post_value_text(key, text, (int)sizeof text))
        return luaL_error(L, "rood.post.get: no post field '%s' (rood.post.list())", key);
    push_value(L, d, text);
    return 1;
}
/* A Lua value as the text a field parses. */
std::string value_text(lua_State* L, int idx)
{
    char n[64];
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx) ? "1" : "0";
    case LUA_TNUMBER:
        snprintf(n, sizeof n, "%.9g", lua_tonumber(L, idx));
        return n;
    case LUA_TSTRING:
        return lua_tostring(L, idx);
    case LUA_TTABLE: {
        std::string out;
        for (lua_Integer k = 1; k <= 4; k++) {
            lua_rawgeti(L, idx, k);
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            snprintf(n, sizeof n, "%.9g", lua_tonumber(L, -1));
            out += (out.empty() ? "" : ",") + std::string(n);
            lua_pop(L, 1);
        }
        return out;
    }
    default:
        luaL_argerror(L, idx, "a boolean, a number, a name or a list of numbers");
        return "";
    }
}
int post_set(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    Plugin* p = plugin_of(L);
    if (!strcmp(key, FORMAT)) {
        float aspect = (float)luaL_checknumber(L, 2);
        if (!std::isfinite(aspect) || aspect < 0 || aspect > 5)
            return fail(L, "format: an aspect ratio 0..5 (0: the whole picture)");
        if (pc_photo_format_set)
            pc_photo_format_set(p->id().c_str(), aspect);
        if (aspect > 0)
            post_owned[p->id()][FORMAT] = value_text(L, 2);
        else
            post_owned[p->id()].erase(FORMAT);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!strcmp(key, FORMAT_COLOUR)) {
        lua_Number rgb = luaL_checknumber(L, 2);
        if (!(rgb >= 0 && rgb <= 0xFFFFFF))
            return fail(L, "format_color: 0xRRGGBB (0 to 16777215)");
        if (pc_photo_format_colour_set)
            pc_photo_format_colour_set(p->id().c_str(), (int)rgb);
        if (rgb > 0)
            post_owned[p->id()][FORMAT_COLOUR] = value_text(L, 2);
        else
            post_owned[p->id()].erase(FORMAT_COLOUR);
        lua_pushboolean(L, 1);
        return 1;
    }
    std::string text = value_text(L, 2);
    char why[256] = "";
    if (!pc_post_override_set)
        return fail(L, "no post-processing in this build");
    if (pc_post_override_set(p->id().c_str(), key, text.c_str(), why, (int)sizeof why))
        return fail(L, why);
    post_owned[p->id()][key] = text;
    lua_pushboolean(L, 1);
    return 1;
}
int post_reset(lua_State* L)
{
    Plugin* p = plugin_of(L);
    const char* key = luaL_optstring(L, 1, nullptr);
    if (!key || !strcmp(key, FORMAT)) {
        if (pc_photo_format_set)
            pc_photo_format_set(p->id().c_str(), 0);
    }
    if ((!key || !strcmp(key, FORMAT_COLOUR)) && pc_photo_format_colour_set)
        pc_photo_format_colour_set(p->id().c_str(), 0);
    if (pc_post_override_clear && (!key || (strcmp(key, FORMAT) && strcmp(key, FORMAT_COLOUR))))
        pc_post_override_clear(p->id().c_str(), key);
    if (key)
        post_owned[p->id()].erase(key);
    else
        post_owned.erase(p->id());
    return 0;
}
int post_overrides(lua_State* L)
{
    lua_newtable(L);
    auto it = post_owned.find(plugin_of(L)->id());
    if (it != post_owned.end())
        for (const auto& kv : it->second) {
            lua_pushstring(L, kv.second.c_str());
            lua_setfield(L, -2, kv.first.c_str());
        }
    return 1;
}

/* An unload, or everything taken back by the host: the plugin's overrides gone. */
void drop_post_owner(const std::string& id)
{
    if (pc_post_override_clear)
        pc_post_override_clear(id.c_str(), nullptr);
    post_owned.erase(id);
}
void drop_post(Plugin* p) { drop_post_owner(p->id()); }

void open_post(lua_State* L, Plugin* p)
{
    if (!granted(p, "post"))
        return;
    lua_newtable(L);
    set_cfunction(L, -1, "list", post_list);
    set_cfunction(L, -1, "get", post_get);
    set_cfunction(L, -1, "set", post_set);
    set_cfunction(L, -1, "reset", post_reset);
    set_cfunction(L, -1, "overrides", post_overrides);
    lock_table(L);
    lua_setfield(L, -2, "post");
}

struct Register {
    Register()
    {
        api_openers().push_back(open_post);
        unload_hooks().push_back(drop_post);
        photo::released_hooks().push_back(drop_post_owner);
    }
} register_post;
} // namespace
} // namespace rl
