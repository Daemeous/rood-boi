// The safety nets every plugin state has (pc/docs/Extensions.md, "Budgets"):
// an allocator that counts a plugin's memory and refuses past its cap; an
// instruction and wall-clock budget per call (a count hook every
// RL_HOOK_STEP instructions; once exceeded, every following instruction
// fails, so pcall cannot swallow it); the same clock inside the C loops a
// plugin can make long: the string pattern matcher, plain find,
// table.insert / remove / move / sort (lstrlib.c, ltablib.c: luai_patternstep,
// luai_cwork; pc/third_party/lua/ROOD-NOTES.md).
#include "rl_internal.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace rl {

Config config;
unsigned long long pattern_steps;

double now_seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

void* alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    Memory* m = (Memory*)ud;
    size_t old = ptr ? osize : 0; /* without ptr, osize is a type tag */
    if (nsize == 0) {
        free(ptr);
        m->used -= old;
        return nullptr;
    }
    if (nsize > old && m->cap && m->used - old + nsize > m->cap + m->grace) {
        ++m->refusals;
        return nullptr; /* Lua raises "not enough memory" */
    }
    void* p = realloc(ptr, nsize);
    if (!p)
        return nsize <= old ? ptr : nullptr; /* Lua assumes shrinking never fails */
    m->used = m->used - old + nsize;
    if (nsize > old)
        ++m->allocations;
    if (m->used > m->peak)
        m->peak = m->used;
    return p;
}

lua_State* new_state(Plugin* p)
{
    lua_State* L = lua_newstate(alloc, &p->mem);
    if (L)
        *(Plugin**)lua_getextraspace(L) = p; /* threads created later copy it */
    return L;
}

Plugin* plugin_of(lua_State* L) { return *(Plugin**)lua_getextraspace(L); }

void budget_error(lua_State* L, const Budget& b, const char* where)
{
    char text[200];
    std::snprintf(text, sizeof text, "budget exceeded%s: %.1f ms and %lld instructions (limits %.1f ms, %lld)",
        where, (now_seconds() - b.start) * 1000.0, b.instructions, b.limit_ms, b.max_instructions);
    luaL_error(L, "%s", text);
}

void check_budget(lua_State* L)
{
    Plugin* p = plugin_of(L);
    if (!p || !p->budget.active)
        return;
    Budget& b = p->budget;
    if (b.killed || now_seconds() > b.deadline) {
        b.killed = true;
        budget_error(L, b, "");
    }
}

/* Every RL_HOOK_STEP instructions (every instruction once a call is over
 * budget, so that pcall cannot swallow the error and go on). */
static void hook(lua_State* L, lua_Debug*)
{
    Plugin* p = plugin_of(L);
    if (!p)
        return;
    Budget& b = p->budget;
    int count = lua_gethookcount(L);
    b.instructions += count;
    if (!b.active)
        return;
    if (!b.killed && (b.instructions > b.max_instructions || now_seconds() > b.deadline))
        b.killed = true;
    if (b.killed) {
        if (count != 1)
            lua_sethook(L, hook, LUA_MASKCOUNT, 1);
        if (p->L && p->L != L) /* killed in a coroutine: its resumer stops at once too */
            lua_sethook(p->L, hook, LUA_MASKCOUNT, 1);
        budget_error(L, b, "");
    }
    if (count != RL_HOOK_STEP) /* a coroutine left at 1 by an earlier call's kill */
        lua_sethook(L, hook, LUA_MASKCOUNT, RL_HOOK_STEP);
}

void begin_budget(Plugin* p, BudgetKind kind)
{
    Budget& b = p->budget;
    bool load = kind != BUDGET_CALL;
    b.active = true;
    b.killed = false;
    b.instructions = 0;
    b.limit_ms = load ? config.load_budget_ms : config.budget_ms;
    b.max_instructions = load ? config.load_budget_instructions : config.budget_instructions;
    b.start = now_seconds();
    b.deadline = b.start + b.limit_ms / 1000.0;
    lua_sethook(p->L, hook, LUA_MASKCOUNT, RL_HOOK_STEP);
}

double end_budget(Plugin* p)
{
    p->budget.active = false;
    p->mem.grace = 0;
    return (now_seconds() - p->budget.start) * 1000.0;
}

} // namespace rl

/* lstrlib.c and ltablib.c (luai_patternstep, luai_cwork): C loops where the
 * instruction hook never fires (the pattern matcher's steps, plain find's
 * compares weighted by their length, table.insert / remove / move / sort):
 * the clock is read every 1024 units of work. */
void rood_lua_work(lua_State* L, size_t units)
{
    static unsigned long long next_check = 1024;
    rl::pattern_steps += units;
    if (rl::pattern_steps < next_check)
        return;
    next_check = rl::pattern_steps + 1024;
    rl::Plugin* p = rl::plugin_of(L);
    if (!p || !p->budget.active)
        return;
    rl::Budget& b = p->budget;
    if (b.killed || b.instructions > b.max_instructions || rl::now_seconds() > b.deadline) {
        b.killed = true;
        rl::budget_error(L, b, " in a C loop (a string pattern, find, or table.insert/remove/move/sort)");
    }
}
