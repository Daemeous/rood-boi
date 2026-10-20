/* The extension system's internals (pc/src/lua; pc/docs/Extensions.md).
 * Lua 5.4 (pc/third_party/lua) is compiled as C++: its errors are C++
 * exceptions, so these files may keep std::string and friends across Lua
 * calls. Include lua.h / lauxlib.h / lualib.h (here), never lua.hpp: its
 * extern "C" would not match the C++ definitions. Everything runs on the
 * game thread (rl::run checks), and a plugin's state is only touched inside
 * rl::run. */
#ifndef ROOD_RL_INTERNAL_H
#define ROOD_RL_INTERNAL_H
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define RL_VERSION "0.1.0"     /* rood.version */
#define RL_API_VERSION 1       /* rood.api_version; manifests name the one they need */
#define RL_HOOK_STEP 1000      /* instructions between two budget checks */
#define RL_MAX_FILE (4u << 20) /* a script, manifest or data file */
#define RL_MAX_LOG_LINE (16u << 10) /* a log line, an error message */
#define RL_MAX_DEPTH 8         /* folders deep: code below that is neither hashed nor loaded */
#define RL_MAX_CODE_FILES 2000 /* a plugin with more is refused (its hash would not cover them) */

namespace prefab {
struct Json;
}

namespace rl {

/* ---- Tunables: settings plugin_* (lua_plugins.cpp tunables()), RPC lua {action "budget"}, the console ---- */
struct Config {
    double budget_ms = 4;                   /* one handler, timer or call between plugins */
    long long budget_instructions = 2000000;
    double load_budget_ms = 250;            /* a main chunk, a test step, an eval */
    long long load_budget_instructions = 50000000;
    double memory_mb = 32;                  /* each plugin's cap */
    int fault_limit = 5;                    /* this many faults ... */
    double fault_window_s = 10;             /* ... within this many seconds disable a plugin */
    int watch_vsyncs = 30;                  /* hot reload: files checked every N vsyncs (and every 0.5 s of frames) */
    bool elevated = false;                  /* plugins_elevated: elevated capabilities may be granted */
};
extern Config config;

struct Memory {
    size_t used = 0, peak = 0;
    size_t cap = 0;   /* 0: none */
    size_t grace = 0; /* extra room while an error is being reported */
    unsigned long long refusals = 0;
    unsigned long long allocations = 0; /* blocks made or grown (the HUD pass's churn: RPC hud stats) */
};
struct Budget {
    bool active = false, killed = false;
    double start = 0, deadline = 0, limit_ms = 0;
    long long instructions = 0, max_instructions = 0;
};
/* CALL: handlers, timers, calls between plugins. LOAD: main chunks. TEST: evals and test
 * steps (load-sized; their faults are logged but never count towards disabling). */
enum BudgetKind { BUDGET_CALL, BUDGET_LOAD, BUDGET_TEST };

struct Stats {
    unsigned long long calls = 0, faults = 0, kills = 0;
    double total_ms = 0, max_ms = 0;
};
enum PluginState { PLUGIN_OFF, PLUGIN_LOADED, PLUGIN_FAILED, PLUGIN_DISABLED };

struct Dependency {
    std::string id, min_version; /* min_version "" : any */
};
struct Manifest {
    std::string id, name, version = "0.0.0", entry = "main.lua", description, author, tests = "tests";
    int api_version = RL_API_VERSION;
    bool default_enabled = true;
    std::vector<Dependency> depends;
    std::vector<std::string> capabilities; /* "fs.read", "fs.write:pc/user/x", "net:example.com", ... */
};

struct Plugin {
    Manifest manifest;
    std::string dir;          /* its folder ("pc/plugins/rood.example"); "" for a console sandbox */
    std::string data_dir;     /* its storage ("pc/user/plugin_data/rood.example"); "" for a console */
    bool shipped = false;     /* from pc/plugins */
    bool console = false;     /* a sandbox without a folder (eval, the console) */
    bool manifest_ok = false; /* its manifest parsed and checked */
    bool waiting = false;     /* failed for a dependency: retried when it loads */
    std::string hash;         /* SHA-256 of its code files (grants follow it) */
    std::vector<std::string> granted; /* capabilities active in this load */
    lua_State* L = nullptr;
    unsigned generation = 0; /* every load and unload: references into an older state go stale */
    int state = PLUGIN_OFF;
    std::string status; /* "loaded", or why it is not */
    Memory mem;
    Budget budget;
    Stats stats;
    int depth = 0; /* calls into it in progress */
    std::vector<double> fault_times;
    std::string disable_reason; /* set by fault(): settle() disables it */
    bool reload_requested = false;
    std::vector<int> pending_unrefs; /* registry references freed at its next call */
    std::map<std::string, std::pair<long long, long long>> files; /* hot reload: path -> (mtime ns, size) */
    const std::string& id() const { return manifest.id; }
};

/* ---- lua_limits.cpp ---- */
double now_seconds(); /* monotonic */
void* alloc(void* ud, void* ptr, size_t osize, size_t nsize); /* lua_Alloc; ud: the plugin's Memory */
lua_State* new_state(Plugin* p); /* with p's allocator and p in the extra space; no libraries */
Plugin* plugin_of(lua_State* L);
extern unsigned long long pattern_steps; /* units of C-loop work so far (rood_lua_work; the probe) */
void begin_budget(Plugin* p, BudgetKind kind); /* a call starts: deadline, instruction limit, the hook */
double end_budget(Plugin* p);                  /* it ended: elapsed ms */
/* Raises "budget exceeded<where>: ..." (never returns). */
void budget_error(lua_State* L, const Budget& b, const char* where);
/* At the start of every rood function (and proxy): a call over its budget stops here, so a
 * loop of costly API calls (copies, JSON) overruns by one call, not by RL_HOOK_STEP of them. */
void check_budget(lua_State* L);

/* ---- lua_value.cpp: values crossing between plugin states ---- */
struct FunctionRef {
    std::string owner;       /* the plugin whose state holds the function */
    unsigned generation = 0; /* that plugin's load it belongs to */
    int ref = LUA_NOREF;     /* in that state's registry */
    ~FunctionRef();          /* frees the reference at the owner's next call */
};
struct Value {
    enum Kind { NIL, BOOLEAN, INTEGER, NUMBER, STRING, TABLE, FUNCTION };
    Kind kind = NIL;
    bool b = false;
    long long i = 0;
    double n = 0;
    std::string s;
    std::vector<std::pair<Value, Value>> table; /* the array part first, in order */
    std::shared_ptr<FunctionRef> fn;

    static Value str(const std::string& text);
    static Value number(double v);
    static Value integer(long long v);
    static Value boolean(bool v);
    static Value new_table();
    /* A copy of the value at idx: plain data; a function becomes a reference to it
     * in this state. Raises a Lua error for a table that contains itself, userdata,
     * threads, more than 32 levels, more than 100000 parts or 32 MB. */
    static Value from_lua(lua_State* L, int idx);
    const Value* get(const char* key) const; /* a table's string key */
    Value& set(const char* key, Value v);    /* adds or replaces a string key */
    void push_back(Value v);                 /* key #+1 */
    size_t length() const;                   /* keys 1..n */
    size_t bytes() const;                    /* about what the host's copy takes */
};
/* Pushes a copy. A function of another state (or of an older load of this one)
 * becomes a proxy that runs it in its own plugin, under that plugin's budget. */
void push(lua_State* L, const Value& v);
/* Calls a function where it lives (a plugin's handler from the game, M1's renderers). */
bool call_function(const std::shared_ptr<FunctionRef>& fn, const std::vector<Value>& args, std::vector<Value>* results,
    std::string* error);
std::string to_json(const Value& v); /* tables with keys 1..n only: arrays; functions: "<function of id>" */
Value from_json(const prefab::Json& j);

/* ---- lua_runtime.cpp: states, protected calls, faults, the log ---- */
enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };
struct LogLine {
    unsigned long long seq;
    int level;
    std::string plugin, text;
};
/* stderr "[lua] ..." and the ring; a text past RL_MAX_LOG_LINE is cut (2000 lines of a plugin's
 * 16 MB strings were 32 GB of the host's memory). */
void log(int level, const std::string& plugin, const std::string& text);
const std::deque<LogLine>& log_lines(); /* the last 2000 */
unsigned long long log_last_seq();
const char* state_name(int state); /* "off", "loaded", "failed", "disabled" */
std::vector<std::unique_ptr<Plugin>>& all(); /* every plugin and console sandbox; never shrinks */
Plugin* find(const std::string& id);
Plugin* console(const std::string& name = "@console"); /* a sandbox without a folder, made on first use */
bool open_state(Plugin* p, std::string* error); /* a fresh state: sandbox and rood */
void close_state(Plugin* p);                    /* lua_close under the load budget (finalizers) */
/* Runs body inside a protected call into p's state, under a budget (the outermost
 * call into p sets it), with a traceback on error. On error: logs it, counts a fault
 * (a CALL or LOAD), puts the message (with traceback) in *error, returns false. */
bool run(Plugin* p, const char* what, BudgetKind kind, const std::function<void(lua_State*)>& body,
    std::string* error = nullptr);
/* Code as an expression ("return " + code) if it parses as one, else as statements;
 * its results copied (BUDGET_TEST: an eval never counts as a fault). */
bool eval(Plugin* p, const std::string& code, std::vector<Value>* results, std::string* error);
void fault(Plugin* p, const char* what, const std::string& message, bool killed, bool counted);
bool on_game_thread(); /* the first caller's thread is the game's */
int calls_in_progress();
/* Lua helpers: C functions wrapped so a C++ exception becomes a Lua error; a read-only view. */
void push_cfunction(lua_State* L, lua_CFunction f);
void set_cfunction(lua_State* L, int table, const char* name, lua_CFunction f);
void lock_table(lua_State* L); /* replaces the table on top with a read-only view of it */
/* Game-side modules (rood.settings) register themselves here (static constructors). */
using ApiOpener = void (*)(lua_State* L, Plugin* p);
using PluginHook = void (*)(Plugin* p);
using Hook = void (*)();
std::vector<ApiOpener>& api_openers(); /* add fields to rood */
std::vector<PluginHook>& unload_hooks(); /* drop what a plugin registered */
std::vector<Hook>& poll_hooks();         /* every tick and frame */
std::vector<Hook>& tick_hooks();         /* every tick, first (the HUD's held moments) */
std::vector<Hook>& flush_hooks();        /* at exit: write what is unsaved */

/* ---- lua_sandbox.cpp, lua_api_core.cpp ---- */
void open_sandbox(lua_State* L, Plugin* p);
void open_rood(lua_State* L, Plugin* p);

/* ---- lua_files.cpp ---- */
/* The whole file; false (and why: "not found", "larger than N KB") past `limit` bytes. */
bool read_file(const std::string& path, std::string* out, size_t limit, std::string* why);
bool write_file(const std::string& path, const std::string& text, std::string* why); /* atomic; makes folders */
bool make_dirs(const std::string& path);
bool file_stat(const std::string& path, long long* mtime_ns, long long* size, bool* is_dir);
std::vector<std::string> list_dir(const std::string& path); /* sorted names, without . and .. */
bool safe_relative(const std::string& path, std::string* why); /* a/b.c: no "..", no absolute path */
/* full (root + "/" + a safe_relative path) is still inside root once links are
 * followed: the deepest part of it that exists is resolved, so a link in any
 * component (the last one too, or a dangling one) cannot lead out; a root that
 * does not exist yet passes (what is made goes below it). POSIX. */
bool inside(const std::string& root, const std::string& full, std::string* why);

/* ---- lua_events.cpp: events, the core's hooks, timers ---- */
void open_events(lua_State* L, Plugin* p); /* rood.on/off/emit, rood.timer, rood.clock */
/* Calls every handler of `event`, each in its own plugin with its own copy of payload
 * and the sender (the emitting plugin's id; "": the core, nil in Lua); returns how
 * many were called. */
int emit(const std::string& event, const Value& payload, const std::string& sender = "");
int emit_to(Plugin* p, const std::string& event, const Value& payload); /* p's handlers only ("unload") */
void tick_events(unsigned vsync);  /* game time: "tick", game-time timers */
void frame_events(double dt);      /* real time: "frame", real-time timers */
void drop_events(const std::string& plugin); /* its handlers and timers */
size_t handler_count(const std::string& plugin);
size_t timer_count(const std::string& plugin);
double game_clock(); /* seconds of game time (ticks / 59.94) */
double real_clock(); /* seconds since the first call */

/* ---- lua_registry.cpp ---- */
struct RegistryEntry {
    std::string kind, id, owner, group;
    std::vector<std::string> after, before; /* ids, or "group:<name>" */
    Value data;
    unsigned long long seq; /* registration order */
};
void open_registry(lua_State* L, Plugin* p); /* rood.registry */
/* A kind's entries in order (after / before, then registration order); group "": all. */
std::vector<const RegistryEntry*> registry_list(const std::string& kind, const std::string& group = "");
const RegistryEntry* registry_get(const std::string& kind, const std::string& id);
std::vector<std::string> registry_kinds();
size_t registry_count(const std::string& owner);
void drop_registry(const std::string& owner); /* its entries (registry.changed for each) */
/* For the host's own APIs (rood.menu): register or replace kind+id as given (owner,
 * relations, data: the caller checked them), and remove one; registry.changed for each. */
void registry_put(const RegistryEntry& entry);
bool registry_drop(const std::string& kind, const std::string& id);

/* ---- lua_caps.cpp ---- */
struct CapabilityInfo {
    const char* name;  /* "fs.read" */
    bool scoped;       /* takes ":<scope>" (a folder, a host) */
    bool implemented;  /* M0: fs.read and fs.write; the others refuse with a message */
    const char* plain; /* what the grant dialog says */
};
const std::vector<CapabilityInfo>& capability_table();
const CapabilityInfo* capability_info(const std::string& name); /* without the scope */
bool capability_ok(const std::string& capability, std::string* why);
bool filesystem_grant_ok(const std::string& capability, std::string* why);
bool external_filesystem_grant(const Plugin* p, const std::string& capability);
struct Grant {
    std::string plugin, hash, capability; /* settings plugin_grant <plugin> <hash> <capability> */
};
std::vector<Grant>& grants();
std::vector<std::string> active_grants(const Plugin* p);  /* declared, granted for its hash, master switch on */
std::vector<std::string> pending_grants(const Plugin* p); /* granted for another hash: asked again */
/* The user's grant (later the dialog; now RPC lua {action "grant"} and the probe): the
 * capability must be declared; tied to the plugin's current hash; saves settings.cfg. */
bool grant(const std::string& plugin, const std::string& capability, bool revoke, std::string* error);
void open_capabilities(lua_State* L, Plugin* p); /* rood.fs, rood.net, ... for p->granted */
void audit(Plugin* p, const std::string& capability, const std::string& detail);
const std::deque<std::string>& audit_lines();

/* ---- lua_plugins.cpp ---- */
std::string shipped_root(); /* ROOD_PLUGINS_SHIPPED, else pc/plugins ("" : none) */
std::string user_root();    /* ROOD_PLUGINS, else pc/user/plugins */
std::string data_root();    /* ROOD_PLUGIN_DATA, else pc/user/plugin_data */
/* A main chunk that returns a table: the plugin's module, in its registry at &module_key
 * (lua_rawgetp; the REPL's M and its exports, lua_repl.cpp). */
extern char module_key;
bool parse_manifest(const Value& v, Manifest& m, std::string* error);
bool read_manifest(const std::string& dir, Manifest& m, std::string* error); /* plugin.json, else plugin.lua */
/* SHA-256 over its code files (*.lua, *.fnl, plugin.json, any case; not hidden; at most
 * RL_MAX_DEPTH folders deep), paths sorted; *complete: false past RL_MAX_CODE_FILES. */
std::string content_hash(const std::string& dir, bool* complete = nullptr);
/* A path code is loaded from (entry, tests, a module): safe_relative, no hidden
 * component, at most RL_MAX_DEPTH folders: what content_hash covers. */
bool code_path_ok(const std::string& path, std::string* why);
void scan();                       /* new folders become plugins (not loaded yet) */
std::vector<Plugin*> load_order(); /* dependencies first, then plugin_order, shipped, id */
bool load(Plugin* p);
void unload(Plugin* p, int new_state, const std::string& status);
void unload_with_dependents(Plugin* p, int new_state, const std::string& status);
void reload(Plugin* p); /* and what depends on it */
void request_reload_all();
bool plugin_enabled(const Plugin* p);
void set_enabled(const std::string& id, bool enabled); /* settings plugin_enabled; saves settings.cfg */
void config_changed(); /* settle() reconciles: enabled set, grants, memory caps */
void save_settings_now(); /* pc_settings_save when the game has it (enable, grants) */
void remember_files(Plugin* p); /* its code files' (mtime, size) now */
void poll_files();              /* changed plugins reload at settle(); new folders load; removed ones unload */

void settle();         /* the safe point: disables, reloads, reconcile; nothing when a call is in progress */
void init();
bool initialized();
void tick();
void frame(double dt);
struct Tunable {
    const char* key;   /* settings.cfg */
    const char* rpc;   /* RPC lua {action "budget"} */
    const char* label; /* the console */
    double* d;
    long long* i;
    int* k;
    double lo, hi;
};
const std::vector<Tunable>& tunables();
double get_tunable(const Tunable& t);
void set_tunable(const Tunable& t, double value); /* clamped; config_changed() */

/* ---- lua_storage.cpp ---- */
void open_storage(lua_State* L, Plugin* p); /* rood.storage */
bool flush_storage(Plugin* p);              /* its store.json now, if anything changed */
void flush_storage_due();                   /* stores changed more than a second ago */
void drop_storage(Plugin* p);               /* writes it and forgets it (an unload) */

/* ---- lua_test.cpp ---- */
void open_test(lua_State* L, Plugin* p); /* rood.test */
/* Collects the cases of every loaded plugin's tests folder (or one plugin's; filter: a
 * substring of the case's name) and runs what is ready; returns the run's id. */
int start_tests(const std::string& plugin, const std::string& filter);
void step_tests(bool tick); /* a tick: waiting cases count down; ready ones run */
std::string test_run_json(int run); /* {run, done, passed, failed, skipped, waiting, results}; "" if unknown */
void drop_tests(const std::string& plugin); /* its waiting cases fail: the plugin unloaded */

/* ---- lua_api_hud.cpp ---- */
/* The claims' draws of the live HUD elements into the command buffer (pc_hud_frame, then
 * its renderer); returns the commands. */
int hud_pass();
struct ClaimInfo {
    const char* kind; /* "status", ... */
    Plugin* plugin;
    int ref;     /* the draw function in the plugin's registry */
    bool failed; /* its draw raised: not honoured until the plugin reloads */
    bool active; /* the kind's owner */
};
std::vector<ClaimInfo> hud_claims(); /* every live claim (the REPL's inspection) */

/* ---- lua_repl.cpp ---- */
/* RPC lua {action "repl"} (dev mode): sessions evaluating in a plugin's or a console's
 * sandbox; false and *error for a refused request. */
bool repl_rpc(const prefab::Json& params, std::string* reply, std::string* error);

/* ---- lua_rpc.cpp ---- */
std::vector<std::string>& console_history(); /* the console's inputs, oldest first (200) */
/* As typed into the console: history, "> input" and "= results" in the log, eval in
 * target ("@console": the console's own sandbox). */
bool console_submit(const std::string& target, const std::string& text, std::vector<Value>* results,
    std::string* error);

} // namespace rl
#endif
