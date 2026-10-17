# Lua in rood-boi

Lua 5.4.8 runs plugins (pc/docs/Extensions.md). It is compiled as C++
(pc/tools/build_boot.py, pc/CMakeLists.txt), so a Lua error is a C++
exception; include lua.h, lauxlib.h and lualib.h, never lua.hpp.

## Compiled

The core (lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject
lopcodes lparser lstate lstring ltable ltm lundump lvm lzio), lauxlib and
the safe libraries (lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib).

Not compiled (vendored only so that an upgrade is a plain copy): liolib.c,
loslib.c, loadlib.c, ldblib.c, linit.c, lua.c, luac.c. A plugin cannot
reach what is not linked.

## Local changes

Every changed line is marked "rood-boi:". Reapply them on upgrade.

- lstrlib.c, match(): `luai_patternstep(ms->L)` at the `init:` label, so
  every step of the pattern matcher (which runs in C, where the instruction
  hook never fires) can check the plugin's time budget.
- lstrlib.c, lmemfind() (plain `string.find`): takes `L` and calls
  `luai_cwork(L, l2 / 64 + 1)` before each compare (a long near-match made
  it O(n * m): hours).
- lstrlib.c, str_rep(): an empty string with an empty separator gives ""
  at once (`string.rep("", math.maxinteger)` looped 2^63 times).
- ltablib.c: `luai_cwork(L, 1)` in the loops of tinsert, tremove and tmove
  and in sort_comp (weighted by a string's length there) (a huge range or a `__len` of 2^40 looped for hours
  without a Lua instruction; sorting many copies of a long string too).

Both macros are empty unless pc/src/lua/rood_luauser.h is force-included
(-include), which makes them rood_lua_work (pc/src/lua/lua_limits.cpp): it
reads the clock every 1024 units of work and raises "budget exceeded".
