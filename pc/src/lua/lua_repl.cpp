// The REPL bridge (plan pc/docs/plans/2026-10-18-freedom-and-fast-iteration.md F6;
// pc/docs/Extensions.md "The REPL"): nREPL-style sessions over RPC lua {action
// "repl"}, used mostly by agents (pc/tools/rood_repl.py). Dev mode only (ROOD_RPC
// set or ROOD_DEV=1). A session evaluates in one sandbox: a plugin's (its globals,
// its module as M) or a console's ("@console", "@<name>"). Its bindings live in its
// own environment over the sandbox's globals: a new name stays in the session, a
// name the sandbox already has is the sandbox's (so redefining a plugin's global
// function is live). A reload of the plugin starts the session again (its state is
// gone), so the file wins.
//   op eval {code, session, plugin}  as the console evaluates (an expression first, else
//                                    statements; BUDGET_TEST: never a counted fault):
//                                    {session, plugin, generation, ok, values [printed],
//                                    results [JSON, null when not plain data], output [what
//                                    it logged], error, traceback, incomplete (the code
//                                    ends early: more lines), reset (why the session's
//                                    bindings were dropped)}
//   op describe {code, ...}          an expression's first value: type, printed, keys,
//                                    length, metatable, a function's source and upvalues
//   op inspect {what, arg, ...}      repl.<what>(arg) in the session: plugins, claims,
//                                    exports, queries, globals, upvalues, complete
//   op sessions                      every session
//   op close {session}
// In a session the table `repl` has the same (repl.describe(v), repl.claims(), ...) and
// the debug pieces plugins never get: repl.upvalues(f), repl.getupvalue(f, name),
// repl.setupvalue(f, name, v), repl.where(f), repl.claim(kind) (this plugin's draw
// function), repl.reload(); `_` is the last eval's first value, `_e` its last error.
#include "rl_internal.h"
#include "hud/hud_internal.h"
#include "light_prefabs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rl {
namespace {
using prefab::Json;

const size_t MAX_SESSIONS = 64;
const size_t MAX_OUTPUT = 64u << 10; /* the output lines a reply carries */

struct Session {
    std::string id, plugin;
    unsigned generation = 0;
    int env = LUA_NOREF; /* in the plugin's registry */
    unsigned long long evals = 0;
    double used = 0;
};
std::vector<Session>& sessions()
{
    static auto* list = new std::vector<Session>();
    return *list;
}
unsigned long long next_session = 1;
const char HELPERS_KEY = 0; /* the registry's helpers table (make_env) */

bool dev_mode()
{
    const char* rpc = std::getenv("ROOD_RPC");
    const char* dev = std::getenv("ROOD_DEV");
    return (rpc && *rpc) || (dev && !std::strcmp(dev, "1"));
}

std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string num(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.10g", v);
    return buffer;
}
const std::string* text_param(const Json& params, const char* key)
{
    const Json* j = params.get(key);
    return j && j->type == Json::String ? &j->string : nullptr;
}
bool id_ok(const std::string& id)
{
    if (id.empty() || id.size() > 32)
        return false;
    for (char c : id)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}

// ---- the debug pieces (C): only a REPL session's `repl` reaches them ----
void set_string(lua_State* L, const char* key, const char* value)
{
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}
void set_int(lua_State* L, const char* key, lua_Integer value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}
void set_bool(lua_State* L, const char* key, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

// fninfo(f) -> {what, source, short_src, line, last_line, nparams, vararg, upvalues = {names}}
int prim_fninfo(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_Debug ar;
    lua_pushvalue(L, 1);
    lua_getinfo(L, ">Slu", &ar);
    lua_createtable(L, 0, 8);
    set_string(L, "what", ar.what);
    set_string(L, "source", ar.source[0] == '@' || ar.source[0] == '=' ? ar.source + 1 : "(text)");
    set_string(L, "short_src", ar.short_src);
    set_int(L, "line", ar.linedefined);
    set_int(L, "last_line", ar.lastlinedefined);
    set_int(L, "nparams", ar.nparams);
    set_bool(L, "vararg", ar.isvararg);
    lua_createtable(L, ar.nups, 0);
    for (int i = 1; i <= ar.nups; ++i) {
        const char* name = lua_getupvalue(L, 1, i);
        lua_pop(L, 1);
        lua_pushstring(L, name && *name ? name : "?");
        lua_rawseti(L, -2, i);
    }
    lua_setfield(L, -2, "upvalues");
    return 1;
}

// The upvalue of f named name: its index, or 0.
int upvalue_index(lua_State* L, int f, const char* name)
{
    for (int i = 1;; ++i) {
        const char* n = lua_getupvalue(L, f, i);
        if (!n)
            return 0;
        lua_pop(L, 1);
        if (!std::strcmp(n, name))
            return i;
    }
}

// upvalues(f) -> {name = value} (a Lua function's; not _ENV)
int prim_upvalues(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_newtable(L);
    for (int i = 1;; ++i) {
        const char* n = lua_getupvalue(L, 1, i);
        if (!n)
            break;
        if (!*n || !std::strcmp(n, "_ENV")) {
            lua_pop(L, 1);
            continue;
        }
        lua_setfield(L, -2, n);
    }
    return 1;
}

// getupvalue(f, name) -> value, found
int prim_getupvalue(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int i = upvalue_index(L, 1, luaL_checkstring(L, 2));
    if (!i) {
        lua_pushnil(L);
        lua_pushboolean(L, 0);
        return 2;
    }
    lua_getupvalue(L, 1, i);
    lua_pushboolean(L, 1);
    return 2;
}

// setupvalue(f, name, v) -> found: every closure sharing the upvalue sees v (a local of the main chunk)
int prim_setupvalue(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checkany(L, 3);
    int i = upvalue_index(L, 1, luaL_checkstring(L, 2));
    if (i) {
        lua_settop(L, 3);
        lua_setupvalue(L, 1, i);
    }
    lua_pushboolean(L, i != 0);
    return 1;
}

int prim_module(lua_State* L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &module_key);
    return 1;
}

// Another plugin's state may be read (never run) while it is not in a call: its stack is ours.
lua_State* readable(lua_State* L, Plugin* q)
{
    if (q == plugin_of(L))
        return L;
    if (!q->L || q->depth != 0 || !lua_checkstack(q->L, 4))
        return nullptr;
    return q->L;
}

// The function at the top of S: "source:line" (popped).
std::string where_top(lua_State* S)
{
    lua_Debug ar;
    lua_getinfo(S, ">S", &ar);
    if (!std::strcmp(ar.what, "C"))
        return "[C]";
    return std::string(ar.short_src) + ":" + std::to_string(ar.linedefined);
}

// plugins() -> {{id, state, status, generation, shipped, console, enabled, claims = {kinds},
//               exports = {{name, type, where}}}, ...}
int prim_plugins(lua_State* L)
{
    std::vector<ClaimInfo> claims = hud_claims();
    lua_newtable(L);
    int n = 0;
    for (auto& up : all()) {
        Plugin& p = *up;
        lua_createtable(L, 0, 9);
        set_string(L, "id", p.id().c_str());
        set_string(L, "state", state_name(p.state));
        set_string(L, "status", p.status.c_str());
        set_int(L, "generation", p.generation);
        set_bool(L, "shipped", p.shipped);
        set_bool(L, "console", p.console);
        set_bool(L, "enabled", p.console || plugin_enabled(&p));
        lua_newtable(L);
        int k = 0;
        for (const ClaimInfo& c : claims)
            if (c.plugin == &p) {
                lua_pushstring(L, c.kind);
                lua_rawseti(L, -2, ++k);
            }
        lua_setfield(L, -2, "claims");
        lua_State* S = p.state == PLUGIN_LOADED ? readable(L, &p) : nullptr;
        if (S && lua_rawgetp(S, LUA_REGISTRYINDEX, &module_key) == LUA_TTABLE) {
            struct Export {
                std::string name, type, where;
            };
            std::vector<Export> exports;
            lua_pushnil(S);
            while (lua_next(S, -2)) {
                if (lua_type(S, -2) == LUA_TSTRING) {
                    Export e{ lua_tostring(S, -2), luaL_typename(S, -1), "" };
                    if (lua_isfunction(S, -1)) {
                        lua_pushvalue(S, -1);
                        e.where = where_top(S);
                    }
                    exports.push_back(e);
                }
                lua_pop(S, 1);
            }
            lua_pop(S, 1);
            std::sort(exports.begin(), exports.end(), [](const Export& a, const Export& b) { return a.name < b.name; });
            lua_createtable(L, (int)exports.size(), 0);
            for (size_t i = 0; i < exports.size(); ++i) {
                lua_createtable(L, 0, 3);
                set_string(L, "name", exports[i].name.c_str());
                set_string(L, "type", exports[i].type.c_str());
                if (!exports[i].where.empty())
                    set_string(L, "where", exports[i].where.c_str());
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            lua_setfield(L, -2, "exports");
        } else if (S) {
            lua_pop(S, 1);
        }
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// claims() -> {{kind, owner, active, failed, honoured, where, fn (this plugin's own)}, ...}
int prim_claims(lua_State* L)
{
    Plugin* self = plugin_of(L);
    lua_newtable(L);
    int n = 0;
    for (const ClaimInfo& c : hud_claims()) {
        lua_createtable(L, 0, 7);
        set_string(L, "kind", c.kind);
        set_string(L, "owner", c.plugin->id().c_str());
        set_bool(L, "active", c.active);
        set_bool(L, "failed", c.failed);
        set_bool(L, "honoured", c.active && !c.failed && pc_hud_honoured(hud::kind_of(c.kind)));
        if (lua_State* S = readable(L, c.plugin)) {
            lua_rawgeti(S, LUA_REGISTRYINDEX, c.ref);
            set_string(L, "where", where_top(S).c_str());
        }
        if (c.plugin == self) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, c.ref);
            lua_setfield(L, -2, "fn");
        }
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// reload([id]) -> true: the plugin (default this one) reloads at the next safe point, after the eval
int prim_reload(lua_State* L)
{
    Plugin* p = lua_isnoneornil(L, 1) ? plugin_of(L) : find(luaL_checkstring(L, 1));
    if (!p || p->console)
        return luaL_error(L, "repl.reload: %s", p ? "a console sandbox has no file to reload" : "no such plugin");
    p->reload_requested = true;
    lua_pushboolean(L, 1);
    return 1;
}

// The helpers (Lua): make_env(id) -> a session's environment, and what C++ calls for it.
const char HELPERS[] = R"LUA(
local C = ...
local G = _ENV
local type, pairs, next, tostring, select, pcall, rawget, rawset, getmetatable, setmetatable, error =
  type, pairs, next, tostring, select, pcall, rawget, rawset, getmetatable, setmetatable, error
local fmt, concat, sort, mtype = string.format, table.concat, table.sort, math.type
local LIMIT, ITEMS, DEPTH = 65536, 200, 6
local KEYWORDS = {}
for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true "
          .. "until while"):gmatch("%a+") do KEYWORDS[w] = true end

local function where(f)
  local i = C.fninfo(f)
  if i.what == "C" then return "[C]" end
  return i.short_src .. ":" .. i.line
end

local function before(a, b)
  local ta, tb = type(a), type(b)
  if ta ~= tb then return ta < tb end
  if ta == "number" or ta == "string" then return a < b end
  return tostring(a) < tostring(b)
end

-- The keys of t (pairs: a read-only view's too), the array part 1..n first; at most `cap`, and how many.
local function keys_of(t, cap)
  local n, len = 0, 0
  while len < cap and t[len + 1] ~= nil do len = len + 1 end
  local rest, count = {}, 0
  for k in pairs(t) do
    count = count + 1
    if not (mtype(k) == "integer" and k >= 1 and k <= len) and #rest < cap * 4 then rest[#rest + 1] = k end
  end
  sort(rest, before)
  local keys = {}
  for i = 1, len do keys[i] = i end
  for i = 1, #rest do keys[len + i] = rest[i] end
  return keys, len, count
end

local repr
local function put(out, s) out.len = out.len + #s out[#out + 1] = s end
local function walk(v, depth, seen, out)
  if out.len > LIMIT then return end
  local t = type(v)
  if t == "string" then put(out, (fmt("%q", v):gsub("\\\n", "\\n")))
  elseif t == "number" or t == "boolean" or t == "nil" then
    if v ~= v then put(out, "0/0")
    elseif v == 1/0 then put(out, "math.huge")
    elseif v == -1/0 then put(out, "-math.huge")
    else put(out, tostring(v)) end
  elseif t == "function" then put(out, "function<" .. where(v) .. ">")
  elseif t == "table" then
    if seen[v] then put(out, "<cycle>") return end
    if depth >= DEPTH then put(out, "{...}") return end
    seen[v] = true
    local keys, len, count = keys_of(v, ITEMS)
    put(out, "{")
    for i = 1, #keys do
      if i > ITEMS or out.len > LIMIT then put(out, ", ...") break end
      local k = keys[i]
      if i > 1 then put(out, ", ") end
      if i > len then
        if type(k) == "string" and k:match("^[%a_][%w_]*$") and not KEYWORDS[k] then put(out, k)
        else put(out, "[") walk(k, depth + 1, seen, out) put(out, "]") end
        put(out, " = ")
      end
      walk(v[k], depth + 1, seen, out)
    end
    if count > #keys and #keys <= ITEMS then put(out, ", ... (" .. (count - #keys) .. " more)") end
    put(out, "}")
    seen[v] = nil
  else put(out, tostring(v)) end
end
function repr(v, depth)
  local out = { len = 0 }
  walk(v, DEPTH - (depth or DEPTH), {}, out)
  local s = concat(out)
  if #s > LIMIT then s = s:sub(1, LIMIT) .. "..." end
  return s
end

-- v as plain data for JSON: functions, userdata and threads printed, cycles and depth cut.
local function plain(v, depth, seen)
  local t = type(v)
  if t == "table" then
    depth, seen = depth or 0, seen or {}
    if seen[v] then return "<cycle>" end
    if depth >= 16 then return "{...}" end
    seen[v] = true
    local out = {}
    for k, x in pairs(v) do
      if type(k) == "string" or mtype(k) == "integer" then out[k] = plain(x, depth + 1, seen) end
    end
    seen[v] = nil
    return out
  elseif t == "function" or t == "userdata" or t == "thread" then
    return repr(v)
  end
  return v
end

local function describe(v)
  local t = type(v)
  local d = { type = t, repr = repr(v, 2) }
  if t == "table" then
    local ok, len = pcall(function() return #v end)
    if ok then d.length = len end
    local mt = getmetatable(v)
    if mt ~= nil then d.metatable = type(mt) == "string" and mt or "table" end
    local keys, _, count = keys_of(v, 500)
    d.count = count
    d.keys = {}
    for i = 1, #keys do
      local k, x = keys[i], v[keys[i]]
      local e = { key = type(k) == "string" and k or repr(k), type = type(x), value = repr(x, 1) }
      if type(x) == "function" then e.where = where(x) end
      d.keys[i] = e
    end
  elseif t == "function" then
    d["function"] = C.fninfo(v)
    d.where = where(v)
  elseif t == "string" then
    d.length = #v
  elseif t == "number" then
    d.subtype = mtype(v)
  end
  return d
end

local function complete(env, text)
  local head, chain = text:match("^(.-)([%w_%.:]*)$")
  local path, sep, partial = chain:match("^(.-)([%.:]?)([%w_]*)$")
  local found, seen = {}, {}
  local function add(k)
    if type(k) == "string" and k:sub(1, #partial) == partial and k:match("^[%a_][%w_]*$") and not seen[k] then
      seen[k] = true
      found[#found + 1] = head .. path .. sep .. k
    end
  end
  if sep == "" then
    for k in next, env do add(k) end
    for k in pairs(G) do add(k) end
    for k in pairs(KEYWORDS) do add(k) end
  else
    local base = env
    for name in path:gmatch("[^%.:]+") do
      if type(base) ~= "table" then return {} end
      base = base[name]
    end
    if type(base) == "string" then base = string end
    if type(base) == "table" then for k in pairs(base) do add(k) end end
  end
  sort(found)
  return found
end

local function make_env(id)
  local env = {}
  local repl = {}
  repl.session = id
  repl.repr = repr
  repl.describe = describe
  repl.where = where
  repl.upvalues = C.upvalues
  repl.getupvalue = C.getupvalue
  repl.setupvalue = C.setupvalue
  repl.plugins = C.plugins
  repl.claims = function()
    local list = C.claims()
    for _, c in ipairs(list) do c.fn = nil end
    return list
  end
  repl.claim = function(kind)
    for _, c in ipairs(C.claims()) do if c.kind == kind and c.fn then return c.fn end end
  end
  repl.exports = function()
    local m, list = C.module(), {}
    if type(m) ~= "table" then return list end
    for k, x in pairs(m) do
      list[#list + 1] = { name = tostring(k), type = type(x), where = type(x) == "function" and where(x) or nil }
    end
    sort(list, function(a, b) return a.name < b.name end)
    return list
  end
  repl.queries = function()
    local list = {}
    local game = rawget(G, "rood") and G.rood.game
    if not game then return list end
    for name, f in pairs(game) do
      local ok, value = pcall(f)
      list[#list + 1] = { name = "rood.game." .. name .. "()", value = ok and value or nil,
                          error = not ok and tostring(value) or nil }
    end
    sort(list, function(a, b) return a.name < b.name end)
    return list
  end
  repl.globals = function()
    local list = {}
    for k, x in next, env do list[#list + 1] = { name = tostring(k), type = type(x), session = true } end
    for k, x in pairs(G) do
      if type(k) == "string" and rawget(env, k) == nil then list[#list + 1] = { name = k, type = type(x) } end
    end
    sort(list, function(a, b) return a.name < b.name end)
    return list
  end
  repl.complete = function(text) return complete(env, text) end
  repl.reload = C.reload
  rawset(env, "repl", repl)
  rawset(env, "M", C.module())
  return setmetatable(env, {
    __index = G,
    -- a name the sandbox has is the sandbox's (a plugin's global function redefined); a new one is the session's
    __newindex = function(t, k, v) if rawget(G, k) ~= nil then G[k] = v else rawset(t, k, v) end end,
    __repl = repl,
    __metatable = "repl session",
  })
end

return { make_env = make_env, repr = repr, describe = describe, plain = plain }
)LUA";

// The helpers table on top (loaded once per state).
void push_helpers(lua_State* L)
{
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &HELPERS_KEY) == LUA_TTABLE)
        return;
    lua_pop(L, 1);
    if (luaL_loadbufferx(L, HELPERS, sizeof HELPERS - 1, "=repl helpers", "t") != LUA_OK)
        lua_error(L);
    static const luaL_Reg prims[] = {
        { "fninfo", prim_fninfo },
        { "upvalues", prim_upvalues },
        { "getupvalue", prim_getupvalue },
        { "setupvalue", prim_setupvalue },
        { "module", prim_module },
        { "plugins", prim_plugins },
        { "claims", prim_claims },
        { "reload", prim_reload },
    };
    lua_createtable(L, 0, (int)(sizeof prims / sizeof *prims));
    for (const luaL_Reg& r : prims)
        set_cfunction(L, -1, r.name, r.func);
    lua_call(L, 1, 1);
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &HELPERS_KEY);
}

// The session's `repl` table on top (its environment's metatable keeps it: `repl = 1` cannot lose it).
void push_repl(lua_State* L, const Session& s)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, s.env);
    lua_getmetatable(L, -1);
    lua_getfield(L, -1, "__repl");
    lua_replace(L, -3);
    lua_pop(L, 1);
}

struct Copy {
    Value* out;
};
int copy_value(lua_State* L)
{
    Copy* c = (Copy*)lua_touserdata(L, 2);
    *c->out = Value::from_lua(L, 1);
    return 0;
}
// The value at idx as JSON ("null" when it is not plain data).
std::string json_of(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    Value v;
    Copy c{ &v };
    lua_pushcfunction(L, copy_value);
    lua_pushvalue(L, idx);
    lua_pushlightuserdata(L, &c);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        lua_pop(L, 1);
        return "null";
    }
    return to_json(v);
}
// The value at idx printed (repl.repr; a failure says so).
std::string repr_of(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    push_helpers(L);
    lua_getfield(L, -1, "repr");
    lua_pushvalue(L, idx);
    std::string s;
    if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_type(L, -1) == LUA_TSTRING)
        s = lua_tostring(L, -1);
    else
        s = std::string("<not printable: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?") + ">";
    lua_pop(L, 2);
    return s;
}

Session* find_session(const std::string& id)
{
    for (Session& s : sessions())
        if (s.id == id)
            return &s;
    return nullptr;
}

void drop_env(Session& s)
{
    Plugin* p = find(s.plugin);
    if (p && p->L && p->generation == s.generation && s.env != LUA_NOREF)
        p->pending_unrefs.push_back(s.env); /* freed at its next call */
    s.env = LUA_NOREF;
}

// The session's plugin, its environment made (again) if the plugin's load changed; *reset says why.
Plugin* prepare(Session& s, std::string* reset, std::string* error)
{
    Plugin* p = !s.plugin.empty() && s.plugin[0] == '@' ? console(s.plugin) : find(s.plugin);
    if (!p || p->state != PLUGIN_LOADED || !p->L) {
        *error = "no loaded plugin '" + s.plugin + "'" + (p ? " (" + std::string(state_name(p->state)) + ")" : "");
        return nullptr;
    }
    if (s.env != LUA_NOREF && s.generation == p->generation)
        return p;
    if (s.env != LUA_NOREF)
        *reset = s.plugin + " reloaded (load " + std::to_string(s.generation) + " -> " + std::to_string(p->generation)
            + "): the session's bindings are gone";
    s.env = LUA_NOREF;
    int ref = LUA_NOREF;
    if (!run(p, "repl session", BUDGET_TEST, [&](lua_State* L) {
            push_helpers(L);
            lua_getfield(L, -1, "make_env");
            lua_pushstring(L, s.id.c_str());
            lua_call(L, 1, 1);
            ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }, error))
        return nullptr;
    s.env = ref;
    s.generation = p->generation;
    return p;
}

Session& open_session(const std::string* id, const std::string& plugin)
{
    std::vector<Session>& list = sessions();
    if (list.size() >= MAX_SESSIONS) { /* the least recently used goes */
        auto oldest = std::min_element(list.begin(), list.end(),
            [](const Session& a, const Session& b) { return a.used < b.used; });
        drop_env(*oldest);
        list.erase(oldest);
    }
    Session s;
    s.id = id ? *id : "s" + std::to_string(next_session++);
    while (!id && find_session(s.id))
        s.id = "s" + std::to_string(next_session++);
    s.plugin = plugin;
    s.used = now_seconds();
    list.push_back(s);
    return list.back();
}

std::string output_since(unsigned long long since, const Plugin* p)
{
    std::string s = "[";
    size_t bytes = 0;
    bool first = true;
    for (const LogLine& line : log_lines()) {
        if (line.seq <= since)
            continue;
        bool own = line.plugin == p->id();
        if (own && line.level == LOG_ERROR && line.text.rfind("repl", 0) == 0)
            continue; /* the eval's own error: in "error" */
        std::string text = own ? line.text : line.plugin + ": " + line.text;
        if (bytes + text.size() > MAX_OUTPUT) {
            s += std::string(first ? "" : ",") + q("... (output cut)");
            break;
        }
        bytes += text.size();
        s += (first ? "" : ",") + q(text);
        first = false;
    }
    return s + "]";
}

// "message\nstack traceback:\n..." -> message, traceback
void split_traceback(const std::string& full, std::string* message, std::string* traceback)
{
    size_t at = full.find("\nstack traceback:");
    *message = full.substr(0, at);
    *traceback = at == std::string::npos ? "" : full.substr(at + 1);
}

// Compiles code in the session (an expression first, else statements) onto the stack, its
// _ENV the session's. False: a syntax error in *error, *incomplete when it ends early.
bool compile(lua_State* L, const Session& s, const std::string& code, bool expression_only, std::string* error,
    bool* incomplete)
{
    std::string name = "=repl:" + s.id;
    std::string expression = "return " + code;
    if (luaL_loadbufferx(L, expression.data(), expression.size(), name.c_str(), "t") != LUA_OK) {
        std::string as_expression = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (expression_only || luaL_loadbufferx(L, code.data(), code.size(), name.c_str(), "t") != LUA_OK) {
            *error = expression_only ? as_expression : lua_tostring(L, -1);
            if (!expression_only)
                lua_pop(L, 1);
            const char* eof = "<eof>";
            *incomplete = error->size() >= 5 && error->compare(error->size() - 5, 5, eof) == 0;
            return false;
        }
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, s.env);
    if (!lua_setupvalue(L, -2, 1))
        lua_pop(L, 1);
    return true;
}

std::string head(const Session& s, const Plugin* p, const std::string& reset)
{
    return "{\"session\":" + q(s.id) + ",\"plugin\":" + q(s.plugin) + ",\"generation\":" + std::to_string(p->generation)
        + (reset.empty() ? "" : ",\"reset\":" + q(reset));
}

std::string op_eval(Session& s, Plugin* p, const std::string& code, const std::string& reset)
{
    unsigned long long since = log_last_seq();
    std::vector<std::string> values, results;
    std::string syntax, failure;
    bool incomplete = false, compiled = false;
    double start = now_seconds();
    bool ok = run(p, "repl", BUDGET_TEST, [&](lua_State* L) {
        if (!compile(L, s, code, false, &syntax, &incomplete))
            return;
        compiled = true;
        int base = lua_gettop(L);
        lua_call(L, 0, LUA_MULTRET);
        int n = lua_gettop(L) - base + 1;
        for (int k = 0; k < n; ++k) {
            values.push_back(repr_of(L, base + k));
            results.push_back(json_of(L, base + k));
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, s.env);
        lua_pushliteral(L, "_");
        if (n)
            lua_pushvalue(L, base);
        else
            lua_pushnil(L);
        lua_rawset(L, -3); /* the session's own, whatever the sandbox has */
    }, &failure);
    double ms = (now_seconds() - start) * 1000.0;
    ++s.evals;
    std::string message, traceback;
    if (!ok) {
        split_traceback(failure, &message, &traceback);
        run(p, "repl", BUDGET_TEST, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, s.env);
            lua_pushliteral(L, "_e");
            lua_pushstring(L, message.c_str());
            lua_rawset(L, -3);
        });
    } else if (!compiled) {
        message = syntax;
        ok = false;
    }
    settle(); /* a reload asked for (repl.reload, a file saved) happens now */
    std::string r = head(s, p, reset) + ",\"ok\":" + (ok ? "true" : "false") + ",\"values\":[";
    for (size_t k = 0; k < values.size(); ++k)
        r += (k ? "," : "") + q(values[k]);
    r += "],\"results\":[";
    for (size_t k = 0; k < results.size(); ++k)
        r += (k ? "," : "") + results[k];
    r += "],\"output\":" + output_since(since, p) + ",\"ms\":" + num(ms);
    if (!ok)
        r += ",\"error\":" + q(message) + ",\"traceback\":" + q(traceback);
    if (incomplete)
        r += ",\"incomplete\":true";
    return r + "}";
}

// describe {code}: the expression's first value; inspect {what, arg}: repl.<what>(arg).
std::string op_call(Session& s, Plugin* p, const std::string& op, const std::string& what, const std::string* code,
    const std::string* arg, const std::string& reset)
{
    unsigned long long since = log_last_seq();
    std::string json, syntax, failure;
    bool incomplete = false, compiled = true;
    bool ok = run(p, "repl", BUDGET_TEST, [&](lua_State* L) {
        push_repl(L, s);
        int repl = lua_gettop(L);
        if (op == "describe") {
            lua_getfield(L, repl, "describe");
            if (!compile(L, s, *code, true, &syntax, &incomplete)) {
                compiled = false;
                return;
            }
            lua_call(L, 0, 1);
        } else {
            if (lua_getfield(L, repl, what.c_str()) != LUA_TFUNCTION || what == "repr" || what == "session")
                luaL_error(L, "repl: no inspection '%s' (plugins, claims, exports, queries, globals, upvalues, "
                              "complete, describe)", what.c_str());
            if (what == "upvalues" || what == "describe") { /* their argument is an expression */
                if (!arg || !compile(L, s, *arg, true, &syntax, &incomplete)) {
                    if (!arg)
                        syntax = "repl: " + what + " needs arg (an expression)";
                    compiled = false;
                    return;
                }
                lua_call(L, 0, 1);
            } else if (arg)
                lua_pushstring(L, arg->c_str());
            else
                lua_pushnil(L);
        }
        lua_call(L, 1, 1);
        push_helpers(L);
        lua_getfield(L, -1, "plain");
        lua_pushvalue(L, -3);
        lua_call(L, 1, 1);
        json = json_of(L, -1);
    }, &failure);
    settle();
    std::string r = head(s, p, reset);
    if (ok && compiled)
        return r + ",\"ok\":true,\"result\":" + json + ",\"output\":" + output_since(since, p) + "}";
    std::string message, traceback;
    if (!compiled)
        message = syntax;
    else
        split_traceback(failure, &message, &traceback);
    return r + ",\"ok\":false,\"error\":" + q(message) + ",\"traceback\":" + q(traceback)
        + (incomplete ? ",\"incomplete\":true" : "") + ",\"output\":" + output_since(since, p) + "}";
}

std::string sessions_json()
{
    std::string r = "{\"sessions\":[";
    double now = now_seconds();
    bool first = true;
    for (const Session& s : sessions()) {
        Plugin* p = find(s.plugin);
        bool live = p && p->state == PLUGIN_LOADED && p->generation == s.generation && s.env != LUA_NOREF;
        r += std::string(first ? "" : ",") + "{\"session\":" + q(s.id) + ",\"plugin\":" + q(s.plugin)
            + ",\"generation\":" + std::to_string(s.generation) + ",\"current\":" + (live ? "true" : "false")
            + ",\"evals\":" + std::to_string(s.evals) + ",\"idle_s\":" + num(now - s.used) + "}";
        first = false;
    }
    return r + "]}";
}
} // namespace

bool repl_rpc(const Json& params, std::string* reply, std::string* error)
{
    if (!dev_mode()) {
        *error = "repl: dev mode only (ROOD_RPC set or ROOD_DEV=1)";
        return false;
    }
    const std::string* o = text_param(params, "op");
    std::string op = o ? *o : "eval";
    const std::string* id = text_param(params, "session");
    const std::string* plugin = text_param(params, "plugin");
    if (op == "sessions") {
        *reply = sessions_json();
        return true;
    }
    if (op == "close") {
        Session* s = id ? find_session(*id) : nullptr;
        if (!s) {
            *error = "repl: no session '" + (id ? *id : std::string()) + "'";
            return false;
        }
        drop_env(*s);
        sessions().erase(sessions().begin() + (s - sessions().data()));
        *reply = sessions_json();
        return true;
    }
    if (op != "eval" && op != "describe" && op != "inspect") {
        *error = "repl: unknown op '" + op + "' (eval, describe, inspect, sessions, close)";
        return false;
    }
    if (id && !id_ok(*id)) {
        *error = "repl: a session id is 1-32 letters, digits, '_', '-' or '.'";
        return false;
    }
    Session* s = id ? find_session(*id) : nullptr;
    if (s && plugin && *plugin != s->plugin) {
        *error = "repl: session '" + s->id + "' evaluates in " + s->plugin + ", not " + *plugin
            + " (a new session id for another sandbox)";
        return false;
    }
    std::string target = plugin ? *plugin : s ? s->plugin : "@console";
    if (!s) {
        Plugin* p = !target.empty() && target[0] == '@' ? nullptr : find(target);
        if (target.empty() || (target[0] != '@' && (!p || p->state != PLUGIN_LOADED))) {
            *error = "repl: no loaded plugin '" + target + "'";
            return false;
        }
        s = &open_session(id, target);
    }
    s->used = now_seconds();
    std::string reset;
    Plugin* p = prepare(*s, &reset, error);
    if (!p) {
        *error = "repl: " + *error;
        return false;
    }
    if (op == "eval") {
        const std::string* code = text_param(params, "code");
        if (!code) {
            *error = "repl: eval needs code";
            return false;
        }
        *reply = op_eval(*s, p, *code, reset);
        return true;
    }
    const std::string* code = text_param(params, "code");
    const std::string* what = text_param(params, "what");
    if (op == "describe" && !code) {
        *error = "repl: describe needs code (an expression)";
        return false;
    }
    if (op == "inspect" && !what) {
        *error = "repl: inspect needs what (plugins, claims, exports, queries, globals, upvalues, complete)";
        return false;
    }
    *reply = op_call(*s, p, op, what ? *what : "", code, text_param(params, "arg"), reset);
    return true;
}
} // namespace rl
