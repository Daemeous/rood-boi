// Fit rect light to window (editor_fit_window.h).
#include "editor_fit_window.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "imgui.h"
#include "editor_math.h"
#include "editor_overlays.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "lights_internal.h"
#include "scene_capture.h"

extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */

using rood_selection::Ref;

namespace rood_fit {
float offset = 2, kelvin = 5600;
}

namespace {
bool is_armed;
Ref aimed; /* the rect light to fit (empty: a new one) */

/* The pane under window pixel (x, y): false when there is no room surface there. */
bool pane_at(float x, float y, rood_math::Pane& pane, std::string* message)
{
    pc_overlay_view v;
    pc_overlay_pick pick = nullptr;
    float w[3], n[3];
    if (!pc_lights_tools_view(&v, &pick) || !pick || !pick(x, y, w, n)) {
        *message = "Fit to window: no surface there";
        return false;
    }
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data(&count);
    int seed = t ? pc_scene_triangle_at(w, 2.0f) : -1;
    if (seed < 0) {
        *message = "Fit to window: not a room surface (a character or a prop)";
        return false;
    }
    std::vector<rood_math::PaneTriangle> tris((size_t)count);
    for (int i = 0; i < count; i++) {
        for (int c = 0; c < 3; c++)
            memcpy(tris[i].v[c], t[i].vertex[c].position, sizeof tris[i].v[c]);
        tris[i].texture = (unsigned)t[i].tpage << 16 | t[i].clut;
    }
    float eye[3];
    rood_math::view_eye(v.view, eye);
    if (!rood_math::fit_pane(tris.data(), count, seed, eye, pane) || pane.width < 1 || pane.height < 1) {
        *message = "Fit to window: no flat pane there";
        return false;
    }
    /* The pane's plane through the clicked point (its triangles may sit a unit or two apart): the
     * offset is measured from the surface one clicked. */
    float d = 0;
    for (int k = 0; k < 3; k++)
        d += pane.normal[k] * (w[k] - pane.centre[k]);
    for (int k = 0; k < 3; k++)
        pane.centre[k] += pane.normal[k] * d;
    return true;
}
} // namespace

void rood_fit::arm(const Ref& light)
{
    is_armed = true;
    aimed = light;
}
bool rood_fit::armed() { return is_armed; }
Ref rood_fit::target() { return aimed; }
bool rood_fit::cancel()
{
    bool was = is_armed;
    is_armed = false;
    return was;
}
bool rood_fit::viewport_click(float x, float y)
{
    if (!is_armed)
        return false;
    is_armed = false;
    std::string message;
    if (fit_at(x, y, aimed, &message) < 0)
        snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
    return true;
}
int rood_fit::fit_at(float x, float y, const Ref& light, std::string* message)
{
    using rood_lights::lights;
    rood_math::Pane pane;
    if (!pane_at(x, y, pane, message))
        return -1;
    rood_lights::track_room();
    int index = light ? rood_scene::index_of(light) : -1;
    if (index < 0 || lights[index].type != 2) { /* a new rect light in daylight */
        rood_lights::Light l;
        l.type = 2;
        rood_math::kelvin_tint(kelvin, 0, l.color);
        lights.push_back(l);
        index = (int)lights.size() - 1;
    }
    rood_lights::Light& l = lights[index];
    for (int k = 0; k < 3; k++) {
        l.position[k] = pane.centre[k] + pane.normal[k] * offset;
        l.direction[k] = pane.normal[k];
    }
    l.size[0] = fminf(fmaxf(pane.width, 8.0f), 4096.0f);
    l.size[1] = fminf(fmaxf(pane.height, 8.0f), 4096.0f);
    rood_lights::dirty = true;
    rood_lights::select_light(index);
    pc_undo_hint("Fit rect light to window");
    snprintf(rood_lights::status, sizeof rood_lights::status, "Fitted a rect light to a %.0f x %.0f pane (%d triangles)",
        pane.width, pane.height, pane.triangles);
    *message = rood_lights::status;
    return index;
}
void rood_fit::hint(void* draw_list)
{
    if (!is_armed)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* front = ImGui::GetForegroundDrawList();
    const char* text = aimed ? "Click a window pane: the rect light fits it (Esc cancels)"
                             : "Click a window pane: a rect light on it (Esc cancels)";
    front->AddText(ImVec2(io.MousePos.x + 16, io.MousePos.y + 14), IM_COL32(255, 230, 120, 255), text);
    /* The pane under the pointer, outlined (recomputed when the pointer moves). */
    static float last[2] = { -1, -1 };
    static rood_math::Pane pane;
    static bool have;
    if (io.MousePos.x != last[0] || io.MousePos.y != last[1]) {
        std::string ignored;
        have = pane_at(io.MousePos.x, io.MousePos.y, pane, &ignored);
        last[0] = io.MousePos.x, last[1] = io.MousePos.y;
    }
    pc_overlay_view v;
    if (!have || !pc_lights_tools_view(&v, nullptr))
        return;
    ImDrawList* d = (ImDrawList*)draw_list;
    ImVec2 q[4];
    for (int c = 0; c < 4; c++) {
        float sr = (c == 1 || c == 2 ? 0.5f : -0.5f) * pane.width, su = (c >= 2 ? 0.5f : -0.5f) * pane.height, p[3], px[2];
        for (int k = 0; k < 3; k++)
            p[k] = pane.centre[k] + pane.right[k] * sr + pane.up[k] * su;
        if (!rood_math::project(v.view, v.projection, v.rect, p, px))
            return;
        q[c] = ImVec2(px[0], px[1]);
    }
    d->AddPolyline(q, 4, IM_COL32(120, 220, 255, 230), ImDrawFlags_Closed, 2.0f);
}
void rood_fit::options()
{
    ImGui::SliderFloat("Offset", &offset, 0, 32, "%.1f u");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("How far in front of the pane the rect light goes (settings key editor_fit_window_offset)");
    ImGui::SliderFloat("New light's colour", &kelvin, 1500, 12000, "%.0f K");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A new rect light's colour temperature (settings key editor_fit_window_kelvin); a fitted\n"
                          "light keeps its own colour");
}
bool rood_fit::parse_setting(const char* key, const char* rest)
{
    if (!strcmp(key, "editor_fit_window_offset")) {
        offset = fminf(fmaxf((float)atof(rest), 0.0f), 32.0f);
        return true;
    }
    if (!strcmp(key, "editor_fit_window_kelvin")) {
        kelvin = fminf(fmaxf((float)atof(rest), 1500.0f), 12000.0f);
        return true;
    }
    return false;
}
void rood_fit::write_settings(FILE* f)
{
    fprintf(f, "editor_fit_window_offset %g\neditor_fit_window_kelvin %g\n", offset, kelvin);
}
std::string rood_fit::state_json()
{
    char text[256];
    snprintf(text, sizeof text, "{\"armed\":%s,\"target\":%s,\"offset\":%g,\"kelvin\":%g}", is_armed ? "true" : "false",
        is_armed && aimed ? prefab::json_quote(aimed.id).c_str() : "null", offset, kelvin);
    return text;
}
