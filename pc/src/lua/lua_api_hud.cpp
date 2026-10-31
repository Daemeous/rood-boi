// The Lua visible layer's HUD API (plan pc/docs/plans/2026-10-18-lua-visible-layer.md;
// pc/docs/Extensions.md "HUD"): the game decides when and what, a plugin's
// claim how it looks.
//   rood.hud.claim(kind, fn[, {flavour}])   fn(e, d) draws each element of the kind (status,
//                              chain, buff, damage, travel) on every presented frame; the game's
//                              own drawing is suppressed while the claim is honoured (the modern
//                              look; the classic look: below). flavour "modern", "classic" or
//                              "both" (default): the looks it draws in. One owner per kind and
//                              look: a claim made for that flavour alone first, then the last
//                              in load order (the console's last of all), with a warning. A
//                              draw error stops honouring it (the game draws again) until the
//                              plugin reloads. The classic look honours a claim made for the
//                              classic flavour when the kind's hud_classic_<kind> is on (the
//                              kinds through the parity gate), any with hud_classic_override.
//   rood.hud.release(kind), rood.hud.kinds(), rood.hud.honoured(kind), rood.hud.look()
//   rood.hud.typography(style, {...} | nil): ui_text's style fields for this run (the classic
//                              look's typography; never saved), dropped on unload.
//   rood.draw (also d):        style{font, size, cap, color, alpha, stroke, stroke_color,
//                              stroke_alpha, tracking, stretch, align, anchor} -> an
//                              immutable handle, parsed once; text(style, s, x, y[, color[,
//                              alpha]]), fit(style, s, x, y, w, h[, color[, alpha]]) (inside
//                              a box, the outline kept in), measure(style, s) -> w, h,
//                              rect(x, y, w, h, color[, alpha]), glyph(button, x, y, h[,
//                              style[, color[, alpha]]]) -> w (a prompt icon),
//                              glyph_width(button, h); in the game's screen (320 x 224 in
//                              the field), into a fixed command buffer (HUD_COMMANDS,
//                              HUD_TEXT_ARENA bytes of text) hud_draw.cpp draws in picture space.
//   rood.dev (dev mode only: ROOD_RPC set or ROOD_DEV=1): show(kind, {timing, variant, age,
//                              hold, value, sign / heal, pool, stat, slot}), clear([kind]), list().
// The pass (pc_hud_frame), RPC hud {list, show, clear, stats, set} (pc_hud_rpc). Claims,
// styles and element tables belong to the plugin's load: an unload drops them; held
// moments belong to the host (hud_elements.cpp) and survive a reload.
#include "rl_internal.h"
#include "hud/hud_internal.h"
#include "lua_bench.h"
#include "light_prefabs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ui_text.c's typography layer (ui_text.h; absent in lua_probe)
extern "C" int pc_ui_text_style_index(const char* name) __attribute__((weak));
extern "C" int pc_ui_text_override(int style, const char* field, const char* value) __attribute__((weak));
extern "C" void pc_ui_text_override_clear(int style) __attribute__((weak));
extern "C" unsigned pc_ui_text_overridden(int style) __attribute__((weak));

namespace hud {
void (*render)(const Command*, int, const char*);
void (*measure)(const Style&, const char*, size_t, float*, float*);
float (*glyph_width)(int, float);
bool Style::operator==(const Style& o) const
{
    return font == o.font && size == o.size && color == o.color && alpha == o.alpha && stroke == o.stroke
        && stroke_color == o.stroke_color && stroke_alpha == o.stroke_alpha && tracking == o.tracking
        && stretch == o.stretch && align == o.align && anchor == o.anchor && cap == o.cap;
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
enum { FLAVOUR_MODERN = 1, FLAVOUR_CLASSIC = 2, FLAVOUR_BOTH = 3 };
struct Claim {
    Plugin* plugin;
    unsigned generation;
    int ref;               // fn in the plugin's registry
    unsigned long long seq;
    bool failed = false;   // its draw raised an error: not honoured until the plugin reloads (or retry_at)
    double retry_at = 0;   // a shipped plugin's failed claim is honoured again from then (fault_cooldown_s)
    int flavours = FLAVOUR_BOTH; // the looks it draws in
};
// Whether a claim is down now: failed, and (a shipped plugin's) not yet due to be tried again.
bool claim_down(Claim* c)
{
    if (!c->failed)
        return false;
    if (c->retry_at > 0 && now_seconds() >= c->retry_at) {
        c->failed = false;
        c->retry_at = 0;
        log(LOG_INFO, c->plugin->id(), "its failed HUD draw is tried again (the cool-down is over)");
        return false;
    }
    return true;
}
bool claim_down(const Claim* c) { return claim_down(const_cast<Claim*>(c)); }
// A claim's draw did not finish. Over its budget (a slow frame: the plugin's fault, counted by the fault
// policy): drawn again next frame. An error: the game draws the kind, until the plugin reloads, or for a
// shipped plugin until fault_cooldown_s has passed (a slow moment never loses a shipped kind for the
// session). Returns whether the claim is down now.
bool claim_fault(Claim* c, int kind, const char* what)
{
    if (c->plugin->budget.killed)
        return false;
    c->failed = true;
    bool retry = c->plugin->shipped && config.fault_cooldown_s > 0;
    c->retry_at = retry ? now_seconds() + config.fault_cooldown_s : 0;
    char until[64];
    std::snprintf(until, sizeof until, retry ? "for %g s" : "until the plugin reloads", config.fault_cooldown_s);
    log(LOG_WARN, c->plugin->id(), std::string("its '") + hud::kind_name(kind) + "' " + what
        + " failed: the game draws it " + until);
    return true;
}
std::vector<Claim> claims[PC_HUD_KINDS];
int active[2][PC_HUD_KINDS] = {}; // per look (0 modern, 1 classic): 1 + the owner's index in claims[kind], 0 none
unsigned long long next_claim = 1;
int look_now() { return hud::classic_look() ? 1 : 0; }

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
    for (int look = 0; look < 2; ++look) { // a claim made for this look's flavour alone first
        int bit = look ? FLAVOUR_CLASSIC : FLAVOUR_MODERN, best = -1;
        auto key = [&](const Claim& c) { return (c.flavours == bit ? 1LL << 40 : 0) + rank(c); };
        for (int i = 0; i < (int)claims[kind].size(); ++i) {
            const Claim& c = claims[kind][i];
            if (valid(c) && (c.flavours & bit) && (best < 0 || key(c) >= key(claims[kind][best])))
                best = i;
        }
        active[look][kind] = best + 1;
    }
}

const Claim* owner_for(int kind, int look)
{
    if (kind <= 0 || kind >= PC_HUD_KINDS || active[look][kind] <= 0
        || active[look][kind] > (int)claims[kind].size())
        return nullptr;
    const Claim& c = claims[kind][active[look][kind] - 1];
    return valid(c) ? &c : nullptr;
}
const Claim* owner(int kind) { return owner_for(kind, look_now()); }

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
        else if (!std::strcmp(k, "cap"))
            st.cap = (float)number(L, k, 0, 120);
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
        style_slots[found].refs = 0;
        style_slots[found].owner = p->id();
        style_slots[found].style = st;
    }
    ++style_slots[found].refs;
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
    else if (!std::strcmp(k, "cap"))
        lua_pushnumber(L, st.cap);
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
// A handle collected: the last one frees its slot (a look rebuilt as settings change does not fill the table).
int style_gc(lua_State* L)
{
    Handle* h = (Handle*)luaL_testudata(L, 1, STYLE_META);
    if (!h || h->slot >= HUD_STYLES)
        return 0;
    StyleSlot& s = style_slots[h->slot];
    if (s.used && s.serial == h->serial && s.refs && !--s.refs) {
        s.used = false;
        s.owner.clear();
        ++s.serial;
    }
    return 0;
}
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
    int element_anchor; // the element's (d.anchor("element"))
} drawing;
bool in_draw; // inside a claim's draw function
// The classic flavour (hud.h pc_hud_classic_cmd): a draw run at the game's hook, into these.
bool classic_pass;
pc_hud_classic_cmd classic_buffer[PC_HUD_CLASSIC_COMMANDS];
int classic_used;
char classic_arena[8192];
unsigned classic_arena_used;
int classic_anchor = -1; // d.anchor in the last classic draw (PC_HUD_ANCHOR_*), -1: the element's
int classic_layer = PC_HUD_LAYER_KIND; // d.layer: the slot of the commands that follow

pc_hud_classic_cmd* next_classic(int type)
{
    if (classic_used >= PC_HUD_CLASSIC_COMMANDS) {
        ++dropped;
        return nullptr;
    }
    pc_hud_classic_cmd* c = &classic_buffer[classic_used++];
    *c = pc_hud_classic_cmd();
    c->type = type;
    c->alpha = 1;
    c->layer = classic_layer;
    return c;
}
int pixel(float v) { return (int)std::floor(v + 0.5f); }

void need_draw(lua_State* L, const char* fn)
{
    if (!in_draw)
        luaL_error(L, "%s: only inside a HUD draw function (rood.hud.claim) or a panel (rood.draw.panel)", fn);
}
// A primitive the PS1 packets have and the modern renderer does not.
void need_classic(lua_State* L, const char* fn)
{
    need_draw(L, fn);
    if (!classic_pass)
        luaL_error(L, "%s: the classic flavour only (a draw function's e.flavour == \"classic\": the classic look "
                      "with hud_classic_override)", fn);
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

// One text command: style at 1, the string at 2, then x, y (and with fit w, h), colour, alpha.
int text_command(lua_State* L, const char* fn, bool fit)
{
    const StyleSlot& s = slot_of(L, 1, fn);
    size_t len;
    const char* text = luaL_checklstring(L, 2, &len);
    float x = coordinate(L, 3, fn), y = coordinate(L, 4, fn), w = 0, h = 0;
    int at = 5;
    if (fit) {
        w = coordinate(L, 5, fn), h = coordinate(L, 6, fn);
        at = 7;
    }
    unsigned colour = s.style.color;
    optional_colour(L, at, &colour, fn);
    float alpha = optional_alpha(L, at + 1, s.style.alpha);
    need_draw(L, fn);
    if (len > 255)
        return luaL_error(L, "%s: text longer than 255 bytes", fn);
    if (classic_pass) { // the game's font (its printers; ui_text restyles them)
        pc_hud_classic_cmd* c = classic_arena_used + len + 1 <= sizeof classic_arena ? next_classic(PC_HUD_CMD_TEXT)
                                                                                      : nullptr;
        if (!c) {
            lua_pushboolean(L, 0);
            return 1;
        }
        std::memcpy(classic_arena + classic_arena_used, text, len);
        classic_arena[classic_arena_used + len] = 0;
        c->text = classic_arena + classic_arena_used;
        c->length = (int)len;
        classic_arena_used += (unsigned)len + 1;
        c->x = pixel(x), c->y = pixel(y), c->w = pixel(w), c->h = pixel(h);
        c->fit = fit;
        c->font = s.style.font, c->align = s.style.align, c->anchor = s.style.anchor;
        c->color = colour, c->alpha = alpha;
        lua_pushboolean(L, 1);
        return 1;
    }
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
    c->x = x, c->y = y, c->w = w, c->h = h;
    c->fit = fit;
    c->color = colour, c->alpha = alpha;
    c->text = arena_used, c->length = (unsigned)len;
    arena_used += (unsigned)len + 1;
    lua_pushboolean(L, 1);
    return 1;
}
int api_text(lua_State* L) { return text_command(L, "rood.draw.text", false); }
// d.fit(style, s, x, y, w, h[, color[, alpha]]): inside the box x, y, w, h (its top-left), the
// outline kept in: no larger than the style (its size or cap), smaller to fit the box's height,
// condensed to fit its width (w 0: any); placed by the style's align across, centred down.
int api_fit(lua_State* L) { return text_command(L, "rood.draw.fit", true); }

// ui's prompt icons (button_prompts.h PC_PROMPT_*, in order).
const char* const BUTTONS[] = { "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square", "select", "l3", "r3",
    "start", "up", "right", "down", "left", "travel" };
#define BUTTON_COUNT 17
int button_arg(lua_State* L, int idx, const char* fn)
{
    const char* name = luaL_checkstring(L, idx);
    for (int i = 0; i < BUTTON_COUNT; ++i)
        if (!std::strcmp(name, BUTTONS[i]))
            return i;
    return luaL_error(L, "%s: unknown button '%s' (l2, r2, l1, r1, triangle, circle, cross, square, select, l3, r3, "
                         "start, up, right, down, left, travel)", fn, name);
}

// d.glyph_width(button, h) -> the icon's width h high (0: the device has none for it).
int api_glyph_width(lua_State* L)
{
    const char* fn = "rood.draw.glyph_width";
    int button = button_arg(L, 1, fn);
    float h = coordinate(L, 2, fn);
    if (classic_pass) { // the game's button sprite (its own size)
        lua_pushnumber(L, pc_hud_classic_glyph ? pc_hud_classic_glyph(button) : 0);
        return 1;
    }
    lua_pushnumber(L, hud::glyph_width ? hud::glyph_width(button, h) : h);
    return 1;
}

// d.glyph(button, x, y, h[, style[, color[, alpha]]]) -> its width: the button's prompt icon for the
// device used last, h high, its left at x, its middle at y; the style's stroke and stroke_color
// outline it; color tints it. 0 (nothing drawn) when that device has no icon for it.
int api_glyph(lua_State* L)
{
    const char* fn = "rood.draw.glyph";
    int button = button_arg(L, 1, fn);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    int style = HUD_NO_STYLE;
    float alpha = 1;
    if (!lua_isnoneornil(L, 5)) {
        const StyleSlot& s = slot_of(L, 5, fn);
        style = (int)(&s - style_slots);
        alpha = s.style.alpha;
    }
    unsigned colour = 0xFFFFFF;
    optional_colour(L, 6, &colour, fn);
    alpha = optional_alpha(L, 7, alpha);
    need_draw(L, fn);
    if (classic_pass) { // the game's button sprite, its middle at y (its own size; colour tints it)
        int gw = pc_hud_classic_glyph ? pc_hud_classic_glyph(button) : 0;
        pc_hud_classic_cmd* c = gw > 0 ? next_classic(PC_HUD_CMD_GLYPH) : nullptr;
        if (c) {
            c->x = pixel(x), c->y = pixel(y), c->w = gw, c->h = gw;
            c->a = button;
            c->color = colour, c->alpha = alpha;
        }
        lua_pushnumber(L, gw);
        return 1;
    }
    float w = hud::glyph_width ? hud::glyph_width(button, h) : h;
    if (w <= 0) {
        lua_pushnumber(L, 0);
        return 1;
    }
    Command* c = next_command();
    if (c) {
        *c = Command();
        c->type = hud::CMD_GLYPH;
        c->anchor = (unsigned char)drawing.anchor;
        c->style = (unsigned short)style;
        c->button = button;
        c->x = x, c->y = y, c->w = w, c->h = h;
        c->color = colour, c->alpha = alpha;
    }
    lua_pushnumber(L, w);
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
    if (classic_pass) { // a tile (alpha below 1: the PS1's half blend)
        pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_RECT);
        if (k) {
            k->x = pixel(x), k->y = pixel(y), k->w = pixel(w), k->h = pixel(h);
            k->color = colour, k->alpha = alpha;
        }
        lua_pushboolean(L, k != nullptr);
        return 1;
    }
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

// d.gradient(x, y, w, h, color, color2[, vertical[, alpha]]): color at the left (the top when vertical)
// to color2 at the right (the bottom). Both flavours (classic: a Gouraud quad).
int api_gradient(lua_State* L)
{
    const char* fn = "rood.draw.gradient";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn), w = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    unsigned a = 0, b = 0;
    if (!colour_at(L, 5, &a) || !colour_at(L, 6, &b))
        return luaL_error(L, "%s: arguments 5 and 6 must be 0xRRGGBB or \"#rrggbb\"", fn);
    bool vertical = lua_toboolean(L, 7);
    float alpha = optional_alpha(L, 8, 1);
    need_draw(L, fn);
    if (classic_pass) {
        pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_GRADIENT);
        if (k) {
            k->x = pixel(x), k->y = pixel(y), k->w = pixel(w), k->h = pixel(h);
            k->color = a, k->color2 = b, k->alpha = alpha, k->a = vertical;
        }
        lua_pushboolean(L, k != nullptr);
        return 1;
    }
    Command* c = next_command();
    if (c) {
        *c = Command();
        c->type = hud::CMD_GRADIENT;
        c->anchor = (unsigned char)drawing.anchor;
        c->x = x, c->y = y, c->w = w, c->h = h;
        c->color = a, c->color2 = b, c->alpha = alpha, c->vertical = vertical;
    }
    lua_pushboolean(L, c != nullptr);
    return 1;
}

int int_field(lua_State* L, int t, const char* k, int lo, int hi, const char* fn)
{
    lua_getfield(L, t, k);
    if (!lua_isinteger(L, -1))
        luaL_error(L, "%s: the sprite's '%s' must be an integer", fn, k);
    lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (v < lo || v > hi)
        luaL_error(L, "%s: the sprite's '%s' %d is outside %d..%d", fn, k, (int)v, lo, hi);
    return (int)v;
}

// d.sprite(x, y, {page_x, page_y, bits, u, v, w, h, clut_x, clut_y}[, color[, alpha]]): a rectangle of
// VRAM as the game draws its sprites (classic only): the texture page at page_x (a multiple of 64),
// page_y (0 or 256), bits 4, 8 or 15, the texels u, v (in the page) w x h, the CLUT at clut_x (a
// multiple of 16), clut_y (4 and 8 bits). color modulates it (white: as it is). abr (0..3): the page's
// blending (the game's setTPage), for a semi-transparent sprite (alpha below 1).
int api_sprite(lua_State* L)
{
    const char* fn = "rood.draw.sprite";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn);
    luaL_checktype(L, 3, LUA_TTABLE);
    int px = int_field(L, 3, "page_x", 0, 1023, fn), py = int_field(L, 3, "page_y", 0, 256, fn);
    int bits = int_field(L, 3, "bits", 4, 15, fn);
    int u = int_field(L, 3, "u", 0, 255, fn), v = int_field(L, 3, "v", 0, 255, fn);
    int w = int_field(L, 3, "w", 0, 256, fn), h = int_field(L, 3, "h", 0, 256, fn);
    int cx = 0, cy = 0;
    if (bits != 15)
        cx = int_field(L, 3, "clut_x", 0, 1023, fn), cy = int_field(L, 3, "clut_y", 0, 511, fn);
    if (px % 64 || (py != 0 && py != 256) || (bits != 4 && bits != 8 && bits != 15) || cx % 16)
        return luaL_error(L, "%s: page_x a multiple of 64, page_y 0 or 256, bits 4, 8 or 15, clut_x a multiple of 16",
            fn);
    unsigned colour = 0xFFFFFF;
    optional_colour(L, 4, &colour, fn);
    float alpha = optional_alpha(L, 5, 1);
    need_classic(L, fn);
    lua_getfield(L, 3, "bgr");
    bool raw = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (raw)
        colour = (unsigned)int_field(L, 3, "bgr", 0, 0xFFFFFF, fn);
    lua_getfield(L, 3, "set_page");
    if (!lua_isnil(L, -1) && !lua_isboolean(L, -1))
        return luaL_error(L, "%s: set_page must be boolean", fn);
    bool inherit_page = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "abr");
    int abr = lua_isnil(L, -1) ? 0 : int_field(L, 3, "abr", 0, 3, fn);
    lua_pop(L, 1);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_SPRITE);
    if (k) {
        k->x = pixel(x), k->y = pixel(y), k->w = w, k->h = h;
        k->a = (px / 64) | (py / 256) << 4 | abr << 5 | (bits == 4 ? 0 : bits == 8 ? 1 : 2) << 7; // the page word (GP0 E1)
        k->b = (cx / 16) | cy << 6;                                                     // the CLUT word
        k->c = u, k->d = v;
        k->font = raw;
        k->align = inherit_page;
        k->color = colour, k->alpha = alpha;
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.quad(x, y, w, h, {page_x, page_y, bits, u, v, w, h, clut_x, clut_y, bgr?, raw?, abr?}[, alpha]): a textured
// four-point polygon as the label loop's draws build theirs (classic only; POLY_FT4: the numbers over the actors,
// the buffs): x, y its top-left and w x h on the screen, the texels u, v w x h in the page (their own size:
// a quad may squash them), bgr the colour word (PS1 BGR, 0x80 full; default 0x808080), raw true: the texture
// unmodulated (setShadeTex 1), abr the page's blending (0..3, the game's setTPage), alpha below 1
// semi-transparent. corners = {x0, y0, x1, y1, x2, y2, x3, y3} (from x, y, in the packet's vertex order: top
// left, top right, bottom left, bottom right of the texels) with uvs = {u0, v0, .. u3, v3}: each corner and its
// texel given (a mirrored or turned quad: the emote balloons, the off-screen arrow); w, h are then unused.
int api_quad(lua_State* L)
{
    const char* fn = "rood.draw.quad";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn), w = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    luaL_checktype(L, 5, LUA_TTABLE);
    int px = int_field(L, 5, "page_x", 0, 1023, fn), py = int_field(L, 5, "page_y", 0, 256, fn);
    int bits = int_field(L, 5, "bits", 4, 15, fn);
    lua_getfield(L, 5, "corners");
    bool explicit_corners = !lua_isnil(L, -1);
    lua_pop(L, 1);
    auto texel = [&](const char* k, int hi) { // (with corners the texel rectangle is unused: 0 when left out)
        lua_getfield(L, 5, k);
        bool none = lua_isnil(L, -1);
        lua_pop(L, 1);
        return none && explicit_corners ? 0 : int_field(L, 5, k, 0, hi, fn);
    };
    int u = texel("u", 255), v = texel("v", 255), tw = texel("w", 256), th = texel("h", 256);
    int cx = 0, cy = 0, abr = 0;
    unsigned colour = 0x808080;
    if (bits != 15)
        cx = int_field(L, 5, "clut_x", 0, 1023, fn), cy = int_field(L, 5, "clut_y", 0, 511, fn);
    if (px % 64 || (py != 0 && py != 256) || (bits != 4 && bits != 8 && bits != 15) || cx % 16)
        return luaL_error(L, "%s: page_x a multiple of 64, page_y 0 or 256, bits 4, 8 or 15, clut_x a multiple of 16",
            fn);
    lua_getfield(L, 5, "bgr");
    bool has_bgr = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (has_bgr)
        colour = (unsigned)int_field(L, 5, "bgr", 0, 0xFFFFFF, fn);
    lua_getfield(L, 5, "abr");
    bool has_abr = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (has_abr)
        abr = int_field(L, 5, "abr", 0, 3, fn);
    lua_getfield(L, 5, "raw");
    if (!lua_isnil(L, -1) && !lua_isboolean(L, -1))
        return luaL_error(L, "%s: raw must be boolean", fn);
    bool raw = lua_toboolean(L, -1);
    lua_pop(L, 1);
    short corner[8] = {};
    unsigned char uv[8] = {};
    lua_getfield(L, 5, "corners");
    if (explicit_corners) {
        luaL_checktype(L, -1, LUA_TTABLE);
        lua_getfield(L, 5, "uvs");
        if (!lua_istable(L, -1))
            return luaL_error(L, "%s: corners need uvs = {u0, v0, .. u3, v3}", fn);
        for (int i = 0; i < 8; ++i) {
            lua_rawgeti(L, -2, i + 1);
            lua_rawgeti(L, -2, i + 1);
            if (!lua_isinteger(L, -2) || !lua_isinteger(L, -1))
                return luaL_error(L, "%s: corners and uvs are 8 integers each", fn);
            lua_Integer c = lua_tointeger(L, -2), t = lua_tointeger(L, -1);
            if (c < -1024 || c > 1024 || t < 0 || t > 255)
                return luaL_error(L, "%s: a corner -1024..1024 from x, y; a texel 0..255", fn);
            corner[i] = (short)c, uv[i] = (unsigned char)t;
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    float alpha = optional_alpha(L, 6, 1);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_QUAD);
    if (k) {
        k->fit = explicit_corners;
        std::memcpy(k->corner, corner, sizeof corner);
        std::memcpy(k->uv, uv, sizeof uv);
        k->x = pixel(x), k->y = pixel(y), k->w = pixel(w), k->h = pixel(h);
        k->a = (px / 64) | (py / 256) << 4 | abr << 5 | (bits == 4 ? 0 : bits == 8 ? 1 : 2) << 7; // setTPage's word
        k->b = (cx / 16) | cy << 6;                                                                 // setClut's
        k->c = u, k->d = v, k->font = tw, k->align = th;
        k->anchor = raw;
        k->color = colour, k->alpha = alpha;
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.frame(x, y, w, h, {box, shape, flip?, tail?}): classic only: a text box's frame drawn by the game's own routine
// (func_800CD3E4) on a copy of box `box` (0..7: its entries of the text boxes' ordering table): x, y its top-left,
// w x h, shape (the frames' table: 0..15), flip (0..3: mirrored across, down), tail (the speaker's part, 0..15).
int api_frame(lua_State* L)
{
    const char* fn = "rood.draw.frame";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn), w = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    luaL_checktype(L, 5, LUA_TTABLE);
    int box = int_field(L, 5, "box", 0, 7, fn), shape = int_field(L, 5, "shape", 0, 15, fn);
    auto optional = [&](const char* k, int hi) {
        lua_getfield(L, 5, k);
        bool none = lua_isnil(L, -1);
        lua_pop(L, 1);
        return none ? 0 : int_field(L, 5, k, 0, hi, fn);
    };
    int flip = optional("flip", 3), tail = optional("tail", 15);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_FRAME);
    if (k) {
        k->x = pixel(x), k->y = pixel(y), k->w = pixel(w), k->h = pixel(h);
        k->a = box, k->b = shape, k->c = flip, k->d = tail;
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.band(box, {top, edge, lines, settled?, icon?}): classic only: a text box's band across the screen drawn by the
// game's own routine (func_800CD158: the name window's) on a copy of box `box` (0..7: its ordering-table entry):
// top (true: from the screen's top), edge (the rows it has grown, 0..240), lines (its text's), settled (open: the
// icon shown, at the bottom), icon (the box's icon field, 0..15).
int api_band(lua_State* L)
{
    const char* fn = "rood.draw.band";
    lua_Integer box = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (box < 0 || box > 7)
        return luaL_error(L, "%s: box 0..7", fn);
    lua_getfield(L, 2, "top");
    bool top = lua_toboolean(L, -1);
    lua_getfield(L, 2, "settled");
    bool settled = lua_toboolean(L, -1);
    lua_pop(L, 2);
    int edge = int_field(L, 2, "edge", 0, 240, fn), lines = int_field(L, 2, "lines", 0, 127, fn);
    lua_getfield(L, 2, "icon");
    bool has_icon = !lua_isnil(L, -1);
    lua_pop(L, 1);
    int icon = has_icon ? int_field(L, 2, "icon", 0, 15, fn) : 0;
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_BAND);
    if (k)
        k->a = (int)box, k->b = top, k->c = edge, k->d = lines, k->font = settled, k->align = icon;
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.char(code, x, y, font, style, brightness): classic only, box_text's: one glyph of the text boxes' font set by
// the game's printer (vs_battle_printVariableWidthFontChar) in the element's box: code as the printer takes it, font
// its table (0, 1), style its colour style (0..7), brightness its sprites' (0x80 full, 0x180 half blended).
int api_char(lua_State* L)
{
    const char* fn = "rood.draw.char";
    lua_Integer code = luaL_checkinteger(L, 1);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer table = luaL_checkinteger(L, 4), style = luaL_checkinteger(L, 5), brightness = luaL_checkinteger(L, 6);
    if (code < 0 || code > 0xFF || table < 0 || table > 1 || style < 0 || style > 7 || brightness < 0 || brightness > 0x1FF)
        return luaL_error(L, "%s: code 0..255, font 0 or 1, style 0..7, brightness 0..0x1FF", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_CHAR);
    if (k)
        k->x = pixel(x), k->y = pixel(y), k->a = (int)code, k->b = (int)table, k->c = (int)style, k->d = (int)brightness;
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.image(image, drawer, clut): classic only (image's): a portrait or still drawn by the game's own drawer
// (D_800EC25C[drawer], as func_800CCA90 calls it) in the kind's slot: image 0..2, clut its CLUT word.
int api_image(lua_State* L)
{
    const char* fn = "rood.draw.image";
    lua_Integer image = luaL_checkinteger(L, 1), drawer = luaL_checkinteger(L, 2), clut = luaL_checkinteger(L, 3);
    if (image < 0 || image > 2 || drawer < 0 || drawer > 3)
        return luaL_error(L, "%s: image 0..2, drawer 0..3", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_IMAGE);
    if (k)
        k->a = (int)image, k->b = (int)drawer, k->c = (int)clut;
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.menu_row([{x, y, w}]): classic only (menu_row's): the row the element recorded drawn by the game's own emit
// (58EC.c: its window, label, counts, icons and scroll arrow, in vs_scratch.unk8's slots) on a copy of its record;
// x, y (its place) and w (its window's width, 0..255) replace the record's when given.
int api_menu_row(lua_State* L)
{
    const char* fn = "rood.draw.menu_row";
    int set = 0, x = 0, y = 0, w = 0;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        auto field = [&](const char* k, int bit, int lo, int hi, int* out) {
            lua_getfield(L, 1, k);
            bool none = lua_isnil(L, -1);
            lua_pop(L, 1);
            if (!none)
                *out = int_field(L, 1, k, lo, hi, fn), set |= bit;
        };
        field("x", 1, -1024, 1024, &x);
        field("y", 2, -1024, 1024, &y);
        field("w", 4, 0, 255, &w);
    }
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_MENU_ROW);
    if (k)
        k->x = x, k->y = y, k->w = w, k->a = set;
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.option_text(text, x, y, colour[, {room, upper}]) -> pen: classic only (option_value's): our Option pages' value
// text by their own draw (classic_options.c: encoded as the page encodes it, capitals when upper, default true; cut
// to room pixels with "..", 0 none), the menu font in colour 0..3 (0 white, 1 lit, 3 grey); returns the pen after it
// (x plus its width), as the page's draw returns it.
extern "C" int pc_options_text_width(const char* utf8, int upper, int room) __attribute__((weak));
int api_option_text(lua_State* L)
{
    const char* fn = "rood.draw.option_text";
    size_t len;
    const char* text = luaL_checklstring(L, 1, &len);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer colour = luaL_checkinteger(L, 4);
    int room = 0, upper = 1;
    if (colour < 0 || colour > 3)
        return luaL_error(L, "%s: colour 0..3", fn);
    if (!lua_isnoneornil(L, 5)) {
        luaL_checktype(L, 5, LUA_TTABLE);
        lua_getfield(L, 5, "room");
        if (!lua_isnil(L, -1))
            room = int_field(L, 5, "room", 0, 320, fn);
        lua_getfield(L, 5, "upper");
        if (!lua_isnil(L, -1))
            upper = lua_toboolean(L, -1);
        lua_pop(L, 2);
    }
    if (len > 127)
        return luaL_error(L, "%s: text longer than 127 bytes", fn);
    need_classic(L, fn);
    std::string copy(text, len);
    int width = pc_options_text_width ? pc_options_text_width(copy.c_str(), upper, room) : 0;
    pc_hud_classic_cmd* c = classic_arena_used + len + 1 <= sizeof classic_arena ? next_classic(PC_HUD_CMD_OPTION_TEXT)
                                                                                  : nullptr;
    if (c) {
        std::memcpy(classic_arena + classic_arena_used, text, len);
        classic_arena[classic_arena_used + len] = 0;
        c->text = classic_arena + classic_arena_used;
        c->length = (int)len;
        classic_arena_used += (unsigned)len + 1;
        c->x = pixel(x), c->y = pixel(y), c->w = room, c->a = upper;
        c->color = (unsigned)colour;
    }
    lua_pushinteger(L, pixel(x) + width);
    return 1;
}

// d.option_width(text[, {room, upper}]) -> pixels: the advance d.option_text would give the same text (nothing drawn).
int api_option_width(lua_State* L)
{
    const char* fn = "rood.draw.option_width";
    std::string text = luaL_checkstring(L, 1);
    int room = 0, upper = 1;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_getfield(L, 2, "room");
        if (!lua_isnil(L, -1))
            room = int_field(L, 2, "room", 0, 320, fn);
        lua_getfield(L, 2, "upper");
        if (!lua_isnil(L, -1))
            upper = lua_toboolean(L, -1);
        lua_pop(L, 2);
    }
    lua_pushinteger(L, pc_options_text_width ? pc_options_text_width(text.c_str(), upper, room) : 0);
    return 1;
}

// d.option_bar(x, y, fill, colour): classic only (option_value's): a slider's gauge as our Option pages draw it (the
// HP / MP / RISK bar, vs_battle_renderStatBar: colour 0..3, fill 0..64 pixels) at x, y + 4.
int api_option_bar(lua_State* L)
{
    const char* fn = "rood.draw.option_bar";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn);
    lua_Integer fill = luaL_checkinteger(L, 3), colour = luaL_checkinteger(L, 4);
    if (fill < 0 || fill > 64 || colour < 0 || colour > 3)
        return luaL_error(L, "%s: fill 0..64, colour 0..3", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_OPTION_BAR);
    if (c)
        c->x = pixel(x), c->y = pixel(y), c->a = (int)colour, c->b = (int)fill;
    lua_pushboolean(L, c != nullptr);
    return 1;
}

// d.menu_int(value, x, y, word) -> x, y: classic only: a number by MAINMENU's printer (vs_mainMenu_renderIntColor:
// its digits right to left from x, each 8 pixels, then a '-' for a negative one; word the PS1 colour word,
// 0xBBGGRR, 0x80 full); returns the pen the game's call returns (left of its last digit, before the sign).
uint32_t packed_xy(lua_Integer x, lua_Integer y);
void push_xy(lua_State* L, uint32_t xy);
int api_menu_int(lua_State* L)
{
    const char* fn = "rood.draw.menu_int";
    lua_Integer value = luaL_checkinteger(L, 1);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer word = luaL_checkinteger(L, 4);
    if (value < -99999999 || value > 99999999 || word < 0 || word > 0xFFFFFF)
        return luaL_error(L, "%s: value -99999999..99999999, the colour word 0..0xFFFFFF", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_MENU_INT);
    if (c)
        c->x = pixel(x), c->y = pixel(y), c->a = (int)value, c->color = (unsigned)word;
    uint32_t xy = packed_xy(pixel(x), pixel(y));
    lua_Integer v = value < 0 ? -value : value;
    do { // (one digit per decimal digit, each 8 pixels left)
        xy -= 8;
        v /= 10;
    } while (v);
    push_xy(L, xy);
    return 2;
}

// d.stat_bar(x, y, fill, colour): classic only: the game's gauge (vs_battle_renderStatBar: the HP / MP / RISK bars,
// the DP / PP bars; colour 0..3 its table's, fill its width in pixels, 0..64 within the frame) at x, y.
int api_stat_bar(lua_State* L)
{
    const char* fn = "rood.draw.stat_bar";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn);
    lua_Integer fill = luaL_checkinteger(L, 3), colour = luaL_checkinteger(L, 4);
    if (fill < 0 || fill > 4096 || colour < 0 || colour > 3)
        return luaL_error(L, "%s: fill 0..4096, colour 0..3", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_STAT_BAR);
    if (c)
        c->x = pixel(x), c->y = pixel(y), c->a = (int)colour, c->b = (int)fill;
    lua_pushboolean(L, c != nullptr);
    return 1;
}

// d.packet(w1[, w2 .. w15]): classic only: one primitive of the game's own words (1..15 of them, after its tag,
// which links it into the layer's slot as vs_battle_addTile and vs_battle_insertTpage link theirs): a tile
// (code and colour, place, size), a draw mode word, a shaded line (colour, place, colour, place), a shaded
// four-point polygon with its draw mode (the rename grid's: mode, colour, place, colour, place, ...); in an
// immediate draw (MENU7's file menu) drawn at once, as DrawPrim draws one (a tile and a polygon in one tag: 12).
int api_packet(lua_State* L)
{
    const char* fn = "rood.draw.packet";
    int n = lua_gettop(L);
    if (n < 1 || n > 15)
        return luaL_error(L, "%s: 1..15 words", fn);
    uint32_t w[15] = { 0 };
    for (int i = 0; i < n; ++i) {
        lua_Integer v = luaL_checkinteger(L, i + 1);
        if (v < -0x80000000LL || v > 0xFFFFFFFFLL)
            return luaL_error(L, "%s: word %d is not 32 bits", fn, i + 1);
        w[i] = (uint32_t)v;
    }
    need_classic(L, fn);
    // (words 5.. in the arena, aligned)
    unsigned at = (classic_arena_used + 3u) & ~3u, extra = n > 4 ? (unsigned)(n - 4) * 4u : 0u;
    pc_hud_classic_cmd* c = !extra || at + extra <= sizeof classic_arena ? next_classic(PC_HUD_CMD_PACKET) : nullptr;
    if (c) {
        c->w = n;
        c->a = (int)w[0], c->b = (int)w[1], c->c = (int)w[2], c->d = (int)w[3];
        if (extra) {
            std::memcpy(classic_arena + at, w + 4, extra);
            c->text = classic_arena + at;
            classic_arena_used = at + extra;
        }
    }
    lua_pushboolean(L, c != nullptr);
    return 1;
}

// d.image_store(x, y, w, h): classic only: the VRAM rectangle (w * h at most 1024 pixels) read into the draw's own
// buffer (StoreImage), for d.image_restore. d.image_load(x, y, w, h, pixels): the rectangle uploaded (LoadImage):
// pixels a string of w * h 16-bit pixels (2 bytes each, little-endian: a record's CLUT, e.file.cluts). d.image_restore():
// the last rectangle read put back. In an immediate draw (MENU7's file menu) each is done in its place among the
// packets (the game's CLUT swaps: read, upload, the sprite, put back); elsewhere at once, as the draw is made.
int api_image_store(lua_State* L)
{
    const char* fn = "rood.draw.image_store";
    lua_Integer x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2), w = luaL_checkinteger(L, 3),
                h = luaL_checkinteger(L, 4);
    if (x < 0 || y < 0 || x + w > 1024 || y + h > 512 || w < 1 || h < 1 || w * h > 1024)
        return luaL_error(L, "%s: a rectangle in VRAM (1024 x 512) of 1..1024 pixels", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_IMAGE_STORE);
    if (c)
        c->x = (int)x, c->y = (int)y, c->w = (int)w, c->h = (int)h;
    lua_pushboolean(L, c != nullptr);
    return 1;
}
int api_image_load(lua_State* L)
{
    const char* fn = "rood.draw.image_load";
    lua_Integer x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2), w = luaL_checkinteger(L, 3),
                h = luaL_checkinteger(L, 4);
    size_t size = 0;
    const char* pixels = luaL_checklstring(L, 5, &size);
    if (x < 0 || y < 0 || x + w > 1024 || y + h > 512 || w < 1 || h < 1 || w * h > 1024)
        return luaL_error(L, "%s: a rectangle in VRAM (1024 x 512) of 1..1024 pixels", fn);
    if (size != (size_t)(w * h * 2))
        return luaL_error(L, "%s: %d bytes of pixels for %d x %d (2 a pixel)", fn, (int)size, (int)w, (int)h);
    need_classic(L, fn);
    unsigned at = (classic_arena_used + 3u) & ~3u;
    pc_hud_classic_cmd* c = at + size <= sizeof classic_arena ? next_classic(PC_HUD_CMD_IMAGE_LOAD) : nullptr;
    if (c) {
        std::memcpy(classic_arena + at, pixels, size);
        c->text = classic_arena + at;
        classic_arena_used = at + (unsigned)size;
        c->x = (int)x, c->y = (int)y, c->w = (int)w, c->h = (int)h;
    }
    lua_pushboolean(L, c != nullptr);
    return 1;
}
int api_image_restore(lua_State* L)
{
    need_classic(L, "rood.draw.image_restore");
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_IMAGE_LOAD);
    if (c)
        c->b = 1;
    lua_pushboolean(L, c != nullptr);
    return 1;
}
// d.text_glyph(code, x, y, advance): classic only, immediate draws (MENU7's file menu): the next packet is a printer's
// glyph `code` (the menu charset's) set at x, y with the printer's advance: reported to ui_text (its line layout, the
// button roles' glyph) as the game's printer reports its own; code -1: a space (the pen moved, no packet).
int api_text_glyph(lua_State* L)
{
    const char* fn = "rood.draw.text_glyph";
    lua_Integer code = luaL_checkinteger(L, 1), advance = luaL_checkinteger(L, 4);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    if (code < -1 || code > 0xFF || advance < 0 || advance > 64)
        return luaL_error(L, "%s: code -1..255, advance 0..64", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_TEXT_GLYPH);
    if (c)
        c->a = (int)code, c->x = pixel(x), c->y = pixel(y), c->b = (int)advance;
    lua_pushboolean(L, c != nullptr);
    return 1;
}

// d.font_text(s, x, y, style) -> x: classic only: s (UTF-8, the menu charset: vs_encode) in the text boxes' font by
// the game's printer (vs_battle_printVariableWidthFontChar, after vs_battle_setFontStyle(style): table 0, its
// brightness 384), from x along y; returns the pen after it (each glyph's width, the game's table).
extern "C" int vs_encode(const char* utf8, unsigned char* out, int cap, int* bad);
extern "C" char vs_battle_characterWidths[];
int api_font_text(lua_State* L)
{
    const char* fn = "rood.draw.font_text";
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer style = luaL_checkinteger(L, 4);
    lua_Integer step = luaL_optinteger(L, 5, 0);
    if (style < 0 || style > 15)
        return luaL_error(L, "%s: style 0..15", fn);
    if (step < 0 || step > 64)
        return luaL_error(L, "%s: step 0..64", fn);
    need_classic(L, fn);
    unsigned char glyphs[64];
    int n;
    if (lua_istable(L, 1)) { // the game's codes as given (a record's words: the rename grid's)
        n = (int)lua_rawlen(L, 1);
        if (n > (int)sizeof glyphs)
            return luaL_error(L, "%s: at most %d codes", fn, (int)sizeof glyphs);
        for (int i = 0; i < n; ++i) {
            lua_rawgeti(L, 1, i + 1);
            lua_Integer g = luaL_checkinteger(L, -1);
            lua_pop(L, 1);
            if (g < 0 || g > 0xE6)
                return luaL_error(L, "%s: code %d is not a glyph (0..0xE6)", fn, i + 1);
            glyphs[i] = (unsigned char)g;
        }
    } else {
        n = vs_encode(luaL_checkstring(L, 1), glyphs, sizeof glyphs, nullptr) - 1; // (without its 0xE7)
        if (n < 0)
            n = 0;
    }
    int pen = pixel(x);
    for (int i = 0; i < n; ++i)
        pen += step ? (int)step : glyphs[i] == ' ' ? 6 : vs_battle_characterWidths[glyphs[i]];
    pc_hud_classic_cmd* c = classic_arena_used + n + 1 <= sizeof classic_arena ? next_classic(PC_HUD_CMD_FONT_TEXT)
                                                                                : nullptr;
    if (c) {
        std::memcpy(classic_arena + classic_arena_used, glyphs, (size_t)n);
        classic_arena[classic_arena_used + n] = 0;
        c->text = classic_arena + classic_arena_used;
        c->length = n;
        classic_arena_used += (unsigned)n + 1;
        c->x = pixel(x), c->y = pixel(y), c->a = (int)style, c->b = (int)step;
    }
    lua_pushinteger(L, pen);
    return 1;
}

// d.font_width(s, x) -> x: the pen d.font_text would leave from x (nothing drawn).
int api_font_width(lua_State* L)
{
    const char* fn = "rood.draw.font_width";
    const char* text = luaL_checkstring(L, 1);
    int pen = pixel(coordinate(L, 2, fn));
    unsigned char glyphs[64];
    int n = vs_encode(text, glyphs, sizeof glyphs, nullptr) - 1;
    for (int i = 0; i < n; ++i)
        pen += glyphs[i] == ' ' ? 6 : vs_battle_characterWidths[glyphs[i]];
    lua_pushinteger(L, pen);
    return 1;
}

// d.button_background(x, y, w, h): classic only: a button's window as MAINMENU draws it behind a button and its word
// (vs_mainmenu_renderButtonBackground: h shaded lines, the i-th from x to x + w - i, dark gold to near black, each
// semi-transparent; the Items screen's STATUS strip), at x, y. In the kind's slot unless d.layer names another (the
// game's: vs_scratch.unk4 - 12).
int api_button_background(lua_State* L)
{
    const char* fn = "rood.draw.button_background";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn);
    lua_Integer w = luaL_checkinteger(L, 3), h = luaL_checkinteger(L, 4);
    if (w < 0 || w > 320 || h < 0 || h > 64)
        return luaL_error(L, "%s: w 0..320, h 0..64", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* c = next_classic(PC_HUD_CMD_BUTTON_BACKGROUND);
    if (c)
        c->x = pixel(x), c->y = pixel(y), c->w = (int)w, c->h = (int)h;
    lua_pushboolean(L, c != nullptr);
    return 1;
}

// d.plate(x, y, w, h, left, right, shadow_x, shadow_y, shadow_w, shadow_h): classic only (target_row's): a target
// row's plate as func_800C1DC4 builds it, one packet: a black tile (shadow_x, shadow_y, shadow_w x shadow_h), then a
// gradient across x, y, w x h from the colour word left to right (PS1 BGR, 0x80 full), dithered.
int api_plate(lua_State* L)
{
    const char* fn = "rood.draw.plate";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn), w = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    lua_Integer left = luaL_checkinteger(L, 5), right = luaL_checkinteger(L, 6);
    float sx = coordinate(L, 7, fn), sy = coordinate(L, 8, fn), sw = coordinate(L, 9, fn), sh = coordinate(L, 10, fn);
    if (left < 0 || left > 0xFFFFFF || right < 0 || right > 0xFFFFFF)
        return luaL_error(L, "%s: the colours are PS1 colour words (0..0xFFFFFF)", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_PLATE);
    if (k) {
        k->x = pixel(x), k->y = pixel(y), k->w = pixel(w), k->h = pixel(h);
        k->color = (unsigned)left, k->color2 = (unsigned)right;
        k->a = pixel(sx), k->b = pixel(sy), k->c = pixel(sw), k->d = pixel(sh);
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.leader(x0, y0, x1, y1, count, left): classic only (target_line's): a target row's leader line by the game's own
// routine (func_800C16FC; left true: func_800C1A40, the left half's) as func_800C1DC4 calls it: from x0, y0 (the
// part's point) to x1, y1 (the row), count its sweep (the segments lit, their fade); in the kind's slot.
int api_leader(lua_State* L)
{
    const char* fn = "rood.draw.leader";
    float x0 = coordinate(L, 1, fn), y0 = coordinate(L, 2, fn), x1 = coordinate(L, 3, fn), y1 = coordinate(L, 4, fn);
    lua_Integer count = luaL_checkinteger(L, 5);
    bool left = lua_toboolean(L, 6);
    if (count < -64 || count > 64)
        return luaL_error(L, "%s: count -64..64", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_LEADER);
    if (k)
        k->x = pixel(x0), k->y = pixel(y0), k->c = pixel(x1), k->d = pixel(y1), k->a = (int)count, k->b = left;
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.window(x, y, w, h[, brightness]): the game's text box window (its frame, body and shadow; classic
// only); brightness 1 as a selected box, 0.5 as the others.
int api_window(lua_State* L)
{
    const char* fn = "rood.draw.window";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn), w = coordinate(L, 3, fn), h = coordinate(L, 4, fn);
    float brightness = optional_alpha(L, 5, 1);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = w >= 4 && h >= 4 ? next_classic(PC_HUD_CMD_WINDOW) : nullptr;
    if (k) {
        k->x = pixel(x), k->y = pixel(y), k->w = pixel(w), k->h = pixel(h);
        k->a = (int)(brightness * 128.0f + 0.5f);
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.cursor(x, y[, step]): the menus' pointing hand, its top-left at x, y (classic only), at its brightness
// animation's step (0..15; default: the game's frame count's).
int api_cursor(lua_State* L)
{
    const char* fn = "rood.draw.cursor";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn);
    int step = lua_isnoneornil(L, 3) ? -1 : (int)luaL_checkinteger(L, 3) & 15;
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_CURSOR);
    if (k) {
        k->x = pixel(x), k->y = pixel(y), k->w = k->h = 16;
        k->a = step;
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.bar(x, y, value, max[, pool]): the status display's bar (pool "hp", "mp" or "risk"), its top-left at
// x, y, filled value / max (classic only).
int api_bar(lua_State* L)
{
    const char* fn = "rood.draw.bar";
    float x = coordinate(L, 1, fn), y = coordinate(L, 2, fn);
    lua_Integer value = luaL_checkinteger(L, 3), max = luaL_checkinteger(L, 4);
    static const char* const pools[] = { "hp", "mp", "risk", nullptr };
    int pool = luaL_checkoption(L, 5, "hp", pools);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_BAR);
    if (k) {
        k->x = pixel(x), k->y = pixel(y), k->w = 66, k->h = 5;
        k->a = (int)std::max<lua_Integer>(0, std::min<lua_Integer>(value, 1 << 20));
        k->b = (int)std::max<lua_Integer>(1, std::min<lua_Integer>(max, 1 << 20));
        k->c = pool;
    }
    lua_pushboolean(L, k != nullptr);
    return 1;
}

void estimate(const Style& st, const char*, size_t len, float* w, float* h)
{
    float size = st.cap > 0 ? st.cap / 0.7f : st.size;
    *w = (float)len * size * (0.55f + st.tracking) * st.stretch;
    *h = size;
}

int api_measure(lua_State* L)
{
    const StyleSlot& s = slot_of(L, 1, "rood.draw.measure");
    size_t len;
    const char* text = luaL_checklstring(L, 2, &len);
    float w = 0, h = 0;
    if (classic_pass && pc_hud_classic_measure) { // the game's font
        int iw = 0, ih = 0;
        pc_hud_classic_measure(s.style.font, text, (int)len, &iw, &ih);
        w = (float)iw, h = (float)ih;
    } else {
        (hud::measure ? hud::measure : estimate)(s.style, text, len, &w, &h);
    }
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

// The typography a classic draw hands ui_text with its packets (typeset.h): the table at idx.
void typeset_arg(lua_State* L, int idx, pc_hud_classic_cmd* c, const char* fn)
{
    c->font = 2; // (the info style's capitals, restyled as the info style)
    if (lua_isnoneornil(L, idx))
        return;
    luaL_checktype(L, idx, LUA_TTABLE);
    pc_text_typeset& t = c->typeset;
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        const char* k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "";
        auto num = [&](float lo, float hi) {
            if (lua_type(L, -1) != LUA_TNUMBER)
                luaL_error(L, "%s: typeset.%s must be a number", fn, k);
            double v = lua_tonumber(L, -1);
            if (!(v >= lo && v <= hi))
                luaL_error(L, "%s: typeset.%s is outside its range", fn, k);
            return (float)v;
        };
        if (!std::strcmp(k, "style")) {
            const char* v = lua_tostring(L, -1);
            if (!v || std::strcmp(v, "status"))
                luaL_error(L, "%s: typeset.style must be \"status\" (the status words' layout)", fn);
            c->font = 4;
        } else if (!std::strcmp(k, "blanks")) {
            const char* v = lua_tostring(L, -1);
            if (!v || (std::strcmp(v, "one") && std::strcmp(v, "each")))
                luaL_error(L, "%s: typeset.blanks must be \"one\" (a run of blanks is one space) or \"each\"", fn);
            t.blanks = !std::strcmp(v, "one");
            t.set |= PC_TYPESET_BLANKS;
        } else if (!std::strcmp(k, "keep_on_screen")) {
            if (!lua_isboolean(L, -1))
                luaL_error(L, "%s: typeset.keep_on_screen must be a boolean", fn);
            t.keep_on_screen = lua_toboolean(L, -1);
            t.set |= PC_TYPESET_KEEP;
        } else if (!std::strcmp(k, "screen_w")) {
            t.screen_w = num(16, 4096);
            t.set |= PC_TYPESET_SCREEN_W;
        } else if (!std::strcmp(k, "margin")) {
            t.margin = num(-64, 64);
            t.set |= PC_TYPESET_MARGIN;
        } else if (!std::strcmp(k, "word_space")) {
            t.word_space = num(0, 8);
            t.set |= PC_TYPESET_WORD_SPACE;
        } else {
            luaL_error(L, "%s: unknown typeset field '%s' (style, blanks, keep_on_screen, screen_w, margin, "
                          "word_space)", fn, k);
        }
        lua_pop(L, 1);
    }
    if (c->font != 4 && t.set)
        luaL_error(L, "%s: typeset fields need style = \"status\"", fn);
}

// d.text_raw(s, x, y, word[, typeset]): classic only: the game's raw printer (the status words', RISK ZERO's:
// vs_battle_renderTextRawColor) called as the game calls it: s as given ('$' centres it on x, '#'
// right-aligns, its own blanks), word the PS1 colour word (0xBBGGRR, 0x80 full); typeset (typeset.h): how
// ui_text sets it ({style = "status", blanks, keep_on_screen, screen_w, margin, word_space}).
int api_text_raw(lua_State* L)
{
    const char* fn = "rood.draw.text_raw";
    size_t len;
    const char* text = luaL_checklstring(L, 1, &len);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer word = luaL_checkinteger(L, 4);
    if (word < 0 || word > 0xFFFFFF)
        return luaL_error(L, "%s: the colour word must be 0..0xFFFFFF (0xBBGGRR)", fn);
    need_classic(L, fn);
    if (len > 63)
        return luaL_error(L, "%s: text longer than 63 bytes", fn);
    pc_hud_classic_cmd scratch = pc_hud_classic_cmd();
    typeset_arg(L, 5, &scratch, fn);
    pc_hud_classic_cmd* c = classic_arena_used + len + 1 <= sizeof classic_arena ? next_classic(PC_HUD_CMD_TEXT_RAW)
                                                                                  : nullptr;
    if (!c) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::memcpy(classic_arena + classic_arena_used, text, len);
    classic_arena[classic_arena_used + len] = 0;
    c->text = classic_arena + classic_arena_used;
    c->length = (int)len;
    classic_arena_used += (unsigned)len + 1;
    c->x = pixel(x), c->y = pixel(y);
    c->a = (int)word;
    c->font = scratch.font;
    c->typeset = scratch.typeset;
    lua_pushboolean(L, 1);
    return 1;
}

// d.anchor(where): where this draw's commands sit in 16:9 (the classic flavour: their packets' range, placed
// exactly, no heuristics; the modern: the commands that follow): "left", "right", "centre", or "element"
// (the element's own, the default).
int api_anchor(lua_State* L)
{
    const char* fn = "rood.draw.anchor";
    static const char* const names[] = { "centre", "left", "right", "element", "center", "screen", "game", nullptr };
    int i = luaL_checkoption(L, 1, nullptr, names);
    need_draw(L, fn);
    int anchor = i == 6 ? PC_HUD_ANCHOR_GAME : i == 5 ? PC_HUD_ANCHOR_SCREEN : i == 4 ? PC_HUD_ANCHOR_CENTRE : i == 3 ? -1 : i;
    if (anchor == PC_HUD_ANCHOR_SCREEN && classic_pass)
        return luaL_error(L, "%s: \"screen\" is a panel's (the modern pass)", fn);
    if (anchor == PC_HUD_ANCHOR_GAME && !classic_pass)
        return luaL_error(L, "%s: \"game\" is the classic flavour's", fn);
    if (classic_pass)
        classic_anchor = anchor;
    else
        drawing.anchor = anchor < 0 ? drawing.element_anchor : anchor;
    return 0;
}

// d.layer(offset | nil): classic only: the ordering-table slot the commands that follow link into, as the
// game names its slots: bytes from vs_scratch.unk4 (-64..64, a multiple of 4; the status words' -16, the
// gauges' bars -12 and digits -16), "unk8" (vs_scratch.unk8: the menus'), "unk8", n (unk8 + n bytes, -64..64,
// a multiple of 4: a menu row icon's + 4); nil: the kind's own. The slot orders packets as the game's do.
int api_layer(lua_State* L)
{
    const char* fn = "rood.draw.layer";
    int layer = PC_HUD_LAYER_KIND;
    if (lua_type(L, 1) == LUA_TSTRING) {
        if (strcmp(lua_tostring(L, 1), "unk8"))
            return luaL_error(L, "%s: the named slot is 'unk8'", fn);
        layer = PC_HUD_LAYER_UNK8;
        if (!lua_isnoneornil(L, 2)) { // ("unk8", bytes): vs_scratch.unk8 + bytes
            lua_Integer v = luaL_checkinteger(L, 2);
            if (v < -64 || v > 64 || v % 4)
                return luaL_error(L, "%s: unk8's offset is -64..64 bytes, a multiple of 4", fn);
            layer = PC_HUD_LAYER_UNK8_AT((int)v);
        }
    } else if (!lua_isnoneornil(L, 1)) {
        lua_Integer v = luaL_checkinteger(L, 1);
        if (v < -64 || v > 64 || v % 4)
            return luaL_error(L, "%s: a slot is -64..64 bytes from vs_scratch.unk4, a multiple of 4", fn);
        layer = (int)v;
    }
    need_classic(L, fn);
    classic_layer = layer;
    return 0;
}

// The game's packed screen position (x in the low half, y in the high: its arithmetic carries between them).
uint32_t packed_xy(lua_Integer x, lua_Integer y) { return (uint32_t)(x & 0xFFFF) + ((uint32_t)(y & 0xFFFF) << 16); }
void push_xy(lua_State* L, uint32_t xy)
{
    lua_pushinteger(L, (int16_t)(xy & 0xFFFF));
    lua_pushinteger(L, (int16_t)(xy >> 16));
}

// d.value(font, x, y, value) -> x, y: classic only: the HUD's digits (vs_battle_renderValue), the rightmost at
// x, y, leftwards; font 0 the small digits, 1 the large, 2 the separator (value 0: the DP / PP slash); returns
// where the game's call leaves its pen (the
// position left of the last digit, as the game's packed arithmetic gives it).
int api_value(lua_State* L)
{
    const char* fn = "rood.draw.value";
    lua_Integer font = luaL_checkinteger(L, 1);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer value = luaL_checkinteger(L, 4);
    if (font < 0 || font > 2 || value < 0 || value > 99999999)
        return luaL_error(L, "%s: font 0..2, value 0..99999999", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_VALUE);
    if (k)
        k->x = pixel(x), k->y = pixel(y), k->a = (int)font, k->b = (int)value;
    uint32_t xy = packed_xy(pixel(x), pixel(y));
    lua_Integer v = value;
    do { // (vs_battle_renderValue: one digit per decimal digit, each 5 + font pixels left)
        xy -= (uint32_t)(5 + font);
        v /= 10;
    } while (v);
    push_xy(L, xy);
    return 2;
}

// d.digit(font, x, y, digit): classic only: one of the HUD's digits (_renderDigit; font 2: the HP / MP separator).
int api_digit(lua_State* L)
{
    const char* fn = "rood.draw.digit";
    lua_Integer font = luaL_checkinteger(L, 1);
    float x = coordinate(L, 2, fn), y = coordinate(L, 3, fn);
    lua_Integer digit = luaL_checkinteger(L, 4);
    if (font < 0 || font > 2 || digit < 0 || digit > 9)
        return luaL_error(L, "%s: font 0..2, digit 0..9", fn);
    need_classic(L, fn);
    pc_hud_classic_cmd* k = next_classic(PC_HUD_CMD_DIGIT);
    if (k)
        k->x = pixel(x), k->y = pixel(y), k->a = (int)font, k->b = (int)digit;
    lua_pushboolean(L, k != nullptr);
    return 1;
}

// d.xy(x, y, delta) -> x, y: the game's packed position arithmetic: x, y packed as the game packs them (x in
// the low half, y in the high) plus delta, an integer as the game writes its steps (56 | (-8 << 16), or
// 0xFFF9 | (0xFFFE << 16): x's carry or borrow reaches y as in the game), modulo 2^32.
int api_xy(lua_State* L)
{
    uint32_t xy = packed_xy(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2)) + (uint32_t)luaL_checkinteger(L, 3);
    push_xy(L, xy);
    return 2;
}

// ---- rood.hud ----
int kind_arg(lua_State* L, int idx, const char* fn)
{
    const char* name = luaL_checkstring(L, idx);
    int kind = hud::kind_of(name);
    if (!kind)
        return luaL_error(L, "%s: unknown HUD kind '%s' (known: " HUD_KIND_LIST ")", fn, name);
    return kind;
}

// Plugins' own layers over the picture (rood.draw.panel): a function per plugin drawn every presented frame in
// the modern pass, after the HUD and the menus, in either look and whatever rood.ui.hide hides (a photo mode's
// card, a notice); never in a photo (captured before the overlay). Their coordinates: the picture's square
// pixels, 240 high (d.anchor("screen")).
struct Panel {
    Plugin* plugin;
    unsigned generation;
    int ref;
    bool failed;
    double retry_at; // a shipped plugin's failed panel is drawn again from then (fault_cooldown_s)
    bool classic;    // {classic = true}: in the classic look the game's frame draws it, with the game's own pieces
};
std::vector<Panel> panels;
const Panel* panel_job;
void drop_panel(Plugin* p, lua_State* L)
{
    for (auto it = panels.begin(); it != panels.end();)
        if (it->plugin == p) {
            if (L && it->generation == p->generation)
                luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            it = panels.erase(it);
        } else {
            ++it;
        }
}
// rood.draw.panel(fn | nil[, {classic = bool}]): fn(d, view) every presented frame (view: width, height, aspect,
// look, flavour, dt). classic: with the classic look the game's frame draws it in the classic flavour
// (panels_classic_next), the classic primitives and the game's own coordinates (d.window, d.sprite, ...).
int api_panel(lua_State* L)
{
    Plugin* p = plugin_of(L);
    if (!lua_isnoneornil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    bool classic = false;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_getfield(L, 2, "classic");
        classic = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    drop_panel(p, L);
    if (lua_isnoneornil(L, 1))
        return 0;
    lua_pushvalue(L, 1);
    panels.push_back({ p, p->generation, luaL_ref(L, LUA_REGISTRYINDEX), false, 0, classic });
    return 0;
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
    if (!PC_HUD_CLAIMABLE(kind))
        return luaL_error(L, "rood.hud.claim: '%s' is an event (rood.on(\"hud.%s\", fn)): the game draws it",
            hud::kind_name(kind), hud::kind_name(kind));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int flavours = FLAVOUR_BOTH;
    if (!lua_isnoneornil(L, 3)) {
        luaL_checktype(L, 3, LUA_TTABLE);
        lua_getfield(L, 3, "flavour");
        static const char* const names[] = { "modern", "classic", "both", nullptr };
        if (!lua_isnil(L, -1))
            flavours = luaL_checkoption(L, -1, nullptr, names) + 1;
        lua_pop(L, 1);
        if ((kind == PC_HUD_TRAVEL || kind == PC_HUD_NOTICE) && flavours == FLAVOUR_CLASSIC)
            return luaL_error(L, "rood.hud.claim: '%s' has no classic flavour (the port's own: drawn over the picture)",
                hud::kind_name(kind));
    }
    if (PC_HUD_CLASSIC_ONLY(kind) && flavours != FLAVOUR_CLASSIC)
        return luaL_error(L, "rood.hud.claim: '%s' is the classic look's only: { flavour = \"classic\" }", hud::kind_name(kind));
    Plugin* p = plugin_of(L);
    const Claim* had = owner(kind);
    Plugin* before = had ? had->plugin : nullptr;
    drop_claim(kind, p, L);
    lua_settop(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Claim made{ p, p->generation, ref, next_claim++ };
    made.flavours = flavours;
    claims[kind].push_back(made);
    choose(kind);
    const Claim* now = owner(kind);
    if (before && before != p && now && now->plugin == p)
        log(LOG_WARN, p->id(), std::string("takes the HUD kind '") + hud::kind_name(kind) + "' from "
            + before->id() + " (one owner per kind: the last in load order)");
    else if (now && now->plugin != p)
        // (the shipped plugins' own layering, rood.hud under rood.classic, is expected: info)
        log(p->shipped && now->plugin->shipped ? LOG_INFO : LOG_WARN, p->id(), std::string("claims the HUD kind '")
            + hud::kind_name(kind) + "', which " + now->plugin->id() + " keeps (later in load order)");
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

// rood.hud.honoured(kind): whether a claim on it is honoured now (a claim that has not failed;
// the modern look or hud_classic_override; the overlay drawing).
int api_honoured(lua_State* L)
{
    lua_pushboolean(L, pc_hud_honoured(kind_arg(L, 1, "rood.hud.honoured")) != 0);
    return 1;
}

// ---- typography: ui_text's styles overridden for this run (ui_text.h pc_ui_text_override) ----
struct Typography {
    std::string owner; // the plugin whose override stands ("" none)
};
Typography typography_of[8];

// rood.hud.typography(style, {font, size, stretch, slant, embolden, stroke, tracking, ...}): a ui_text
// style's fields (the game's own text drawn from fonts with ui_text font: the classic look's
// status words, labels, ...) for this run, over the settings (never saved); nil or {} gives the
// settings' back. Also ui_text's tunables (word_space, edge_margin, popup_scale, descender_lift: the
// digits' old-style figures risen into the room above them), held through the style named. One owner
// per style, the last to call; dropped when the plugin unloads.
int api_typography(lua_State* L)
{
    const char* fn = "rood.hud.typography";
    const char* name = luaL_checkstring(L, 1);
    if (!pc_ui_text_style_index || !pc_ui_text_override)
        return luaL_error(L, "%s: no ui_text in this build", fn);
    int style = pc_ui_text_style_index(name);
    if (style < 0 || style >= 8)
        return luaL_error(L, "%s: unknown style '%s' (dialogue, menu, info, digits, status, labels)", fn, name);
    Plugin* p = plugin_of(L);
    pc_ui_text_override_clear(style);
    typography_of[style].owner.clear();
    if (lua_isnoneornil(L, 2))
        return 0;
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            return luaL_error(L, "%s: fields are named", fn);
        const char* k = lua_tostring(L, -2);
        char value[300];
        if (lua_type(L, -1) == LUA_TBOOLEAN)
            std::snprintf(value, sizeof value, "%d", lua_toboolean(L, -1));
        else if (lua_type(L, -1) == LUA_TNUMBER)
            std::snprintf(value, sizeof value, "%.9g", (double)lua_tonumber(L, -1));
        else if (lua_type(L, -1) == LUA_TSTRING && !std::strcmp(k, "font"))
            std::snprintf(value, sizeof value, "%s", lua_tostring(L, -1));
        else
            return luaL_error(L, "%s: field '%s' has the wrong type", fn, k);
        if (!pc_ui_text_override(style, k, value)) {
            pc_ui_text_override_clear(style);
            return luaL_error(L, "%s: unknown field '%s' (font, stretch, slant, embolden, aspect, small_caps, "
                                 "outline, size, stroke, tracking; word_space, edge_margin, popup_scale, "
                                 "descender_lift)", fn, k);
        }
        lua_pop(L, 1);
    }
    if (pc_ui_text_overridden(style))
        typography_of[style].owner = p->id();
    return 0;
}

// rood.hud.look() -> "classic" (ui_menus 0: claims draw at the game's hooks, in the classic flavour) or "modern".
int api_look(lua_State* L)
{
    lua_pushstring(L, hud::classic_look() ? "classic" : "modern");
    return 1;
}

// rood.hud.kinds([flavour]): the kinds a claim draws (name_window: an event only) in the modern look (the
// default; a claim for both flavours may take each), or with "classic" those of the classic look (the
// gauges: the classic look's only; not travel or notice, the port's own).
int api_kinds(lua_State* L)
{
    static const char* const flavours[] = { "modern", "classic", nullptr };
    int classic = luaL_checkoption(L, 1, "modern", flavours) == 1;
    lua_createtable(L, PC_HUD_KINDS, 0);
    int n = 0;
    for (int k = 1; k < PC_HUD_KINDS; ++k)
        if (PC_HUD_CLAIMABLE(k) && (classic ? k != PC_HUD_TRAVEL && k != PC_HUD_NOTICE : !PC_HUD_CLASSIC_ONLY(k))) {
            lua_pushstring(L, hud::kind_name(k));
            lua_rawseti(L, -2, ++n);
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

// A name (either case) or an index for slot / stat / pool args.
int word_arg(lua_State* L, const char* (*word)(int), int count)
{
    if (lua_isinteger(L, -1)) {
        lua_Integer i = lua_tointeger(L, -1);
        return i >= 0 && i < count ? (int)i : -1;
    }
    return lua_type(L, -1) == LUA_TSTRING ? hud::word_index(lua_tostring(L, -1), word, count) : -1;
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
        bool ok = true;
        if (!std::strcmp(k, "timing")) {
            int t = lua_type(L, -1) == LUA_TSTRING ? hud::timing_of(lua_tostring(L, -1))
                : lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
            if (t < 0 || t > 4) {
                *why = "timing must be too_fast, fast, success, slow or too_slow";
                return false;
            }
            a->timing = t;
        } else if ((!std::strcmp(k, "variant") || !std::strcmp(k, "message")) && lua_isinteger(L, -1)) {
            a->variant = (int)lua_tointeger(L, -1);
        } else if (!std::strcmp(k, "age") && lua_isinteger(L, -1)) {
            a->age = (int)lua_tointeger(L, -1);
        } else if (!std::strcmp(k, "hold") && lua_isboolean(L, -1)) {
            a->hold = lua_toboolean(L, -1);
        } else if ((!std::strcmp(k, "value") || !std::strcmp(k, "count") || !std::strcmp(k, "index"))
            && lua_isinteger(L, -1)) {
            a->value = (int)lua_tointeger(L, -1);
        } else if (!std::strcmp(k, "sign") && lua_isinteger(L, -1)) {
            a->sign = lua_tointeger(L, -1) < 0 ? -1 : 1;
        } else if (!std::strcmp(k, "heal") && lua_isboolean(L, -1)) {
            a->sign = lua_toboolean(L, -1) ? 1 : -1;
        } else if (!std::strcmp(k, "text") && lua_type(L, -1) == LUA_TSTRING) {
            std::snprintf(a->text, sizeof a->text, "%s", lua_tostring(L, -1));
        } else if (!std::strcmp(k, "pool")) {
            ok = (a->pool = word_arg(L, hud::pool_name, 3)) >= 0;
        } else if (!std::strcmp(k, "stat")) {
            ok = (a->stat = word_arg(L, hud::stat_word, 13)) >= 0;
        } else if (!std::strcmp(k, "slot")) {
            ok = (a->slot = word_arg(L, hud::slot_word, 7)) >= 0;
        } else {
            *why = std::string("unknown or mistyped arg '") + k
                + "' (timing, variant, age, hold, value or count, sign or heal, pool, stat, slot; name_window: index,"
                  " message, text)";
            return false;
        }
        if (!ok) {
            *why = std::string("arg '") + k + "': an unknown name or index";
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

// The registry's table `name` (made once) on top.
void reused_table(lua_State* L, const char* name, int size)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, name) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_createtable(L, size, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, name);
    }
}

void set_nil(lua_State* L, const char* k)
{
    lua_pushnil(L);
    lua_setfield(L, -2, k);
}

const char* const ANCHORS_OF[] = { "centre", "left", "right", "picture" };

// An element's fields into the table on top (reused: every field set every time, those another
// kind has to nil). parts: fresh tables (rood.dev.list), else the registry's reused ones.
// MAINMENU's frame (58EC.c, the menu_* kinds): what each piece drew from. menu_background: fade (0..16), brightness
// (opaque), ot_slot (bytes from vs_scratch.unk4), buffer (the frame buffer drawn: 0 or 1); menu_command: action (call_y its
// slide); menu_cursor: step, color (call_x, call_y its place); menu_tabs: mode, step, lit; menu_row: row = the
// record's fields (a copy, read before the emit steps its gradient), drawn as it is by d.menu_row.
// clear: nil the menu kinds' keys first (the table last held another kind; each kind below sets all of its own).
void fill_menu(lua_State* L, const pc_hud_fields& f, bool clear)
{
    int k = f.kind;
    // ("fade" is target_row's too: nil for the other kinds by its block, above)
    static const char* const keys[] = { "brightness", "ot_slot", "buffer", "action", "color", "mode", "lit", "row",
        "option", "items", "equip", "bars", "gems", "page", "ok", "storage", "loot", "discard", "rename", "highlight",
        "file", "file_menu" };
    if (clear)
        for (const char* key : keys)
            set_nil(L, key);
    if (k == PC_HUD_MENU_BACKGROUND) {
        set_int(L, "fade", f.menu_fade);
        set_int(L, "brightness", f.menu_brightness);
        set_int(L, "ot_slot", f.menu_slot);
        set_int(L, "buffer", f.menu_frame);
    } else if (k == PC_HUD_MENU_COMMAND) {
        set_int(L, "action", f.menu_action);
    } else if (k == PC_HUD_MENU_CURSOR) {
        set_int(L, "step", f.menu_step);
        set_int(L, "color", f.menu_color);
    } else if (k == PC_HUD_MENU_TABS) {
        set_int(L, "step", f.menu_step);
        set_int(L, "mode", f.menu_mode);
        set_bool(L, "lit", f.menu_lit != 0);
    } else if (k == PC_HUD_MENU_ROW) {
        const unsigned char* b = f.menu_item; // (vs_battle_menuItem_t's layout: 5BF94.h)
        auto s16 = [&](int at) { return (int)(int16_t)(b[at] | b[at + 1] << 8); };
        auto u16 = [&](int at) { return (int)(b[at] | b[at + 1] << 8); };
        lua_createtable(L, 0, 20);
        set_int(L, "id", f.menu_id);
        set_int(L, "state", b[0]);
        set_int(L, "w", b[1]);
        set_int(L, "background", b[2]);
        set_int(L, "gradient", b[4]);
        set_int(L, "scrollable", b[5]);
        set_bool(L, "selected", b[6] != 0);
        set_bool(L, "unselectable", b[7] != 0);
        set_int(L, "button", b[8]);
        set_int(L, "icon", b[9]);
        set_int(L, "color", b[10]);
        set_bool(L, "invert", b[11] != 0);
        set_int(L, "material", b[12]);
        set_int(L, "outset", b[13]);
        set_int(L, "page", u16(14));
        set_int(L, "count", u16(16));
        set_int(L, "max", u16(18));
        set_int(L, "x", s16(20));
        set_int(L, "y", s16(22));
        lock_table(L);
        lua_setfield(L, -2, "row");
    } else if (k == PC_HUD_OPTION_VALUE) { // our Option pages' value column: what classic_options.c draws it from
        static const char* const kinds[] = { "choice", "toggle", "slider", "action", "binding", "info", "header",
            "note", "link" };
        lua_createtable(L, 0, 20);
        set_int(L, "row", f.opt_row);
        lua_pushstring(L, f.opt_kind >= 0 && f.opt_kind < 9 ? kinds[f.opt_kind] : "");
        lua_setfield(L, -2, "kind");
        set_bool(L, "enabled", f.opt_enabled != 0);
        set_bool(L, "focused", f.opt_focused != 0);
        set_int(L, "index", f.opt_index);
        set_int(L, "right", f.opt_right);
        set_int(L, "lit", f.opt_lit);
        set_int(L, "other", f.opt_other);
        set_int(L, "gap", f.opt_gap);
        set_int(L, "bar_colour", f.opt_bar_colour);
        set_int(L, "bar_width", f.opt_bar_width);
        set_int(L, "fill", f.opt_fill);
        set_bool(L, "link", f.opt_link != 0);
        set_bool(L, "upper", f.opt_upper != 0);
        set_int(L, "window_x", f.opt_window_x);
        lua_pushstring(L, f.opt_value);
        lua_setfield(L, -2, "value");
        lua_pushstring(L, f.opt_on);
        lua_setfield(L, -2, "on");
        lua_pushstring(L, f.opt_off);
        lua_setfield(L, -2, "off");
        lua_pushstring(L, f.opt_label);
        lua_setfield(L, -2, "label");
        lock_table(L);
        lua_setfield(L, -2, "option");
    } else if (k == PC_HUD_ITEMS_CATEGORY) { // Items' category icons: what the loop draws them from
        lua_createtable(L, 0, 5);
        set_int(L, "step", f.items_step);
        set_int(L, "slot", f.items_slot);
        set_bool(L, "highlight", f.items_highlight == 1);
        set_int(L, "selected", f.items_selected);
        set_int(L, "on_row", f.items_on_row);
        lock_table(L);
        lua_setfield(L, -2, "items");
    } else if (k == PC_HUD_EQUIP_STATS) { // the stats panel: what vs_mainMenu_renderEquipStats prints, as it reads it
        const auto& q = f.equip;
        auto three = [&](const char* key, const int* v) {
            lua_createtable(L, 3, 0);
            for (int i = 0; i < 3; ++i) {
                lua_pushinteger(L, v[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, key);
        };
        lua_createtable(L, 0, 14);
        set_int(L, "x", (int16_t)(q.position & 0xFFFF));
        set_int(L, "y", (int16_t)((unsigned)q.position >> 16));
        set_int(L, "known", q.known);
        set_bool(L, "range_risk", q.range_risk_shown != 0);
        set_int(L, "range", q.range_risk[0]);
        set_int(L, "risk", q.range_risk[1]);
        set_int(L, "range_delta", q.range_delta);
        set_int(L, "subtype", q.subtype);
        three("stat", q.stat);
        three("threshold", q.threshold);
        three("base", q.base);
        three("total", q.total);
        three("accessory", q.accessory);
        lock_table(L);
        lua_setfield(L, -2, "equip");
    } else if (k == PC_HUD_DP_PP_BARS) { // the DP / PP bars: the step and the values
        lua_createtable(L, 0, 6);
        set_int(L, "step", f.bars.step);
        set_int(L, "target", f.bars.target);
        set_int(L, "dp", f.bars.dp);
        set_int(L, "max_dp", f.bars.max_dp);
        set_int(L, "pp", f.bars.pp);
        set_int(L, "max_pp", f.bars.max_pp);
        lock_table(L);
        lua_setfield(L, -2, "bars");
    } else if (k == PC_HUD_STAT_PAGE) { // the class / affinity / type table: what _renderStatPage reads
        const auto& p = f.stat_page;
        lua_createtable(L, 0, 5);
        set_int(L, "x_offset", p.x_offset);
        set_int(L, "page", p.page);
        set_int(L, "enabled", p.enabled);
        set_int(L, "subtype", p.subtype);
        lua_createtable(L, 63, 1); // (0-based, as the game indexes it)
        for (int i = 0; i < 64; ++i) {
            lua_pushinteger(L, p.stats[i]);
            lua_rawseti(L, -2, i);
        }
        lua_setfield(L, -2, "stats");
        lock_table(L);
        lua_setfield(L, -2, "page");
    } else if (k == PC_HUD_GEM_SLOTS) { // a grip's gem slots
        lua_createtable(L, 0, 1);
        set_int(L, "count", f.gem_count);
        lock_table(L);
        lua_setfield(L, -2, "gems");
    } else if (k == PC_HUD_OK_MARK) { // the OK mark: its place and level
        lua_createtable(L, 0, 3);
        set_int(L, "x", f.ok.x);
        set_int(L, "y", f.ok.y);
        set_int(L, "level", f.ok.level);
        lock_table(L);
        lua_setfield(L, -2, "ok");
    } else if (k == PC_HUD_DISCARD_COUNT) { // a misc item's discard amount: the count, the units' place
        lua_createtable(L, 0, 3);
        set_int(L, "count", f.discard.count);
        set_int(L, "x", f.discard.x);
        set_int(L, "y", f.discard.y);
        lock_table(L);
        lua_setfield(L, -2, "discard");
    } else if (k == PC_HUD_RENAME_GRID) { // the rename grid: its slide, bars, arrows, the name and the game's words
        auto codes = [L](const unsigned char* s, int n, bool to_end) { // (the menu charset's codes, 1-based)
            lua_createtable(L, n, 0);
            for (int i = 0; i < n && !(to_end && s[i] == 0xE7); ++i) {
                lua_pushinteger(L, s[i]);
                lua_rawseti(L, -2, i + 1);
            }
        };
        lua_createtable(L, 0, 8);
        set_int(L, "x_offset", f.rename.x_offset);
        lua_createtable(L, 4, 0);
        for (int i = 0; i < 4; ++i) {
            lua_pushinteger(L, f.rename.fade[i]);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "fade");
        lua_pushboolean(L, f.rename.lit != 0);
        lua_setfield(L, -2, "lit");
        set_int(L, "step", f.rename.step);
        codes(f.rename.name, 20, false);
        lua_setfield(L, -2, "name");
        lua_createtable(L, 4, 0);
        for (int i = 0; i < 4; ++i) {
            codes(f.rename.commands[i], 16, true);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "commands");
        lua_createtable(L, 9, 0);
        for (int i = 0; i < 9; ++i) {
            codes(f.rename.table[i], 14, false);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "table");
        lock_table(L);
        lua_setfield(L, -2, "rename");
    } else if (k == PC_HUD_FILE_MENU) { // MENU7's file menu's frame: its part, the band's x, the hand, the message, fade
        const auto& m = f.file_menu;
        lua_createtable(L, 0, 8);
        set_int(L, "part", m.part);
        set_int(L, "x", m.x);
        set_int(L, "cursor_xy", m.cursor_xy);
        set_int(L, "cursor_color", m.cursor_color);
        set_int(L, "fade", m.fade);
        set_int(L, "arrow", m.arrow);
        if (m.has_message) { // (its codes as the game keeps them: to 0xE7, the control pairs with it)
            int n = 0;
            while (n < (int)sizeof m.message && m.message[n] != 0xE7)
                n += m.message[n] >= 0xEC ? 2 : 1;
            n = n < (int)sizeof m.message ? n + 1 : (int)sizeof m.message;
            lua_createtable(L, n, 0);
            for (int i = 0; i < n; ++i) {
                lua_pushinteger(L, m.message[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, "message");
        }
        lock_table(L);
        lua_setfield(L, -2, "file_menu");
    } else if (k == PC_HUD_FILE_ELEMENT) { // a card's or a file's panel in it
        const auto& e = f.file;
        auto codes = [L](const unsigned char* s, int size, int end) { // (to its end code, kept; 1-based)
            int n = 0;
            while (n < size && s[n] != end)
                n += s[n] >= 0xEC && s[n] != 0xFF ? 2 : 1;
            n = n < size ? n + 1 : size;
            lua_createtable(L, n, 0);
            for (int i = 0; i < n; ++i) {
                lua_pushinteger(L, s[i]);
                lua_rawseti(L, -2, i + 1);
            }
        };
        lua_createtable(L, 0, 32);
        set_int(L, "id", e.id);
        set_int(L, "x", e.x);
        set_int(L, "y", e.y);
        set_int(L, "w", e.w);
        set_int(L, "h", e.h);
        set_bool(L, "selected", e.selected != 0);
        set_bool(L, "unavailable", e.unavailable != 0);
        set_int(L, "outer", e.outer);
        set_int(L, "inner", e.inner);
        set_int(L, "slot", e.slot);
        set_int(L, "location", e.location);
        set_bool(L, "saving", e.saving != 0);
        set_int(L, "arrow", e.arrow);
        set_int(L, "slot_state", e.slot_state);
        set_int(L, "map", e.map);
        set_int(L, "saves", e.saves);
        set_int(L, "clears", e.clears);
        set_int(L, "hours", e.hours);
        set_int(L, "minutes", e.minutes);
        set_int(L, "seconds", e.seconds);
        set_int(L, "hp", e.hp);
        set_int(L, "hp_max", e.hp_max);
        set_int(L, "mp", e.mp);
        set_int(L, "mp_max", e.mp_max);
        set_int(L, "progress", e.progress);
        set_int(L, "progress_x", e.progress_x);
        set_int(L, "progress_colour", e.progress_colour);
        codes(e.text, (int)sizeof e.text, 0xFF);
        lua_setfield(L, -2, "text");
        codes(e.place, (int)sizeof e.place, 0xE7);
        lua_setfield(L, -2, "place");
        codes(e.in_use, (int)sizeof e.in_use, 0xE7);
        lua_setfield(L, -2, "in_use");
        codes(e.empty, (int)sizeof e.empty, 0xE7);
        lua_setfield(L, -2, "empty");
        codes(e.fresh, (int)sizeof e.fresh, 0xE7);
        lua_setfield(L, -2, "fresh");
        lua_createtable(L, 2, 0);
        for (int i = 0; i < 2; ++i) { // (the CLUTs as d.image_load takes them: 2 bytes a pixel, little-endian)
            char bytes[512];
            for (int j = 0; j < 256; ++j)
                bytes[2 * j] = (char)(e.cluts[i][j] & 0xFF), bytes[2 * j + 1] = (char)(e.cluts[i][j] >> 8);
            lua_pushlstring(L, bytes, sizeof bytes);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "cluts");
        lock_table(L);
        lua_setfield(L, -2, "file");
    } else if (k == PC_HUD_RENAME_BOX) { // a highlight box on the rename grid: its place and size
        lua_createtable(L, 0, 4);
        set_int(L, "x", f.rename_box.x);
        set_int(L, "y", f.rename_box.y);
        set_int(L, "w", f.rename_box.w);
        set_int(L, "h", f.rename_box.h);
        lock_table(L);
        lua_setfield(L, -2, "highlight");
    } else if (k == PC_HUD_STORAGE_BUTTONS) { // Storage's button strip: its slide and state
        lua_createtable(L, 0, 2);
        set_int(L, "x", f.storage_buttons.x);
        set_int(L, "state", f.storage_buttons.state);
        lock_table(L);
        lua_setfield(L, -2, "storage");
    } else if (k == PC_HUD_STORAGE_COUNTS) { // Storage's counts: the layout, the slide, the columns (1-based)
        const auto& c = f.storage_counts;
        lua_createtable(L, 0, 5);
        set_int(L, "layout", c.layout);
        set_int(L, "x", c.x);
        set_int(L, "container", c.container);
        for (int which = 0; which < 2; ++which) {
            lua_createtable(L, 2, 0);
            for (int col = 0; col < 2; ++col) {
                lua_createtable(L, 7, 0);
                for (int i = 0; i < 7; ++i) {
                    lua_pushinteger(L, which ? c.count[col][i] : c.capacity[col][i]);
                    lua_rawseti(L, -2, i + 1);
                }
                lua_rawseti(L, -2, col + 1);
            }
            lua_setfield(L, -2, which ? "count" : "capacity");
        }
        lock_table(L);
        lua_setfield(L, -2, "storage");
    } else if (k == PC_HUD_LOOT_CAPACITIES) { // Loot's capacities strip: what _displayInventoryCapacities reads
        const auto& l = f.loot;
        auto seven = [&](const char* key, const int* v) {
            lua_createtable(L, 7, 0);
            for (int i = 0; i < 7; ++i) {
                lua_pushinteger(L, v[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, key);
        };
        lua_createtable(L, 0, 7);
        set_int(L, "x", l.x);
        set_int(L, "controls", l.controls);
        set_int(L, "looking", l.looking);
        set_bool(L, "all", l.all != 0);
        seven("capacity", l.capacity);
        seven("count", l.count);
        seven("incoming", l.incoming);
        lock_table(L);
        lua_setfield(L, -2, "loot");
    } else if (k == PC_HUD_ITEMS_STATUS_BUTTON) { // Items' STATUS strip: its slide and state
        lua_createtable(L, 0, 2);
        set_int(L, "row", f.items_step);
        set_int(L, "state", f.items_highlight);
        lock_table(L);
        lua_setfield(L, -2, "items");
    }
}

// The kind the pass's shared element table was last filled with, and whose table (a new one holds none).
int shared_kind = -1;
lua_State* shared_state;

void fill_element(lua_State* L, const hud::Element& e, bool fresh = false)
{
    const pc_hud_fields& f = e.f;
    int k = f.kind;
    // the menu keys need clearing only when the shared table held another kind (the gauges: twenty a frame)
    bool clear_menu = !fresh && shared_kind != k;
    if (!fresh)
        shared_kind = k;
    set_str(L, "kind", hud::kind_name(k));
    set_int(L, "id", e.id);
    set_str(L, "text", f.text);
    set_field(L, "x", f.x);
    set_field(L, "y", f.y);
    set_field(L, "w", f.w);
    set_field(L, "h", f.h);
    set_int(L, "rgb", f.rgb);
    set_int(L, "age", f.age);
    set_int(L, "life", f.life);
    set_bool(L, "held", e.held);
    set_bool(L, "synthetic", e.synthetic);
    set_str(L, "flavour", classic_pass ? "classic" : "modern");
    set_str(L, "anchor", ANCHORS_OF[f.anchor >= 0 && f.anchor < 4 ? f.anchor : 0]);
    if (k == PC_HUD_STATUS) {
        set_str(L, "timing", hud::timing_name(f.timing));
        set_int(L, "variant", f.variant);
    } else {
        set_nil(L, "timing");
        set_nil(L, "variant");
    }
    static const char* const icon_fields[] = { "previous", "current", "next", "count", "selected", "offset" };
    const int icon_values[] = { f.icon_previous, f.icon_current, f.icon_next, f.icon_count, f.icon_selected, f.icon_offset };
    for (int i = 0; i < 6; ++i) {
        if (k == PC_HUD_STATUS_ICONS)
            set_int(L, icon_fields[i], icon_values[i]);
        else
            set_nil(L, icon_fields[i]);
    }
    if (k == PC_HUD_ABILITY_ICONS) {
        set_int(L, "animation", f.ability_animation);
        set_int(L, "offset", f.ability_offset);
        set_int(L, "group", f.ability_group);
        set_int(L, "availability", f.ability_availability);
    } else {
        set_nil(L, "animation");
        set_nil(L, "group");
        set_nil(L, "availability");
    }
    if (k == PC_HUD_TEXT_BAND) { // the box as func_800CD158 reads it
        set_int(L, "box", f.box_id);
        set_bool(L, "top", f.band_top != 0);
        set_int(L, "edge", f.band_edge);
        set_int(L, "lines", f.band_lines);
        set_bool(L, "settled", f.band_settled != 0);
        set_int(L, "icon", f.band_icon);
    } else {
        static const char* const band_fields[] = { "box", "top", "edge", "lines", "settled", "icon" };
        for (const char* k2 : band_fields)
            if (k != PC_HUD_BOX_TEXT || strcmp(k2, "box"))
                set_nil(L, k2);
    }
    if (k == PC_HUD_IMAGE) {
        set_int(L, "image", f.image_index);
        set_int(L, "drawer", f.image_mode);
        set_int(L, "clut", f.image_clut);
    } else {
        set_nil(L, "image");
        set_nil(L, "drawer");
        set_nil(L, "clut");
    }
    if (k == PC_HUD_BOX_TEXT) { // the glyphs _printVariableWidthFont set: 6 integers each, one table reused
        const int* g = nullptr;
        int n = pc_hud_box_text_glyphs ? pc_hud_box_text_glyphs(&g) : 0;
        set_int(L, "box", f.box_id);
        lua_getfield(L, -1, "glyphs");
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_createtable(L, n * 6, 0);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, "glyphs");
        }
        lua_Integer had = (lua_Integer)lua_rawlen(L, -1);
        for (int i = 0; i < n * 6; ++i) {
            lua_pushinteger(L, g[i]);
            lua_rawseti(L, -2, i + 1);
        }
        for (lua_Integer i = n * 6 + 1; i <= had; ++i) {
            lua_pushnil(L);
            lua_rawseti(L, -2, i);
        }
        lua_pop(L, 1);
    } else {
        set_nil(L, "glyphs");
    }
    if (k == PC_HUD_TEXT_FRAME) { // the box as func_800CD3E4 reads it
        set_int(L, "box", f.box_id);
        set_int(L, "box_x", f.box_x);
        set_int(L, "box_y", f.box_y);
        set_int(L, "box_w", f.box_w);
        set_int(L, "box_h", f.box_h);
        set_int(L, "shape", f.box_shape);
        set_int(L, "flip", f.box_flip);
        set_int(L, "tail", f.box_tail);
    } else {
        static const char* const box_fields[] = { "box_x", "box_y", "box_w", "box_h", "shape", "flip", "tail" };
        for (const char* k2 : box_fields)
            set_nil(L, k2);
    }
    if (k == PC_HUD_EMOTE) { // what func_80095C18 decided
        set_str(L, "draw", f.emote_draw == 1 ? "balloon" : f.emote_draw == 2 ? "arrow" : "far");
        set_int(L, "emote", f.emote_kind);
        set_int(L, "side", f.emote_side);
        set_int(L, "z", f.emote_z);
        set_int(L, "direction", f.emote_direction);
        set_int(L, "frame", f.emote_frame);
        set_int(L, "actor", f.value);
    } else {
        static const char* const emote_fields[] = { "draw", "emote", "side", "z", "direction", "frame", "actor" };
        for (const char* k2 : emote_fields)
            set_nil(L, k2);
    }
    if (k == PC_HUD_TIMER) { // as the game's string shows them
        set_int(L, "minutes", f.minutes);
        set_int(L, "seconds", f.seconds);
        set_int(L, "hundredths", f.hundredths);
    } else {
        set_nil(L, "minutes");
        set_nil(L, "seconds");
        set_nil(L, "hundredths");
    }
    if (k == PC_HUD_ACTOR_WORD) { // which draw the game chose (MISS or a combat label) and its inputs
        set_str(L, "word", f.word_miss ? "miss" : "label");
        set_int(L, "label", f.word_label);
        set_bool(L, "lowered", f.word_lowered != 0);
    } else {
        set_nil(L, "word");
        set_nil(L, "label");
        set_nil(L, "lowered");
    }
    if (k == PC_HUD_BATTLE_MODE) {
        set_int(L, "step", f.battle_step);
        set_int(L, "pulse", f.battle_pulse);
    } else {
        set_nil(L, "step");
        set_nil(L, "pulse");
    }
    set_int(L, "stance", f.stance);
    static const char* const limb_fields[] = { "limb_0_status", "limb_1_status", "limb_2_status", "limb_3_status", "limb_4_status" };
    for (int i = 0; i < 5; ++i) {
        if (k == PC_HUD_FIGURE)
            set_int(L, limb_fields[i], f.limb_status[i]);
        else
            set_nil(L, limb_fields[i]);
    }
    if (k != PC_HUD_FIGURE)
        set_nil(L, "stance");
    if (k == PC_HUD_STATUS_ICONS || k == PC_HUD_BATTLE_MODE)
        set_int(L, "slide", f.slide);
    if (k == PC_HUD_GAUGES) {
        set_int(L, "hp", f.hp);
        set_int(L, "max_hp", f.max_hp);
        set_int(L, "mp", f.mp);
        set_int(L, "max_mp", f.max_mp);
        set_int(L, "risk", f.risk);
        set_int(L, "slide", f.slide);
    } else {
        static const char* const gauge_fields[] = { "hp", "max_hp", "mp", "max_mp", "risk", "slide" };
        for (const char* g : gauge_fields)
            if ((k != PC_HUD_STATUS_ICONS && k != PC_HUD_BATTLE_MODE) || strcmp(g, "slide"))
                set_nil(L, g);
    }
    if (((k >= PC_HUD_STATUS && k <= PC_HUD_DAMAGE) || k == PC_HUD_ACTOR_WORD || k == PC_HUD_TIMER || k == PC_HUD_EMOTE
            || k == PC_HUD_TARGET_ROW || k == PC_HUD_TARGET_LINE || k == PC_HUD_TARGET_MARKER || k == PC_HUD_MENU_COMMAND
            || k == PC_HUD_MENU_ROW || k == PC_HUD_MENU_CURSOR || k == PC_HUD_OPTION_VALUE) && !e.synthetic) { // where the game's call put it
        set_int(L, "call_x", f.call_x);
        set_int(L, "call_y", f.call_y);
    } else {
        set_nil(L, "call_x");
        set_nil(L, "call_y");
    }
    if (k == PC_HUD_TARGET_ROW) { // the row as func_800C1DC4 reads it (fade: before the game steps it)
        set_bool(L, "selected", f.row_selected != 0);
        set_int(L, "fade", f.row_fade);
        set_int(L, "bank", f.row_bank);
    } else {
        if (k != PC_HUD_STATUS_ICONS)
            set_nil(L, "selected");
        set_nil(L, "fade");
        set_nil(L, "bank");
    }
    if (k == PC_HUD_TARGET_LINE) { // the row the lines lead to (call_x, call_y their start; age their sweep)
        set_int(L, "row_x", f.row_x);
        set_int(L, "row_y", f.row_y);
    } else {
        set_nil(L, "row_x");
        set_nil(L, "row_y");
    }
    if (k == PC_HUD_TARGET_MARKER) { // func_800C28AC's lock (arg1) and the brightness step it stepped
        set_bool(L, "lock", f.marker_lock != 0);
        set_int(L, "step", f.marker_step);
    } else {
        set_nil(L, "lock");
        if (k != PC_HUD_BATTLE_MODE)
            set_nil(L, "step");
    }
    fill_menu(L, f, clear_menu);
    if (k == PC_HUD_CHAIN || k == PC_HUD_BUFF || k == PC_HUD_DAMAGE)
        set_int(L, "value", f.value);
    else
        set_nil(L, "value");
    if (k == PC_HUD_NOTICE)
        set_int(L, "slot", f.value); // the save state slot (0: none)
    if (k == PC_HUD_NAME_WINDOW) { // the action (vs_main_actions' index) and the scene message
        set_int(L, "index", f.value);
        set_int(L, "message", f.variant);
    } else {
        set_nil(L, "index");
        set_nil(L, "message");
    }
    if (k == PC_HUD_BUFF || k == PC_HUD_DAMAGE)
        set_int(L, "sign", f.sign);
    else
        set_nil(L, "sign");
    if (k == PC_HUD_BUFF) {
        set_str(L, "slot", hud::slot_word(f.slot));
        set_str(L, "stat", hud::stat_word(f.stat));
        set_int(L, "rise", f.rise);
    } else if (k == PC_HUD_DAMAGE && f.pool == 2) {
        set_nil(L, "slot");
        set_nil(L, "rise");
        set_int(L, "stat", f.stat);
    } else {
        if (k != PC_HUD_NOTICE)
            set_nil(L, "slot");
        set_nil(L, "stat");
        set_nil(L, "rise");
    }
    if (k == PC_HUD_DAMAGE) {
        set_str(L, "pool", hud::pool_name(f.pool));
        set_field(L, "bounce", f.bounce);
    } else {
        set_nil(L, "pool");
        set_nil(L, "bounce");
    }
    if (k == PC_HUD_TRAVEL) {
        set_str(L, "glyph", f.glyph >= 0 && f.glyph < BUTTON_COUNT ? BUTTONS[f.glyph] : "travel");
        set_int(L, "corner", f.variant); // the prompt's anchor: 0..8, top left .. bottom right
        set_field(L, "unit", f.unit);
    } else if (k == PC_HUD_NOTICE) {
        set_nil(L, "glyph");
        set_int(L, "corner", f.variant); // which of its corners goes on x, y (0 top left)
        set_field(L, "unit", f.unit);
    } else {
        set_nil(L, "glyph");
        set_nil(L, "corner");
        set_nil(L, "unit");
    }
    if (!f.part_count) {
        set_nil(L, "parts");
        return;
    }
    // parts: { {x, y, w, h, text}, ... }; reused: the view and, kept apart, each part's table
    if (fresh) {
        lua_createtable(L, f.part_count, 0);
    } else {
        reused_table(L, "rood.hud.parts", PC_HUD_PARTS);
        reused_table(L, "rood.hud.part_tables", PC_HUD_PARTS);
        lua_insert(L, -2); // ... parts' tables, the view
    }
    for (int i = 0; i < PC_HUD_PARTS; ++i) {
        if (i >= f.part_count) {
            lua_pushnil(L);
            lua_rawseti(L, -2, i + 1);
            continue;
        }
        if (fresh) {
            lua_createtable(L, 0, 5);
        } else if (lua_rawgeti(L, -2, i + 1) != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_createtable(L, 0, 5);
            lua_pushvalue(L, -1);
            lua_rawseti(L, -4, i + 1);
        }
        const pc_hud_part& pt = f.parts[i];
        set_field(L, "x", pt.x);
        set_field(L, "y", pt.y);
        set_field(L, "w", pt.w);
        set_field(L, "h", pt.h);
        set_str(L, "text", pt.text);
        lua_rawseti(L, -2, i + 1);
    }
    if (!fresh)
        lua_remove(L, -2);
    lua_setfield(L, -2, "parts");
}

int api_list(lua_State* L)
{
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < PC_HUD_POOL; ++i) {
        const hud::Element& e = hud::pool()[i];
        if (!hud::live(e))
            continue;
        lua_createtable(L, 0, 24);
        fill_element(L, e, true);
        set_int(L, "drawn", e.drawn);
        set_bool(L, "suppressed", e.suppressed);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// ---- the pass ----
#define FRAME_RING 256
double frame_ms[FRAME_RING]; // the claims' Lua per presented frame
unsigned frame_count;
// Allocations (blocks made or grown) in the claims' states during the passes, and the passes measured.
unsigned long long pass_allocations, classic_allocations;
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
        shared_kind = -1;
    }
    if (L != shared_state)
        shared_state = L, shared_kind = -1;
    fill_element(L, *job.element);
    lua_getfield(L, LUA_REGISTRYINDEX, "rood.hud.draw");
    lua_call(L, 2, 0);
    job.ok = true;
}

void drop_hud(Plugin* p)
{
    for (int i = 0; i < 8; ++i)
        if (typography_of[i].owner == p->id()) {
            if (pc_ui_text_override_clear)
                pc_ui_text_override_clear(i);
            typography_of[i].owner.clear();
        }
    drop_panel(p, nullptr);
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

void open_hud(lua_State* L, Plugin* p)
{
    if (luaL_newmetatable(L, STYLE_META)) {
        lua_pushcfunction(L, style_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, style_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, style_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushcfunction(L, style_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
    lua_newtable(L);
    set_cfunction(L, -1, "claim", api_claim);
    set_cfunction(L, -1, "release", api_release);
    set_cfunction(L, -1, "kinds", api_kinds);
    set_cfunction(L, -1, "honoured", api_honoured);
    set_cfunction(L, -1, "look", api_look);
    set_cfunction(L, -1, "typography", api_typography);
    lock_table(L);
    lua_setfield(L, -2, "hud");
    lua_newtable(L);
    set_cfunction(L, -1, "style", api_style);
    set_cfunction(L, -1, "text", api_text);
    set_cfunction(L, -1, "measure", api_measure);
    set_cfunction(L, -1, "rect", api_rect);
    set_cfunction(L, -1, "fit", api_fit);
    set_cfunction(L, -1, "glyph", api_glyph);
    set_cfunction(L, -1, "glyph_width", api_glyph_width);
    set_cfunction(L, -1, "gradient", api_gradient);
    set_cfunction(L, -1, "sprite", api_sprite);
    set_cfunction(L, -1, "quad", api_quad);
    set_cfunction(L, -1, "menu_row", api_menu_row);
    set_cfunction(L, -1, "option_text", api_option_text);
    set_cfunction(L, -1, "option_bar", api_option_bar);
    set_cfunction(L, -1, "button_background", api_button_background);
    set_cfunction(L, -1, "menu_int", api_menu_int);
    set_cfunction(L, -1, "packet", api_packet);
    set_cfunction(L, -1, "image_store", api_image_store);
    set_cfunction(L, -1, "image_load", api_image_load);
    set_cfunction(L, -1, "image_restore", api_image_restore);
    set_cfunction(L, -1, "text_glyph", api_text_glyph);
    set_cfunction(L, -1, "font_text", api_font_text);
    set_cfunction(L, -1, "font_width", api_font_width);
    set_cfunction(L, -1, "stat_bar", api_stat_bar);
    set_cfunction(L, -1, "option_width", api_option_width);
    set_cfunction(L, -1, "frame", api_frame);
    set_cfunction(L, -1, "band", api_band);
    set_cfunction(L, -1, "char", api_char);
    set_cfunction(L, -1, "image", api_image);
    set_cfunction(L, -1, "plate", api_plate);
    set_cfunction(L, -1, "leader", api_leader);
    set_cfunction(L, -1, "window", api_window);
    set_cfunction(L, -1, "cursor", api_cursor);
    set_cfunction(L, -1, "bar", api_bar);
    set_cfunction(L, -1, "text_raw", api_text_raw);
    set_cfunction(L, -1, "anchor", api_anchor);
    set_cfunction(L, -1, "layer", api_layer);
    set_cfunction(L, -1, "value", api_value);
    set_cfunction(L, -1, "digit", api_digit);
    set_cfunction(L, -1, "xy", api_xy);
    set_cfunction(L, -1, "panel", api_panel);
    lock_table(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "rood.hud.draw"); // d in draw functions
    lua_setfield(L, -2, "draw");
    bool tools = granted(p, "dev.tools"); // rood.dev.window, rood.dev.rpc (lua_dev_tools.cpp)
    if (dev_mode() || tools) {
        lua_newtable(L);
        if (dev_mode()) {
            set_cfunction(L, -1, "show", api_show);
            set_cfunction(L, -1, "clear", api_clear);
            set_cfunction(L, -1, "list", api_list);
            open_bench(L);
        }
        if (tools)
            for (ApiOpener open : dev_openers())
                open(L, p);
        lock_table(L);
        lua_setfield(L, -2, "dev");
    }
}

// An element as an event's payload: the fields its draw table has (fill_element), as plain data.
Value element_value(const hud::Element& e)
{
    const pc_hud_fields& f = e.f;
    int k = f.kind;
    Value v = Value::new_table();
    v.set("kind", Value::str(hud::kind_name(k)));
    v.set("id", Value::integer(e.id));
    v.set("text", Value::str(f.text));
    v.set("x", Value::number(f.x));
    v.set("y", Value::number(f.y));
    v.set("w", Value::number(f.w));
    v.set("h", Value::number(f.h));
    v.set("rgb", Value::integer(f.rgb));
    v.set("age", Value::integer(f.age));
    v.set("life", Value::integer(f.life));
    v.set("held", Value::boolean(e.held));
    v.set("synthetic", Value::boolean(e.synthetic));
    v.set("anchor", Value::str(ANCHORS_OF[f.anchor >= 0 && f.anchor < 4 ? f.anchor : 0]));
    if (k == PC_HUD_STATUS) {
        v.set("timing", Value::str(hud::timing_name(f.timing)));
        v.set("variant", Value::integer(f.variant));
    }
    if (k == PC_HUD_CHAIN || k == PC_HUD_BUFF || k == PC_HUD_DAMAGE)
        v.set("value", Value::integer(f.value));
    if (k == PC_HUD_BUFF || k == PC_HUD_DAMAGE)
        v.set("sign", Value::integer(f.sign));
    if (k == PC_HUD_BUFF) {
        v.set("slot", Value::str(hud::slot_word(f.slot)));
        v.set("stat", Value::str(hud::stat_word(f.stat)));
    }
    if (k == PC_HUD_DAMAGE) {
        v.set("pool", Value::str(hud::pool_name(f.pool)));
        if (f.pool == 2)
            v.set("stat", Value::integer(f.stat));
    }
    if (k == PC_HUD_NAME_WINDOW) {
        v.set("index", Value::integer(f.value));
        v.set("message", Value::integer(f.variant));
    }
    if (k == PC_HUD_NOTICE)
        v.set("slot", Value::integer(f.value));
    return v;
}

// Each tick: the held moments and expiry (hud_elements.cpp), then an event per new appearance:
// "hud.<kind>" with the element (for logic: drawing is the claims').
unsigned long long events_emitted;
void hud_tick()
{
    pc_hud_tick();
    bench_resume_tick();
    hud::Appearance a[64];
    int n = hud::take_appearances(a, 64);
    for (int i = 0; i < n; ++i) {
        const hud::Element& e = hud::pool()[a[i].slot];
        if (!e.used || e.id != a[i].id)
            continue;
        ++events_emitted;
        emit(std::string("hud.") + hud::kind_name(e.f.kind), element_value(e));
    }
}

struct Register {
    Register()
    {
        api_openers().push_back(open_hud);
        unload_hooks().push_back(drop_hud);
        tick_hooks().push_back(hud_tick);
    }
} register_hud;
} // namespace

// The claims' draws of every live element into the command buffer (lua_probe calls it
// without a renderer); returns the commands.
int hud_pass()
{
    command_used = 0;
    arena_used = 0;
    if (hud::classic_look())
        return 0; // claims draw at the game's hooks then (pc_hud_classic_draw), never over the picture
    double start = now_seconds();
    for (int i = 0; i < PC_HUD_POOL; ++i) {
        hud::Element& e = hud::pool()[i];
        if (!hud::live(e) || !(e.suppressed || e.synthetic))
            continue; // the game draws it (no claim was honoured when it drew)
        Claim* c = const_cast<Claim*>(owner(e.f.kind));
        if (!c || claim_down(c) || !pc_hud_honoured(e.f.kind))
            continue;
        job = { c, &e, false };
        drawing.anchor = drawing.element_anchor = e.f.anchor;
        in_draw = true;
        std::string error;
        unsigned long long allocations = c->plugin->mem.allocations;
        bool ok = run(c->plugin, "hud draw", BUDGET_CALL, draw_element, &error) && job.ok;
        pass_allocations += c->plugin->mem.allocations - allocations;
        in_draw = false;
        hud::KindStats& s = hud::stats(e.f.kind);
        if (ok) {
            ++s.drawn;
            ++e.drawn;
        } else {
            ++s.faults;
            claim_fault(c, e.f.kind, "draw"); // (down: the game's own drawing from the next frame on)
        }
    }
    if (command_used || frame_count) // (frames with nothing claimed do not dilute the figures)
        frame_ms[frame_count++ % FRAME_RING] = (now_seconds() - start) * 1000.0;
    return command_used;
}

// The panels' draws (rood.draw.panel). Modern: into the command buffer, after the HUD's pass drew its own.
// Classic ({classic = true} panels with the classic look): at the game's frame (panels_classic_next), into the
// classic commands, a panel at a time.
double panel_dt;
float panel_width = 320; // the picture's width in square pixels (the last presented frame's)
constexpr int PANEL_LAYER = -12; // classic panels link where the menus' windows do (bytes from vs_scratch.unk4)
bool panel_runs(Panel& pn)
{
    Plugin* p = pn.plugin;
    if (pn.failed && pn.retry_at > 0 && now_seconds() >= pn.retry_at)
        pn.failed = false, pn.retry_at = 0;
    return !(pn.failed || !p->L || p->state != PLUGIN_LOADED || p->generation != pn.generation
        || !p->disable_reason.empty());
}
void draw_panel(size_t i, float width, float height, bool classic)
{
    Panel& pn = panels[i];
    Plugin* p = pn.plugin;
    panel_job = &pn;
    drawing.anchor = drawing.element_anchor = PC_HUD_ANCHOR_SCREEN;
    in_draw = true;
    classic_pass = classic;
    std::string error;
    bool ok = run(p, "panel draw", BUDGET_CALL, [&](lua_State* L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, panel_job->ref);
        lua_getfield(L, LUA_REGISTRYINDEX, "rood.hud.draw");
        lua_createtable(L, 0, 6);
        lua_pushnumber(L, width);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, height);
        lua_setfield(L, -2, "height");
        lua_pushnumber(L, height > 0 ? width / height : 4.0 / 3.0);
        lua_setfield(L, -2, "aspect");
        lua_pushstring(L, hud::classic_look() ? "classic" : "modern");
        lua_setfield(L, -2, "look");
        lua_pushstring(L, classic ? "classic" : "modern");
        lua_setfield(L, -2, "flavour");
        lua_pushnumber(L, panel_dt);
        lua_setfield(L, -2, "dt");
        lua_call(L, 2, 0);
    }, &error);
    in_draw = classic_pass = false;
    if (i < panels.size() && &panels[i] == panel_job && !ok) {
        if (p->budget.killed) {
            // Over its budget (a slow frame): drawn again next frame; one that keeps overrunning is
            // disabled by the fault policy (fault_limit within fault_window_s).
        } else {
            // an error: until the plugin reloads or sets its panel again (shipped: or the cool-down)
            panels[i].failed = true;
            bool retry = p->shipped && config.fault_cooldown_s > 0;
            panels[i].retry_at = retry ? now_seconds() + config.fault_cooldown_s : 0;
            log(LOG_WARN, p->id(), retry ? "its panel draw failed: not drawn for a while (the cool-down)"
                                         : "its panel draw failed: not drawn until the plugin reloads or sets it again");
        }
    }
    panel_job = nullptr;
}
int panels_pass(float width, float height)
{
    command_used = 0;
    arena_used = 0;
    bool classic = hud::classic_look();
    for (size_t i = 0; i < panels.size(); ++i) {
        if ((classic && panels[i].classic) || !panel_runs(panels[i]))
            continue;
        draw_panel(i, width, height, false);
    }
    return command_used;
}
// The last classic panels' first commands, for RPC hud stats (classic_buffer is the HUD claims' by then).
struct PanelSnapshot {
    int count;
    pc_hud_classic_cmd cmd[16];
    char text[16][96];
} panel_snapshot;
void panel_remember(int index)
{
    if (index == 0)
        panel_snapshot.count = 0;
    for (int i = 0; i < classic_used && panel_snapshot.count < 16; ++i) {
        pc_hud_classic_cmd& c = panel_snapshot.cmd[panel_snapshot.count];
        c = classic_buffer[i];
        if (c.text) {
            char* t = panel_snapshot.text[panel_snapshot.count];
            std::snprintf(t, sizeof panel_snapshot.text[0], "%.*s", c.length, c.text);
            c.text = t;
            c.length = (int)std::strlen(t);
        }
        ++panel_snapshot.count;
    }
}
// The next classic panel from *cursor (a panel's index): its commands in classic_buffer and its anchor, or -1
// when there is none.
int panels_classic_next(int* cursor, int* anchor)
{
    if (!hud::classic_look())
        return -1;
    for (size_t i = (size_t)*cursor; i < panels.size(); ++i) {
        if (!panels[i].classic || !panel_runs(panels[i]))
            continue;
        classic_used = 0;
        classic_arena_used = 0;
        classic_anchor = -1;
        classic_layer = PANEL_LAYER;
        draw_panel(i, panel_width, 240.0f, true);
        panel_remember(*cursor);
        *cursor = (int)i + 1;
        *anchor = classic_anchor >= 0 ? classic_anchor : PC_HUD_ANCHOR_LEFT;
        return classic_used;
    }
    return -1;
}

// The classic flavour: the claim on the element just recorded draws now, at the game's hook (pc_hud_classic_draw).
#define CLASSIC_RING 256
double classic_ms[CLASSIC_RING]; // the claims' Lua per game frame (every element drawn in it)
unsigned classic_frames;
unsigned classic_frame_vsync = ~0u;
int classic_draw(bool parity = false, const char** why = nullptr)
{
    classic_used = 0;
    classic_arena_used = 0;
    classic_anchor = -1;
    classic_layer = PC_HUD_LAYER_KIND;
    hud::Element* e = hud::last_recorded();
    if (!e || !e->used || (!parity && !e->suppressed)) {
        if (why)
            *why = "no element recorded";
        return -1;
    }
    Claim* c = const_cast<Claim*>(parity ? owner_for(e->f.kind, 1) : owner(e->f.kind));
    if (parity && (!c || c->flavours != FLAVOUR_CLASSIC)) {
        if (why)
            *why = "no claim made for the classic flavour";
        return -1;
    }
    if (!c || claim_down(c)) {
        if (why)
            *why = "its classic claim failed";
        return -1;
    }
    double start = now_seconds();
    job = { c, e, false };
    drawing.anchor = drawing.element_anchor = e->f.anchor;
    in_draw = classic_pass = true;
    std::string error;
    unsigned long long allocations = c->plugin->mem.allocations;
    bool ok = run(c->plugin, "hud draw", BUDGET_CALL, draw_element, &error) && job.ok;
    classic_allocations += c->plugin->mem.allocations - allocations;
    in_draw = classic_pass = false;
    double ms = (now_seconds() - start) * 1000.0;
    unsigned now = hud::vsync(); // (a game frame is 2-3 vsyncs: its elements' draws add up)
    if (now != classic_frame_vsync) {
        classic_frame_vsync = now;
        classic_ms[classic_frames++ % CLASSIC_RING] = ms;
    } else {
        classic_ms[(classic_frames + CLASSIC_RING - 1) % CLASSIC_RING] += ms;
    }
    hud::KindStats& s = hud::stats(e->f.kind);
    if (!ok && parity) {
        ++s.faults;
        claim_fault(c, e->f.kind, "classic draw (parity)");
        if (why)
            *why = "its classic draw failed";
        return -1;
    }
    if (!ok) {
        ++s.faults;
        claim_fault(c, e->f.kind, "draw");
        e->suppressed = false; // this element: the game's packets stay
        --s.suppressed, ++s.fallback;
        return -1;
    }
    ++s.drawn;
    ++e->drawn;
    return classic_used;
}

namespace {
std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string num(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", v);
    return buffer;
}

std::string parts_json(const pc_hud_fields& f)
{
    std::string out = "[";
    for (int i = 0; i < f.part_count && i < PC_HUD_PARTS; ++i)
        out += std::string(i ? "," : "") + "{\"x\":" + num(f.parts[i].x) + ",\"y\":" + num(f.parts[i].y) + ",\"w\":"
            + num(f.parts[i].w) + ",\"h\":" + num(f.parts[i].h) + ",\"text\":" + q(f.parts[i].text) + "}";
    return out + "]";
}

std::string element_json(const hud::Element& e)
{
    const Claim* c = owner(e.f.kind);
    std::string icons;
    if (e.f.kind == PC_HUD_ABILITY_ICONS)
        icons = ",\"animation\":" + std::to_string(e.f.ability_animation)
            + ",\"offset\":" + std::to_string(e.f.ability_offset)
            + ",\"group\":" + std::to_string(e.f.ability_group)
            + ",\"availability\":" + std::to_string(e.f.ability_availability);
    if (e.f.kind == PC_HUD_IMAGE)
        icons = ",\"image\":" + std::to_string(e.f.image_index) + ",\"drawer\":" + std::to_string(e.f.image_mode);
    if (e.f.kind == PC_HUD_BOX_TEXT)
        icons = ",\"box\":" + std::to_string(e.f.box_id);
    if (e.f.kind == PC_HUD_TEXT_BAND)
        icons = ",\"box\":" + std::to_string(e.f.box_id) + ",\"top\":" + std::to_string(e.f.band_top)
            + ",\"edge\":" + std::to_string(e.f.band_edge) + ",\"lines\":" + std::to_string(e.f.band_lines)
            + ",\"settled\":" + std::to_string(e.f.band_settled) + ",\"icon\":" + std::to_string(e.f.band_icon);
    if (e.f.kind == PC_HUD_TEXT_FRAME)
        icons = ",\"box\":" + std::to_string(e.f.box_id) + ",\"box_x\":" + std::to_string(e.f.box_x)
            + ",\"box_y\":" + std::to_string(e.f.box_y) + ",\"box_w\":" + std::to_string(e.f.box_w)
            + ",\"box_h\":" + std::to_string(e.f.box_h) + ",\"shape\":" + std::to_string(e.f.box_shape)
            + ",\"flip\":" + std::to_string(e.f.box_flip) + ",\"tail\":" + std::to_string(e.f.box_tail);
    if (e.f.kind == PC_HUD_EMOTE)
        icons = std::string(",\"draw\":") + (e.f.emote_draw == 1 ? "\"balloon\"" : e.f.emote_draw == 2 ? "\"arrow\"" : "\"far\"")
            + ",\"emote\":" + std::to_string(e.f.emote_kind)
            + ",\"side\":" + std::to_string(e.f.emote_side) + ",\"z\":" + std::to_string(e.f.emote_z)
            + ",\"direction\":" + std::to_string(e.f.emote_direction) + ",\"frame\":" + std::to_string(e.f.emote_frame)
            + ",\"call_x\":" + std::to_string(e.f.call_x) + ",\"call_y\":" + std::to_string(e.f.call_y);
    if (e.f.kind == PC_HUD_TARGET_MARKER)
        icons = std::string(",\"lock\":") + (e.f.marker_lock ? "true" : "false")
            + ",\"step\":" + std::to_string(e.f.marker_step);
    if (e.f.kind == PC_HUD_TARGET_LINE)
        icons = ",\"row_x\":" + std::to_string(e.f.row_x) + ",\"row_y\":" + std::to_string(e.f.row_y);
    if (e.f.kind == PC_HUD_TARGET_ROW)
        icons = std::string(",\"selected\":") + (e.f.row_selected ? "true" : "false")
            + ",\"fade\":" + std::to_string(e.f.row_fade) + ",\"bank\":" + std::to_string(e.f.row_bank);
    if (e.f.kind == PC_HUD_TIMER)
        icons = ",\"minutes\":" + std::to_string(e.f.minutes) + ",\"seconds\":" + std::to_string(e.f.seconds)
            + ",\"hundredths\":" + std::to_string(e.f.hundredths);
    if (e.f.kind == PC_HUD_ACTOR_WORD)
        icons = std::string(",\"word\":") + (e.f.word_miss ? "\"miss\"" : "\"label\"")
            + ",\"label\":" + std::to_string(e.f.word_label) + ",\"lowered\":" + (e.f.word_lowered ? "true" : "false");
    if (e.f.kind == PC_HUD_BATTLE_MODE)
        icons = ",\"step\":" + std::to_string(e.f.battle_step)
            + ",\"pulse\":" + std::to_string(e.f.battle_pulse)
            + ",\"slide\":" + std::to_string(e.f.slide);
    if (e.f.kind == PC_HUD_STATUS_ICONS) {
        icons = ",\"previous\":" + std::to_string(e.f.icon_previous)
            + ",\"current\":" + std::to_string(e.f.icon_current)
            + ",\"next\":" + std::to_string(e.f.icon_next)
            + ",\"count\":" + std::to_string(e.f.icon_count)
            + ",\"selected\":" + std::to_string(e.f.icon_selected)
            + ",\"offset\":" + std::to_string(e.f.icon_offset)
            + ",\"slide\":" + std::to_string(e.f.slide);
    }
    std::string figure;
    if (e.f.kind == PC_HUD_FIGURE) {
        figure = ",\"stance\":" + std::to_string(e.f.stance);
        for (int i = 0; i < 5; ++i)
            figure += ",\"limb_" + std::to_string(i) + "_status\":" + std::to_string(e.f.limb_status[i]);
    }
    return "{\"id\":" + std::to_string(e.id) + ",\"kind\":" + q(hud::kind_name(e.f.kind)) + ",\"text\":" + q(e.f.text)
        + ",\"x\":" + num(e.f.x) + ",\"y\":" + num(e.f.y) + ",\"w\":" + num(e.f.w) + ",\"h\":" + num(e.f.h)
        + ",\"rgb\":" + std::to_string(e.f.rgb) + ",\"timing\":" + q(hud::timing_name(e.f.timing))
        + ",\"variant\":" + std::to_string(e.f.variant) + ",\"value\":" + std::to_string(e.f.value)
        + ",\"sign\":" + std::to_string(e.f.sign) + ",\"pool\":" + q(hud::pool_name(e.f.pool))
        + ",\"stat\":" + std::to_string(e.f.stat) + ",\"slot\":" + std::to_string(e.f.slot)
        + ",\"bounce\":" + num(e.f.bounce) + ",\"rise\":" + std::to_string(e.f.rise) + ",\"unit\":" + num(e.f.unit) + ",\"anchor\":" + std::to_string(e.f.anchor)
        + ",\"call_x\":" + std::to_string(e.f.call_x) + ",\"call_y\":" + std::to_string(e.f.call_y)
        + (e.f.kind == PC_HUD_GAUGES ? ",\"hp\":" + std::to_string(e.f.hp) + ",\"max_hp\":" + std::to_string(e.f.max_hp)
                    + ",\"mp\":" + std::to_string(e.f.mp) + ",\"max_mp\":" + std::to_string(e.f.max_mp)
                    + ",\"risk\":" + std::to_string(e.f.risk) + ",\"slide\":" + std::to_string(e.f.slide)
                                     : std::string())
        + figure + icons + ",\"parts\":" + parts_json(e.f) + ",\"age\":" + std::to_string(e.f.age)
        + ",\"life\":" + std::to_string(e.f.life) + ",\"held\":" + (e.held ? "true" : "false")
        + ",\"synthetic\":" + (e.synthetic ? "true" : "false") + ",\"live\":" + (hud::live(e) ? "true" : "false")
        + ",\"suppressed\":" + (e.suppressed ? "true" : "false") + ",\"drawn\":" + std::to_string(e.drawn)
        + ",\"claimed_by\":" + (c ? q(c->plugin->id()) : "null") + ",\"age_vsyncs\":"
        + std::to_string(hud::vsync() - e.first_seen) + "}";
}

// A ring's mean, p99 and max (ms).
struct Summary {
    double mean = 0, p99 = 0, max = 0;
};
Summary summary(const double* ring, unsigned count, unsigned size)
{
    std::vector<double> ms(ring, ring + std::min(count, size));
    std::sort(ms.begin(), ms.end());
    Summary out;
    for (double v : ms)
        out.mean += v;
    if (ms.empty())
        return out;
    out.mean /= ms.size();
    out.p99 = ms[std::min(ms.size() - 1, (size_t)(ms.size() * 0.99))];
    out.max = ms.back();
    return out;
}

std::string stats_json()
{
    Summary modern = summary(frame_ms, frame_count, FRAME_RING);
    Summary classic = summary(classic_ms, classic_frames, CLASSIC_RING);
    double mean = modern.mean, p99 = modern.p99;
    std::string kinds;
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        const hud::KindStats& s = hud::stats(k);
        const Claim* c = owner(k);
        kinds += std::string(k > 1 ? "," : "") + q(hud::kind_name(k)) + ":{\"seen\":" + std::to_string(s.seen)
            + ",\"suppressed\":" + std::to_string(s.suppressed) + ",\"fallback\":" + std::to_string(s.fallback)
            + ",\"drawn\":" + std::to_string(s.drawn) + ",\"faults\":" + std::to_string(s.faults)
            + ",\"claimed_by\":" + (c ? q(c->plugin->id()) : "null") + ",\"claim_failed\":"
            + (c && claim_down(c) ? "true" : "false") + ",\"honoured\":" + (pc_hud_honoured(k) ? "true" : "false") + "}";
    }
    std::string cmds;
    size_t memory = 0;
    for (int i = 0; i < command_used && i < 16; ++i) {
        const Command& c = command_buffer[i];
        cmds += std::string(i ? "," : "") + "{\"type\":"
            + q(c.type == hud::CMD_TEXT ? "text" : c.type == hud::CMD_GLYPH ? "glyph" : "rect")
            + ",\"x\":" + num(c.x) + ",\"y\":" + num(c.y) + ",\"color\":" + std::to_string(c.color);
        if (c.type == hud::CMD_TEXT) {
            const Style& st = style_slots[c.style].style;
            cmds += ",\"text\":" + q(std::string(arena + c.text, c.length)) + ",\"font\":" + q(FONTS[st.font])
                + ",\"size\":" + num(st.size) + ",\"cap\":" + num(st.cap) + ",\"stroke\":" + num(st.stroke)
                + ",\"fit\":" + (c.fit ? "true" : "false") + ",\"w\":" + num(c.w) + ",\"h\":" + num(c.h);
        } else if (c.type == hud::CMD_GLYPH) {
            cmds += ",\"button\":" + q(BUTTONS[c.button]) + ",\"w\":" + num(c.w) + ",\"h\":" + num(c.h);
        } else {
            cmds += ",\"w\":" + num(c.w) + ",\"h\":" + num(c.h);
        }
        cmds += "}";
    }
    static const char* const CLASSIC_TYPES[] = { "text", "rect", "gradient", "glyph", "sprite", "window", "cursor",
        "bar", "text_raw", "value", "digit", "frame", "band", "char", "image", "plate", "leader", "quad" };
    unsigned panel_first = 0, panel_last = 0; // the classic panels' packets of the last game frame
    if (pc_hud_panel_range)
        pc_hud_panel_range(&panel_first, &panel_last);
    auto classic_json = [&](const pc_hud_classic_cmd* buffer, int used) {
    std::string classic_cmds;
    for (int i = 0; i < used && i < 16; ++i) {
        const pc_hud_classic_cmd& c = buffer[i];
        classic_cmds += std::string(i ? "," : "") + "{\"type\":"
            + q(CLASSIC_TYPES[c.type >= 0 && c.type <= PC_HUD_CMD_QUAD ? c.type : 0]) + ",\"x\":"
            + std::to_string(c.x) + ",\"y\":" + std::to_string(c.y) + ",\"w\":" + std::to_string(c.w) + ",\"h\":"
            + std::to_string(c.h) + ",\"color\":" + std::to_string(c.color)
            + (c.type == PC_HUD_CMD_TEXT || c.type == PC_HUD_CMD_TEXT_RAW
                    ? ",\"text\":" + q(std::string(c.text, (size_t)c.length)) + ",\"font\":" + q(FONTS[c.font])
                        + ",\"align\":" + std::to_string(c.align) + ",\"word\":" + std::to_string(c.a)
                    : std::string())
            + "}";
    }
    return classic_cmds;
    };
    std::string classic_cmds = classic_json(classic_buffer, classic_used);
    std::string panel_cmds = classic_json(panel_snapshot.cmd, panel_snapshot.count);
    int styles_used = 0;
    for (const StyleSlot& s : style_slots)
        styles_used += s.used;
    for (int k = 1; k < PC_HUD_KINDS; ++k)
        if (const Claim* c = owner(k))
            memory += c->plugin->mem.used;
    return "{\"kinds\":{" + kinds + "},\"frames\":" + std::to_string(frame_count) + ",\"mean_ms\":" + num(mean)
        + ",\"p99_ms\":" + num(p99) + ",\"max_ms\":" + num(modern.max)
        + ",\"allocations_per_frame\":"
        + num(frame_count ? (double)pass_allocations / std::min<unsigned>(frame_count, ~0u) : 0)
        + ",\"classic_allocations_per_frame\":"
        + num(classic_frames ? (double)classic_allocations / classic_frames : 0)
        + ",\"budget_ms\":" + num(config.budget_ms)
        + ",\"events\":" + std::to_string(events_emitted) + ",\"classic_frames\":" + std::to_string(classic_frames) + ",\"classic_mean_ms\":" + num(classic.mean)
        + ",\"classic_p99_ms\":" + num(classic.p99) + ",\"classic_max_ms\":" + num(classic.max)
        + ",\"classic_commands\":" + std::to_string(classic_used)
        + ",\"render_mean_ms\":" + num(render_count ? render_total_ms / render_count : 0)
        + ",\"commands\":" + std::to_string(command_used) + ",\"text_bytes\":" + std::to_string(arena_used)
        + ",\"dropped\":" + std::to_string(dropped) + ",\"styles\":" + std::to_string(styles_used)
        + ",\"claim_memory_kb\":" + num(memory / 1024.0) + ",\"classic_override\":" + std::to_string(pc_hud_classic_override)
        + ",\"panel_packets\":{\"start\":" + std::to_string(panel_first) + ",\"end\":" + std::to_string(panel_last)
        + "},\"panel_last\":[" + panel_cmds + "],\"last\":[" + cmds + "],\"classic_last\":[" + classic_cmds + "]}";
}
} // namespace
} // namespace rl

extern "C" int pc_hud_classic_draw(const pc_hud_classic_cmd** out)
{
    *out = rl::classic_buffer;
    return rl::initialized() ? rl::classic_draw() : -1;
}
extern "C" int pc_hud_classic_parity_draw(const pc_hud_classic_cmd** out, const char** why)
{
    *out = rl::classic_buffer;
    *why = "Lua not running";
    return rl::initialized() ? rl::classic_draw(true, why) : -1;
}
extern "C" int pc_hud_classic_anchor(void) { return rl::classic_anchor; }
extern "C" int pc_hud_classic_panel(int* cursor, const pc_hud_classic_cmd** out, int* anchor)
{
    *out = rl::classic_buffer;
    return rl::initialized() && !rl::panels.empty() ? rl::panels_classic_next(cursor, anchor) : -1;
}

/* dev_overlay.cpp, every presented frame after the HUD and the menus, whatever is hidden: plugins' panels
 * over the picture (pc_gl_view's size). */
extern "C" void pc_hud_panels(float dt, int view_w, int view_h)
{
    if (!rl::initialized() || rl::panels.empty() || view_w <= 0 || view_h <= 0)
        return;
    rl::panel_dt = dt;
    float height = 240.0f, width = 240.0f * (float)view_w / (float)view_h;
    rl::panel_width = width;
    if (rl::panels_pass(width, height) > 0 && hud::render)
        hud::render(rl::command_buffer, rl::command_used, rl::arena);
}

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

namespace rl {
std::vector<ClaimInfo> hud_claims()
{
    std::vector<ClaimInfo> out;
    for (int k = 1; k < PC_HUD_KINDS; ++k)
        for (int i = 0; i < (int)claims[k].size() && PC_HUD_CLAIMABLE(k); ++i) {
            const Claim& c = claims[k][i];
            if (valid(c))
                out.push_back({ hud::kind_name(k), c.plugin, c.ref, c.failed, i + 1 == active[rl::look_now()][k] });
        }
    return out;
}
} // namespace rl

namespace hud {
Command* commands() { return rl::command_buffer; }
int command_count() { return rl::command_used; }
const char* text_arena() { return rl::arena; }
StyleSlot* styles() { return rl::style_slots; }
bool claimed(int kind)
{
    const rl::Claim* c = rl::owner(kind);
    return c && !rl::claim_down(c);
}
bool claimed_classic(int kind)
{
    const rl::Claim* c = rl::owner_for(kind, 1);
    return c && !rl::claim_down(c) && c->flavours == rl::FLAVOUR_CLASSIC;
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
        if (const Json* v = p.get("value"))
            args.value = (int)v->number;
        if (const Json* v = p.get("count"))
            args.value = (int)v->number;
        if (const Json* v = p.get("index")) // name_window: the action
            args.value = (int)v->number;
        if (const Json* v = p.get("message"))
            args.variant = (int)v->number;
        if (const Json* v = p.get("text"); v && v->type == Json::String)
            std::snprintf(args.text, sizeof args.text, "%s", v->string.c_str());
        if (const Json* v = p.get("sign"))
            args.sign = v->number < 0 ? -1 : 1;
        if (const Json* v = p.get("heal"))
            args.sign = v->boolean ? 1 : -1;
        const char* names[3] = { "pool", "stat", "slot" };
        const char* (*words[3])(int) = { hud::pool_name, hud::stat_word, hud::slot_word };
        int counts[3] = { 3, 13, 7 }, *into[3] = { &args.pool, &args.stat, &args.slot };
        for (int i = 0; i < 3; ++i)
            if (const Json* v = p.get(names[i])) {
                int at = v->type == Json::String ? hud::word_index(v->string.c_str(), words[i], counts[i])
                                                 : (int)v->number;
                if (at < 0 || at >= counts[i]) {
                    std::snprintf(out, (size_t)size, "%s: an unknown name or index", names[i]);
                    return -1;
                }
                *into[i] = at;
            }
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
            rl::classic_frames = 0;
            rl::pass_allocations = rl::classic_allocations = 0;
            rl::dropped = 0;
            rl::render_total_ms = 0;
            rl::render_count = 0;
        }
        reply = rl::stats_json();
    } else if (action == "bench") { // the gallery: {op save / list / load, name}
        const Json* op = p.get("op");
        const Json* name = p.get("name");
        reply = rl::bench_rpc(op && op->type == Json::String ? op->string : "list",
            name && name->type == Json::String ? name->string : "", &error);
        if (reply.empty()) {
            std::snprintf(out, (size_t)size, "%s", error.c_str());
            return -1;
        }
    } else if (action == "set") {
        if (const Json* v = p.get("classic_override"))
            pc_hud_classic_override = v->type == Json::Bool ? v->boolean : v->number != 0;
        reply = rl::stats_json();
    } else {
        std::snprintf(out, (size_t)size, "unknown action '%s' (list, show, clear, stats, bench, set)", action.c_str());
        return -1;
    }
    if ((int)reply.size() >= size) {
        std::snprintf(out, (size_t)size, "reply too long");
        return -1;
    }
    std::memcpy(out, reply.c_str(), reply.size() + 1);
    return (int)reply.size();
}
