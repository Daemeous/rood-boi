// The photo mode foundation in Lua (pc/docs/Extensions.md "Photo mode foundation"; the host:
// pc/src/photo.h, photo.cpp): rood.game.pause / resume (the game.pause grant; rood.game.paused always),
// what its files share (lua_photo.h), the host's notices (what it took back without the plugin asking:
// the "photo.released" event), and everything given back when a plugin unloads (an edit, a revoked
// grant, turned off, disabled for its faults). One file per capability, each existing only when granted
// (declared in plugin.lua; a shipped plugin has what it declares):
//
//   game.pause     rood.game.pause() / resume()        (rood.game.paused() always)   here
//   camera         rood.camera.begin / finish / get / set / orbit / move / active      lua_photo_camera.cpp
//   post           rood.post.list / get / set / reset / overrides                      lua_photo_post.cpp
//   ui.hide        rood.ui.hide{hud, game2d, overlay} / show / hidden                  lua_photo_ui.cpp
//   capture        rood.capture.photo{scale, path, done} / status                      lua_photo_capture.cpp
//   input.capture  rood.input.capture(fn) / release / state / captured                lua_photo_input.cpp
//
// Everything a plugin holds is its own (stacked per plugin: the pause holds while any plugin holds
// it, hidden layers add up, post overrides apply in the order the plugins began). Nothing reaches
// settings.cfg or the game's state (photo.cpp: a paused game's renders are rewound).
#include "lua_photo.h"

#include <cstring>
#include <string>
#include <vector>

extern "C" {
int pc_photo_pause(const char* owner, char* why, int size) __attribute__((weak));
void pc_photo_resume(const char* owner) __attribute__((weak));
int pc_photo_paused(void) __attribute__((weak));
void pc_photo_release(const char* owner) __attribute__((weak));
int pc_photo_notice_next(char* owner, int owner_size, char* what, int what_size, char* reason, int reason_size)
    __attribute__((weak));
int pc_photo_action_next(char* name, int name_size, char* device, int device_size) __attribute__((weak));
}

namespace rl {
int photo::fail(lua_State* L, const char* why)
{
    luaL_pushfail(L);
    lua_pushstring(L, why);
    return 2;
}
bool photo::live(const Callback& c, Plugin** out)
{
    Plugin* p = find(c.plugin);
    if (!p || !p->L || p->state != PLUGIN_LOADED || p->generation != c.generation)
        return false;
    *out = p;
    return true;
}
const char* photo::owner_of(lua_State* L) { return plugin_of(L)->id().c_str(); }
float photo::number_field(lua_State* L, int t, const char* k, bool* found)
{
    lua_getfield(L, t, k);
    *found = !lua_isnil(L, -1);
    float v = *found ? (float)luaL_checknumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    return v;
}
bool photo::vector_field(lua_State* L, int t, const char* k, float out[3])
{
    lua_getfield(L, t, k);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    luaL_argcheck(L, lua_istable(L, -1), t, (std::string(k) + ": a {x, y, z}").c_str());
    static const char* const names[3] = { "x", "y", "z" };
    for (int i = 0; i < 3; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_getfield(L, -1, names[i]);
        }
        if (!lua_isnumber(L, -1))
            luaL_error(L, "%s: a {x, y, z} of numbers", k);
        out[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}
void photo::push_vector(lua_State* L, const float v[3])
{
    lua_createtable(L, 0, 3);
    static const char* const names[3] = { "x", "y", "z" };
    for (int i = 0; i < 3; i++) {
        lua_pushnumber(L, v[i]);
        lua_setfield(L, -2, names[i]);
    }
}

bool photo::call_back(const Callback& c, const Value& arg, const char* what)
{
    Plugin* p;
    if (!live(c, &p))
        return true;
    return run(p, what, BUDGET_CALL, [&](lua_State* L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, c.ref);
        push(L, arg);
        lua_call(L, 1, 0);
    });
}

std::vector<void (*)(const std::string&)>& photo::released_hooks()
{
    static auto* list = new std::vector<void (*)(const std::string&)>();
    return *list;
}

namespace {
using photo::Callback;
using photo::fail;
using photo::owner_of;

/* ------------------------------------------------------------ rood.game */

int game_pause(lua_State* L)
{
    char why[256] = "";
    if (!pc_photo_pause)
        return fail(L, "no photo mode in this build");
    if (pc_photo_pause(owner_of(L), why, (int)sizeof why))
        return fail(L, why);
    lua_pushboolean(L, 1);
    return 1;
}
int game_resume(lua_State* L)
{
    if (pc_photo_resume)
        pc_photo_resume(owner_of(L));
    return 0;
}
int game_paused(lua_State* L)
{
    lua_pushboolean(L, pc_photo_paused && pc_photo_paused());
    return 1;
}

/* Every tick and frame: what the host took back, to its owner; and the port's action buttons pressed (the
 * photo mode's: bind_key_photo / bind_pad_photo), to every plugin: "input.action" {name, device}. */
void poll_notices()
{
    char name[24], device[12];
    while (pc_photo_action_next && pc_photo_action_next(name, (int)sizeof name, device, (int)sizeof device)) {
        Value v = Value::new_table();
        v.set("name", Value::str(name));
        v.set("device", Value::str(device));
        emit("input.action", v);
    }
    if (!pc_photo_notice_next)
        return;
    char owner[128], what[32], reason[160];
    while (pc_photo_notice_next(owner, (int)sizeof owner, what, (int)sizeof what, reason, (int)sizeof reason)) {
        if (!strcmp(what, "everything"))
            for (auto hook : photo::released_hooks())
                hook(owner);
        Plugin* p = find(owner);
        if (!p || p->state != PLUGIN_LOADED)
            continue;
        Value v = Value::new_table();
        v.set("what", Value::str(what));
        v.set("reason", Value::str(reason));
        emit_to(p, "photo.released", v);
    }
}

/* An unload: everything the host holds for it (the pause, the camera, hidden layers, the format, captures,
 * the input); the capability files drop their own (post overrides, kept functions). */
void drop_pause(Plugin* p)
{
    if (pc_photo_release)
        pc_photo_release(p->id().c_str());
}

struct Register {
    Register()
    {
        unload_hooks().push_back(drop_pause);
        poll_hooks().push_back(poll_notices);
    }
} register_pause;
} // namespace

void open_game_pause(lua_State* L, Plugin* p)
{
    set_cfunction(L, -1, "paused", game_paused);
    if (granted(p, "game.pause")) {
        set_cfunction(L, -1, "pause", game_pause);
        set_cfunction(L, -1, "resume", game_resume);
    }
}
} // namespace rl
