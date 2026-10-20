// The Option pages model (pc/src/ui/option_pages.cpp) over the real settings
// catalog (ui_settings.cpp, against ui_settings_stubs.cpp): the C++ fallback
// pages are today's System pages row for row, every catalog row is on exactly
// one of them, sibling order by after / before / group, parent and link
// cycles refused, rows of every form, the generation.
#include "ui/option_pages.h"
#include "ui/ui_settings.h"
#include "fast_travel.h"
#include "ui_settings_stubs.h"
#include "button_prompts.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
extern int pc_hw_interpolate, pc_qol_fast_travel_cost;
}

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static bool same(const char* a, const char* b) { return !std::strcmp(a ? a : "", b ? b : ""); }

static std::string page_id(int page)
{
    pc_option_page p;
    return pc_option_pages_info(page, &p) ? p.id : "";
}
static std::vector<std::string> top_ids()
{
    int pages[64];
    int n = pc_option_pages_children(-1, pages, 64);
    std::vector<std::string> ids;
    for (int k = 0; k < n && k < 64; k++)
        ids.push_back(page_id(pages[k]));
    return ids;
}
static std::vector<std::string> row_ids(int page, int where)
{
    std::vector<std::string> ids;
    for (int i = 0, n = pc_option_page_rows(page, where); i < n; i++) {
        pc_option_row r;
        if (pc_option_page_row(page, where, i, &r))
            ids.push_back(r.row.id);
    }
    return ids;
}
static std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : ",") + x;
    return s;
}

// ---- the fallback = today's System pages ----

static void fallback_tree()
{
    CHECK(pc_option_pages_fallback_active());
    CHECK(pc_option_pages_count() == 7);
    CHECK(join(top_ids()) == "rood.options/gameplay,rood.options/display,rood.options/graphics,rood.options/audio,"
                             "rood.options/interface,rood.options/controls");
    int controls = pc_option_pages_index("rood.options/controls");
    int bindings = pc_option_pages_index("rood.options/bindings");
    pc_option_page p;
    CHECK(pc_option_pages_info(bindings, &p) && p.parent == controls && p.depth == 1);
    CHECK(bindings == controls + 1); // tree order: sub-pages follow their parent
    CHECK(same(p.source, "rood.options") && same(p.group, ""));
    CHECK(pc_option_pages_info(controls, &p) && p.parent == -1 && p.depth == 0 && same(p.group, "system"));
    CHECK(same(p.help, "The right stick camera and vibration."));
    int children[4];
    CHECK(pc_option_pages_children(controls, children, 4) == 1 && children[0] == bindings);
    CHECK(pc_option_pages_children(bindings, children, 4) == 0);
    CHECK(pc_option_pages_index("rood.options/nothing") == -1);
    CHECK(!pc_option_pages_info(7, &p) && !pc_option_pages_info(-1, &p));
}

// Each catalog home (the System pages before the page model), row by row: the
// fallback page of that id shows exactly the home's rows the catalog shows now,
// in its order, as the catalog fills them; and the System pages
// (pc_ui_settings_*, the page model's since B1) show the same.
static void fallback_matches_settings()
{
    CHECK(pc_ui_settings_catalog_homes() == 7);
    for (int home = 0; home < pc_ui_settings_catalog_homes(); home++) {
        const char* title = nullptr;
        std::string id = pc_ui_settings_catalog_home(home, &title);
        int page = pc_option_pages_index(id.c_str());
        pc_option_page info;
        CHECK(page >= 0 && pc_option_pages_info(page, &info));
        if (page < 0)
            continue;
        CHECK(same(info.title, title) && same(pc_ui_settings_page_title(page), title));
        CHECK(pc_ui_settings_page_index(id.c_str()) == page && same(pc_ui_settings_page_id(page), id.c_str() + 13));
        std::vector<int> want;
        for (int i = 0; i < pc_ui_settings_catalog_count(); i++) {
            pc_ui_setting_row r;
            if (pc_ui_settings_catalog_shown(i) && pc_ui_settings_catalog_row(i, &r) && same(r.home, id.c_str())
                && (r.where & PC_UI_WHERE_MODERN))
                want.push_back(i);
        }
        int n = (int)want.size();
        if (pc_option_page_rows(page, PC_UI_WHERE_MODERN) != n)
            std::printf("page %s: %d rows, the catalog %d (%s)\n", id.c_str(),
                pc_option_page_rows(page, PC_UI_WHERE_MODERN), n, join(row_ids(page, PC_UI_WHERE_MODERN)).c_str());
        CHECK(pc_option_page_rows(page, PC_UI_WHERE_MODERN) == n && pc_ui_settings_rows(page) == n);
        for (int i = 0; i < n && i < pc_option_page_rows(page, PC_UI_WHERE_MODERN); i++) {
            pc_ui_setting_row r0, sys;
            pc_option_row got;
            CHECK(pc_ui_settings_catalog_row(want[(size_t)i], &r0));
            const pc_ui_setting_row& want_row = r0;
            CHECK(pc_option_page_row(page, PC_UI_WHERE_MODERN, i, &got));
            CHECK(pc_ui_settings_row(page, i, &sys));
            for (const pc_ui_setting_row* r : {&got.row, &sys}) {
                bool ok = got.form == PC_OPT_SETTING && got.catalog == want[(size_t)i] && same(r->id, want_row.id)
                    && same(r->label, want_row.label) && same(r->help, want_row.help) && r->kind == want_row.kind
                    && same(r->value, want_row.value) && r->enabled == want_row.enabled
                    && same(r->reason, want_row.reason) && r->index == want_row.index && r->count == want_row.count
                    && r->number == want_row.number && r->min == want_row.min && r->max == want_row.max
                    && r->button == want_row.button && r->game_option == want_row.game_option
                    && same(r->home, id.c_str()) && r->where == want_row.where && r->confirm == want_row.confirm;
                if (!ok)
                    std::printf("page %s row %d: %s / the catalog's %s\n", id.c_str(), i, r->id ? r->id : "?",
                        want_row.id);
                CHECK(ok);
            }
            CHECK(pc_option_page_find_row(page, PC_UI_WHERE_MODERN, want_row.id) == i);
        }
    }
}

// Every catalog row on exactly one fallback page, in the catalog's (today's) order,
// in each state of the rows that come and go.
static void every_row_once()
{
    std::set<std::string> seen_any;
    for (int state = 0; state < 4; state++) {
        pc_hw_interpolate = state != 1; // Images per vsync shows with frame interpolation
        pc_qol_fast_travel_cost = state == 2 ? PC_FT_COST_FLAT : state == 3 ? PC_FT_COST_SCALED : 0;
        std::vector<std::string> listed;
        // the catalog's homes in order, every screen
        for (int home = 0; home < pc_ui_settings_catalog_homes(); home++) {
            int page = pc_option_pages_index(pc_ui_settings_catalog_home(home, nullptr));
            for (int i = 0, n = pc_option_page_rows(page, PC_UI_WHERE_ALL); i < n; i++) {
                pc_option_row r;
                CHECK(pc_option_page_row(page, PC_UI_WHERE_ALL, i, &r));
                if (r.form == PC_OPT_SETTING)
                    listed.push_back(r.row.id);
            }
        }
        std::vector<std::string> catalog;
        for (int i = 0; i < pc_ui_settings_catalog_count(); i++)
            if (pc_ui_settings_catalog_shown(i))
                catalog.push_back(pc_ui_settings_catalog_id(i));
        CHECK(listed == catalog);
        std::set<std::string> unique(listed.begin(), listed.end());
        CHECK(unique.size() == listed.size());
        seen_any.insert(listed.begin(), listed.end());
    }
    pc_hw_interpolate = 1;
    pc_qol_fast_travel_cost = 0;
    CHECK((int)seen_any.size() == pc_ui_settings_catalog_count());
    CHECK(seen_any.count("present_rate") && seen_any.count("qol_fast_travel_flat")
        && seen_any.count("qol_fast_travel_scale"));
}

// The catalog by id, its homes and screens.
static void catalog()
{
    int n = pc_ui_settings_catalog_count();
    CHECK(n > 50);
    int i = pc_ui_settings_catalog_index("widescreen");
    pc_ui_setting_row r;
    CHECK(i >= 0 && pc_ui_settings_catalog_row(i, &r));
    CHECK(same(r.id, "widescreen") && same(r.home, "rood.options/display") && r.where == PC_UI_WHERE_ALL);
    CHECK(pc_ui_settings_catalog_where(i) == PC_UI_WHERE_ALL && pc_ui_settings_catalog_where(n) == 0);
    CHECK(pc_ui_settings_catalog_where(pc_ui_settings_catalog_index("vibration")) == PC_UI_WHERE_MODERN);
    CHECK(same(pc_ui_settings_catalog_id(i), "widescreen"));
    CHECK(pc_ui_settings_catalog_index("nonsense") == -1 && pc_ui_settings_catalog_index(nullptr) == -1);
    CHECK(!pc_ui_settings_catalog_row(n, &r) && pc_ui_settings_catalog_id(-1) == nullptr);
    // the game's own options: the modern screen only
    const char* game[] = {"sound", "vibration", "map", "ability_timing", "weapon_status", "armor_status", "cursor_memory",
        "information", "puzzle_mode"};
    for (const char* id : game) {
        CHECK(pc_ui_settings_catalog_row(pc_ui_settings_catalog_index(id), &r));
        CHECK(r.game_option && r.where == PC_UI_WHERE_MODERN);
    }
    CHECK(pc_ui_settings_catalog_row(pc_ui_settings_catalog_index("bind_circle"), &r));
    CHECK(same(r.home, "rood.options/bindings"));
    // a row the catalog hides now is still in it, by id
    pc_hw_interpolate = 0;
    i = pc_ui_settings_catalog_index("present_rate");
    CHECK(i >= 0 && !pc_ui_settings_catalog_shown(i) && pc_ui_settings_catalog_row(i, &r));
    pc_hw_interpolate = 1;
    CHECK(pc_ui_settings_catalog_shown(i));
    // the JSON names the new kinds (never on a catalog page, but the table covers them)
    char json[65536];
    CHECK(pc_ui_settings_json(pc_ui_settings_page_index("display"), json, (int)sizeof json) > 0);
    CHECK(std::strstr(json, "\"id\":\"window_mode\"") && std::strstr(json, "\"confirm\":true"));
    CHECK(pc_ui_settings_catalog_row(pc_ui_settings_catalog_index("window_mode"), &r) && r.confirm);
    CHECK(pc_ui_settings_catalog_row(pc_ui_settings_catalog_index("widescreen"), &r) && !r.confirm);
    // the homes, in the catalog's order
    const char* title = nullptr;
    CHECK(same(pc_ui_settings_catalog_home(0, &title), "rood.options/graphics") && same(title, "Graphics"));
    CHECK(pc_ui_settings_catalog_home(7, &title) == nullptr);
    // a binding by its button
    const char *bid = nullptr, *blabel = nullptr;
    CHECK(pc_ui_settings_binding(PC_PROMPT_CIRCLE, &bid, &blabel) == pc_ui_settings_catalog_index("bind_circle")
        && same(bid, "bind_circle") && same(blabel, "Circle"));
}

// The classic screen: no game options (the vanilla rows show them), Bindings by a link.
static void classic_view()
{
    int controls = pc_option_pages_index("rood.options/controls");
    int bindings = pc_option_pages_index("rood.options/bindings");
    std::vector<std::string> modern = row_ids(controls, PC_UI_WHERE_MODERN);
    std::vector<std::string> classic = row_ids(controls, PC_UI_WHERE_CLASSIC);
    CHECK(!modern.empty() && modern.back() == "vibration");
    CHECK(!classic.empty() && classic.back() == "rood.options/bindings");
    CHECK(classic.size() == modern.size()); // vibration out, the link in
    int last = pc_option_page_rows(controls, PC_UI_WHERE_CLASSIC) - 1;
    pc_option_row r;
    CHECK(pc_option_page_row(controls, PC_UI_WHERE_CLASSIC, last, &r));
    CHECK(r.form == PC_OPT_LINK && r.row.kind == PC_UI_SET_LINK && r.link == bindings && r.row.enabled);
    CHECK(same(r.row.label, "Bindings") && same(r.row.value, "Bindings") && r.row.where == PC_UI_WHERE_CLASSIC);
    CHECK(same(r.row.help, "Keys and gamepad buttons for each PlayStation button."));
    CHECK(pc_option_page_find_row(controls, PC_UI_WHERE_CLASSIC, "rood.options/bindings") == last);
    CHECK(pc_option_page_find_row(controls, PC_UI_WHERE_MODERN, "rood.options/bindings") == -1);
    CHECK(pc_option_page_rows(controls, PC_UI_WHERE_ALL) == (int)modern.size() + 1);
    int gameplay = pc_option_pages_index("rood.options/gameplay");
    for (const std::string& id : row_ids(gameplay, PC_UI_WHERE_CLASSIC))
        CHECK(id != "map" && id != "puzzle_mode" && id != "information");
    CHECK(pc_option_page_find_row(gameplay, PC_UI_WHERE_MODERN, "puzzle_mode") >= 0);
    int audio = pc_option_pages_index("rood.options/audio");
    CHECK(join(row_ids(audio, PC_UI_WHERE_CLASSIC)) == "master_volume");
    CHECK(join(row_ids(audio, PC_UI_WHERE_MODERN)) == "master_volume,sound");
}

// Live values: a setting changed through the catalog shows on its page at once.
static void live_values()
{
    int display = pc_option_pages_index("rood.options/display");
    int row = pc_option_page_find_row(display, PC_UI_WHERE_ALL, "widescreen");
    pc_option_row r;
    unsigned gen = pc_option_pages_generation();
    CHECK(pc_option_page_row(display, PC_UI_WHERE_ALL, row, &r) && same(r.row.value, "Off"));
    int page = -1, legacy_row = -1;
    CHECK(pc_ui_settings_find("widescreen", &page, &legacy_row) == 1);
    CHECK(pc_ui_settings_change(page, legacy_row, 1) == 1);
    CHECK(pc_option_page_row(display, PC_UI_WHERE_ALL, row, &r) && same(r.row.value, "On"));
    CHECK(pc_ui_settings_change(page, legacy_row, 1) == 1);
    CHECK(pc_option_pages_generation() == gen); // values are not page changes
    pc_ui_settings_flush();
}

// ---- pages of one's own ----

static pc_option_page_def page(const char* id, const char* title)
{
    pc_option_page_def d = {};
    d.id = id, d.title = title;
    return d;
}
static int define(pc_option_page_def d, const char* source = "test") { return pc_option_pages_define(source, &d); }

static void ordering()
{
    pc_option_pages_clear();
    CHECK(pc_option_pages_count() == 0 && !pc_option_pages_fallback_active());
    // definition order when nothing says otherwise
    CHECK(define(page("t/a", "A")) == PC_OPT_OK);
    CHECK(define(page("t/b", "B")) == PC_OPT_OK);
    CHECK(join(top_ids()) == "t/a,t/b");
    // after: c after a, before b
    pc_option_page_def c = page("t/c", "C");
    c.after[0] = "t/a";
    CHECK(define(c) == PC_OPT_OK);
    CHECK(join(top_ids()) == "t/a,t/b,t/c");
    c.before[0] = "t/b";
    CHECK(define(c) == PC_OPT_OK); // redefined in place
    CHECK(join(top_ids()) == "t/a,t/c,t/b");
    // before the first
    pc_option_page_def d = page("t/d", "D");
    d.before[0] = "t/a";
    CHECK(define(d) == PC_OPT_OK);
    CHECK(join(top_ids()) == "t/d,t/a,t/c,t/b");
    // groups: plugins' pages come after the system group, whatever the definition order
    pc_option_pages_clear();
    pc_option_page_def plug = page("p/x", "Plugin X");
    plug.group = "plugins";
    CHECK(define(plug, "p") == PC_OPT_OK);
    pc_option_page_def s1 = page("s/one", "One"), s2 = page("s/two", "Two");
    s1.group = s2.group = "system";
    CHECK(define(s1, "s") == PC_OPT_OK && define(s2, "s") == PC_OPT_OK);
    CHECK(join(top_ids()) == "s/one,s/two,p/x");
    // "group:<name>" in after / before
    pc_option_page_def first = page("p/first", "First");
    first.before[0] = "group:system";
    CHECK(define(first, "p") == PC_OPT_OK);
    CHECK(join(top_ids()) == "p/first,s/one,s/two,p/x");
    pc_option_page_def between = page("p/between", "Between");
    between.group = "plugins";
    between.after[0] = "s/one";
    between.before[0] = "p/x";
    CHECK(define(between, "p") == PC_OPT_OK);
    CHECK(join(top_ids()) == "p/first,s/one,s/two,p/between,p/x");
    // several ids in one after
    pc_option_page_def late = page("p/late", "Late");
    late.after[0] = "p/x";
    late.after[1] = "s/two";
    late.before[0] = "nothing/there"; // unknown ids are ignored
    CHECK(define(late, "p") == PC_OPT_OK);
    CHECK(join(top_ids()) == "p/first,s/one,s/two,p/between,p/x,p/late");
    // an after / before cycle is not refused: definition order breaks it
    pc_option_pages_clear();
    pc_option_page_def a = page("t/a", "A"), b = page("t/b", "B");
    a.after[0] = "t/b";
    b.after[0] = "t/a";
    CHECK(define(a) == PC_OPT_OK && define(b) == PC_OPT_OK);
    CHECK(join(top_ids()) == "t/a,t/b");
    // siblings only: an after naming a page elsewhere in the tree changes nothing
    pc_option_page_def kid1 = page("t/k1", "K1"), kid2 = page("t/k2", "K2");
    kid1.parent = kid2.parent = "t/a";
    kid1.after[0] = "t/b";
    CHECK(define(kid1) == PC_OPT_OK && define(kid2) == PC_OPT_OK);
    int pa = pc_option_pages_index("t/a"), kids[4];
    CHECK(pc_option_pages_children(pa, kids, 4) == 2 && page_id(kids[0]) == "t/k1" && page_id(kids[1]) == "t/k2");
    CHECK(join(top_ids()) == "t/a,t/b");
    std::vector<std::string> all;
    for (int i = 0; i < pc_option_pages_count(); i++)
        all.push_back(page_id(i));
    CHECK(join(all) == "t/a,t/k1,t/k2,t/b"); // tree order
}

static void cycles_and_errors()
{
    pc_option_pages_clear();
    CHECK(define(page("t/p", "P")) == PC_OPT_OK);
    pc_option_page_def q = page("t/q", "Q");
    q.parent = "t/p";
    CHECK(define(q) == PC_OPT_OK);
    pc_option_page_def r = page("t/r", "R");
    r.parent = "t/q";
    CHECK(define(r) == PC_OPT_OK);
    unsigned gen = pc_option_pages_generation();
    // parent cycles: P under R (R is under Q under P), P under itself
    pc_option_page_def p = page("t/p", "P");
    p.parent = "t/r";
    CHECK(define(p) == PC_OPT_ERR_CYCLE);
    p.parent = "t/p";
    CHECK(define(p) == PC_OPT_ERR_CYCLE);
    pc_option_page info;
    CHECK(pc_option_pages_info(pc_option_pages_index("t/p"), &info) && info.parent == -1); // unchanged
    CHECK(pc_option_pages_generation() == gen);
    // link cycles: R links back to P (P -> Q -> R by parents); a page links itself; two pages link each other
    pc_option_row_def to_p = {PC_OPT_LINK, "t/p", nullptr, nullptr, 0};
    r.rows = &to_p, r.row_count = 1;
    CHECK(define(r) == PC_OPT_ERR_CYCLE);
    pc_option_row_def to_self = {PC_OPT_LINK, "t/x", nullptr, nullptr, 0};
    pc_option_page_def x = page("t/x", "X");
    x.rows = &to_self, x.row_count = 1;
    CHECK(define(x) == PC_OPT_ERR_CYCLE);
    pc_option_row_def to_y = {PC_OPT_LINK, "t/y", nullptr, nullptr, 0}, to_x = {PC_OPT_LINK, "t/x", nullptr, nullptr, 0};
    x.rows = &to_y;
    CHECK(define(x) == PC_OPT_OK); // a link to a page not there yet: allowed (greyed)
    pc_option_page_def y = page("t/y", "Y");
    y.rows = &to_x, y.row_count = 1;
    CHECK(define(y) == PC_OPT_ERR_CYCLE);
    CHECK(pc_option_pages_index("t/y") == -1);
    // a link down the tree is fine (Controls -> Bindings)
    pc_option_row_def to_r = {PC_OPT_LINK, "t/r", nullptr, nullptr, 0};
    p = page("t/p", "P");
    p.rows = &to_r, p.row_count = 1;
    CHECK(define(p) == PC_OPT_OK);
    // other errors
    CHECK(define(page("", "Empty")) == PC_OPT_ERR_ID);
    CHECK(define(page("has space", "S")) == PC_OPT_ERR_ID);
    CHECK(define(page("t/n", "")) == PC_OPT_ERR_TITLE);
    pc_option_page_def orphan = page("t/o", "O");
    orphan.parent = "t/none";
    CHECK(define(orphan) == PC_OPT_ERR_PARENT);
    CHECK(define(page("t/p", "Taken"), "other") == PC_OPT_ERR_OWNER);
    pc_option_row_def bad = {PC_OPT_SETTING, nullptr, nullptr, nullptr, 0};
    pc_option_page_def badp = page("t/bad", "Bad");
    badp.rows = &bad, badp.row_count = 1;
    CHECK(define(badp) == PC_OPT_ERR_ROW);
    bad = {PC_OPT_HEADER, nullptr, nullptr, nullptr, 0};
    CHECK(define(badp) == PC_OPT_ERR_ROW);
    bad = {42, "x", nullptr, nullptr, 0};
    CHECK(define(badp) == PC_OPT_ERR_ROW);
    CHECK(pc_option_pages_index("t/bad") == -1);
    CHECK(std::strstr(pc_option_pages_error(PC_OPT_ERR_CYCLE), "back"));
}

static void rows_of_every_form()
{
    pc_option_pages_clear();
    pc_option_page_def sub_def = page("t/sub", "Sub page");
    sub_def.help = "Below";
    CHECK(define(sub_def) == PC_OPT_OK);
    const pc_option_row_def rows[] = {
        {PC_OPT_HEADER, nullptr, "Picture", nullptr, 0},
        {PC_OPT_SETTING, "widescreen", nullptr, nullptr, 0},
        {PC_OPT_SETTING, "aspect", "Fill", "How it fills.", 0},
        {PC_OPT_SETTING, "plugin:gone/speed", "Speed", nullptr, 0},
        {PC_OPT_NOTE, nullptr, "Restart to apply.", nullptr, 0},
        {PC_OPT_ACTION, "settings.save", "Save", "Now.", 0},
        {PC_OPT_LINK, "t/sub", nullptr, nullptr, 0},
        {PC_OPT_LINK, "t/later", "Later", nullptr, 0},
        {PC_OPT_SETTING, "vibration", nullptr, nullptr, 0},                    // the game's: modern only
        {PC_OPT_SETTING, "skip_intro", nullptr, nullptr, PC_UI_WHERE_CLASSIC}, // narrowed by the page
    };
    pc_option_page_def d = page("t/main", "Main");
    d.rows = rows, d.row_count = (int)(sizeof rows / sizeof rows[0]);
    CHECK(define(d) == PC_OPT_OK);
    int main_page = pc_option_pages_index("t/main"), sub = pc_option_pages_index("t/sub");
    CHECK(pc_option_page_rows(main_page, PC_UI_WHERE_ALL) == 10);
    CHECK(join(row_ids(main_page, PC_UI_WHERE_MODERN))
        == "t/main#0,widescreen,aspect,plugin:gone/speed,t/main#4,settings.save,t/sub,t/later,vibration");
    CHECK(join(row_ids(main_page, PC_UI_WHERE_CLASSIC))
        == "t/main#0,widescreen,aspect,plugin:gone/speed,t/main#4,settings.save,t/sub,t/later,skip_intro");
    pc_option_row r;
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 0, &r));
    CHECK(r.form == PC_OPT_HEADER && r.row.kind == PC_UI_SET_HEADER && same(r.row.label, "Picture") && !r.row.enabled);
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 1, &r));
    CHECK(r.row.kind == PC_UI_SET_TOGGLE && same(r.row.label, "Widescreen 16:9") && same(r.row.home, "rood.options/display"));
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 2, &r));
    CHECK(r.row.kind == PC_UI_SET_CHOICE && same(r.row.label, "Fill") && same(r.row.help, "How it fills.")
        && same(r.row.value, "Keep aspect"));
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 3, &r));
    CHECK(r.catalog == -1 && !r.row.enabled && r.row.reason[0] && same(r.row.label, "Speed")
        && r.row.kind == PC_UI_SET_INFO);
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 4, &r));
    CHECK(r.row.kind == PC_UI_SET_NOTE && same(r.row.label, "Restart to apply.") && !r.row.enabled);
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 5, &r));
    // a core action (option_actions.h): its own label and help unless the row gives them; greyed with its reason
    CHECK(r.form == PC_OPT_ACTION && r.row.kind == PC_UI_SET_ACTION && same(r.row.label, "Save")
        && same(r.row.help, "Now.") && same(r.ref, "settings.save") && same(r.row.home, "t/main"));
    CHECK(!pc_ui_settings_dirty() && !r.row.enabled && std::strstr(r.row.reason, "Nothing to save"));
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 6, &r));
    CHECK(r.row.kind == PC_UI_SET_LINK && r.link == sub && same(r.row.label, "Sub page") && same(r.row.help, "Below")
        && r.row.enabled);
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 7, &r));
    CHECK(r.link == -1 && !r.row.enabled && same(r.row.label, "Later"));
    CHECK(!pc_option_page_row(main_page, PC_UI_WHERE_ALL, 10, &r) && !pc_option_page_row(99, PC_UI_WHERE_ALL, 0, &r));
    CHECK(pc_option_page_find_row(main_page, PC_UI_WHERE_ALL, "t/main#4") == 4);
    // the linked page arrives: the link works
    pc_option_page_def later = page("t/later", "Later page");
    CHECK(define(later) == PC_OPT_OK);
    main_page = pc_option_pages_index("t/main");
    CHECK(pc_option_page_row(main_page, PC_UI_WHERE_ALL, 7, &r) && r.link == pc_option_pages_index("t/later")
        && r.row.enabled && same(r.row.label, "Later")); // the row's own label wins
    // a setting row the catalog hides now is not listed
    pc_option_row_def hidden_rows[] = {{PC_OPT_SETTING, "present_rate", nullptr, nullptr, 0}};
    pc_option_page_def h = page("t/hidden", "Hidden");
    h.rows = hidden_rows, h.row_count = 1;
    CHECK(define(h) == PC_OPT_OK);
    pc_hw_interpolate = 0;
    CHECK(pc_option_page_rows(pc_option_pages_index("t/hidden"), PC_UI_WHERE_ALL) == 0);
    pc_hw_interpolate = 1;
    CHECK(pc_option_page_rows(pc_option_pages_index("t/hidden"), PC_UI_WHERE_ALL) == 1);
}

static void generation_and_sources()
{
    pc_option_pages_clear();
    unsigned g0 = pc_option_pages_generation();
    CHECK(define(page("a/one", "One"), "a") == PC_OPT_OK);
    unsigned g1 = pc_option_pages_generation();
    CHECK(g1 != g0);
    pc_option_page_def kid = page("b/kid", "Kid");
    kid.parent = "a/one";
    CHECK(define(kid, "b") == PC_OPT_OK);
    CHECK(define(page("b/two", "Two"), "b") == PC_OPT_OK);
    unsigned g2 = pc_option_pages_generation();
    CHECK(g2 != g1);
    CHECK(pc_option_pages_count() == 3);
    // a refused define changes nothing
    CHECK(define(page("a/one", "Mine"), "b") == PC_OPT_ERR_OWNER);
    CHECK(pc_option_pages_generation() == g2);
    // reading changes nothing
    pc_option_pages_count();
    pc_option_page_rows(0, PC_UI_WHERE_ALL);
    CHECK(pc_option_pages_generation() == g2);
    // the parent goes: its sub-page is no longer listed (nor reachable)
    CHECK(pc_option_pages_remove("a/one") == 1);
    CHECK(pc_option_pages_generation() != g2);
    CHECK(pc_option_pages_count() == 1 && pc_option_pages_index("b/kid") == -1);
    CHECK(pc_option_pages_remove("a/one") == 0);
    unsigned g3 = pc_option_pages_generation();
    CHECK(pc_option_pages_remove_source("nobody") == 0 && pc_option_pages_generation() == g3);
    CHECK(pc_option_pages_remove_source("b") == 2 && pc_option_pages_count() == 0);
    CHECK(pc_option_pages_generation() != g3);
    // the fallback back: today's pages; off: gone, others' pages stay
    pc_option_pages_fallback(1);
    CHECK(pc_option_pages_fallback_active() && pc_option_pages_count() == 7);
    pc_option_page_def mine = page("c/mine", "Mine");
    mine.group = "plugins"; // after the core pages, even when those are defined again later
    CHECK(define(mine, "c") == PC_OPT_OK);
    // the fallback's pages are its own: another source cannot take them over
    CHECK(define(page("rood.options/display", "Display"), "c") == PC_OPT_ERR_OWNER);
    unsigned g4 = pc_option_pages_generation();
    pc_option_pages_fallback(0);
    CHECK(!pc_option_pages_fallback_active() && pc_option_pages_count() == 1 && pc_option_pages_generation() != g4);
    pc_option_pages_fallback(1);
    CHECK(pc_option_pages_count() == 8);
    CHECK(join(top_ids()) == "rood.options/gameplay,rood.options/display,rood.options/graphics,rood.options/audio,"
                             "rood.options/interface,rood.options/controls,c/mine");
}

int main()
{
    // the fallback before anything else touches the model (it is there from the first call)
    fallback_tree();
    fallback_matches_settings();
    every_row_once();
    catalog();
    classic_view();
    live_values();
    fallback_matches_settings(); // after a change too
    ordering();
    cycles_and_errors();
    rows_of_every_form();
    generation_and_sources();
    fallback_matches_settings(); // the restored fallback
    if (failures) {
        std::printf("option_pages_probe: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("option_pages_probe: ok\n");
    return 0;
}
