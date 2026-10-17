// The particle emitters in the editor (particles_internal.h; pc/docs/Particles.md):
// their schema kind (Details, the value ladder, the reset arrow to the asset's
// value, RPC editor_schema), Details' tools, the Outliner rows, the
// Atmosphere tab's Dust section, the viewport's outlines, icons and gizmo.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_details.h"
#include "editor_overlays.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "lighting_layers.h"
#include "lights_internal.h"
#include "particle_assets.h"
#include "particles.h"
#include "particles_internal.h"

extern "C" void pc_lights_mark_dirty(void);
extern "C" int pc_lights_gizmo_space; /* lights.cpp: the move tool's axes (0 world, 1 local) */

namespace {
namespace rs = rood_schema;
using rs::Field;
using rs::Value;
const char* const SHAPE_OPTIONS[] = { "Box", "Room", "Point", "Disc", "Surface" };

/* A schema value as lighting text. */
std::string text_of(rs::Type type, const std::vector<std::string>& options, const Value& v)
{
    char text[96];
    switch (type) {
    case rs::TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        std::string s = i >= 0 && i < (int)options.size() ? options[i] : options.empty() ? "" : options[0];
        for (char& c : s)
            c = (char)tolower((unsigned char)c);
        return s;
    }
    case rs::TYPE_BOOL:
        return v.x() > 0.5f ? "1" : "0";
    case rs::TYPE_INT:
        return std::to_string((int)lroundf(v.x()));
    case rs::TYPE_VEC3:
    case rs::TYPE_COLOUR:
        snprintf(text, sizeof text, "%.4f,%.4f,%.4f", v.at(0), v.at(1), v.at(2));
        return text;
    default:
        snprintf(text, sizeof text, "%.6g", v.x());
        return text;
    }
}

rs::Kind emitter_kind()
{
    rs::Kind k;
    k.name = "emitter";
    k.label = "Particle emitter";
    k.object = rood_selection::KIND_EMITTER;
    k.closed = { "Motion", "Look" };
    k.extras = [](int i) { rood_particles::details_extra(i); };
    /* an asset field: through the item's text, its asset's value for the reset arrow */
    auto add = [&k](Field f) {
        const char* text = lighting::field_default("emitter", f.name);
        if (*text)
            f.def = rs::from_text(f.type, f.options, text);
        std::string key = f.name;
        rs::Type type = f.type;
        std::vector<std::string> options = f.options;
        f.get = [key, type, options](int i) { return rs::from_text(type, options, rood_particles::field(i, key.c_str())); };
        f.set = [key, type, options](int i, const Value& v) {
            rood_particles::set_field(i, key.c_str(), text_of(type, options, v));
        };
        f.base = [key, type, options](int i) {
            return rs::from_text(type, options, rood_particles::asset_value(i, key.c_str()));
        };
        f.base_source = [](int i) -> const char* { return rood_particles::own(i).asset.empty() ? "default" : "asset"; };
        f.overridden = [key](int i) { return rood_particles::overrides(i, key.c_str()); };
        f.reset = [key](int i) { rood_particles::clear_field(i, key.c_str()); };
        k.fields.push_back(f);
    };
    auto integer = [](Field f) {
        f.type = rs::TYPE_INT;
        return f;
    };
    /* Basics: its own switch, then how many and how they light */
    {
        Field f = rs::bool_field("enabled", "Enabled", "Basics", true, "Off: none of its particles are drawn");
        f.get = [](int i) { return Value::scalar(rood_particles::own(i).enabled ? 1.0f : 0.0f); };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            o.enabled = v.x() > 0.5f;
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    add(integer(rs::number_field("count", "Count", "Basics", 0, PC_PARTICLES_MAX, 1, 400, "", true,
        "Particles alive at once (Graphics > Dust High; Low draws dust_low of them)")));
    /* Lighting (the item's `lit`, 0 or 1; the asset's lighting.mode) */
    add(rs::enum_field("lit", "Lighting", "Basics", { "In light only", "Lit" }, 0,
        "In light only: they show only where light reaches them (in shadow they vanish); Lit: their ambient light too, "
        "everywhere"));
    k.fields.back().set = [](int i, const Value& v) { rood_particles::set_field(i, "lit", v.x() > 0.5f ? "1" : "0"); };
    add(rs::number_field("ambient", "Ambient", "Basics", 0, 1, 0.001f, 0.05f, "", false,
        "Lit: their light everywhere (in light only: unused)"));
    add(rs::enum_field("quality", "Graphics setting", "Basics", { "Dust", "Always" }, 0,
        "Dust: Graphics > Dust sets how many are drawn (Off none); Always: all of them, whatever Dust is"));
    /* Spawn */
    add(rs::enum_field("shape", "Shape", "Spawn", std::vector<std::string>(SHAPE_OPTIONS, SHAPE_OPTIONS + 5), 0,
        "Box: in its box; Room: in the room's bounds; Point: a ball of its radius; Disc: a horizontal disc; Surface: on the "
        "floor under its box"));
    add(rs::vec3_field("size", "Size", "Spawn", 2, 8192, 1, Value::vec(256, 192, 256), "u", "Its box's full extents"));
    add(rs::number_field("radius", "Radius", "Spawn", 0, 4096, 1, 64, "u", false, "A point's or a disc's radius"));
    add(rs::number_field("lifetime", "Lifetime", "Spawn", 0.1f, 120, 0.1f, 10, "s", true, "Seconds each particle lives"));
    add(rs::number_field("lifetime_jitter", "Lifetime jitter", "Spawn", 0, 0.95f, 0.01f, 0.5f, "", false,
        "The share of its lifetime a particle may live less"));
    {
        Field f = integer(rs::number_field("seed", "Seed", "Spawn", 1, 999999, 1, 1, "", false,
            "Its random choices (two emitters with one seed move alike)"));
        f.shared = false;
        f.get = [](int i) { return Value::scalar((float)rood_particles::own(i).seed); };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            o.seed = (int)lroundf(v.x());
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    /* Motion: the behaviour modules (0: off) */
    add(rs::vec3_field("drift", "Drift", "Motion", -200, 200, 0.1f, Value::vec(0, 0, 0), "u/s", "Its steady drift (y down)"));
    add(rs::number_field("curl", "Turbulence", "Motion", 0, 200, 0.1f, 0, "u", false,
        "How far a smooth swirling field moves each particle"));
    add(rs::number_field("curl_scale", "Swirl size", "Motion", 4, 2048, 1, 64, "u", true));
    add(rs::number_field("curl_speed", "Swirl speed", "Motion", 0, 4, 0.001f, 0.1f, "/s", false, "How fast the swirls change"));
    add(rs::number_field("gravity", "Gravity", "Motion", -200, 200, 0.01f, 0, "u/s2", false, "y down: negative rises (buoyancy)"));
    add(rs::number_field("drag", "Drag", "Motion", 0, 10, 0.001f, 0, "/s", false, "It falls at most gravity / drag"));
    add(rs::number_field("fade_in", "Fade in", "Motion", 0, 30, 0.01f, 0, "s"));
    add(rs::number_field("fade_out", "Fade out", "Motion", 0, 30, 0.01f, 0, "s"));
    /* Look: the renderer */
    /* The renderer (`render`) has one value so far, pixel: square game pixels, nearest. It joins the
     * schema (an enum needs two options) with the soft sprite and the streak; until then there is
     * nothing to choose or reset. */
    add(integer(rs::number_field("pixel_min", "Size (min)", "Look", 1, 4, 1, 1, "px", false, "Game pixels")));
    add(integer(rs::number_field("pixel_max", "Size (max)", "Look", 1, 4, 1, 2, "px", false, "Game pixels")));
    add(rs::colour_field("color", "Colour", "Look", 4, Value::vec(1, 1, 1), "Linear colour"));
    add(rs::number_field("brightness", "Brightness", "Look", 0, 8, 0.01f, 1, "", false, "Scale on the light it reflects"));
    add(rs::number_field("variation", "Variation", "Look", 0, 1, 0.01f, 0.5f, "", false, "How much dimmer each one may be"));
    /* Transform: its own */
    {
        Field f = rs::vec3_field("pos", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its centre");
        f.resettable = false, f.shared = false;
        f.get = [](int i) {
            rood_particles::Own o = rood_particles::own(i);
            return Value::vec(o.pos[0], o.pos[1], o.pos[2]);
        };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            for (int c = 0; c < 3; c++)
                o.pos[c] = v.at(c);
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    {
        Field f = rs::angle_field("yaw", "Yaw", "Transform", -180, 180, 0.5f, 0);
        f.get = [](int i) { return Value::scalar(rood_particles::own(i).yaw); };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            o.yaw = v.x();
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    return k;
}
/* The particles' settings (particles.h; project settings, RPC particles {settings, low}):
 * the standalone kind "particles" (one object), so Details > Atmosphere and the dev tools'
 * Graphics tab draw them with their ranges, the reset arrows, the value ladder and undo, and
 * RPC editor_schema {kind "particles"} reads and edits them. Dust itself is the Graphics
 * setting (System > Graphics, the dev tools' Dust row, RPC graphics {dust}). */
rs::Kind settings_kind()
{
    rs::Kind k;
    k.name = "particles";
    k.label = "Particle settings";
    k.object = rood_selection::KIND_NONE;
    auto number = [&k](Field f, float* v) {
        f.get = [v](int) { return Value::scalar(*v); };
        f.set = [v](int, const Value& x) { *v = x.x(); };
        k.fields.push_back(f);
    };
    auto integer = [&k](Field f, int* v) {
        f.type = f.type == rs::TYPE_BOOL ? rs::TYPE_BOOL : rs::TYPE_INT;
        f.get = [v](int) { return Value::scalar((float)*v); };
        f.set = [v](int, const Value& x) { *v = (int)lroundf(x.x()); };
        k.fields.push_back(f);
    };
    number(rs::number_field("dust_low", "Low's share", "Dust", 0, 1, 0.01f, 0.4f, "", false,
               "Dust Low draws this share of each dust emitter's count (settings dust_low)"),
        &pc_dust_low);
    number(rs::number_field("particles_glow", "Glows' light", "Lighting", 0, 4, 0.01f, 1, "", false,
               "How much the glows (fires, windows, the save point) light the particles (settings particles_glow)"),
        &pc_particles_glow);
    integer(rs::number_field("particles_lights", "Sources per particle", "Lighting", 1, 4, 1, 2, "", false,
                "Its strongest lights and glows, each with a shadow ray, and the sun (settings particles_lights)"),
        &pc_particles_lights);
    integer(rs::bool_field("particles_glow_shadows", "Glows cast shadows on them", "Lighting", true,
                "The glows' light is shadowed too: the air behind a wall stays dark (settings particles_glow_shadows)"),
        &pc_particles_glow_shadows);
    integer(rs::number_field("particles_cpu_rays", "CPU shadow rays a frame", "OpenGL", 64, 8192, 1, 1536, "", true,
                "OpenGL: the motes' shadow rays a frame, round-robin (settings particles_cpu_rays). Fewer: a mote "
                "crossing a shadow's edge keeps its old light for more frames"),
        &pc_particles_cpu_rays);
    return k;
}
struct Register {
    Register()
    {
        rs::add_kind(emitter_kind());
        rs::add_kind(settings_kind());
    }
} registered;
} // namespace

namespace {
const float DEG = 3.14159265f / 180.0f;
void tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
int asset_choice = 0; /* the Atmosphere tab's asset for Add emitter */
const char* const LEVELS = "Off\0Low\0High\0";
} // namespace

int rood_particles::add_here(const std::string& asset)
{
    const Asset* a = find_asset(asset);
    float rect[4];
    rood_lights::Placement where;
    if (!a || !rood_lights::view_rect(rect)
        || !rood_lights::place_compute("light:point", rect[0] + rect[2] * 0.5f, rect[1] + rect[3] * 0.5f, PC_SNAP_MODE_SURFACE,
            where))
        return -1;
    int i = add_on_surface(a->id, where.surface);
    pc_undo_hint("Add emitter");
    return i;
}

int rood_particles::add_on_surface(const std::string& asset, const float surface[3])
{
    const Asset* a = find_asset(asset);
    if (!a)
        return -1;
    float size[3] = { 256, 192, 256 };
    if (const std::string* s = a->get("size"))
        sscanf(s->c_str(), "%f,%f,%f", &size[0], &size[1], &size[2]);
    float pos[3] = { surface[0], surface[1] - size[1] * 0.5f - 8, surface[2] }; /* y down: the box's bottom on it */
    return add_emitter(a->id, pos);
}

/* Details > an emitter's tools under its fields: its name, its asset (its own
 * values stay, the rest follow the new one), the gizmo's mode, Reset to its
 * asset, Delete. */
void rood_particles::details_extra(int index)
{
    if (index < 0 || index >= emitter_count())
        return;
    Own o = own(index);
    ImGui::PushID(index);
    ImGui::SeparatorText("Tools");
    char name[96];
    snprintf(name, sizeof name, "%s", o.name.c_str());
    if (ImGui::InputText("Name", name, sizeof name)) {
        o.name = name;
        set_own(index, o);
        pc_undo_hint("Rename emitter");
    }
    const auto& list = assets();
    if (ImGui::BeginCombo("Asset", o.asset.empty() ? "(none)" : o.asset.c_str())) {
        for (const Asset& a : list)
            if (ImGui::Selectable(a.name.c_str(), a.id == o.asset)) {
                o.asset = a.id;
                set_own(index, o);
                pc_undo_hint("Change emitter asset");
            }
        ImGui::EndCombo();
    }
    tooltip("pc/assets/emitters: every field it does not set comes from its asset (the reset arrows go back to it).");
    ImGui::RadioButton("Move (W)", &rood_lights::gizmo_operation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Turn (E)", &rood_lights::gizmo_operation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Size (R)", &rood_lights::gizmo_operation, 2);
    if (ImGui::Button("Reset all to its asset")) {
        pc_undo_hint("Reset emitter to its asset");
        for (const auto& f : lighting::Fields(emitter_item(index).fields))
            if (lighting::preset_field("emitter", f.first))
                clear_field(index, f.first.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete emitter")) {
        pc_undo_hint("Delete emitter");
        remove_emitter(index);
        rood_scene::refresh();
        pc_lights_mark_dirty();
    }
    ImGui::Text("%d particles drawn", drawn(index));
    ImGui::PopID();
}

/* Outliner rows for the room's emitters: the enabled box, click to select (Shift adds, Ctrl toggles), the layer badge. */
void rood_particles::outliner(const char* filter)
{
    if (!emitter_count()) {
        ImGui::TextDisabled("None in this room (Details > Atmosphere: Add emitter)");
        return;
    }
    for (int i = 0; i < emitter_count(); i++) {
        std::string label = emitter_label(i);
        if (filter && filter[0] && !strcasestr(label.c_str(), filter))
            continue;
        ImGui::PushID(i);
        Own o = own(i);
        bool on = o.enabled;
        if (ImGui::Checkbox("##on", &on)) {
            o.enabled = on;
            set_own(i, o);
            pc_undo_hint(on ? "Enable emitter" : "Disable emitter");
        }
        ImGui::SameLine();
        rood_selection::Ref ref = rood_scene::emitter_ref(i);
        bool hidden = rood_selection::hidden(ref);
        if (!on || hidden)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (hidden)
            label += "  [hidden]###" + emitter_label(i);
        if (ImGui::Selectable(label.c_str(), rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
        rood_scene::row_drawn(ref);
        if (!on || hidden)
            ImGui::PopStyleColor();
        rood_lights::layer_row(ref.id);
        ImGui::PopID();
    }
}

/* Details > Atmosphere > Dust and particles: the setting, adding an emitter,
 * the renderer's settings and what it drew. Returns true when the room changed. */
bool pc_particles_page(void)
{
    bool changed = false;
    ImGui::PushID("particles");
    ImGui::SeparatorText("Dust and particles");
    ImGui::Combo("Dust (all rooms)", &pc_dust, LEVELS);
    tooltip("Graphics > Dust (settings key dust): Off draws none (the picture as before), Low dust_low of each emitter's\n"
            "count, High all of them. With Dust Off the room's emitters still show here, dimmed.");
    const auto& list = rood_particles::assets();
    if (!list.empty()) {
        asset_choice = std::min(asset_choice, (int)list.size() - 1);
        if (ImGui::BeginCombo("##asset", list[asset_choice].name.c_str())) {
            for (int i = 0; i < (int)list.size(); i++)
                if (ImGui::Selectable(list[i].name.c_str(), i == asset_choice))
                    asset_choice = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add emitter"))
            changed |= rood_particles::add_here(list[asset_choice].id) >= 0;
        tooltip("An emitter of this asset on the surface under the view's centre, selected: its fields are in Details.");
    } else {
        ImGui::TextDisabled("%s", rood_particles::assets_status().c_str());
    }
    ImGui::Checkbox("Show emitters in the view", rood_particles::emitters_shown_flag());
    bool debug = pc_particles_debug != 0;
    if (ImGui::Checkbox("Show every particle (unlit)", &debug))
        pc_particles_debug = debug;
    tooltip("Where they are: every particle white, whatever reaches it (RPC particles {debug}).");
    if (ImGui::TreeNode("Particle renderer")) {
        if (const rs::Kind* k = rs::find_kind("particles"))
            rood_details::draw_kind(*k, 0); /* the settings: ranges, reset arrows, the value ladder, undo */
        if (ImGui::SmallButton("Reload assets"))
            rood_particles::reload();
        ImGui::TextDisabled("%s", rood_particles::assets_status().c_str());
        ImGui::TreePop();
    }
    ImGui::PopID();
    return changed;
}

/* The room's emitters in the editor view (Show > Emitters): their outlines,
 * an icon at each centre (a click picks it, box select and the RPC see it),
 * the primary's gizmo (W move, E turn, R its box's size or its radius).
 * Returns true while the gizmo has the mouse. */
bool pc_particles_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list)
{
    ImDrawList* draw = (ImDrawList*)draw_list;
    auto project = [&](const float w[3], ImVec2& out) {
        float v[4], c[4];
        for (int r = 0; r < 4; r++)
            v[r] = view[r] * w[0] + view[4 + r] * w[1] + view[8 + r] * w[2] + view[12 + r];
        for (int r = 0; r < 4; r++)
            c[r] = projection[r] * v[0] + projection[4 + r] * v[1] + projection[8 + r] * v[2] + projection[12 + r] * v[3];
        if (c[3] <= 1e-3f)
            return false;
        out = ImVec2(rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * rect[2], rect[1] + (0.5f - c[1] / c[3] * 0.5f) * rect[3]);
        return true;
    };
    using namespace rood_particles;
    int primary = rood_selection::primary().kind == rood_selection::KIND_EMITTER
        ? emitter_index(rood_selection::primary().id) : -1;
    for (int i = 0; emitters_shown() && i < emitter_count(); i++) {
        rood_selection::Ref ref = rood_scene::emitter_ref(i);
        if (rood_selection::hidden(ref))
            continue;
        Own o = own(i);
        bool chosen = rood_selection::contains(ref);
        /* Dust Off (the default): the authored emitters still show, dimmed (nothing of them is drawn) */
        int alpha = !o.enabled ? 45 : pc_dust ? 110 : 60;
        ImU32 col = i == primary ? IM_COL32(255, 230, 120, 230) : chosen ? IM_COL32(255, 230, 120, 140)
                                                                         : IM_COL32(200, 255, 190, alpha);
        std::string shape = field(i, "shape");
        float size[3];
        emitter_size(i, size);
        float c = cosf(o.yaw * DEG), s = sinf(o.yaw * DEG);
        if (shape == "box" || shape == "surface") {
            static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            ImVec2 q[8];
            bool ok[8];
            for (int k = 0; k < 8; k++) {
                float lx = (k & 1 ? 0.5f : -0.5f) * size[0], ly = (k & 2 ? 0.5f : -0.5f) * size[1],
                      lz = (k & 4 ? 0.5f : -0.5f) * size[2];
                float p[3] = { o.pos[0] + c * lx + s * lz, o.pos[1] + ly, o.pos[2] - s * lx + c * lz };
                ok[k] = project(p, q[k]);
            }
            for (const auto& e : edges)
                if (ok[e[0]] && ok[e[1]])
                    draw->AddLine(q[e[0]], q[e[1]], col, chosen ? 2.0f : 1.0f);
        } else if (shape == "point" || shape == "disc") {
            float r = (float)atof(field(i, "radius").c_str());
            ImVec2 pts[33];
            int n = 0;
            for (int k = 0; k <= 32; k++) {
                float a = k * 6.2831853f / 32, p[3] = { o.pos[0] + r * cosf(a), o.pos[1], o.pos[2] + r * sinf(a) };
                if (project(p, pts[n]))
                    n++;
            }
            if (n > 1)
                draw->AddPolyline(pts, n, col, 0, chosen ? 2.0f : 1.0f);
        }
        ImVec2 at;
        if (project(o.pos, at)) {
            draw->AddCircleFilled(at, 4.5f, col);
            char label[32];
            snprintf(label, sizeof label, pc_dust ? "emitter %d" : "emitter %d (Dust Off)", i + 1);
            draw->AddText(ImVec2(at.x + 7, at.y - 7), col, label);
            rood_scene::icon(ref, at.x, at.y, 9, true); /* a click picks it (not a ray: a room-sized box would hide the room) */
        }
    }
    if (primary < 0 || rood_lights::selected >= 0 || rood_lights::selected_actor >= 0)
        return false;
    Own o = own(primary);
    std::string shape = field(primary, "shape");
    bool ball = shape == "point" || shape == "disc"; /* R sizes its radius */
    float size[3];
    emitter_size(primary, size);
    if (ball)
        size[0] = size[1] = size[2] = fmaxf(2 * (float)atof(field(primary, "radius").c_str()), 2);
    float c = cosf(o.yaw * DEG), s = sinf(o.yaw * DEG);
    float model[16] = { c * size[0], 0, -s * size[0], 0, 0, size[1], 0, 0, s * size[2], 0, c * size[2], 0, o.pos[0], o.pos[1],
        o.pos[2], 1 };
    int operation = rood_lights::gizmo_operation;
    ImGuizmo::OPERATION op = operation == 2 ? ImGuizmo::SCALE : operation == 1 ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = operation == 0 && !pc_lights_gizmo_space ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    if (ImGuizmo::Manipulate(view, projection, op, mode, model)) {
        float sx = sqrtf(model[0] * model[0] + model[1] * model[1] + model[2] * model[2]);
        float sy = sqrtf(model[4] * model[4] + model[5] * model[5] + model[6] * model[6]);
        float sz = sqrtf(model[8] * model[8] + model[9] * model[9] + model[10] * model[10]);
        o.pos[0] = model[12], o.pos[1] = model[13], o.pos[2] = model[14];
        if (operation == 1 && sx > 1e-6f)
            o.yaw = atan2f(-model[2], model[0]) / DEG;
        set_own(primary, o);
        if (operation == 2) {
            if (ball) {
                char text[32];
                snprintf(text, sizeof text, "%.1f", fmaxf(fmaxf(sx, sy), sz) * 0.5f);
                set_field(primary, "radius", text);
            } else {
                float out[3] = { fmaxf(sx, 2), fmaxf(sy, 2), fmaxf(sz, 2) };
                set_emitter_size(primary, out);
            }
        }
        pc_undo_hint(operation == 0 ? "Move emitter" : operation == 2 ? "Size emitter" : "Turn emitter");
    }
    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}
