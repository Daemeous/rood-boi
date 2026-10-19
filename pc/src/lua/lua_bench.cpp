// The scenario bench's gallery (plan pc/docs/plans/2026-10-18-lua-visible-layer.md "Gallery";
// pc/docs/Extensions.md "HUD"): a moment saved and summoned again.
//   rood.dev.bench.save(name)   the game's save state at the next vsync boundary (RPC snapshot's)
//                               as <bench>/<name>.snap, and beside it <name>.lua: a Lua table of what
//                               the save state does not hold (the held rood.dev.show moments: the
//                               host's), the room, when
//   rood.dev.bench.list()       the entries: {name, saved, zone, room, area, room_name, held = {kinds},
//                               valid, why}; valid false when this build cannot resume its state
//                               (stale after a game-code rebuild; Lua edits keep it valid)
//   rood.dev.bench.load(name)   restarts the process resuming it (RPC restart {snapshot}'s way: the
//                               same command line and environment, so the RPC socket and the memory
//                               cards stay), then holds its moments again
//   rood.dev.bench.current()    the entry this process resumed (nil)
// <bench> is ROOD_BENCH, else pc/user/bench. The Lua console's Bench section and RPC hud {action
// "bench", op save / list / load, name} use the same. An entry is evaluated as a plugin manifest is:
// a table constructor in a state with no libraries.
#include "lua_bench.h"
#include "hud/game_query.h"
#include "light_prefabs.h"
#include "platform.h" /* pc_setenv */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

extern "C" int pc_rpc_bench_save(const char* path) __attribute__((weak));       // dev_rpc.c
extern "C" int pc_rpc_bench_restart(const char* path) __attribute__((weak));    // dev_rpc.c
extern "C" int pc_snapshot_check(const char* path, char* why, int size) __attribute__((weak)); // snapshot.c

namespace rl {

std::string bench_dir()
{
    const char* dir = std::getenv("ROOD_BENCH");
    return dir && *dir ? dir : "pc/user/bench";
}

namespace {
std::string current_entry; // the entry this process resumed

bool name_ok(const std::string& name, std::string* why)
{
    bool ok = !name.empty() && name.size() <= 48;
    for (char c : name)
        ok = ok && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
    if (!ok && why)
        *why = "a name is 1 to 48 of a-z, 0-9, - and _";
    return ok;
}

std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out += '\\', out += (char)c;
        else if (c < 32) {
            char e[8];
            std::snprintf(e, sizeof e, "\\%03d", c);
            out += e;
        } else
            out += (char)c;
    }
    return out + "\"";
}

// An entry file (a table constructor) as a Value, in a throwaway state with no libraries.
bool read_entry(const std::string& path, Value* out, std::string* why)
{
    std::string code;
    if (!read_file(path, &code, 64 << 10, why))
        return false;
    Plugin temp;
    temp.manifest.id = "@bench";
    temp.console = true;
    temp.mem.cap = 1u << 20;
    temp.L = new_state(&temp);
    if (!temp.L) {
        *why = "cannot create a state";
        return false;
    }
    std::string chunk = "@" + path;
    bool ok = run(&temp, "bench entry", BUDGET_TEST, [&](lua_State* L) {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 1);
        *out = Value::from_lua(L, -1);
    }, why);
    close_state(&temp);
    return ok && out->kind == Value::TABLE;
}

long long int_of(const Value* v, long long fallback)
{
    return !v ? fallback : v->kind == Value::INTEGER ? v->i : v->kind == Value::NUMBER ? (long long)v->n : fallback;
}
std::string str_of(const Value* v) { return v && v->kind == Value::STRING ? v->s : ""; }
} // namespace

std::vector<BenchEntry> bench_list()
{
    std::vector<BenchEntry> out;
    std::string dir = bench_dir();
    for (const std::string& file : list_dir(dir)) {
        if (file.size() < 5 || file.compare(file.size() - 4, 4, ".lua"))
            continue;
        BenchEntry e;
        e.name = file.substr(0, file.size() - 4);
        if (!name_ok(e.name, nullptr))
            continue;
        e.entry = dir + "/" + file;
        e.snap = dir + "/" + e.name + ".snap";
        Value v;
        std::string why;
        if (!read_entry(e.entry, &v, &why)) {
            e.why = "unreadable entry: " + why;
            out.push_back(e);
            continue;
        }
        e.saved = str_of(v.get("saved"));
        e.zone = (int)int_of(v.get("zone"), -1), e.room = (int)int_of(v.get("room"), -1);
        e.area = str_of(v.get("area")), e.room_name = str_of(v.get("room_name"));
        if (const Value* held = v.get("held"))
            for (const auto& item : held->table) {
                const Value& h = item.second;
                int kind = hud::kind_of(str_of(h.get("kind")).c_str());
                if (h.kind != Value::TABLE || !kind)
                    continue;
                hud::HeldMoment m{ kind, hud::ShowArgs() };
                m.args.timing = (int)int_of(h.get("timing"), 3), m.args.variant = (int)int_of(h.get("variant"), 0);
                m.args.age = (int)int_of(h.get("age"), 10), m.args.value = (int)int_of(h.get("value"), -1);
                m.args.sign = (int)int_of(h.get("sign"), 0), m.args.pool = (int)int_of(h.get("pool"), 0);
                m.args.stat = (int)int_of(h.get("stat"), 0), m.args.slot = (int)int_of(h.get("slot"), 0);
                std::snprintf(m.args.text, sizeof m.args.text, "%s", str_of(h.get("text")).c_str());
                e.held.push_back(m);
            }
        char reason[256] = "save states unavailable in this build";
        e.valid = pc_snapshot_check && pc_snapshot_check(e.snap.c_str(), reason, (int)sizeof reason);
        e.why = e.valid ? "" : reason;
        out.push_back(e);
    }
    return out;
}

bool bench_save(const std::string& name, std::string* path, std::string* why)
{
    if (!name_ok(name, why))
        return false;
    if (!pc_rpc_bench_save) {
        *why = "save states unavailable in this build";
        return false;
    }
    std::string dir = bench_dir(), snap = dir + "/" + name + ".snap";
    if (!make_dirs(dir)) {
        *why = "cannot make " + dir;
        return false;
    }
    pc_game_query q = {};
    q.zone = q.room = -1;
    if (pc_game_query_fill)
        pc_game_query_fill(&q);
    char area[64] = "", room[96] = "";
    if (q.zone >= 0 && pc_game_query_room_names)
        pc_game_query_room_names(q.zone, q.room, area, sizeof area, room, sizeof room);
    char saved[32];
    std::time_t now = std::time(nullptr);
    std::strftime(saved, sizeof saved, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::string text = "-- rood.dev.bench: what " + name + ".snap (the game's save state) does not hold.\nreturn {\n"
        "  name = " + quote(name) + ",\n  saved = " + quote(saved) + ",\n  vsync = " + std::to_string(hud::vsync())
        + ",\n  zone = " + std::to_string(q.zone) + ", room = " + std::to_string(q.room) + ",\n  area = "
        + quote(area) + ", room_name = " + quote(room) + ",\n  held = {\n";
    hud::HeldMoment held[PC_HUD_KINDS];
    int n = hud::held_moments(held, PC_HUD_KINDS);
    for (int i = 0; i < n; ++i) {
        const hud::ShowArgs& a = held[i].args;
        text += "    { kind = " + quote(hud::kind_name(held[i].kind)) + ", timing = " + std::to_string(a.timing)
            + ", variant = " + std::to_string(a.variant) + ", age = " + std::to_string(a.age) + ", value = "
            + std::to_string(a.value) + ", sign = " + std::to_string(a.sign) + ", pool = " + std::to_string(a.pool)
            + ", stat = " + std::to_string(a.stat) + ", slot = " + std::to_string(a.slot) + ", text = "
            + quote(a.text) + " },\n";
    }
    text += "  },\n}\n";
    if (!pc_rpc_bench_save(snap.c_str())) {
        *why = "a save state is being written: try again at the next vsync";
        return false;
    }
    if (!write_file(dir + "/" + name + ".lua", text, why))
        return false;
    log(LOG_INFO, "", "bench: " + name + " saved at the next vsync (" + snap + ")");
    *path = snap;
    return true;
}

bool bench_load(const std::string& name, std::string* why)
{
    if (!name_ok(name, why))
        return false;
    for (const BenchEntry& e : bench_list()) {
        if (e.name != name)
            continue;
        if (!e.valid) {
            *why = name + ": " + e.why;
            return false;
        }
        if (!pc_rpc_bench_restart || !pc_rpc_bench_restart(e.snap.c_str())) {
            *why = "cannot restart now (a save state is being written)";
            return false;
        }
        pc_setenv("ROOD_BENCH_ENTRY", e.entry.c_str()); // (the new process holds its moments again)
        log(LOG_INFO, "", "bench: restarting into " + name);
        return true;
    }
    *why = "no bench entry '" + name + "' in " + bench_dir();
    return false;
}

const std::string& bench_current() { return current_entry; }

// A restarted process (ROOD_BENCH_ENTRY): once the field is there (or after a while: away from
// it), the entry's moments are held again.
void bench_resume_tick()
{
    static int waited = -1;
    const char* path = std::getenv("ROOD_BENCH_ENTRY");
    if (!path || !*path)
        return;
    if (++waited < 120 && !(pc_hud_bridge_field && pc_hud_bridge_field()))
        return;
    std::string entry = path;
    pc_unsetenv("ROOD_BENCH_ENTRY");
    for (const BenchEntry& e : bench_list())
        if (e.entry == entry) {
            for (const hud::HeldMoment& m : e.held)
                hud::adopt(m.kind, m.args);
            current_entry = e.name;
            log(LOG_INFO, "", "bench: resumed " + e.name + " (" + std::to_string(e.held.size()) + " held)");
        }
}

namespace {
void push_entry(lua_State* L, const BenchEntry& e)
{
    lua_createtable(L, 0, 10);
    auto str = [&](const char* k, const std::string& v) {
        lua_pushstring(L, v.c_str());
        lua_setfield(L, -2, k);
    };
    str("name", e.name);
    str("saved", e.saved);
    lua_pushinteger(L, e.zone);
    lua_setfield(L, -2, "zone");
    lua_pushinteger(L, e.room);
    lua_setfield(L, -2, "room");
    str("area", e.area);
    str("room_name", e.room_name);
    lua_createtable(L, (int)e.held.size(), 0);
    for (size_t i = 0; i < e.held.size(); ++i) {
        lua_pushstring(L, hud::kind_name(e.held[i].kind));
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    lua_setfield(L, -2, "held");
    lua_pushboolean(L, e.valid);
    lua_setfield(L, -2, "valid");
    str("why", e.why);
}

int api_save(lua_State* L)
{
    std::string name = luaL_checkstring(L, 1), path, why;
    if (!bench_save(name, &path, &why))
        return luaL_error(L, "rood.dev.bench.save: %s", why.c_str());
    lua_pushstring(L, path.c_str());
    return 1;
}
int api_list(lua_State* L)
{
    std::vector<BenchEntry> list = bench_list();
    lua_createtable(L, (int)list.size(), 0);
    for (size_t i = 0; i < list.size(); ++i) {
        push_entry(L, list[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}
int api_load(lua_State* L)
{
    std::string why;
    if (!bench_load(luaL_checkstring(L, 1), &why))
        return luaL_error(L, "rood.dev.bench.load: %s", why.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int api_current(lua_State* L)
{
    if (current_entry.empty())
        lua_pushnil(L);
    else
        lua_pushstring(L, current_entry.c_str());
    return 1;
}
} // namespace

void open_bench(lua_State* L)
{
    lua_newtable(L);
    set_cfunction(L, -1, "save", api_save);
    set_cfunction(L, -1, "list", api_list);
    set_cfunction(L, -1, "load", api_load);
    set_cfunction(L, -1, "current", api_current);
    lock_table(L);
    lua_setfield(L, -2, "bench");
}

std::string bench_rpc(const std::string& op, const std::string& name, std::string* error)
{
    if (op == "save") {
        std::string path;
        return bench_save(name, &path, error) ? "{\"saving\":" + prefab::json_quote(path) + "}" : "";
    }
    if (op == "load")
        return bench_load(name, error) ? "{\"restarting\":" + prefab::json_quote(name) + "}" : "";
    if (op != "list") {
        *error = "bench op: save, list or load";
        return "";
    }
    std::string out = "{\"dir\":" + prefab::json_quote(bench_dir()) + ",\"current\":"
        + (current_entry.empty() ? "null" : prefab::json_quote(current_entry)) + ",\"entries\":[";
    bool first = true;
    for (const BenchEntry& e : bench_list()) {
        std::string held;
        for (const hud::HeldMoment& m : e.held)
            held += (held.empty() ? "" : ",") + prefab::json_quote(hud::kind_name(m.kind));
        out += (first ? "" : ",") + std::string("{\"name\":") + prefab::json_quote(e.name) + ",\"saved\":"
            + prefab::json_quote(e.saved) + ",\"zone\":" + std::to_string(e.zone) + ",\"room\":"
            + std::to_string(e.room) + ",\"room_name\":" + prefab::json_quote(e.room_name) + ",\"held\":[" + held
            + "],\"valid\":" + (e.valid ? "true" : "false") + ",\"why\":" + prefab::json_quote(e.why) + "}";
        first = false;
    }
    return out + "]}";
}

} // namespace rl
