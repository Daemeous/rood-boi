// A fog volume's handles (editor_handles.h): the colour swatch (the colour
// popover on its albedo), the density ring around its centre (logarithmic,
// as a light's intensity) and the soft edge's dot on its inner outline (the
// full-density core, half size x (1 - edge)). R sizes it (atmosphere.cpp's
// gizmo). Built from the fog schema's fields (atmosphere.cpp schema_kind):
// a drag changes exactly one of them.
#include "editor_handles.h"

#include <math.h>
#include <array>
#include <string>

#include "imgui.h"
#include "editor_math.h"
#include "editor_scene.h"

using prefab::Value;
using rood_handles::Drag;
using rood_handles::Handle;
using rood_selection::Ref;

namespace {
using V3 = std::array<float, 3>;
const float DEG = 3.14159265f / 180.0f;
float em() { return ImGui::GetStyle().FontScaleMain; }
bool pixel(const pc_overlay_view& v, const V3& w, float out[2]) { return rood_math::project(v.view, v.projection, v.rect, w.data(), out); }
V3 along(const V3& p, const V3& u, float s) { return { p[0] + u[0] * s, p[1] + u[1] * s, p[2] + u[2] * s }; }
uint32_t colour_of(const Value& c)
{
    return IM_COL32((int)(fminf(fmaxf(c.at(0), 0), 1) * 255), (int)(fminf(fmaxf(c.at(1), 0), 1) * 255),
        (int)(fminf(fmaxf(c.at(2), 0), 1) * 255), 255);
}
/* A volume's frame from its fields (pos, size, yaw, shape): atmosphere.cpp's
 * corner() turn (x' = (cos, 0, -sin), z' = (sin, 0, cos)). */
struct Frame {
    V3 centre, axis[3];
    float half[3];
    bool sphere;
};
Frame frame_of(const Value& pos, const Value& size, float yaw, bool sphere)
{
    Frame f;
    float c = cosf(yaw * DEG), s = sinf(yaw * DEG);
    f.centre = { pos.at(0), pos.at(1), pos.at(2) };
    f.axis[0] = { c, 0, -s }, f.axis[1] = { 0, 1, 0 }, f.axis[2] = { s, 0, c };
    for (int k = 0; k < 3; k++)
        f.half[k] = size.at(k) * 0.5f;
    f.sphere = sphere;
    return f;
}
Frame frame_now(const Ref& r)
{
    return frame_of(rood_handles::get_value(r, "pos"), rood_handles::get_value(r, "size"), rood_handles::get(r, "yaw"),
        rood_handles::get(r, "shape") > 0.5f);
}
Frame frame_at_press(const Drag& d)
{
    return frame_of(d.field("pos"), d.field("size"), d.field("yaw").x(), d.field("shape").x() > 0.5f);
}
/* Density: the light's intensity ring (100 px right doubles it, Ctrl a
 * tenth, Shift 1-2-5), in units of 1e-3 so log_drag's floor (0.01) is
 * 1e-5 here, below any fog worth seeing. */
const float DENSITY_UNIT = 1e-3f;
Handle density_ring(const Ref& r, float cx, float cy, const Value& colour)
{
    Handle h;
    h.name = "density", h.label = "Density";
    h.shape = rood_handles::SHAPE_RING;
    h.cx = cx, h.cy = cy, h.radius = 22 * em();
    h.x = cx + h.radius, h.y = cy;
    h.colour = colour_of(colour);
    h.drag = [r](const Drag& d) {
        float v = rood_math::log_drag(d.field("density").x() / DENSITY_UNIT, d.now[0] - d.press[0], d.fine);
        rood_handles::set(r, "density", Value::scalar((d.snap ? rood_math::snap_125(v) : v) * DENSITY_UNIT));
    };
    h.readout = [r]() { return rood_handles::readout(r, "density"); };
    float gx = h.x, gy = h.y;
    h.where = [r, gx, gy](const Value& v, float out[2]) {
        float now = rood_handles::get(r, "density");
        if (v.x() <= 0 || now <= 0)
            return false;
        out[0] = gx + 100 * log2f(v.x() / now), out[1] = gy;
        return true;
    };
    return h;
}
/* The axis of x and z more across the view (the dot slides on screen, not toward the eye). */
int across_axis(const pc_overlay_view& v, const Frame& f)
{
    float r[3], u[3], fw[3];
    rood_math::view_axes(v.view, v.projection, v.rect, r, u, fw);
    float dx = fabsf(f.axis[0][0] * fw[0] + f.axis[0][1] * fw[1] + f.axis[0][2] * fw[2]);
    float dz = fabsf(f.axis[2][0] * fw[0] + f.axis[2][1] * fw[1] + f.axis[2][2] * fw[2]);
    return dz < dx ? 2 : 0;
}
/* Soft edge: a dot on the core's outline along that axis (at half x (1 - edge));
 * dragged out the core grows (edge falls). Ctrl a tenth; Shift steps of 0.05. */
Handle edge_dot(const pc_overlay_view& v, const Ref& r, const Frame& f)
{
    int a = across_axis(v, f);
    float edge = rood_handles::get(r, "edge");
    Handle h;
    h.name = "edge", h.label = "Soft edge";
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(f.centre, f.axis[a], f.half[a] * (1 - edge)), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = IM_COL32(255, 170, 90, 255);
    h.drag = [r, a](const Drag& d) {
        Frame f0 = frame_at_press(d);
        float half = fmaxf(f0.half[a], 1e-3f);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, f0.centre.data(), f0.axis[a].data());
        float t1 = rood_math::line_param(d.origin, d.dir, f0.centre.data(), f0.axis[a].data());
        float s = rood_math::linear_drag(half * (1 - d.field("edge").x()), t0, t1, d.fine);
        float e = fminf(fmaxf(1 - s / half, 0.01f), 1.0f);
        rood_handles::set(r, "edge", Value::scalar(d.snap ? rood_math::snap_step(e, 0.05f) : e));
    };
    h.readout = [r]() { return rood_handles::readout(r, "edge"); };
    pc_overlay_view view = v;
    Frame at = f;
    h.where = [view, at, a](const Value& value, float out[2]) {
        return pixel(view, along(at.centre, at.axis[a], at.half[a] * (1 - value.x())), out);
    };
    return h;
}
} // namespace

void rood_handles::volume_handles(const pc_overlay_view& v, const Ref& r, int, std::vector<Handle>& out)
{
    Frame f = frame_now(r);
    float c[2];
    if (!pixel(v, f.centre, c))
        return;
    Value colour = rood_handles::get_value(r, "color");
    Handle ring = density_ring(r, c[0], c[1], colour);
    out.push_back(ring);
    out.push_back(edge_dot(v, r, f));
    float rgb[3] = { colour.at(0), colour.at(1), colour.at(2) };
    out.push_back(rood_handles::colour_swatch(r, c[0], c[1], ring.radius, rgb));
}

/* Its core (the part at full density) as a thin outline inside its own. */
void rood_handles::volume_extras(const pc_overlay_view& v, int index, void* draw_list)
{
    Ref r = rood_scene::volume_ref(index);
    if (!r)
        return;
    Frame f = frame_now(r);
    float inner = 1 - fminf(fmaxf(rood_handles::get(r, "edge"), 0.0f), 1.0f);
    ImDrawList* draw = (ImDrawList*)draw_list;
    ImU32 col = IM_COL32(255, 170, 90, 170);
    auto at = [&](float x, float y, float z) {
        V3 p = along(along(along(f.centre, f.axis[0], x * f.half[0] * inner), f.axis[1], y * f.half[1] * inner), f.axis[2],
            z * f.half[2] * inner);
        return p;
    };
    if (f.sphere) {
        for (int axis = 0; axis < 3; axis++) {
            ImVec2 pts[33];
            int n = 0;
            for (int k = 0; k <= 32; k++) {
                float a = k * 6.2831853f / 32, u = cosf(a), w = sinf(a), px[2];
                V3 p = axis == 0 ? at(u, w, 0) : axis == 1 ? at(u, 0, w) : at(0, u, w);
                if (pixel(v, p, px))
                    pts[n++] = ImVec2(px[0], px[1]);
            }
            if (n > 1)
                draw->AddPolyline(pts, n, col, 0, 1.0f);
        }
        return;
    }
    static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 },
        { 1, 5 }, { 2, 6 }, { 3, 7 } };
    float q[8][2];
    bool ok[8];
    for (int c = 0; c < 8; c++)
        ok[c] = pixel(v, at(c & 1 ? 1 : -1, c & 2 ? 1 : -1, c & 4 ? 1 : -1), q[c]);
    for (const auto& e : edges)
        if (ok[e[0]] && ok[e[1]])
            draw->AddLine(ImVec2(q[e[0]][0], q[e[0]][1]), ImVec2(q[e[1]][0], q[e[1]][1]), col, 1.0f);
}
