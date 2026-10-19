// The extension system without the game (pc/src/lua; pc/docs/Extensions.md):
// Lua compiled as C++, the allocator's cap, the budgets, the sandbox, values
// between plugins, events, timers, the registry, plugins on disk, storage,
// capabilities, hot reload, the test harness, the RPC and the HUD layer
// (claims, the draw API, rood.dev; pc/src/hud). ctest lua_probe
// (argv[1]: a scratch directory the probe owns).
#include "lua/lua_runtime.h"
#include "lua/rl_internal.h"
#include "lua/sha256.h"
#include "hud/hud_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <vector>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string scratch;
static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
/* luaL_dostring with its status (the macro's || gives only 0 or 1). */
static int dostring(lua_State* L, const char* code)
{
    int status = luaL_loadstring(L, code);
    return status == LUA_OK ? lua_pcall(L, 0, LUA_MULTRET, 0) : status;
}

/* Lua is compiled as C++: an error unwinds to lua_pcall as an exception; the
 * pattern matcher calls rood_lua_pattern_step (rood_luauser.h). */
static void lua_core()
{
    rl::Plugin p;
    p.manifest.id = "@probe-core";
    lua_State* L = rl::new_state(&p);
    CHECK(L != nullptr);
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_settop(L, 0);
    CHECK(dostring(L, "error('boom')") == LUA_ERRRUN && contains(lua_tostring(L, -1), "boom"));
    lua_settop(L, 0);
    unsigned long long before = rl::pattern_steps;
    CHECK(dostring(L, "return string.find(string.rep('a', 50), 'a-b')") == LUA_OK);
    CHECK(rl::pattern_steps > before);
    lua_close(L);
}

/* Each plugin's allocator counts its memory and refuses past the cap: Lua
 * raises "not enough memory", the state stays usable, and closing it gives
 * every byte back. */
static void allocator_cap()
{
    rl::Plugin p;
    p.manifest.id = "@probe-memory";
    p.mem.cap = 2u << 20;
    lua_State* L = rl::new_state(&p);
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_settop(L, 0);
    CHECK(p.mem.used > 0 && p.mem.used < p.mem.cap);
    CHECK(dostring(L, "local t = {} for i = 1, 1e7 do t[i] = ('x'):rep(64) .. i end") == LUA_ERRMEM);
    lua_settop(L, 0);
    CHECK(p.mem.refusals > 0);
    CHECK(p.mem.peak <= p.mem.cap);
    lua_gc(L, LUA_GCCOLLECT);
    CHECK(p.mem.used < p.mem.cap / 2);
    CHECK(dostring(L, "return 1 + 1") == LUA_OK && lua_tointeger(L, -1) == 2);
    lua_close(L);
    CHECK(p.mem.used == 0);
}

static int budget_run(rl::Plugin& p, const char* code, double* ms)
{
    rl::begin_budget(&p, rl::BUDGET_CALL);
    int status = dostring(p.L, code);
    *ms = rl::end_budget(&p);
    return status;
}

/* A runaway script is stopped by its instruction budget or its wall clock,
 * pcall and coroutines cannot keep it alive, a string pattern (C code) is
 * stopped too, and the next call runs normally. */
static void budgets()
{
    rl::Plugin p;
    p.manifest.id = "@probe-budget";
    p.L = rl::new_state(&p);
    luaL_requiref(p.L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(p.L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(p.L, LUA_COLIBNAME, luaopen_coroutine, 1);
    luaL_requiref(p.L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(p.L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_settop(p.L, 0);
    double ms = 0;
    rl::config.budget_instructions = 200000;
    rl::config.budget_ms = 2000;
    CHECK(budget_run(p, "while true do end", &ms) == LUA_ERRRUN);
    CHECK(contains(lua_tostring(p.L, -1), "budget exceeded"));
    CHECK(p.budget.instructions >= 200000 && p.budget.instructions < 200000 + 2 * RL_HOOK_STEP);
    lua_settop(p.L, 0);
    CHECK(budget_run(p, "while true do pcall(function() while true do end end) end", &ms) == LUA_ERRRUN);
    lua_settop(p.L, 0);
    CHECK(budget_run(p, "local co = coroutine.wrap(function() while true do end end) co()", &ms) == LUA_ERRRUN);
    lua_settop(p.L, 0);
    /* killed in a coroutine, the error caught by resume: the resumer stops at once too */
    CHECK(budget_run(p, "local co = coroutine.create(function() while true do end end) coroutine.resume(co) "
                        "local n = 0 for i = 1, 100 do n = n + 1 end return n", &ms) == LUA_ERRRUN);
    lua_settop(p.L, 0);
    rl::config.budget_instructions = 1LL << 50;
    rl::config.budget_ms = 30;
    CHECK(budget_run(p, "while true do end", &ms) == LUA_ERRRUN && ms >= 29 && ms < 500);
    lua_settop(p.L, 0);
    CHECK(budget_run(p, "return string.find(string.rep('a', 4000), '.-.-.-.-.-.-.-b')", &ms) == LUA_ERRRUN);
    CHECK(contains(lua_tostring(p.L, -1), "string pattern") && ms < 500);
    lua_settop(p.L, 0);
    /* Other C loops a plugin can make long (each ran for hours or forever before
     * the budget reached them): repeating nothing, a plain find with a long
     * near-match, moving, inserting or removing across a huge range, sorting
     * many copies of a long string. */
    CHECK(budget_run(p, "return #string.rep('', math.maxinteger) + #string.rep('', 1 << 40, '')", &ms) == LUA_OK);
    CHECK(lua_tointeger(p.L, -1) == 0 && ms < 100);
    lua_settop(p.L, 0);
    static const char* const loops[] = {
        "local s = string.rep('a', 1 << 22) return s:find(string.rep('a', 1 << 21) .. 'b', 1, true)",
        "table.move({}, 1, 1 << 40, 2)",
        "table.move({}, 1, 1 << 40, 0)",
        "table.insert(setmetatable({}, { __len = function() return 1 << 40 end }), 1, 'x')",
        "table.remove(setmetatable({}, { __len = function() return 1 << 40 end }), 1)",
        "local s = string.rep('a', 1 << 22) local t = {} for i = 1, 20000 do t[i] = s end table.sort(t)",
    };
    for (const char* code : loops) {
        int status = budget_run(p, code, &ms);
        CHECK(status == LUA_ERRRUN && contains(lua_tostring(p.L, -1), "budget exceeded") && ms < 1000);
        if (status != LUA_ERRRUN || ms >= 1000)
            std::printf("  (%s: %.0f ms)\n", code, ms);
        lua_settop(p.L, 0);
    }
    rl::config = rl::Config();
    CHECK(budget_run(p, "local n = 0 for i = 1, 1000 do n = n + i end return n", &ms) == LUA_OK);
    CHECK(lua_tointeger(p.L, -1) == 500500);
    lua_close(p.L);
}

static std::string eval_json(rl::Plugin* p, const char* code)
{
    if (!p)
        return "no plugin";
    std::vector<rl::Value> results;
    std::string error;
    if (!rl::eval(p, code, &results, &error))
        return "error: " + error;
    std::string s;
    for (size_t k = 0; k < results.size(); k++)
        s += (k ? "," : "") + rl::to_json(results[k]);
    return s;
}
static std::string eval_error(rl::Plugin* p, const char* code)
{
    std::string error;
    if (!p || rl::eval(p, code, nullptr, &error))
        return "";
    return error;
}
/* Runs code inside rl::run, errors propagating (unlike luaL_dostring). */
static void lua_do(lua_State* L, const char* code)
{
    if (luaL_loadstring(L, code) != LUA_OK)
        lua_error(L);
    lua_call(L, 0, LUA_MULTRET);
}

/* Protected calls with tracebacks; plain data is copied between plugin
 * states; a function stays in its plugin and is called there through a
 * proxy; only the game thread may call in. */
static void values_and_calls()
{
    rl::Plugin* a = rl::console("@probe-a");
    rl::Plugin* b = rl::console("@probe-b");
    CHECK(a->state == rl::PLUGIN_LOADED && b->state == rl::PLUGIN_LOADED && a->console);
    CHECK(eval_json(a, "1 + 2, 'x', nil, true") == "3,\"x\",null,true");
    CHECK(eval_json(a, "{10, 20, 30}") == "[10,20,30]");
    CHECK(eval_json(a, "local t = {1, 2, n = {k = 'v'}} return t") == "{\"1\":1,\"2\":2,\"n\":{\"k\":\"v\"}}");
    CHECK(eval_json(a, "x = 5") == "");
    std::string e = eval_error(a, "local function f() error('deep') end f()");
    CHECK(contains(e, "deep") && contains(e, "stack traceback"));
    CHECK(contains(rl::log_lines().back().text, "deep") && rl::log_lines().back().level == rl::LOG_ERROR);
    for (int k = 0; k < 10; k++)
        eval_error(a, "error('again')");
    CHECK(a->state == rl::PLUGIN_LOADED); /* a console sandbox is never disabled */
    std::string error;
    rl::Value v;
    bool ok = rl::run(a, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        lua_do(L, "local t = {} t.self = t return t");
        v = rl::Value::from_lua(L, -1);
    }, &error);
    CHECK(!ok && contains(error, "contains itself"));
    ok = rl::run(a, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        lua_do(L, "return coroutine.create(print)");
        v = rl::Value::from_lua(L, -1);
    }, &error);
    CHECK(!ok && contains(error, "cannot pass a thread"));
    rl::Value fn;
    CHECK(rl::run(a, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        lua_do(L, "secret = 'only a' return function(x) return x * 2, secret end");
        fn = rl::Value::from_lua(L, -1);
    }));
    CHECK(fn.kind == rl::Value::FUNCTION && fn.fn->owner == "@probe-a");
    CHECK(rl::run(b, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        rl::push(L, fn);
        lua_setglobal(L, "from_a");
    }));
    CHECK(eval_json(b, "type(from_a), from_a(21)") == "\"function\",42,\"only a\"");
    CHECK(eval_json(b, "secret") == "null");
    std::vector<rl::Value> results;
    CHECK(rl::call_function(fn.fn, { rl::Value::integer(5) }, &results, &error));
    CHECK(results.size() == 2 && results[0].kind == rl::Value::INTEGER && results[0].i == 10);
    /* a table passed on is the receiver's own */
    rl::Value t = rl::Value::new_table();
    t.set("n", rl::Value::integer(1));
    CHECK(rl::run(b, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        rl::push(L, t);
        lua_setglobal(L, "copy");
    }));
    CHECK(eval_json(b, "copy.n = 2 return copy.n") == "2" && t.get("n")->i == 1);
    /* only the game thread may call in */
    std::thread other([&] { ok = rl::eval(a, "1", nullptr, &error); });
    other.join();
    CHECK(!ok && contains(error, "game thread"));
    CHECK(a->stats.calls > 0 && a->stats.faults >= 11);
}

/* What reaches outside the sandbox is gone; load takes text only; require
 * loads the plugin's own files; print goes to the log. */
static void sandbox()
{
    rl::Plugin* s = rl::console("@probe-sandbox");
    CHECK(eval_json(s, "io, os, debug, package, dofile, loadfile, warn, string.dump")
        == "null,null,null,null,null,null,null,null");
    CHECK(contains(eval_error(s, "load(function() return nil end)"), "text chunks"));
    CHECK(contains(eval_json(s, "select(2, load('\\27LuaT\\0', '=bin'))"), "binary chunk"));
    CHECK(contains(eval_json(s, "select(2, load('return 1', '=c', 'b'))"), "text chunks"));
    CHECK(eval_json(s, "load('return x', '=c', 't', {x = 5})()") == "5");
    CHECK(eval_json(s, "getmetatable('')") == "\"locked\"");
    CHECK(contains(eval_error(s, "collectgarbage('stop')"), "not allowed"));
    CHECK(contains(eval_json(s, "collectgarbage('count') > 0"), "true"));
    /* a handler that is a C function catching the kill (pcall) returns without a Lua
     * instruction after it: still a runaway, a fault */
    std::string error;
    unsigned long long faults = s->stats.faults;
    CHECK(!rl::run(s, "probe", rl::BUDGET_CALL, [](lua_State* L) {
        lua_getglobal(L, "pcall");
        lua_do(L, "return function() while true do end end");
        lua_call(L, 1, 0);
    }, &error) && contains(error, "budget exceeded") && s->stats.faults == faults + 1);
    CHECK(contains(eval_error(s, "require('os')"), "no folder"));
    CHECK(contains(eval_error(s, "require('../x')"), "bad module name"));
    /* no finalizers: they would run with the budget's hook off (an endless one hung the game) */
    CHECK(contains(eval_error(s, "setmetatable({}, { __gc = function() while true do end end }) collectgarbage()"),
        "__gc is not available"));
    CHECK(contains(eval_error(s, "setmetatable({}, { __gc = true })"), "__gc is not available"));
    CHECK(eval_json(s, "local mt = {} local t = setmetatable({}, mt) mt.__gc = function() ran = true end "
                       "t = nil collectgarbage() collectgarbage() return ran")
        == "null");
    CHECK(contains(eval_error(s, "setmetatable(rood, {})"), "protected"));
    CHECK(eval_json(s, "print('hello from print')") == "");
    CHECK(rl::log_lines().back().text == "hello from print" && rl::log_lines().back().plugin == "@probe-sandbox");
}

/* rood: log levels, version, api_version, the plugin's own manifest; read-only. */
static void core_api()
{
    rl::Plugin* c = rl::console("@probe-api");
    CHECK(eval_json(c, "rood.version, rood.api_version, rood.plugin.id, rood.plugin.elevated")
        == "\"0.1.0\",1,\"@probe-api\",false");
    CHECK(eval_json(c, "rood.warn('careful', 1)") == "");
    CHECK(rl::log_lines().back().level == rl::LOG_WARN && rl::log_lines().back().text == "careful 1");
    CHECK(eval_json(c, "rood.error('bad')") == "" && rl::log_lines().back().level == rl::LOG_ERROR);
    CHECK(contains(eval_error(c, "rood.log = nil"), "read-only"));
    CHECK(contains(eval_error(c, "rood.plugin.id = 'x'"), "read-only"));
    CHECK(eval_json(c, "getmetatable(rood)") == "\"locked\"");
    CHECK(eval_json(c, "local n = 0 for k in pairs(rood) do n = n + 1 end return n >= 6") == "true");
}

/* rood.on/off/emit: each plugin gets its own copy of a payload; a handler's
 * error is its own plugin's fault, with a traceback; plugins emit only
 * namespaced events; the core's tick reaches every plugin. */
static void events()
{
    rl::Plugin* a = rl::console("@probe-ev-a");
    rl::Plugin* b = rl::console("@probe-ev-b");
    CHECK(eval_json(b, "got = {} from = {} h = rood.on('probe/ping', function(v, sender) got[#got + 1] = v "
                       "from[#from + 1] = sender end) return h > 0") == "true");
    CHECK(eval_json(a, "payload = { n = 1, list = { 1, 2 } } return rood.emit('probe/ping', payload)") == "1");
    CHECK(eval_json(b, "got[1].n, got[1].list[2]") == "1,2");
    CHECK(eval_json(b, "from[1]") == "\"@probe-ev-a\""); /* who emitted it: another plugin's namespace can be used */
    CHECK(eval_json(b, "got[1].list[1] = 99 return got[1].list[1]") == "99");
    CHECK(eval_json(a, "payload.list[1]") == "1");
    CHECK(contains(eval_error(a, "rood.emit('tick', {})"), "namespaced"));
    CHECK(contains(eval_error(a, "rood.on('bad name!', print)"), "bad event name"));
    CHECK(eval_json(b, "rood.on('probe/boom', function() error('handler failed') end) return true") == "true");
    unsigned long long faults = b->stats.faults;
    CHECK(eval_json(a, "rood.emit('probe/boom')") == "1");
    CHECK(b->stats.faults == faults + 1);
    CHECK(contains(rl::log_lines().back().text, "handler failed") && contains(rl::log_lines().back().text, "stack traceback"));
    CHECK(eval_json(b, "rood.off(h), rood.off(h)") == "true,false");
    CHECK(eval_json(a, "rood.emit('probe/ping', 2)") == "0");
    CHECK(eval_json(a, "ticks = 0 rood.on('tick', function(t) ticks = ticks + 1 last = t.vsync end) return true") == "true");
    rl::tick_events(41);
    rl::tick_events(42);
    CHECK(eval_json(a, "ticks, last") == "2,42");
    CHECK(eval_json(a, "s = 1 hs = rood.on('tick', function(t, sender) s = sender end) return hs > 0") == "true");
    rl::tick_events(43);
    CHECK(eval_json(a, "s, rood.off(hs)") == "null,true"); /* the core's events have no sender */
    CHECK(rl::handler_count("@probe-ev-a") == 1 && rl::handler_count("@probe-ev-b") == 1);
}

/* rood.timer: game time advances with ticks (and stops when the game is
 * paused), real time with frames; every / cancel; bad arguments refused. */
static void timers()
{
    rl::Plugin* t = rl::console("@probe-timers");
    CHECK(eval_json(t, "fired = 0 rood.timer.after(0.05, function() fired = fired + 1 end) return fired") == "0");
    rl::tick_events(1);
    rl::tick_events(2);
    CHECK(eval_json(t, "fired") == "0");
    rl::tick_events(3);
    CHECK(eval_json(t, "fired") == "1");
    rl::tick_events(4);
    CHECK(eval_json(t, "fired") == "1");
    CHECK(eval_json(t, "n = 0 id = rood.timer.every(1 / 59.94, function() n = n + 1 end) return true") == "true");
    for (int k = 0; k < 3; k++)
        rl::tick_events(10 + k);
    CHECK(eval_json(t, "n >= 2, rood.timer.cancel(id), rood.timer.cancel(id)") == "true,true,false");
    CHECK(eval_json(t, "before = n return true") == "true");
    for (int k = 0; k < 3; k++)
        rl::tick_events(20 + k);
    CHECK(eval_json(t, "n == before") == "true");
    CHECK(eval_json(t, "real = 0 rood.timer.after(0, function() real = real + 1 end, { clock = 'real' }) return real")
        == "0");
    rl::frame_events(1.0 / 60);
    CHECK(eval_json(t, "real") == "1");
    CHECK(contains(eval_error(t, "rood.timer.after(-1, print)"), "seconds"));
    CHECK(contains(eval_error(t, "rood.timer.after(1, print, { clock = 'wall' })"), "clock"));
    CHECK(eval_json(t, "rood.clock.game() > 0, rood.clock.real() >= 0") == "true,true");
    CHECK(rl::timer_count("@probe-timers") == 0);
}

/* rood.registry: open kinds, ids in the owner's namespace, order by
 * after / before / group:<name>, entries as copies with callable functions,
 * only the owner removes; a cycle falls back to registration order. */
static void registry()
{
    rl::Plugin* r = rl::console("@probe-reg");
    rl::Plugin* o = rl::console("@probe-other");
    CHECK(eval_json(r,
        "local R = rood.registry "
        "R.register('probe.thing', '@probe-reg/b', { v = 'b' }, { after = '@probe-reg/a' }) "
        "R.register('probe.thing', '@probe-reg/a', { v = 'a', fn = function(x) return x + 1 end }) "
        "R.register('probe.thing', '@probe-reg/c', { v = 'c' }, { before = '@probe-reg/a', group = 'early' }) "
        "R.register('probe.thing', '@probe-reg/d', { v = 'd' }, { after = 'group:early', before = '@probe-reg/a' }) "
        "return true") == "true");
    CHECK(eval_json(o, "local ids = {} for _, e in ipairs(rood.registry.list('probe.thing')) do ids[#ids + 1] = e.id end "
                       "return table.concat(ids, ' ')")
        == "\"@probe-reg/c @probe-reg/d @probe-reg/a @probe-reg/b\"");
    CHECK(eval_json(o, "#rood.registry.list('probe.thing', { group = 'early' })") == "1");
    CHECK(eval_json(o, "local e = rood.registry.get('probe.thing', '@probe-reg/a') return e.owner, e.data.v, e.data.fn(41)")
        == "\"@probe-reg\",\"a\",42");
    CHECK(eval_json(o, "rood.registry.get('probe.thing', '@probe-reg/zz')") == "null");
    CHECK(contains(eval_error(o, "rood.registry.register('probe.thing', '@probe-reg/x', {})"), "namespace"));
    CHECK(contains(eval_error(o, "rood.registry.remove('probe.thing', '@probe-reg/a')"), "belongs"));
    CHECK(contains(eval_error(r, "rood.registry.register('probe.thing', '@probe-reg/a', {})"), "already"));
    CHECK(contains(eval_error(r, "rood.registry.register('Bad Kind', '@probe-reg/z', {})"), "bad kind"));
    CHECK(eval_json(r, "rood.registry.register('probe.loop', '@probe-reg/x', {}, { after = '@probe-reg/y' }) "
                       "rood.registry.register('probe.loop', '@probe-reg/y', {}, { after = '@probe-reg/x' }) "
                       "return #rood.registry.list('probe.loop')") == "2");
    CHECK(rl::registry_count("@probe-reg") == 6);
    CHECK(eval_json(r, "rood.registry.remove('probe.loop', '@probe-reg/y'), rood.registry.kinds()['probe.thing']")
        == "true,4");
    rl::drop_registry("@probe-reg");
    CHECK(eval_json(o, "#rood.registry.list('probe.thing')") == "0" && rl::registry_count("@probe-reg") == 0);
    /* bounded: entries per plugin, ids per relation; a listing of thousands related to
     * their whole group stays fast (it was quadratic in entries and relations) */
    rl::Plugin* big = rl::console("@probe-reg-big");
    rl::config.load_budget_ms = 10000;
    CHECK(eval_json(big, "for i = 1, 4096 do rood.registry.register('probe.many', '@probe-reg-big/e' .. i, i, "
                         "{ group = 'g', after = 'group:g' }) end return true") == "true");
    rl::config.load_budget_ms = 250;
    CHECK(contains(eval_error(big, "rood.registry.register('probe.many', '@probe-reg-big/more', 1)"), "more than 4096"));
    std::string refs = "local t = {} for i = 1, 33 do t[i] = 'x' .. i end "
                       "rood.registry.register('probe.refs', '@probe-reg/r', 1, { after = t })";
    CHECK(contains(eval_error(r, refs.c_str()), "more than 32 ids"));
    double start = rl::now_seconds();
    CHECK(rl::registry_list("probe.many").size() == 4096);
    CHECK(rl::now_seconds() - start < 1.0);
    rl::drop_registry("@probe-reg-big");
}

static void files_and_hash()
{
    CHECK(rl::sha256_hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(rl::sha256_hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string million(1000000, 'a');
    CHECK(rl::sha256_hex(million.data(), million.size())
        == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::string why;
    CHECK(rl::safe_relative("data/a-b_c.txt", &why));
    CHECK(!rl::safe_relative("../x", &why) && contains(why, ".."));
    CHECK(!rl::safe_relative("/etc/passwd", &why));
    CHECK(!rl::safe_relative("a//b", &why));
    CHECK(!rl::safe_relative("C:/x", &why));
    CHECK(!rl::safe_relative("a\\b", &why));
    CHECK(!rl::safe_relative("con", &why) && !rl::safe_relative("data/COM1.txt", &why) && !rl::safe_relative("Nul.lua", &why));
    CHECK(!rl::safe_relative("data/a.", &why) && rl::safe_relative("console.txt", &why) && rl::safe_relative("com10", &why));
    std::string dir = scratch + "/files";
    CHECK(rl::write_file(dir + "/deep/er/note.txt", "hello", &why));
    std::string text;
    CHECK(rl::read_file(dir + "/deep/er/note.txt", &text, 100, &why) && text == "hello");
    CHECK(!rl::read_file(dir + "/deep/er/note.txt", &text, 3, &why) && contains(why, "larger"));
    CHECK((rl::list_dir(dir + "/deep") == std::vector<std::string>{ "er" }));
    long long mtime = 0, size = 0;
    bool is_dir = true;
    CHECK(rl::file_stat(dir + "/deep/er/note.txt", &mtime, &size, &is_dir) && size == 5 && !is_dir && mtime > 0);
}

static void write_text(const std::string& path, const std::string& text)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
static std::string state_of(const char* id)
{
    rl::Plugin* p = rl::find(id);
    return p ? std::string(rl::state_name(p->state)) : std::string("missing");
}
static std::string settings_text()
{
    FILE* f = std::tmpfile();
    pc_lua_settings_write(f);
    std::rewind(f);
    char buffer[8192] = {};
    size_t n = std::fread(buffer, 1, sizeof buffer - 1, f);
    std::fclose(f);
    return std::string(buffer, n);
}

/* Plugins from folders: manifests (Lua or JSON) checked, dependencies
 * first, a plugin that faults on every tick disabled at the next safe point
 * while the others go on, enable/disable and order through settings lines. */
static void plugins_on_disk()
{
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.alpha/plugin.lua",
        "return { id = 'probe.alpha', name = 'Alpha', version = '1.2.0', api_version = 1 }\n");
    write_text(root + "/probe.alpha/main.lua",
        "ticks = 0\n"
        "rood.on('tick', function() ticks = ticks + 1 end)\n"
        "rood.registry.register('probe.entry', 'probe.alpha/one', { version = 1 })\n");
    write_text(root + "/probe.beta/plugin.json",
        "{ \"id\": \"probe.beta\", \"version\": \"0.1.0\", \"api_version\": 1,\n"
        "  \"depends\": [ { \"id\": \"probe.alpha\", \"version\": \">=1.0.0\" } ] }\n");
    write_text(root + "/probe.beta/main.lua",
        "rood.registry.register('probe.entry', 'probe.beta/two', {}, { after = 'probe.alpha/one' })\n");
    write_text(root + "/probe.misnamed/plugin.lua", "return { id = 'probe.other', api_version = 1 }\n");
    write_text(root + "/probe.misnamed/main.lua", "\n");
    write_text(root + "/probe.future/plugin.lua", "return { id = 'probe.future', api_version = 99 }\n");
    write_text(root + "/probe.future/main.lua", "\n");
    write_text(root + "/probe.faulty/plugin.lua", "return { id = 'probe.faulty', api_version = 1 }\n");
    write_text(root + "/probe.faulty/main.lua", "rood.on('tick', function() error('every tick') end)\n");
    write_text(root + "/probe.broken/plugin.lua", "return { id = 'probe.broken', api_version = 1 }\n");
    write_text(root + "/probe.broken/main.lua", "this is not lua\n");
    write_text(root + "/probe.off/plugin.lua", "return { id = 'probe.off', api_version = 1, default_enabled = false }\n");
    write_text(root + "/probe.off/main.lua", "\n");
    write_text(root + "/rood.reserved/plugin.lua", "return { id = 'rood.reserved', api_version = 1 }\n");
    write_text(root + "/rood.reserved/main.lua", "\n");
    pc_lua_settings_parse("plugin_order", "probe.beta,probe.alpha\n");
    pc_lua_init();
    CHECK(state_of("probe.alpha") == "loaded" && state_of("probe.beta") == "loaded");
    CHECK(state_of("probe.off") == "off");
    CHECK(state_of("probe.misnamed") == "failed" && contains(rl::find("probe.misnamed")->status, "named after its id"));
    CHECK(state_of("probe.future") == "failed" && contains(rl::find("probe.future")->status, "api_version"));
    CHECK(state_of("probe.broken") == "failed" && contains(rl::find("probe.broken")->status, "main chunk"));
    CHECK(state_of("rood.reserved") == "failed" && contains(rl::find("rood.reserved")->status, "reserved"));
    std::vector<std::string> ids;
    for (rl::Plugin* p : rl::load_order())
        ids.push_back(p->id());
    CHECK(std::find(ids.begin(), ids.end(), "probe.alpha") < std::find(ids.begin(), ids.end(), "probe.beta"));
    CHECK(rl::find("probe.alpha")->data_dir == scratch + "/data/probe.alpha");
    CHECK(eval_json(rl::find("probe.alpha"), "rood.plugin.name, rood.plugin.version, rood.plugin.shipped")
        == "\"Alpha\",\"1.2.0\",false");
    for (int k = 0; k < 6; k++)
        pc_lua_tick();
    CHECK(state_of("probe.faulty") == "disabled" && contains(rl::find("probe.faulty")->status, "faults within"));
    CHECK(eval_json(rl::find("probe.alpha"), "ticks") == "6");
    pc_lua_settings_parse("plugin_enabled", "probe.alpha 0\n");
    pc_lua_settings_end();
    pc_lua_tick();
    CHECK(state_of("probe.alpha") == "off" && state_of("probe.beta") == "failed");
    CHECK(rl::registry_list("probe.entry").empty());
    pc_lua_settings_parse("plugin_enabled", "probe.alpha 1\n");
    pc_lua_settings_end();
    pc_lua_tick();
    CHECK(state_of("probe.alpha") == "loaded" && state_of("probe.beta") == "loaded");
    std::string written = settings_text();
    CHECK(contains(written, "plugin_budget_ms 4\n") && contains(written, "plugin_enabled probe.alpha 1\n"));
    CHECK(contains(written, "plugin_order probe.beta,probe.alpha\n") && contains(written, "plugins_elevated 0\n"));
}

/* rood.storage: plain data kept as JSON in the plugin's data folder (kept
 * across a reload); its own folder read-only; data files in the data folder. */
static void storage()
{
    rl::Plugin* a = rl::find("probe.alpha");
    CHECK(eval_json(a, "rood.storage.set('answer', { n = 42, list = { 1, 2 } }) return rood.storage.get('answer').n") == "42");
    CHECK(eval_json(a, "rood.storage.get('missing', 'fallback')") == "\"fallback\"");
    CHECK(contains(eval_error(a, "rood.storage.set('f', print)"), "function"));
    CHECK(eval_json(a, "rood.storage.flush()") == "true");
    std::string text, why;
    CHECK(rl::read_file(scratch + "/data/probe.alpha/store.json", &text, 4096, &why));
    CHECK(contains(text, "\"n\":42") && contains(text, "\"list\":[1,2]"));
    CHECK(contains(eval_json(a, "rood.storage.read('main.lua')"), "rood.on('tick'"));
    CHECK(contains(eval_error(a, "rood.storage.read('../probe.beta/main.lua')"), "bad path"));
    CHECK(eval_json(a, "rood.storage.write_data('notes/today.txt', 'x') return rood.storage.read_data('notes/today.txt')")
        == "\"x\"");
    CHECK(contains(eval_error(a, "rood.storage.write_data('store.json', '{}')"), "reserved"));
    CHECK(eval_json(a, "rood.storage.keys()[1], #rood.storage.list_data()") == "\"answer\",1");
    CHECK(eval_json(rl::console("@probe-nostore"), "pcall(rood.storage.get, 'x')") != "true");
    a->reload_requested = true;
    pc_lua_tick();
    CHECK(eval_json(rl::find("probe.alpha"), "rood.storage.get('answer').list[2]") == "2");
}

/* Ungranted capabilities are absent; a grant needs the master switch and the
 * plugin's current code; fs is scoped; unimplemented ones refuse plainly;
 * elevated calls are audited; revoking or changing the code takes them away. */
static void capabilities()
{
    std::string why;
    CHECK(!rl::filesystem_grant_ok("fs.write:" + scratch, &why));
    CHECK(!rl::filesystem_grant_ok("fs.write:" + rl::user_root(), &why));
    CHECK(!rl::filesystem_grant_ok("fs.write:" + rl::data_root(), &why));
    CHECK(!rl::filesystem_grant_ok("fs.write:/", &why));
    CHECK(rl::filesystem_grant_ok("fs.write:" + scratch + "/exports/new", &why));
    CHECK(rl::filesystem_grant_ok("fs.read:" + scratch, &why));
    // Missing components must not hide traversal back into a protected root.
    CHECK(!rl::filesystem_grant_ok("fs.write:" + scratch + "/missing/..", &why));
    CHECK(!rl::filesystem_grant_ok("fs.write:" + scratch + "/missing/../plugins/new", &why));
    CHECK(!rl::filesystem_grant_ok("fs.write:" + rl::user_root() + "/../plugins", &why));
    CHECK(!rl::capability_ok("fs.write:" + scratch + "/missing/../exports", &why));
    CHECK(!rl::capability_ok("fs.write:" + scratch + "/missing\\..\\exports", &why));
#ifndef _WIN32
    CHECK(symlink(rl::user_root().c_str(), (scratch + "/grant-link").c_str()) == 0);
    CHECK(!rl::filesystem_grant_ok("fs.write:" + scratch + "/grant-link", &why));
    CHECK(!rl::filesystem_grant_ok("fs.write:" + scratch + "/grant-link/..", &why));
    CHECK(symlink((scratch + "/missing-target").c_str(), (scratch + "/dangling-grant").c_str()) == 0);
    CHECK(!rl::filesystem_grant_ok("fs.write:" + scratch + "/dangling-grant/new", &why));
#endif
    size_t grants_before = rl::grants().size();
    pc_lua_settings_parse("plugin_grant", ("probe.caps hash fs.write:" + scratch).c_str());
    CHECK(rl::grants().size() == grants_before);
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.caps/plugin.lua",
        "return { id = 'probe.caps', api_version = 1, capabilities = { 'fs.read', 'fs.write', 'net:example.com', 'exec' } }\n");
    write_text(root + "/probe.caps/main.lua", "\n");
    write_text(root + "/probe.badcap/plugin.lua", "return { id = 'probe.badcap', api_version = 1, capabilities = { 'fs.read:../up' } }\n");
    write_text(root + "/probe.badcap/main.lua", "\n");
    rl::scan();
    rl::config_changed();
    pc_lua_tick();
    rl::Plugin* c = rl::find("probe.caps");
    CHECK(c && c->state == rl::PLUGIN_LOADED);
    CHECK(state_of("probe.badcap") == "failed" && contains(rl::find("probe.badcap")->status, ".."));
    CHECK(eval_json(c, "rood.fs, rood.net, rood.exec, #rood.plugin.missing") == "null,null,null,4");
    std::string error;
    CHECK(!rl::grant("probe.caps", "native", false, &error) && contains(error, "does not declare"));
    CHECK(rl::grant("probe.caps", "fs.read", false, &error) && rl::grant("probe.caps", "fs.write", false, &error)
        && rl::grant("probe.caps", "net:example.com", false, &error));
    pc_lua_tick();
    CHECK(eval_json(rl::find("probe.caps"), "rood.fs") == "null"); /* the master switch is off */
    rl::config.elevated = true;
    rl::config_changed();
    pc_lua_tick();
    c = rl::find("probe.caps");
    CHECK((c->granted == std::vector<std::string>{ "fs.read", "fs.write", "net:example.com" }));
    CHECK(eval_json(c, "rood.fs.write('out/a.txt', 'hi'), rood.fs.read('out/a.txt'), rood.fs.exists('out/a.txt')")
        == "true,\"hi\",true");
    CHECK(contains(eval_error(c, "rood.fs.write('../escape.txt', 'x')"), "bad path"));
    CHECK(contains(eval_error(c, "rood.fs.read('/etc/hostname')"), "bad path"));
    CHECK(contains(eval_error(c, "rood.net.request('https://example.com')"), "not available in this version"));
    CHECK(eval_json(c, "rood.exec, rood.plugin.elevated, rood.plugin.missing[1]") == "null,true,\"exec\"");
    CHECK(!rl::audit_lines().empty() && contains(rl::audit_lines().back(), "probe.caps net"));
    std::string written = settings_text();
    CHECK(contains(written, "plugin_grant probe.caps " + c->hash + " fs.write\n") && contains(written, "plugins_elevated 1\n"));
    CHECK(rl::grant("probe.caps", "fs.write", true, &error));
    pc_lua_tick();
    CHECK(eval_json(rl::find("probe.caps"), "rood.fs.write, type(rood.fs.read)") == "null,\"function\"");
    write_text(root + "/probe.caps/main.lua", "-- changed\n");
    rl::find("probe.caps")->reload_requested = true;
    pc_lua_tick();
    c = rl::find("probe.caps");
    CHECK(c->granted.empty() && rl::pending_grants(c).size() == 2 && eval_json(c, "rood.fs") == "null");
    rl::config.elevated = false;
    rl::config_changed();
    pc_lua_tick();
}

/* A plugin's folder comes from an archive or a clone, links included: no link
 * may lead a read, a require, an entry or a write out of it (storage.read, which
 * needs no capability, read any file of the user's through one). */
static void links()
{
    namespace fs = std::filesystem;
    std::string root = scratch + "/plugins", out = scratch + "/outside", dir = root + "/probe.links";
    write_text(out + "/secret.txt", "SECRET");
    write_text(out + "/victim.txt", "victim");
    write_text(out + "/sub/mod.lua", "return 'outside'\n");
    write_text(dir + "/plugin.lua", "return { id = 'probe.links', api_version = 1, capabilities = { 'fs.read', 'fs.write' } }\n");
    write_text(dir + "/main.lua", "\n");
    write_text(dir + "/inside.txt", "fine");
    fs::create_symlink(out + "/secret.txt", dir + "/secret");
    fs::create_symlink(out, dir + "/dirlink");
    fs::create_symlink(out + "/victim.txt", dir + "/out.txt.tmp");
    fs::create_symlink(out + "/nowhere", dir + "/dangling");
    fs::create_symlink(dir + "/inside.txt", dir + "/samefolder");
    CHECK(mkfifo((dir + "/fifo").c_str(), 0600) == 0);
    write_text(root + "/probe.linkentry/plugin.lua", "return { id = 'probe.linkentry', api_version = 1 }\n");
    fs::create_symlink(out + "/sub/mod.lua", root + "/probe.linkentry/main.lua");
    rl::scan();
    rl::config_changed();
    pc_lua_tick();
    CHECK(state_of("probe.linkentry") == "failed" && contains(rl::find("probe.linkentry")->status, "outside"));
    rl::Plugin* p = rl::find("probe.links");
    CHECK(p && p->state == rl::PLUGIN_LOADED);
    CHECK(contains(eval_error(p, "rood.storage.read('secret')"), "outside its folder"));
    CHECK(contains(eval_error(p, "rood.storage.read('dirlink/secret.txt')"), "outside its folder"));
    CHECK(eval_json(p, "rood.storage.read('samefolder')") == "\"fine\"");
    CHECK(contains(eval_error(p, "assert(rood.storage.read('fifo'))"), "not a regular file")); /* open blocked */
    CHECK(contains(eval_error(p, "require('dirlink.sub.mod')"), "outside its folder"));
    std::string error;
    rl::config.elevated = true;
    CHECK(rl::grant("probe.links", "fs.read", false, &error) && rl::grant("probe.links", "fs.write", false, &error));
    pc_lua_tick();
    p = rl::find("probe.links");
    CHECK(eval_json(p, "rood.fs.read('samefolder'), rood.fs.read('inside.txt')") == "\"fine\",\"fine\"");
    CHECK(contains(eval_error(p, "rood.fs.read('secret')"), "outside its folder"));
    CHECK(contains(eval_error(p, "rood.fs.exists('dirlink/secret.txt')"), "outside its folder"));
    CHECK(contains(eval_error(p, "rood.fs.list('dirlink')"), "outside its folder"));
    CHECK(contains(eval_error(p, "rood.fs.write('dirlink/new/f.txt', 'x')"), "outside its folder"));
    CHECK(contains(eval_error(p, "rood.fs.write('dangling', 'x')"), "leads nowhere"));
    CHECK(eval_json(p, "rood.fs.write('out.txt', 'mine')") == "true"); /* through out.txt.tmp before */
    std::string text, why;
    CHECK(rl::read_file(out + "/victim.txt", &text, 100, &why) && text == "victim");
    CHECK(rl::read_file(dir + "/out.txt", &text, 100, &why) && text == "mine");
    CHECK(!fs::exists(out + "/new") && !fs::exists(out + "/nowhere"));
    rl::config.elevated = false;
    rl::config_changed();
    pc_lua_tick();
}

/* Grants follow the content hash, so whatever a plugin can load as code must
 * be in it: no hidden entry or test file, no module deeper than the hash
 * looks, any case of .lua, no plugin with more code files than it counts. */
static void hash_coverage()
{
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.hidden/plugin.lua", "return { id = 'probe.hidden', api_version = 1, entry = '.main.lua' }\n");
    write_text(root + "/probe.hidden/.main.lua", "\n");
    write_text(root + "/probe.many/plugin.lua", "return { id = 'probe.many', api_version = 1 }\n");
    write_text(root + "/probe.many/main.lua", "\n");
    for (int k = 0; k < 2001; k++)
        write_text(root + "/probe.many/lib/m" + std::to_string(k) + ".lua", "\n");
    std::string deep = root + "/probe.deep/a/b/c/d/e/f/g/h";
    write_text(root + "/probe.deep/plugin.lua", "return { id = 'probe.deep', api_version = 1 }\n");
    write_text(root + "/probe.deep/main.lua", "\n");
    write_text(deep + "/ok.lua", "return 'eight folders'\n");
    write_text(deep + "/i/m.lua", "return 'nine folders'\n");
    write_text(root + "/probe.deep/UPPER.LUA", "return 1\n");
    rl::scan();
    rl::config_changed();
    pc_lua_tick();
    CHECK(state_of("probe.hidden") == "failed" && contains(rl::find("probe.hidden")->status, "hidden"));
    CHECK(state_of("probe.many") == "failed" && contains(rl::find("probe.many")->status, "code files"));
    rl::Plugin* p = rl::find("probe.deep");
    CHECK(p && p->state == rl::PLUGIN_LOADED);
    CHECK(eval_json(p, "require('a.b.c.d.e.f.g.h.ok')") == "\"eight folders\"");
    CHECK(contains(eval_error(p, "require('a.b.c.d.e.f.g.h.i.m')"), "folders deep"));
    std::string before = rl::content_hash(p->dir);
    write_text(root + "/probe.deep/UPPER.LUA", "return 2\n");
    CHECK(rl::content_hash(p->dir) != before);
    before = rl::content_hash(p->dir);
    write_text(deep + "/ok.lua", "return 'changed'\n");
    CHECK(rl::content_hash(p->dir) != before);
}

/* What the host keeps or copies for a plugin is bounded like its own memory:
 * a value passed on (a string shared by many parts was copied for each), log
 * lines and error messages, registry entries. */
static void host_memory()
{
    rl::Plugin* m = rl::console("@probe-mem");
    CHECK(contains(eval_error(m, "local s = string.rep('x', 1 << 20) local t = {} for i = 1, 200 do t[i] = s end "
                                 "rood.emit('probe/big', t)"),
        "too large"));
    CHECK(eval_json(m, "rood.log(string.rep('y', 1 << 20))") == "");
    CHECK(rl::log_lines().back().text.size() < 20000 && contains(rl::log_lines().back().text, "1048576 bytes"));
    std::string e = eval_error(m, "error(string.rep('z', 1 << 20))");
    CHECK(e.size() < 20000 && contains(rl::log_lines().back().text, "bytes)"));
    CHECK(contains(eval_error(m, "local s = string.rep('r', 4 << 20) for i = 1, 20 do "
                                 "rood.registry.register('probe.big', '@probe-mem/e' .. i, { s = s }) end"),
        "more than 32 MB"));
    CHECK(rl::registry_list("probe.big").size() >= 6 && rl::registry_list("probe.big").size() < 9);
    eval_json(m, "for i = 1, 20 do rood.registry.remove('probe.big', '@probe-mem/e' .. i) end");
    /* a loop of costly rood calls (each copying 60000 parts) stops at the first call past the
     * budget, not RL_HOOK_STEP instructions later (3 s for a 250 ms eval before) */
    double start = rl::now_seconds();
    CHECK(contains(eval_error(m, "local t = {} for i = 1, 60000 do t[i] = i end while true do rood.emit('probe/x', t) end"),
        "budget exceeded"));
    CHECK(rl::now_seconds() - start < 0.6);
    CHECK(rl::registry_list("probe.big").empty());
}

/* Hot reload: an edit reloads the plugin and what depends on it, with
 * everything it registered gone first; a new folder loads; a broken edit
 * fails and fixing it reloads. */
static void hot_reload()
{
    std::string root = scratch + "/plugins";
    rl::Plugin* a = rl::find("probe.alpha");
    unsigned generation = a->generation;
    size_t handlers = rl::handler_count("probe.alpha");
    write_text(root + "/probe.alpha/main.lua",
        "ticks = 0\n"
        "rood.on('tick', function() ticks = ticks + 1 end)\n"
        "rood.registry.register('probe.entry', 'probe.alpha/one-v2', { version = 2 })\n");
    rl::config.watch_vsyncs = 2;
    pc_lua_tick();
    pc_lua_tick();
    a = rl::find("probe.alpha");
    CHECK(a->generation > generation && a->state == rl::PLUGIN_LOADED);
    CHECK(rl::handler_count("probe.alpha") == handlers);
    std::vector<std::string> ids;
    for (const rl::RegistryEntry* e : rl::registry_list("probe.entry"))
        ids.push_back(e->id);
    CHECK((ids == std::vector<std::string>{ "probe.alpha/one-v2", "probe.beta/two" }));
    write_text(root + "/probe.late/plugin.lua", "return { id = 'probe.late', api_version = 1 }\n");
    write_text(root + "/probe.late/main.lua", "late = true\n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(state_of("probe.late") == "loaded");
    write_text(root + "/probe.late/main.lua", "late = \n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(state_of("probe.late") == "failed");
    write_text(root + "/probe.late/main.lua", "late = 2\n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(state_of("probe.late") == "loaded" && eval_json(rl::find("probe.late"), "late") == "2");
    rl::config.watch_vsyncs = 30;
}

/* rood.test: cases collected from the .lua files in a plugin's tests folder,
 * run as coroutines (wait(n) resumes after n ticks); pass / fail / skip with
 * messages; a runaway case is stopped by the budget and the plugin stays
 * loaded. */
static void test_harness()
{
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.alpha/tests/alpha_test.lua",
        "local t = rood.test\n"
        "t.case('passes', function() t.eq({ 1, { 2 } }, { 1, { 2 } }) end)\n"
        "t.case('fails', function() t.eq(1, 2, 'one') end)\n"
        "t.case('skips', function() t.skip('not here') end)\n"
        "t.case('waits', function() local before = ticks t.wait(3) t.eq(ticks, before + 3) end)\n"
        "t.case('raises', function() t.raises(function() error('x marks') end, 'marks') end)\n");
    int run = rl::start_tests("probe.alpha", "");
    std::string json = rl::test_run_json(run);
    CHECK(contains(json, "\"done\":false") && contains(json, "\"waiting\":1"));
    for (int k = 0; k < 4; k++)
        pc_lua_tick();
    json = rl::test_run_json(run);
    CHECK(contains(json, "\"done\":true") && contains(json, "\"passed\":3") && contains(json, "\"failed\":1")
        && contains(json, "\"skipped\":1"));
    CHECK(contains(json, "expected 2, got 1") && contains(json, "not here"));
    write_text(root + "/probe.alpha/tests/zz_runaway_test.lua", "rood.test.case('runaway', function() while true do end end)\n");
    run = rl::start_tests("probe.alpha", "runaway");
    json = rl::test_run_json(run);
    CHECK(contains(json, "\"done\":true") && contains(json, "budget exceeded") && contains(json, "\"failed\":1"));
    CHECK(state_of("probe.alpha") == "loaded");
    std::filesystem::remove(root + "/probe.alpha/tests/zz_runaway_test.lua");
    CHECK(contains(eval_error(rl::find("probe.alpha"), "rood.test.case('x', print)"), "only in a test file"));
}

static std::string rpc(const char* params)
{
    static char out[262144];
    int n = pc_lua_rpc(params, out, (int)sizeof out);
    return n < 0 ? std::string("error: ") + out : std::string(out);
}

/* RPC lua: eval with output, the plugin list, budgets, the console's path,
 * the registry, enable, emit, tests, the log; unknown actions refused. */
static void rpc_actions()
{
    CHECK(contains(rpc("{\"action\":\"eval\",\"code\":\"return 6 * 7\"}"), "\"results\":[42]"));
    std::string r = rpc("{\"action\":\"eval\",\"code\":\"print('from eval') error('nope')\"}");
    CHECK(contains(r, "\"ok\":false") && contains(r, "nope") && contains(r, "from eval"));
    r = rpc("{\"action\":\"plugins\"}");
    CHECK(contains(r, "\"id\":\"probe.alpha\"") && contains(r, "\"state\":\"loaded\"") && contains(r, "\"api_version\":1"));
    r = rpc("{\"action\":\"budget\",\"budget_ms\":8}");
    CHECK(contains(r, "\"budget_ms\":8") && rl::config.budget_ms == 8);
    rpc("{\"action\":\"budget\",\"budget_ms\":4}");
    r = rpc("{\"action\":\"console\",\"input\":\"return 1 + 1\"}");
    CHECK(contains(r, "\"results\":[2]") && rl::console_history().back() == "return 1 + 1");
    CHECK(contains(rpc("{\"action\":\"registry\",\"kind\":\"probe.entry\"}"), "probe.alpha/one-v2"));
    rpc("{\"action\":\"enable\",\"plugin\":\"probe.off\",\"enabled\":true}");
    CHECK(state_of("probe.off") == "loaded");
    CHECK(contains(rpc("{\"action\":\"emit\",\"event\":\"probe/rpc\",\"payload\":{\"x\":1}}"), "\"handlers\":0"));
    CHECK(rpc("{\"action\":\"nope\"}").rfind("error: ", 0) == 0);
    r = rpc("{\"action\":\"test\",\"plugin\":\"probe.alpha\",\"filter\":\"passes\"}");
    CHECK(contains(r, "\"done\":true") && contains(r, "\"passed\":1"));
    CHECK(contains(rpc("{\"action\":\"test_status\",\"run\":1}"), "\"run\":1"));
    CHECK(contains(rpc("{\"action\":\"log\",\"since\":0}"), "\"lines\":["));
    CHECK(contains(rpc("{\"action\":\"audit\"}"), "probe.caps"));
}


/* An element as the game's hook reports it (hud_bridge.c): SLOW!, aged 10. */
static int record_status(int age = 10)
{
    pc_hud_fields f = {};
    f.kind = PC_HUD_STATUS;
    std::snprintf(f.text, sizeof f.text, "SLOW!");
    f.x = 44, f.y = 53, f.w = 40, f.h = 10;
    f.rgb = 0xC0FF40;
    f.timing = 3;
    f.age = age, f.life = 46;
    f.anchor = 1;
    return pc_hud_record(&f);
}
static std::string command_text(int i)
{
    const hud::Command& c = hud::commands()[i];
    return c.type == hud::CMD_TEXT ? std::string(hud::text_arena() + c.text, c.length) : std::string("<rect>");
}
static std::string hud_rpc(const char* params)
{
    static char out[65536];
    int n = pc_hud_rpc(params, out, (int)sizeof out);
    return n < 0 ? std::string("error: ") + out : std::string(out);
}

/* The HUD layer (pc/src/hud, lua_api_hud.cpp): the draw API's validation,
 * a claim honoured (the game's packets suppressed) and drawn into the
 * command buffer, a failing draw falls back to the game's drawing, claims
 * and styles dropped on reload, the bounded command buffer, rood.dev only
 * in dev mode, show away from the field (a synthetic element), RPC hud. */
static void hud_layer()
{
    std::string root = scratch + "/plugins";
    unsetenv("ROOD_RPC");
    unsetenv("ROOD_DEV");
    write_text(root + "/probe.hud/plugin.lua", "return { id = 'probe.hud', api_version = 1 }\n");
    write_text(root + "/probe.hud/main.lua",
        "local d = rood.draw\n"
        "word = d.style{ font = 'labels', size = 12, color = 0xFFEEDD, stroke = 1.5, align = 'center', anchor = 'middle' }\n"
        "rood.hud.claim('status', function(e, d) d.text(word, e.text .. '/v1', e.x, e.y, e.rgb) end)\n");
    rl::config.watch_vsyncs = 2;
    pc_lua_tick();
    pc_lua_tick();
    rl::Plugin* p = rl::find("probe.hud");
    CHECK(p && p->state == rl::PLUGIN_LOADED);
    if (!p || p->state != rl::PLUGIN_LOADED)
        return;
    // validation
    CHECK(contains(eval_error(p, "rood.draw.style{ colour = 0xFF, sizes = 3 }"), "unknown field 'sizes'"));
    CHECK(contains(eval_error(p, "rood.draw.style{ font = 'comic' }"), "is not one of dialogue"));
    CHECK(contains(eval_error(p, "rood.draw.style{ size = 0 }"), "outside"));
    CHECK(contains(eval_error(p, "rood.draw.style{ color = 'red' }"), "0xRRGGBB"));
    CHECK(contains(eval_error(p, "rood.draw.style{ 12 }"), "fields are named"));
    CHECK(contains(eval_error(p, "rood.hud.claim('compass', print)"), "unknown HUD kind 'compass'"));
    CHECK(contains(eval_error(p, "rood.hud.claim('status', 3)"), "function expected"));
    CHECK(contains(eval_error(p, "rood.draw.text(word, 'x', 1, 2)"), "only inside a HUD draw function"));
    CHECK(contains(eval_error(p, "rood.draw.text({}, 'x', 1, 2)"), "must be a style"));
    CHECK(contains(eval_error(p, "word.size = 3"), "immutable"));
    CHECK(eval_json(p, "word.size, word.font, word.align") == "12,\"labels\",\"center\"");
    CHECK(eval_json(p, "rood.draw.style{ font = 'labels', size = 12, color = 0xFFEEDD, stroke = 1.5, align = 'center',"
                       " anchor = 'middle' } ~= nil") == "true");
    CHECK(eval_json(p, "select('#', rood.draw.measure(word, 'SLOW!'))") == "2");
    CHECK(eval_json(p, "rood.dev") == "null"); // not in dev mode
    // claimed: the game's packets suppressed, the claim draws the element
    CHECK(hud::claimed(PC_HUD_STATUS));
    CHECK(record_status() == 1);
    pc_hud_frame();
    CHECK(hud::command_count() == 1 && command_text(0) == "SLOW!/v1");
    CHECK(hud::commands()[0].color == 0xC0FF40 && hud::commands()[0].anchor == 1);
    CHECK(contains(hud_rpc("{\"action\":\"stats\"}"), "\"claimed_by\":\"probe.hud\""));
    CHECK(contains(hud_rpc("{\"action\":\"list\"}"), "\"text\":\"SLOW!\""));
    // reload: the claim and its styles go with the old load, the new one claims again
    write_text(root + "/probe.hud/main.lua",
        "local d = rood.draw\n"
        "word = d.style{ font = 'status', size = 14 }\n"
        "rood.hud.claim('status', function(e, d) d.rect(e.x, e.y, 4, 4, 0xFF0000) d.text(word, e.text .. '/v2', e.x, e.y) end)\n");
    pc_lua_tick();
    pc_lua_tick();
    p = rl::find("probe.hud");
    CHECK(record_status() == 1);
    pc_hud_frame();
    CHECK(hud::command_count() == 2 && command_text(1) == "SLOW!/v2");
    int styles = 0;
    for (int i = 0; i < HUD_STYLES; ++i)
        styles += hud::styles()[i].used && hud::styles()[i].owner == "probe.hud";
    CHECK(styles == 1);
    write_text(root + "/probe.hud/main.lua", "-- no claim\n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(!hud::claimed(PC_HUD_STATUS) && record_status() == 0); // the game draws it
    pc_hud_frame();
    CHECK(hud::command_count() == 0);
    // a draw that raises: counted, and the game draws it from then on
    write_text(root + "/probe.hud/main.lua",
        "rood.hud.claim('status', function(e, d) error('broken draw') end)\n");
    pc_lua_tick();
    pc_lua_tick();
    unsigned long long faults = hud::stats(PC_HUD_STATUS).faults;
    CHECK(record_status() == 1);
    pc_hud_frame();
    CHECK(hud::stats(PC_HUD_STATUS).faults == faults + 1 && record_status() == 0);
    // the command buffer is bounded: past it draws are refused, nothing grows
    write_text(root + "/probe.hud/main.lua",
        "local s = rood.draw.style{}\n"
        "refused = 0\n"
        "rood.hud.claim('status', function(e, d) refused = 0 for i = 1, 3000 do if not d.text(s, 'many', i, 1) then"
        " refused = refused + 1 end end end)\n");
    pc_lua_tick();
    pc_lua_tick();
    p = rl::find("probe.hud");
    CHECK(record_status() == 1);
    pc_hud_frame();
    CHECK(hud::command_count() == HUD_COMMANDS && eval_json(p, "refused") == std::to_string(3000 - HUD_COMMANDS));
    // dev mode: rood.dev; show away from the field gives a synthetic element claims draw
    setenv("ROOD_DEV", "1", 1);
    write_text(root + "/probe.hud/main.lua",
        "local s = rood.draw.style{ size = 9 }\n"
        "rood.hud.claim('status', function(e, d) d.text(s, e.text, e.x, e.y) end)\n");
    pc_lua_tick();
    pc_lua_tick();
    p = rl::find("probe.hud");
    CHECK(eval_json(p, "type(rood.dev)") == "\"table\"");
    CHECK(contains(eval_error(p, "rood.dev.show('status', { timing = 'sluggish' })"), "timing must be"));
    for (int k = 0; k < 10; k++)
        pc_lua_tick(); // the recorded element is gone (not drawn for a while)
    CHECK(eval_json(p, "rood.dev.show('status', { timing = 'too_fast' })") == "true");
    CHECK(eval_json(p, "#rood.dev.list(), rood.dev.list()[1].text, rood.dev.list()[1].synthetic") == "1,\"TOO FAST!\",true");
    for (int k = 0; k < 60; k++)
        pc_lua_tick(); // held: it stays
    pc_hud_frame();
    CHECK(hud::command_count() == 1 && command_text(0) == "TOO FAST!");
    CHECK(contains(hud_rpc("{\"action\":\"clear\"}"), "\"ok\":true"));
    pc_hud_frame();
    CHECK(hud::command_count() == 0);
    CHECK(contains(hud_rpc("{\"action\":\"show\",\"kind\":\"status\",\"timing\":\"success\",\"variant\":4,\"hold\":false}"),
        "\"ok\":true"));
    pc_hud_frame();
    CHECK(hud::command_count() == 1 && command_text(0) == "PERFECT!");
    for (int k = 0; k < 60; k++)
        pc_lua_tick(); // not held: it plays out
    pc_hud_frame();
    CHECK(hud::command_count() == 0);
    // the other kinds (tasks 5-6), away from the field: synthetic elements with their fields; d.fit
    // (a buff's boxes), d.glyph (the Travel prompt's icon), every kind claimed at once
    write_text(root + "/probe.hud/main.lua",
        "local w = rood.draw.style{ size = 9, stroke = 1, cap = 6 }\n"
        "for _, k in ipairs(rood.hud.kinds()) do rood.hud.claim(k, function(e, d)\n"
        "  if e.kind == 'buff' then for _, p in ipairs(e.parts) do d.fit(w, p.text, p.x, p.y, p.w, p.h, e.rgb) end\n"
        "  elseif e.kind == 'travel' then d.text(w, e.text, e.x + d.glyph(e.glyph, e.x, e.y, 8, w), e.y)\n"
        "  else d.text(w, e.text .. (e.value and ('#' .. e.value) or ''), e.x, e.y + (e.bounce or 0)) end\n"
        "end) end\n");
    pc_lua_tick();
    pc_lua_tick();
    p = rl::find("probe.hud");
    CHECK(eval_json(p, "#rood.hud.kinds(), rood.hud.kinds()[5]") == "5,\"travel\"");
    CHECK(contains(hud_rpc("{\"action\":\"show\",\"kind\":\"chain\",\"count\":5}"), "\"ok\":true"));
    CHECK(contains(hud_rpc("{\"action\":\"show\",\"kind\":\"buff\",\"slot\":\"head\",\"stat\":\"fire\",\"value\":3,"
                           "\"sign\":-1}"), "\"ok\":true"));
    CHECK(eval_json(p, "rood.dev.show('damage', { value = 77, heal = true, pool = 'mp' })") == "true");
    CHECK(eval_json(p, "rood.dev.show('travel')") == "true");
    CHECK(contains(eval_error(p, "rood.dev.show('buff', { slot = 'tail' })"), "an unknown name"));
    std::string list = hud_rpc("{\"action\":\"list\"}");
    CHECK(contains(list, "\"text\":\"5 CHAINS\"") && contains(list, "\"text\":\"HEAD FIRE -3\"")
        && contains(list, "\"text\":\"77\"") && contains(list, "\"text\":\"TRAVEL\""));
    CHECK(eval_json(p, "(function() for _, e in ipairs(rood.dev.list()) do if e.kind == 'buff' then return #e.parts,"
                       " e.parts[1].text, e.parts[2].text, e.parts[4].text, e.slot, e.stat, e.sign end end end)()")
        == "4,\"HEAD\",\"FIRE\",\"3\",\"HEAD\",\"FIRE\",-1");
    CHECK(eval_json(p, "(function() for _, e in ipairs(rood.dev.list()) do if e.kind == 'damage' then return e.pool,"
                       " e.sign, e.anchor end end end)()") == "\"mp\",1,\"centre\"");
    CHECK(eval_json(p, "(function() for _, e in ipairs(rood.dev.list()) do if e.kind == 'travel' then return e.glyph,"
                       " e.anchor, e.unit > 0 end end end)()") == "\"travel\",\"picture\",true");
    pc_hud_frame();
    int fitted = 0, glyphs = 0;
    std::string texts;
    for (int i = 0; i < hud::command_count(); ++i) {
        const hud::Command& c = hud::commands()[i];
        fitted += c.type == hud::CMD_TEXT && c.fit && c.w > 0 && c.h > 0;
        glyphs += c.type == hud::CMD_GLYPH && c.button == 16 && c.h == 8 && c.anchor == PC_HUD_ANCHOR_PICTURE;
        texts += command_text(i) + "|";
    }
    CHECK(fitted == 4 && glyphs == 1);
    CHECK(contains(texts, "5 CHAINS#5|") && contains(texts, "77#77|") && contains(texts, "TRAVEL|"));
    // styles let go when their handles are collected (a look rebuilt as settings change)
    CHECK(eval_json(p, "for i = 1, 200 do rood.draw.style{ size = 1 + i / 2 } end collectgarbage() collectgarbage()"
                       " for i = 1, 200 do rood.draw.style{ size = 2 + i / 3, stroke = 2 } end return true") == "true");
    // typography needs ui_text (not in this probe)
    CHECK(contains(eval_error(p, "rood.hud.typography('status', { size = 2 })"), "no ui_text in this build"));
    CHECK(contains(hud_rpc("{\"action\":\"clear\"}"), "\"ok\":true"));
    CHECK(hud_rpc("{\"action\":\"nope\"}").rfind("error: ", 0) == 0);
    unsetenv("ROOD_DEV");
    std::filesystem::remove_all(root + "/probe.hud");
    pc_lua_tick();
    pc_lua_tick();
    rl::config.watch_vsyncs = 30;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: lua_probe <scratch directory>\n");
        return 2;
    }
    scratch = argv[1];
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch + "/plugins");
    setenv("ROOD_PLUGINS", (scratch + "/plugins").c_str(), 1);
    setenv("ROOD_PLUGIN_DATA", (scratch + "/data").c_str(), 1);
    setenv("ROOD_PLUGINS_SHIPPED", "", 1);
    lua_core();
    allocator_cap();
    budgets();
    values_and_calls();
    sandbox();
    core_api();
    events();
    timers();
    registry();
    files_and_hash();
    plugins_on_disk();
    storage();
    capabilities();
    links();
    hash_coverage();
    host_memory();
    hot_reload();
    test_harness();
    rpc_actions();
    hud_layer();
    if (failures) {
        std::printf("lua_probe: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("lua_probe: ok\n");
    return 0;
}
