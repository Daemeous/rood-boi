// rood.menu (pc/src/lua/lua_api_menu.cpp) without the game, over the real
// settings catalog (ui_settings.cpp against ui_settings_stubs.cpp) and the
// Option page model (option_pages.cpp): the shipped rood.options defines
// exactly the C++ fallback's pages, and they come back when it is off or
// broken; a plugin's pages, namespaced and validated (errors raise in the
// plugin), ordered after the core pages; refused actions, a plugin's own
// action run in its sandbox; everything a plugin defined gone when it
// unloads or reloads. ctest lua_menu_probe (argv[1]: a scratch directory the
// probe owns; argv[2]: pc/plugins, rood.options' source).
#include "lua/lua_runtime.h"
#include "lua/rl_internal.h"
#include "hud/game_query.h"
#include "ui/option_actions.h"
#include "ui/option_pages.h"
#include "ui/option_style.h"
#include "ui/ui_settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* rood.game's source (lua_api_game.cpp): nothing in this probe. */
extern "C" void pc_game_query_fill(pc_game_query* out) { *out = pc_game_query(); }
extern "C" int pc_game_query_room_names(int, int, char*, int, char*, int) { return 0; }

static std::string scratch, shipped;
static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
static void write_text(const std::string& path, const std::string& text)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
static std::string eval_json(const char* plugin, const char* code)
{
    rl::Plugin* p = rl::find(plugin);
    std::vector<rl::Value> results;
    std::string error;
    if (!p || !rl::eval(p, code, &results, &error))
        return "error: " + error;
    std::string s;
    for (size_t k = 0; k < results.size(); k++)
        s += (k ? "," : "") + rl::to_json(results[k]);
    return s;
}
static std::string eval_error(const char* plugin, const char* code)
{
    std::string error;
    rl::Plugin* p = rl::find(plugin);
    if (!p || rl::eval(p, code, nullptr, &error))
        return "";
    return error;
}
static std::string state_of(const char* id)
{
    rl::Plugin* p = rl::find(id);
    return p ? std::string(rl::state_name(p->state)) : std::string("missing");
}
static void enable(const char* id, bool on)
{
    pc_lua_settings_parse("plugin_enabled", (std::string(id) + (on ? " 1\n" : " 0\n")).c_str());
    pc_lua_settings_end();
    pc_lua_tick();
}
static void ticks(int n)
{
    for (int k = 0; k < n; k++)
        pc_lua_tick();
}

/* Every listed page and its rows on every screen, as text (what the renderers see). */
static std::string snapshot()
{
    std::string s;
    for (int i = 0; i < pc_option_pages_count(); i++) {
        pc_option_page p;
        if (!pc_option_pages_info(i, &p))
            continue;
        s += std::string("page ") + p.id + " | " + p.title + " | " + p.help + " | " + p.source + " | " + p.group
            + " | " + std::to_string(p.parent) + " " + std::to_string(p.depth) + "\n";
        for (int r = 0; r < pc_option_page_rows(i, PC_UI_WHERE_ALL); r++) {
            pc_option_row o;
            if (!pc_option_page_row(i, PC_UI_WHERE_ALL, r, &o))
                continue;
            s += "  " + std::to_string(o.form) + " " + (o.ref ? o.ref : "-") + " | " + o.row.id + " | " + o.row.label
                + " | " + o.row.help + " | " + std::to_string(o.row.kind) + " " + std::to_string(o.row.where) + " "
                + std::to_string(o.catalog) + " " + std::to_string(o.link) + "\n";
        }
    }
    return s;
}
static std::string top_ids()
{
    int pages[64];
    int n = pc_option_pages_children(-1, pages, 64);
    std::string s;
    for (int k = 0; k < n && k < 64; k++) {
        pc_option_page p;
        pc_option_pages_info(pages[k], &p);
        s += (k ? "," : "") + std::string(p.id);
    }
    return s;
}

/* The shipped pages are the fallback's, row for row; they replace it while loaded. */
static void shipped_equals_fallback(const std::string& fallback)
{
    CHECK(state_of("rood.options") == "loaded");
    CHECK(!pc_option_pages_fallback_active());
    std::string now = snapshot();
    if (now != fallback)
        std::printf("rood.options:\n%s\nthe fallback:\n%s\n", now.c_str(), fallback.c_str());
    CHECK(now == fallback);
    CHECK(rl::registry_get("menu.page", "rood.options/display") != nullptr);
    CHECK(eval_json("rood.options", "local p = rood.menu.pages() return #p, p[1].id, p[7].parent")
        == "7,\"rood.options/gameplay\",\"rood.options/controls\"");
    // its look (M.style through rood.menu.style) is what the C defaults are: keep the two equal
    for (int i = 0; i < pc_option_style_count(); i++) {
        if (pc_option_style_value(i) != pc_option_style_default(i))
            std::printf("style %s: rood.options %g, C %g\n", pc_option_style_name(i), pc_option_style_value(i),
                pc_option_style_default(i));
        CHECK(pc_option_style_value(i) == pc_option_style_default(i));
    }
}

static void validation()
{
    const char* P = "probe.menu";
    auto err = [&](const char* code) { return eval_error(P, code); };
    CHECK(contains(err("rood.menu.page{ id = 'Bad Id', title = 'X' }"), "not a slug"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = '' }"), "1 to 24 characters"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = string.rep('a', 25) }"), "1 to 24 characters"));
    CHECK(err("rood.menu.page{ id = 'x', title = string.rep('\\u{e9}', 24) }").empty()); // characters, not bytes
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'Ma\\u{f1}ana \\u{2603}' }"), "font lacks"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { setting = 'widescreen', header = 'H' } } }"),
        "exactly one"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { label = 'nothing' } } }"), "exactly one"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { 'no_such_setting' } }"), "unknown setting"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { 'plugin:probe.menu/speed' } }"),
        "is not defined"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { 42 } }"), "a setting's id or a table"));
    CHECK(contains(err("local r = {} for i = 1, 65 do r[i] = 'widescreen' end rood.menu.page{ id = 'x', title = 'X', rows = r }"),
        "more than 64 rows"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', after = 'nowhere' }"), "'probe.menu/nowhere' is not a page"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', before = { 'rood.options/nope' } }"), "not a page"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', parent = 'rood.options/nope' }"), "parent"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', group = 'system' }"), "shipped pages'"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { action = 'plugins.reload' } } }"),
        "not offered by plugins' pages"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { action = 'no.such' } } }"), "no core action"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { action = 'probe.other/go' } } }"),
        "another plugin"));
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { define = 'fast', kind = 'toggle' } } }"),
        "rood.settings")); // the settings API is the game's (lua_api_settings.cpp)
    CHECK(contains(err("rood.menu.page{ id = 'x', title = 'X', rows = { { setting = 'widescreen', where = 'up' } } }"),
        "where"));
    CHECK(contains(err("rood.menu.action('Bad', function() end)"), "not a slug"));
    CHECK(contains(err("rood.menu.style{ classic = { row_x = 10 } }"), "only rood.options"));
    CHECK(eval_json(P, "return rood.menu.style().classic_value_x") == "140");
    // a refused page left nothing behind (the one valid title above was defined, then is redefined)
    CHECK(pc_option_pages_index("probe.menu/x") >= 0);
    CHECK(eval_json(P, "rood.menu.remove('x')") == "true" && pc_option_pages_index("probe.menu/x") == -1);
    CHECK(eval_json(P, "rood.menu.remove('x')") == "false");
    // 16 pages a plugin
    CHECK(contains(err("for i = 1, 17 do rood.menu.page{ id = 'p' .. i, title = 'P' } end"), "more than 16 pages"));
    CHECK(eval_json(P, "for i = 1, 16 do rood.menu.remove('p' .. i) end return #rood.menu.pages()") == "7");
}

static void a_plugins_page()
{
    const char* P = "probe.menu";
    unsigned gen = pc_option_pages_generation();
    CHECK(eval_json(P, "return rood.menu.page{ id = 'tweaks', title = 'Tweaks', help = 'Mine.', rows = {"
                       " 'widescreen', { header = 'Advanced' }, { help = 'Restart to apply.' },"
                       " { setting = 'aspect', label = 'Fill', help = 'How it fills.' },"
                       " { page = 'more' }, { action = 'settings.save' }, { action = 'clear', label = 'Clear' },"
                       " { setting = 'plugin:other.plugin/speed', label = 'Speed' },"
                       " { setting = 'skip_intro', where = 'modern' } } }")
        == "\"probe.menu/tweaks\"");
    CHECK(pc_option_pages_generation() != gen);
    // after every core page (group plugins), whatever the load order
    CHECK(top_ids() == "rood.options/gameplay,rood.options/display,rood.options/graphics,rood.options/audio,"
                       "rood.options/interface,rood.options/controls,probe.menu/tweaks");
    CHECK(eval_json(P, "local r = rood.menu.rows('tweaks') return #r, r[2].kind, r[2].label, r[3].kind, r[4].label,"
                       " r[5].kind, r[5].enabled, r[6].kind, r[7].enabled, r[8].enabled, r[8].reason")
        == "9,\"header\",\"Advanced\",\"note\",\"Fill\",\"link\",false,\"action\",false,false,"
           "\"Not available: its plugin is not loaded\"");
    CHECK(eval_json(P, "#rood.menu.rows('tweaks', 'classic'), #rood.menu.rows('tweaks', 'modern')") == "8,9");
    // the link's page arrives: it works; before / after among the plugins' pages
    CHECK(eval_json(P, "rood.menu.page{ id = 'more', title = 'More', parent = 'tweaks' }"
                       " rood.menu.page{ id = 'first', title = 'First', before = 'tweaks' }"
                       " local r = rood.menu.rows('tweaks') return r[5].enabled, r[5].page")
        == "true,\"probe.menu/more\"");
    CHECK(top_ids() == "rood.options/gameplay,rood.options/display,rood.options/graphics,rood.options/audio,"
                       "rood.options/interface,rood.options/controls,probe.menu/first,probe.menu/tweaks");
    pc_option_page info;
    CHECK(pc_option_pages_info(pc_option_pages_index("probe.menu/more"), &info) && info.depth == 1
        && !std::strcmp(info.group, ""));
    // its action: greyed until registered, then run in the plugin's sandbox
    CHECK(pc_option_action_exists("probe.menu/clear") == 0 && pc_option_action_run("probe.menu/clear") == 0);
    CHECK(eval_json(P, "rood.menu.action('clear', function() cleared = (cleared or 0) + 1 end,"
                       " { help = 'Forget it all.' })")
        == "\"probe.menu/clear\"");
    CHECK(pc_option_action_exists("probe.menu/clear") == 2 && pc_option_action_run("probe.menu/clear") == 1);
    CHECK(eval_json(P, "cleared") == "1");
    CHECK(eval_json(P, "local r = rood.menu.rows('tweaks')[7] return r.enabled, r.label, r.help")
        == "true,\"Clear\",\"Forget it all.\"");
    // an action that raises: a fault of its plugin, not the caller's
    CHECK(eval_json(P, "rood.menu.action('boom', function() error('no') end)") == "\"probe.menu/boom\"");
    CHECK(pc_option_action_run("probe.menu/boom") == 0 && state_of(P) == "loaded");
    // core actions listed with their flag
    CHECK(eval_json(P, "local a = rood.menu.actions() return a[1].id, a[1].plugin_ok, a[2].id, a[2].plugin_ok")
        == "\"settings.save\",true,\"plugins.reload\",false");
    // another plugin sees it (the registry), and cannot remove it
    CHECK(eval_json("probe.other", "local e = rood.registry.get('menu.page', 'probe.menu/tweaks') return e.owner, e.group")
        == "\"probe.menu\",\"plugins\"");
    CHECK(contains(eval_error("probe.other", "rood.menu.remove('probe.menu/tweaks')"), "belongs to plugin probe.menu"));
    CHECK(contains(eval_error("probe.other", "rood.menu.page{ id = 'probe.menu/tweaks', title = 'Mine' }"), "not a slug"));
    // a redefinition in place keeps its place; pages() lists the tree
    CHECK(eval_json(P, "rood.menu.page{ id = 'tweaks', title = 'Tweaks 2', rows = { 'filter' } }"
                       " local n = 0 for _, p in ipairs(rood.menu.pages()) do if p.source == 'probe.menu' then n = n + 1 end end"
                       " return n, #rood.menu.rows('tweaks')")
        == "3,1");
    CHECK(top_ids() == "rood.options/gameplay,rood.options/display,rood.options/graphics,rood.options/audio,"
                       "rood.options/interface,rood.options/controls,probe.menu/first,probe.menu/tweaks");
}

/* Hot reload: a changed plugin loses its pages and actions and defines them again;
 * turned off, they are gone. */
static void reload_and_off()
{
    write_text(scratch + "/plugins/probe.menu/main.lua",
        "rood.menu.page{ id = 'tweaks', title = 'Reloaded', rows = { 'widescreen' } }\n");
    rl::config.watch_vsyncs = 2;
    ticks(3);
    rl::config.watch_vsyncs = 30;
    CHECK(state_of("probe.menu") == "loaded");
    CHECK(pc_option_pages_index("probe.menu/first") == -1 && pc_option_pages_index("probe.menu/more") == -1);
    pc_option_page info;
    CHECK(pc_option_pages_info(pc_option_pages_index("probe.menu/tweaks"), &info) && !std::strcmp(info.title, "Reloaded"));
    CHECK(pc_option_action_exists("probe.menu/clear") == 0);
    CHECK(rl::registry_get("menu.page", "probe.menu/first") == nullptr);
    enable("probe.menu", false);
    CHECK(state_of("probe.menu") == "off" && pc_option_pages_index("probe.menu/tweaks") == -1);
    CHECK(rl::registry_list("menu.page").size() == 7);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: lua_menu_probe <scratch directory> <pc/plugins>\n");
        return 2;
    }
    scratch = argv[1];
    std::filesystem::remove_all(scratch);
    shipped = scratch + "/shipped";
    std::filesystem::create_directories(shipped);
    std::filesystem::copy(std::string(argv[2]) + "/rood.options", shipped + "/rood.options",
        std::filesystem::copy_options::recursive);
    setenv("ROOD_PLUGINS_SHIPPED", shipped.c_str(), 1);
    setenv("ROOD_PLUGINS", (scratch + "/plugins").c_str(), 1);
    setenv("ROOD_PLUGIN_DATA", (scratch + "/data").c_str(), 1);
    write_text(scratch + "/plugins/probe.menu/plugin.lua", "return { id = 'probe.menu', api_version = 1 }\n");
    write_text(scratch + "/plugins/probe.menu/main.lua", "\n");
    write_text(scratch + "/plugins/probe.other/plugin.lua", "return { id = 'probe.other', api_version = 1 }\n");
    write_text(scratch + "/plugins/probe.other/main.lua", "\n");

    // Without Lua: the C++ fallback.
    CHECK(pc_option_pages_fallback_active() && pc_option_pages_count() == 7);
    const std::string fallback = snapshot();
    pc_lua_init();
    shipped_equals_fallback(fallback);
    validation();
    a_plugins_page();
    reload_and_off();
    // rood.options off: the fallback, the same pages; on again: its own
    enable("rood.options", false);
    CHECK(state_of("rood.options") == "off" && pc_option_pages_fallback_active() && snapshot() == fallback);
    enable("rood.options", true);
    shipped_equals_fallback(fallback);
    // broken: the fallback until it is fixed
    std::string main_lua = shipped + "/rood.options/main.lua";
    std::string good;
    {
        std::ifstream in(main_lua, std::ios::binary);
        good.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    write_text(main_lua, good + "\nerror('broken on purpose')\n");
    rl::config.watch_vsyncs = 2;
    ticks(3);
    CHECK(state_of("rood.options") == "failed" && pc_option_pages_fallback_active() && snapshot() == fallback);
    write_text(main_lua, good);
    ticks(3);
    rl::config.watch_vsyncs = 30;
    shipped_equals_fallback(fallback);
    if (failures) {
        std::printf("lua_menu_probe: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("lua_menu_probe: ok\n");
    return 0;
}
