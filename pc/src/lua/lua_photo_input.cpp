// rood.input (the input.capture grant; pc/docs/Extensions.md "Photo mode foundation"; the host:
// pc/src/photo_input.cpp): the pad, the keyboard and the RPC's buttons to a plugin's handler instead of
// the game; Escape or Start gives them back.
#include "lua_photo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
int pc_photo_input_capture(const char* owner, int on) __attribute__((weak));
int pc_photo_input_owner(char* out, int size) __attribute__((weak));
int pc_photo_input_events(pc_photo_event* out, int max) __attribute__((weak));
void pc_photo_input_state_get(pc_photo_input_state* out) __attribute__((weak));
}

namespace rl {
namespace {
using photo::Callback;
using photo::fail;
using photo::owner_of;
using photo::number_field;
using photo::push_vector;
using photo::vector_field;
/* PS1 pad bits (boot_controller.h's order), for the held state. */
const char* const BUTTONS[16] = { "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square", "select", "l3", "r3",
    "start", "up", "right", "down", "left" };
std::vector<Callback> input_handlers;

int input_capture(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    Plugin* p = plugin_of(L);
    if (!pc_photo_input_capture)
        return fail(L, "no photo mode in this build");
    if (pc_photo_input_capture(p->id().c_str(), 1)) {
        char owner[128] = "";
        pc_photo_input_owner(owner, (int)sizeof owner);
        return fail(L, (std::string("the input belongs to ") + owner).c_str());
    }
    for (auto it = input_handlers.begin(); it != input_handlers.end();)
        if (it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            it = input_handlers.erase(it);
        } else
            ++it;
    lua_settop(L, 1);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    input_handlers.push_back({ p->id(), p->generation, ref, 0 });
    lua_pushboolean(L, 1);
    return 1;
}
int input_release_api(lua_State* L)
{
    Plugin* p = plugin_of(L);
    if (pc_photo_input_capture)
        pc_photo_input_capture(p->id().c_str(), 0);
    for (auto it = input_handlers.begin(); it != input_handlers.end();)
        if (it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            it = input_handlers.erase(it);
        } else
            ++it;
    return 0;
}
int input_captured(lua_State* L)
{
    char owner[128] = "";
    lua_pushboolean(L, pc_photo_input_owner && pc_photo_input_owner(owner, (int)sizeof owner)
            && plugin_of(L)->id() == owner);
    return 1;
}
Value state_value(const pc_photo_input_state& s)
{
    Value t = Value::new_table();
    Value held = Value::new_table();
    for (int b = 0; b < 16; b++)
        if (s.buttons & (1u << b))
            held.set(BUTTONS[b], Value::boolean(true));
    t.set("held", held);
    Value left = Value::new_table(), right = Value::new_table();
    left.set("x", Value::number(s.left[0])), left.set("y", Value::number(s.left[1]));
    right.set("x", Value::number(s.right[0])), right.set("y", Value::number(s.right[1]));
    t.set("left", left), t.set("right", right);
    t.set("l2", Value::number(s.l2)), t.set("r2", Value::number(s.r2));
    return t;
}
int input_state(lua_State* L)
{
    pc_photo_input_state s;
    memset(&s, 0, sizeof s);
    if (pc_photo_input_state_get)
        pc_photo_input_state_get(&s);
    push(L, state_value(s));
    return 1;
}

/* Calls a kept function in its plugin with one value; false when it raised. */
/* Every tick and frame: the input's events to its handler (a raising handler gives the input back). */
double last_analog;
void poll_input()
{
    if (pc_photo_input_events && !input_handlers.empty()) {
        pc_photo_event events[64];
        int n = pc_photo_input_events(events, 64);
        pc_photo_input_state s;
        memset(&s, 0, sizeof s);
        pc_photo_input_state_get(&s);
        Value base = state_value(s);
        std::vector<Value> out;
        for (int i = 0; i < n; i++) {
            const pc_photo_event& e = events[i];
            Value v = base;
            v.set("type", Value::str(e.type == PC_PHOTO_EVENT_BUTTON ? "button"
                    : e.type == PC_PHOTO_EVENT_KEY                  ? "key"
                                                                    : "released"));
            v.set(e.type == PC_PHOTO_EVENT_RELEASED ? "reason" : "name", Value::str(e.name));
            if (e.type != PC_PHOTO_EVENT_RELEASED) {
                v.set("down", Value::boolean(e.down != 0));
                v.set("device", Value::str(e.device));
            }
            out.push_back(v);
        }
        /* The sticks and triggers while they are off rest: an analog event a frame (at most 60 a second). */
        double now = now_seconds();
        bool moved = fabsf(s.left[0]) > 0.1f || fabsf(s.left[1]) > 0.1f || fabsf(s.right[0]) > 0.1f
            || fabsf(s.right[1]) > 0.1f || s.l2 > 0.05f || s.r2 > 0.05f;
        if (moved && now - last_analog > 1.0 / 60) {
            Value v = base;
            v.set("type", Value::str("analog"));
            v.set("dt", Value::number(last_analog > 0 ? fmin(now - last_analog, 0.25) : 1.0 / 60));
            out.push_back(v);
            last_analog = now;
        } else if (!moved)
            last_analog = 0;
        for (const Value& v : out) {
            if (input_handlers.empty())
                break;
            Callback c = input_handlers.front();
            bool released = v.get("type") && v.get("type")->s == "released";
            if (!call_back(c, v, "input")) { /* it raised: the input is the game's again */
                if (pc_photo_input_capture)
                    pc_photo_input_capture(c.plugin.c_str(), 0);
                input_handlers.erase(input_handlers.begin());
                log(LOG_WARN, c.plugin, "its input handler raised: the input is the game's again");
                break;
            }
            if (released && !input_handlers.empty() && input_handlers.front().plugin == c.plugin)
                input_handlers.erase(input_handlers.begin()); /* (Escape, Start) */
        }
    }
}

void drop_input(Plugin* p)
{
    const std::string& id = p->id();
    auto mine = [&](const Callback& c) { return c.plugin == id; }; /* (its state closes: the references go too) */
    input_handlers.erase(std::remove_if(input_handlers.begin(), input_handlers.end(), mine), input_handlers.end());
}

void open_input(lua_State* L, Plugin* p)
{
    if (!granted(p, "input.capture"))
        return;
    lua_newtable(L);
    set_cfunction(L, -1, "capture", input_capture);
    set_cfunction(L, -1, "release", input_release_api);
    set_cfunction(L, -1, "captured", input_captured);
    set_cfunction(L, -1, "state", input_state);
    lock_table(L);
    lua_setfield(L, -2, "input");
}

struct Register {
    Register()
    {
        api_openers().push_back(open_input);
        unload_hooks().push_back(drop_input);
        poll_hooks().push_back(poll_input);
    }
} register_input;
} // namespace
} // namespace rl
