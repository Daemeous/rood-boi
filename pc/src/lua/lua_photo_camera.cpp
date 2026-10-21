// rood.camera (the camera grant; pc/docs/Extensions.md "Photo mode foundation"; the host:
// pc/src/photo_camera.cpp): the editor's free camera driving the view while the game is paused.
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
int pc_photo_camera_begin(const char* owner, char* why, int size) __attribute__((weak));
void pc_photo_camera_finish(const char* owner) __attribute__((weak));
int pc_photo_camera_owner(char* out, int size) __attribute__((weak));
int pc_photo_camera_get(pc_photo_pose* out) __attribute__((weak));
int pc_photo_camera_set(const char* owner, const pc_photo_pose* pose, unsigned mask, char* why, int size)
    __attribute__((weak));
int pc_photo_camera_orbit(const char* owner, float yaw, float pitch, float zoom, char* why, int size)
    __attribute__((weak));
int pc_photo_camera_move(const char* owner, float right, float up, float forward, char* why, int size)
    __attribute__((weak));
}

namespace rl {
namespace {
using photo::Callback;
using photo::fail;
using photo::owner_of;
using photo::number_field;
using photo::push_vector;
using photo::vector_field;
int camera_begin(lua_State* L)
{
    char why[256] = "";
    if (!pc_photo_camera_begin)
        return fail(L, "no photo mode in this build");
    if (pc_photo_camera_begin(owner_of(L), why, (int)sizeof why))
        return fail(L, why);
    lua_pushboolean(L, 1);
    return 1;
}
int camera_finish(lua_State* L)
{
    if (pc_photo_camera_finish)
        pc_photo_camera_finish(owner_of(L));
    return 0;
}
int camera_active(lua_State* L)
{
    char owner[128] = "";
    lua_pushboolean(L, pc_photo_camera_owner && pc_photo_camera_owner(owner, (int)sizeof owner)
            && plugin_of(L)->id() == owner);
    return 1;
}
int camera_get(lua_State* L)
{
    pc_photo_pose pose;
    char owner[128] = "";
    if (!pc_photo_camera_get || !pc_photo_camera_owner || !pc_photo_camera_owner(owner, (int)sizeof owner)
        || plugin_of(L)->id() != owner || !pc_photo_camera_get(&pose)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 8);
    push_vector(L, pose.position);
    lua_setfield(L, -2, "position");
    push_vector(L, pose.target);
    lua_setfield(L, -2, "target");
    lua_pushnumber(L, pose.yaw);
    lua_setfield(L, -2, "yaw");
    lua_pushnumber(L, pose.pitch);
    lua_setfield(L, -2, "pitch");
    lua_pushnumber(L, pose.roll);
    lua_setfield(L, -2, "roll");
    lua_pushnumber(L, pose.fov);
    lua_setfield(L, -2, "fov");
    lua_pushboolean(L, pose.collision);
    lua_setfield(L, -2, "collision");
    return 1;
}
int camera_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    pc_photo_pose pose;
    memset(&pose, 0, sizeof pose);
    unsigned mask = 0;
    bool found;
    if (vector_field(L, 1, "position", pose.position))
        mask |= PC_POSE_POSITION;
    if (vector_field(L, 1, "target", pose.target))
        mask |= PC_POSE_TARGET;
    struct {
        const char* key;
        float* to;
        unsigned bit;
    } numbers[] = { { "yaw", &pose.yaw, PC_POSE_YAW }, { "pitch", &pose.pitch, PC_POSE_PITCH },
        { "roll", &pose.roll, PC_POSE_ROLL }, { "fov", &pose.fov, PC_POSE_FOV } };
    for (auto& n : numbers) {
        *n.to = number_field(L, 1, n.key, &found);
        if (found)
            mask |= n.bit;
    }
    lua_getfield(L, 1, "collision");
    if (!lua_isnil(L, -1)) {
        pose.collision = lua_toboolean(L, -1);
        mask |= PC_POSE_COLLISION;
    }
    lua_pop(L, 1);
    char why[256] = "";
    if (!pc_photo_camera_set)
        return fail(L, "no photo mode in this build");
    if (pc_photo_camera_set(owner_of(L), &pose, mask, why, (int)sizeof why))
        return fail(L, why);
    lua_pushboolean(L, 1);
    return 1;
}
int camera_orbit(lua_State* L)
{
    float yaw = (float)luaL_checknumber(L, 1), pitch = (float)luaL_optnumber(L, 2, 0);
    float zoom = (float)luaL_optnumber(L, 3, 1);
    char why[256] = "";
    if (!pc_photo_camera_orbit)
        return fail(L, "no photo mode in this build");
    if (pc_photo_camera_orbit(owner_of(L), yaw, pitch, zoom, why, (int)sizeof why))
        return fail(L, why);
    lua_pushboolean(L, 1);
    return 1;
}
int camera_move(lua_State* L)
{
    float right = (float)luaL_checknumber(L, 1), up = (float)luaL_optnumber(L, 2, 0);
    float ahead = (float)luaL_optnumber(L, 3, 0);
    char why[256] = "";
    if (!pc_photo_camera_move)
        return fail(L, "no photo mode in this build");
    if (pc_photo_camera_move(owner_of(L), right, up, ahead, why, (int)sizeof why))
        return fail(L, why);
    lua_pushboolean(L, 1);
    return 1;
}

void open_camera(lua_State* L, Plugin* p)
{
    if (!granted(p, "camera"))
        return;
    lua_newtable(L);
    set_cfunction(L, -1, "begin", camera_begin);
    set_cfunction(L, -1, "finish", camera_finish);
    set_cfunction(L, -1, "active", camera_active);
    set_cfunction(L, -1, "get", camera_get);
    set_cfunction(L, -1, "set", camera_set);
    set_cfunction(L, -1, "orbit", camera_orbit);
    set_cfunction(L, -1, "move", camera_move);
    lock_table(L);
    lua_setfield(L, -2, "camera");
}

struct Register {
    Register() { api_openers().push_back(open_camera); }
} register_camera;
} // namespace
} // namespace rl
