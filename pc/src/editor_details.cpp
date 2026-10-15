// Details (editor_details.h), as Unreal's Details panel: the object's name
// and a lock, a search box, collapsible categories of label / value / reset
// rows (the reset arrow restores the default, or the preset's or prefab's
// value; tooltips give the range and unit), and the kind's own tools under
// them. A middle-drag on a number scrubs it on Houdini's value ladder: rows
// of steps 10 / 1 / 0.1 / 0.01 (up and down picks the step, left and right
// adds steps); Escape cancels; the release is one undo step.
#include "editor_details.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_selection.h"
#include "editor_undo.h"

extern "C" bool pc_lights_glow_details(void); /* lights.cpp: a glow's overrides */

using rood_schema::Field;
using rood_schema::Kind;
using rood_schema::Value;
using rood_selection::Ref;

namespace {
const ImVec4 changed_colour(1.0f, 0.78f, 0.3f, 1); /* a value off its default, as the room settings' overrides */
const float STEPS[4] = { 10, 1, 0.1f, 0.01f }; /* the value ladder's rows, top to bottom */
char search[64];
/* As last drawn (RPC editor_schema {action "ui"}). */
Ref shown;
std::string shown_kind;
int shown_frame = -1;
std::vector<std::string> shown_fields;
std::map<std::string, ImVec4> value_rects, reset_rects; /* window pixels: x, y, w, h */
float row_height, notch;

struct Ladder {
    bool active = false;
    Ref target;
    std::string field;
    int component = 0; /* a vec3's x, y or z */
    int row = 0, first_row = 0; /* STEPS indices */
    float press_x = 0, press_y = 0, origin_x = 0;
    float base = 0, value = 0; /* the value when the row was chosen; now */
    std::vector<std::pair<Ref, Value>> before; /* every object it changes, as they were (Escape) */
} ladder;

bool numeric(const Field& f)
{
    return f.type == rood_schema::TYPE_FLOAT || f.type == rood_schema::TYPE_INT || f.type == rood_schema::TYPE_ANGLE
        || f.type == rood_schema::TYPE_VEC3;
}
bool matches(const Field& f) { return !search[0] || strcasestr(f.label.c_str(), search) || strcasestr(f.name.c_str(), search); }
void keep_rect(std::map<std::string, ImVec4>& rects, const std::string& name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    rects[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
std::string number_format(const Field& f)
{
    int d = f.step >= 1 ? 0 : f.step >= 0.1f ? 1 : f.step >= 0.01f ? 2 : f.step >= 0.001f ? 3 : 5;
    std::string s = "%." + std::to_string(d) + "f";
    return f.unit.empty() ? s : s + " " + f.unit;
}
void tooltip(const Field& f, int index)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;
    std::string tip = f.tooltip.empty() ? f.label : f.tooltip;
    if (numeric(f)) {
        Field one = f;
        one.type = rood_schema::TYPE_FLOAT;
        tip += "\nRange " + rood_schema::format(one, Value::scalar(f.min)) + " to " + rood_schema::format(one, Value::scalar(f.max));
    }
    tip += "\nDefault " + rood_schema::format(f, f.def);
    const char* source = rood_schema::base_source_of(f, index);
    if (strcmp(source, "default"))
        tip += std::string("\nThe ") + source + "'s value " + rood_schema::format(f, rood_schema::base_of(f, index));
    if (numeric(f))
        tip += "\nMiddle-drag: the value ladder (steps of 10, 1, 0.1, 0.01)";
    ImGui::SetTooltip("%s", tip.c_str());
}

void ladder_begin(const Kind& k, const Field& f, const Ref& target, int index, int component)
{
    ladder = Ladder();
    ladder.active = true;
    ladder.target = target;
    ladder.field = f.name;
    ladder.component = component;
    int first = 3;
    float best = 1e30f;
    for (int r = 0; r < 4; r++) {
        float d = fabsf(logf(STEPS[r]) - logf(f.step > 0 ? f.step : 0.01f));
        if (d < best)
            best = d, first = r;
    }
    if (f.type == rood_schema::TYPE_INT && first > 1)
        first = 1;
    ladder.row = ladder.first_row = first;
    ImVec2 m = ImGui::GetIO().MousePos;
    ladder.press_x = ladder.origin_x = m.x;
    ladder.press_y = m.y;
    ladder.base = ladder.value = f.get(index).at(component);
    for (const Ref& r : rood_schema::targets(k, f, target)) {
        int i = rood_scene::index_of(r);
        if (i >= 0)
            ladder.before.push_back({ r, f.get(i) });
    }
    pc_undo_begin(("Edit " + f.label).c_str()); /* the whole drag is one undo step */
}
void ladder_end(bool keep)
{
    if (!keep)
        for (const auto& b : ladder.before) {
            int i;
            const Kind* k = rood_schema::kind_of(b.first, &i);
            const Field* f = k ? k->field(ladder.field) : nullptr;
            if (f)
                f->set(i, b.second);
        }
    ladder.active = false;
    ladder.before.clear();
    pc_undo_end();
}
void ladder_hover(const Kind& k, const Field& f, const Ref& target, int index, int component)
{
    if (!ladder.active && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        ladder_begin(k, f, target, index, component);
}

/* The field's widget; true when the user changed `v`. */
bool widget(const Kind& k, const Field& f, const Ref& target, int index, Value& v)
{
    bool edited = false;
    ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | (f.logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
    std::string fmt = number_format(f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    switch (f.type) {
    case rood_schema::TYPE_FLOAT:
    case rood_schema::TYPE_ANGLE: {
        float x = v.x();
        edited = ImGui::DragFloat("##value", &x, f.step, f.min, f.max, fmt.c_str(), flags);
        if (edited)
            v = Value::scalar(x);
        keep_rect(value_rects, f.name);
        ladder_hover(k, f, target, index, 0);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_INT: {
        int x = (int)lroundf(v.x());
        edited = ImGui::DragInt("##value", &x, f.step < 1 ? 1.0f : f.step, (int)f.min, (int)f.max, "%d", flags);
        if (edited)
            v = Value::scalar((float)x);
        keep_rect(value_rects, f.name);
        ladder_hover(k, f, target, index, 0);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_BOOL: {
        bool b = v.x() > 0.5f;
        edited = ImGui::Checkbox("##value", &b);
        if (edited)
            v = Value::scalar(b ? 1.0f : 0.0f);
        keep_rect(value_rects, f.name);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        bool open = ImGui::BeginCombo("##value", i >= 0 && i < (int)f.options.size() ? f.options[i].c_str() : "?");
        keep_rect(value_rects, f.name);
        if (!open)
            tooltip(f, index);
        if (open) {
            for (int o = 0; o < (int)f.options.size(); o++)
                if (ImGui::Selectable(f.options[o].c_str(), o == i)) {
                    v = Value::scalar((float)o);
                    edited = true;
                }
            ImGui::EndCombo();
        }
        break;
    }
    case rood_schema::TYPE_COLOUR: {
        float c[3] = { v.at(0), v.at(1), v.at(2) };
        edited = ImGui::ColorEdit3("##value", c, ImGuiColorEditFlags_Float | (f.max > 1 ? ImGuiColorEditFlags_HDR : 0));
        if (edited)
            v = Value::vec(c[0], c[1], c[2]);
        keep_rect(value_rects, f.name);
        tooltip(f, index);
        break;
    }
    case rood_schema::TYPE_VEC3: {
        /* Three fields, so the value ladder knows which one it scrubs. */
        float c[3] = { v.at(0), v.at(1), v.at(2) };
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        float w = (ImGui::GetContentRegionAvail().x - spacing * 2) / 3;
        static const char* const ids[3] = { "##x", "##y", "##z" };
        for (int a = 0; a < 3; a++) {
            if (a)
                ImGui::SameLine(0, spacing);
            ImGui::SetNextItemWidth(w);
            edited |= ImGui::DragFloat(ids[a], &c[a], f.step, f.min, f.max, fmt.c_str(), flags);
            keep_rect(value_rects, f.name + "." + std::string(1, "xyz"[a]));
            ladder_hover(k, f, target, index, a);
            tooltip(f, index);
        }
        if (edited)
            v = Value::vec(c[0], c[1], c[2]);
        break;
    }
    }
    return edited;
}

void row(const Kind& k, const Field& f, const Ref& target, int index)
{
    shown_fields.push_back(f.name);
    ImGui::PushID(f.name.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    bool off_base = f.resettable && rood_schema::is_overridden(f, index);
    ImGui::AlignTextToFramePadding();
    if (off_base)
        ImGui::TextColored(changed_colour, "%s", f.label.c_str());
    else
        ImGui::TextUnformatted(f.label.c_str());
    tooltip(f, index);
    ImGui::TableNextColumn();
    Value v = f.get(index), before = v;
    if (widget(k, f, target, index, v)) {
        /* A vec3's one component dragged or typed: the other selected
         * objects take that component only. */
        int component = -1;
        if (f.type == rood_schema::TYPE_VEC3)
            for (int c = 0, n = 0; c < 3; c++)
                if (v.at(c) != before.at(c))
                    component = ++n == 1 ? c : -1;
        rood_schema::apply(k, f, target, v, component);
    }
    ImGui::TableNextColumn();
    if (off_base) {
        if (ImGui::ArrowButton("##reset", ImGuiDir_Left)) /* Unreal's yellow reset arrow */
            rood_schema::apply_reset(k, f, target);
        keep_rect(reset_rects, f.name);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            const char* source = rood_schema::base_source_of(f, index);
            std::string to = rood_schema::format(f, rood_schema::base_of(f, index));
            if (strcmp(source, "default"))
                ImGui::SetTooltip("Reset to the %s's value: %s", source, to.c_str());
            else
                ImGui::SetTooltip("Reset to the default: %s", to.c_str());
        }
    }
    ImGui::PopID();
}

/* A padlock: closed while locked, its shackle swung open when not. */
bool lock_button(bool locked)
{
    float side = ImGui::GetFrameHeight();
    if (locked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    bool clicked = ImGui::Button("##lock", ImVec2(side, side));
    if (locked)
        ImGui::PopStyleColor(2);
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    float cx = (lo.x + hi.x) * 0.5f, w = side * 0.42f, body = lo.y + side * 0.48f;
    d->AddRectFilled(ImVec2(cx - w * 0.5f, body), ImVec2(cx + w * 0.5f, hi.y - side * 0.18f), col, side * 0.06f);
    float r = w * 0.32f, sx = locked ? cx : cx + w * 0.3f;
    d->PathArcTo(ImVec2(sx, body - side * 0.02f), r, 3.14159265f, 2 * 3.14159265f, 10);
    d->PathStroke(col, 0, side * 0.09f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(locked ? "Locked: Details keeps this object while you select others (click to follow the selection)"
                                 : "Lock Details to this object while you select others");
    return clicked;
}
} // namespace

void rood_details::draw()
{
    shown_fields.clear();
    value_rects.clear();
    reset_rects.clear();
    shown_frame = ImGui::GetFrameCount();
    row_height = ImGui::GetFrameHeight();
    notch = ImGui::GetFontSize() * 0.75f;
    Ref locked = rood_selection::locked();
    Ref subject = locked ? locked : rood_selection::primary();
    shown = subject;
    shown_kind.clear();
    ImGui::PushID("details");
    if (!subject) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::TextWrapped("Click a light, light actor, glow or fog volume in the view or the Outliner: its properties "
                           "show here. Room-wide settings are on the Lighting, Atmosphere, Post-processing and Graphics tabs.");
        ImGui::PopID();
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rood_scene::label(subject).c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    if (lock_button((bool)locked))
        rood_selection::lock(!locked);
    if (subject.kind == rood_selection::KIND_GLOW) {
        pc_lights_glow_details();
        ImGui::PopID();
        return;
    }
    int index;
    const Kind* k = rood_schema::kind_of(subject, &index);
    if (!k) {
        ImGui::TextDisabled("It is gone (deleted or reloaded).");
        ImGui::PopID();
        return;
    }
    shown_kind = k->name;
    size_t n = rood_selection::items().size();
    if (locked && !rood_selection::contains(subject))
        ImGui::TextDisabled("%s (locked; the selection is elsewhere)", k->label.c_str());
    else if (n > 1)
        ImGui::TextDisabled("%s; %zu selected: edits go to every selected %s", k->label.c_str(), n, k->label.c_str());
    else
        ImGui::TextDisabled("%s", k->label.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Search details", search, sizeof search);
    std::vector<std::string> categories;
    for (const Field& f : k->fields)
        if (std::find(categories.begin(), categories.end(), f.category) == categories.end())
            categories.push_back(f.category);
    for (const std::string& c : categories) {
        bool any = false;
        for (const Field& f : k->fields)
            any |= f.category == c && matches(f);
        if (!any)
            continue;
        if (search[0])
            ImGui::SetNextItemOpen(true); /* a search shows its matches in every category */
        bool closed = std::find(k->closed.begin(), k->closed.end(), c) != k->closed.end();
        if (!ImGui::CollapsingHeader((c + "##category").c_str(), closed ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        if (ImGui::BeginTable(c.c_str(), 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            for (const Field& f : k->fields)
                if (f.category == c && matches(f))
                    row(*k, f, subject, index);
            ImGui::EndTable();
        }
    }
    if (k->extras) {
        ImGui::Spacing();
        k->extras(index);
    }
    ImGui::PopID();
}

void rood_details::ladder_frame()
{
    if (!ladder.active)
        return;
    int index;
    const Kind* k = rood_schema::kind_of(ladder.target, &index);
    const Field* f = k ? k->field(ladder.field) : nullptr;
    if (!f) {
        ladder_end(false);
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    row_height = ImGui::GetFrameHeight();
    notch = ImGui::GetFontSize() * 0.75f;
    int last = f->type == rood_schema::TYPE_INT ? 1 : 3;
    int row = ladder.first_row + (int)floorf((io.MousePos.y - ladder.press_y) / row_height + 0.5f);
    row = row < 0 ? 0 : row > last ? last : row;
    if (row != ladder.row) { /* a new step: scrub on from the value now */
        ladder.row = row;
        ladder.base = ladder.value;
        ladder.origin_x = io.MousePos.x;
    }
    int notches = (int)((io.MousePos.x - ladder.origin_x) / notch); /* toward zero */
    Value v = f->get(index);
    if (f->type == rood_schema::TYPE_VEC3)
        v = Value::vec(v.at(0), v.at(1), v.at(2));
    v.v[ladder.component] = ladder.base + STEPS[ladder.row] * notches;
    v = rood_schema::clamp(*f, v);
    ladder.value = v.at(ladder.component);
    rood_schema::apply(*k, *f, ladder.target, v, f->type == rood_schema::TYPE_VEC3 ? ladder.component : -1);
    /* The ladder: a column of steps with the chosen one lit, the value beside it. */
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float w = ImGui::GetFontSize() * 4;
    for (int r = 0; r <= last; r++) {
        float cy = ladder.press_y + (r - ladder.first_row) * row_height;
        ImVec2 lo(ladder.press_x - w * 0.5f, cy - row_height * 0.5f), hi(ladder.press_x + w * 0.5f, cy + row_height * 0.5f);
        d->AddRectFilled(lo, hi, r == ladder.row ? IM_COL32(70, 110, 170, 235) : IM_COL32(30, 30, 36, 220));
        d->AddRect(lo, hi, IM_COL32(0, 0, 0, 200));
        char text[16];
        snprintf(text, sizeof text, "%g", STEPS[r]);
        ImVec2 ts = ImGui::CalcTextSize(text);
        d->AddText(ImVec2(ladder.press_x - ts.x * 0.5f, cy - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), text);
    }
    Field one = *f;
    one.type = f->type == rood_schema::TYPE_VEC3 ? rood_schema::TYPE_FLOAT : f->type;
    std::string shown_value = rood_schema::format(one, Value::scalar(ladder.value));
    float cy = ladder.press_y + (ladder.row - ladder.first_row) * row_height;
    d->AddText(ImVec2(ladder.press_x + w * 0.5f + 6, cy - ImGui::GetFontSize() * 0.5f), IM_COL32(255, 230, 120, 255),
        shown_value.c_str());
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        ladder_end(true);
}

bool rood_details::ladder_cancel()
{
    if (!ladder.active)
        return false;
    ladder_end(false);
    return true;
}

void rood_details::set_search(const std::string& text) { snprintf(search, sizeof search, "%s", text.c_str()); }

std::string rood_details::state_json()
{
    auto q = [](const std::string& s) { return prefab::json_quote(s); };
    bool current = ImGui::GetCurrentContext() && shown_frame == ImGui::GetFrameCount();
    auto rects_json = [&](const std::map<std::string, ImVec4>& rects) {
        std::string s = "{";
        bool first = true;
        for (const auto& kv : rects) {
            if (!current)
                break;
            char r[96];
            snprintf(r, sizeof r, "[%.1f,%.1f,%.1f,%.1f]", kv.second.x, kv.second.y, kv.second.z, kv.second.w);
            s += (first ? "" : ",") + q(kv.first) + ":" + r;
            first = false;
        }
        return s + "}";
    };
    Ref pinned = rood_selection::locked();
    std::string s = "{\"subject\":" + (current && shown ? q(shown.id) : std::string("null"))
        + ",\"kind\":" + (current && !shown_kind.empty() ? q(shown_kind) : std::string("null"))
        + ",\"locked\":" + (pinned ? q(pinned.id) : std::string("null")) + ",\"search\":" + q(search) + ",\"fields\":[";
    for (size_t i = 0; current && i < shown_fields.size(); i++)
        s += (i ? "," : "") + q(shown_fields[i]);
    s += "],\"rects\":" + rects_json(value_rects) + ",\"reset\":" + rects_json(reset_rects);
    char lad[400];
    snprintf(lad, sizeof lad,
        ",\"ladder\":{\"active\":%s,\"field\":%s,\"component\":%d,\"row\":%d,\"first_row\":%d,\"step\":%g,"
        "\"row_height\":%.2f,\"notch\":%.2f,\"value\":%.6g}}",
        ladder.active ? "true" : "false", q(ladder.field).c_str(), ladder.component, ladder.row, ladder.first_row,
        STEPS[ladder.row], row_height, notch, ladder.value);
    return s + lad;
}
