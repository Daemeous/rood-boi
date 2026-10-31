// rood.imgui (the dev.tools grant; pc/docs/Extensions.md "Developer tool windows"; plan
// pc/docs/plans/2026-10-27-rood-imgui.md): Dear ImGui for developer tool windows written in Lua.
// The names are Dear ImGui's (ImGui::SliderFloat is imgui.SliderFloat, ImGuiWindowFlags_MenuBar is
// imgui.WindowFlags_MenuBar, ImGuiCol_Text is imgui.Col_Text, ImGuiKey_Enter is imgui.Key_Enter), so
// its documentation applies; values go in and come back Lua's way:
//
//   local changed, v = imgui.SliderFloat("Strength", v, 0, 1)
//   local changed, text = imgui.InputText("Name", text)
//   local changed, rgb = imgui.ColorEdit3("Tint", rgb)      -- {r, g, b}, a new table
//   local changed, i = imgui.Combo("Preset", i, names)       -- i: 1-based, as names[i]
//   local visible, open = imgui.BeginTabItem("Tab", open)
//
// Text is never a format (imgui.Text("100%") shows 100%: use string.format). Every call answers only
// inside its plugin's window draw (lua_dev_tools.cpp), and every Begin/Push is checked against its
// End/Pop here, before Dear ImGui sees it: a mismatched End raises a Lua error instead of Dear ImGui's
// assertion, and the window's draw is then rewound (ErrorRecoveryTryToRecoverState) and shown failed.
// Key queries answer only while the window has the keyboard: focused, nothing being typed, the camera
// not flying (as the Material review's own keys).
#include "imgui.h"
#include "imgui_internal.h"
#include "lua_imgui.h"

#include <cstring>
#include <string>
#include <vector>

namespace rl {
namespace imgui_host {
namespace {
// What is open: each Begin*/Push* that needs its End*/Pop*.
enum Scope : unsigned char {
    S_WINDOW, S_CHILD, S_ID, S_COLOR, S_VAR, S_DISABLED, S_TREE, S_COMBO, S_TABLE, S_TABBAR, S_TABITEM,
    S_TOOLTIP, S_ITEMWIDTH, S_GROUP, S_POPUP, S_MENU, S_MENUBAR, S_LISTBOX, S_CLIPPER, S_CLIPRECT
};
const char* const BEGIN_NAMES[] = { "Begin", "BeginChild", "PushID", "PushStyleColor", "PushStyleVar",
    "BeginDisabled", "TreeNode", "BeginCombo", "BeginTable", "BeginTabBar", "BeginTabItem", "BeginTooltip",
    "PushItemWidth", "BeginGroup", "BeginPopup", "BeginMenu", "BeginMenuBar", "BeginListBox",
    "ListClipper_Begin", "PushClipRect" };
const size_t MAX_SCOPES = 256;
const size_t MAX_TEXT = 1u << 16; // InputText's longest text

Plugin* current;
bool keys_ok;
std::vector<unsigned char> scopes;
ImGuiListClipper* clipper; // ListClipper_Begin's, until its Step returns false (one at a time)

Plugin* ready(lua_State* L)
{
    Plugin* p = plugin_of(L);
    if (p != current || !p)
        luaL_error(L, "rood.imgui: only inside its own window's draw (rood.dev.window)");
    return p;
}
void push_scope(lua_State* L, Scope s)
{
    if (scopes.size() >= MAX_SCOPES)
        luaL_error(L, "rood.imgui.%s: more than %d scopes open", BEGIN_NAMES[s], (int)MAX_SCOPES);
    scopes.push_back(s);
}
// The innermost scope must be s (its End/Pop is being called); raises otherwise.
void pop_scope(lua_State* L, Scope s, const char* end)
{
    if (scopes.empty())
        luaL_error(L, "rood.imgui.%s without %s", end, BEGIN_NAMES[s]);
    if (scopes.back() != s)
        luaL_error(L, "rood.imgui.%s, but the innermost open scope is %s", end, BEGIN_NAMES[scopes.back()]);
    scopes.pop_back();
}
// s is open in the current window (a table's row and column calls, a tab item in its bar).
void need_scope(lua_State* L, Scope s, const char* what)
{
    for (size_t i = scopes.size(); i-- > 0;) {
        if (scopes[i] == s)
            return;
        unsigned char k = scopes[i];
        if (k == S_WINDOW || k == S_CHILD || k == S_POPUP || k == S_TOOLTIP || k == S_COMBO || k == S_MENU
            || k == S_TABITEM || k == S_TABLE || k == S_LISTBOX)
            break;
    }
    luaL_error(L, "rood.imgui.%s outside %s", what, BEGIN_NAMES[s]);
}

const char* label(lua_State* L, int idx) { return luaL_checkstring(L, idx); }
int flags(lua_State* L, int idx) { return (int)luaL_optinteger(L, idx, 0); }
float num(lua_State* L, int idx) { return (float)luaL_checknumber(L, idx); }
float optnum(lua_State* L, int idx, float d) { return (float)luaL_optnumber(L, idx, d); }
ImVec2 size_args(lua_State* L, int idx) { return ImVec2(optnum(L, idx, 0), optnum(L, idx + 1, 0)); }
// {r, g, b[, a]} (0..1): a colour; n components read (a: 1 when absent).
void color_arg(lua_State* L, int idx, float* out, int n)
{
    luaL_checktype(L, idx, LUA_TTABLE);
    for (int k = 0; k < n; ++k) {
        lua_rawgeti(L, idx, k + 1);
        out[k] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : (k == 3 ? 1.0f : 0.0f);
        lua_pop(L, 1);
    }
}
ImVec4 vec4_arg(lua_State* L, int idx)
{
    float c[4] = { 0, 0, 0, 1 };
    color_arg(L, idx, c, 4);
    return ImVec4(c[0], c[1], c[2], c[3]);
}
void push_floats(lua_State* L, const float* v, int n)
{
    lua_createtable(L, n, 0);
    for (int k = 0; k < n; ++k) {
        lua_pushnumber(L, v[k]);
        lua_rawseti(L, -2, k + 1);
    }
}
int boolean(lua_State* L, bool b)
{
    lua_pushboolean(L, b);
    return 1;
}
int changed_number(lua_State* L, bool changed, double v)
{
    lua_pushboolean(L, changed);
    lua_pushnumber(L, v);
    return 2;
}
int changed_integer(lua_State* L, bool changed, int v)
{
    lua_pushboolean(L, changed);
    lua_pushinteger(L, v);
    return 2;
}

// ---- windows ----
// Begin(name[, open[, flags]]) -> visible, open: End always follows (Dear ImGui's rule).
int Begin(lua_State* L)
{
    ready(L);
    const char* name = label(L, 1);
    bool has_open = !lua_isnoneornil(L, 2);
    bool open = has_open ? lua_toboolean(L, 2) != 0 : true;
    int f = flags(L, 3);
    push_scope(L, S_WINDOW);
    bool visible = ImGui::Begin(name, has_open ? &open : nullptr, f);
    lua_pushboolean(L, visible);
    if (has_open)
        lua_pushboolean(L, open);
    else
        lua_pushnil(L);
    return 2;
}
int End(lua_State* L)
{
    ready(L);
    pop_scope(L, S_WINDOW, "End");
    ImGui::End();
    return 0;
}
// BeginChild(id[, w, h[, child_flags[, window_flags]]]) -> visible; EndChild always follows.
int BeginChild(lua_State* L)
{
    ready(L);
    const char* id = label(L, 1);
    ImVec2 size = size_args(L, 2);
    int cf = flags(L, 4), wf = flags(L, 5);
    push_scope(L, S_CHILD);
    return boolean(L, ImGui::BeginChild(id, size, cf, wf));
}
int EndChild(lua_State* L)
{
    ready(L);
    pop_scope(L, S_CHILD, "EndChild");
    ImGui::EndChild();
    return 0;
}
int SetNextWindowSize(lua_State* L)
{
    ready(L);
    ImGui::SetNextWindowSize(ImVec2(num(L, 1), num(L, 2)), flags(L, 3));
    return 0;
}
int SetNextWindowPos(lua_State* L)
{
    ready(L);
    ImGui::SetNextWindowPos(ImVec2(num(L, 1), num(L, 2)), flags(L, 3));
    return 0;
}
int IsWindowFocused(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsWindowFocused(flags(L, 1)));
}
int IsWindowHovered(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsWindowHovered(flags(L, 1)));
}

// ---- text ----
int Text(lua_State* L)
{
    ready(L);
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    ImGui::TextUnformatted(s, s + n);
    return 0;
}
int TextWrapped(lua_State* L)
{
    ready(L);
    ImGui::TextWrapped("%s", luaL_checkstring(L, 1));
    return 0;
}
int TextDisabled(lua_State* L)
{
    ready(L);
    ImGui::TextDisabled("%s", luaL_checkstring(L, 1));
    return 0;
}
// TextColored({r, g, b[, a]}, text)
int TextColored(lua_State* L)
{
    ready(L);
    ImVec4 c = vec4_arg(L, 1);
    ImGui::TextColored(c, "%s", luaL_checkstring(L, 2));
    return 0;
}
int LabelText(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    ImGui::LabelText(l, "%s", luaL_checkstring(L, 2));
    return 0;
}
int BulletText(lua_State* L)
{
    ready(L);
    ImGui::BulletText("%s", luaL_checkstring(L, 1));
    return 0;
}
int SeparatorText(lua_State* L)
{
    ready(L);
    ImGui::SeparatorText(label(L, 1));
    return 0;
}

// ---- buttons and toggles ----
int Button(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    return boolean(L, ImGui::Button(l, size_args(L, 2)));
}
int SmallButton(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::SmallButton(label(L, 1)));
}
// Checkbox(label, v) -> changed, v
int Checkbox(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    bool changed = ImGui::Checkbox(l, &v);
    lua_pushboolean(L, changed);
    lua_pushboolean(L, v);
    return 2;
}
// RadioButton(label, active) -> clicked
int RadioButton(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    return boolean(L, ImGui::RadioButton(l, lua_toboolean(L, 2) != 0));
}
// Selectable(label[, selected[, flags[, w, h]]]) -> clicked
int Selectable(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    bool selected = lua_toboolean(L, 2) != 0;
    int f = flags(L, 3);
    return boolean(L, ImGui::Selectable(l, selected, f, size_args(L, 4)));
}
// ProgressBar(fraction[, w, h[, overlay]])
int ProgressBar(lua_State* L)
{
    ready(L);
    float f = num(L, 1);
    ImVec2 size(optnum(L, 2, -FLT_MIN), optnum(L, 3, 0));
    ImGui::ProgressBar(f, size, luaL_optstring(L, 4, nullptr));
    return 0;
}
int Bullet(lua_State* L)
{
    ready(L);
    ImGui::Bullet();
    return 0;
}

// ---- numbers ----
// SliderFloat(label, v, min, max[, format[, flags]]) -> changed, v
int SliderFloat(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    float v = num(L, 2), lo = num(L, 3), hi = num(L, 4);
    const char* format = luaL_optstring(L, 5, "%.3f");
    bool changed = ImGui::SliderFloat(l, &v, lo, hi, format, flags(L, 6));
    return changed_number(L, changed, v);
}
int SliderInt(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    int v = (int)luaL_checkinteger(L, 2), lo = (int)luaL_checkinteger(L, 3), hi = (int)luaL_checkinteger(L, 4);
    const char* format = luaL_optstring(L, 5, "%d");
    bool changed = ImGui::SliderInt(l, &v, lo, hi, format, flags(L, 6));
    return changed_integer(L, changed, v);
}
// DragFloat(label, v[, speed[, min, max[, format[, flags]]]]) -> changed, v
int DragFloat(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    float v = num(L, 2), speed = optnum(L, 3, 1), lo = optnum(L, 4, 0), hi = optnum(L, 5, 0);
    const char* format = luaL_optstring(L, 6, "%.3f");
    bool changed = ImGui::DragFloat(l, &v, speed, lo, hi, format, flags(L, 7));
    return changed_number(L, changed, v);
}
int DragInt(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    int v = (int)luaL_checkinteger(L, 2);
    float speed = optnum(L, 3, 1);
    int lo = (int)luaL_optinteger(L, 4, 0), hi = (int)luaL_optinteger(L, 5, 0);
    const char* format = luaL_optstring(L, 6, "%d");
    bool changed = ImGui::DragInt(l, &v, speed, lo, hi, format, flags(L, 7));
    return changed_integer(L, changed, v);
}
// DragFloat2/3/4(label, {x, y, ...}[, speed[, min, max[, format[, flags]]]]) -> changed, a new table
template <int N> int DragFloatN(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    float v[4] = { 0, 0, 0, 0 };
    color_arg(L, 2, v, N);
    float speed = optnum(L, 3, 1), lo = optnum(L, 4, 0), hi = optnum(L, 5, 0);
    const char* format = luaL_optstring(L, 6, "%.3f");
    bool changed = ImGui::DragScalarN(l, ImGuiDataType_Float, v, N, speed, &lo, &hi, format, flags(L, 7));
    lua_pushboolean(L, changed);
    push_floats(L, v, N);
    return 2;
}
// InputFloat(label, v[, step, step_fast[, format[, flags]]]) -> changed, v
int InputFloat(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    float v = num(L, 2), step = optnum(L, 3, 0), fast = optnum(L, 4, 0);
    const char* format = luaL_optstring(L, 5, "%.3f");
    bool changed = ImGui::InputFloat(l, &v, step, fast, format, flags(L, 6));
    return changed_number(L, changed, v);
}
int InputInt(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    int v = (int)luaL_checkinteger(L, 2), step = (int)luaL_optinteger(L, 3, 1), fast = (int)luaL_optinteger(L, 4, 100);
    bool changed = ImGui::InputInt(l, &v, step, fast, flags(L, 5));
    return changed_integer(L, changed, v);
}

// ---- text input ----
// InputText(label, text[, flags]) -> changed, text; InputTextMultiline(label, text[, w, h[, flags]]).
int input_text(lua_State* L, bool multiline)
{
    ready(L);
    const char* l = label(L, 1);
    size_t n = 0;
    const char* text = lua_isnoneornil(L, 2) ? "" : luaL_checklstring(L, 2, &n);
    if (n >= MAX_TEXT)
        return luaL_error(L, "rood.imgui.InputText: text longer than %d bytes", (int)MAX_TEXT - 1);
    std::vector<char> buffer(std::max<size_t>(n + 1024, 256));
    std::memcpy(buffer.data(), text, n);
    bool changed = multiline
        ? ImGui::InputTextMultiline(l, buffer.data(), buffer.size(), size_args(L, 3), flags(L, 5))
        : ImGui::InputText(l, buffer.data(), buffer.size(), flags(L, 3));
    lua_pushboolean(L, changed);
    lua_pushstring(L, buffer.data());
    return 2;
}
int InputText(lua_State* L) { return input_text(L, false); }
int InputTextMultiline(lua_State* L) { return input_text(L, true); }

// ---- choices ----
// Combo(label, current, items[, height_in_items]) -> changed, current (1-based: items[current]; 0: none)
int Combo(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    int current = (int)luaL_checkinteger(L, 2) - 1;
    luaL_checktype(L, 3, LUA_TTABLE);
    int height = (int)luaL_optinteger(L, 4, -1);
    lua_Integer count = (lua_Integer)lua_rawlen(L, 3);
    if (count > 4096)
        return luaL_error(L, "rood.imgui.Combo: more than 4096 items");
    std::vector<const char*> items((size_t)count);
    luaL_checkstack(L, (int)count + 4, "rood.imgui.Combo: items");
    for (lua_Integer k = 1; k <= count; ++k) {
        lua_rawgeti(L, 3, k);
        items[(size_t)k - 1] = luaL_tolstring(L, -1, nullptr); // kept on the stack until the call returns
        lua_remove(L, -2);
    }
    bool changed = ImGui::Combo(l, &current, items.data(), (int)count, height);
    lua_settop(L, 4);
    return changed_integer(L, changed, current + 1);
}
// BeginCombo(label, preview[, flags]) -> open; EndCombo only when it opened.
int BeginCombo(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    const char* preview = luaL_optstring(L, 2, "");
    if (!ImGui::BeginCombo(l, preview, flags(L, 3)))
        return boolean(L, false);
    scopes.push_back(S_COMBO); // (after the call: it is open now, whatever the push costs)
    return boolean(L, true);
}
int EndCombo(lua_State* L)
{
    ready(L);
    pop_scope(L, S_COMBO, "EndCombo");
    ImGui::EndCombo();
    return 0;
}
// BeginListBox(label[, w, h]) -> open; EndListBox only when it opened.
int BeginListBox(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    if (!ImGui::BeginListBox(l, size_args(L, 2)))
        return boolean(L, false);
    scopes.push_back(S_LISTBOX);
    return boolean(L, true);
}
int EndListBox(lua_State* L)
{
    ready(L);
    pop_scope(L, S_LISTBOX, "EndListBox");
    ImGui::EndListBox();
    return 0;
}

// ---- colours ----
// ColorEdit3(label, {r, g, b}[, flags]) -> changed, {r, g, b}; ColorEdit4 with a.
template <int N> int ColorEditN(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    float c[4] = { 0, 0, 0, 1 };
    color_arg(L, 2, c, N);
    bool changed = N == 3 ? ImGui::ColorEdit3(l, c, flags(L, 3)) : ImGui::ColorEdit4(l, c, flags(L, 3));
    lua_pushboolean(L, changed);
    push_floats(L, c, N);
    return 2;
}

// ---- layout ----
int SameLine(lua_State* L)
{
    ready(L);
    ImGui::SameLine(optnum(L, 1, 0), optnum(L, 2, -1));
    return 0;
}
int Separator(lua_State* L)
{
    ready(L);
    ImGui::Separator();
    return 0;
}
int Spacing(lua_State* L)
{
    ready(L);
    ImGui::Spacing();
    return 0;
}
int NewLine(lua_State* L)
{
    ready(L);
    ImGui::NewLine();
    return 0;
}
int Dummy(lua_State* L)
{
    ready(L);
    ImGui::Dummy(ImVec2(num(L, 1), num(L, 2)));
    return 0;
}
int Indent(lua_State* L)
{
    ready(L);
    ImGui::Indent(optnum(L, 1, 0));
    return 0;
}
int Unindent(lua_State* L)
{
    ready(L);
    ImGui::Unindent(optnum(L, 1, 0));
    return 0;
}
int BeginGroup(lua_State* L)
{
    ready(L);
    push_scope(L, S_GROUP);
    ImGui::BeginGroup();
    return 0;
}
int EndGroup(lua_State* L)
{
    ready(L);
    pop_scope(L, S_GROUP, "EndGroup");
    ImGui::EndGroup();
    return 0;
}
int PushItemWidth(lua_State* L)
{
    ready(L);
    float w = num(L, 1);
    push_scope(L, S_ITEMWIDTH);
    ImGui::PushItemWidth(w);
    return 0;
}
int PopItemWidth(lua_State* L)
{
    ready(L);
    pop_scope(L, S_ITEMWIDTH, "PopItemWidth");
    ImGui::PopItemWidth();
    return 0;
}
int SetNextItemWidth(lua_State* L)
{
    ready(L);
    ImGui::SetNextItemWidth(num(L, 1));
    return 0;
}
int AlignTextToFramePadding(lua_State* L)
{
    ready(L);
    ImGui::AlignTextToFramePadding();
    return 0;
}
int GetContentRegionAvail(lua_State* L)
{
    ready(L);
    ImVec2 v = ImGui::GetContentRegionAvail();
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    return 2;
}
int CalcTextSize(lua_State* L)
{
    ready(L);
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    ImVec2 v = ImGui::CalcTextSize(s, s + n);
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    return 2;
}
int GetFontSize(lua_State* L)
{
    ready(L);
    lua_pushnumber(L, ImGui::GetFontSize());
    return 1;
}
int GetFrameHeight(lua_State* L)
{
    ready(L);
    lua_pushnumber(L, ImGui::GetFrameHeight());
    return 1;
}

// ---- scrolling and long lists ----
int SetScrollHereY(lua_State* L)
{
    ready(L);
    ImGui::SetScrollHereY(optnum(L, 1, 0.5f));
    return 0;
}
int SetScrollY(lua_State* L)
{
    ready(L);
    ImGui::SetScrollY(num(L, 1));
    return 0;
}
int GetScrollY(lua_State* L)
{
    ready(L);
    lua_pushnumber(L, ImGui::GetScrollY());
    return 1;
}
int GetTextLineHeightWithSpacing(lua_State* L)
{
    ready(L);
    lua_pushnumber(L, ImGui::GetTextLineHeightWithSpacing());
    return 1;
}
// GetStyleColorVec4(imgui.Col_*) -> {r, g, b, a}
int GetStyleColorVec4(lua_State* L)
{
    ready(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= ImGuiCol_COUNT)
        return luaL_error(L, "rood.imgui.GetStyleColorVec4: no colour %d (imgui.Col_*)", idx);
    const ImVec4& c = ImGui::GetStyleColorVec4(idx);
    float v[4] = { c.x, c.y, c.z, c.w };
    push_floats(L, v, 4);
    return 1;
}
// Dear ImGui's ImGuiListClipper, as cimgui names it: only the rows in view are drawn.
//   imgui.ListClipper_Begin(#rows[, row_height])
//   while true do
//     local more, first, last = imgui.ListClipper_Step()   -- 1-based, inclusive
//     if not more then break end
//     for i = first, last do ... end
//   end
int ListClipper_Begin(lua_State* L)
{
    ready(L);
    int count = (int)luaL_checkinteger(L, 1);
    float height = optnum(L, 2, -1);
    if (clipper)
        return luaL_error(L, "rood.imgui.ListClipper_Begin: one clipper at a time (Step until it returns false)");
    if (count < 0)
        return luaL_error(L, "rood.imgui.ListClipper_Begin: a count of %d", count);
    push_scope(L, S_CLIPPER);
    clipper = new ImGuiListClipper();
    clipper->Begin(count, height);
    return 0;
}
int ListClipper_Step(lua_State* L)
{
    ready(L);
    if (!clipper || scopes.empty() || scopes.back() != S_CLIPPER)
        return luaL_error(L, "rood.imgui.ListClipper_Step without ListClipper_Begin (or with a scope opened since)");
    if (!clipper->Step()) {
        delete clipper; // (Step ended it)
        clipper = nullptr;
        scopes.pop_back();
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    lua_pushinteger(L, clipper->DisplayStart + 1);
    lua_pushinteger(L, clipper->DisplayEnd);
    return 3;
}

// ---- trees ----
// TreeNode(label[, flags]) -> open; TreePop only when it opened (unless NoTreePushOnOpen).
int TreeNode(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    int f = flags(L, 2);
    bool open = ImGui::TreeNodeEx(l, f);
    if (open && !(f & ImGuiTreeNodeFlags_NoTreePushOnOpen))
        scopes.push_back(S_TREE);
    return boolean(L, open);
}
int TreePop(lua_State* L)
{
    ready(L);
    pop_scope(L, S_TREE, "TreePop");
    ImGui::TreePop();
    return 0;
}
// CollapsingHeader(label[, flags]) -> open (nothing to close)
int CollapsingHeader(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    return boolean(L, ImGui::CollapsingHeader(l, flags(L, 2)));
}
int SetNextItemOpen(lua_State* L)
{
    ready(L);
    ImGui::SetNextItemOpen(lua_toboolean(L, 1) != 0, flags(L, 2));
    return 0;
}

// ---- tables ----
// BeginTable(id, columns[, flags[, w, h]]) -> open; EndTable only when it opened.
int BeginTable(lua_State* L)
{
    ready(L);
    const char* id = label(L, 1);
    int columns = (int)luaL_checkinteger(L, 2);
    if (columns < 1 || columns > 64)
        return luaL_error(L, "rood.imgui.BeginTable: 1 to 64 columns");
    int f = flags(L, 3);
    ImVec2 size = size_args(L, 4);
    if (!ImGui::BeginTable(id, columns, f, size))
        return boolean(L, false);
    scopes.push_back(S_TABLE);
    return boolean(L, true);
}
int EndTable(lua_State* L)
{
    ready(L);
    pop_scope(L, S_TABLE, "EndTable");
    ImGui::EndTable();
    return 0;
}
// TableSetupColumn(label[, flags[, width]])
int TableSetupColumn(lua_State* L)
{
    ready(L);
    need_scope(L, S_TABLE, "TableSetupColumn");
    ImGui::TableSetupColumn(label(L, 1), flags(L, 2), optnum(L, 3, 0));
    return 0;
}
int TableSetupScrollFreeze(lua_State* L)
{
    ready(L);
    need_scope(L, S_TABLE, "TableSetupScrollFreeze");
    ImGui::TableSetupScrollFreeze((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2));
    return 0;
}
int TableHeadersRow(lua_State* L)
{
    ready(L);
    need_scope(L, S_TABLE, "TableHeadersRow");
    ImGui::TableHeadersRow();
    return 0;
}
// TableNextRow([flags[, min_height]])
int TableNextRow(lua_State* L)
{
    ready(L);
    need_scope(L, S_TABLE, "TableNextRow");
    ImGui::TableNextRow(flags(L, 1), optnum(L, 2, 0));
    return 0;
}
int TableNextColumn(lua_State* L)
{
    ready(L);
    need_scope(L, S_TABLE, "TableNextColumn");
    return boolean(L, ImGui::TableNextColumn());
}
// TableSetColumnIndex(n): 0-based, as Dear ImGui's columns
int TableSetColumnIndex(lua_State* L)
{
    ready(L);
    need_scope(L, S_TABLE, "TableSetColumnIndex");
    int n = (int)luaL_checkinteger(L, 1);
    if (n < 0 || n >= ImGui::TableGetColumnCount())
        return luaL_error(L, "rood.imgui.TableSetColumnIndex: no column %d (0-based)", n);
    return boolean(L, ImGui::TableSetColumnIndex(n));
}

// ---- tabs ----
int BeginTabBar(lua_State* L)
{
    ready(L);
    const char* id = label(L, 1);
    if (!ImGui::BeginTabBar(id, flags(L, 2)))
        return boolean(L, false);
    scopes.push_back(S_TABBAR);
    return boolean(L, true);
}
int EndTabBar(lua_State* L)
{
    ready(L);
    pop_scope(L, S_TABBAR, "EndTabBar");
    ImGui::EndTabBar();
    return 0;
}
// BeginTabItem(label[, open[, flags]]) -> selected, open; EndTabItem only when selected.
int BeginTabItem(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    if (scopes.empty() || scopes.back() != S_TABBAR)
        return luaL_error(L, "rood.imgui.BeginTabItem outside BeginTabBar");
    bool has_open = !lua_isnoneornil(L, 2);
    bool open = has_open ? lua_toboolean(L, 2) != 0 : true;
    bool selected = ImGui::BeginTabItem(l, has_open ? &open : nullptr, flags(L, 3));
    if (selected)
        scopes.push_back(S_TABITEM);
    lua_pushboolean(L, selected);
    if (has_open)
        lua_pushboolean(L, open);
    else
        lua_pushnil(L);
    return 2;
}
int EndTabItem(lua_State* L)
{
    ready(L);
    pop_scope(L, S_TABITEM, "EndTabItem");
    ImGui::EndTabItem();
    return 0;
}

// ---- tooltips, popups, menus ----
int BeginTooltip(lua_State* L)
{
    ready(L);
    if (!ImGui::BeginTooltip())
        return boolean(L, false);
    scopes.push_back(S_TOOLTIP);
    return boolean(L, true);
}
int BeginItemTooltip(lua_State* L)
{
    ready(L);
    if (!ImGui::BeginItemTooltip())
        return boolean(L, false);
    scopes.push_back(S_TOOLTIP);
    return boolean(L, true);
}
int EndTooltip(lua_State* L)
{
    ready(L);
    pop_scope(L, S_TOOLTIP, "EndTooltip");
    ImGui::EndTooltip();
    return 0;
}
int SetTooltip(lua_State* L)
{
    ready(L);
    ImGui::SetTooltip("%s", luaL_checkstring(L, 1));
    return 0;
}
// SetClipboardText(text): the system clipboard (Copy JSON buttons). IsItemToggledOpen() -> the last
// tree node's arrow toggled it (a click on its label selects instead).
int SetClipboardText(lua_State* L)
{
    ready(L);
    ImGui::SetClipboardText(luaL_checkstring(L, 1));
    return 0;
}
int IsItemToggledOpen(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsItemToggledOpen());
}
int SetItemTooltip(lua_State* L)
{
    ready(L);
    ImGui::SetItemTooltip("%s", luaL_checkstring(L, 1));
    return 0;
}
int OpenPopup(lua_State* L)
{
    ready(L);
    ImGui::OpenPopup(label(L, 1), flags(L, 2));
    return 0;
}
// BeginPopup(id[, flags]) -> open; EndPopup only when it opened.
int BeginPopup(lua_State* L)
{
    ready(L);
    const char* id = label(L, 1);
    if (!ImGui::BeginPopup(id, flags(L, 2)))
        return boolean(L, false);
    scopes.push_back(S_POPUP);
    return boolean(L, true);
}
// BeginPopupContextItem([id[, flags]]) -> open (right click on the last item)
int BeginPopupContextItem(lua_State* L)
{
    ready(L);
    const char* id = luaL_optstring(L, 1, nullptr);
    if (!ImGui::BeginPopupContextItem(id, lua_isnoneornil(L, 2) ? 1 : flags(L, 2)))
        return boolean(L, false);
    scopes.push_back(S_POPUP);
    return boolean(L, true);
}
int EndPopup(lua_State* L)
{
    ready(L);
    pop_scope(L, S_POPUP, "EndPopup");
    ImGui::EndPopup();
    return 0;
}
int CloseCurrentPopup(lua_State* L)
{
    ready(L);
    ImGui::CloseCurrentPopup();
    return 0;
}
// BeginMenuBar() -> open (the window registered with flags = imgui.WindowFlags_MenuBar)
int BeginMenuBar(lua_State* L)
{
    ready(L);
    if (!ImGui::BeginMenuBar())
        return boolean(L, false);
    scopes.push_back(S_MENUBAR);
    return boolean(L, true);
}
int EndMenuBar(lua_State* L)
{
    ready(L);
    pop_scope(L, S_MENUBAR, "EndMenuBar");
    ImGui::EndMenuBar();
    return 0;
}
int BeginMenu(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    if (!ImGui::BeginMenu(l, lua_isnoneornil(L, 2) || lua_toboolean(L, 2)))
        return boolean(L, false);
    scopes.push_back(S_MENU);
    return boolean(L, true);
}
int EndMenu(lua_State* L)
{
    ready(L);
    pop_scope(L, S_MENU, "EndMenu");
    ImGui::EndMenu();
    return 0;
}
// MenuItem(label[, shortcut[, selected[, enabled]]]) -> clicked, selected (toggled when clicked)
int MenuItem(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    const char* shortcut = luaL_optstring(L, 2, nullptr);
    bool selected = lua_toboolean(L, 3) != 0;
    bool enabled = lua_isnoneornil(L, 4) || lua_toboolean(L, 4);
    bool clicked = ImGui::MenuItem(l, shortcut, &selected, enabled);
    lua_pushboolean(L, clicked);
    lua_pushboolean(L, selected);
    return 2;
}

// ---- items ----
int IsItemHovered(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsItemHovered(flags(L, 1)));
}
int IsItemClicked(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsItemClicked((int)luaL_optinteger(L, 1, 0)));
}
int IsItemActive(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsItemActive());
}
int IsItemEdited(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsItemEdited());
}
int IsItemDeactivatedAfterEdit(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsItemDeactivatedAfterEdit());
}
int IsAnyItemActive(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsAnyItemActive());
}
int SetItemDefaultFocus(lua_State* L)
{
    ready(L);
    ImGui::SetItemDefaultFocus();
    return 0;
}
int SetKeyboardFocusHere(lua_State* L)
{
    ready(L);
    ImGui::SetKeyboardFocusHere((int)luaL_optinteger(L, 1, 0));
    return 0;
}

// ---- scopes: IDs, disabled, style ----
// PushID(text | integer)
int PushID(lua_State* L)
{
    ready(L);
    if (lua_isinteger(L, 1)) {
        push_scope(L, S_ID);
        ImGui::PushID((int)lua_tointeger(L, 1));
    } else {
        size_t n;
        const char* s = luaL_checklstring(L, 1, &n);
        push_scope(L, S_ID);
        ImGui::PushID(s, s + n);
    }
    return 0;
}
int PopID(lua_State* L)
{
    ready(L);
    pop_scope(L, S_ID, "PopID");
    ImGui::PopID();
    return 0;
}
// BeginDisabled([disabled = true])
int BeginDisabled(lua_State* L)
{
    ready(L);
    bool disabled = lua_isnoneornil(L, 1) || lua_toboolean(L, 1);
    push_scope(L, S_DISABLED);
    ImGui::BeginDisabled(disabled);
    return 0;
}
int EndDisabled(lua_State* L)
{
    ready(L);
    pop_scope(L, S_DISABLED, "EndDisabled");
    ImGui::EndDisabled();
    return 0;
}
// PushStyleColor(imgui.Col_*, {r, g, b[, a]})
int PushStyleColor(lua_State* L)
{
    ready(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= ImGuiCol_COUNT)
        return luaL_error(L, "rood.imgui.PushStyleColor: no colour %d (imgui.Col_*)", idx);
    ImVec4 c = vec4_arg(L, 2);
    push_scope(L, S_COLOR);
    ImGui::PushStyleColor(idx, c);
    return 0;
}
// PopStyleColor([count = 1])
int pop_count(lua_State* L, Scope s, const char* end)
{
    int n = (int)luaL_optinteger(L, 1, 1);
    if (n < 0 || n > (int)MAX_SCOPES)
        return luaL_error(L, "rood.imgui.%s: a count of %d", end, n);
    for (int k = 0; k < n; ++k)
        pop_scope(L, s, end);
    return n;
}
int PopStyleColor(lua_State* L)
{
    ready(L);
    ImGui::PopStyleColor(pop_count(L, S_COLOR, "PopStyleColor"));
    return 0;
}
// PushStyleVar(imgui.StyleVar_*, number | {x, y})
int PushStyleVar(lua_State* L)
{
    ready(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= ImGuiStyleVar_COUNT)
        return luaL_error(L, "rood.imgui.PushStyleVar: no style variable %d (imgui.StyleVar_*)", idx);
    const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(idx);
    if (info->Count == 2) {
        float v[2] = { 0, 0 };
        color_arg(L, 2, v, 2);
        push_scope(L, S_VAR);
        ImGui::PushStyleVar(idx, ImVec2(v[0], v[1]));
    } else {
        float v = num(L, 2);
        push_scope(L, S_VAR);
        ImGui::PushStyleVar(idx, v);
    }
    return 0;
}
int PopStyleVar(lua_State* L)
{
    ready(L);
    ImGui::PopStyleVar(pop_count(L, S_VAR, "PopStyleVar"));
    return 0;
}

// ---- plots ----
// PlotLines(label, values[, overlay[, min, max[, w, h]]]); PlotHistogram the same
std::vector<float> values_arg(lua_State* L, int idx)
{
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
    if (n > 65536)
        luaL_error(L, "rood.imgui: more than 65536 values");
    std::vector<float> v((size_t)n);
    for (lua_Integer k = 1; k <= n; ++k) {
        lua_rawgeti(L, idx, k);
        v[(size_t)k - 1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    return v;
}
template <bool Lines> int Plot(lua_State* L)
{
    ready(L);
    const char* l = label(L, 1);
    std::vector<float> v = values_arg(L, 2);
    const char* overlay = luaL_optstring(L, 3, nullptr);
    float lo = optnum(L, 4, FLT_MAX), hi = optnum(L, 5, FLT_MAX);
    ImVec2 size = size_args(L, 6);
    if (Lines)
        ImGui::PlotLines(l, v.data(), (int)v.size(), 0, overlay, lo, hi, size);
    else
        ImGui::PlotHistogram(l, v.data(), (int)v.size(), 0, overlay, lo, hi, size);
    return 0;
}

// ---- keys and input ----
extern "C" int pc_editor_flying(void) __attribute__((weak));
extern "C" int pc_cutscene_camera_flying(void) __attribute__((weak));

// A key: imgui.Key_* or its name ("A", "Enter", "Backspace", "F1", "LeftCtrl").
ImGuiKey key_arg(lua_State* L, int idx)
{
    if (lua_isinteger(L, idx)) {
        lua_Integer k = lua_tointeger(L, idx);
        if (k >= ImGuiKey_NamedKey_BEGIN && k < ImGuiKey_NamedKey_END)
            return (ImGuiKey)k;
    } else if (lua_type(L, idx) == LUA_TSTRING) {
        const char* name = lua_tostring(L, idx);
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
            if (!std::strcmp(ImGui::GetKeyName((ImGuiKey)k), name))
                return (ImGuiKey)k;
    }
    luaL_error(L, "rood.imgui: not a key: %s (imgui.Key_* or a name: \"A\", \"Enter\", \"F1\")",
        luaL_tolstring(L, idx, nullptr));
    return ImGuiKey_None;
}
// The window's keys: focused, nothing typed, no slider held, the camera not flying (begin_draw).
bool keys_now()
{
    ImGuiIO& io = ImGui::GetIO();
    return keys_ok && !io.WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right);
}
// IsKeyPressed(key[, repeat = true]) -> pressed (false unless the window has the keys)
int IsKeyPressed(lua_State* L)
{
    ready(L);
    ImGuiKey k = key_arg(L, 1);
    bool repeat = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
    return boolean(L, keys_now() && ImGui::IsKeyPressed(k, repeat));
}
int IsKeyDown(lua_State* L)
{
    ready(L);
    ImGuiKey k = key_arg(L, 1);
    return boolean(L, keys_now() && ImGui::IsKeyDown(k));
}
int IsKeyReleased(lua_State* L)
{
    ready(L);
    ImGuiKey k = key_arg(L, 1);
    return boolean(L, keys_now() && ImGui::IsKeyReleased(k));
}
// HasKeys() -> the window has the keyboard (what IsKeyPressed checks first)
int HasKeys(lua_State* L)
{
    ready(L);
    return boolean(L, keys_now());
}
// GetIO() -> {KeyCtrl, KeyShift, KeyAlt, KeySuper, WantTextInput, WantCaptureKeyboard, DeltaTime,
// Framerate, DisplaySize = {w, h}, MousePos = {x, y}}: a copy, read only
int GetIO(lua_State* L)
{
    ready(L);
    ImGuiIO& io = ImGui::GetIO();
    lua_createtable(L, 0, 10);
    auto flag = [&](const char* name, bool v) {
        lua_pushboolean(L, v);
        lua_setfield(L, -2, name);
    };
    flag("KeyCtrl", io.KeyCtrl);
    flag("KeyShift", io.KeyShift);
    flag("KeyAlt", io.KeyAlt);
    flag("KeySuper", io.KeySuper);
    flag("WantTextInput", io.WantTextInput);
    flag("WantCaptureKeyboard", io.WantCaptureKeyboard);
    lua_pushnumber(L, io.DeltaTime);
    lua_setfield(L, -2, "DeltaTime");
    lua_pushnumber(L, io.Framerate);
    lua_setfield(L, -2, "Framerate");
    float d[2] = { io.DisplaySize.x, io.DisplaySize.y }, m[2] = { io.MousePos.x, io.MousePos.y };
    push_floats(L, d, 2);
    lua_setfield(L, -2, "DisplaySize");
    push_floats(L, m, 2);
    lua_setfield(L, -2, "MousePos");
    return 1;
}
int GetTime(lua_State* L)
{
    ready(L);
    lua_pushnumber(L, ImGui::GetTime());
    return 1;
}

// ---- a canvas: the cursor, the mouse, invisible buttons and the window's draw list (the node editor) ----
int GetCursorScreenPos(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetCursorScreenPos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int SetCursorScreenPos(lua_State* L)
{
    ready(L);
    ImGui::SetCursorScreenPos(ImVec2(num(L, 1), num(L, 2)));
    return 0;
}
int GetCursorPos(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetCursorPos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int SetCursorPos(lua_State* L)
{
    ready(L);
    ImGui::SetCursorPos(ImVec2(num(L, 1), num(L, 2)));
    return 0;
}
int GetWindowPos(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetWindowPos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int GetWindowSize(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetWindowSize();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
// InvisibleButton(id, w, h[, flags]) -> pressed
int InvisibleButton(lua_State* L)
{
    ready(L);
    const char* id = label(L, 1);
    ImVec2 size(num(L, 2), num(L, 3));
    if (size.x <= 0) size.x = 1;
    if (size.y <= 0) size.y = 1;
    return boolean(L, ImGui::InvisibleButton(id, size, flags(L, 4)));
}
int GetItemRectMin(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetItemRectMin();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int GetItemRectMax(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetItemRectMax();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int SetItemAllowOverlap(lua_State* L)
{
    ready(L);
    ImGui::SetNextItemAllowOverlap();
    return 0;
}
int GetMousePos(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetMousePos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int IsMouseDown(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsMouseDown((int)luaL_optinteger(L, 1, 0)));
}
int IsMouseClicked(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsMouseClicked((int)luaL_optinteger(L, 1, 0), lua_toboolean(L, 2)));
}
int IsMouseDoubleClicked(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsMouseDoubleClicked((int)luaL_optinteger(L, 1, 0)));
}
int IsMouseReleased(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsMouseReleased((int)luaL_optinteger(L, 1, 0)));
}
// IsMouseDragging([button[, lock_threshold]]) -> dragging
int IsMouseDragging(lua_State* L)
{
    ready(L);
    return boolean(L, ImGui::IsMouseDragging((int)luaL_optinteger(L, 1, 0), optnum(L, 2, -1.0f)));
}
int GetMouseDragDelta(lua_State* L)
{
    ready(L);
    ImVec2 p = ImGui::GetMouseDragDelta((int)luaL_optinteger(L, 1, 0), optnum(L, 2, -1.0f));
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
int ResetMouseDragDelta(lua_State* L)
{
    ready(L);
    ImGui::ResetMouseDragDelta((int)luaL_optinteger(L, 1, 0));
    return 0;
}
int IsMouseHoveringRect(lua_State* L)
{
    ready(L);
    bool clip = lua_isnoneornil(L, 5) || lua_toboolean(L, 5);
    return boolean(L, ImGui::IsMouseHoveringRect(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)), clip));
}
int GetMouseWheel(lua_State* L)
{
    ready(L);
    lua_pushnumber(L, ImGui::GetIO().MouseWheel);
    return 1;
}
// PushClipRect(x0, y0, x1, y1[, intersect = true]); PopClipRect (the pair is checked as a scope)
int PushClipRect(lua_State* L)
{
    ready(L);
    bool intersect = lua_isnoneornil(L, 5) || lua_toboolean(L, 5);
    ImGui::PushClipRect(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)), intersect);
    push_scope(L, S_CLIPRECT);
    return 0;
}
int PopClipRect(lua_State* L)
{
    ready(L);
    pop_scope(L, S_CLIPRECT, "PopClipRect");
    ImGui::PopClipRect();
    return 0;
}
ImU32 col32_arg(lua_State* L, int idx)
{
    float c[4] = { 1, 1, 1, 1 };
    color_arg(L, idx, c, 4);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
}
// The window's draw list, screen coordinates, colours as {r, g, b[, a]}:
// DrawList_AddLine(x0, y0, x1, y1, color[, thickness])
int DrawList_AddLine(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)), col32_arg(L, 5), optnum(L, 6, 1));
    return 0;
}
// DrawList_AddRect(x0, y0, x1, y1, color[, rounding[, thickness]])
int DrawList_AddRect(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddRect(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)), col32_arg(L, 5),
        optnum(L, 6, 0), 0, optnum(L, 7, 1));
    return 0;
}
int DrawList_AddRectFilled(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)), col32_arg(L, 5),
        optnum(L, 6, 0));
    return 0;
}
// DrawList_AddCircle(x, y, radius, color[, segments[, thickness]])
int DrawList_AddCircle(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddCircle(ImVec2(num(L, 1), num(L, 2)), num(L, 3), col32_arg(L, 4), (int)luaL_optinteger(L, 5, 0),
        optnum(L, 6, 1));
    return 0;
}
int DrawList_AddCircleFilled(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(num(L, 1), num(L, 2)), num(L, 3), col32_arg(L, 4), (int)luaL_optinteger(L, 5, 0));
    return 0;
}
// DrawList_AddTriangleFilled(x0, y0, x1, y1, x2, y2, color)
int DrawList_AddTriangleFilled(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)),
        ImVec2(num(L, 5), num(L, 6)), col32_arg(L, 7));
    return 0;
}
// DrawList_AddBezierCubic(x0, y0, x1, y1, x2, y2, x3, y3, color[, thickness])
int DrawList_AddBezierCubic(lua_State* L)
{
    ready(L);
    ImGui::GetWindowDrawList()->AddBezierCubic(ImVec2(num(L, 1), num(L, 2)), ImVec2(num(L, 3), num(L, 4)),
        ImVec2(num(L, 5), num(L, 6)), ImVec2(num(L, 7), num(L, 8)), col32_arg(L, 9), optnum(L, 10, 1));
    return 0;
}
// DrawList_AddText(x, y, color, text)
int DrawList_AddText(lua_State* L)
{
    ready(L);
    size_t n = 0;
    const char* text = luaL_checklstring(L, 4, &n);
    ImGui::GetWindowDrawList()->AddText(ImVec2(num(L, 1), num(L, 2)), col32_arg(L, 3), text, text + n);
    return 0;
}

const luaL_Reg FUNCTIONS[] = {
    { "GetCursorScreenPos", GetCursorScreenPos }, { "SetCursorScreenPos", SetCursorScreenPos },
    { "GetCursorPos", GetCursorPos }, { "SetCursorPos", SetCursorPos },
    { "GetWindowPos", GetWindowPos }, { "GetWindowSize", GetWindowSize },
    { "InvisibleButton", InvisibleButton }, { "GetItemRectMin", GetItemRectMin }, { "GetItemRectMax", GetItemRectMax },
    { "SetNextItemAllowOverlap", SetItemAllowOverlap },
    { "GetMousePos", GetMousePos }, { "IsMouseDown", IsMouseDown }, { "IsMouseClicked", IsMouseClicked },
    { "IsMouseDoubleClicked", IsMouseDoubleClicked }, { "IsMouseReleased", IsMouseReleased },
    { "IsMouseDragging", IsMouseDragging }, { "GetMouseDragDelta", GetMouseDragDelta },
    { "ResetMouseDragDelta", ResetMouseDragDelta }, { "IsMouseHoveringRect", IsMouseHoveringRect },
    { "GetMouseWheel", GetMouseWheel }, { "PushClipRect", PushClipRect }, { "PopClipRect", PopClipRect },
    { "DrawList_AddLine", DrawList_AddLine }, { "DrawList_AddRect", DrawList_AddRect },
    { "DrawList_AddRectFilled", DrawList_AddRectFilled }, { "DrawList_AddCircle", DrawList_AddCircle },
    { "DrawList_AddCircleFilled", DrawList_AddCircleFilled }, { "DrawList_AddTriangleFilled", DrawList_AddTriangleFilled },
    { "DrawList_AddBezierCubic", DrawList_AddBezierCubic }, { "DrawList_AddText", DrawList_AddText },
    { "Begin", Begin }, { "End", End }, { "BeginChild", BeginChild }, { "EndChild", EndChild },
    { "SetNextWindowSize", SetNextWindowSize }, { "SetNextWindowPos", SetNextWindowPos },
    { "IsWindowFocused", IsWindowFocused }, { "IsWindowHovered", IsWindowHovered },
    { "Text", Text }, { "TextUnformatted", Text }, { "TextWrapped", TextWrapped }, { "TextDisabled", TextDisabled },
    { "TextColored", TextColored }, { "LabelText", LabelText }, { "BulletText", BulletText },
    { "SeparatorText", SeparatorText },
    { "Button", Button }, { "SmallButton", SmallButton }, { "Checkbox", Checkbox }, { "RadioButton", RadioButton },
    { "Selectable", Selectable }, { "ProgressBar", ProgressBar }, { "Bullet", Bullet },
    { "SliderFloat", SliderFloat }, { "SliderInt", SliderInt }, { "DragFloat", DragFloat }, { "DragInt", DragInt },
    { "DragFloat2", DragFloatN<2> }, { "DragFloat3", DragFloatN<3> }, { "DragFloat4", DragFloatN<4> },
    { "InputFloat", InputFloat }, { "InputInt", InputInt }, { "InputText", InputText },
    { "InputTextMultiline", InputTextMultiline },
    { "Combo", Combo }, { "BeginCombo", BeginCombo }, { "EndCombo", EndCombo },
    { "BeginListBox", BeginListBox }, { "EndListBox", EndListBox },
    { "ColorEdit3", ColorEditN<3> }, { "ColorEdit4", ColorEditN<4> },
    { "SameLine", SameLine }, { "Separator", Separator }, { "Spacing", Spacing }, { "NewLine", NewLine },
    { "Dummy", Dummy }, { "Indent", Indent }, { "Unindent", Unindent }, { "BeginGroup", BeginGroup },
    { "EndGroup", EndGroup }, { "PushItemWidth", PushItemWidth }, { "PopItemWidth", PopItemWidth },
    { "SetNextItemWidth", SetNextItemWidth }, { "AlignTextToFramePadding", AlignTextToFramePadding },
    { "GetContentRegionAvail", GetContentRegionAvail }, { "CalcTextSize", CalcTextSize },
    { "GetFontSize", GetFontSize }, { "GetFrameHeight", GetFrameHeight },
    { "SetScrollHereY", SetScrollHereY }, { "SetScrollY", SetScrollY }, { "GetScrollY", GetScrollY },
    { "GetTextLineHeightWithSpacing", GetTextLineHeightWithSpacing }, { "GetStyleColorVec4", GetStyleColorVec4 },
    { "ListClipper_Begin", ListClipper_Begin }, { "ListClipper_Step", ListClipper_Step },
    { "TreeNode", TreeNode }, { "TreeNodeEx", TreeNode }, { "TreePop", TreePop },
    { "CollapsingHeader", CollapsingHeader }, { "SetNextItemOpen", SetNextItemOpen },
    { "BeginTable", BeginTable }, { "EndTable", EndTable }, { "TableSetupColumn", TableSetupColumn },
    { "TableSetupScrollFreeze", TableSetupScrollFreeze }, { "TableHeadersRow", TableHeadersRow },
    { "TableNextRow", TableNextRow }, { "TableNextColumn", TableNextColumn },
    { "TableSetColumnIndex", TableSetColumnIndex },
    { "BeginTabBar", BeginTabBar }, { "EndTabBar", EndTabBar }, { "BeginTabItem", BeginTabItem },
    { "EndTabItem", EndTabItem },
    { "BeginTooltip", BeginTooltip }, { "BeginItemTooltip", BeginItemTooltip }, { "EndTooltip", EndTooltip },
    { "SetTooltip", SetTooltip }, { "SetItemTooltip", SetItemTooltip },
    { "SetClipboardText", SetClipboardText }, { "IsItemToggledOpen", IsItemToggledOpen },
    { "OpenPopup", OpenPopup }, { "BeginPopup", BeginPopup }, { "BeginPopupContextItem", BeginPopupContextItem },
    { "EndPopup", EndPopup }, { "CloseCurrentPopup", CloseCurrentPopup },
    { "BeginMenuBar", BeginMenuBar }, { "EndMenuBar", EndMenuBar }, { "BeginMenu", BeginMenu },
    { "EndMenu", EndMenu }, { "MenuItem", MenuItem },
    { "IsItemHovered", IsItemHovered }, { "IsItemClicked", IsItemClicked }, { "IsItemActive", IsItemActive },
    { "IsItemEdited", IsItemEdited }, { "IsItemDeactivatedAfterEdit", IsItemDeactivatedAfterEdit },
    { "IsAnyItemActive", IsAnyItemActive }, { "SetItemDefaultFocus", SetItemDefaultFocus },
    { "SetKeyboardFocusHere", SetKeyboardFocusHere },
    { "PushID", PushID }, { "PopID", PopID }, { "BeginDisabled", BeginDisabled }, { "EndDisabled", EndDisabled },
    { "PushStyleColor", PushStyleColor }, { "PopStyleColor", PopStyleColor }, { "PushStyleVar", PushStyleVar },
    { "PopStyleVar", PopStyleVar },
    { "PlotLines", Plot<true> }, { "PlotHistogram", Plot<false> },
    { "IsKeyPressed", IsKeyPressed }, { "IsKeyDown", IsKeyDown }, { "IsKeyReleased", IsKeyReleased },
    { "HasKeys", HasKeys }, { "GetIO", GetIO }, { "GetTime", GetTime },
    { nullptr, nullptr },
};

#define C(prefix, name) { #prefix "_" #name, ImGui##prefix##_##name }
const struct Constant {
    const char* name;
    int value;
} CONSTANTS[] = {
    C(WindowFlags, None), C(WindowFlags, NoTitleBar), C(WindowFlags, NoResize), C(WindowFlags, NoMove),
    C(WindowFlags, NoScrollbar), C(WindowFlags, NoScrollWithMouse), C(WindowFlags, NoCollapse),
    C(WindowFlags, AlwaysAutoResize), C(WindowFlags, NoBackground), C(WindowFlags, NoSavedSettings),
    C(WindowFlags, NoMouseInputs), C(WindowFlags, MenuBar), C(WindowFlags, HorizontalScrollbar),
    C(WindowFlags, NoFocusOnAppearing), C(WindowFlags, NoBringToFrontOnFocus), C(WindowFlags, AlwaysVerticalScrollbar),
    C(WindowFlags, NoNavInputs), C(WindowFlags, NoNav), C(WindowFlags, NoDecoration), C(WindowFlags, NoInputs),
    C(ChildFlags, None), C(ChildFlags, Borders), C(ChildFlags, AlwaysUseWindowPadding), C(ChildFlags, ResizeX),
    C(ChildFlags, ResizeY), C(ChildFlags, AutoResizeX), C(ChildFlags, AutoResizeY), C(ChildFlags, FrameStyle),
    C(InputTextFlags, None), C(InputTextFlags, CharsDecimal), C(InputTextFlags, CharsHexadecimal),
    C(InputTextFlags, CharsUppercase), C(InputTextFlags, CharsNoBlank), C(InputTextFlags, AllowTabInput),
    C(InputTextFlags, EnterReturnsTrue), C(InputTextFlags, EscapeClearsAll), C(InputTextFlags, CtrlEnterForNewLine),
    C(InputTextFlags, ReadOnly), C(InputTextFlags, Password), C(InputTextFlags, AutoSelectAll),
    C(InputTextFlags, NoUndoRedo),
    C(TreeNodeFlags, None), C(TreeNodeFlags, Selected), C(TreeNodeFlags, Framed), C(TreeNodeFlags, NoTreePushOnOpen),
    C(TreeNodeFlags, DefaultOpen), C(TreeNodeFlags, OpenOnDoubleClick), C(TreeNodeFlags, OpenOnArrow),
    C(TreeNodeFlags, Leaf), C(TreeNodeFlags, Bullet), C(TreeNodeFlags, SpanAvailWidth), C(TreeNodeFlags, SpanFullWidth),
    C(TreeNodeFlags, CollapsingHeader),
    C(SelectableFlags, None), C(SelectableFlags, NoAutoClosePopups), C(SelectableFlags, SpanAllColumns),
    C(SelectableFlags, AllowDoubleClick), C(SelectableFlags, Disabled), C(SelectableFlags, AllowOverlap),
    C(ComboFlags, None), C(ComboFlags, PopupAlignLeft), C(ComboFlags, HeightSmall), C(ComboFlags, HeightRegular),
    C(ComboFlags, HeightLarge), C(ComboFlags, HeightLargest), C(ComboFlags, NoArrowButton), C(ComboFlags, NoPreview),
    C(TabBarFlags, None), C(TabBarFlags, Reorderable), C(TabBarFlags, AutoSelectNewTabs), C(TabBarFlags, FittingPolicyScroll),
    C(TabItemFlags, None), C(TabItemFlags, UnsavedDocument), C(TabItemFlags, SetSelected), C(TabItemFlags, NoCloseWithMiddleMouseButton),
    C(TableFlags, None), C(TableFlags, Resizable), C(TableFlags, Reorderable), C(TableFlags, Hideable),
    C(TableFlags, Sortable), C(TableFlags, RowBg), C(TableFlags, BordersInnerH), C(TableFlags, BordersOuterH),
    C(TableFlags, BordersInnerV), C(TableFlags, BordersOuterV), C(TableFlags, BordersH), C(TableFlags, BordersV),
    C(TableFlags, BordersInner), C(TableFlags, BordersOuter), C(TableFlags, Borders), C(TableFlags, SizingFixedFit),
    C(TableFlags, SizingFixedSame), C(TableFlags, SizingStretchProp), C(TableFlags, SizingStretchSame),
    C(TableFlags, ScrollX), C(TableFlags, ScrollY), C(TableFlags, NoHostExtendX), C(TableFlags, PadOuterX),
    C(TableColumnFlags, None), C(TableColumnFlags, DefaultHide), C(TableColumnFlags, DefaultSort),
    C(TableColumnFlags, WidthStretch), C(TableColumnFlags, WidthFixed), C(TableColumnFlags, NoResize),
    C(TableColumnFlags, NoSort), C(TableColumnFlags, NoHide),
    C(TableRowFlags, None), C(TableRowFlags, Headers),
    C(SliderFlags, None), C(SliderFlags, Logarithmic), C(SliderFlags, NoRoundToFormat), C(SliderFlags, NoInput),
    C(SliderFlags, AlwaysClamp),
    C(ColorEditFlags, None), C(ColorEditFlags, NoAlpha), C(ColorEditFlags, NoPicker), C(ColorEditFlags, NoOptions),
    C(ColorEditFlags, NoSmallPreview), C(ColorEditFlags, NoInputs), C(ColorEditFlags, NoTooltip),
    C(ColorEditFlags, NoLabel), C(ColorEditFlags, AlphaBar), C(ColorEditFlags, HDR), C(ColorEditFlags, DisplayRGB),
    C(ColorEditFlags, DisplayHSV), C(ColorEditFlags, DisplayHex), C(ColorEditFlags, Float),
    C(HoveredFlags, None), C(HoveredFlags, ChildWindows), C(HoveredFlags, RootWindow), C(HoveredFlags, AnyWindow),
    C(HoveredFlags, AllowWhenBlockedByActiveItem), C(HoveredFlags, AllowWhenDisabled), C(HoveredFlags, ForTooltip),
    C(HoveredFlags, Stationary), C(HoveredFlags, DelayShort), C(HoveredFlags, DelayNormal),
    C(FocusedFlags, None), C(FocusedFlags, ChildWindows), C(FocusedFlags, RootWindow), C(FocusedFlags, AnyWindow),
    C(FocusedFlags, RootAndChildWindows),
    C(PopupFlags, None), C(PopupFlags, MouseButtonRight),
    C(Cond, None), C(Cond, Always), C(Cond, Once), C(Cond, FirstUseEver), C(Cond, Appearing),
    C(StyleVar, Alpha), C(StyleVar, DisabledAlpha), C(StyleVar, WindowPadding), C(StyleVar, WindowRounding),
    C(StyleVar, WindowMinSize), C(StyleVar, ChildRounding), C(StyleVar, FramePadding), C(StyleVar, FrameRounding),
    C(StyleVar, FrameBorderSize), C(StyleVar, ItemSpacing), C(StyleVar, ItemInnerSpacing), C(StyleVar, IndentSpacing),
    C(StyleVar, CellPadding), C(StyleVar, GrabMinSize), C(StyleVar, TabRounding), C(StyleVar, ButtonTextAlign),
    C(StyleVar, SelectableTextAlign),
    C(MouseButton, Left), C(MouseButton, Right), C(MouseButton, Middle),
};
#undef C
} // namespace

void begin_draw(Plugin* p, bool keys)
{
    current = p;
    keys_ok = keys && !(pc_editor_flying && pc_editor_flying())
        && !(pc_cutscene_camera_flying && pc_cutscene_camera_flying());
    scopes.clear();
}
std::string end_draw()
{
    if (clipper) { // left running (an error, a loop left early): ended while its window is current
        clipper->End();
        delete clipper;
        clipper = nullptr;
    }
    std::string open;
    for (unsigned char s : scopes)
        open += (open.empty() ? "" : ", ") + std::string(BEGIN_NAMES[s]);
    current = nullptr;
    scopes.clear();
    return open;
}
Plugin* drawing() { return current; }

void open(lua_State* L, Plugin* p)
{
    if (!granted(p, "dev.tools"))
        return;
    lua_createtable(L, 0, 256);
    for (const luaL_Reg* f = FUNCTIONS; f->name; ++f)
        set_cfunction(L, -1, f->name, f->func);
    for (const Constant& c : CONSTANTS) {
        lua_pushinteger(L, c.value);
        lua_setfield(L, -2, c.name);
    }
    // Every colour (Col_Text, ...) and named key (Key_A, Key_Enter, Key_F1, ...) by Dear ImGui's own names.
    for (int k = 0; k < ImGuiCol_COUNT; ++k) {
        lua_pushinteger(L, k);
        lua_setfield(L, -2, (std::string("Col_") + ImGui::GetStyleColorName(k)).c_str());
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        std::string name = ImGui::GetKeyName((ImGuiKey)k);
        bool plain = !name.empty();
        for (char c : name)
            plain &= (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!plain)
            continue;
        lua_pushinteger(L, k);
        lua_setfield(L, -2, ("Key_" + name).c_str());
    }
    lock_table(L);
    lua_setfield(L, -2, "imgui");
}

namespace {
struct Register {
    Register() { api_openers().push_back(open); }
} register_imgui;
} // namespace
} // namespace imgui_host
} // namespace rl
