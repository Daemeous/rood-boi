// rood.capture (the capture grant; pc/docs/Extensions.md "Photo mode foundation"; the host:
// pc/src/photo_capture.cpp): a clean PNG of the presented picture in the photos folder (or a folder the
// plugin's fs.write grant names), told when written.
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
int pc_photo_capture(const char* owner, const char* path, int scale, char* path_out, int path_size, char* why,
    int why_size) __attribute__((weak));
int pc_photo_capture_result_get(int id, pc_photo_capture_result* out) __attribute__((weak));
const char* pc_photo_dir(void) __attribute__((weak));
}

namespace rl {
namespace {
using photo::Callback;
using photo::fail;
using photo::owner_of;
using photo::number_field;
using photo::push_vector;
using photo::vector_field;
/* Every capture a plugin asked for (its done comes once), and the done functions. */
std::vector<std::pair<std::string, int>> pending_captures;
std::vector<Callback> capture_waits;

const char* const SCALE_NOTE = "scale > 1 is not supported yet: the photo is the window's picture (scale 1), "
                               "nothing upscaled";

/* A path the plugin may write a photo to: inside a folder its fs.write grant names (the grant checked as
 * rood.fs.write checks it), a .png. */
bool photo_path_ok(Plugin* p, const std::string& path, std::string* why)
{
    if (path.size() < 5 || path.compare(path.size() - 4, 4, ".png")) {
        *why = "path: a .png file";
        return false;
    }
    for (const std::string& cap : p->granted) {
        if (cap.compare(0, 9, "fs.write:"))
            continue;
        std::string root = cap.substr(9), s = path;
        for (char& c : root)
            c = c == '\\' ? '/' : c;
        for (char& c : s)
            c = c == '\\' ? '/' : c;
        while (root.size() > 1 && root.back() == '/')
            root.pop_back();
        if (!filesystem_grant_ok(cap, why))
            continue;
        if (s.size() <= root.size() + 1 || s.compare(0, root.size(), root) || s[root.size()] != '/')
            continue;
        std::string rest = s.substr(root.size() + 1);
        if (!safe_relative(rest, why) || !inside(root, s, why))
            return false;
        return true;
    }
    *why = "path: only inside a folder the plugin's fs.write grant names (else the photos folder: no path)";
    return false;
}
int capture_photo(lua_State* L)
{
    Plugin* p = plugin_of(L);
    int scale = 1;
    std::string path;
    bool has_done = false;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_getfield(L, 1, "scale");
        if (!lua_isnil(L, -1)) {
            lua_Integer s = luaL_checkinteger(L, -1);
            luaL_argcheck(L, s >= 1 && s <= 4, 1, "scale: 1..4");
            scale = (int)s;
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "path");
        if (!lua_isnil(L, -1))
            path = luaL_checkstring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "done");
        has_done = !lua_isnil(L, -1);
        if (has_done)
            luaL_checktype(L, -1, LUA_TFUNCTION);
        lua_pop(L, 1);
    }
    std::string why;
    if (!path.empty() && !photo_path_ok(p, path, &why))
        return fail(L, why.c_str());
    if (!pc_photo_capture)
        return fail(L, "no photo mode in this build");
    char out[512] = "", reason[256] = "";
    int id = pc_photo_capture(p->id().c_str(), path.empty() ? nullptr : path.c_str(), scale, out, (int)sizeof out,
        reason, (int)sizeof reason);
    if (!id)
        return fail(L, reason);
    if (!path.empty())
        audit(p, "fs.write", path + " (photo)");
    pending_captures.emplace_back(p->id(), id);
    if (has_done) {
        lua_getfield(L, 1, "done");
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        capture_waits.push_back({ p->id(), p->generation, ref, id });
    }
    lua_pushstring(L, out);
    lua_pushinteger(L, id);
    if (scale > 1) {
        lua_pushstring(L, SCALE_NOTE);
        return 3;
    }
    return 2;
}
void push_result(lua_State* L, int id, const pc_photo_capture_result& r, int asked_scale)
{
    lua_createtable(L, 0, 9);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, r.done);
    lua_setfield(L, -2, "done");
    lua_pushboolean(L, r.ok);
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, r.path);
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, r.width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, r.height);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, r.scale ? r.scale : 1);
    lua_setfield(L, -2, "scale");
    if (asked_scale > 1) {
        lua_pushstring(L, SCALE_NOTE);
        lua_setfield(L, -2, "note");
    }
    if (r.error[0]) {
        lua_pushstring(L, r.error);
        lua_setfield(L, -2, "error");
    }
}
int capture_status(lua_State* L)
{
    int id = (int)luaL_checkinteger(L, 1);
    pc_photo_capture_result r;
    if (!pc_photo_capture_result_get || !pc_photo_capture_result_get(id, &r)) {
        lua_pushnil(L);
        return 1;
    }
    push_result(L, id, r, 1);
    return 1;
}

/* Every tick and frame: finished captures to their plugins (done, the photo.capture event). */
void poll_captures()
{
    for (size_t i = 0; i < pending_captures.size();) {
        pc_photo_capture_result r;
        int id = pending_captures[i].second;
        if (!pc_photo_capture_result_get || !pc_photo_capture_result_get(id, &r) || !r.done) {
            if (pc_photo_capture_result_get && !pc_photo_capture_result_get(id, &r))
                pending_captures.erase(pending_captures.begin() + (long)i); /* dropped (its owner let go) */
            else
                ++i;
            continue;
        }
        std::string owner = pending_captures[i].first;
        pending_captures.erase(pending_captures.begin() + (long)i);
        Value v = Value::new_table();
        v.set("id", Value::integer(id));
        v.set("ok", Value::boolean(r.ok != 0));
        v.set("path", Value::str(r.path));
        v.set("width", Value::integer(r.width));
        v.set("height", Value::integer(r.height));
        v.set("scale", Value::integer(1));
        if (r.error[0])
            v.set("error", Value::str(r.error));
        for (auto it = capture_waits.begin(); it != capture_waits.end(); ++it)
            if (it->id == id) {
                Callback c = *it;
                capture_waits.erase(it);
                call_back(c, v, "capture done");
                break;
            }
        if (Plugin* p = find(owner))
            if (p->state == PLUGIN_LOADED)
                emit_to(p, "photo.capture", v);
    }
}

void drop_capture(Plugin* p)
{
    const std::string& id = p->id();
    auto mine = [&](const Callback& c) { return c.plugin == id; }; /* (its state closes: the references go too) */
    capture_waits.erase(std::remove_if(capture_waits.begin(), capture_waits.end(), mine), capture_waits.end());
    pending_captures.erase(std::remove_if(pending_captures.begin(), pending_captures.end(),
                               [&](const auto& c) { return c.first == id; }),
        pending_captures.end());
}

void open_capture(lua_State* L, Plugin* p)
{
    if (!granted(p, "capture"))
        return;
    lua_newtable(L);
    set_cfunction(L, -1, "photo", capture_photo);
    set_cfunction(L, -1, "status", capture_status);
    lua_pushstring(L, pc_photo_dir ? pc_photo_dir() : "pc/user/photos");
    lua_setfield(L, -2, "folder");
    lock_table(L);
    lua_setfield(L, -2, "capture");
}

struct Register {
    Register()
    {
        api_openers().push_back(open_capture);
        unload_hooks().push_back(drop_capture);
        poll_hooks().push_back(poll_captures);
    }
} register_capture;
} // namespace
} // namespace rl
