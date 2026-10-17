// Capabilities (pc/docs/Extensions.md, "Capabilities"): what a plugin may
// ask for beyond the sandbox, declared in its manifest ("fs.read",
// "fs.write:pc/user/screens", "net:example.com", "exec", "memory.read",
// "memory.write", "flags.write", "native"), granted by the user per plugin
// and per capability, tied to the plugin's content hash. Implemented in
// M0: fs.read and fs.write (the plugin's own folder and the folders the
// grant names); the others exist when granted and refuse with a plain
// message. Every elevated call is audited (the log,
// <ROOD_PLUGIN_DATA>/audit.log, RPC lua {action "audit"}).
#include "rl_internal.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace rl {

const std::vector<CapabilityInfo>& capability_table()
{
    static const std::vector<CapabilityInfo> table = {
        { "fs.read", true, true, "Read files in its own folder and in the folders you approve" },
        { "fs.write", true, true, "Create and change files in its own folder and in the folders you approve" },
        { "net", true, false, "Connect to the internet (the hosts it lists)" },
        { "exec", false, false, "Run programs on your computer" },
        { "memory.read", false, false, "Read the game's memory" },
        { "memory.write", false, false, "Change the game's memory (this can corrupt saves)" },
        { "flags.write", false, false, "Change the game's story flags (this can corrupt saves)" },
        { "native", false, false, "Load native code (full access to your computer)" },
    };
    return table;
}

const CapabilityInfo* capability_info(const std::string& name)
{
    for (const CapabilityInfo& c : capability_table())
        if (name == c.name)
            return &c;
    return nullptr;
}

bool capability_ok(const std::string& capability, std::string* why)
{
    size_t colon = capability.find(':');
    std::string name = capability.substr(0, colon);
    std::string scope = colon == std::string::npos ? std::string() : capability.substr(colon + 1);
    const CapabilityInfo* info = capability_info(name);
    if (!info) {
        *why = "unknown capability '" + name + "' (fs.read, fs.write, net, exec, memory.read, memory.write, "
               "flags.write, native)";
        return false;
    }
    if (colon != std::string::npos && !info->scoped) {
        *why = "'" + name + "' takes no scope";
        return false;
    }
    if (colon != std::string::npos && scope.empty()) {
        *why = "an empty scope";
        return false;
    }
    for (char c : scope)
        if (std::isspace((unsigned char)c)) {
            *why = "a scope may not contain spaces";
            return false;
        }
    if (name == "fs.read" || name == "fs.write") {
        std::string path = scope;
        for (char& c : path)
            if (c == '\\')
                c = '/';
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.find('/', start);
            if (end == std::string::npos)
                end = path.size();
            if (path.compare(start, end - start, "..") == 0 && end - start == 2) {
                *why = "a scope may not contain '..'";
                return false;
            }
            start = end + 1;
        }
    }
    return true;
}

namespace {
std::deque<std::string>& audit_buffer()
{
    static auto* lines = new std::deque<std::string>();
    return *lines;
}

std::string name_of(const std::string& capability) { return capability.substr(0, capability.find(':')); }

/* The plugin's own folder and the folders its grants of `which` (fs.read / fs.write) name. */
bool roots_for(Plugin* p, const char* which, std::vector<std::string>& roots)
{
    roots.clear();
    bool any = false;
    for (const std::string& cap : p->granted) {
        if (name_of(cap) != which)
            continue;
        any = true;
        size_t colon = cap.find(':');
        if (colon != std::string::npos) {
            std::string scope = cap.substr(colon + 1);
            for (char& ch : scope)
                if (ch == '\\')
                    ch = '/';
            while (scope.size() > 1 && scope.back() == '/')
                scope.pop_back();
            roots.push_back(scope);
        }
    }
    if (any)
        roots.insert(roots.begin(), p->dir);
    return any;
}

/* A link inside the folder must not lead out of it (lua_files.cpp inside). */
void check_inside(lua_State* L, const std::string& root, const std::string& full, const char* path)
{
    std::string why;
    if (!inside(root, full, &why))
        luaL_error(L, "rood.fs: '%s' refused: %s", path, why.c_str());
}

std::string resolve(lua_State* L, const char* which, const char* path)
{
    Plugin* p = plugin_of(L);
    std::vector<std::string> roots;
    if (!roots_for(p, which, roots))
        luaL_error(L, "rood.fs: '%s' is not granted", which);
    std::string s = path;
    for (char& ch : s)
        if (ch == '\\')
            ch = '/';
    std::string why;
    for (size_t k = 1; k < roots.size(); k++) { /* a granted folder, as the manifest names it */
        const std::string& root = roots[k];
        if (s != root && (s.size() <= root.size() || s.compare(0, root.size(), root) != 0 || s[root.size()] != '/'))
            continue;
        std::string rest = s.size() > root.size() ? s.substr(root.size() + 1) : std::string();
        if (!rest.empty() && !safe_relative(rest, &why))
            luaL_error(L, "rood.fs: bad path '%s': %s", path, why.c_str());
        check_inside(L, root, s, path);
        return s;
    }
    if (!safe_relative(s, &why))
        luaL_error(L, "rood.fs: bad path '%s': %s (paths are relative to the plugin's folder or inside a granted folder)",
            path, why.c_str());
    std::string full = p->dir + "/" + s;
    check_inside(L, p->dir, full, path);
    return full;
}

int fs_read(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::string full = resolve(L, "fs.read", path);
    audit(plugin_of(L), "fs.read", path);
    std::string text, why;
    if (!read_file(full, &text, RL_MAX_FILE * 4, &why)) {
        luaL_pushfail(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}
int fs_exists(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::string full = resolve(L, "fs.read", path);
    lua_pushboolean(L, file_stat(full, nullptr, nullptr, nullptr));
    return 1;
}
int fs_list(lua_State* L)
{
    const char* path = luaL_optstring(L, 1, ".");
    std::string full = std::strcmp(path, ".") ? resolve(L, "fs.read", path) : plugin_of(L)->dir;
    audit(plugin_of(L), "fs.read", std::string(path) + " (list)");
    std::vector<std::string> names = list_dir(full);
    lua_createtable(L, (int)names.size(), 0);
    for (size_t k = 0; k < names.size(); k++) {
        bool is_dir = false;
        file_stat(full + "/" + names[k], nullptr, nullptr, &is_dir);
        lua_pushstring(L, (names[k] + (is_dir ? "/" : "")).c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}
int fs_write(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    size_t length;
    const char* text = luaL_checklstring(L, 2, &length);
    std::string full = resolve(L, "fs.write", path);
    if (length > RL_MAX_FILE * 4)
        return luaL_error(L, "rood.fs.write: larger than %d MB", (int)(RL_MAX_FILE * 4 >> 20));
    audit(plugin_of(L), "fs.write", std::string(path) + " (" + std::to_string(length) + " bytes)");
    std::string why;
    if (!write_file(full, std::string(text, length), &why))
        return luaL_error(L, "rood.fs.write: %s", why.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int fs_remove(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::string full = resolve(L, "fs.write", path);
    if (full == plugin_of(L)->dir)
        return luaL_error(L, "rood.fs.remove: not the plugin's own folder");
    audit(plugin_of(L), "fs.write", std::string(path) + " (remove)");
    lua_pushboolean(L, std::remove(full.c_str()) == 0);
    return 1;
}

/* A granted capability this version does not provide yet: it says so, plainly. */
int stub_call(lua_State* L)
{
    const char* cap = lua_tostring(L, lua_upvalueindex(1));
    const char* fn = lua_tostring(L, lua_upvalueindex(2));
    audit(plugin_of(L), cap, std::string(fn) + " (not available)");
    return luaL_error(L, "%s: not available in this version of rood-boi (the '%s' capability is granted but not "
                         "implemented yet)", fn, cap);
}
} // namespace

std::vector<Grant>& grants()
{
    static auto* list = new std::vector<Grant>();
    return *list;
}

std::vector<std::string> active_grants(const Plugin* p)
{
    std::vector<std::string> out;
    if (!config.elevated || p->console)
        return out;
    for (const std::string& cap : p->manifest.capabilities)
        for (const Grant& g : grants())
            if (g.plugin == p->id() && g.hash == p->hash && g.capability == cap) {
                out.push_back(cap);
                break;
            }
    return out;
}

std::vector<std::string> pending_grants(const Plugin* p)
{
    std::vector<std::string> out;
    for (const Grant& g : grants())
        if (g.plugin == p->id() && g.hash != p->hash)
            out.push_back(g.capability);
    return out;
}

bool grant(const std::string& plugin, const std::string& capability, bool revoke, std::string* error)
{
    Plugin* p = find(plugin);
    if (!p || p->console || !p->manifest_ok) {
        *error = "no plugin '" + plugin + "'";
        return false;
    }
    const std::vector<std::string>& declared = p->manifest.capabilities;
    if (std::find(declared.begin(), declared.end(), capability) == declared.end()) {
        *error = "plugin '" + plugin + "' does not declare '" + capability + "'";
        return false;
    }
    if (p->hash.empty())
        p->hash = content_hash(p->dir);
    std::vector<Grant>& list = grants();
    list.erase(std::remove_if(list.begin(), list.end(),
                   [&](const Grant& g) { return g.plugin == plugin && g.capability == capability; }),
        list.end());
    if (!revoke)
        list.push_back({ plugin, p->hash, capability });
    audit(p, revoke ? "revoke" : "grant", capability);
    config_changed();
    save_settings_now();
    return true;
}

void audit(Plugin* p, const std::string& capability, const std::string& detail)
{
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::string line = std::string(stamp) + " " + p->id() + " " + capability + " " + detail;
    audit_buffer().push_back(line);
    while (audit_buffer().size() > 500)
        audit_buffer().pop_front();
    make_dirs(data_root());
    if (FILE* f = std::fopen((data_root() + "/audit.log").c_str(), "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fclose(f);
    }
    log(LOG_INFO, p->id(), "[audit] " + capability + " " + detail);
}
const std::deque<std::string>& audit_lines() { return audit_buffer(); }

void open_capabilities(lua_State* L, Plugin* p)
{
    bool read = false, write = false;
    for (const std::string& cap : p->granted) {
        read |= name_of(cap) == "fs.read";
        write |= name_of(cap) == "fs.write";
    }
    if (read || write) {
        lua_newtable(L);
        if (read) {
            set_cfunction(L, -1, "read", fs_read);
            set_cfunction(L, -1, "exists", fs_exists);
            set_cfunction(L, -1, "list", fs_list);
        }
        if (write) {
            set_cfunction(L, -1, "write", fs_write);
            set_cfunction(L, -1, "remove", fs_remove);
        }
        lock_table(L);
        lua_setfield(L, -2, "fs");
    }
    struct Stub {
        const char* capability;
        const char* table;
        const char* field;
    };
    static const Stub stubs[] = {
        { "net", "net", "request" }, { "exec", "exec", "run" }, { "memory.read", "memory", "read" },
        { "memory.write", "memory", "write" }, { "flags.write", "flags", "write" }, { "native", "native", "load" },
    };
    for (const char* table : { "net", "exec", "memory", "flags", "native" }) {
        bool made = false;
        for (const Stub& s : stubs) {
            if (std::strcmp(s.table, table))
                continue;
            bool granted = false;
            for (const std::string& cap : p->granted)
                granted |= name_of(cap) == s.capability;
            if (!granted)
                continue;
            if (!made) {
                lua_newtable(L);
                made = true;
            }
            lua_pushstring(L, s.capability);
            lua_pushfstring(L, "rood.%s.%s", s.table, s.field);
            lua_pushcclosure(L, stub_call, 2);
            lua_setfield(L, -2, s.field);
        }
        if (made) {
            lock_table(L);
            lua_setfield(L, -2, table);
        }
    }
}

} // namespace rl
