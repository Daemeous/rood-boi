// The colour popover (editor_colour.h).
#include "editor_colour.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_handles.h"
#include "editor_math.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lights_internal.h"

using prefab::Value;
using rood_selection::Ref;

extern "C" int pc_present_sample_request(float x, float y); /* gl_present.c */
extern "C" int pc_present_sample_result(float rgb[3]);

namespace {
using RGB = std::array<float, 3>;
int tabs[5]; /* the last tab per type: point, spot, rect, sun, fog volume */
std::vector<RGB> recent; /* newest first, at most 8 */
struct Stop {
    float kelvin;
    const char* name;
};
const Stop STOPS[] = { { 1900, "Candle" }, { 2700, "Tungsten" }, { 3200, "Halogen" }, { 4100, "Moonlight" },
    { 5600, "Daylight" }, { 6500, "Overcast" }, { 10000, "Blue sky" } };

bool shown;
Ref target;
float anchor[2];
int tab = rood_colour::COLOUR_TAB_TEMPERATURE;
int select_frames; /* the remembered tab is selected over the frames after opening */
int opened_frame = -100, shown_frame = -100;
float kelvin = 6500, tint = 0;
RGB estimated_for = { -1, -1, -1 }; /* the colour kelvin and tint were read from */
char hex[16];
bool hex_active;
bool armed, waiting; /* the eyedropper: armed, then a pixel asked for */
std::map<std::string, ImVec4> rects; /* window pixels: x, y, w, h (RPC) */

int type_index(const Ref& r)
{
    if (r.kind == rood_selection::KIND_SUN)
        return 3;
    if (r.kind == rood_selection::KIND_VOLUME)
        return 4;
    int i = rood_scene::index_of(r);
    if (r.kind != rood_selection::KIND_LIGHT || i < 0)
        return 0;
    int type = rood_lights::lights[i].type;
    return type == 1 ? 1 : type == 2 ? 2 : 0;
}
bool colour_of(const Ref& r, float out[3])
{
    if (r.kind == rood_selection::KIND_SUN) {
        rood_env::SunState s = rood_env::sun_state();
        memcpy(out, s.color, sizeof s.color);
        return s.defined;
    }
    if ((r.kind != rood_selection::KIND_LIGHT && r.kind != rood_selection::KIND_VOLUME) || rood_scene::index_of(r) < 0)
        return false;
    Value v = rood_handles::get_value(r, "color");
    for (int k = 0; k < 3; k++)
        out[k] = v.at(k);
    return true;
}
void remember(const float rgb[3])
{
    RGB c = { rgb[0], rgb[1], rgb[2] };
    for (size_t i = 0; i < recent.size(); i++)
        if (fabsf(recent[i][0] - c[0]) + fabsf(recent[i][1] - c[1]) + fabsf(recent[i][2] - c[2]) < 0.006f) {
            recent.erase(recent.begin() + i);
            break;
        }
    recent.insert(recent.begin(), c);
    if (recent.size() > 8)
        recent.resize(8);
}
/* The colour to the target (0..1), one undo step per change; `keep`: into the recent colours. */
void apply(const float in[3], bool keep, const char* step = "Edit Colour")
{
    float rgb[3];
    for (int k = 0; k < 3; k++)
        rgb[k] = fminf(fmaxf(in[k], 0.0f), 1.0f);
    if (target.kind == rood_selection::KIND_SUN) {
        rood_env::SunState s = rood_env::sun_state();
        memcpy(s.color, rgb, sizeof rgb);
        rood_env::set_sun_state(s);
    } else {
        rood_handles::set(target, "color", Value::vec(rgb[0], rgb[1], rgb[2]));
    }
    pc_undo_hint(step);
    if (keep)
        remember(rgb);
}
/* The colours this room's lights use (lights, its actors' lights, the sun): each once, at most 12. */
std::vector<RGB> palette()
{
    std::vector<RGB> out;
    auto add = [&](const float c[3]) {
        for (const RGB& p : out)
            if (fabsf(p[0] - c[0]) + fabsf(p[1] - c[1]) + fabsf(p[2] - c[2]) < 0.02f)
                return;
        if (out.size() < 12)
            out.push_back({ c[0], c[1], c[2] });
    };
    for (const rood_lights::Light& l : rood_lights::lights)
        add(l.color);
    std::vector<prefab::EvalLight> ls;
    for (const prefab::Actor& a : rood_lights::actors) {
        rood_lights::actor_lights(a, ls, false);
        for (const prefab::EvalLight& e : ls)
            add(e.color);
    }
    rood_env::SunState s = rood_env::sun_state();
    if (s.defined)
        add(s.color);
    return out;
}
void keep_rect(const std::string& name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    rects[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
void set_tab(int t)
{
    tab = t;
    tabs[type_index(target)] = t;
}
/* The Kelvin strip: blackbody colours from 1000 to 12000 K (even in mired),
 * ticks at the named stops, the current one marked; click or drag to pick. */
bool kelvin_strip()
{
    float w = ImGui::GetFontSize() * 16, h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##kelvin", ImVec2(w, h));
    keep_rect("kelvin_strip");
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int N = 48;
    for (int i = 0; i < N; i++) {
        float c[3];
        rood_math::kelvin_tint(rood_math::kelvin_strip_k((i + 0.5f) / N), 0, c);
        d->AddRectFilled(ImVec2(p.x + w * i / N, p.y), ImVec2(p.x + w * (i + 1) / N + 1, p.y + h),
            IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), 255));
    }
    for (const Stop& s : STOPS) {
        float x = p.x + w * rood_math::kelvin_strip_u(s.kelvin);
        d->AddLine(ImVec2(x, p.y + h * 0.65f), ImVec2(x, p.y + h), IM_COL32(0, 0, 0, 200), 1.5f);
    }
    float x = p.x + w * rood_math::kelvin_strip_u(kelvin);
    d->AddRect(ImVec2(x - 3, p.y - 2), ImVec2(x + 3, p.y + h + 2), IM_COL32(255, 255, 255, 255), 1, 0, 2.0f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
        ImGui::SetTooltip("%.0f K", rood_math::kelvin_strip_k(fminf(fmaxf((ImGui::GetIO().MousePos.x - p.x) / w, 0.0f), 1.0f)));
    if (!ImGui::IsItemActive())
        return false;
    kelvin = rood_math::kelvin_strip_k(fminf(fmaxf((ImGui::GetIO().MousePos.x - p.x) / w, 0.0f), 1.0f));
    return true;
}
void temperature_tab(float rgb[3])
{
    bool changed = kelvin_strip();
    bool done = ImGui::IsItemDeactivated();
    for (int i = 0; i < (int)(sizeof STOPS / sizeof STOPS[0]); i++) { /* the named stops */
        if (i % 4)
            ImGui::SameLine();
        char label[48];
        snprintf(label, sizeof label, "%s##stop", STOPS[i].name);
        if (ImGui::SmallButton(label)) {
            kelvin = STOPS[i].kelvin;
            changed = done = true;
        }
        keep_rect("stop_" + std::to_string((int)STOPS[i].kelvin));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%.0f K", STOPS[i].kelvin);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    changed |= ImGui::DragFloat("Kelvin", &kelvin, 10, 1000, 12000, "%.0f K");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("kelvin");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    changed |= ImGui::SliderFloat("Tint", &tint, -1, 1, "%.2f");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("tint");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Magenta (-1) to green (+1)");
    if (changed || done) {
        float c[3];
        rood_math::kelvin_tint(kelvin, tint, c);
        apply(c, done);
        memcpy(rgb, c, sizeof c);
        estimated_for = { c[0], c[1], c[2] }; /* keep the Kelvin and tint as chosen */
    }
}
void colour_tab(float rgb[3])
{
    float c[3] = { rgb[0], rgb[1], rgb[2] };
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    bool changed = ImGui::ColorPicker3("##wheel", c,
        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs
            | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("wheel");
    float value = fmaxf(c[0], fmaxf(c[1], c[2]));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::SliderFloat("Value", &value, 0, 1, "%.3f")) {
        float m = fmaxf(c[0], fmaxf(c[1], c[2]));
        for (int k = 0; k < 3; k++)
            c[k] = m > 1e-6f ? c[k] / m * value : value;
        changed = true;
    }
    done |= ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("value");
    if (changed || done) {
        apply(c, done);
        memcpy(rgb, c, sizeof c);
    }
}
/* Under both tabs: the room's palette, recent colours, Hex and RGB. */
void shared(float rgb[3])
{
    auto swatches = [&](const char* title, const char* name, const std::vector<RGB>& colours, const char* none) {
        ImGui::SeparatorText(title);
        for (size_t i = 0; i < colours.size(); i++) {
            if (i % 8)
                ImGui::SameLine();
            ImGui::PushID((std::string(name) + std::to_string(i)).c_str());
            if (ImGui::ColorButton("##swatch", ImVec4(colours[i][0], colours[i][1], colours[i][2], 1),
                    ImGuiColorEditFlags_NoAlpha)) {
                apply(colours[i].data(), true);
                memcpy(rgb, colours[i].data(), 3 * sizeof(float));
            }
            keep_rect(name + std::to_string(i));
            ImGui::PopID();
        }
        if (colours.empty())
            ImGui::TextDisabled("%s", none);
    };
    swatches("Room palette", "palette", palette(), "No lights yet");
    swatches("Recent", "recent", std::vector<RGB>(recent), "None yet");
    ImGui::Separator();
    if (ImGui::Button(armed ? "Click the picture..." : "Eyedropper"))
        armed = !armed, waiting = false;
    keep_rect("eyedropper");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Pick a colour from the picture: click here, then on the view.\n"
                          "Its brightest channel becomes 1 (Intensity keeps the brightness).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    if (!hex_active)
        snprintf(hex, sizeof hex, "%s", rood_math::format_hex(rgb).c_str());
    if (ImGui::InputText("Hex", hex, sizeof hex, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        float c[3];
        if (rood_math::parse_hex(hex, c)) {
            apply(c, true);
            memcpy(rgb, c, sizeof c);
        }
    }
    hex_active = ImGui::IsItemActive();
    keep_rect("hex");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    float c[3] = { rgb[0], rgb[1], rgb[2] };
    if (ImGui::DragFloat3("RGB", c, 0.005f, 0, 1, "%.3f")) {
        apply(c, false);
        memcpy(rgb, c, sizeof c);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        remember(rgb);
    keep_rect("rgb");
}
std::string colours_json(const std::vector<RGB>& colours)
{
    std::string s = "[";
    for (size_t i = 0; i < colours.size(); i++) {
        char c[64];
        snprintf(c, sizeof c, "%s[%.4f,%.4f,%.4f]", i ? "," : "", colours[i][0], colours[i][1], colours[i][2]);
        s += c;
    }
    return s + "]";
}
} // namespace

void rood_colour::open(const Ref& r, float x, float y)
{
    target = r;
    anchor[0] = x, anchor[1] = y;
    shown = true;
    armed = waiting = false;
    tab = tabs[type_index(r)];
    select_frames = 2;
    opened_frame = ImGui::GetFrameCount();
    estimated_for = { -1, -1, -1 };
    hex_active = false;
}
bool rood_colour::is_open() { return shown; }
void rood_colour::close()
{
    shown = false;
    armed = false;
}
bool rood_colour::cancel()
{
    if (armed || waiting) {
        armed = waiting = false;
        return true;
    }
    if (!shown)
        return false;
    close();
    return true;
}
bool rood_colour::viewport_click(float x, float y)
{
    if (!armed || !shown)
        return false;
    armed = false;
    waiting = pc_present_sample_request(x, y) != 0;
    return true;
}
void rood_colour::draw()
{
    shown_frame = ImGui::GetFrameCount();
    rects.clear();
    /* The pixel the eyedropper asked for has arrived: display to linear, brightest channel 1. */
    float picked[3];
    if (waiting && pc_present_sample_result(picked)) {
        waiting = false;
        float c[3];
        for (int k = 0; k < 3; k++)
            c[k] = rood_math::srgb_to_linear(picked[k]);
        rood_math::normalize_colour(c);
        if (rood_scene::exists(target))
            apply(c, true, "Pick colour");
    }
    if (!shown)
        return;
    float rgb[3];
    if (target != rood_selection::primary() || !colour_of(target, rgb)) { /* the selection moved on */
        close();
        return;
    }
    if (fabsf(rgb[0] - estimated_for[0]) + fabsf(rgb[1] - estimated_for[1]) + fabsf(rgb[2] - estimated_for[2]) > 0.004f) {
        rood_math::estimate_kelvin_tint(rgb, &kelvin, &tint);
        estimated_for = { rgb[0], rgb[1], rgb[2] };
    }
    ImGui::SetNextWindowPos(ImVec2(anchor[0] + 12, anchor[1] + 12), ImGuiCond_Appearing);
    bool open_now = true;
    if (ImGui::Begin("Colour##popover", &open_now,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("%s", rood_scene::label(target).c_str());
        if (ImGui::BeginTabBar("##colour tabs")) {
            bool first = select_frames > 0;
            bool on = ImGui::BeginTabItem("Temperature", nullptr,
                first && tab == COLOUR_TAB_TEMPERATURE ? ImGuiTabItemFlags_SetSelected : 0);
            keep_rect("tab_temperature");
            if (on) {
                if (!first && tab != COLOUR_TAB_TEMPERATURE)
                    set_tab(COLOUR_TAB_TEMPERATURE);
                temperature_tab(rgb);
                ImGui::EndTabItem();
            }
            on = ImGui::BeginTabItem("Colour", nullptr, first && tab == COLOUR_TAB_WHEEL ? ImGuiTabItemFlags_SetSelected : 0);
            keep_rect("tab_colour");
            if (on) {
                if (!first && tab != COLOUR_TAB_WHEEL)
                    set_tab(COLOUR_TAB_WHEEL);
                colour_tab(rgb);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        shared(rgb);
    }
    bool inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
    if (select_frames > 0)
        --select_frames;
    /* Closed by its button, or by a click elsewhere (not on a handle: the swatch toggles it). */
    if (!open_now
        || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside && !armed && !waiting && !rood_handles::hot()
            && ImGui::GetFrameCount() > opened_frame + 1))
        close();
}
std::string rood_colour::state_json()
{
    auto q = [](const std::string& s) { return prefab::json_quote(s); };
    bool current = shown && shown_frame >= ImGui::GetFrameCount() - 1;
    float rgb[3] = { 0, 0, 0 };
    if (shown)
        colour_of(target, rgb);
    char head[400];
    snprintf(head, sizeof head,
        "{\"open\":%s,\"subject\":%s,\"tab\":\"%s\",\"tabs\":[%d,%d,%d,%d,%d],\"kelvin\":%.1f,\"tint\":%.3f,"
        "\"colour\":[%.4f,%.4f,%.4f],\"armed\":%s,\"waiting\":%s",
        current ? "true" : "false", shown ? q(target.id).c_str() : "null", tab == COLOUR_TAB_WHEEL ? "colour" : "temperature",
        tabs[0], tabs[1], tabs[2], tabs[3], tabs[4], kelvin, tint, rgb[0], rgb[1], rgb[2], armed ? "true" : "false",
        waiting ? "true" : "false");
    std::string s = std::string(head) + ",\"palette\":" + colours_json(palette()) + ",\"recent\":" + colours_json(recent)
        + ",\"rects\":{";
    bool first = true;
    for (const auto& kv : rects) {
        if (!current)
            break;
        char r[96];
        snprintf(r, sizeof r, "[%.1f,%.1f,%.1f,%.1f]", kv.second.x, kv.second.y, kv.second.z, kv.second.w);
        s += (first ? "" : ",") + q(kv.first) + ":" + r;
        first = false;
    }
    return s + "}}";
}
bool rood_colour::rpc(const prefab::Json& p, std::string* error)
{
    if (const prefab::Json* o = p.get("open")) {
        bool on = o->type == prefab::Json::Bool ? o->boolean : o->number != 0;
        if (!on) {
            close();
        } else {
            Ref r = rood_selection::primary();
            if (r.kind != rood_selection::KIND_LIGHT && r.kind != rood_selection::KIND_SUN
                && r.kind != rood_selection::KIND_VOLUME) {
                *error = "the colour popover needs a light, the sun or a fog volume selected";
                return false;
            }
            float at[2] = { 0, 0 };
            rood_handles::handle_pixel("colour", at);
            open(r, at[0], at[1]);
        }
    }
    bool edits = p.get("tab") || p.get("kelvin") || p.get("tint") || p.get("hex") || p.get("rgb") || p.get("palette")
        || p.get("recent");
    if (!edits)
        return true;
    if (!shown) {
        *error = "the colour popover is not open (open: true)";
        return false;
    }
    if (const prefab::Json* t = p.get("tab")) {
        set_tab(t->string == "colour" ? COLOUR_TAB_WHEEL : COLOUR_TAB_TEMPERATURE);
        select_frames = 2;
    }
    if (p.get("kelvin") || p.get("tint")) {
        if (const prefab::Json* k = p.get("kelvin"))
            kelvin = (float)k->number;
        if (const prefab::Json* t = p.get("tint"))
            tint = (float)t->number;
        float c[3];
        rood_math::kelvin_tint(kelvin, tint, c);
        apply(c, true);
        estimated_for = { c[0], c[1], c[2] };
    }
    if (const prefab::Json* h = p.get("hex")) {
        float c[3];
        if (!rood_math::parse_hex(h->string, c)) {
            *error = "hex: #RRGGBB";
            return false;
        }
        apply(c, true);
    }
    if (const prefab::Json* c = p.get("rgb")) {
        if (c->type != prefab::Json::Array || c->items.size() != 3) {
            *error = "rgb: [r, g, b] (0..1)";
            return false;
        }
        float v[3] = { (float)c->items[0].number, (float)c->items[1].number, (float)c->items[2].number };
        apply(v, true);
    }
    for (const char* list : { "palette", "recent" })
        if (const prefab::Json* i = p.get(list)) {
            std::vector<RGB> colours = !strcmp(list, "palette") ? palette() : recent;
            int k = (int)i->number;
            if (k < 0 || k >= (int)colours.size()) {
                *error = std::string(list) + ": no colour " + std::to_string(k);
                return false;
            }
            apply(colours[k].data(), true);
        }
    return true;
}
bool rood_colour::parse_setting(const char* key, const char* rest)
{
    if (!strcmp(key, "editor_colour_tabs")) {
        int t[5] = { 0, 0, 0, 0, 0 };
        sscanf(rest, "%d %d %d %d %d", &t[0], &t[1], &t[2], &t[3], &t[4]); /* four from before fog volumes: the fifth 0 */
        for (int k = 0; k < 5; k++)
            tabs[k] = t[k] == COLOUR_TAB_WHEEL ? COLOUR_TAB_WHEEL : COLOUR_TAB_TEMPERATURE;
        return true;
    }
    if (!strcmp(key, "editor_recent_colours")) {
        recent.clear();
        const char* p = rest;
        RGB c;
        int used = 0;
        while (recent.size() < 8 && sscanf(p, " %f,%f,%f%n", &c[0], &c[1], &c[2], &used) == 3) {
            recent.push_back(c);
            p += used;
        }
        return true;
    }
    return false;
}
void rood_colour::write_settings(FILE* f)
{
    fprintf(f, "editor_colour_tabs %d %d %d %d %d\n", tabs[0], tabs[1], tabs[2], tabs[3], tabs[4]);
    if (recent.empty())
        return;
    fprintf(f, "editor_recent_colours");
    for (const RGB& c : recent)
        fprintf(f, " %.3f,%.3f,%.3f", c[0], c[1], c[2]);
    fputc('\n', f);
}
