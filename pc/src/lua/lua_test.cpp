// rood.test (pc/docs/Extensions.md, "Testing"): Lua tests that run inside a
// running game, without a relaunch. A plugin's tests are the .lua files in
// its tests folder (manifest "tests", default "tests"); each calls
// rood.test.case(name, fn). RPC lua {action "test"} collects them and runs
// every case as a coroutine under the test budget: rood.test.wait(n) resumes
// it after n game ticks (rood_rpc.Game.lua_test steps the game meanwhile).
// Assertions: eq (deep), ok, near, raises, skip. Results as JSON.
#include "rl_internal.h"
#include "light_prefabs.h"

#include <cstdio>
#include <cstring>

namespace rl {
namespace {
const char* const TEST_LIB = R"lua(
local test, collect_case, wait = ...
local function show(v)
  if type(v) == "string" then return string.format("%q", v) end
  if type(v) ~= "table" then return tostring(v) end
  local parts, n = {}, 0
  for k, x in pairs(v) do
    n = n + 1
    if n > 8 then parts[#parts + 1] = "..." break end
    parts[#parts + 1] = tostring(k) .. "=" .. show(x)
  end
  return "{" .. table.concat(parts, ", ") .. "}"
end
local function deep_eq(a, b, seen)
  if a == b then return true end
  if type(a) ~= "table" or type(b) ~= "table" then return false end
  seen = seen or {}
  if seen[a] == b then return true end
  seen[a] = b
  for k, v in pairs(a) do if not deep_eq(v, b[k], seen) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end
local function prefix(msg) return msg and (tostring(msg) .. ": ") or "" end
function test.eq(actual, expected, msg)
  if not deep_eq(actual, expected) then
    error(prefix(msg) .. "expected " .. show(expected) .. ", got " .. show(actual), 2)
  end
end
function test.ok(value, msg)
  if not value then error(prefix(msg) .. "expected a true value, got " .. show(value), 2) end
  return value
end
function test.near(actual, expected, eps, msg)
  eps = eps or 1e-6
  if type(actual) ~= "number" or math.abs(actual - expected) > eps then
    error(prefix(msg) .. "expected " .. show(expected) .. " within " .. eps .. ", got " .. show(actual), 2)
  end
end
function test.raises(fn, pattern, msg)
  local ok, err = pcall(fn)
  if ok then error(prefix(msg) .. "expected an error", 2) end
  err = tostring(err)
  if pattern and not string.find(err, pattern) then
    error(prefix(msg) .. "error " .. show(err) .. " does not match " .. show(pattern), 2)
  end
  return err
end
function test.skip(reason) error("\1skip:" .. tostring(reason or ""), 0) end
test.case = collect_case
test.wait = wait
return test
)lua";

struct Case {
    std::string plugin, file, name;
    unsigned generation;
    int thread_ref;
    int wait = 0;
    double ms = 0;
};
struct Result {
    std::string plugin, file, name, status, message;
    double ms;
};
struct TestRun {
    int id = 0;
    std::vector<Case> pending;
    std::vector<Result> results;
    bool done = false;
};
std::deque<TestRun>& runs()
{
    static auto* list = new std::deque<TestRun>();
    return *list;
}
int next_run = 1;

int test_case(lua_State* L)
{
    luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "rood.test.collect") != LUA_TTABLE)
        return luaL_error(L, "rood.test.case: only in a test file (run them with RPC lua {action \"test\"})");
    lua_createtable(L, 2, 0);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, 2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    return 0;
}

int test_wait(lua_State* L)
{
    lua_Integer ticks = luaL_optinteger(L, 1, 1);
    if (ticks < 1 || ticks > 100000)
        return luaL_error(L, "rood.test.wait: 1 to 100000 ticks");
    if (!lua_isyieldable(L))
        return luaL_error(L, "rood.test.wait: only inside a test case");
    lua_settop(L, 0);
    lua_pushinteger(L, ticks);
    return lua_yield(L, 1);
}

std::string number(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.3f", v);
    return buffer;
}

/* Collects one test file's cases: each a coroutine anchored in the registry. */
void collect(Plugin* p, const std::string& file, const std::string& filter, TestRun& r)
{
    std::string folder = p->dir + "/" + p->manifest.tests, code, why;
    if (!inside(p->dir, folder + "/" + file, &why) || !read_file(folder + "/" + file, &code, RL_MAX_FILE, &why)) {
        r.results.push_back({ p->id(), file, "(file)", "fail", why, 0 });
        return;
    }
    std::string chunk = "@" + p->id() + "/" + p->manifest.tests + "/" + file, error;
    std::vector<std::pair<std::string, int>> cases;
    bool ok = run(p, "collect tests", BUDGET_TEST, [&](lua_State* L) {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "rood.test.collect");
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 0);
        lua_getfield(L, LUA_REGISTRYINDEX, "rood.test.collect"); /* 1 */
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "rood.test.collect");
        for (lua_Integer k = 1; k <= (lua_Integer)lua_rawlen(L, 1); k++) {
            lua_rawgeti(L, 1, k); /* 2: {name, fn} */
            lua_rawgeti(L, 2, 1);
            std::string name = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!filter.empty() && name.find(filter) == std::string::npos) {
                lua_pop(L, 1);
                continue;
            }
            lua_State* co = lua_newthread(L); /* 3 */
            lua_rawgeti(L, 2, 2);
            lua_xmove(L, co, 1);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the thread */
            lua_pop(L, 1);
            cases.emplace_back(name, ref);
        }
    }, &error);
    if (!ok) {
        r.results.push_back({ p->id(), file, "(file)", "fail", error, 0 });
        return;
    }
    for (auto& c : cases)
        r.pending.push_back({ p->id(), file, c.first, p->generation, c.second, 0, 0 });
}

void step_run(TestRun& r, bool tick)
{
    for (size_t k = 0; k < r.pending.size();) {
        Case& c = r.pending[k];
        if (tick && c.wait > 0)
            c.wait--;
        if (c.wait > 0) {
            k++;
            continue;
        }
        Plugin* p = find(c.plugin);
        std::string status, message, error;
        int wait = 0;
        if (!p || p->state != PLUGIN_LOADED || p->generation != c.generation) {
            status = "fail";
            message = "the plugin was unloaded or reloaded during the test";
        } else {
            double start = now_seconds();
            bool ok = run(p, "test", BUDGET_TEST, [&](lua_State* L) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, c.thread_ref);
                lua_State* co = lua_tothread(L, -1);
                int results = 0;
                int st = lua_resume(co, L, 0, &results);
                if (st == LUA_YIELD) {
                    wait = results > 0 && lua_isinteger(co, -1) ? (int)lua_tointeger(co, -1) : 1;
                    lua_pop(co, results);
                    status = "wait";
                } else if (st == LUA_OK) {
                    status = "pass";
                } else {
                    const char* m = lua_tostring(co, -1);
                    std::string text = m ? m : "(error object is not a string)";
                    if (text.compare(0, 6, "\1skip:") == 0) {
                        status = "skip";
                        message = text.substr(6);
                    } else {
                        luaL_traceback(L, co, text.c_str(), 0);
                        message = lua_tostring(L, -1);
                        lua_pop(L, 1);
                        status = "fail";
                    }
                }
                lua_pop(L, 1);
            }, &error);
            c.ms += (now_seconds() - start) * 1000.0;
            if (!ok) {
                if (status != "fail") /* keep the case's own traceback (a budget kill inside it) */
                    message = error;
                status = "fail";
            }
            if (status == "wait") {
                c.wait = wait;
                k++;
                continue;
            }
            p->pending_unrefs.push_back(c.thread_ref);
        }
        r.results.push_back({ c.plugin, c.file, c.name, status, message, c.ms });
        r.pending.erase(r.pending.begin() + (long)k);
    }
    if (r.pending.empty())
        r.done = true;
}
} // namespace

void open_test(lua_State* L, Plugin*)
{
    if (luaL_loadbufferx(L, TEST_LIB, std::strlen(TEST_LIB), "=rood.test", "t") != LUA_OK)
        lua_error(L);
    lua_newtable(L);
    push_cfunction(L, test_case);
    push_cfunction(L, test_wait);
    lua_call(L, 3, 1);
    lock_table(L);
    lua_setfield(L, -2, "test");
}

int start_tests(const std::string& plugin, const std::string& filter)
{
    TestRun r;
    r.id = next_run++;
    for (auto& up : all()) {
        Plugin* p = up.get();
        if (p->console || p->state != PLUGIN_LOADED || (!plugin.empty() && p->id() != plugin))
            continue;
        for (const std::string& file : list_dir(p->dir + "/" + p->manifest.tests))
            if (file.size() > 4 && file[0] != '.' && file.compare(file.size() - 4, 4, ".lua") == 0) /* hidden: not hashed */
                collect(p, file, filter, r);
    }
    runs().push_back(std::move(r));
    while (runs().size() > 20)
        runs().pop_front();
    step_run(runs().back(), false);
    return runs().back().id;
}

void step_tests(bool tick)
{
    for (TestRun& r : runs())
        if (!r.done)
            step_run(r, tick);
}

std::string test_run_json(int id)
{
    for (const TestRun& r : runs()) {
        if (r.id != id)
            continue;
        int passed = 0, failed = 0, skipped = 0;
        std::string results;
        for (const Result& x : r.results) {
            passed += x.status == "pass";
            failed += x.status == "fail";
            skipped += x.status == "skip";
            results += (results.empty() ? "" : ",") + std::string("{\"plugin\":") + prefab::json_quote(x.plugin)
                + ",\"file\":" + prefab::json_quote(x.file) + ",\"name\":" + prefab::json_quote(x.name)
                + ",\"status\":" + prefab::json_quote(x.status) + ",\"message\":" + prefab::json_quote(x.message)
                + ",\"ms\":" + number(x.ms) + "}";
        }
        return "{\"run\":" + std::to_string(r.id) + ",\"done\":" + (r.done ? "true" : "false")
            + ",\"passed\":" + std::to_string(passed) + ",\"failed\":" + std::to_string(failed)
            + ",\"skipped\":" + std::to_string(skipped) + ",\"waiting\":" + std::to_string(r.pending.size())
            + ",\"results\":[" + results + "]}";
    }
    return "";
}

void drop_tests(const std::string& plugin)
{
    for (TestRun& r : runs())
        for (size_t k = 0; k < r.pending.size();)
            if (r.pending[k].plugin == plugin) {
                const Case& c = r.pending[k];
                r.results.push_back({ c.plugin, c.file, c.name, "fail", "the plugin was unloaded during the test", c.ms });
                r.pending.erase(r.pending.begin() + (long)k);
            } else
                k++;
}

} // namespace rl
