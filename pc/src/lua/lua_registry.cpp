// rood.registry (pc/docs/Extensions.md, "Registry"): open registries keyed
// by kind ("menu.entry", "my.plugin.widget": lowercase words joined by
// dots) and namespaced ids ("<plugin id>/<name>": a plugin registers only
// in its own namespace and only the owner removes). Entries are data
// (copies; functions in them run in their owner, lua_value.cpp), ordered by
// relation, not index: after / before name an id or "group:<name>"; ties
// and cycles keep registration order. Everything a plugin registered goes
// when it unloads (drop_registry); registry.changed tells the others.
#include "rl_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <queue>
#include <set>
#include <unordered_map>

namespace rl {
namespace {
std::vector<RegistryEntry>& entries()
{
    static auto* list = new std::vector<RegistryEntry>();
    return *list;
}
unsigned long long next_seq = 1;
std::set<std::string> warned; /* kinds whose cycle was reported since the last change */
/* Bounds that keep a listing (and the registry.changed events of an unload) short:
 * entries per plugin, ids in one after / before, relations in one listing. */
const size_t MAX_ENTRIES = 4096, MAX_REFS = 32, MAX_EDGES = 1u << 20;

bool kind_ok(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 64 || !std::islower((unsigned char)s[0]))
        return false;
    for (size_t k = 0; k < n; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c == '.') {
            if (k + 1 == n || !std::islower((unsigned char)s[k + 1]))
                return false;
            continue;
        }
        if (!(std::islower(c) || std::isdigit(c) || c == '_'))
            return false;
    }
    return true;
}
bool local_ok(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 64)
        return false;
    for (; *s; s++)
        if (!(std::isalnum((unsigned char)*s) || *s == '_' || *s == '.' || *s == '-'))
            return false;
    return true;
}

std::vector<std::string> string_list(lua_State* L, int table, const char* field)
{
    std::vector<std::string> out;
    lua_getfield(L, table, field);
    if (lua_type(L, -1) == LUA_TSTRING)
        out.push_back(lua_tostring(L, -1));
    else if (lua_istable(L, -1))
        for (lua_Integer k = 1; k <= (lua_Integer)lua_rawlen(L, -1); k++) {
            if (out.size() >= MAX_REFS)
                luaL_error(L, "rood.registry.register: more than %d ids in %s", (int)MAX_REFS, field);
            lua_rawgeti(L, -1, k);
            if (lua_type(L, -1) == LUA_TSTRING)
                out.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    else if (!lua_isnil(L, -1))
        luaL_error(L, "rood.registry.register: %s is an id or a list of ids", field);
    lua_pop(L, 1);
    return out;
}

void changed(const RegistryEntry& e, const char* action)
{
    warned.clear();
    Value payload = Value::new_table();
    payload.set("kind", Value::str(e.kind));
    payload.set("id", Value::str(e.id));
    payload.set("action", Value::str(action));
    emit("registry.changed", payload);
}

void push_strings(lua_State* L, const std::vector<std::string>& list)
{
    lua_createtable(L, (int)list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        lua_pushstring(L, list[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
}
void push_entry(lua_State* L, const RegistryEntry& e)
{
    lua_createtable(L, 0, 7);
    lua_pushstring(L, e.kind.c_str());
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, e.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, e.owner.c_str());
    lua_setfield(L, -2, "owner");
    if (!e.group.empty()) {
        lua_pushstring(L, e.group.c_str());
        lua_setfield(L, -2, "group");
    }
    push_strings(L, e.after);
    lua_setfield(L, -2, "after");
    push_strings(L, e.before);
    lua_setfield(L, -2, "before");
    push(L, e.data);
    lua_setfield(L, -2, "data");
}

int api_register(lua_State* L)
{
    const char* kind = luaL_checkstring(L, 1);
    const char* id = luaL_checkstring(L, 2);
    Plugin* p = plugin_of(L);
    if (!kind_ok(kind))
        return luaL_error(L, "rood.registry.register: bad kind '%s' (lowercase words joined by dots)", kind);
    std::string prefix = p->id() + "/";
    if (std::strncmp(id, prefix.c_str(), prefix.size()) != 0 || !local_ok(id + prefix.size()))
        return luaL_error(L, "rood.registry.register: id '%s' must be in this plugin's namespace ('%s<name>')", id,
            prefix.c_str());
    size_t own = 0;
    for (const RegistryEntry& e : entries()) {
        if (e.kind == kind && e.id == id)
            return luaL_error(L, "rood.registry.register: '%s' is already registered as a %s (remove it first)", id, kind);
        own += e.owner == p->id();
    }
    if (own >= MAX_ENTRIES)
        return luaL_error(L, "rood.registry.register: more than %d entries in one plugin", (int)MAX_ENTRIES);
    RegistryEntry e;
    e.kind = kind;
    e.id = id;
    e.owner = p->id();
    e.data = lua_isnoneornil(L, 3) ? Value() : Value::from_lua(L, 3);
    size_t held = e.data.bytes(), cap = (size_t)(config.memory_mb * 1048576.0);
    for (const RegistryEntry& other : entries())
        if (other.owner == e.owner)
            held += other.data.bytes();
    if (held > cap) /* the host keeps entries: bounded like the plugin's own memory */
        return luaL_error(L, "rood.registry.register: this plugin's entries would hold more than %d MB (plugin_memory_mb)",
            (int)config.memory_mb);
    if (lua_istable(L, 4)) {
        e.after = string_list(L, 4, "after");
        e.before = string_list(L, 4, "before");
        lua_getfield(L, 4, "group");
        if (lua_type(L, -1) == LUA_TSTRING)
            e.group = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    e.seq = next_seq++;
    entries().push_back(e);
    changed(e, "register");
    lua_pushstring(L, id);
    return 1;
}

int api_get(lua_State* L)
{
    const RegistryEntry* e = registry_get(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    if (e)
        push_entry(L, *e);
    else
        lua_pushnil(L);
    return 1;
}

int api_list(lua_State* L)
{
    const char* kind = luaL_checkstring(L, 1);
    std::string group;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "group");
        if (lua_type(L, -1) == LUA_TSTRING)
            group = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    std::vector<const RegistryEntry*> list = registry_list(kind, group);
    lua_createtable(L, (int)list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        push_entry(L, *list[k]);
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}

int api_remove(lua_State* L)
{
    const char* kind = luaL_checkstring(L, 1);
    const char* id = luaL_checkstring(L, 2);
    Plugin* p = plugin_of(L);
    std::vector<RegistryEntry>& list = entries();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->kind != kind || it->id != id)
            continue;
        if (it->owner != p->id())
            return luaL_error(L, "rood.registry.remove: '%s' belongs to plugin %s", id, it->owner.c_str());
        RegistryEntry gone = *it;
        list.erase(it);
        changed(gone, "remove");
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int api_kinds(lua_State* L)
{
    lua_newtable(L);
    for (const std::string& kind : registry_kinds()) {
        lua_pushinteger(L, (lua_Integer)registry_list(kind).size());
        lua_setfield(L, -2, kind.c_str());
    }
    return 1;
}
} // namespace

std::vector<const RegistryEntry*> registry_list(const std::string& kind, const std::string& group)
{
    std::vector<const RegistryEntry*> nodes;
    for (const RegistryEntry& e : entries()) /* registration order */
        if (e.kind == kind)
            nodes.push_back(&e);
    size_t n = nodes.size(), edges = 0;
    std::unordered_map<std::string, size_t> by_id;
    std::unordered_map<std::string, std::vector<size_t>> by_group;
    for (size_t k = 0; k < n; k++) {
        by_id.emplace(nodes[k]->id, k);
        if (!nodes[k]->group.empty())
            by_group[nodes[k]->group].push_back(k);
    }
    std::vector<std::vector<size_t>> next(n);
    std::vector<int> incoming(n, 0);
    bool clipped = false;
    auto edge = [&](size_t from, size_t to) {
        if (from == to)
            return;
        if (++edges > MAX_EDGES) { /* a group related to a group, thousands each: order what fits */
            clipped = true;
            return;
        }
        next[from].push_back(to);
        incoming[to]++;
    };
    auto each_target = [&](const std::string& ref, const std::function<void(size_t)>& f) {
        if (ref.compare(0, 6, "group:") == 0) {
            auto it = by_group.find(ref.substr(6));
            if (it != by_group.end())
                for (size_t t : it->second)
                    f(t);
        } else {
            auto it = by_id.find(ref);
            if (it != by_id.end())
                f(it->second);
        }
    };
    for (size_t k = 0; k < n; k++) {
        for (const std::string& a : nodes[k]->after)
            each_target(a, [&](size_t t) { edge(t, k); });
        for (const std::string& b : nodes[k]->before)
            each_target(b, [&](size_t t) { edge(k, t); });
    }
    /* The earliest registered entry that is ready, each time (a min-heap); a cycle: the
     * earliest registered of what is left. */
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    for (size_t k = 0; k < n; k++)
        if (incoming[k] == 0)
            ready.push(k);
    std::vector<const RegistryEntry*> ordered;
    std::vector<bool> done(n, false);
    size_t scan = 0;
    bool cycle = false;
    while (ordered.size() < n) {
        size_t pick = n;
        while (!ready.empty() && pick == n) {
            size_t k = ready.top();
            ready.pop();
            if (!done[k])
                pick = k;
        }
        if (pick == n) {
            cycle = true;
            while (done[scan])
                scan++;
            pick = scan;
        }
        done[pick] = true;
        ordered.push_back(nodes[pick]);
        for (size_t t : next[pick])
            if (--incoming[t] == 0 && !done[t])
                ready.push(t);
    }
    if (cycle && warned.insert(kind).second)
        log(LOG_WARN, "", "registry kind '" + kind + "': after/before form a cycle; registration order breaks it");
    if (clipped && warned.insert(kind + " (relations)").second)
        log(LOG_WARN, "", "registry kind '" + kind + "': more than " + std::to_string(MAX_EDGES)
            + " after/before relations; the rest are ignored");
    if (!group.empty())
        ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                          [&](const RegistryEntry* e) { return e->group != group; }),
            ordered.end());
    return ordered;
}

const RegistryEntry* registry_get(const std::string& kind, const std::string& id)
{
    for (const RegistryEntry& e : entries())
        if (e.kind == kind && e.id == id)
            return &e;
    return nullptr;
}

std::vector<std::string> registry_kinds()
{
    std::set<std::string> kinds;
    for (const RegistryEntry& e : entries())
        kinds.insert(e.kind);
    return std::vector<std::string>(kinds.begin(), kinds.end());
}

size_t registry_count(const std::string& owner)
{
    return (size_t)std::count_if(entries().begin(), entries().end(),
        [&](const RegistryEntry& e) { return e.owner == owner; });
}

void drop_registry(const std::string& owner)
{
    std::vector<RegistryEntry> gone;
    std::vector<RegistryEntry>& list = entries();
    for (auto it = list.begin(); it != list.end();)
        if (it->owner == owner) {
            gone.push_back(*it);
            it = list.erase(it);
        } else
            ++it;
    for (const RegistryEntry& e : gone)
        changed(e, "remove");
}

void open_registry(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "register", api_register);
    set_cfunction(L, -1, "get", api_get);
    set_cfunction(L, -1, "list", api_list);
    set_cfunction(L, -1, "remove", api_remove);
    set_cfunction(L, -1, "kinds", api_kinds);
    lock_table(L);
    lua_setfield(L, -2, "registry");
}

} // namespace rl
