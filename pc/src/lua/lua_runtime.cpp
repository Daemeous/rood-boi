// The extension system's runtime (pc/docs/Extensions.md): each plugin's own
// Lua state, protected calls with budgets and tracebacks, faults (a plugin
// that keeps failing is disabled at the next safe point, lua_plugins.cpp
// settle), the log the console and the RPC read, console sandboxes and
// eval, and the Lua helpers the API files share. Everything runs on the
// game thread (pc/docs/Extensions.md "Where Lua runs"); run() refuses any
// other thread.
#include "rl_internal.h"

#include <cstdio>
#include <exception>
#ifndef _WIN32
#include <pthread.h>
#endif

namespace rl {
namespace {
int in_progress; /* calls into any plugin */
unsigned long long next_seq = 1;
unsigned long long owner_thread;
bool owner_known;

unsigned long long thread_id()
{
#ifdef _WIN32
    return (unsigned long long)GetCurrentThreadId();
#else
    return (unsigned long long)pthread_self();
#endif
}

std::deque<LogLine>& log_buffer()
{
    static auto* lines = new std::deque<LogLine>();
    return *lines;
}

struct Job {
    const std::function<void(lua_State*)>* body;
};

int trampoline(lua_State* L)
{
    Job* job = (Job*)lua_touserdata(L, 1);
    lua_settop(L, 0);
    Plugin* p = plugin_of(L);
    for (int ref : p->pending_unrefs)
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    p->pending_unrefs.clear();
    try {
        (*job->body)(L);
    } catch (const std::exception& e) {
        return luaL_error(L, "internal error: %s", e.what());
    }
    return 0;
}

int traceback(lua_State* L)
{
    plugin_of(L)->mem.grace = 256 * 1024; /* room for the text when the cap is what failed */
    const char* message = lua_tostring(L, 1);
    if (!message)
        message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    luaL_traceback(L, L, message, 1);
    return 1;
}

int guarded_call(lua_State* L)
{
    lua_CFunction f = lua_tocfunction(L, lua_upvalueindex(1));
    check_budget(L);
    try {
        return f(L);
    } catch (const std::exception& e) {
        return luaL_error(L, "internal error: %s", e.what());
    }
}

int locked_newindex(lua_State* L)
{
    return luaL_error(L, "rood is read-only (cannot set '%s')", luaL_tolstring(L, 2, nullptr));
}
int raw_next(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);
    if (lua_next(L, 1))
        return 2;
    lua_pushnil(L);
    return 1;
}
int locked_pairs(lua_State* L)
{
    lua_pushcfunction(L, raw_next);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushnil(L);
    return 3;
}

void warn_to_log(void* ud, const char* message, int tocont)
{
    static std::string pending;
    Plugin* p = (Plugin*)ud;
    if (pending.empty() && !tocont && message[0] == '@')
        return; /* a control message ("@on") */
    pending += message;
    if (!tocont) {
        log(LOG_WARN, p->id(), pending);
        pending.clear();
    }
}

std::string clipped(const std::string& text)
{
    if (text.size() <= RL_MAX_LOG_LINE)
        return text;
    return text.substr(0, RL_MAX_LOG_LINE) + "... (" + std::to_string(text.size()) + " bytes)";
}
} // namespace

void log(int level, const std::string& plugin, const std::string& full)
{
    static const char* const levels[] = { "info", "warn", "error" };
    std::string text = clipped(full);
    std::fprintf(stderr, "[lua] %s%s%s: %s\n", plugin.c_str(), plugin.empty() ? "" : " ", levels[level], text.c_str());
    log_buffer().push_back({ next_seq++, level, plugin, text });
    while (log_buffer().size() > 2000)
        log_buffer().pop_front();
}
const std::deque<LogLine>& log_lines() { return log_buffer(); }
unsigned long long log_last_seq() { return next_seq - 1; }

const char* state_name(int state)
{
    switch (state) {
    case PLUGIN_LOADED:
        return "loaded";
    case PLUGIN_FAILED:
        return "failed";
    case PLUGIN_DISABLED:
        return "disabled";
    default:
        return "off";
    }
}

std::vector<std::unique_ptr<Plugin>>& all()
{
    static auto* list = new std::vector<std::unique_ptr<Plugin>>();
    return *list;
}
Plugin* find(const std::string& id)
{
    for (auto& p : all())
        if (p->id() == id)
            return p.get();
    return nullptr;
}

std::vector<ApiOpener>& api_openers()
{
    static auto* list = new std::vector<ApiOpener>();
    return *list;
}
std::vector<PluginHook>& unload_hooks()
{
    static auto* list = new std::vector<PluginHook>();
    return *list;
}
std::vector<Hook>& poll_hooks()
{
    static auto* list = new std::vector<Hook>();
    return *list;
}
std::vector<Hook>& tick_hooks()
{
    static auto* list = new std::vector<Hook>();
    return *list;
}
std::vector<Hook>& flush_hooks()
{
    static auto* list = new std::vector<Hook>();
    return *list;
}

bool on_game_thread()
{
    if (!owner_known) {
        owner_thread = thread_id();
        owner_known = true;
    }
    return thread_id() == owner_thread;
}
int calls_in_progress() { return in_progress; }

void push_cfunction(lua_State* L, lua_CFunction f)
{
    lua_pushcfunction(L, f);
    lua_pushcclosure(L, guarded_call, 1);
}
void set_cfunction(lua_State* L, int table, const char* name, lua_CFunction f)
{
    table = lua_absindex(L, table);
    push_cfunction(L, f);
    lua_setfield(L, table, name);
}
void lock_table(lua_State* L)
{
    lua_newtable(L);          /* the view */
    lua_createtable(L, 0, 4); /* its metatable */
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, "__index");
    lua_pushvalue(L, -3);
    lua_pushcclosure(L, locked_pairs, 1);
    lua_setfield(L, -2, "__pairs");
    lua_pushcfunction(L, locked_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_remove(L, -2);
}

bool open_state(Plugin* p, std::string* error)
{
    p->mem = Memory();
    p->mem.cap = (size_t)(config.memory_mb * 1048576.0);
    p->budget = Budget();
    p->pending_unrefs.clear();
    p->L = new_state(p);
    if (!p->L) {
        if (error)
            *error = "cannot create a Lua state (is the memory cap too small?)";
        return false;
    }
    lua_setwarnf(p->L, warn_to_log, p);
    bool ok = run(p, "open", BUDGET_LOAD, [p](lua_State* L) {
        open_sandbox(L, p);
        open_rood(L, p);
    }, error);
    if (!ok)
        close_state(p);
    return ok;
}

void close_state(Plugin* p)
{
    if (!p->L)
        return;
    begin_budget(p, BUDGET_LOAD); /* finalizers run under a budget too */
    ++in_progress;
    lua_close(p->L);
    --in_progress;
    end_budget(p);
    p->L = nullptr;
    p->pending_unrefs.clear();
}

bool run(Plugin* p, const char* what, BudgetKind kind, const std::function<void(lua_State*)>& body, std::string* error)
{
    if (!p || !p->L) {
        if (error)
            *error = "not loaded";
        return false;
    }
    if (!on_game_thread()) {
        log(LOG_ERROR, p->id(), std::string(what) + ": called off the game thread; refused");
        if (error)
            *error = "called off the game thread";
        return false;
    }
    lua_State* L = p->L;
    if (!lua_checkstack(L, 4)) {
        if (error)
            *error = "Lua stack exhausted";
        return false;
    }
    bool top = p->depth == 0;
    if (top)
        begin_budget(p, kind);
    ++p->depth;
    ++in_progress;
    Job job{ &body };
    int base = lua_gettop(L);
    lua_pushcfunction(L, traceback);
    lua_pushcfunction(L, trampoline);
    lua_pushlightuserdata(L, &job);
    int status = lua_pcall(L, 1, 0, base + 1);
    std::string message;
    if (status == LUA_OK && top && p->budget.killed) {
        /* killed where the error was caught (a coroutine's resume) and returned before the
         * next check: still a runaway, for the fault policy too */
        status = LUA_ERRRUN;
        lua_pushfstring(L, "budget exceeded (caught and returned from: %s)", what);
    }
    if (status != LUA_OK) {
        const char* text = lua_tostring(L, -1);
        message = clipped(text ? text : "(error without a message)");
    }
    lua_settop(L, base);
    --p->depth;
    --in_progress;
    bool killed = p->budget.killed;
    if (top) {
        double ms = end_budget(p);
        p->stats.calls++;
        p->stats.total_ms += ms;
        if (ms > p->stats.max_ms)
            p->stats.max_ms = ms;
    }
    if (status == LUA_OK)
        return true;
    fault(p, what, message, killed, kind != BUDGET_TEST);
    if (error)
        *error = message;
    return false;
}

void fault(Plugin* p, const char* what, const std::string& message, bool killed, bool counted)
{
    p->stats.faults++;
    if (killed)
        p->stats.kills++;
    log(LOG_ERROR, p->id(), std::string(what) + ": " + message);
    if (!counted || p->console || p->state != PLUGIN_LOADED || !p->disable_reason.empty())
        return;
    double now = now_seconds();
    p->fault_times.push_back(now);
    while (!p->fault_times.empty() && now - p->fault_times.front() > config.fault_window_s)
        p->fault_times.erase(p->fault_times.begin());
    if ((int)p->fault_times.size() >= config.fault_limit) {
        char window[32];
        std::snprintf(window, sizeof window, "%g", config.fault_window_s);
        p->disable_reason = std::to_string(p->fault_times.size()) + " faults within " + window + " s (last: "
            + message.substr(0, message.find('\n')) + ")";
    }
}

bool eval(Plugin* p, const std::string& code, std::vector<Value>* results, std::string* error)
{
    return run(p, "eval", BUDGET_TEST, [&](lua_State* L) {
        std::string expression = "return " + code;
        if (luaL_loadbufferx(L, expression.data(), expression.size(), "=eval", "t") != LUA_OK) {
            lua_pop(L, 1);
            if (luaL_loadbufferx(L, code.data(), code.size(), "=eval", "t") != LUA_OK)
                lua_error(L);
        }
        lua_call(L, 0, LUA_MULTRET);
        if (results)
            for (int k = 1; k <= lua_gettop(L); k++)
                results->push_back(Value::from_lua(L, k));
    }, error);
}

Plugin* console(const std::string& name)
{
    if (Plugin* p = find(name))
        return p;
    auto made = std::make_unique<Plugin>();
    Plugin* p = made.get();
    p->manifest.id = p->manifest.name = name;
    p->console = true;
    all().push_back(std::move(made));
    ++p->generation;
    std::string error;
    if (open_state(p, &error)) {
        p->state = PLUGIN_LOADED;
        p->status = "loaded";
    } else {
        p->state = PLUGIN_FAILED;
        p->status = error;
    }
    return p;
}

} // namespace rl
