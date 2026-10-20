// Option pages (option_pages.h): pages from sources, ordered among their
// siblings as the plugin registry orders entries (lua_registry.cpp
// registry_list: after / before an id or "group:<name>", the earliest
// defined first, a cycle broken by definition order), resolved into rows
// through the settings catalog (ui_settings.h pc_ui_settings_catalog_*).
#include "ui/option_pages.h"
#include "ui/option_actions.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const char* const FALLBACK_SOURCE = "rood.options";
const char* const MISSING_SETTING = "Not available: its plugin is not loaded";
const char* const MISSING_PAGE = "Not available";

struct RowDef {
    int form = PC_OPT_SETTING;
    std::string ref, label, help, id; // id: header / note "<page id>#<n>"
    bool has_label = false, has_help = false;
    int where = 0;
};
struct Page {
    std::string id, title, help, parent, group, source;
    std::vector<std::string> after, before;
    std::vector<RowDef> rows;
    unsigned long long seq = 0; // definition order (kept by a redefinition)
};

std::vector<Page> defined;
unsigned long long next_seq = 1;
unsigned generation = 1;
bool started, fallback_on;

// The listing (tree order) for `listed_generation`.
std::vector<const Page*> listed;
std::vector<int> listed_parent, listed_depth;
unsigned listed_generation;

Page* page_by_id(const std::string& id)
{
    for (Page& p : defined)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool id_ok(const char* id)
{
    size_t n = id ? std::strlen(id) : 0;
    if (n == 0 || n > 64)
        return false;
    for (const char* c = id; *c; c++)
        if ((unsigned char)*c <= ' ' || *c == '#')
            return false;
    return true;
}

// The parent and link graph has no cycle (parent -> child, page -> linked page).
bool acyclic()
{
    std::unordered_map<std::string, size_t> index;
    for (size_t k = 0; k < defined.size(); k++)
        index.emplace(defined[k].id, k);
    std::vector<std::vector<size_t>> next(defined.size());
    for (size_t k = 0; k < defined.size(); k++) {
        const Page& p = defined[k];
        if (!p.parent.empty()) {
            auto it = index.find(p.parent);
            if (it != index.end())
                next[it->second].push_back(k);
        }
        for (const RowDef& r : p.rows)
            if (r.form == PC_OPT_LINK) {
                auto it = index.find(r.ref);
                if (it != index.end())
                    next[k].push_back(it->second);
            }
    }
    std::vector<int> state(defined.size(), 0); // 0 new, 1 on the path, 2 done
    std::function<bool(size_t)> visit = [&](size_t k) {
        state[k] = 1;
        for (size_t t : next[k])
            if (state[t] == 1 || (state[t] == 0 && !visit(t)))
                return false;
        state[k] = 2;
        return true;
    };
    for (size_t k = 0; k < defined.size(); k++)
        if (state[k] == 0 && !visit(k))
            return false;
    return true;
}

void changed() { ++generation; }

// ---- the C++ fallback: today's System pages (ui_settings.h), from the catalog's homes ----

struct FallbackPage {
    const char* id;
    const char* title;
    const char* help;
    const char* parent;
};
// In the Option screen's order (the plan's group:system); Bindings under Controls.
const FallbackPage FALLBACK[] = {
    {"rood.options/gameplay", "Gameplay", "Quality-of-life options and the game's own options.", nullptr},
    {"rood.options/display", "Display", "Widescreen, scaling, the window and brightness.", nullptr},
    {"rood.options/graphics", "Graphics", "Lighting, resolution, anti-aliasing and the 3D picture.", nullptr},
    {"rood.options/audio", "Audio", "Volume and the game's sound output.", nullptr},
    {"rood.options/interface", "Interface", "Menus, text, 2D upscaling and button prompts.", nullptr},
    {"rood.options/controls", "Controls", "The right stick camera and vibration.", nullptr},
    {"rood.options/bindings", "Bindings", "Keys and gamepad buttons for each PlayStation button.",
        "rood.options/controls"},
};

void define_fallback()
{
    for (const FallbackPage& f : FALLBACK) {
        std::vector<pc_option_row_def> rows;
        for (int i = 0, n = pc_ui_settings_catalog_count(); i < n; i++) {
            pc_ui_setting_row r;
            if (pc_ui_settings_catalog_row(i, &r) && r.home && !std::strcmp(r.home, f.id))
                rows.push_back({PC_OPT_SETTING, r.id, nullptr, nullptr, 0});
        }
        // The classic Option screen reaches Bindings from Controls; the modern
        // System list shows it as an entry of its own.
        if (!std::strcmp(f.id, "rood.options/controls"))
            rows.push_back({PC_OPT_LINK, "rood.options/bindings", nullptr, nullptr, PC_UI_WHERE_CLASSIC});
        pc_option_page_def def = {};
        def.id = f.id, def.title = f.title, def.help = f.help, def.parent = f.parent;
        def.group = f.parent ? nullptr : "system";
        def.rows = rows.data(), def.row_count = (int)rows.size();
        pc_option_pages_define(FALLBACK_SOURCE, &def);
    }
}

// The fallback is in place from the first use.
void start()
{
    if (started)
        return;
    started = true;
    fallback_on = true;
    define_fallback();
}

// ---- ordering ----

// Siblings in order: the earliest defined that nothing still has to precede.
std::vector<const Page*> order(std::vector<const Page*> nodes)
{
    std::sort(nodes.begin(), nodes.end(), [](const Page* a, const Page* b) { return a->seq < b->seq; });
    size_t n = nodes.size();
    std::unordered_map<std::string, size_t> by_id;
    std::unordered_map<std::string, std::vector<size_t>> by_group;
    for (size_t k = 0; k < n; k++) {
        by_id.emplace(nodes[k]->id, k);
        if (!nodes[k]->group.empty())
            by_group[nodes[k]->group].push_back(k);
    }
    std::vector<std::vector<size_t>> next(n);
    std::vector<int> incoming(n, 0);
    auto edge = [&](size_t from, size_t to) {
        if (from == to)
            return;
        next[from].push_back(to);
        incoming[to]++;
    };
    auto each_target = [&](const std::string& ref, const std::function<void(size_t)>& f) {
        if (ref.compare(0, 6, "group:") == 0) {
            auto it = by_group.find(ref.substr(6));
            if (it != by_group.end())
                for (size_t t : it->second)
                    f(t);
        } else {
            auto it = by_id.find(ref);
            if (it != by_id.end())
                f(it->second);
        }
    };
    for (size_t k = 0; k < n; k++) {
        for (const std::string& a : nodes[k]->after)
            each_target(a, [&](size_t t) { edge(t, k); });
        for (const std::string& b : nodes[k]->before)
            each_target(b, [&](size_t t) { edge(k, t); });
        if (nodes[k]->group == "plugins") // plugins' pages after the core ones
            each_target("group:system", [&](size_t t) { edge(t, k); });
    }
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    for (size_t k = 0; k < n; k++)
        if (incoming[k] == 0)
            ready.push(k);
    std::vector<const Page*> out;
    std::vector<bool> done(n, false);
    size_t scan = 0;
    while (out.size() < n) {
        size_t pick = n;
        while (!ready.empty() && pick == n) {
            size_t k = ready.top();
            ready.pop();
            if (!done[k])
                pick = k;
        }
        if (pick == n) { // a cycle: the earliest defined of what is left
            while (done[scan])
                scan++;
            pick = scan;
        }
        done[pick] = true;
        out.push_back(nodes[pick]);
        for (size_t t : next[pick])
            if (--incoming[t] == 0 && !done[t])
                ready.push(t);
    }
    return out;
}

void list_children(const std::string& parent, int parent_index, int depth)
{
    std::vector<const Page*> children;
    for (const Page& p : defined)
        if (p.parent == parent)
            children.push_back(&p);
    for (const Page* p : order(children)) {
        int index = (int)listed.size();
        listed.push_back(p);
        listed_parent.push_back(parent_index);
        listed_depth.push_back(depth);
        list_children(p->id, index, depth + 1);
    }
}

void relist()
{
    start();
    if (listed_generation == generation)
        return;
    listed.clear(), listed_parent.clear(), listed_depth.clear();
    list_children("", -1, 0);
    listed_generation = generation;
}

const Page* listed_page(int page)
{
    relist();
    return page >= 0 && page < (int)listed.size() ? listed[(size_t)page] : nullptr;
}

int listed_index(const std::string& id)
{
    relist();
    for (size_t k = 0; k < listed.size(); k++)
        if (listed[k]->id == id)
            return (int)k;
    return -1;
}

// ---- rows ----

// A row's where: as defined, else the setting's own (catalog), else every screen.
int row_where(const RowDef& r, int catalog)
{
    if (r.where)
        return r.where;
    int own = r.form == PC_OPT_SETTING ? pc_ui_settings_catalog_where(catalog) : 0;
    return own ? own : PC_UI_WHERE_ALL;
}

bool row_listed(const RowDef& r, int where, int* catalog)
{
    *catalog = r.form == PC_OPT_SETTING ? pc_ui_settings_catalog_index(r.ref.c_str()) : -1;
    if (!(row_where(r, *catalog) & where))
        return false;
    return r.form != PC_OPT_SETTING || *catalog < 0 || pc_ui_settings_catalog_shown(*catalog);
}

const RowDef* listed_row(const Page* p, int where, int row, int* catalog)
{
    if (!p || row < 0)
        return nullptr;
    int n = 0;
    for (const RowDef& r : p->rows)
        if (row_listed(r, where, catalog) && n++ == row)
            return &r;
    return nullptr;
}

void fill(const Page* p, const RowDef& d, int catalog, pc_option_row* out)
{
    std::memset(out, 0, sizeof *out);
    pc_ui_setting_row& r = out->row;
    out->form = d.form;
    out->ref = d.ref.empty() ? nullptr : d.ref.c_str();
    out->catalog = catalog;
    out->link = -1;
    switch (d.form) {
    case PC_OPT_SETTING:
        if (catalog >= 0)
            pc_ui_settings_catalog_row(catalog, &r);
        else { // a missing plugin's setting: shown greyed
            r.id = d.ref.c_str();
            r.label = d.ref.c_str();
            r.kind = PC_UI_SET_INFO;
            r.index = r.button = -1;
            std::snprintf(r.reason, sizeof r.reason, "%s", MISSING_SETTING);
        }
        break;
    case PC_OPT_ACTION: {
        r.id = r.label = d.ref.c_str();
        r.kind = PC_UI_SET_ACTION;
        r.index = r.button = -1;
        r.enabled = 1;
        if (const char* label = pc_option_action_label(d.ref.c_str()))
            r.label = label;
        r.help = pc_option_action_help(d.ref.c_str());
        const char* why = pc_option_action_unavailable(d.ref.c_str());
        if (why) {
            r.enabled = 0;
            std::snprintf(r.reason, sizeof r.reason, "%s", why);
        }
        break;
    }
    case PC_OPT_LINK: {
        r.id = r.label = d.ref.c_str();
        r.kind = PC_UI_SET_LINK;
        r.index = r.button = -1;
        out->link = listed_index(d.ref);
        if (out->link >= 0) {
            const Page* target = listed[(size_t)out->link];
            r.label = target->title.c_str();
            r.help = target->help.c_str();
            std::snprintf(r.value, sizeof r.value, "%s", target->title.c_str());
            r.enabled = 1;
        } else
            std::snprintf(r.reason, sizeof r.reason, "%s", MISSING_PAGE);
        break;
    }
    default: // header, note
        r.id = d.id.c_str();
        r.label = d.label.c_str();
        r.kind = d.form == PC_OPT_HEADER ? PC_UI_SET_HEADER : PC_UI_SET_NOTE;
        r.index = r.button = -1;
        r.enabled = 0; // not selectable
        break;
    }
    if (d.has_label && d.form != PC_OPT_HEADER && d.form != PC_OPT_NOTE)
        r.label = d.label.c_str();
    if (d.has_help)
        r.help = d.help.c_str();
    if (!r.help)
        r.help = "";
    if (!r.home)
        r.home = p->id.c_str();
    r.where = row_where(d, catalog);
}

} // namespace

extern "C" int pc_option_pages_define(const char* source, const pc_option_page_def* def)
{
    start();
    if (!def || !id_ok(def->id))
        return PC_OPT_ERR_ID;
    if (!def->title || !def->title[0])
        return PC_OPT_ERR_TITLE;
    std::string src = source ? source : "";
    Page* existing = page_by_id(def->id);
    if (existing && existing->source != src)
        return PC_OPT_ERR_OWNER;
    if (def->parent && def->parent[0] && (!std::strcmp(def->parent, def->id) || !page_by_id(def->parent)))
        return !std::strcmp(def->parent, def->id) ? PC_OPT_ERR_CYCLE : PC_OPT_ERR_PARENT;
    if (def->row_count < 0 || (def->row_count > 0 && !def->rows))
        return PC_OPT_ERR_ROW;
    Page p;
    p.id = def->id, p.title = def->title, p.help = def->help ? def->help : "";
    p.parent = def->parent ? def->parent : "", p.group = def->group ? def->group : "", p.source = src;
    for (int k = 0; k < PC_OPT_REFS; k++) {
        if (def->after[k] && def->after[k][0])
            p.after.push_back(def->after[k]);
        if (def->before[k] && def->before[k][0])
            p.before.push_back(def->before[k]);
    }
    for (int k = 0; k < def->row_count; k++) {
        const pc_option_row_def& d = def->rows[k];
        RowDef r;
        r.form = d.form;
        r.where = d.where & PC_UI_WHERE_ALL;
        bool text = d.form == PC_OPT_HEADER || d.form == PC_OPT_NOTE;
        if (d.form < PC_OPT_SETTING || d.form > PC_OPT_NOTE || (text ? !d.label || !d.label[0] : !d.ref || !d.ref[0]))
            return PC_OPT_ERR_ROW;
        if (!text)
            r.ref = d.ref;
        if (d.form == PC_OPT_LINK && r.ref == p.id)
            return PC_OPT_ERR_CYCLE;
        if (d.label)
            r.label = d.label, r.has_label = true;
        if (d.help)
            r.help = d.help, r.has_help = true;
        if (text)
            r.id = p.id + "#" + std::to_string(k);
        p.rows.push_back(r);
    }
    // In place (keeping its definition order), then the graph must stay acyclic.
    Page old;
    if (existing) {
        p.seq = existing->seq;
        old = *existing;
        *existing = p;
    } else {
        p.seq = next_seq++;
        defined.push_back(p);
    }
    if (!acyclic()) {
        if (existing)
            *page_by_id(def->id) = old;
        else
            defined.pop_back();
        return PC_OPT_ERR_CYCLE;
    }
    changed();
    return PC_OPT_OK;
}

extern "C" int pc_option_pages_remove(const char* id)
{
    start();
    for (auto it = defined.begin(); id && it != defined.end(); ++it)
        if (it->id == id) {
            defined.erase(it);
            changed();
            return 1;
        }
    return 0;
}

extern "C" int pc_option_pages_remove_source(const char* source)
{
    start();
    std::string src = source ? source : "";
    size_t before = defined.size();
    defined.erase(std::remove_if(defined.begin(), defined.end(), [&](const Page& p) { return p.source == src; }),
        defined.end());
    int removed = (int)(before - defined.size());
    if (removed)
        changed();
    return removed;
}

extern "C" void pc_option_pages_fallback(int on)
{
    start();
    pc_option_pages_remove_source(FALLBACK_SOURCE);
    fallback_on = on != 0;
    if (fallback_on)
        define_fallback();
    changed();
}

extern "C" int pc_option_pages_fallback_active(void)
{
    start();
    return fallback_on;
}

extern "C" void pc_option_pages_clear(void)
{
    start();
    defined.clear();
    fallback_on = false;
    changed();
}

extern "C" unsigned pc_option_pages_generation(void)
{
    start();
    return generation;
}

extern "C" const char* pc_option_pages_error(int code)
{
    switch (code) {
    case PC_OPT_OK:
        return "ok";
    case PC_OPT_ERR_ID:
        return "the page id is missing, has spaces or # in it, or is longer than 64";
    case PC_OPT_ERR_TITLE:
        return "the page has no title";
    case PC_OPT_ERR_PARENT:
        return "the parent is not a page";
    case PC_OPT_ERR_CYCLE:
        return "the parent or a link leads back to the page";
    case PC_OPT_ERR_OWNER:
        return "another source defined that page";
    case PC_OPT_ERR_ROW:
        return "a row has no reference or text, or no known form";
    default:
        return "unknown error";
    }
}

extern "C" int pc_option_pages_count(void)
{
    relist();
    return (int)listed.size();
}

extern "C" int pc_option_pages_index(const char* id) { return id ? listed_index(id) : -1; }

extern "C" int pc_option_pages_info(int page, pc_option_page* out)
{
    const Page* p = listed_page(page);
    if (!p || !out)
        return 0;
    out->id = p->id.c_str();
    out->title = p->title.c_str();
    out->help = p->help.c_str();
    out->source = p->source.c_str();
    out->group = p->group.c_str();
    out->parent = listed_parent[(size_t)page];
    out->depth = listed_depth[(size_t)page];
    return 1;
}

extern "C" int pc_option_pages_children(int parent, int* out, int max)
{
    relist();
    int n = 0;
    for (size_t k = 0; k < listed.size(); k++)
        if (listed_parent[k] == parent) {
            if (out && n < max)
                out[n] = (int)k;
            ++n;
        }
    return n;
}

extern "C" int pc_option_page_rows(int page, int where)
{
    const Page* p = listed_page(page);
    if (!p)
        return 0;
    int n = 0, catalog;
    for (const RowDef& r : p->rows)
        n += row_listed(r, where, &catalog);
    return n;
}

extern "C" int pc_option_page_row(int page, int where, int row, pc_option_row* out)
{
    const Page* p = listed_page(page);
    int catalog = -1;
    const RowDef* d = listed_row(p, where, row, &catalog);
    if (!d || !out)
        return 0;
    fill(p, *d, catalog, out);
    return 1;
}

extern "C" int pc_option_page_find_row(int page, int where, const char* id)
{
    const Page* p = listed_page(page);
    if (!p || !id)
        return -1;
    int n = 0, catalog;
    for (const RowDef& r : p->rows) {
        if (!row_listed(r, where, &catalog))
            continue;
        const std::string& rid = r.form == PC_OPT_HEADER || r.form == PC_OPT_NOTE ? r.id : r.ref;
        if (rid == id)
            return n;
        ++n;
    }
    return -1;
}
