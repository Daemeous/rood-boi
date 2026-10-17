/* Force-included into the vendored Lua core (-include; build_boot.py,
 * CMakeLists.txt; pc/third_party/lua/ROOD-NOTES.md): the budget checks of
 * the C loops a plugin can make long (the pattern matcher, plain find,
 * table.insert / remove / move / sort). Lua is compiled as C++, so this is
 * a C++ declaration. */
#ifndef ROOD_LUAUSER_H
#define ROOD_LUAUSER_H
#include <stddef.h>
struct lua_State;
void rood_lua_work(struct lua_State* L, size_t units);
#define luai_patternstep(L) rood_lua_work(L, 1)
#define luai_cwork(L, n) rood_lua_work(L, n)
#endif
