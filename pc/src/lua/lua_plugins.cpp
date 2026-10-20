// Plugins (pc/docs/Extensions.md, "Plugins"): folders with a manifest
// (plugin.lua returning a table, or plugin.json) in pc/plugins (shipped,
// committed; ROOD_PLUGINS_SHIPPED) and in the user's folder
// (pc/user/plugins; ROOD_PLUGINS), their data in
// pc/user/plugin_data/<id> (ROOD_PLUGIN_DATA). Discovery and the
// manifest's checks; enable / disable and load order (settings.cfg
// plugin_enabled, plugin_order; dependencies always first); loading a
// plugin's main chunk in a fresh sandbox, unloading everything it
// registered, reloading it with what depends on it; the safe point
// (settle) where faults disable plugins and reloads happen; hot reload
// (code files' mtime and size every watch_vsyncs ticks and every 0.5 s of
// frames: the plugin and what depends on it reload, everything they
// registered gone first); the tunables
// (settings plugin_*); and the game's entry points (lua_runtime.h).
#include "rl_internal.h"
#include "light_prefabs.h"
#include "lua_runtime.h"
#include "sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>

extern "C" int pc_overlay_active(void) __attribute__((weak));
extern "C" unsigned pc_vsync_count(void) __attribute__((weak));
extern "C" void pc_settings_save(void) __attribute__((weak));

/* Hot reload checks made (RPC status polls.plugins): a test that edited a
 * plugin's file waits for the next check instead of guessing its period. */
extern "C" unsigned pc_lua_file_polls;
unsigned pc_lua_file_polls;

namespace rl {
std::map<std::string, bool> enabled_override; /* settings plugin_enabled (pc_lua_settings_* below) */
std::vector<std::string> order;               /* settings plugin_order */
char module_key;                              /* the registry's key of a plugin's module */
namespace {
bool started;
bool dirty_config;
unsigned ticks;

std::string env_or(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

bool id_ok(const std::string& id)
{
    if (id.empty() || id.size() > 64 || id.front() == '.' || id.back() == '.')
        return false;
    int dots = 0;
    for (size_t k = 0; k < id.size(); k++) {
        char c = id[k];
        if (c == '.') {
            if (id[k + 1] == '.')
                return false;
            ++dots;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return dots >= 1;
}

bool version_ok(const std::string& v)
{
    int parts = 0;
    size_t start = 0;
    while (true) {
        size_t end = v.find('.', start);
        std::string part = v.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part.size() > 9 || part.find_first_not_of("0123456789") != std::string::npos)
            return false;
        ++parts;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return parts == 3;
}

int compare_versions(const std::string& a, const std::string& b)
{
    long x[3] = {}, y[3] = {};
    std::sscanf(a.c_str(), "%ld.%ld.%ld", &x[0], &x[1], &x[2]);
    std::sscanf(b.c_str(), "%ld.%ld.%ld", &y[0], &y[1], &y[2]);
    for (int k = 0; k < 3; k++)
        if (x[k] != y[k])
            return x[k] < y[k] ? -1 : 1;
    return 0;
}

std::string first_line(const std::string& s) { return s.substr(0, s.find('\n')); }

std::string folder_name(const std::string& dir)
{
    size_t slash = dir.find_last_of('/');
    return slash == std::string::npos ? dir : dir.substr(slash + 1);
}

/* plugin.lua in a throwaway state with no libraries: a table constructor is all it can be. */
bool manifest_from_lua(const std::string& dir, const std::string& code, Value* out, std::string* error)
{
    Plugin temp;
    temp.manifest.id = "@manifest";
    temp.console = true;
    temp.mem.cap = 1u << 20;
    temp.L = new_state(&temp);
    if (!temp.L) {
        *error = "plugin.lua: cannot create a state";
        return false;
    }
    std::string chunk = "@" + dir + "/plugin.lua", why;
    bool ok = run(&temp, "plugin.lua", BUDGET_TEST, [&](lua_State* L) {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 1);
        *out = Value::from_lua(L, -1);
    }, &why);
    close_state(&temp);
    if (!ok)
        *error = "plugin.lua: " + first_line(why);
    return ok;
}

/* The checks every load repeats: the manifest, the folder's name, the reserved namespace. */
bool check_manifest(const std::string& dir, bool shipped, Manifest& m, std::string* error)
{
    if (!read_manifest(dir, m, error))
        return false;
    if (m.id != folder_name(dir)) {
        *error = "the folder must be named after its id ('" + m.id + "')";
        return false;
    }
    if (!shipped && m.id.compare(0, 5, "rood.") == 0) {
        *error = "ids starting with 'rood.' are reserved for plugins shipped with the game";
        return false;
    }
    return true;
}

bool ends_with(const std::string& s, const char* suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
/* Any case: on Windows and macOS require('m') loads M.LUA too. */
bool ends_with_nocase(const std::string& s, const char* suffix)
{
    size_t n = std::strlen(suffix);
    if (s.size() < n)
        return false;
    for (size_t k = 0; k < n; k++)
        if (std::tolower((unsigned char)s[s.size() - n + k]) != std::tolower((unsigned char)suffix[k]))
            return false;
    return true;
}

/* Code files under dir (relative, sorted): *.lua, *.fnl, plugin.json (any case), not
 * hidden, at most RL_MAX_DEPTH folders deep (what code_path_ok lets load); `skip`: a
 * subfolder left out; *complete: false when there were more than RL_MAX_CODE_FILES. */
std::vector<std::string> code_files(const std::string& dir, const std::string& skip, bool* complete = nullptr)
{
    std::vector<std::string> out;
    std::vector<std::pair<std::string, int>> todo{ { std::string(), 0 } };
    if (complete)
        *complete = true;
    while (!todo.empty() && out.size() <= RL_MAX_CODE_FILES) {
        std::pair<std::string, int> next = todo.back();
        todo.pop_back();
        std::string here = next.first.empty() ? dir : dir + "/" + next.first;
        for (const std::string& name : list_dir(here)) {
            if (name[0] == '.')
                continue;
            std::string path = next.first.empty() ? name : next.first + "/" + name;
            bool is_dir = false;
            if (!file_stat(here + "/" + name, nullptr, nullptr, &is_dir))
                continue;
            if (is_dir) {
                if (next.second < RL_MAX_DEPTH && path != skip)
                    todo.push_back({ path, next.second + 1 });
                continue;
            }
            if (ends_with_nocase(name, ".lua") || ends_with_nocase(name, ".fnl") || ends_with_nocase(name, "plugin.json"))
                out.push_back(path);
        }
    }
    if (out.size() > RL_MAX_CODE_FILES && complete)
        *complete = false;
    std::sort(out.begin(), out.end());
    return out;
}

/* What the watcher compares: code files, without the tests folder (a test edit
 * does not reload the plugin: the next test run reads it). */
std::map<std::string, std::pair<long long, long long>> watched_files(const Plugin* p)
{
    std::map<std::string, std::pair<long long, long long>> out;
    for (const std::string& file : code_files(p->dir, p->manifest.tests)) {
        long long mtime = 0, size = 0;
        if (file_stat(p->dir + "/" + file, &mtime, &size, nullptr))
            out[file] = { mtime, size };
    }
    return out;
}

/* Loaded plugins that depend on p, directly or not, in load order. */
std::vector<Plugin*> dependents_of(Plugin* p)
{
    std::vector<Plugin*> out;
    std::vector<std::string> ids{ p->id() };
    for (Plugin* q : load_order()) {
        if (q == p || q->state != PLUGIN_LOADED)
            continue;
        for (const Dependency& d : q->manifest.depends)
            if (std::find(ids.begin(), ids.end(), d.id) != ids.end()) {
                out.push_back(q);
                ids.push_back(q->id());
                break;
            }
    }
    return out;
}

void reconcile()
{
    size_t cap = (size_t)(config.memory_mb * 1048576.0);
    for (auto& up : all())
        if (up->L)
            up->mem.cap = cap;
    for (Plugin* p : load_order()) {
        bool want = plugin_enabled(p);
        if (want && (p->state == PLUGIN_OFF || (p->state == PLUGIN_FAILED && p->waiting)))
            load(p);
        else if (want && p->state == PLUGIN_LOADED && active_grants(p) != p->granted)
            reload(p);
        else if (!want && p->state == PLUGIN_LOADED)
            unload_with_dependents(p, PLUGIN_OFF, "off (disabled in the plugin list)");
        else if (!want && p->state != PLUGIN_OFF) {
            p->state = PLUGIN_OFF;
            p->status = "off (disabled in the plugin list)";
            p->waiting = false;
        }
    }
}
} // namespace

std::string shipped_root() { return env_or("ROOD_PLUGINS_SHIPPED", "pc/plugins"); }
std::string user_root() { return env_or("ROOD_PLUGINS", "pc/user/plugins"); }
std::string data_root() { return env_or("ROOD_PLUGIN_DATA", "pc/user/plugin_data"); }

void save_settings_now()
{
    if (pc_settings_save)
        pc_settings_save();
}

bool parse_manifest(const Value& v, Manifest& m, std::string* error)
{
    m = Manifest();
    if (v.kind != Value::TABLE) {
        *error = "the manifest must be a table (plugin.lua: return { ... }) or an object (plugin.json)";
        return false;
    }
    auto text = [&](const char* key, std::string& out, bool required) {
        const Value* f = v.get(key);
        if (!f || f->kind == Value::NIL) {
            if (required)
                *error = std::string("the manifest needs ") + key;
            return !required;
        }
        if (f->kind != Value::STRING) {
            *error = std::string("the manifest's ") + key + " must be a string";
            return false;
        }
        out = f->s;
        return true;
    };
    if (!text("id", m.id, true) || !text("name", m.name, false) || !text("version", m.version, false)
        || !text("entry", m.entry, false) || !text("description", m.description, false)
        || !text("author", m.author, false) || !text("tests", m.tests, false))
        return false;
    if (!id_ok(m.id)) {
        *error = "id '" + m.id + "': lowercase words joined by dots, at least two (author.plugin)";
        return false;
    }
    if (m.name.empty())
        m.name = m.id;
    if (!version_ok(m.version)) {
        *error = "version '" + m.version + "': three numbers, as in 1.0.0";
        return false;
    }
    std::string why;
    if (!code_path_ok(m.entry, &why) || !ends_with(m.entry, ".lua")) {
        *error = "entry '" + m.entry + "': a .lua file in the plugin's folder" + (why.empty() ? "" : " (" + why + ")");
        return false;
    }
    if (!code_path_ok(m.tests, &why) || std::count(m.tests.begin(), m.tests.end(), '/') >= RL_MAX_DEPTH) {
        *error = "tests '" + m.tests + "': " + why;
        return false;
    }
    const Value* api = v.get("api_version");
    if (!api || api->kind != Value::INTEGER) {
        *error = "the manifest needs api_version (an integer; this build has " + std::to_string(RL_API_VERSION) + ")";
        return false;
    }
    if (api->i < 1 || api->i > RL_API_VERSION) {
        *error = "needs api_version " + std::to_string(api->i) + "; this build has " + std::to_string(RL_API_VERSION);
        return false;
    }
    m.api_version = (int)api->i;
    if (const Value* on = v.get("default_enabled")) {
        if (on->kind != Value::BOOLEAN) {
            *error = "default_enabled must be true or false";
            return false;
        }
        m.default_enabled = on->b;
    }
    if (const Value* deps = v.get("depends")) {
        if (deps->kind != Value::TABLE) {
            *error = "depends must be a list";
            return false;
        }
        for (const auto& kv : deps->table) {
            const Value& d = kv.second;
            Dependency dep;
            if (d.kind == Value::STRING)
                dep.id = d.s;
            else if (d.kind == Value::TABLE && d.get("id") && d.get("id")->kind == Value::STRING) {
                dep.id = d.get("id")->s;
                if (const Value* ver = d.get("version")) {
                    if (ver->kind != Value::STRING || ver->s.compare(0, 2, ">=") != 0 || !version_ok(ver->s.substr(2))) {
                        *error = "depends: a version is \">=x.y.z\"";
                        return false;
                    }
                    dep.min_version = ver->s.substr(2);
                }
            } else {
                *error = "depends: each is an id or { id = ..., version = \">=x.y.z\" }";
                return false;
            }
            if (!id_ok(dep.id)) {
                *error = "depends: bad id '" + dep.id + "'";
                return false;
            }
            m.depends.push_back(dep);
        }
    }
    if (const Value* caps = v.get("capabilities")) {
        if (caps->kind != Value::TABLE) {
            *error = "capabilities must be a list";
            return false;
        }
        for (const auto& kv : caps->table) {
            if (kv.second.kind != Value::STRING) {
                *error = "capabilities: each is a string";
                return false;
            }
            if (!capability_ok(kv.second.s, &why)) {
                *error = "capability '" + kv.second.s + "': " + why;
                return false;
            }
            m.capabilities.push_back(kv.second.s);
        }
    }
    return true;
}

bool read_manifest(const std::string& dir, Manifest& m, std::string* error)
{
    std::string text, why;
    Value v;
    if (read_file(dir + "/plugin.json", &text, RL_MAX_FILE, &why)) {
        prefab::Json j;
        if (!prefab::json_parse(text.c_str(), j, &why)) {
            *error = "plugin.json: " + why;
            return false;
        }
        v = from_json(j);
    } else if (read_file(dir + "/plugin.lua", &text, RL_MAX_FILE, &why)) {
        if (!manifest_from_lua(dir, text, &v, error))
            return false;
    } else {
        *error = "no plugin.lua or plugin.json";
        return false;
    }
    return parse_manifest(v, m, error);
}

bool code_path_ok(const std::string& path, std::string* why)
{
    if (!safe_relative(path, why))
        return false;
    int folders = 0;
    for (size_t k = 0; k < path.size(); k++) {
        if ((k == 0 || path[k - 1] == '/') && path[k] == '.') {
            *why = "a hidden file or folder (never code: the content hash leaves them out)";
            return false;
        }
        folders += path[k] == '/';
    }
    if (folders > RL_MAX_DEPTH) {
        *why = "more than " + std::to_string(RL_MAX_DEPTH) + " folders deep";
        return false;
    }
    return true;
}

std::string content_hash(const std::string& dir, bool* complete)
{
    Sha256 h;
    for (const std::string& file : code_files(dir, "", complete)) {
        std::string text, why;
        read_file(dir + "/" + file, &text, RL_MAX_FILE * 4, &why);
        unsigned char size[8];
        uint64_t n = text.size();
        for (int k = 0; k < 8; k++)
            size[k] = (unsigned char)(n >> (8 * k));
        h.update(file.data(), file.size() + 1); /* with its terminating zero */
        h.update(size, sizeof size);
        h.update(text.data(), text.size());
    }
    return h.hex();
}

void scan()
{
    for (int pass = 0; pass < 2; pass++) {
        bool shipped = pass == 0;
        std::string root = shipped ? shipped_root() : user_root();
        if (root.empty())
            continue;
        for (const std::string& name : list_dir(root)) {
            std::string dir = root + "/" + name;
            bool is_dir = false;
            if (name[0] == '.' || !file_stat(dir, nullptr, nullptr, &is_dir) || !is_dir)
                continue;
            bool known = false;
            for (auto& p : all())
                known |= p->dir == dir;
            if (known)
                continue;
            Manifest m;
            std::string error;
            bool ok = check_manifest(dir, shipped, m, &error);
            if (find(ok ? m.id : name)) {
                log(LOG_ERROR, "", dir + ": another plugin already has the id " + (ok ? m.id : name) + "; skipped");
                continue;
            }
            auto made = std::make_unique<Plugin>();
            Plugin* p = made.get();
            p->dir = dir;
            p->shipped = shipped;
            if (ok) {
                p->manifest = m;
                p->manifest_ok = true;
                p->status = "off";
            } else {
                p->manifest.id = p->manifest.name = name;
                p->state = PLUGIN_FAILED;
                p->status = error;
                log(LOG_ERROR, name, error);
            }
            p->data_dir = data_root() + "/" + p->id();
            p->files = watched_files(p);
            all().push_back(std::move(made));
        }
    }
}

std::vector<Plugin*> load_order()
{
    std::vector<Plugin*> nodes;
    for (auto& up : all())
        if (!up->console && up->manifest_ok)
            nodes.push_back(up.get());
    auto rank = [&](Plugin* p) {
        auto it = std::find(order.begin(), order.end(), p->id());
        return std::make_tuple(it == order.end() ? 1 : 0, (long)(it - order.begin()), p->shipped ? 0 : 1, p->id());
    };
    std::sort(nodes.begin(), nodes.end(), [&](Plugin* a, Plugin* b) { return rank(a) < rank(b); });
    std::vector<Plugin*> out;
    std::vector<bool> done(nodes.size(), false);
    for (size_t round = 0; round < nodes.size(); round++) {
        size_t pick = nodes.size();
        for (size_t k = 0; k < nodes.size() && pick == nodes.size(); k++) {
            if (done[k])
                continue;
            bool ready = true;
            for (const Dependency& d : nodes[k]->manifest.depends)
                for (size_t j = 0; j < nodes.size(); j++)
                    if (!done[j] && j != k && nodes[j]->id() == d.id)
                        ready = false;
            if (ready)
                pick = k;
        }
        for (size_t k = 0; k < nodes.size() && pick == nodes.size(); k++) /* a cycle: it fails at load */
            if (!done[k])
                pick = k;
        done[pick] = true;
        out.push_back(nodes[pick]);
    }
    return out;
}

bool plugin_enabled(const Plugin* p)
{
    auto it = enabled_override.find(p->id());
    return it != enabled_override.end() ? it->second : p->manifest.default_enabled;
}

void set_enabled(const std::string& id, bool enabled)
{
    enabled_override[id] = enabled;
    config_changed();
    save_settings_now();
}

void config_changed() { dirty_config = true; }

bool load(Plugin* p)
{
    if (p->console || !p->manifest_ok)
        return false;
    if (p->state == PLUGIN_LOADED)
        return true;
    p->waiting = false;
    for (const Dependency& d : p->manifest.depends) {
        Plugin* dep = find(d.id);
        std::string why;
        if (!dep || !dep->manifest_ok)
            why = "needs plugin " + d.id + " (not installed)";
        else if (dep->state != PLUGIN_LOADED)
            why = "needs plugin " + d.id + " (" + state_name(dep->state) + ")";
        else if (!d.min_version.empty() && compare_versions(dep->manifest.version, d.min_version) < 0)
            why = "needs " + d.id + " " + d.min_version + " or newer (" + dep->manifest.version + " is installed)";
        if (!why.empty()) {
            p->state = PLUGIN_FAILED;
            p->status = why;
            p->waiting = true;
            log(LOG_ERROR, p->id(), why);
            return false;
        }
    }
    std::string code, error;
    if (!inside(p->dir, p->dir + "/" + p->manifest.entry, &error)
        || !read_file(p->dir + "/" + p->manifest.entry, &code, RL_MAX_FILE, &error)) {
        p->state = PLUGIN_FAILED;
        p->status = "cannot read " + p->manifest.entry + ": " + error;
        log(LOG_ERROR, p->id(), p->status);
        return false;
    }
    bool complete = true;
    p->hash = content_hash(p->dir, &complete);
    if (!complete) {
        p->state = PLUGIN_FAILED;
        p->status = "more than " + std::to_string(RL_MAX_CODE_FILES) + " code files";
        log(LOG_ERROR, p->id(), p->status);
        return false;
    }
    remember_files(p);
    p->granted = active_grants(p);
    ++p->generation;
    p->fault_times.clear();
    p->disable_reason.clear();
    if (!open_state(p, &error)) {
        p->state = PLUGIN_FAILED;
        p->status = first_line(error);
        return false;
    }
    p->state = PLUGIN_LOADED; /* its main chunk may already register, emit and call */
    std::string chunk = "@" + p->id() + "/" + p->manifest.entry;
    bool ok = run(p, "load", BUDGET_LOAD, [&](lua_State* L) {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 1);
        if (lua_istable(L, -1)) /* its module: the REPL's M and exports (lua_repl.cpp) */
            lua_rawsetp(L, LUA_REGISTRYINDEX, &module_key);
    }, &error);
    if (!ok) {
        unload(p, PLUGIN_FAILED, "its main chunk failed: " + first_line(error));
        return false;
    }
    p->status = "loaded";
    log(LOG_INFO, p->id(), "loaded (version " + p->manifest.version + ")");
    Value payload = Value::new_table();
    payload.set("id", Value::str(p->id()));
    emit("plugin.loaded", payload);
    return true;
}

void unload(Plugin* p, int new_state, const std::string& status)
{
    bool was_loaded = p->state == PLUGIN_LOADED && p->L;
    if (was_loaded)
        emit_to(p, "unload", Value());
    for (PluginHook hook : unload_hooks())
        hook(p);
    drop_events(p->id());
    drop_registry(p->id());
    drop_storage(p);
    drop_tests(p->id());
    close_state(p);
    ++p->generation;
    p->state = new_state;
    p->status = status;
    p->granted.clear();
    p->disable_reason.clear();
    p->fault_times.clear();
    if (was_loaded) {
        Value payload = Value::new_table();
        payload.set("id", Value::str(p->id()));
        emit("plugin.unloaded", payload);
    }
}

void unload_with_dependents(Plugin* p, int new_state, const std::string& status)
{
    std::vector<Plugin*> dependents = dependents_of(p);
    for (auto it = dependents.rbegin(); it != dependents.rend(); ++it) {
        unload(*it, PLUGIN_FAILED, "needs plugin " + p->id() + " (" + status + ")");
        (*it)->waiting = true;
    }
    unload(p, new_state, status);
}

void reload(Plugin* p)
{
    if (p->console)
        return;
    std::vector<Plugin*> dependents = dependents_of(p);
    for (auto it = dependents.rbegin(); it != dependents.rend(); ++it)
        unload(*it, PLUGIN_OFF, "reloading " + p->id());
    if (p->state == PLUGIN_LOADED)
        unload(p, PLUGIN_OFF, "reloading");
    Manifest m;
    std::string error;
    if (!check_manifest(p->dir, p->shipped, m, &error)) {
        p->manifest_ok = false;
        p->state = PLUGIN_FAILED;
        p->status = error;
        remember_files(p);
        log(LOG_ERROR, p->id(), error);
    } else {
        p->manifest = m;
        p->manifest_ok = true;
        if (plugin_enabled(p))
            load(p);
        else {
            p->state = PLUGIN_OFF;
            p->status = "off (disabled in the plugin list)";
        }
    }
    for (Plugin* d : dependents)
        if (plugin_enabled(d))
            load(d);
}

void request_reload_all()
{
    scan();
    for (auto& up : all())
        if (!up->console && (up->state != PLUGIN_OFF || !up->manifest_ok))
            up->reload_requested = true;
    config_changed();
}

void remember_files(Plugin* p) { p->files = watched_files(p); }

void poll_files()
{
    size_t before = all().size();
    scan();
    if (all().size() != before)
        config_changed(); /* new folders: reconcile loads the enabled ones */
    for (auto& up : all()) {
        Plugin* p = up.get();
        if (p->console || p->dir.empty())
            continue;
        bool is_dir = false;
        if (!file_stat(p->dir, nullptr, nullptr, &is_dir) || !is_dir) {
            if (p->state == PLUGIN_LOADED)
                unload_with_dependents(p, PLUGIN_OFF, "its folder was removed");
            p->manifest_ok = false;
            p->state = PLUGIN_OFF;
            p->status = "its folder was removed";
            p->files.clear();
            continue;
        }
        if (p->state == PLUGIN_OFF && p->manifest_ok)
            continue; /* not enabled: picked up when it is */
        auto now = watched_files(p);
        if (now != p->files) {
            p->files = now;
            p->reload_requested = true;
            log(LOG_INFO, p->id(), "files changed: reloading");
        }
    }
    pc_lua_file_polls++;
}

void settle()
{
    if (calls_in_progress() > 0)
        return;
    for (auto& up : all()) {
        Plugin* p = up.get();
        if (p->disable_reason.empty())
            continue;
        std::string reason = p->disable_reason;
        p->disable_reason.clear();
        if (p->state == PLUGIN_LOADED) {
            log(LOG_ERROR, p->id(), "disabled: " + reason + "; it reloads when its files change");
            unload_with_dependents(p, PLUGIN_DISABLED, "disabled: " + reason);
        }
    }
    for (auto& up : all())
        if (up->reload_requested) {
            up->reload_requested = false;
            reload(up.get());
        }
    if (dirty_config) {
        dirty_config = false;
        reconcile();
    }
    flush_storage_due();
}

void init()
{
    if (started)
        return;
    on_game_thread(); /* this thread is the game's */
    started = true;
    scan();
    reconcile();
    int found = 0, loaded = 0;
    for (auto& up : all())
        if (!up->console) {
            ++found;
            loaded += up->state == PLUGIN_LOADED;
        }
    log(LOG_INFO, "", std::to_string(found) + " plugin(s) in " + (shipped_root().empty() ? "-" : shipped_root()) + " and "
        + user_root() + ", " + std::to_string(loaded) + " loaded");
}
bool initialized() { return started; }

void tick()
{
    ++ticks;
    for (Hook hook : tick_hooks())
        hook();
    tick_events(pc_vsync_count ? pc_vsync_count() : ticks);
    for (Hook hook : poll_hooks())
        hook();
    step_tests(true);
    if (!pc_overlay_active || !pc_overlay_active())
        frame_events(1.0 / 59.94); /* no overlay (software presenter): frames come from here */
    if (config.watch_vsyncs > 0 && ticks % (unsigned)config.watch_vsyncs == 0)
        poll_files();
    settle();
}

void frame(double dt)
{
    frame_events(dt);
    for (Hook hook : poll_hooks())
        hook();
    static double last_poll;
    double now = now_seconds();
    if (now - last_poll >= 0.5) { /* paused redraws: edits still reload */
        last_poll = now;
        poll_files();
    }
    settle();
}

const std::vector<Tunable>& tunables()
{
    static const std::vector<Tunable> list = {
        { "plugin_budget_ms", "budget_ms", "Call budget (ms)", &config.budget_ms, nullptr, nullptr, 0.1, 1000 },
        { "plugin_budget_instructions", "budget_instructions", "Call budget (instructions)", nullptr,
            &config.budget_instructions, nullptr, 1000, 1e11 },
        { "plugin_load_budget_ms", "load_budget_ms", "Load budget (ms)", &config.load_budget_ms, nullptr, nullptr, 1,
            60000 },
        { "plugin_load_budget_instructions", "load_budget_instructions", "Load budget (instructions)", nullptr,
            &config.load_budget_instructions, nullptr, 1000, 1e12 },
        { "plugin_memory_mb", "memory_mb", "Memory per plugin (MB)", &config.memory_mb, nullptr, nullptr, 1, 4096 },
        { "plugin_fault_limit", "fault_limit", "Faults before disabling", nullptr, nullptr, &config.fault_limit, 1,
            1000 },
        { "plugin_fault_window", "fault_window_s", "Fault window (s)", &config.fault_window_s, nullptr, nullptr, 0.1,
            3600 },
        { "plugin_watch_vsyncs", "watch_vsyncs", "Hot reload check (vsyncs)", nullptr, nullptr, &config.watch_vsyncs, 1,
            3600 },
    };
    return list;
}
double get_tunable(const Tunable& t) { return t.d ? *t.d : t.i ? (double)*t.i : (double)*t.k; }
void set_tunable(const Tunable& t, double value)
{
    value = value < t.lo ? t.lo : value > t.hi ? t.hi : value;
    if (t.d)
        *t.d = value;
    else if (t.i)
        *t.i = (long long)value;
    else
        *t.k = (int)value;
    config_changed();
}

} // namespace rl

extern "C" void pc_lua_init(void)
{
    if (rl::initialized())
        return;
    rl::init();
    std::atexit(pc_lua_shutdown);
}

extern "C" void pc_lua_shutdown(void)
{
    if (!rl::initialized() || !rl::on_game_thread())
        return;
    for (rl::Hook hook : rl::flush_hooks())
        hook();
    for (auto& up : rl::all())
        if (up->L)
            rl::flush_storage(up.get());
}

extern "C" void pc_lua_tick(void)
{
    if (rl::initialized())
        rl::tick();
}

extern "C" void pc_lua_reload_all(void)
{
    if (rl::initialized())
        rl::request_reload_all(); /* at the next safe point (settle) */
}

extern "C" void pc_lua_frame(double dt)
{
    if (rl::initialized())
        rl::frame(dt);
}

extern "C" void pc_lua_settings_begin(void)
{
    rl::enabled_override.clear();
    rl::order.clear();
    rl::grants().clear();
}

extern "C" int pc_lua_settings_parse(const char* key, const char* rest)
{
    char a[256], b[1024];
    long long n;
    if (!std::strcmp(key, "plugins_elevated")) {
        if (std::sscanf(rest, "%lld", &n) == 1)
            rl::config.elevated = n != 0;
        rl::config_changed();
        return 1;
    }
    if (!std::strcmp(key, "plugin_enabled")) {
        if (std::sscanf(rest, "%255s %lld", a, &n) == 2)
            rl::enabled_override[a] = n != 0;
        rl::config_changed();
        return 1;
    }
    if (!std::strcmp(key, "plugin_order")) {
        rl::order.clear();
        if (std::sscanf(rest, "%1023s", b) == 1)
            for (char* id = std::strtok(b, ","); id; id = std::strtok(nullptr, ","))
                rl::order.push_back(id);
        rl::config_changed();
        return 1;
    }
    if (!std::strcmp(key, "plugin_grant")) {
        char hash[80], cap[512];
        if (std::sscanf(rest, "%255s %79s %511s", a, hash, cap) == 3) {
            std::string why;
            if (!rl::capability_ok(cap, &why) || !rl::filesystem_grant_ok(cap, &why)) {
                rl::log(rl::LOG_WARN, a, "refused settings grant: " + why);
                return 1;
            }
            bool known = false;
            for (const rl::Grant& g : rl::grants())
                known |= g.plugin == a && g.hash == hash && g.capability == cap;
            if (!known)
                rl::grants().push_back({ a, hash, cap });
        }
        rl::config_changed();
        return 1;
    }
    for (const rl::Tunable& t : rl::tunables()) {
        if (std::strcmp(key, t.key))
            continue;
        double value;
        if (std::sscanf(rest, "%lf", &value) == 1)
            rl::set_tunable(t, value);
        return 1;
    }
    return 0;
}

extern "C" void pc_lua_settings_end(void) { rl::config_changed(); }

extern "C" void pc_lua_settings_write(FILE* f)
{
    std::fprintf(f, "plugins_elevated %d\n", rl::config.elevated ? 1 : 0);
    for (const rl::Tunable& t : rl::tunables())
        std::fprintf(f, "%s %.10g\n", t.key, rl::get_tunable(t));
    for (const auto& e : rl::enabled_override)
        std::fprintf(f, "plugin_enabled %s %d\n", e.first.c_str(), e.second ? 1 : 0);
    if (!rl::order.empty()) {
        std::string joined;
        for (const std::string& id : rl::order)
            joined += (joined.empty() ? "" : ",") + id;
        std::fprintf(f, "plugin_order %s\n", joined.c_str());
    }
    for (const rl::Grant& g : rl::grants())
        std::fprintf(f, "plugin_grant %s %s %s\n", g.plugin.c_str(), g.hash.c_str(), g.capability.c_str());
}
