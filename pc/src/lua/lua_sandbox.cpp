// A plugin's sandbox (pc/docs/Extensions.md, "Sandbox"): its own Lua state
// with the safe standard libraries only (base, coroutine, table, string,
// math, utf8; io, os, package and debug are not even linked); no dofile,
// loadfile or warn; no string.dump; load takes text only; require loads the
// plugin's own .lua files; print goes to the log; collectgarbage only
// measures and collects; setmetatable refuses __gc (finalizers run with
// the budget's hook off); getmetatable("") gives "locked". What a plugin may
// do beyond that comes through rood (lua_api_core.cpp) and, when granted,
// its capabilities (lua_caps.cpp).
#include "rl_internal.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace rl {
namespace {
const size_t MAX_CHUNK = 1u << 20; /* load(): text up to 1 MB */
char loading_marker;

int sandbox_print(lua_State* L)
{
    std::string text;
    for (int k = 1, n = lua_gettop(L); k <= n; k++) {
        size_t len;
        const char* s = luaL_tolstring(L, k, &len);
        if (k > 1)
            text += '\t';
        text.append(s, len);
        lua_pop(L, 1);
    }
    log(LOG_INFO, plugin_of(L)->id(), text);
    return 0;
}

int sandbox_load(lua_State* L)
{
    if (lua_type(L, 1) != LUA_TSTRING)
        return luaL_error(L, "load: only text chunks (a string) can be loaded here");
    size_t len;
    const char* text = lua_tolstring(L, 1, &len);
    const char* name = luaL_optstring(L, 2, "=(load)");
    const char* mode = luaL_optstring(L, 3, "t");
    bool env = !lua_isnoneornil(L, 4);
    if (env)
        luaL_checktype(L, 4, LUA_TTABLE);
    if (std::strcmp(mode, "t") != 0) {
        luaL_pushfail(L);
        lua_pushliteral(L, "load: only text chunks (mode \"t\") can be loaded here");
        return 2;
    }
    if (len > MAX_CHUNK) {
        luaL_pushfail(L);
        lua_pushliteral(L, "load: chunk larger than 1 MB");
        return 2;
    }
    if (luaL_loadbufferx(L, text, len, name, "t") != LUA_OK) {
        luaL_pushfail(L);
        lua_insert(L, -2);
        return 2;
    }
    if (env) {
        lua_pushvalue(L, 4);
        if (!lua_setupvalue(L, -2, 1))
            lua_pop(L, 1);
    }
    return 1;
}

bool module_name_ok(const char* s)
{
    bool start = true;
    if (!*s)
        return false;
    for (; *s; s++) {
        if (*s == '.') {
            if (start)
                return false;
            start = true;
            continue;
        }
        if (!(std::isalnum((unsigned char)*s) || *s == '_'))
            return false;
        start = false;
    }
    return !start;
}

int sandbox_require(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    Plugin* p = plugin_of(L);
    if (!module_name_ok(name))
        return luaL_error(L, "require: bad module name '%s' (letters, digits, _ and dots)", name);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "rood.loaded"); /* 2 */
    lua_getfield(L, 2, name);                          /* 3 */
    if (lua_toboolean(L, 3)) {
        if (lua_islightuserdata(L, 3))
            return luaL_error(L, "require: module '%s' is loading (a cycle) or failed to load", name);
        return 1;
    }
    lua_pop(L, 1);
    if (p->dir.empty())
        return luaL_error(L, "require: module '%s' not found (this sandbox has no folder)", name);
    std::string relative = name;
    for (char& c : relative)
        if (c == '.')
            c = '/';
    relative += ".lua";
    std::string code, why;
    if (!code_path_ok(relative, &why))
        return luaL_error(L, "require: module '%s' refused (%s)", name, why.c_str());
    if (!inside(p->dir, p->dir + "/" + relative, &why))
        return luaL_error(L, "require: module '%s' refused (%s: %s)", name, relative.c_str(), why.c_str());
    if (!read_file(p->dir + "/" + relative, &code, RL_MAX_FILE, &why))
        return luaL_error(L, "require: module '%s' not found in plugin %s (%s: %s)", name, p->id().c_str(),
            relative.c_str(), why.c_str());
    lua_pushlightuserdata(L, &loading_marker);
    lua_setfield(L, 2, name);
    std::string chunk = "@" + p->id() + "/" + relative;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) /* 3 */
        return lua_error(L);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1); /* 3: the module */
    if (lua_isnil(L, 3)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, 3);
    lua_setfield(L, 2, name);
    return 1;
}

/* setmetatable without finalizers: Lua runs __gc with the debug hooks off (lgc.c
 * GCTM), so a finalizer would run outside the budget (an endless one hangs the
 * game, at a collection or at lua_close). An object is marked for finalization
 * only when setmetatable sees a __gc field, so refusing it here is enough: a
 * __gc added to the metatable later is never called. */
int sandbox_setmetatable(lua_State* L)
{
    int t = lua_type(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
    if (t == LUA_TTABLE) {
        lua_pushliteral(L, "__gc");
        bool gc = lua_rawget(L, 2) != LUA_TNIL;
        lua_pop(L, 1);
        if (gc)
            return luaL_error(L, "setmetatable: __gc is not available to plugins (a finalizer would run outside the "
                                 "budget); use rood.on('unload', ...) or a to-be-closed variable (__close)");
    }
    if (luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL)
        return luaL_error(L, "cannot change a protected metatable");
    lua_settop(L, 2);
    lua_setmetatable(L, 1);
    return 1;
}

int sandbox_collectgarbage(lua_State* L)
{
    const char* option = luaL_optstring(L, 1, "collect");
    if (!std::strcmp(option, "count")) {
        int kb = lua_gc(L, LUA_GCCOUNT), bytes = lua_gc(L, LUA_GCCOUNTB);
        lua_pushnumber(L, (lua_Number)kb + (lua_Number)bytes / 1024);
        return 1;
    }
    if (!std::strcmp(option, "collect")) {
        lua_gc(L, LUA_GCCOLLECT);
        lua_pushinteger(L, 0);
        return 1;
    }
    if (!std::strcmp(option, "step")) {
        lua_pushboolean(L, lua_gc(L, LUA_GCSTEP, (int)luaL_optinteger(L, 2, 0)));
        return 1;
    }
    if (!std::strcmp(option, "isrunning")) {
        lua_pushboolean(L, lua_gc(L, LUA_GCISRUNNING));
        return 1;
    }
    return luaL_error(L, "collectgarbage: '%s' is not allowed in a plugin (count, collect, step, isrunning)", option);
}
} // namespace

void open_sandbox(lua_State* L, Plugin*)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base },
        { LUA_COLIBNAME, luaopen_coroutine },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    for (const luaL_Reg& lib : libs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }
    for (const char* name : { "dofile", "loadfile", "warn" }) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    lua_pushliteral(L, ""); /* getmetatable("") gives "locked", not the string library's metatable */
    lua_getmetatable(L, -1);
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 2);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "rood.loaded");
    static const luaL_Reg replaced[] = {
        { "print", sandbox_print },
        { "load", sandbox_load },
        { "require", sandbox_require },
        { "collectgarbage", sandbox_collectgarbage },
        { "setmetatable", sandbox_setmetatable },
    };
    for (const luaL_Reg& f : replaced) {
        push_cfunction(L, f.func);
        lua_setglobal(L, f.name);
    }
}

} // namespace rl
