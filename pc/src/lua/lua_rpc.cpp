// RPC method lua (dev_rpc.c; dev-only: the RPC exists only with ROOD_RPC;
// pc/docs/Extensions.md "Developer tools"). Actions:
//   eval {code, plugin}       run code in a plugin (default: the console's own
//                             sandbox) under the test budget: {ok, results [JSON],
//                             output [what it logged], error}
//   console {input, plugin}   as typed into the overlay's console (history, echo)
//   plugins                   every plugin: state, status, manifest, grants, memory,
//                             faults, budget kills, time, handlers, timers, entries
//   reload {plugin}           one (and what depends on it); without plugin, all
//   rescan                    new folders load, removed ones unload
//   enable {plugin, enabled}  settings plugin_enabled
//   grant {plugin, capability, revoke, elevated}
//                             a grant as the user's dialog will make it (dev mode;
//                             elevated: the master switch), saved in settings.cfg
//   budget {budget_ms, budget_instructions, load_budget_ms, load_budget_instructions,
//           memory_mb, fault_limit, fault_window_s, watch_vsyncs, reset_stats}
//                             the tunables (tunables()), then them and every plugin
//   test {plugin, filter}     start a test run (rood.test); test_status {run} polls it
//   log {since}               log lines after a sequence number
//   audit                     the elevated calls (recent)
//   emit {event, payload}     a core event to every plugin (pc_lua_emit_json)
//   registry {kind}           the registry's entries (every kind without kind)
// Also pc_lua_emit_json, the game's way to emit an event (M1's menu hooks).
#include "rl_internal.h"
#include "light_prefabs.h"
#include "lua_runtime.h"

#include <cstdio>
#include <cstring>

namespace rl {
std::vector<std::string>& console_history()
{
    static auto* history = new std::vector<std::string>();
    return *history;
}

bool console_submit(const std::string& target, const std::string& text, std::vector<Value>* results, std::string* error)
{
    std::vector<std::string>& history = console_history();
    if (history.empty() || history.back() != text)
        history.push_back(text);
    if (history.size() > 200)
        history.erase(history.begin());
    log(LOG_INFO, "console", "> " + text);
    Plugin* p = target.empty() || target == "@console" ? console() : find(target);
    if (!p || p->state != PLUGIN_LOADED) {
        std::string why = "no loaded plugin '" + target + "'";
        log(LOG_ERROR, "console", why);
        if (error)
            *error = why;
        return false;
    }
    std::vector<Value> local;
    std::vector<Value>* out = results ? results : &local;
    if (!eval(p, text, out, error))
        return false;
    if (!out->empty()) {
        std::string shown;
        for (size_t k = 0; k < out->size(); k++)
            shown += (k ? ", " : "") + to_json((*out)[k]);
        if (shown.size() > 2000)
            shown = shown.substr(0, 2000) + "...";
        log(LOG_INFO, "console", "= " + shown);
    }
    return true;
}
} // namespace rl

namespace {
using prefab::Json;

std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string num(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.10g", v);
    return buffer;
}
std::string list(const std::vector<std::string>& items)
{
    std::string s = "[";
    for (size_t k = 0; k < items.size(); k++)
        s += (k ? "," : "") + q(items[k]);
    return s + "]";
}
const std::string* text_param(const Json& params, const char* key)
{
    const Json* j = params.get(key);
    return j && j->type == Json::String ? &j->string : nullptr;
}
bool flag_param(const Json& params, const char* key, bool fallback)
{
    const Json* j = params.get(key);
    return j && j->type == Json::Bool ? j->boolean : fallback;
}

std::string plugin_json(const rl::Plugin& p)
{
    const rl::Manifest& m = p.manifest;
    std::vector<std::string> external;
    for (const std::string& cap : p.granted)
        if (rl::external_filesystem_grant(&p, cap))
            external.push_back(cap);
    return "{\"id\":" + q(m.id) + ",\"name\":" + q(m.name) + ",\"version\":" + q(m.version)
        + ",\"state\":" + q(rl::state_name(p.state)) + ",\"status\":" + q(p.status) + ",\"dir\":" + q(p.dir)
        + ",\"shipped\":" + (p.shipped ? "true" : "false") + ",\"console\":" + (p.console ? "true" : "false")
        + ",\"enabled\":" + (p.console || rl::plugin_enabled(&p) ? "true" : "false")
        + ",\"generation\":" + std::to_string(p.generation) + ",\"hash\":" + q(p.hash)
        + ",\"external_filesystem_grants\":" + list(external)
        + ",\"declared\":" + list(m.capabilities) + ",\"granted\":" + list(p.granted)
        + ",\"pending\":" + list(rl::pending_grants(&p)) + ",\"elevated\":" + (p.granted.empty() ? "false" : "true")
        + ",\"memory\":{\"used_kb\":" + num(p.mem.used / 1024.0) + ",\"peak_kb\":" + num(p.mem.peak / 1024.0)
        + ",\"cap_kb\":" + num(p.mem.cap / 1024.0) + ",\"refusals\":" + std::to_string(p.mem.refusals) + "}"
        + ",\"calls\":" + std::to_string(p.stats.calls) + ",\"faults\":" + std::to_string(p.stats.faults)
        + ",\"kills\":" + std::to_string(p.stats.kills) + ",\"max_ms\":" + num(p.stats.max_ms)
        + ",\"total_ms\":" + num(p.stats.total_ms) + ",\"handlers\":" + std::to_string(rl::handler_count(m.id))
        + ",\"timers\":" + std::to_string(rl::timer_count(m.id)) + ",\"entries\":" + std::to_string(rl::registry_count(m.id))
        + "}";
}

std::string plugins_reply()
{
    std::string s = "{\"version\":" + q(RL_VERSION) + ",\"api_version\":" + std::to_string(RL_API_VERSION)
        + ",\"elevated\":" + (rl::config.elevated ? "true" : "false") + ",\"plugins\":[";
    bool first = true;
    for (auto& p : rl::all()) {
        s += (first ? "" : ",") + plugin_json(*p);
        first = false;
    }
    return s + "]}";
}

std::string eval_reply(bool ok, const std::vector<rl::Value>& results, const std::string& error,
    unsigned long long since)
{
    std::string s = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"results\":[";
    for (size_t k = 0; k < results.size(); k++)
        s += (k ? "," : "") + rl::to_json(results[k]);
    s += "],\"output\":[";
    bool first = true;
    for (const rl::LogLine& line : rl::log_lines())
        if (line.seq > since) {
            s += (first ? "" : ",") + q(line.text);
            first = false;
        }
    s += "]";
    if (!ok)
        s += ",\"error\":" + q(error);
    return s + "}";
}

const char* level_name(int level) { return level == rl::LOG_ERROR ? "error" : level == rl::LOG_WARN ? "warn" : "info"; }
} // namespace

extern "C" int pc_lua_rpc(const char* params_text, char* out, int size)
{
    auto failure = [&](const std::string& message) {
        std::snprintf(out, (size_t)size, "%s", message.c_str());
        return -1;
    };
    Json params;
    std::string error, reply;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return failure("lua: params: " + (error.empty() ? std::string("expected an object") : error));
    if (!rl::on_game_thread())
        return failure("lua: off the game thread");
    const std::string* a = text_param(params, "action");
    std::string action = a ? *a : "plugins";
    const std::string* plugin = text_param(params, "plugin");
    if (action == "eval" || action == "console") {
        const std::string* code = text_param(params, action == "eval" ? "code" : "input");
        if (!code)
            return failure("lua: " + action + " needs " + (action == "eval" ? "code" : "input"));
        unsigned long long since = rl::log_last_seq();
        std::vector<rl::Value> results;
        bool ok;
        if (action == "console")
            ok = rl::console_submit(plugin ? *plugin : "@console", *code, &results, &error);
        else {
            rl::Plugin* p = plugin ? rl::find(*plugin) : rl::console();
            if (!p || p->state != rl::PLUGIN_LOADED)
                return failure("lua: no loaded plugin '" + (plugin ? *plugin : std::string("@console")) + "'");
            ok = rl::eval(p, *code, &results, &error);
        }
        rl::settle();
        reply = eval_reply(ok, results, error, since);
    } else if (action == "plugins") {
        reply = plugins_reply();
    } else if (action == "reload" || action == "rescan") {
        if (action == "rescan") {
            rl::scan();
            rl::config_changed();
        } else if (plugin) {
            rl::Plugin* p = rl::find(*plugin);
            if (!p || p->console)
                return failure("lua: no plugin '" + *plugin + "'");
            p->reload_requested = true;
        } else
            rl::request_reload_all();
        rl::settle();
        reply = plugins_reply();
    } else if (action == "enable") {
        if (!plugin || !rl::find(*plugin))
            return failure("lua: enable needs a known plugin");
        rl::set_enabled(*plugin, flag_param(params, "enabled", true));
        rl::settle();
        reply = plugins_reply();
    } else if (action == "grant") {
        const std::string* capability = text_param(params, "capability");
        if (!plugin || !capability)
            return failure("lua: grant needs plugin and capability");
        if (params.get("elevated")) {
            rl::config.elevated = flag_param(params, "elevated", rl::config.elevated);
            rl::config_changed();
        }
        if (!rl::grant(*plugin, *capability, flag_param(params, "revoke", false), &error))
            return failure("lua: " + error);
        rl::settle();
        reply = plugins_reply();
    } else if (action == "budget") {
        for (const rl::Tunable& t : rl::tunables()) {
            const Json* v = params.get(t.rpc);
            if (v && v->type == Json::Number)
                rl::set_tunable(t, v->number);
        }
        if (flag_param(params, "reset_stats", false))
            for (auto& p : rl::all())
                p->stats = rl::Stats();
        rl::settle();
        reply = "{\"config\":{";
        for (const rl::Tunable& t : rl::tunables())
            reply += q(t.rpc) + ":" + num(rl::get_tunable(t)) + ",";
        reply += "\"elevated\":" + std::string(rl::config.elevated ? "true" : "false") + "},\"plugins\":"
            + plugins_reply() + "}";
    } else if (action == "test") {
        const std::string* filter = text_param(params, "filter");
        int run = rl::start_tests(plugin ? *plugin : "", filter ? *filter : "");
        rl::settle();
        reply = rl::test_run_json(run);
    } else if (action == "test_status") {
        const Json* run = params.get("run");
        if (!run || run->type != Json::Number)
            return failure("lua: test_status needs run");
        reply = rl::test_run_json((int)run->number);
        if (reply.empty())
            return failure("lua: no test run " + num(run->number));
    } else if (action == "log") {
        const Json* since = params.get("since");
        unsigned long long after = since && since->type == Json::Number ? (unsigned long long)since->number : 0;
        reply = "{\"lines\":[";
        bool first = true;
        size_t skip = rl::log_lines().size() > 500 ? rl::log_lines().size() - 500 : 0;
        for (const rl::LogLine& line : rl::log_lines()) {
            if (skip) {
                --skip;
                continue;
            }
            if (line.seq <= after)
                continue;
            reply += std::string(first ? "" : ",") + "{\"seq\":" + std::to_string(line.seq) + ",\"level\":\""
                + level_name(line.level) + "\",\"plugin\":" + q(line.plugin) + ",\"text\":" + q(line.text) + "}";
            first = false;
        }
        reply += "],\"next\":" + std::to_string(rl::log_last_seq()) + "}";
    } else if (action == "audit") {
        std::vector<std::string> lines(rl::audit_lines().begin(), rl::audit_lines().end());
        reply = "{\"lines\":" + list(lines) + "}";
    } else if (action == "emit") {
        const std::string* event = text_param(params, "event");
        if (!event)
            return failure("lua: emit needs event");
        const Json* payload = params.get("payload");
        int handlers = rl::emit(*event, payload ? rl::from_json(*payload) : rl::Value());
        rl::settle();
        reply = "{\"handlers\":" + std::to_string(handlers) + "}";
    } else if (action == "registry") {
        const std::string* kind = text_param(params, "kind");
        std::vector<std::string> kinds = kind ? std::vector<std::string>{ *kind } : rl::registry_kinds();
        reply = "{\"kinds\":" + list(rl::registry_kinds()) + ",\"entries\":[";
        bool first = true;
        for (const std::string& k : kinds)
            for (const rl::RegistryEntry* e : rl::registry_list(k)) {
                reply += std::string(first ? "" : ",") + "{\"kind\":" + q(e->kind) + ",\"id\":" + q(e->id)
                    + ",\"owner\":" + q(e->owner) + ",\"group\":" + q(e->group) + ",\"data\":" + rl::to_json(e->data) + "}";
                first = false;
            }
        reply += "]}";
    } else
        return failure("lua: unknown action '" + action
            + "' (eval, console, plugins, reload, rescan, enable, grant, budget, test, test_status, log, audit, emit, "
              "registry)");
    if ((int)reply.size() >= size)
        return failure("lua: the reply is larger than " + std::to_string(size) + " bytes");
    std::memcpy(out, reply.c_str(), reply.size() + 1);
    return (int)reply.size();
}

extern "C" int pc_lua_emit_json(const char* event, const char* json)
{
    if (!rl::initialized() || !event)
        return 0;
    rl::Value payload;
    if (json && *json) {
        prefab::Json j;
        if (!prefab::json_parse(json, j, nullptr))
            return -1;
        payload = rl::from_json(j);
    }
    int handlers = rl::emit(event, payload);
    rl::settle();
    return handlers;
}
