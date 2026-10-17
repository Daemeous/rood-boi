// rood.storage (pc/docs/Extensions.md, "Storage"): a plugin's own key/value
// store (plain data only, at most 1 MB, saved as JSON in its data folder,
// <ROOD_PLUGIN_DATA>/<id>/store.json, a second after a change, on unload and
// at exit) and its files: read() reads the plugin's own folder (its code
// and data as installed; no capability needed), read_data / write_data /
// list_data its data folder. Nothing outside those two folders; the fs.*
// capabilities (lua_caps.cpp) go further.
#include "rl_internal.h"
#include "light_prefabs.h"

#include <algorithm>
#include <cstring>

namespace rl {
namespace {
const size_t STORE_LIMIT = 1u << 20;

struct Store {
    Value data = Value::new_table();
    bool loaded = false, dirty = false;
    double since = 0;
};
std::map<std::string, Store>& stores()
{
    static auto* map = new std::map<std::string, Store>();
    return *map;
}

Plugin* need_storage(lua_State* L, const char* fn)
{
    Plugin* p = plugin_of(L);
    if (p->data_dir.empty())
        luaL_error(L, "rood.storage.%s: this sandbox has no storage (plugins from a folder have)", fn);
    return p;
}

Store& store_of(Plugin* p)
{
    Store& s = stores()[p->id()];
    if (!s.loaded) {
        s.loaded = true;
        std::string text, why;
        prefab::Json j;
        if (read_file(p->data_dir + "/store.json", &text, STORE_LIMIT * 2, &why)) {
            if (prefab::json_parse(text.c_str(), j, &why) && j.type == prefab::Json::Object)
                s.data = from_json(j);
            else
                log(LOG_WARN, p->id(), "store.json is unreadable (" + why + "); starting empty");
        }
    }
    return s;
}

bool holds_function(const Value& v)
{
    if (v.kind == Value::FUNCTION)
        return true;
    for (const auto& kv : v.table)
        if (holds_function(kv.first) || holds_function(kv.second))
            return true;
    return false;
}

std::string checked_name(lua_State* L, const char* fn, int arg)
{
    const char* name = luaL_checkstring(L, arg);
    std::string why;
    if (!safe_relative(name, &why))
        luaL_error(L, "rood.storage.%s: bad path '%s': %s", fn, name, why.c_str());
    return name;
}

int storage_get(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    lua_settop(L, 2);
    Store& s = store_of(need_storage(L, "get"));
    const Value* v = s.data.get(key);
    if (v && v->kind != Value::NIL)
        push(L, *v);
    else
        lua_pushvalue(L, 2);
    return 1;
}

int storage_set(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    Plugin* p = need_storage(L, "set");
    size_t length = std::strlen(key);
    if (length == 0 || length > 128)
        return luaL_error(L, "rood.storage.set: a key is 1 to 128 characters");
    Value v = lua_isnoneornil(L, 2) ? Value() : Value::from_lua(L, 2);
    if (holds_function(v))
        return luaL_error(L, "rood.storage.set: '%s' holds a function; storage keeps plain data", key);
    Store& s = store_of(p);
    Value updated = s.data;
    auto& t = updated.table;
    t.erase(std::remove_if(t.begin(), t.end(),
                [&](const std::pair<Value, Value>& kv) { return kv.first.kind == Value::STRING && kv.first.s == key; }),
        t.end());
    if (v.kind != Value::NIL)
        t.emplace_back(Value::str(key), v);
    if (to_json(updated).size() > STORE_LIMIT)
        return luaL_error(L, "rood.storage.set: storage is limited to 1 MB per plugin");
    s.data = std::move(updated);
    if (!s.dirty) {
        s.dirty = true;
        s.since = now_seconds();
    }
    return 0;
}

int storage_remove(lua_State* L)
{
    lua_settop(L, 1);
    lua_pushnil(L);
    return storage_set(L);
}

int storage_keys(lua_State* L)
{
    Store& s = store_of(need_storage(L, "keys"));
    std::vector<std::string> keys;
    for (const auto& kv : s.data.table)
        if (kv.first.kind == Value::STRING && kv.second.kind != Value::NIL)
            keys.push_back(kv.first.s);
    std::sort(keys.begin(), keys.end());
    lua_createtable(L, (int)keys.size(), 0);
    for (size_t k = 0; k < keys.size(); k++) {
        lua_pushstring(L, keys[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}

int storage_flush(lua_State* L)
{
    lua_pushboolean(L, flush_storage(need_storage(L, "flush")));
    return 1;
}

int read_in(lua_State* L, const std::string& folder, const std::string& name)
{
    std::string text, why;
    if (!inside(folder, folder + "/" + name, &why))
        return luaL_error(L, "rood.storage: '%s' refused: %s", name.c_str(), why.c_str());
    if (!read_file(folder + "/" + name, &text, RL_MAX_FILE, &why)) {
        luaL_pushfail(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int storage_read(lua_State* L)
{
    std::string name = checked_name(L, "read", 1);
    Plugin* p = plugin_of(L);
    if (p->dir.empty())
        return luaL_error(L, "rood.storage.read: this sandbox has no folder");
    return read_in(L, p->dir, name);
}

int storage_read_data(lua_State* L)
{
    std::string name = checked_name(L, "read_data", 1);
    return read_in(L, need_storage(L, "read_data")->data_dir, name);
}

int storage_write_data(lua_State* L)
{
    std::string name = checked_name(L, "write_data", 1);
    size_t length;
    const char* text = luaL_checklstring(L, 2, &length);
    Plugin* p = need_storage(L, "write_data");
    if (name == "store.json" || name == "settings.json")
        return luaL_error(L, "rood.storage.write_data: '%s' is reserved (rood.storage.set, rood.settings)", name.c_str());
    if (length > RL_MAX_FILE)
        return luaL_error(L, "rood.storage.write_data: larger than %d KB", (int)(RL_MAX_FILE / 1024));
    std::string why;
    if (!inside(p->data_dir, p->data_dir + "/" + name, &why))
        return luaL_error(L, "rood.storage.write_data: '%s' refused: %s", name.c_str(), why.c_str());
    if (!write_file(p->data_dir + "/" + name, std::string(text, length), &why))
        return luaL_error(L, "rood.storage.write_data: %s", why.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int storage_list_data(lua_State* L)
{
    Plugin* p = need_storage(L, "list_data");
    std::vector<std::string> names;
    for (const std::string& name : list_dir(p->data_dir))
        if (name != "store.json" && name != "settings.json")
            names.push_back(name);
    lua_createtable(L, (int)names.size(), 0);
    for (size_t k = 0; k < names.size(); k++) {
        lua_pushstring(L, names[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}
} // namespace

bool flush_storage(Plugin* p)
{
    auto it = stores().find(p->id());
    if (it == stores().end() || !it->second.dirty)
        return true;
    std::string why;
    if (!write_file(p->data_dir + "/store.json", to_json(it->second.data), &why)) {
        log(LOG_ERROR, p->id(), "storage: " + why);
        return false;
    }
    it->second.dirty = false;
    return true;
}

void flush_storage_due()
{
    double now = now_seconds();
    for (auto& kv : stores())
        if (kv.second.dirty && now - kv.second.since >= 1.0)
            if (Plugin* p = find(kv.first))
                flush_storage(p);
}

void drop_storage(Plugin* p)
{
    flush_storage(p);
    stores().erase(p->id());
}

void open_storage(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "get", storage_get);
    set_cfunction(L, -1, "set", storage_set);
    set_cfunction(L, -1, "remove", storage_remove);
    set_cfunction(L, -1, "keys", storage_keys);
    set_cfunction(L, -1, "flush", storage_flush);
    set_cfunction(L, -1, "read", storage_read);
    set_cfunction(L, -1, "read_data", storage_read_data);
    set_cfunction(L, -1, "write_data", storage_write_data);
    set_cfunction(L, -1, "list_data", storage_list_data);
    lock_table(L);
    lua_setfield(L, -2, "storage");
}

} // namespace rl
