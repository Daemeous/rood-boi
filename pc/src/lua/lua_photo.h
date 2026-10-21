// The photo mode foundation in Lua, what its files share (lua_api_photo.cpp: rood.game.pause and the
// host's notices; lua_photo_camera.cpp, lua_photo_post.cpp, lua_photo_ui.cpp, lua_photo_capture.cpp,
// lua_photo_input.cpp: one capability each, its own opener, unload and poll hooks). The host
// (pc/src/photo.h, post.h) is reached through weak symbols: lua_probe gives a fake one.
#ifndef ROOD_LUA_PHOTO_H
#define ROOD_LUA_PHOTO_H
#include "rl_internal.h"
#include "photo.h"

#include <string>
#include <vector>

namespace rl {
namespace photo {
/* A function of a plugin's kept for later: an input handler, a capture's done. */
struct Callback {
    std::string plugin;
    unsigned generation;
    int ref;
    int id; /* a capture's */
};
int fail(lua_State* L, const char* why); /* nil, why */
bool live(const Callback& c, Plugin** out);
/* Calls a kept function in its plugin with one value; false when it raised (a fault of the plugin's). */
bool call_back(const Callback& c, const Value& arg, const char* what);
const char* owner_of(lua_State* L); /* the calling plugin's id */
float number_field(lua_State* L, int t, const char* k, bool* found);
/* {x, y, z} or {x =, y =, z =} at field k of table t: true and out. */
bool vector_field(lua_State* L, int t, const char* k, float out[3]);
void push_vector(lua_State* L, const float v[3]);
/* Called when the host took everything of an owner back (the safety, an engine reload). */
std::vector<void (*)(const std::string& owner)>& released_hooks();
} // namespace photo
} // namespace rl
#endif
