// rood.game (pc/docs/Extensions.md "Queries"; plan
// pc/docs/plans/2026-10-18-lua-visible-layer.md, binding layer 3): read-only
// game state as Lua tables, for logic and for draw functions.
//   rood.game.ashley()        nil, or {hp, max_hp, mp, max_mp, risk, str, int, agl,
//                             position = {x, y, z}, facing, battle, statuses = {{name,
//                             buff, seconds}, ...}}
//   rood.game.mode()          "title", "attract", "cutscene", "menu", "battle", "field",
//                             or "none" (no room: loading, another overlay)
//   rood.game.room()          nil, or {zone, room, area, name}
//   rood.game.first_person()  boolean
//   rood.game.paused()        the photo mode's pause holds (or is asked for); pause() and
//                             resume() with the game.pause grant (lua_api_photo.cpp)
//   rood.game.inventory(), weapons(), shields(), armour(), accessories(), equipment(),
//   workshop(), labels(), combine(a, b), craftable()   the player state (lua_api_player.cpp)
// The game's records are copied (hud/query_bridge.c) at most once per game vsync, on
// the first query; each plugin gets one read-only table per query per vsync (the
// same table until the next vsync: compare with ==, no churn in a draw function).
#include "rl_internal.h"
#include "hud/game_query.h"

#include <cstring>

extern "C" int pc_game_phase __attribute__((weak));          // boot_runtime.c: 1 title, 2 attract demo
extern "C" int pc_ui_menu_active(void) __attribute__((weak)); // ui/menu_bridge.c: the modern menus

namespace rl {
namespace {
pc_game_query state;
unsigned long long ticks = 1; // query generations (a tick hook moves it on)
unsigned long long filled;    // the generation `state` holds
struct Names {
    int zone = -1, room = -1;
    bool found = false;
    char area[64] = "", name[96] = "";
} names;

const pc_game_query& now()
{
    if (filled != ticks) {
        if (pc_game_query_fill)
            pc_game_query_fill(&state);
        else {
            std::memset(&state, 0, sizeof state);
            state.zone = state.room = -1;
        }
        filled = ticks;
    }
    return state;
}

void set_int(lua_State* L, const char* k, lua_Integer v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_str(lua_State* L, const char* k, const char* v)
{
    lua_pushstring(L, v);
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v)
{
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}

// This vsync's table for `key` on top (true), or false with nothing pushed: made by
// build() (which leaves a table, or nil, on top) once per vsync, read-only, kept in the
// plugin's registry.
bool cached(lua_State* L, const char* key)
{
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        bool fresh = (unsigned long long)lua_tointeger(L, -1) == ticks;
        lua_pop(L, 1);
        if (fresh) {
            lua_rawgeti(L, -1, 2);
            lua_remove(L, -2);
            return true;
        }
    }
    lua_pop(L, 1);
    return false;
}
void keep(lua_State* L, const char* key)
{
    lua_createtable(L, 2, 0);
    lua_pushinteger(L, (lua_Integer)ticks);
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, 2);
    lua_setfield(L, LUA_REGISTRYINDEX, key);
}

int api_ashley(lua_State* L)
{
    if (cached(L, "rood.game.ashley"))
        return 1;
    const pc_game_query& s = now();
    if (!s.ashley) {
        lua_pushnil(L);
    } else {
        lua_createtable(L, 0, 14);
        set_int(L, "hp", s.hp);
        set_int(L, "max_hp", s.max_hp);
        set_int(L, "mp", s.mp);
        set_int(L, "max_mp", s.max_mp);
        set_int(L, "risk", s.risk);
        set_int(L, "str", s.str);
        set_int(L, "int", s.intel);
        set_int(L, "agl", s.agl);
        set_int(L, "facing", s.facing);
        set_bool(L, "battle", s.battle != 0);
        lua_createtable(L, 0, 3);
        set_int(L, "x", s.x);
        set_int(L, "y", s.y);
        set_int(L, "z", s.z);
        lock_table(L);
        lua_setfield(L, -2, "position");
        lua_createtable(L, s.status_count, 0);
        for (int i = 0; i < s.status_count && i < PC_GAME_STATUSES; ++i) {
            lua_createtable(L, 0, 3);
            set_str(L, "name", s.statuses[i].name);
            set_bool(L, "buff", s.statuses[i].buff != 0);
            set_int(L, "seconds", s.statuses[i].seconds);
            lock_table(L);
            lua_rawseti(L, -2, i + 1);
        }
        lock_table(L);
        lua_setfield(L, -2, "statuses");
        lock_table(L);
    }
    keep(L, "rood.game.ashley");
    return 1;
}

const char* mode_of(const pc_game_query& s)
{
    if (&pc_game_phase && pc_game_phase == 1)
        return "title";
    if (&pc_game_phase && pc_game_phase == 2)
        return "attract";
    if (!s.field)
        return "none";
    if (s.event)
        return "cutscene";
    if (s.menu || (pc_ui_menu_active && pc_ui_menu_active()))
        return "menu";
    return s.ashley && s.battle ? "battle" : "field";
}

int api_mode(lua_State* L)
{
    lua_pushstring(L, mode_of(now()));
    return 1;
}

int api_room(lua_State* L)
{
    if (cached(L, "rood.game.room"))
        return 1;
    const pc_game_query& s = now();
    if (!s.field || s.zone < 0) {
        lua_pushnil(L);
    } else {
        if (names.zone != s.zone || names.room != s.room) {
            names = Names();
            names.zone = s.zone, names.room = s.room;
            names.found = pc_game_query_room_names
                && pc_game_query_room_names(s.zone, s.room, names.area, sizeof names.area, names.name,
                    sizeof names.name);
        }
        lua_createtable(L, 0, 4);
        set_int(L, "zone", s.zone);
        set_int(L, "room", s.room);
        set_str(L, "area", names.area);
        set_str(L, "name", names.name);
        lock_table(L);
    }
    keep(L, "rood.game.room");
    return 1;
}

int api_first_person(lua_State* L)
{
    lua_pushboolean(L, now().first_person != 0);
    return 1;
}

void open_game(lua_State* L, Plugin* p)
{
    lua_newtable(L);
    set_cfunction(L, -1, "ashley", api_ashley);
    set_cfunction(L, -1, "mode", api_mode);
    set_cfunction(L, -1, "room", api_room);
    set_cfunction(L, -1, "first_person", api_first_person);
    open_game_pause(L, p); /* paused; pause and resume with the game.pause grant (lua_api_photo.cpp) */
    open_game_player(L);   /* the bag, equipment, workshop, combine (lua_api_player.cpp) */
    lock_table(L);
    lua_setfield(L, -2, "game");
}

void next_tick() { ++ticks; }

struct Register {
    Register()
    {
        api_openers().push_back(open_game);
        tick_hooks().push_back(next_tick);
    }
} register_game;
} // namespace
} // namespace rl
