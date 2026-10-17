// Values crossing between plugin states (pc/docs/Extensions.md,
// "Isolation"): plain data is copied (nil, booleans, numbers, strings and
// tables, without their metatables); a function stays in its plugin, and
// another plugin (or the game) calls it through a proxy that runs it there,
// under that plugin's budget. JSON for the RPC, storage and manifests.
#include "rl_internal.h"
#include "light_prefabs.h" /* prefab::Json */

#include <cmath>
#include <cstdio>
#include <exception>
#include <new>

namespace rl {
namespace {
const int MAX_DEPTH = 32;
const size_t MAX_PARTS = 100000;
/* The host's copy: a string shared by many parts is copied for each, so parts alone
 * do not bound it (100000 references to a 16 MB string were 1.6 TB). */
const size_t MAX_BYTES = 32u << 20;

int proxy_call(lua_State* L);

struct Copy {
    size_t parts = 0, bytes = 0;
    std::vector<const void*> open; /* the tables being copied, outermost first */
};

void copy(lua_State* L, int idx, Value& out, int depth, Copy& c)
{
    idx = lua_absindex(L, idx);
    if (++c.parts > MAX_PARTS)
        luaL_error(L, "value too large to pass between plugins (more than %d parts)", (int)MAX_PARTS);
    c.bytes += sizeof(Value);
    if (lua_type(L, idx) == LUA_TSTRING)
        c.bytes += lua_rawlen(L, idx);
    if (c.bytes > MAX_BYTES)
        luaL_error(L, "value too large to pass between plugins (more than %d MB)", (int)(MAX_BYTES >> 20));
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        out = Value();
        return;
    case LUA_TBOOLEAN:
        out = Value::boolean(lua_toboolean(L, idx) != 0);
        return;
    case LUA_TNUMBER:
        out = lua_isinteger(L, idx) ? Value::integer((long long)lua_tointeger(L, idx))
                                    : Value::number((double)lua_tonumber(L, idx));
        return;
    case LUA_TSTRING: {
        size_t len;
        const char* s = lua_tolstring(L, idx, &len);
        out = Value::str(std::string(s, len));
        return;
    }
    case LUA_TFUNCTION: {
        out = Value();
        out.kind = Value::FUNCTION;
        if (lua_tocfunction(L, idx) == proxy_call) { /* a proxy: the function it stands for */
            lua_getupvalue(L, idx, 1);
            out.fn = *(std::shared_ptr<FunctionRef>*)lua_touserdata(L, -1);
            lua_pop(L, 1);
            return;
        }
        Plugin* p = plugin_of(L);
        auto fn = std::make_shared<FunctionRef>();
        fn->owner = p ? p->id() : std::string();
        fn->generation = p ? p->generation : 0;
        lua_pushvalue(L, idx);
        fn->ref = luaL_ref(L, LUA_REGISTRYINDEX);
        out.fn = fn;
        return;
    }
    case LUA_TTABLE:
        break;
    default:
        luaL_error(L, "cannot pass a %s value between plugins", luaL_typename(L, idx));
    }
    if (depth >= MAX_DEPTH)
        luaL_error(L, "table nested too deeply to pass between plugins (more than %d levels)", MAX_DEPTH);
    const void* self = lua_topointer(L, idx);
    for (const void* open : c.open)
        if (open == self)
            luaL_error(L, "cannot pass a table that contains itself");
    luaL_checkstack(L, 4, "copying a table");
    c.open.push_back(self);
    out = Value::new_table();
    lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
    for (lua_Integer k = 1; k <= n; k++) {
        lua_rawgeti(L, idx, k);
        Value item;
        copy(L, -1, item, depth + 1, c);
        lua_pop(L, 1);
        out.table.emplace_back(Value::integer((long long)k), std::move(item));
    }
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        int kt = lua_type(L, -2);
        if (kt == LUA_TNUMBER && lua_isinteger(L, -2)) {
            lua_Integer k = lua_tointeger(L, -2);
            if (k >= 1 && k <= n) {
                lua_pop(L, 1);
                continue;
            }
        }
        if (kt != LUA_TSTRING && kt != LUA_TNUMBER && kt != LUA_TBOOLEAN)
            luaL_error(L, "cannot pass a table with %s keys between plugins", lua_typename(L, kt));
        Value key, item;
        copy(L, -2, key, depth + 1, c);
        copy(L, -1, item, depth + 1, c);
        lua_pop(L, 1);
        out.table.emplace_back(std::move(key), std::move(item));
    }
    c.open.pop_back();
}

int release_box(lua_State* L)
{
    ((std::shared_ptr<FunctionRef>*)lua_touserdata(L, 1))->~shared_ptr();
    return 0;
}

void push_ref(lua_State* L, const std::shared_ptr<FunctionRef>& fn)
{
    Plugin* p = plugin_of(L);
    if (p && fn && fn->owner == p->id() && fn->generation == p->generation && fn->ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn->ref);
        return;
    }
    void* box = lua_newuserdatauv(L, sizeof(std::shared_ptr<FunctionRef>), 0);
    new (box) std::shared_ptr<FunctionRef>(fn);
    if (luaL_newmetatable(L, "rood.function")) {
        lua_pushcfunction(L, release_box);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
    }
    lua_setmetatable(L, -2);
    lua_pushcclosure(L, proxy_call, 1);
}

void push_value(lua_State* L, const Value& v, int depth)
{
    luaL_checkstack(L, 3, "passing a value between plugins");
    switch (v.kind) {
    case Value::NIL:
        lua_pushnil(L);
        return;
    case Value::BOOLEAN:
        lua_pushboolean(L, v.b);
        return;
    case Value::INTEGER:
        lua_pushinteger(L, (lua_Integer)v.i);
        return;
    case Value::NUMBER:
        lua_pushnumber(L, (lua_Number)v.n);
        return;
    case Value::STRING:
        lua_pushlstring(L, v.s.data(), v.s.size());
        return;
    case Value::FUNCTION:
        push_ref(L, v.fn);
        return;
    case Value::TABLE:
        break;
    }
    if (depth >= MAX_DEPTH)
        luaL_error(L, "table nested too deeply (more than %d levels)", MAX_DEPTH);
    size_t array = v.length();
    lua_createtable(L, (int)array, (int)(v.table.size() - array));
    for (const auto& kv : v.table) {
        if (kv.first.kind == Value::NIL || kv.second.kind == Value::NIL)
            continue;
        push_value(L, kv.first, depth + 1);
        push_value(L, kv.second, depth + 1);
        lua_rawset(L, -3);
    }
}

/* A function of another plugin, called from this one: arguments and results copied. */
int proxy_call(lua_State* L)
{
    static int depth;
    std::shared_ptr<FunctionRef> fn = *(std::shared_ptr<FunctionRef>*)lua_touserdata(L, lua_upvalueindex(1));
    if (depth >= 16)
        return luaL_error(L, "calls between plugins nested more than 16 deep");
    check_budget(L);
    std::vector<Value> args, results;
    std::string error;
    bool ok;
    try {
        for (int k = 1, n = lua_gettop(L); k <= n; k++)
            args.push_back(Value::from_lua(L, k));
        struct Nested {
            Nested() { ++depth; }
            ~Nested() { --depth; }
        } nested;
        ok = call_function(fn, args, &results, &error);
    } catch (const std::exception& e) {
        return luaL_error(L, "internal error: %s", e.what());
    }
    if (!ok)
        return luaL_error(L, "%s", error.c_str());
    luaL_checkstack(L, (int)results.size() + 1, "results of a call between plugins");
    for (const Value& r : results)
        push_value(L, r, 0);
    return (int)results.size();
}

std::string key_text(const Value& k)
{
    char buffer[40];
    switch (k.kind) {
    case Value::STRING:
        return k.s;
    case Value::INTEGER:
        return std::to_string(k.i);
    case Value::BOOLEAN:
        return k.b ? "true" : "false";
    case Value::NUMBER:
        std::snprintf(buffer, sizeof buffer, "%.17g", k.n);
        return buffer;
    default:
        return "?";
    }
}
} // namespace

FunctionRef::~FunctionRef()
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL)
        return;
    Plugin* p = find(owner);
    if (p && p->L && p->generation == generation)
        p->pending_unrefs.push_back(ref);
}

Value Value::str(const std::string& text)
{
    Value v;
    v.kind = STRING;
    v.s = text;
    return v;
}
Value Value::number(double x)
{
    Value v;
    v.kind = NUMBER;
    v.n = x;
    return v;
}
Value Value::integer(long long x)
{
    Value v;
    v.kind = INTEGER;
    v.i = x;
    return v;
}
Value Value::boolean(bool x)
{
    Value v;
    v.kind = BOOLEAN;
    v.b = x;
    return v;
}
Value Value::new_table()
{
    Value v;
    v.kind = TABLE;
    return v;
}
Value Value::from_lua(lua_State* L, int idx)
{
    Value out;
    Copy c;
    copy(L, idx, out, 0, c);
    return out;
}
const Value* Value::get(const char* key) const
{
    for (const auto& kv : table)
        if (kv.first.kind == STRING && kv.first.s == key)
            return &kv.second;
    return nullptr;
}
Value& Value::set(const char* key, Value v)
{
    kind = TABLE;
    for (auto& kv : table)
        if (kv.first.kind == STRING && kv.first.s == key) {
            kv.second = std::move(v);
            return kv.second;
        }
    table.emplace_back(str(key), std::move(v));
    return table.back().second;
}
size_t Value::bytes() const
{
    size_t n = sizeof(Value) + s.size();
    for (const auto& kv : table)
        n += kv.first.bytes() + kv.second.bytes();
    return n;
}
size_t Value::length() const
{
    size_t n = 0;
    for (const auto& kv : table)
        if (kv.first.kind == INTEGER && kv.first.i == (long long)n + 1)
            ++n;
    return n;
}
void Value::push_back(Value v)
{
    kind = TABLE;
    table.emplace_back(integer((long long)length() + 1), std::move(v));
}

void push(lua_State* L, const Value& v) { push_value(L, v, 0); }

bool call_function(const std::shared_ptr<FunctionRef>& fn, const std::vector<Value>& args, std::vector<Value>* results,
    std::string* error)
{
    Plugin* owner = fn ? find(fn->owner) : nullptr;
    if (!owner || !owner->L || owner->state != PLUGIN_LOADED || owner->generation != fn->generation) {
        if (error)
            *error = "the function's plugin '" + (fn ? fn->owner : std::string("?")) + "' was unloaded or reloaded";
        return false;
    }
    return run(owner, "call from another plugin", BUDGET_CALL, [&](lua_State* L) {
        int base = lua_gettop(L);
        luaL_checkstack(L, (int)args.size() + 2, "arguments of a call between plugins");
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn->ref);
        for (const Value& a : args)
            push_value(L, a, 0);
        lua_call(L, (int)args.size(), LUA_MULTRET);
        if (results)
            for (int k = base + 1; k <= lua_gettop(L); k++)
                results->push_back(Value::from_lua(L, k));
    }, error);
}

std::string to_json(const Value& v)
{
    char buffer[40];
    switch (v.kind) {
    case Value::NIL:
        return "null";
    case Value::BOOLEAN:
        return v.b ? "true" : "false";
    case Value::INTEGER:
        return std::to_string(v.i);
    case Value::NUMBER:
        if (!std::isfinite(v.n))
            return "null";
        std::snprintf(buffer, sizeof buffer, "%.17g", v.n);
        return buffer;
    case Value::STRING:
        return prefab::json_quote(v.s);
    case Value::FUNCTION:
        return prefab::json_quote("<function of " + (v.fn ? v.fn->owner : std::string("?")) + ">");
    case Value::TABLE:
        break;
    }
    size_t n = v.length();
    bool array = n > 0 && n == v.table.size();
    std::string s(1, array ? '[' : '{');
    bool first = true;
    for (const auto& kv : v.table) {
        if (!array && kv.second.kind == Value::NIL)
            continue;
        if (!first)
            s += ',';
        first = false;
        if (!array)
            s += prefab::json_quote(key_text(kv.first)) + ":";
        s += to_json(kv.second);
    }
    s += array ? ']' : '}';
    return s;
}

Value from_json(const prefab::Json& j)
{
    switch (j.type) {
    case prefab::Json::Bool:
        return Value::boolean(j.boolean);
    case prefab::Json::Number:
        if (std::floor(j.number) == j.number && std::fabs(j.number) < 9007199254740992.0)
            return Value::integer((long long)j.number);
        return Value::number(j.number);
    case prefab::Json::String:
        return Value::str(j.string);
    case prefab::Json::Array: {
        Value t = Value::new_table();
        for (const prefab::Json& item : j.items)
            t.push_back(from_json(item));
        return t;
    }
    case prefab::Json::Object: {
        Value t = Value::new_table();
        for (const auto& m : j.members)
            t.set(m.first.c_str(), from_json(m.second));
        return t;
    }
    default:
        return Value();
    }
}

} // namespace rl
