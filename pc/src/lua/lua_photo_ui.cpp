// rood.ui (the ui.hide grant; pc/docs/Extensions.md "Photo mode foundation"; the host:
// pc/src/photo_ui.cpp): the HUD, the game's 2D layer and the developer overlay hidden for a clean frame.
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
void pc_photo_ui_hide(const char* owner, int flags) __attribute__((weak));
int pc_photo_ui_hidden(void) __attribute__((weak));
}

namespace rl {
namespace {
using photo::Callback;
using photo::fail;
using photo::owner_of;
using photo::number_field;
using photo::push_vector;
using photo::vector_field;
int ui_hide(lua_State* L)
{
    int flags = PC_PHOTO_HIDE_HUD | PC_PHOTO_HIDE_GAME2D | PC_PHOTO_HIDE_OVERLAY;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        flags = 0;
        static const struct {
            const char* key;
            int bit;
        } layers[] = { { "hud", PC_PHOTO_HIDE_HUD }, { "game2d", PC_PHOTO_HIDE_GAME2D },
            { "overlay", PC_PHOTO_HIDE_OVERLAY } };
        for (const auto& l : layers) {
            lua_getfield(L, 1, l.key);
            if (lua_toboolean(L, -1))
                flags |= l.bit;
            lua_pop(L, 1);
        }
        /* actors: "ashley", "others", "all" (or nil / "none"): characters not drawn */
        lua_getfield(L, 1, "actors");
        if (!lua_isnil(L, -1)) {
            const char* a = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
            if (!strcmp(a, "ashley"))
                flags |= PC_PHOTO_HIDE_ASHLEY;
            else if (!strcmp(a, "others"))
                flags |= PC_PHOTO_HIDE_OTHERS;
            else if (!strcmp(a, "all"))
                flags |= PC_PHOTO_HIDE_ASHLEY | PC_PHOTO_HIDE_OTHERS;
            else if (strcmp(a, "none"))
                return luaL_error(L, "rood.ui.hide: actors '%s' (ashley, others, all, none)", a);
        }
        lua_pop(L, 1);
        lua_pushnil(L);
        while (lua_next(L, 1)) {
            lua_pop(L, 1);
            const char* k = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
            if (strcmp(k, "hud") && strcmp(k, "game2d") && strcmp(k, "overlay") && strcmp(k, "actors"))
                return luaL_error(L, "rood.ui.hide: unknown layer '%s' (hud, game2d, overlay, actors)", k);
        }
    }
    if (pc_photo_ui_hide)
        pc_photo_ui_hide(owner_of(L), flags);
    lua_pushboolean(L, 1);
    return 1;
}
int ui_show(lua_State* L)
{
    if (pc_photo_ui_hide)
        pc_photo_ui_hide(owner_of(L), 0);
    return 0;
}
int ui_hidden(lua_State* L)
{
    int flags = pc_photo_ui_hidden ? pc_photo_ui_hidden() : 0;
    lua_createtable(L, 0, 3);
    lua_pushboolean(L, (flags & PC_PHOTO_HIDE_HUD) != 0);
    lua_setfield(L, -2, "hud");
    lua_pushboolean(L, (flags & PC_PHOTO_HIDE_GAME2D) != 0);
    lua_setfield(L, -2, "game2d");
    lua_pushboolean(L, (flags & PC_PHOTO_HIDE_OVERLAY) != 0);
    lua_setfield(L, -2, "overlay");
    int actors = flags & (PC_PHOTO_HIDE_ASHLEY | PC_PHOTO_HIDE_OTHERS);
    if (actors) {
        lua_pushstring(L, actors == PC_PHOTO_HIDE_ASHLEY ? "ashley" : actors == PC_PHOTO_HIDE_OTHERS ? "others" : "all");
        lua_setfield(L, -2, "actors");
    }
    return 1;
}

void open_ui(lua_State* L, Plugin* p)
{
    if (!granted(p, "ui.hide"))
        return;
    lua_newtable(L);
    set_cfunction(L, -1, "hide", ui_hide);
    set_cfunction(L, -1, "show", ui_show);
    set_cfunction(L, -1, "hidden", ui_hidden);
    lock_table(L);
    lua_setfield(L, -2, "ui");
}

struct Register {
    Register() { api_openers().push_back(open_ui); }
} register_ui;
} // namespace
} // namespace rl
