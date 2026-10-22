// The event bus and the core's hooks (pc/docs/Extensions.md, "Events and
// timers"): rood.on / off / emit; the core's events, un-namespaced: tick
// (every game vsync while the game runs: {vsync, game_time}), frame (every
// presented image, real time: {dt, real_time}), plugin.loaded and
// plugin.unloaded ({id}), registry.changed ({kind, id, action}), and unload
// (to the plugin itself, before it goes). Plugins emit namespaced events
// only ("my.plugin/changed"), in any namespace; a handler gets the payload
// and the id of the plugin that emitted it (nil: the core), so it can tell
// its own namespace's events from another plugin's. Handlers run one by one,
// each under its own plugin's budget, with its own copy of the payload. Timers:
// rood.timer.after / every / cancel on the game's clock (ticks; it stops
// while the game is paused) or the real one ({ clock = "real" }; frames);
// rood.clock.game() / real().
#include "rl_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace rl {
namespace {
struct Handler {
    unsigned long long id;
    std::string plugin;
    unsigned generation;
    int ref;
    std::string event;
};
std::vector<Handler>& handlers()
{
    static auto* list = new std::vector<Handler>();
    return *list;
}
unsigned long long next_id = 1;
int emit_depth;
double game_time;

bool event_name_ok(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 64)
        return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!(std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '/' || c == ':'))
            return false;
    }
    return true;
}

bool callable(const std::string& plugin, unsigned generation, Plugin** out)
{
    Plugin* p = find(plugin);
    if (!p || !p->L || p->state != PLUGIN_LOADED || p->generation != generation || !p->disable_reason.empty())
        return false;
    *out = p;
    return true;
}

bool registered(unsigned long long id)
{
    for (const Handler& h : handlers())
        if (h.id == id)
            return true;
    return false;
}

int call_handlers(const std::vector<Handler>& matching, const std::string& event, const Value& payload,
    const std::string& sender)
{
    int called = 0;
    ++emit_depth;
    for (const Handler& h : matching) {
        Plugin* p;
        if (!registered(h.id) || !callable(h.plugin, h.generation, &p))
            continue;
        ++called;
        run(p, ("on " + event).c_str(), BUDGET_CALL, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, h.ref);
            push(L, payload);
            if (sender.empty())
                lua_pushnil(L);
            else
                lua_pushlstring(L, sender.data(), sender.size());
            lua_call(L, 2, 0);
        });
    }
    --emit_depth;
    return called;
}

int api_on(lua_State* L)
{
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!event_name_ok(event))
        return luaL_error(L, "rood.on: bad event name '%s' (letters, digits and _ . - / :)", event);
    Plugin* p = plugin_of(L);
    lua_settop(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    handlers().push_back({ next_id, p->id(), p->generation, ref, event });
    lua_pushinteger(L, (lua_Integer)next_id++);
    return 1;
}

int api_off(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    Plugin* p = plugin_of(L);
    std::vector<Handler>& list = handlers();
    for (auto it = list.begin(); it != list.end(); ++it)
        if ((lua_Integer)it->id == id && it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            list.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

int api_emit(lua_State* L)
{
    const char* event = luaL_checkstring(L, 1);
    if (!event_name_ok(event) || !std::strchr(event, '/'))
        return luaL_error(L, "rood.emit: plugins emit namespaced events ('%s' needs a '/', as in 'my.plugin/changed')",
            event);
    if (emit_depth >= 8)
        return luaL_error(L, "rood.emit: events nested more than 8 deep");
    Value payload = lua_isnoneornil(L, 2) ? Value() : Value::from_lua(L, 2);
    lua_pushinteger(L, emit(event, payload, plugin_of(L)->id()));
    return 1;
}
struct Timer {
    unsigned long long id;
    std::string plugin;
    unsigned generation;
    int ref;
    bool real;
    double due, interval; /* interval 0: once */
};
std::vector<Timer>& timers()
{
    static auto* list = new std::vector<Timer>();
    return *list;
}

int make_timer(lua_State* L, bool repeat)
{
    lua_Number seconds = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    bool real = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "clock");
        const char* clock = lua_tostring(L, -1);
        if (clock && !std::strcmp(clock, "real"))
            real = true;
        else if (clock && std::strcmp(clock, "game"))
            return luaL_error(L, "rood.timer: clock is 'game' or 'real', not '%s'", clock);
        lua_pop(L, 1);
    }
    if (!(seconds >= 0) || (repeat && seconds < 1.0 / 240))
        return luaL_error(L, "rood.timer: %s seconds must be %s", repeat ? "every's" : "after's",
            repeat ? "at least 1/240" : "0 or more");
    Plugin* p = plugin_of(L);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    double now = real ? real_clock() : game_time;
    timers().push_back({ next_id, p->id(), p->generation, ref, real, now + seconds, repeat ? (double)seconds : 0 });
    lua_pushinteger(L, (lua_Integer)next_id++);
    return 1;
}
int api_after(lua_State* L) { return make_timer(L, false); }
int api_every(lua_State* L) { return make_timer(L, true); }
int api_cancel(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    Plugin* p = plugin_of(L);
    std::vector<Timer>& list = timers();
    for (auto it = list.begin(); it != list.end(); ++it)
        if ((lua_Integer)it->id == id && it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            list.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}
int api_clock_game(lua_State* L)
{
    lua_pushnumber(L, game_time);
    return 1;
}
int api_clock_real(lua_State* L)
{
    lua_pushnumber(L, real_clock());
    return 1;
}

/* The due timers of one clock, earliest first; a timer cancelled by an earlier one is skipped. */
void fire_timers(bool real, double now)
{
    std::vector<Timer> due;
    for (const Timer& t : timers())
        if (t.real == real && t.due <= now + 1e-9)
            due.push_back(t);
    std::sort(due.begin(), due.end(), [](const Timer& a, const Timer& b) {
        return a.due != b.due ? a.due < b.due : a.id < b.id;
    });
    for (const Timer& snapshot : due) {
        std::vector<Timer>& list = timers();
        auto it = std::find_if(list.begin(), list.end(), [&](const Timer& t) { return t.id == snapshot.id; });
        if (it == list.end())
            continue;
        Plugin* p;
        if (!callable(it->plugin, it->generation, &p))
            continue;
        int ref = it->ref;
        bool once = it->interval <= 0;
        if (once)
            list.erase(it);
        else {
            it->due += it->interval;
            if (it->due <= now)
                it->due = now + it->interval; /* no catching up after a long frame */
        }
        run(p, "timer", BUDGET_CALL, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            if (once)
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            lua_call(L, 0, 0);
        });
    }
}

} // namespace

int emit(const std::string& event, const Value& payload, const std::string& sender)
{
    std::vector<Handler> matching;
    for (const Handler& h : handlers())
        if (h.event == event)
            matching.push_back(h);
    return call_handlers(matching, event, payload, sender);
}

int emit_to(Plugin* p, const std::string& event, const Value& payload)
{
    std::vector<Handler> matching;
    for (const Handler& h : handlers())
        if (h.event == event && h.plugin == p->id())
            matching.push_back(h);
    return call_handlers(matching, event, payload, "");
}

void tick_events(unsigned vsync)
{
    game_time += 1.0 / 59.94;
    Value payload = Value::new_table();
    payload.set("vsync", Value::integer(vsync));
    payload.set("game_time", Value::number(game_time));
    emit("tick", payload);
    fire_timers(false, game_time);
}

void frame_events(double dt)
{
    Value payload = Value::new_table();
    payload.set("dt", Value::number(dt));
    payload.set("real_time", Value::number(real_clock()));
    emit("frame", payload);
    fire_timers(true, real_clock());
}

void drop_events(const std::string& plugin)
{
    std::vector<Handler>& list = handlers();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Handler& h) { return h.plugin == plugin; }),
        list.end());
    std::vector<Timer>& list2 = timers();
    list2.erase(std::remove_if(list2.begin(), list2.end(), [&](const Timer& t) { return t.plugin == plugin; }),
        list2.end());
}

bool has_handler(const std::string& plugin, const std::string& event)
{
    for (const Handler& h : handlers())
        if (h.plugin == plugin && h.event == event)
            return true;
    return false;
}

size_t handler_count(const std::string& plugin)
{
    return (size_t)std::count_if(handlers().begin(), handlers().end(),
        [&](const Handler& h) { return h.plugin == plugin; });
}

size_t timer_count(const std::string& plugin)
{
    return (size_t)std::count_if(timers().begin(), timers().end(), [&](const Timer& t) { return t.plugin == plugin; });
}

double game_clock() { return game_time; }
double real_clock()
{
    static double origin = -1;
    double now = now_seconds();
    if (origin < 0)
        origin = now;
    return now - origin;
}

void open_events(lua_State* L, Plugin*)
{
    set_cfunction(L, -1, "on", api_on);
    set_cfunction(L, -1, "off", api_off);
    set_cfunction(L, -1, "emit", api_emit);
    lua_newtable(L);
    set_cfunction(L, -1, "after", api_after);
    set_cfunction(L, -1, "every", api_every);
    set_cfunction(L, -1, "cancel", api_cancel);
    lock_table(L);
    lua_setfield(L, -2, "timer");
    lua_newtable(L);
    set_cfunction(L, -1, "game", api_clock_game);
    set_cfunction(L, -1, "real", api_clock_real);
    lock_table(L);
    lua_setfield(L, -2, "clock");
}

} // namespace rl
