// rood.menu (pc/docs/Extensions.md, "Option pages"; plan
// pc/docs/plans/2026-10-18-lua-m1-options.md B2): a plugin's pages of the Option
// screens. A page is a Lua table: an id, a title, help, its place (after /
// before a page or "group:<name>", or a parent page) and rows, each a setting
// of the catalog (ui_settings.h; "widescreen", a plugin's "plugin:<id>/<name>"),
// a setting defined in place, a header, a help line, a link to another page or
// an action. Validated here (errors raise in the plugin), then handed to the
// page model (pc/src/ui/option_pages.h) under the plugin's id as its source and
// registered as a "menu.page" registry entry (other plugins see it there and
// in registry.changed). Everything a plugin defined goes when it unloads; the
// shipped rood.options' pages give way to the C++ fallback (the same pages)
// while it is not loaded.
//
// Actions: core ones (option_actions.h; a third-party page offers only those
// flagged plugin_ok) and the plugin's own (rood.menu.action), run in its
// sandbox under its budget when the player picks the row.
#include "rl_internal.h"
#include "ui/option_actions.h"
#include "ui/option_pages.h"
#include "ui/option_style.h"
#include "ui/ui_settings.h"
#include "ui/vs_encode.h"

#include <cctype>
#include <cstring>
#include <deque>
#include <list>

namespace rl {
namespace {
const char* const CORE_PLUGIN = "rood.options"; /* the shipped pages; the C++ fallback's ids */
const char* const PAGE_KIND = "menu.page";      /* their registry entries */
const size_t MAX_ROWS = 64, MAX_PAGES = 16, MAX_TITLE = 24, MAX_NAME = 32, MAX_TEXT = 512;

struct Action {
    std::string id, owner, label, help;
    std::shared_ptr<FunctionRef> fn;
};
std::list<Action>& actions() /* a list: the provider hands out pointers into its strings */
{
    static auto* list = new std::list<Action>();
    return *list;
}

/* ---- validation helpers ---- */
bool slug_ok(const std::string& s)
{
    if (s.empty() || s.size() > MAX_NAME || !(std::islower((unsigned char)s[0]) || std::isdigit((unsigned char)s[0])))
        return false;
    for (char c : s)
        if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_' || c == '-'))
            return false;
    return true;
}
size_t utf8_length(const std::string& s)
{
    size_t n = 0;
    for (unsigned char c : s)
        n += (c & 0xC0) != 0x80;
    return n;
}
/* The game's font can show it (vs_encode.h: anything else becomes '?'). */
bool encodable(const std::string& s)
{
    std::vector<unsigned char> out(s.size() + 2);
    int bad = 0;
    vs_encode(s.c_str(), out.data(), (int)out.size(), &bad);
    for (unsigned char c : s)
        if (c < 0x20)
            return false;
    return bad == 0;
}
/* "x" in p's namespace; an id with a slash or "group:" as given. */
std::string qualify(Plugin* p, const std::string& ref)
{
    if (ref.compare(0, 6, "group:") == 0 || ref.find('/') != std::string::npos)
        return ref;
    return p->id() + "/" + ref;
}
bool own(Plugin* p, const std::string& id) { return id.compare(0, p->id().size() + 1, p->id() + "/") == 0; }

/* A string field of the table at idx: "" when absent; raises on another type. */
std::string text_field(lua_State* L, int idx, const char* field, const char* what)
{
    lua_getfield(L, idx, field);
    std::string out;
    if (lua_type(L, -1) == LUA_TSTRING)
        out = lua_tostring(L, -1);
    else if (!lua_isnil(L, -1))
        luaL_error(L, "rood.menu.page: %s's %s must be a string (got a %s)", what, field, luaL_typename(L, -1));
    lua_pop(L, 1);
    if (out.size() > MAX_TEXT)
        luaL_error(L, "rood.menu.page: %s's %s is longer than %d bytes", what, field, (int)MAX_TEXT);
    return out;
}
std::vector<std::string> refs_field(lua_State* L, int idx, const char* field)
{
    std::vector<std::string> out;
    lua_getfield(L, idx, field);
    if (lua_type(L, -1) == LUA_TSTRING)
        out.push_back(lua_tostring(L, -1));
    else if (lua_istable(L, -1)) {
        lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
        if (n > PC_OPT_REFS)
            luaL_error(L, "rood.menu.page: more than %d ids in %s", PC_OPT_REFS, field);
        for (lua_Integer k = 1; k <= n; k++) {
            lua_rawgeti(L, -1, k);
            if (lua_type(L, -1) != LUA_TSTRING)
                luaL_error(L, "rood.menu.page: %s is a page id or a list of them", field);
            out.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else if (!lua_isnil(L, -1))
        luaL_error(L, "rood.menu.page: %s is a page id or a list of them", field);
    lua_pop(L, 1);
    return out;
}
int where_field(lua_State* L, int idx, int row)
{
    lua_getfield(L, idx, "where");
    int where = 0;
    if (lua_type(L, -1) == LUA_TSTRING) {
        std::string w = lua_tostring(L, -1);
        where = w == "classic" ? PC_UI_WHERE_CLASSIC : w == "modern" ? PC_UI_WHERE_MODERN : w == "all" ? PC_UI_WHERE_ALL : -1;
    }
    if (where < 0 || (!lua_isnil(L, -1) && lua_type(L, -1) != LUA_TSTRING))
        luaL_error(L, "rood.menu.page: row %d's where is \"classic\", \"modern\" or \"all\"", row);
    lua_pop(L, 1);
    return where;
}

/* A catalog key a row may name: the catalog has it, or another plugin's own
 * setting (shown greyed while that plugin is not loaded). */
void check_setting(lua_State* L, Plugin* p, const std::string& key, int row)
{
    if (pc_ui_settings_catalog_index(key.c_str()) >= 0)
        return;
    if (key.compare(0, 7, "plugin:") == 0) {
        size_t slash = key.find('/');
        std::string owner = key.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
        if (slash != std::string::npos && owner != p->id())
            return;
        luaL_error(L, "rood.menu.page: row %d: '%s' is not defined (rood.settings.define it first, or a { define = ... } row)",
            row, key.c_str());
    }
    luaL_error(L, "rood.menu.page: row %d: unknown setting '%s'", row, key.c_str());
}

/* { define = "name", type / kind, default, label, help, ... }: rood.settings.define
 * in the plugin, then a row of that setting. */
std::string define_setting(lua_State* L, int idx, const std::string& name, int row)
{
    lua_getglobal(L, "rood");
    lua_getfield(L, -1, "settings");
    if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
        luaL_error(L, "rood.menu.page: row %d: a { define = ... } row needs rood.settings (not in this build)", row);
    lua_getfield(L, -1, "define");
    lua_pushstring(L, name.c_str());
    lua_newtable(L); /* the spec: the row's fields but define and where; kind's names as type's */
    int spec = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        std::string key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "";
        if (key == "kind") {
            static const char* const KINDS[][2] = { { "toggle", "boolean" }, { "choice", "enum" }, { "slider", "number" } };
            const char* kind = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
            const char* type = nullptr;
            for (auto& k : KINDS)
                if (!std::strcmp(kind, k[0]))
                    type = k[1];
            if (!type)
                luaL_error(L, "rood.menu.page: row %d: kind is \"toggle\", \"choice\" or \"slider\" (or give type)", row);
            lua_pushstring(L, type);
            lua_setfield(L, spec, "type");
        } else if (!key.empty() && key != "define" && key != "where") {
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_settable(L, spec);
        }
        lua_pop(L, 1);
    }
    lua_call(L, 2, 1);
    std::string id = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 3);
    return id;
}

int own_pages(Plugin* p, const std::string& except)
{
    int n = 0;
    for (const RegistryEntry* e : registry_list(PAGE_KIND))
        n += e->owner == p->id() && e->id != except;
    return n;
}

/* ---- rood.menu ---- */
int api_page(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    Plugin* p = plugin_of(L);
    bool core = p->shipped; /* rood.* plugins: every core action, the system group */
    std::deque<std::string> keep; /* the definition's strings */
    auto hold = [&](const std::string& s) { keep.push_back(s); return keep.back().c_str(); };

    std::string local = text_field(L, 1, "id", "the page");
    if (own(p, local))
        local = local.substr(p->id().size() + 1);
    if (!slug_ok(local))
        return luaL_error(L, "rood.menu.page: id '%s' is not a slug (1 to %d of a-z, 0-9, _ and -)", local.c_str(),
            (int)MAX_NAME);
    std::string id = p->id() + "/" + local;
    std::string title = text_field(L, 1, "title", "the page");
    if (title.empty() || utf8_length(title) > MAX_TITLE)
        return luaL_error(L, "rood.menu.page: '%s': the title must be 1 to %d characters", id.c_str(), (int)MAX_TITLE);
    if (!encodable(title))
        return luaL_error(L, "rood.menu.page: '%s': the title '%s' has characters the game's font lacks", id.c_str(),
            title.c_str());
    std::string help = text_field(L, 1, "help", "the page");
    if (!help.empty() && !encodable(help))
        log(LOG_WARN, p->id(), "rood.menu.page '" + id + "': its help has characters the game's font lacks (shown as ?)");
    if (own_pages(p, id) >= (int)MAX_PAGES)
        return luaL_error(L, "rood.menu.page: more than %d pages in one plugin", (int)MAX_PAGES);

    pc_option_page_def def = {};
    def.id = hold(id), def.title = hold(title), def.help = hold(help);
    std::string parent = text_field(L, 1, "parent", "the page");
    if (!parent.empty()) {
        parent = qualify(p, parent);
        if (pc_option_pages_index(parent.c_str()) < 0)
            return luaL_error(L, "rood.menu.page: '%s': parent '%s' is not a page", id.c_str(), parent.c_str());
        def.parent = hold(parent);
    }
    std::string group = text_field(L, 1, "group", "the page");
    if (group.empty() && parent.empty()) /* (a sub-page is placed among its siblings) */
        group = core ? "system" : "plugins";
    if (group == "system" && !core)
        return luaL_error(L, "rood.menu.page: '%s': group 'system' is the shipped pages' (plugins' pages are 'plugins')",
            id.c_str());
    def.group = hold(group);
    const char* rel[2] = { "after", "before" };
    for (int r = 0; r < 2; r++) {
        std::vector<std::string> refs = refs_field(L, 1, rel[r]);
        for (size_t k = 0; k < refs.size(); k++) {
            std::string ref = qualify(p, refs[k]);
            if (ref.compare(0, 6, "group:") != 0 && pc_option_pages_index(ref.c_str()) < 0)
                return luaL_error(L, "rood.menu.page: '%s': %s '%s' is not a page", id.c_str(), rel[r], ref.c_str());
            (r ? def.before : def.after)[k] = hold(ref);
        }
    }

    std::vector<pc_option_row_def> rows;
    lua_getfield(L, 1, "rows");
    int rows_idx = lua_gettop(L);
    if (!lua_isnil(L, rows_idx) && !lua_istable(L, rows_idx))
        return luaL_error(L, "rood.menu.page: '%s': rows is a list", id.c_str());
    size_t n = lua_istable(L, rows_idx) ? lua_rawlen(L, rows_idx) : 0;
    if (n > MAX_ROWS)
        return luaL_error(L, "rood.menu.page: '%s': more than %d rows", id.c_str(), (int)MAX_ROWS);
    for (size_t k = 1; k <= n; k++) {
        int row = (int)k;
        lua_rawgeti(L, rows_idx, (lua_Integer)k);
        int r_idx = lua_gettop(L);
        pc_option_row_def d = {};
        if (lua_type(L, r_idx) == LUA_TSTRING) { /* "widescreen" */
            std::string key = lua_tostring(L, r_idx);
            check_setting(L, p, key, row);
            d.form = PC_OPT_SETTING, d.ref = hold(key);
        } else if (lua_istable(L, r_idx)) {
            static const char* const FORMS[] = { "setting", "define", "header", "page", "action" };
            const char* form = nullptr;
            int forms = 0;
            for (const char* f : FORMS) {
                lua_getfield(L, r_idx, f);
                if (!lua_isnil(L, -1))
                    form = f, forms++;
                lua_pop(L, 1);
            }
            std::string note = text_field(L, r_idx, "help", "a row");
            if (!forms && !note.empty())
                form = "help", forms = 1;
            if (forms != 1)
                return luaL_error(L, "rood.menu.page: '%s' row %d: exactly one of setting, define, header, page, "
                    "action (or help alone: a line of help)", id.c_str(), row);
            std::string label = text_field(L, r_idx, "label", "a row");
            std::string value = !std::strcmp(form, "help") ? note : text_field(L, r_idx, form, "a row");
            if (value.empty())
                return luaL_error(L, "rood.menu.page: '%s' row %d: %s must be a non-empty string", id.c_str(), row, form);
            d.where = where_field(L, r_idx, row);
            lua_getfield(L, r_idx, "preview"); /* true / false: a preview offered (option_peek.h); nil: the setting's own */
            if (lua_isboolean(L, -1))
                d.preview = lua_toboolean(L, -1) ? 1 : -1;
            else if (!lua_isnil(L, -1))
                return luaL_error(L, "rood.menu.page: '%s' row %d: preview is true or false", id.c_str(), row);
            lua_pop(L, 1);
            if (!std::strcmp(form, "setting")) {
                check_setting(L, p, value, row);
                d.form = PC_OPT_SETTING, d.ref = hold(value);
            } else if (!std::strcmp(form, "define")) {
                d.form = PC_OPT_SETTING, d.ref = hold(define_setting(L, r_idx, value, row));
                label.clear(); /* the definition's own label */
            } else if (!std::strcmp(form, "header") || !std::strcmp(form, "help")) {
                d.form = !std::strcmp(form, "header") ? PC_OPT_HEADER : PC_OPT_NOTE;
                d.label = hold(value);
                if (!encodable(value))
                    log(LOG_WARN, p->id(), "rood.menu.page '" + id + "': row " + std::to_string(row)
                        + " has characters the game's font lacks (shown as ?)");
            } else if (!std::strcmp(form, "page")) {
                d.form = PC_OPT_LINK, d.ref = hold(qualify(p, value));
            } else { /* action */
                std::string a = value;
                if (a.find('/') == std::string::npos && a.find('.') == std::string::npos)
                    a = p->id() + "/" + a; /* a name of its own */
                if (a.find('/') != std::string::npos) {
                    if (!own(p, a))
                        return luaL_error(L, "rood.menu.page: '%s' row %d: action '%s' belongs to another plugin",
                            id.c_str(), row, a.c_str());
                } else if (pc_option_action_exists(a.c_str()) != 1)
                    return luaL_error(L, "rood.menu.page: '%s' row %d: no core action '%s'", id.c_str(), row, a.c_str());
                else if (!core && !pc_option_action_plugin_ok(a.c_str()))
                    return luaL_error(L, "rood.menu.page: '%s' row %d: core action '%s' is not offered by plugins' pages",
                        id.c_str(), row, a.c_str());
                d.form = PC_OPT_ACTION, d.ref = hold(a);
            }
            if (d.form != PC_OPT_HEADER && d.form != PC_OPT_NOTE) {
                if (!label.empty() && !encodable(label))
                    log(LOG_WARN, p->id(), "rood.menu.page '" + id + "': row " + std::to_string(row)
                        + "'s label has characters the game's font lacks (shown as ?)");
                d.label = label.empty() ? nullptr : hold(label);
                std::string row_help = d.form == PC_OPT_NOTE ? "" : note;
                d.help = row_help.empty() ? nullptr : hold(row_help);
            }
        } else
            return luaL_error(L, "rood.menu.page: '%s' row %d: a setting's id or a table", id.c_str(), row);
        rows.push_back(d);
        lua_settop(L, rows_idx);
    }
    lua_settop(L, rows_idx - 1);
    def.rows = rows.data(), def.row_count = (int)rows.size();

    /* The shipped plugin's first page: the fallback steps aside (its pages are these). */
    if (p->id() == CORE_PLUGIN && pc_option_pages_fallback_active())
        pc_option_pages_fallback(0);
    int rc = pc_option_pages_define(p->id().c_str(), &def);
    if (rc != PC_OPT_OK)
        return luaL_error(L, "rood.menu.page: '%s': %s", id.c_str(), pc_option_pages_error(rc));
    RegistryEntry e;
    e.kind = PAGE_KIND, e.id = id, e.owner = p->id(), e.group = group;
    for (int k = 0; k < PC_OPT_REFS; k++) {
        if (def.after[k])
            e.after.push_back(def.after[k]);
        if (def.before[k])
            e.before.push_back(def.before[k]);
    }
    e.data = Value::from_lua(L, 1);
    registry_put(e);
    lua_pushstring(L, id.c_str());
    return 1;
}

int api_action(lua_State* L)
{
    std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    Plugin* p = plugin_of(L);
    if (own(p, name))
        name = name.substr(p->id().size() + 1);
    if (!slug_ok(name))
        return luaL_error(L, "rood.menu.action: name '%s' is not a slug (1 to %d of a-z, 0-9, _ and -)", name.c_str(),
            (int)MAX_NAME);
    Action a;
    a.id = p->id() + "/" + name, a.owner = p->id();
    if (lua_istable(L, 3)) {
        a.label = text_field(L, 3, "label", "the action");
        a.help = text_field(L, 3, "help", "the action");
    }
    a.fn = Value::from_lua(L, 2).fn;
    for (Action& old : actions())
        if (old.id == a.id) {
            old = a;
            lua_pushstring(L, a.id.c_str());
            return 1;
        }
    if (actions().size() >= 4096)
        return luaL_error(L, "rood.menu.action: too many actions");
    actions().push_back(a);
    lua_pushstring(L, a.id.c_str());
    return 1;
}

int api_remove(lua_State* L)
{
    Plugin* p = plugin_of(L);
    std::string id = qualify(p, luaL_checkstring(L, 1));
    const RegistryEntry* e = registry_get(PAGE_KIND, id);
    if (!e)
        return lua_pushboolean(L, 0), 1;
    if (e->owner != p->id())
        return luaL_error(L, "rood.menu.remove: '%s' belongs to plugin %s", id.c_str(), e->owner.c_str());
    pc_option_pages_remove(id.c_str());
    registry_drop(PAGE_KIND, id);
    lua_pushboolean(L, 1);
    return 1;
}

void set_str(lua_State* L, const char* key, const char* value)
{
    lua_pushstring(L, value ? value : "");
    lua_setfield(L, -2, key);
}

int api_pages(lua_State* L)
{
    int n = pc_option_pages_count();
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        pc_option_page info;
        if (!pc_option_pages_info(i, &info))
            continue;
        lua_createtable(L, 0, 8);
        set_str(L, "id", info.id);
        set_str(L, "title", info.title);
        set_str(L, "help", info.help);
        set_str(L, "source", info.source);
        set_str(L, "group", info.group);
        pc_option_page parent;
        if (info.parent >= 0 && pc_option_pages_info(info.parent, &parent))
            set_str(L, "parent", parent.id);
        lua_pushinteger(L, info.depth);
        lua_setfield(L, -2, "depth");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* rood.menu.rows(page[, where]): the rows a screen shows now. */
int api_rows(lua_State* L)
{
    Plugin* p = plugin_of(L);
    std::string id = qualify(p, luaL_checkstring(L, 1));
    const char* w = luaL_optstring(L, 2, "all");
    int where = !std::strcmp(w, "classic") ? PC_UI_WHERE_CLASSIC : !std::strcmp(w, "modern") ? PC_UI_WHERE_MODERN
        : !std::strcmp(w, "all") ? PC_UI_WHERE_ALL : 0;
    if (!where)
        return luaL_error(L, "rood.menu.rows: where is \"classic\", \"modern\" or \"all\"");
    int page = pc_option_pages_index(id.c_str());
    if (page < 0)
        return lua_pushnil(L), 1;
    static const char* const KINDS[] = { "choice", "toggle", "slider", "action", "binding", "info", "header", "note",
        "link" };
    static const char* const FORMS[] = { "setting", "action", "page", "header", "help" };
    int n = pc_option_page_rows(page, where);
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        pc_option_row r;
        if (!pc_option_page_row(page, where, i, &r))
            continue;
        lua_createtable(L, 0, 10);
        set_str(L, "id", r.row.id);
        set_str(L, "form", FORMS[r.form]);
        set_str(L, "kind", r.row.kind >= 0 && r.row.kind <= PC_UI_SET_LINK ? KINDS[r.row.kind] : "");
        set_str(L, "label", r.row.label);
        set_str(L, "help", r.row.help);
        set_str(L, "text", r.row.value);
        lua_pushboolean(L, r.row.enabled);
        lua_setfield(L, -2, "enabled");
        if (r.row.reason[0])
            set_str(L, "reason", r.row.reason);
        if (r.form == PC_OPT_LINK) {
            pc_option_page target;
            if (r.link >= 0 && pc_option_pages_info(r.link, &target))
                set_str(L, "page", target.id);
        }
        static const char* const WHERE[] = { "", "classic", "modern", "all" };
        set_str(L, "where", WHERE[r.row.where & PC_UI_WHERE_ALL]);
        lua_pushboolean(L, r.row.preview);
        lua_setfield(L, -2, "preview");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int api_actions(lua_State* L)
{
    lua_newtable(L);
    lua_Integer n = 0;
    for (int i = 0; i < pc_option_action_count(); i++) {
        const char* id = pc_option_action_id(i);
        lua_createtable(L, 0, 4);
        set_str(L, "id", id);
        set_str(L, "label", pc_option_action_label(id));
        set_str(L, "help", pc_option_action_help(id));
        lua_pushboolean(L, pc_option_action_plugin_ok(id));
        lua_setfield(L, -2, "plugin_ok");
        lua_rawseti(L, -2, ++n);
    }
    for (const Action& a : actions()) {
        lua_createtable(L, 0, 4);
        set_str(L, "id", a.id.c_str());
        set_str(L, "label", a.label.c_str());
        set_str(L, "help", a.help.c_str());
        set_str(L, "owner", a.owner.c_str());
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

/* rood.menu.style{ classic = { row_x = 65, ... }, confirm = { seconds = 10 } }: the Option
 * screens' look and feel (option_style.h; fields named "<group>_<field>"), set by the shipped
 * rood.options from its table; back to the defaults when it unloads. Returns every value. */
int api_style(lua_State* L)
{
    Plugin* p = plugin_of(L);
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        if (p->id() != CORE_PLUGIN)
            return luaL_error(L, "rood.menu.style: only %s sets the Option screens' look", CORE_PLUGIN);
        lua_pushnil(L);
        while (lua_next(L, 1)) {
            if (lua_type(L, -2) != LUA_TSTRING || !lua_istable(L, -1))
                return luaL_error(L, "rood.menu.style: groups are tables by name (classic, confirm)");
            std::string group = lua_tostring(L, -2);
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) != LUA_TSTRING)
                    return luaL_error(L, "rood.menu.style: %s's fields are named", group.c_str());
                std::string name = group + "_" + lua_tostring(L, -2);
                double v = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? 1 : 0) : luaL_checknumber(L, -1);
                if (!pc_option_style_set(name.c_str(), v))
                    return luaL_error(L, "rood.menu.style: no field %s.%s", group.c_str(), lua_tostring(L, -2));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    }
    lua_newtable(L);
    for (int i = 0; i < pc_option_style_count(); i++) {
        lua_pushnumber(L, pc_option_style_value(i));
        lua_setfield(L, -2, pc_option_style_name(i));
    }
    return 1;
}

int api_generation(lua_State* L)
{
    lua_pushinteger(L, (lua_Integer)pc_option_pages_generation());
    return 1;
}

/* ---- the plugins' actions for option_actions.c ---- */
const Action* action(const char* id)
{
    for (const Action& a : actions())
        if (id && a.id == id) {
            Plugin* p = find(a.owner);
            return p && p->state == PLUGIN_LOADED ? &a : nullptr;
        }
    return nullptr;
}
int action_exists(const char* id) { return action(id) != nullptr; }
const char* action_label(const char* id)
{
    const Action* a = action(id);
    return a && !a->label.empty() ? a->label.c_str() : nullptr;
}
const char* action_help(const char* id)
{
    const Action* a = action(id);
    return a && !a->help.empty() ? a->help.c_str() : nullptr;
}
int action_run(const char* id)
{
    const Action* a = action(id);
    if (!a)
        return 0;
    std::shared_ptr<FunctionRef> fn = a->fn; /* the call may reload its plugin (and drop the entry) */
    std::string error;
    return call_function(fn, {}, nullptr, &error) ? 1 : 0;
}

void open_menu(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "page", api_page);
    set_cfunction(L, -1, "action", api_action);
    set_cfunction(L, -1, "remove", api_remove);
    set_cfunction(L, -1, "pages", api_pages);
    set_cfunction(L, -1, "rows", api_rows);
    set_cfunction(L, -1, "actions", api_actions);
    set_cfunction(L, -1, "generation", api_generation);
    set_cfunction(L, -1, "style", api_style);
    lock_table(L);
    lua_setfield(L, -2, "menu");
}

/* An unloading plugin's pages and actions go; the shipped pages give way to the fallback. */
void drop_menu(Plugin* p)
{
    actions().remove_if([&](const Action& a) { return a.owner == p->id(); });
    if (p->id() == CORE_PLUGIN)
        pc_option_style_reset(); /* its look's values go too */
    if (p->id() != CORE_PLUGIN)
        pc_option_pages_remove_source(p->id().c_str());
    else if (!pc_option_pages_fallback_active()) /* (the fallback's pages share its source: they stay) */
        pc_option_pages_fallback(1);             /* its pages go, the fallback's come back */
}

struct Register {
    Register()
    {
        static const pc_option_action_provider provider = { action_exists, action_label, action_help, nullptr,
            action_run };
        pc_option_actions_set_provider(&provider);
        api_openers().push_back(open_menu);
        unload_hooks().push_back(drop_menu);
    }
} register_menu;
} // namespace
} // namespace rl
