// The Lua visible layer's HUD API (plan pc/docs/plans/2026-10-18-lua-visible-layer.md;
// pc/docs/Extensions.md "HUD"): the game decides when and what, a plugin's
// claim how it looks.
//   rood.hud.claim(kind, fn)   fn(e, d) draws each element of the kind ("status") on every
//                              presented frame; the game's own drawing is suppressed while
//                              the claim is honoured (the modern look, or
//                              hud_classic_override). One owner per kind: the last in load
//                              order (the console's last of all), with a warning. A draw
//                              error stops honouring it (the game draws again) until the
//                              plugin reloads.
//   rood.hud.release(kind), rood.hud.kinds()
//   rood.draw (also d):        style{font, size, color, alpha, stroke, stroke_color,
//                              stroke_alpha, tracking, stretch, align, anchor} -> an
//                              immutable handle, parsed once; text(style, s, x, y[, color[,
//                              alpha]]), measure(style, s) -> w, h, rect(x, y, w, h, color[,
//                              alpha]); in the game's 320 x 240 screen, into a fixed
//                              command buffer (HUD_COMMANDS, HUD_TEXT_ARENA bytes of text)
//                              hud_draw.cpp draws in picture space.
//   rood.dev (dev mode only: ROOD_RPC set or ROOD_DEV=1): show(kind, {timing, variant, age,
//                              hold}), clear([kind]), list().
// The pass (pc_hud_frame), RPC hud {list, show, clear, stats, set} (pc_hud_rpc). Claims,
// styles and element tables belong to the plugin's load: an unload drops them; held
// moments belong to the host (hud_elements.cpp) and survive a reload.
#include "rl_internal.h"
#include "hud/hud_internal.h"
#include "light_prefabs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hud {
void (*render)(const Command*, int, const char*);
void (*measure)(const Style&, const char*, size_t, float*, float*);
bool Style::operator==(const Style& o) const
{
    return font == o.font && size == o.size && color == o.color && alpha == o.alpha && stroke == o.stroke
        && stroke_color == o.stroke_color && stroke_alpha == o.stroke_alpha && tracking == o.tracking
        && stretch == o.stretch && align == o.align && anchor == o.anchor;
}
} // namespace hud

namespace rl {
namespace {
using hud::Command;
using hud::Style;
using hud::StyleSlot;

// ui_text's styles (ui_text.h PC_UI_TEXT_*, in order): a style's font is one of theirs.
const char* const FONTS[] = { "dialogue", "menu", "info", "digits", "status", "labels" };
const char* const ALIGNS[] = { "left", "center", "right" };
const char* const ANCHORS[] = { "top", "middle", "baseline", "bottom" };

// ---- claims ----
struct Claim {
    Plugin* plugin;
    unsigned generation;
    int ref;               // fn in the plugin's registry
    unsigned long long seq;
    bool failed = false;   // its draw raised: not honoured until the plugin reloads
};
std::vector<Claim> claims[PC_HUD_KINDS];
int active[PC_HUD_KINDS] = { -1, -1 }; // index into claims[kind]
unsigned long long next_claim = 1;

bool valid(const Claim& c)
{
    return c.plugin->L && c.plugin->state == PLUGIN_LOADED && c.plugin->generation == c.generation
        && c.plugin->disable_reason.empty();
}

// The owner: the last in load order; console sandboxes after every plugin, by when they claimed.
void choose(int kind)
{
    std::vector<Plugin*> order = load_order();
    auto rank = [&](const Claim& c) -> long long {
        if (c.plugin->console)
            return (long long)order.size() + (long long)c.seq;
        auto it = std::find(order.begin(), order.end(), c.plugin);
        return it == order.end() ? -1 : (long long)(it - order.begin());
    };
    int best = -1;
    for (int i = 0; i < (int)claims[kind].size(); ++i)
        if (valid(claims[kind][i]) && (best < 0 || rank(claims[kind][i]) >= rank(claims[kind][best])))
            best = i;
    active[kind] = best;
}

const Claim* owner(int kind)
{
    if (kind <= 0 || kind >= PC_HUD_KINDS || active[kind] < 0 || active[kind] >= (int)claims[kind].size())
        return nullptr;
    const Claim& c = claims[kind][active[kind]];
    return valid(c) ? &c : nullptr;
}

// ---- styles ----
StyleSlot style_slots[HUD_STYLES];
struct Handle {
    unsigned slot, serial;
};
const char* const STYLE_META = "rood.style";

const StyleSlot& slot_of(lua_State* L, int idx, const char* fn)
{
    Handle* h = (Handle*)luaL_testudata(L, idx, STYLE_META);
    if (!h)
        luaL_error(L, "%s: argument %d must be a style (rood.draw.style{...})", fn, idx);
    const StyleSlot& s = style_slots[h->slot];
    if (!s.used || s.serial != h->serial || s.owner != plugin_of(L)->id())
        luaL_error(L, "%s: a stale style (made before the plugin reloaded)", fn);
    return s;
}

int choice(lua_State* L, const char* field, const char* const* names, int count)
{
    const char* s = lua_tostring(L, -1);
    if (!s || lua_type(L, -1) != LUA_TSTRING)
        return luaL_error(L, "rood.draw.style: %s must be a string", field);
    if (!std::strcmp(s, "centre"))
        s = "center";
    for (int i = 0; i < count; ++i)
        if (!std::strcmp(s, names[i]))
            return i;
    std::string list;
    for (int i = 0; i < count; ++i)
        list += std::string(i ? ", " : "") + names[i];
    return luaL_error(L, "rood.draw.style: %s '%s' is not one of %s", field, s, list.c_str());
}

double number(lua_State* L, const char* field, double lo, double hi)
{
    if (lua_type(L, -1) != LUA_TNUMBER)
        luaL_error(L, "rood.draw.style: %s must be a number", field);
    double v = lua_tonumber(L, -1);
    if (!std::isfinite(v) || v < lo || v > hi) {
        char text[160]; // (lua_pushfstring has no %g)
        std::snprintf(text, sizeof text, "rood.draw.style: %s %g is outside %g..%g", field, v, lo, hi);
        luaL_error(L, "%s", text);
    }
    return v;
}

// A colour: 0xRRGGBB, or "#rrggbb".
bool colour_at(lua_State* L, int idx, unsigned* out)
{
    if (lua_type(L, idx) == LUA_TNUMBER && lua_isinteger(L, idx)) {
        lua_Integer v = lua_tointeger(L, idx);
        if (v < 0 || v > 0xFFFFFF)
            return false;
        *out = (unsigned)v;
        return true;
    }
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char* s = lua_tostring(L, idx);
        if (s[0] != '#' || std::strlen(s) != 7 || std::strspn(s + 1, "0123456789abcdefABCDEF") != 6)
            return false;
        *out = (unsigned)std::strtoul(s + 1, nullptr, 16);
        return true;
    }
    return false;
}
unsigned colour_field(lua_State* L, const char* field)
{
    unsigned c;
    if (!colour_at(L, -1, &c))
        luaL_error(L, "rood.draw.style: %s must be 0xRRGGBB or \"#rrggbb\"", field);
    return c;
}

int api_style(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    Style st;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            return luaL_error(L, "rood.draw.style: fields are named (a %s key)", luaL_typename(L, -2));
        const char* k = lua_tostring(L, -2);
        if (!std::strcmp(k, "font"))
            st.font = choice(L, k, FONTS, 6);
        else if (!std::strcmp(k, "size"))
            st.size = (float)number(L, k, 1, 120);
        else if (!std::strcmp(k, "color") || !std::strcmp(k, "colour"))
            st.color = colour_field(L, k);
        else if (!std::strcmp(k, "alpha"))
            st.alpha = (float)number(L, k, 0, 1);
        else if (!std::strcmp(k, "stroke"))
            st.stroke = (float)number(L, k, 0, 16);
        else if (!std::strcmp(k, "stroke_color") || !std::strcmp(k, "stroke_colour"))
            st.stroke_color = colour_field(L, k);
        else if (!std::strcmp(k, "stroke_alpha"))
            st.stroke_alpha = (float)number(L, k, 0, 1);
        else if (!std::strcmp(k, "tracking"))
            st.tracking = (float)number(L, k, -1, 4);
        else if (!std::strcmp(k, "stretch"))
            st.stretch = (float)number(L, k, 0.1, 10);
        else if (!std::strcmp(k, "align"))
            st.align = choice(L, k, ALIGNS, 3);
        else if (!std::strcmp(k, "anchor"))
            st.anchor = choice(L, k, ANCHORS, 4);
        else
            return luaL_error(L, "rood.draw.style: unknown field '%s'", k);
        lua_pop(L, 1);
    }
    Plugin* p = plugin_of(L);
    int found = -1, empty = -1;
    for (int i = 0; i < HUD_STYLES && found < 0; ++i) {
        if (style_slots[i].used && style_slots[i].owner == p->id() && style_slots[i].style == st)
            found = i; // the same style again: the same slot
        else if (!style_slots[i].used && empty < 0)
            empty = i;
    }
    if (found < 0) {
        if (empty < 0)
            return luaL_error(L, "rood.draw.style: no room for more styles (%d in use; make them once, at load)",
                HUD_STYLES);
        found = empty;
        style_slots[found].used = true;
        style_slots[found].owner = p->id();
        style_slots[found].style = st;
    }
    Handle* h = (Handle*)lua_newuserdatauv(L, sizeof(Handle), 0);
    h->slot = (unsigned)found;
    h->serial = style_slots[found].serial;
    luaL_setmetatable(L, STYLE_META);
    return 1;
}

// A style's fields, read only (style.size).
int style_index(lua_State* L)
{
    const Style& st = slot_of(L, 1, "style").style;
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "font"))
        lua_pushstring(L, FONTS[st.font]);
    else if (!std::strcmp(k, "size"))
        lua_pushnumber(L, st.size);
    else if (!std::strcmp(k, "color"))
        lua_pushinteger(L, st.color);
    else if (!std::strcmp(k, "alpha"))
        lua_pushnumber(L, st.alpha);
    else if (!std::strcmp(k, "stroke"))
        lua_pushnumber(L, st.stroke);
    else if (!std::strcmp(k, "stroke_color"))
        lua_pushinteger(L, st.stroke_color);
    else if (!std::strcmp(k, "stroke_alpha"))
        lua_pushnumber(L, st.stroke_alpha);
    else if (!std::strcmp(k, "tracking"))
        lua_pushnumber(L, st.tracking);
    else if (!std::strcmp(k, "stretch"))
        lua_pushnumber(L, st.stretch);
    else if (!std::strcmp(k, "align"))
        lua_pushstring(L, ALIGNS[st.align]);
    else if (!std::strcmp(k, "anchor"))
        lua_pushstring(L, ANCHORS[st.anchor]);
    else
        lua_pushnil(L);
    return 1;
}
int style_newindex(lua_State* L) { return luaL_error(L, "a style is immutable (make another with rood.draw.style)"); }
int style_tostring(lua_State* L)
{
    const Style& st = slot_of(L, 1, "style").style;
    lua_pushfstring(L, "style(%s %d)", FONTS[st.font], (int)st.size);
    return 1;
}

// ---- the command buffer ----
Command command_buffer[HUD_COMMANDS];
int command_used;
char arena[HUD_TEXT_ARENA];
unsigned arena_used;
unsigned long long dropped; // commands the full buffer refused
struct Drawing {
    int anchor;
} drawing;
bool in_draw; // inside a claim's draw function

void need_draw(lua_State* L, const char* fn)
{
    if (!in_draw)
        luaL_error(L, "%s: only inside a HUD draw function (rood.hud.claim)", fn);
}

float coordinate(lua_State* L, int idx, const char* fn)
{
    lua_Number v = luaL_checknumber(L, idx);
    if (!std::isfinite(v) || std::fabs(v) > 100000)
        luaL_error(L, "%s: argument %d is not a usable coordinate", fn, idx);
    return (float)v;
}

void optional_colour(lua_State* L, int idx, unsigned* colour, const char* fn)
{
    if (lua_isnoneornil(L, idx))
        return;
    if (!colour_at(L, idx, colour))
        luaL_error(L, "%s: argument %d must be 0xRRGGBB or \"#rrggbb\"", fn, idx);
}
float optional_alpha(lua_State* L, int idx, float fallback)
{
    if (lua_isnoneornil(L, idx))
        return fallback;
    lua_Number a = luaL_checknumber(L, idx);
    return (float)(a < 0 ? 0 : a > 1 ? 1 : a);
}

Command* next_command()
{
    if (command_used >= HUD_COMMANDS) {
        ++dropped;
        return nullptr;
    }
    return &command_buffer[command_used++];
}

int api_text(lua_State* L)
{
    const char* fn = "rood.draw.text";
    const StyleSlot& s = slot_of(L, 1, fn);
    size_t len;
    const char* text = luaL_checklstring(L, 2, &len);
    float x = coordinate(L, 3, fn), y = coordinate(L, 4, fn);
    unsigned colour = s.style.color;
    optional_colour(L, 5, &colour, fn);
    float alpha = optional_alpha(L, 6, s.style.alpha);
    need_draw(L, fn);
    if (len > 255)
        return luaL_error(L, "%s: text longer than 255 bytes", fn);
    if (arena_used + len + 1 > HUD_TEXT_ARENA) {
        ++dropped;
        lua_pushboolean(L, 0);
        return 1;
    }
    Command* c = next_command();
    if (!c) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::memcpy(arena + arena_used, text, len);
    arena[arena_used + len] = 0;
    *c = Command();
    c->type = hud::CMD_TEXT;
    c->anchor = (unsigned char)drawing.anchor;
    c->style = (unsigned short)(&s - style_slots);
    c->x = x, c->y = y;
    c->color = colour, c->alpha = alpha;
    c->text = arena_used, c->length = (unsigned)len;
    arena_used += (unsigned)len + 1;
    lua_pushboolean(L, 1);
    return 1;
}

int api_rect(lua_State* L)
{
    const char* fn = "rood.draw.rect";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn), w = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    unsigned colour = 0;
    if (!colour_at(L, 5, &colour))
        return luaL_error(L, "%s: argument 5 must be 0xRRGGBB or \"#rrggbb\"", fn);
    float alpha = optional_alpha(L, 6, 1);
    need_draw(L, fn);
    Command* c = next_command();
    if (!c) {
        lua_pushboolean(L, 0);
        return 1;
    }
    *c = Command();
    c->type = hud::CMD_RECT;
    c->anchor = (unsigned char)drawing.anchor;
    c->x = x, c->y = y, c->w = w, c->h = h;
    c->color = colour, c->alpha = alpha;
    lua_pushboolean(L, 1);
    return 1;
}

void estimate(const Style& st, const char*, size_t len, float* w, float* h)
{
    *w = (float)len * st.size * (0.55f + st.tracking) * st.stretch;
    *h = st.size;
}

int api_measure(lua_State* L)
{
    const StyleSlot& s = slot_of(L, 1, "rood.draw.measure");
    size_t len;
    const char* text = luaL_checklstring(L, 2, &len);
    float w = 0, h = 0;
    (hud::measure ? hud::measure : estimate)(s.style, text, len, &w, &h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

// ---- rood.hud ----
int kind_arg(lua_State* L, int idx, const char* fn)
{
    const char* name = luaL_checkstring(L, idx);
    int kind = hud::kind_of(name);
    if (!kind)
        return luaL_error(L, "%s: unknown HUD kind '%s' (known: status)", fn, name);
    return kind;
}

void drop_claim(int kind, Plugin* p, lua_State* L)
{
    auto& list = claims[kind];
    for (auto it = list.begin(); it != list.end();)
        if (it->plugin == p) {
            if (L && it->generation == p->generation)
                luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            it = list.erase(it);
        } else {
            ++it;
        }
}

int api_claim(lua_State* L)
{
    int kind = kind_arg(L, 1, "rood.hud.claim");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    Plugin* p = plugin_of(L);
    const Claim* had = owner(kind);
    Plugin* before = had ? had->plugin : nullptr;
    drop_claim(kind, p, L);
    lua_settop(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    claims[kind].push_back({ p, p->generation, ref, next_claim++ });
    choose(kind);
    const Claim* now = owner(kind);
    if (before && before != p && now && now->plugin == p)
        log(LOG_WARN, p->id(), std::string("takes the HUD kind '") + hud::kind_name(kind) + "' from "
            + before->id() + " (one owner per kind: the last in load order)");
    else if (now && now->plugin != p)
        log(LOG_WARN, p->id(), std::string("claims the HUD kind '") + hud::kind_name(kind) + "', which "
            + now->plugin->id() + " keeps (later in load order)");
    lua_pushboolean(L, now && now->plugin == p);
    return 1;
}

int api_release(lua_State* L)
{
    int kind = kind_arg(L, 1, "rood.hud.release");
    drop_claim(kind, plugin_of(L), L);
    choose(kind);
    return 0;
}

int api_kinds(lua_State* L)
{
    lua_createtable(L, PC_HUD_KINDS - 1, 0);
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        lua_pushstring(L, hud::kind_name(k));
        lua_rawseti(L, -2, k);
    }
    return 1;
}

// ---- rood.dev ----
bool dev_mode()
{
    const char* rpc = std::getenv("ROOD_RPC");
    const char* dev = std::getenv("ROOD_DEV");
    return (rpc && *rpc) || (dev && !std::strcmp(dev, "1"));
}

// show's args from the table at idx (nil: the defaults); false and why.
bool show_args(lua_State* L, int idx, hud::ShowArgs* a, std::string* why)
{
    if (lua_isnoneornil(L, idx))
        return true;
    if (!lua_istable(L, idx)) {
        *why = "args must be a table";
        return false;
    }
    idx = lua_absindex(L, idx);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        const char* k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "";
        if (!std::strcmp(k, "timing")) {
            int t = lua_type(L, -1) == LUA_TSTRING ? hud::timing_of(lua_tostring(L, -1))
                : lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
            if (t < 0 || t > 4) {
                *why = "timing must be too_fast, fast, success, slow or too_slow";
                return false;
            }
            a->timing = t;
        } else if (!std::strcmp(k, "variant") && lua_isinteger(L, -1)) {
            a->variant = (int)lua_tointeger(L, -1);
        } else if (!std::strcmp(k, "age") && lua_isinteger(L, -1)) {
            a->age = (int)lua_tointeger(L, -1);
        } else if (!std::strcmp(k, "hold") && lua_isboolean(L, -1)) {
            a->hold = lua_toboolean(L, -1);
        } else {
            *why = std::string("unknown or mistyped arg '") + k + "' (timing, variant, age, hold)";
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

int api_show(lua_State* L)
{
    int kind = kind_arg(L, 1, "rood.dev.show");
    hud::ShowArgs a;
    std::string why;
    if (!show_args(L, 2, &a, &why) || !hud::show(kind, a, &why))
        return luaL_error(L, "rood.dev.show: %s", why.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int api_clear(lua_State* L)
{
    hud::clear(lua_isnoneornil(L, 1) ? 0 : kind_arg(L, 1, "rood.dev.clear"));
    return 0;
}

void set_field(lua_State* L, const char* k, lua_Number v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, k);
}
void set_int(lua_State* L, const char* k, lua_Integer v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_str(lua_State* L, const char* k, const char* v)
{
    lua_pushstring(L, v);
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v)
{
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}

// An element's fields into the table on top (reused: every field set every time).
void fill_element(lua_State* L, const hud::Element& e)
{
    set_str(L, "kind", hud::kind_name(e.f.kind));
    set_int(L, "id", e.id);
    set_str(L, "text", e.f.text);
    set_field(L, "x", e.f.x);
    set_field(L, "y", e.f.y);
    set_field(L, "w", e.f.w);
    set_field(L, "h", e.f.h);
    set_int(L, "rgb", e.f.rgb);
    set_str(L, "timing", hud::timing_name(e.f.timing));
    set_int(L, "variant", e.f.variant);
    set_int(L, "age", e.f.age);
    set_int(L, "life", e.f.life);
    set_bool(L, "held", e.held);
    set_bool(L, "synthetic", e.synthetic);
}

int api_list(lua_State* L)
{
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < PC_HUD_POOL; ++i) {
        const hud::Element& e = hud::pool()[i];
        if (!hud::live(e))
            continue;
        lua_createtable(L, 0, 16);
        fill_element(L, e);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// ---- the pass ----
#define FRAME_RING 256
double frame_ms[FRAME_RING]; // the claims' Lua per presented frame
unsigned frame_count;
double render_total_ms; // the renderer's, since the last reset
unsigned render_count;
struct Job {
    const Claim* claim;
    const hud::Element* element;
    bool ok;
};
Job job;

void draw_element(lua_State* L)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, job.claim->ref);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "rood.hud.element") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 16);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "rood.hud.element");
    }
    fill_element(L, *job.element);
    lua_getfield(L, LUA_REGISTRYINDEX, "rood.hud.draw");
    lua_call(L, 2, 0);
    job.ok = true;
}

void drop_hud(Plugin* p)
{
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        size_t before = claims[k].size();
        drop_claim(k, p, nullptr); // (its state closes: the references go with it)
        if (claims[k].size() != before)
            choose(k);
    }
    for (StyleSlot& s : style_slots)
        if (s.used && s.owner == p->id()) {
            s.used = false;
            s.owner.clear();
            ++s.serial;
        }
}

void open_hud(lua_State* L, Plugin*)
{
    if (luaL_newmetatable(L, STYLE_META)) {
        lua_pushcfunction(L, style_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, style_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, style_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
    lua_newtable(L);
    set_cfunction(L, -1, "claim", api_claim);
    set_cfunction(L, -1, "release", api_release);
    set_cfunction(L, -1, "kinds", api_kinds);
    lock_table(L);
    lua_setfield(L, -2, "hud");
    lua_newtable(L);
    set_cfunction(L, -1, "style", api_style);
    set_cfunction(L, -1, "text", api_text);
    set_cfunction(L, -1, "measure", api_measure);
    set_cfunction(L, -1, "rect", api_rect);
    lock_table(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "rood.hud.draw"); // d in draw functions
    lua_setfield(L, -2, "draw");
    if (dev_mode()) {
        lua_newtable(L);
        set_cfunction(L, -1, "show", api_show);
        set_cfunction(L, -1, "clear", api_clear);
        set_cfunction(L, -1, "list", api_list);
        lock_table(L);
        lua_setfield(L, -2, "dev");
    }
}

struct Register {
    Register()
    {
        api_openers().push_back(open_hud);
        unload_hooks().push_back(drop_hud);
        tick_hooks().push_back(pc_hud_tick);
    }
} register_hud;
} // namespace

// The claims' draws of every live element into the command buffer (lua_probe calls it
// without a renderer); returns the commands.
int hud_pass()
{
    command_used = 0;
    arena_used = 0;
    double start = now_seconds();
    for (int i = 0; i < PC_HUD_POOL; ++i) {
        hud::Element& e = hud::pool()[i];
        if (!hud::live(e) || !(e.suppressed || e.synthetic))
            continue; // the game draws it (no claim was honoured when it drew)
        Claim* c = const_cast<Claim*>(owner(e.f.kind));
        if (!c || c->failed || !pc_hud_honoured(e.f.kind))
            continue;
        job = { c, &e, false };
        drawing.anchor = e.f.anchor;
        in_draw = true;
        std::string error;
        bool ok = run(c->plugin, "hud draw", BUDGET_CALL, draw_element, &error) && job.ok;
        in_draw = false;
        hud::KindStats& s = hud::stats(e.f.kind);
        if (ok) {
            ++s.drawn;
            ++e.drawn;
        } else {
            ++s.faults;
            c->failed = true; // the game's own drawing from the next frame on
            log(LOG_WARN, c->plugin->id(), std::string("its '") + hud::kind_name(e.f.kind)
                + "' draw failed: the game draws it until the plugin reloads");
        }
    }
    if (command_used || frame_count) // (frames with nothing claimed do not dilute the figures)
        frame_ms[frame_count++ % FRAME_RING] = (now_seconds() - start) * 1000.0;
    return command_used;
}

namespace {
std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string num(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", v);
    return buffer;
}

std::string element_json(const hud::Element& e)
{
    const Claim* c = owner(e.f.kind);
    return "{\"id\":" + std::to_string(e.id) + ",\"kind\":" + q(hud::kind_name(e.f.kind)) + ",\"text\":" + q(e.f.text)
        + ",\"x\":" + num(e.f.x) + ",\"y\":" + num(e.f.y) + ",\"w\":" + num(e.f.w) + ",\"h\":" + num(e.f.h)
        + ",\"rgb\":" + std::to_string(e.f.rgb) + ",\"timing\":" + q(hud::timing_name(e.f.timing))
        + ",\"variant\":" + std::to_string(e.f.variant) + ",\"age\":" + std::to_string(e.f.age)
        + ",\"life\":" + std::to_string(e.f.life) + ",\"held\":" + (e.held ? "true" : "false")
        + ",\"synthetic\":" + (e.synthetic ? "true" : "false") + ",\"live\":" + (hud::live(e) ? "true" : "false")
        + ",\"suppressed\":" + (e.suppressed ? "true" : "false") + ",\"drawn\":" + std::to_string(e.drawn)
        + ",\"claimed_by\":" + (c ? q(c->plugin->id()) : "null") + ",\"age_vsyncs\":"
        + std::to_string(hud::vsync() - e.first_seen) + "}";
}

std::string stats_json()
{
    std::vector<double> ms(frame_ms, frame_ms + std::min<unsigned>(frame_count, FRAME_RING));
    std::sort(ms.begin(), ms.end());
    double mean = 0;
    for (double v : ms)
        mean += v;
    mean = ms.empty() ? 0 : mean / ms.size();
    double p99 = ms.empty() ? 0 : ms[std::min(ms.size() - 1, (size_t)(ms.size() * 0.99))];
    std::string kinds;
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        const hud::KindStats& s = hud::stats(k);
        const Claim* c = owner(k);
        kinds += std::string(k > 1 ? "," : "") + q(hud::kind_name(k)) + ":{\"seen\":" + std::to_string(s.seen)
            + ",\"suppressed\":" + std::to_string(s.suppressed) + ",\"fallback\":" + std::to_string(s.fallback)
            + ",\"drawn\":" + std::to_string(s.drawn) + ",\"faults\":" + std::to_string(s.faults)
            + ",\"claimed_by\":" + (c ? q(c->plugin->id()) : "null") + ",\"claim_failed\":"
            + (c && c->failed ? "true" : "false") + ",\"honoured\":" + (pc_hud_honoured(k) ? "true" : "false") + "}";
    }
    std::string cmds;
    size_t memory = 0;
    for (int i = 0; i < command_used && i < 16; ++i) {
        const Command& c = command_buffer[i];
        cmds += std::string(i ? "," : "") + "{\"type\":" + q(c.type == hud::CMD_TEXT ? "text" : "rect")
            + ",\"x\":" + num(c.x) + ",\"y\":" + num(c.y) + ",\"color\":" + std::to_string(c.color);
        if (c.type == hud::CMD_TEXT) {
            const Style& st = style_slots[c.style].style;
            cmds += ",\"text\":" + q(std::string(arena + c.text, c.length)) + ",\"font\":" + q(FONTS[st.font])
                + ",\"size\":" + num(st.size) + ",\"stroke\":" + num(st.stroke);
        } else {
            cmds += ",\"w\":" + num(c.w) + ",\"h\":" + num(c.h);
        }
        cmds += "}";
    }
    int styles_used = 0;
    for (const StyleSlot& s : style_slots)
        styles_used += s.used;
    for (int k = 1; k < PC_HUD_KINDS; ++k)
        if (const Claim* c = owner(k))
            memory += c->plugin->mem.used;
    return "{\"kinds\":{" + kinds + "},\"frames\":" + std::to_string(frame_count) + ",\"mean_ms\":" + num(mean)
        + ",\"p99_ms\":" + num(p99) + ",\"max_ms\":" + num(ms.empty() ? 0 : ms.back())
        + ",\"render_mean_ms\":" + num(render_count ? render_total_ms / render_count : 0)
        + ",\"commands\":" + std::to_string(command_used) + ",\"text_bytes\":" + std::to_string(arena_used)
        + ",\"dropped\":" + std::to_string(dropped) + ",\"styles\":" + std::to_string(styles_used)
        + ",\"claim_memory_kb\":" + num(memory / 1024.0) + ",\"classic_override\":" + std::to_string(pc_hud_classic_override)
        + ",\"last\":[" + cmds + "]}";
}
} // namespace
} // namespace rl

extern "C" void pc_hud_frame(void)
{
    if (!rl::initialized())
        return;
    if (rl::hud_pass() > 0 && hud::render) {
        double start = rl::now_seconds();
        hud::render(rl::command_buffer, rl::command_used, rl::arena);
        rl::render_total_ms += (rl::now_seconds() - start) * 1000.0;
        ++rl::render_count;
    }
}

namespace hud {
Command* commands() { return rl::command_buffer; }
int command_count() { return rl::command_used; }
const char* text_arena() { return rl::arena; }
StyleSlot* styles() { return rl::style_slots; }
bool claimed(int kind)
{
    const rl::Claim* c = rl::owner(kind);
    return c && !c->failed;
}
} // namespace hud

// RPC hud (dev_rpc.c; pc/src/hud/hud.h):
//   list                          the elements (live and recent) and the claims
//   show {kind, timing, variant, age, hold}   as rood.dev.show
//   clear {kind}                  as rood.dev.clear (every kind without kind)
//   stats {reset}                 per kind: seen, suppressed, fallback, drawn, faults, the owner;
//                                 the claims' Lua per frame (mean / p99 / max ms), the renderer's
//                                 mean ms, the last frame's commands (16)
//   set {classic_override}        settings hud_classic_override
extern "C" int pc_hud_rpc(const char* params, char* out, int size)
{
    using prefab::Json;
    Json p;
    std::string error, reply;
    if (!prefab::json_parse(params ? params : "{}", p, &error) || p.type != Json::Object) {
        std::snprintf(out, (size_t)size, "bad params: %s", error.c_str());
        return -1;
    }
    const Json* a = p.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    const Json* k = p.get("kind");
    int kind = k && k->type == Json::String ? hud::kind_of(k->string.c_str()) : 0;
    if (k && !kind) {
        std::snprintf(out, (size_t)size, "unknown HUD kind");
        return -1;
    }
    if (action == "list") {
        reply = "{\"elements\":[";
        bool first = true;
        for (int i = 0; i < PC_HUD_POOL; ++i)
            if (hud::pool()[i].used) {
                reply += (first ? "" : ",") + rl::element_json(hud::pool()[i]);
                first = false;
            }
        reply += "],\"vsync\":" + std::to_string(hud::vsync()) + "}";
    } else if (action == "show") {
        hud::ShowArgs args;
        const Json* t = p.get("timing");
        if (t && t->type == Json::String)
            args.timing = hud::timing_of(t->string.c_str());
        else if (t && t->type == Json::Number)
            args.timing = (int)t->number;
        if (const Json* v = p.get("variant"))
            args.variant = (int)v->number;
        if (const Json* v = p.get("age"))
            args.age = (int)v->number;
        if (const Json* v = p.get("hold"))
            args.hold = v->boolean;
        if (args.timing < 0 || args.timing > 4) {
            std::snprintf(out, (size_t)size, "timing must be too_fast, fast, success, slow or too_slow");
            return -1;
        }
        if (!hud::show(kind ? kind : PC_HUD_STATUS, args, &error)) {
            std::snprintf(out, (size_t)size, "%s", error.c_str());
            return -1;
        }
        reply = "{\"ok\":true}";
    } else if (action == "clear") {
        hud::clear(kind);
        reply = "{\"ok\":true}";
    } else if (action == "stats") {
        const Json* r = p.get("reset");
        if (r && r->type == Json::Bool && r->boolean) {
            for (int i = 1; i < PC_HUD_KINDS; ++i)
                hud::stats(i) = hud::KindStats();
            rl::frame_count = 0;
            rl::dropped = 0;
            rl::render_total_ms = 0;
            rl::render_count = 0;
        }
        reply = rl::stats_json();
    } else if (action == "set") {
        if (const Json* v = p.get("classic_override"))
            pc_hud_classic_override = v->type == Json::Bool ? v->boolean : v->number != 0;
        reply = rl::stats_json();
    } else {
        std::snprintf(out, (size_t)size, "unknown action '%s' (list, show, clear, stats, set)", action.c_str());
        return -1;
    }
    if ((int)reply.size() >= size) {
        std::snprintf(out, (size_t)size, "reply too long");
        return -1;
    }
    std::memcpy(out, reply.c_str(), reply.size() + 1);
    return (int)reply.size();
}
