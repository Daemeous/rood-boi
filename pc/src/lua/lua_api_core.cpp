// The rood table every plugin gets (pc/docs/Extensions.md, "API"): log,
// warn, error (to the log and the console), version, api_version, plugin
// (its own manifest, folders and capabilities), then the modules: events
// and timers, registry, storage, tests, capabilities when granted, and what
// game-side modules add (rood.settings). Read-only views throughout.
#include "rl_internal.h"

#include <algorithm>

namespace rl {
namespace {
std::string joined(lua_State* L)
{
    std::string text;
    for (int k = 1, n = lua_gettop(L); k <= n; k++) {
        size_t len;
        const char* s = luaL_tolstring(L, k, &len);
        if (k > 1)
            text += ' ';
        text.append(s, len);
        lua_pop(L, 1);
    }
    return text;
}
int api_log(lua_State* L)
{
    log(LOG_INFO, plugin_of(L)->id(), joined(L));
    return 0;
}
int api_warn(lua_State* L)
{
    log(LOG_WARN, plugin_of(L)->id(), joined(L));
    return 0;
}
int api_error(lua_State* L)
{
    log(LOG_ERROR, plugin_of(L)->id(), joined(L));
    return 0;
}

void set_string(lua_State* L, const char* key, const std::string& value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}
void set_strings(lua_State* L, const char* key, const std::vector<std::string>& list)
{
    lua_createtable(L, (int)list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        lua_pushstring(L, list[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    lua_setfield(L, -2, key);
}

void push_plugin_info(lua_State* L, Plugin* p)
{
    const Manifest& m = p->manifest;
    lua_createtable(L, 0, 16);
    set_string(L, "id", m.id);
    set_string(L, "name", m.name);
    set_string(L, "version", m.version);
    set_string(L, "description", m.description);
    set_string(L, "author", m.author);
    set_string(L, "dir", p->dir);
    set_string(L, "data_dir", p->data_dir);
    lua_pushinteger(L, m.api_version);
    lua_setfield(L, -2, "api_version");
    lua_pushboolean(L, p->shipped);
    lua_setfield(L, -2, "shipped");
    bool elevated = false; /* an elevated capability granted (not the photo mode's) */
    for (const std::string& cap : p->granted) {
        const CapabilityInfo* info = capability_info(cap.substr(0, cap.find(':')));
        elevated |= !info || info->elevated;
    }
    lua_pushboolean(L, elevated);
    lua_setfield(L, -2, "elevated");
    set_strings(L, "capabilities", m.capabilities);
    set_strings(L, "granted", p->granted);
    std::vector<std::string> missing;
    for (const std::string& cap : m.capabilities)
        if (std::find(p->granted.begin(), p->granted.end(), cap) == p->granted.end())
            missing.push_back(cap);
    set_strings(L, "missing", missing);
    lock_table(L);
}
} // namespace

void open_rood(lua_State* L, Plugin* p)
{
    lua_newtable(L);
    set_cfunction(L, -1, "log", api_log);
    set_cfunction(L, -1, "warn", api_warn);
    set_cfunction(L, -1, "error", api_error);
    lua_pushliteral(L, RL_VERSION);
    lua_setfield(L, -2, "version");
    lua_pushinteger(L, RL_API_VERSION);
    lua_setfield(L, -2, "api_version");
    push_plugin_info(L, p);
    lua_setfield(L, -2, "plugin");
    open_events(L, p);
    open_registry(L, p);
    open_storage(L, p);
    open_capabilities(L, p);
    open_test(L, p);
    for (ApiOpener open : api_openers())
        open(L, p);
    lock_table(L);
    lua_setglobal(L, "rood");
}

} // namespace rl
