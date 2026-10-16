// The menu model's studio light in the editor (menu_light.h): the schema
// kind "menu_model_light" (editor_schema.h; one object, index 0), so Details
// draws it (rood_details::draw_kind; the dev tools' Graphics tab, "Menu
// model light") with its ranges, ↺ to the shipped values, the value ladder
// and undo, and RPC editor_schema reads and edits it {kind
// "menu_model_light"}. Its tools: save into the theme file, reset all.
#include <cmath>
#include <cstring>
#include <string>

#include "imgui.h"
#include "editor_schema.h"
#include "editor_undo.h"
#include "ui/menu_light.h"
#include "ui/menu_light_fields.h"

namespace {
using rood_schema::Field;
using rood_schema::Kind;
using rood_schema::Value;

pc_menu_light now()
{
    pc_menu_light l;
    pc_ui_menu_model_light(&l);
    return l;
}
pc_menu_light defaults()
{
    pc_menu_light d;
    pc_ui_menu_model_light_defaults(&d);
    return d;
}
float* member(pc_menu_light& l, const char* name) { return ui::menu_light_field(name)->at(l); }

Field number(const char* name, const char* label, const char* category, const char* unit, const char* tooltip)
{
    const ui::MenuLightField* m = ui::menu_light_field(name);
    pc_menu_light d = defaults();
    float span = m->max - m->min;
    Field f = rood_schema::number_field(name, label, category, m->min, m->max, span > 4 ? 0.05f : 0.01f,
        *m->at(d), unit, false, tooltip);
    std::string key = name;
    f.get = [key](int) {
        pc_menu_light l = now();
        return Value::scalar(*member(l, key.c_str()));
    };
    f.set = [key](int, const Value& v) {
        pc_menu_light l = now();
        *member(l, key.c_str()) = v.x();
        pc_ui_menu_model_light_set(&l);
    };
    return f;
}
Field colour(const char* name, const char* label, const char* category, const char* tooltip)
{
    const ui::MenuLightField* m = ui::menu_light_field(name);
    pc_menu_light d = defaults();
    const float* c = m->at(d);
    Field f = rood_schema::colour_field(name, label, category, m->max, Value::vec(c[0], c[1], c[2]), tooltip);
    std::string key = name;
    f.get = [key](int) {
        pc_menu_light l = now();
        const float* v = member(l, key.c_str());
        return Value::vec(v[0], v[1], v[2]);
    };
    f.set = [key](int, const Value& v) {
        pc_menu_light l = now();
        float* dst = member(l, key.c_str());
        for (int i = 0; i < 3; i++)
            dst[i] = v.at(i);
        pc_ui_menu_model_light_set(&l);
    };
    return f;
}
/* A light's direction as two angles. Both back at the shipped ones (↺):
 * the shipped vector itself, so the default rig is exactly the shipped one. */
Field angle(const char* light, bool elevation, const char* category)
{
    std::string dir = std::string(light) + "_direction";
    pc_menu_light d = defaults();
    float az, el;
    ui::menu_light_angles(member(d, dir.c_str()), &az, &el);
    std::string name = std::string(light) + (elevation ? "_elevation" : "_azimuth");
    Field f = elevation
        ? rood_schema::angle_field(name.c_str(), "Elevation", category, -90, 90, 0.5f, el,
              "Degrees above the model's horizon (view space: 90 straight above, -90 below)")
        : rood_schema::angle_field(name.c_str(), "Azimuth", category, -180, 180, 0.5f, az,
              "Degrees around the model, seen from above: 0 from the camera, positive to the right, 180 from behind");
    f.get = [dir, elevation](int) {
        pc_menu_light l = now();
        float a, e;
        ui::menu_light_angles(member(l, dir.c_str()), &a, &e);
        return Value::scalar(elevation ? e : a);
    };
    f.set = [dir, elevation, az, el](int, const Value& v) {
        pc_menu_light l = now();
        float* vec = member(l, dir.c_str());
        float a, e;
        ui::menu_light_angles(vec, &a, &e);
        (elevation ? e : a) = v.x();
        if (std::fabs(a - az) < 1e-3f && std::fabs(e - el) < 1e-3f) {
            pc_menu_light s = defaults();
            std::memcpy(vec, member(s, dir.c_str()), sizeof(float) * 3);
        } else {
            ui::menu_light_direction(a, e, vec);
        }
        pc_ui_menu_model_light_set(&l);
    };
    return f;
}

char status[320];

void tools(int)
{
    bool edited = ui::menu_light_edited();
    if (ImGui::Button("Save to theme.json")) {
        char error[256];
        if (pc_ui_menu_model_light_save(error, sizeof error))
            std::snprintf(status, sizeof status, "Not saved: %s", error);
        else
            std::snprintf(status, sizeof status, "Saved to the theme file.");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Write the rig into pc/assets/ui/theme.json's \"menu_model_light\" block (the file's "
                          "other keys kept). Unsaved, edits last until the theme file changes on disk.");
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        pc_menu_light d = defaults();
        pc_ui_menu_model_light_set(&d);
        pc_undo_hint("Reset menu model light");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Every field back to the shipped rig (the theme file is unchanged until saved)");
    if (edited)
        ImGui::TextDisabled("Edited: differs from the theme file");
    if (status[0])
        ImGui::TextWrapped("%s", status);
}

Kind make_kind()
{
    Kind k;
    k.name = "menu_model_light";
    k.label = "Menu model light";
    k.object = rood_selection::KIND_NONE;
    const char* lights[3][2] = { { "key", "Key light" }, { "fill", "Fill light" }, { "rim", "Rim light" } };
    const char* tips[3] = { "The main light", "Fills the key's shadow side", "Outlines the model from behind" };
    for (auto& light : lights) {
        std::string n = light[0];
        int i = (int)(&light - lights);
        k.fields.push_back(angle(light[0], false, light[1]));
        k.fields.push_back(angle(light[0], true, light[1]));
        k.fields.push_back(colour((n + "_color").c_str(), "Colour", light[1], "Linear colour (times intensity)"));
        k.fields.push_back(number((n + "_intensity").c_str(), "Intensity", light[1], "", tips[i]));
        k.fields.push_back(number((n + "_wrap").c_str(), "Wrap", light[1], "",
            "How far it wraps round to the far side: 0 Lambert (dark at 90 degrees), 1 the same everywhere"));
    }
    k.fields.push_back(number("rim_power", "Power", "Rim light", "",
        "Its falloff from the outline inward: (1 - n.v) to this power; higher is a thinner rim"));
    k.fields.push_back(number("rim_lift", "Lift", "Rim light", "",
        "Added to the texture's colour under the rim, so dark texels still catch it"));
    k.fields.push_back(colour("ambient_sky", "Sky", "Ambient", "Linear colour on upward faces"));
    k.fields.push_back(colour("ambient_ground", "Ground", "Ambient", "Linear colour on downward faces"));
    k.fields.push_back(number("ambient_intensity", "Intensity", "Ambient", "", "Scale on the sky and ground"));
    k.fields.push_back(number("exposure", "Exposure", "Output", "x", "Scale on the lit colour"));
    k.fields.push_back(number("saturation", "Saturation", "Output", "", "0 grey, 1 as lit, above 1 more colourful"));
    k.closed = { "Ambient", "Output" };
    k.extras = tools;
    return k;
}

/* Registered at start-up (the schema's registry is built on first use). */
struct Register {
    Register() { rood_schema::add_kind(make_kind()); }
} registered;
} // namespace
