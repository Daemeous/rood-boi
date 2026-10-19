// The scenario bench's gallery (lua_bench.cpp): rood.dev.bench, the Lua console's Bench section,
// RPC hud {action "bench"}. Game thread only.
#pragma once
#include "hud/hud_internal.h"
#include "rl_internal.h"

#include <string>
#include <vector>

namespace rl {
struct BenchEntry {
    std::string name, entry, snap; // its <name>.lua and <name>.snap
    std::string saved, area, room_name;
    int zone = -1, room = -1;
    std::vector<hud::HeldMoment> held;
    bool valid = false; // this build resumes its save state
    std::string why;    // when not
};
std::string bench_dir(); // ROOD_BENCH, else pc/user/bench
std::vector<BenchEntry> bench_list();
// Saved at the next vsync boundary; *path: its save state.
bool bench_save(const std::string& name, std::string* path, std::string* why);
// The process restarts into it at the next poll.
bool bench_load(const std::string& name, std::string* why);
const std::string& bench_current();
void bench_resume_tick();     // each tick: a restarted process holds its entry's moments again
void open_bench(lua_State* L); // rood.dev.bench, into the table on top
std::string bench_rpc(const std::string& op, const std::string& name, std::string* error); // "" and *error
} // namespace rl
