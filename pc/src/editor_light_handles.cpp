// Each kind's handles (editor_handles.h), built every frame from the object
// as it is; their drag functions work from the object as the drag began
// (Drag::light), so a handle that moves the object it is on stays steady.
// Point: the intensity ring (logarithmic), the radius dot, the shadows toggle.
#include "editor_handles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <string>

#include "imgui.h"
#include "editor_math.h"
#include "editor_colour.h"
#include "editor_scene.h"
#include "editor_transform.h"
#include "editor_undo.h"

using prefab::Value;
using rood_handles::Drag;
using rood_handles::Handle;
using rood_lights::Light;
using rood_lights::lights;
using rood_selection::Ref;

namespace {
using V3 = std::array<float, 3>;
const float DEG = 3.14159265f / 180.0f;
float em() { return ImGui::GetStyle().FontScaleMain; }
V3 vec3(const float* p) { return { p[0], p[1], p[2] }; }
V3 along(const V3& p, const V3& u, float s) { return { p[0] + u[0] * s, p[1] + u[1] * s, p[2] + u[2] * s }; }
uint32_t colour_of(const float c[3])
{
    return IM_COL32((int)(fminf(c[0], 1) * 255), (int)(fminf(c[1], 1) * 255), (int)(fminf(c[2], 1) * 255), 255);
}
bool pixel(const pc_overlay_view& v, const V3& w, float out[2]) { return rood_math::project(v.view, v.projection, v.rect, w.data(), out); }
void frame_of(const Light& l, V3& forward, V3& right, V3& up)
{
    rood_math::light_frame(l.direction, forward.data(), right.data(), up.data());
}
V3 view_right(const pc_overlay_view& v)
{
    float r[3], u[3], f[3];
    rood_math::view_axes(v.view, v.projection, v.rect, r, u, f);
    return vec3(r);
}

/* The intensity ring around an icon (cx, cy): dragged right it doubles every
 * 100 pixels, left halves (Ctrl: every 1000; Shift: 1, 2, 5 steps). */
Handle ring(float cx, float cy, uint32_t colour, const char* label, std::function<float(const Drag&)> start,
    std::function<void(float)> apply, std::function<float()> now, std::function<std::string()> readout)
{
    Handle h;
    h.name = "intensity", h.label = label;
    h.shape = rood_handles::SHAPE_RING;
    h.cx = cx, h.cy = cy, h.radius = 22 * em();
    h.x = cx + h.radius, h.y = cy;
    h.colour = colour;
    h.drag = [start, apply](const Drag& d) {
        float v = rood_math::log_drag(start(d), d.now[0] - d.press[0], d.fine);
        apply(d.snap ? rood_math::snap_125(v) : v);
    };
    h.readout = readout;
    float gx = h.x, gy = h.y;
    h.where = [now, gx, gy](const Value& v, float out[2]) {
        if (v.x() <= 0)
            return false;
        out[0] = gx + 100 * log2f(v.x() / fmaxf(now(), 0.01f)), out[1] = gy;
        return true;
    };
    return h;
}
/* A dot on the world line p + s u (u unit) at s, dragged along it: `line`
 * gives the line from the object as the drag began; the field's value is its
 * start plus the change of s (Ctrl a tenth; Shift multiples of `step`). */
Handle line_dot(const pc_overlay_view& v, const Ref& r, const char* name, const char* field, const char* label, const V3& p,
    const V3& u, float s, std::function<void(const Drag&, V3&, V3&)> line, std::function<float(const Drag&)> start,
    float step)
{
    Handle h;
    h.name = name, h.label = label;
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(p, u, s), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = IM_COL32(255, 230, 120, 255);
    std::string f = field;
    h.drag = [r, f, line, start, step](const Drag& d) {
        V3 lp, lu;
        line(d, lp, lu);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, lp.data(), lu.data());
        float t1 = rood_math::line_param(d.origin, d.dir, lp.data(), lu.data());
        float value = rood_math::linear_drag(start(d), t0, t1, d.fine);
        rood_handles::set(r, f.c_str(), Value::scalar(d.snap ? rood_math::snap_step(value, step) : value));
    };
    h.readout = [r, f]() { return rood_handles::readout(r, f.c_str()); };
    pc_overlay_view view = v;
    h.where = [view, p, u](const Value& value, float out[2]) { return pixel(view, along(p, u, value.x()), out); };
    return h;
}
/* The point light's radius: a dot on its sphere's outline, to the right on screen. */
Handle radius_dot(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    return line_dot(v, r, "radius", "radius", "Radius", vec3(l.position), view_right(v), l.radius,
        [](const Drag& d, V3& p, V3& u) { p = vec3(d.light.position), u = view_right(d.view); },
        [](const Drag& d) { return d.light.radius; }, 16);
}
/* Casts shadows: a toggle on top of the ring (dark when on). */
Handle shadows_toggle(const Ref& r, float cx, float cy, float ring_radius)
{
    Handle h;
    h.name = "shadows", h.label = "Casts shadows";
    h.shape = rood_handles::SHAPE_TOGGLE;
    h.x = cx, h.y = cy - ring_radius, h.radius = 5 * em();
    h.colour = rood_handles::get(r, "shadows") > 0.5f ? IM_COL32(40, 40, 48, 255) : IM_COL32(235, 235, 235, 255);
    h.click = [r]() { rood_handles::set(r, "shadows", Value::scalar(rood_handles::get(r, "shadows") > 0.5f ? 0.0f : 1.0f)); };
    h.readout = [r]() { return rood_handles::readout(r, "shadows"); };
    return h;
}
/* The colour swatch, below right of the ring: a click opens (or closes) the colour popover. */
Handle swatch(const Ref& r, float cx, float cy, float ring_radius, const float colour[3])
{
    Handle h;
    h.name = "colour", h.label = "Colour";
    h.shape = rood_handles::SHAPE_SQUARE;
    h.x = cx + ring_radius * 0.75f + 10 * em(), h.y = cy + ring_radius * 0.75f + 10 * em(), h.radius = 6 * em();
    h.colour = colour_of(colour);
    float x = h.x, y = h.y;
    h.click = [r, x, y]() {
        if (rood_colour::is_open())
            rood_colour::close();
        else
            rood_colour::open(r, x, y);
    };
    return h;
}
/* A spot's or rect's range: a dot on its axis at its radius. */
Handle range_dot(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    return line_dot(v, r, "range", "radius", "Range", vec3(l.position), f, l.radius,
        [](const Drag& d, V3& p, V3& u) {
            V3 rt, up;
            frame_of(d.light, u, rt, up);
            p = vec3(d.light.position);
        },
        [](const Drag& d) { return d.light.radius; }, 16);
}
/* The side of a spot's cone facing the camera: across its axis and the view. */
V3 cone_side(const pc_overlay_view& v, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float vr[3], vu[3], vf[3];
    rood_math::view_axes(v.view, v.projection, v.rect, vr, vu, vf);
    V3 s = { f[1] * vf[2] - f[2] * vf[1], f[2] * vf[0] - f[0] * vf[2], f[0] * vf[1] - f[1] * vf[0] };
    float n = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (n < 1e-3f) /* looking down its axis */
        return rt;
    return { s[0] / n, s[1] / n, s[2] / n };
}
float cone_angle(const Light& l, bool inner) { return inner ? (l.inner > 0 ? l.inner : rood_math::auto_inner(l.cone)) : l.cone; }
/* A cone handle: `share` of the radius along the axis, out to the cone's edge
 * (outer) or the inner cone's (inner) on the side facing the camera; dragged
 * across, its angle = 2 atan(offset / height) (Ctrl a tenth; Shift 5 degrees). */
Handle cone_handle(const pc_overlay_view& v, const Ref& r, const Light& l, bool inner)
{
    const float share = inner ? 0.3f : 0.5f;
    const char* field = inner ? "inner" : "cone";
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float height = l.radius * share;
    V3 base = along(vec3(l.position), f, height), side = cone_side(v, l);
    Handle h;
    h.name = field, h.label = inner ? "Inner cone" : "Cone";
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(base, side, height * tanf(cone_angle(l, inner) * 0.5f * DEG)), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = inner ? IM_COL32(255, 170, 90, 255) : IM_COL32(120, 220, 255, 255);
    h.drag = [r, inner, share, field](const Drag& d) {
        V3 f, rt, up;
        frame_of(d.light, f, rt, up);
        float height = d.light.radius * share;
        V3 base = along(vec3(d.light.position), f, height), side = cone_side(d.view, d.light);
        float s0 = height * tanf(cone_angle(d.light, inner) * 0.5f * DEG);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, base.data(), side.data());
        float t1 = rood_math::line_param(d.origin, d.dir, base.data(), side.data());
        float s = rood_math::linear_drag(s0, t0, t1, d.fine);
        float angle = 2 * atanf(fmaxf(s, 0.0f) / height) / DEG;
        rood_handles::set(r, field, Value::scalar(d.snap ? rood_math::snap_step(angle, 5) : angle));
    };
    h.readout = [r, field]() { return rood_handles::readout(r, field); };
    pc_overlay_view view = v;
    h.where = [view, base, side, height](const Value& a, float out[2]) {
        return pixel(view, along(base, side, height * tanf(a.x() * 0.5f * DEG)), out);
    };
    return h;
}
/* "Aim at surface": a crosshair left of the ring; a click arms it, the next
 * click on a surface points the spot there (editor_handles.cpp viewport_click). */
Handle aim_handle(const Ref& r, float cx, float cy, float ring_radius)
{
    Handle h;
    h.name = "aim", h.label = "Aim at surface";
    h.shape = rood_handles::SHAPE_CROSS;
    h.x = cx - ring_radius - 14 * em(), h.y = cy, h.radius = 6 * em();
    h.colour = IM_COL32(255, 255, 255, 230);
    h.click = [r]() { rood_handles::arm_aim(r); };
    return h;
}
/* A rect's edge: its width (axis 0, along its right) or height (axis 1, up)
 * on the + or - side. A plain drag moves that edge only (the opposite edge
 * stays: the width and the centre change); Alt keeps the centre (both edges
 * move). Shift: multiples of 8 units. */
Handle edge_handle(const pc_overlay_view& v, const Ref& r, const Light& l, int axis, int sign)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    V3 u = axis == 0 ? rt : up, p = vec3(l.position);
    Handle h;
    h.name = std::string(axis == 0 ? "width" : "height") + (sign > 0 ? "+" : "-");
    h.label = axis == 0 ? "Width" : "Height";
    h.shape = rood_handles::SHAPE_SQUARE;
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(p, u, sign * l.size[axis] * 0.5f), px);
    h.x = px[0], h.y = px[1], h.radius = 4 * em();
    h.colour = IM_COL32(120, 220, 255, 255);
    const char* field = axis == 0 ? "width" : "height";
    h.drag = [r, axis, sign, field](const Drag& d) {
        V3 f, rt, up;
        frame_of(d.light, f, rt, up);
        V3 u = axis == 0 ? rt : up, p = vec3(d.light.position);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, p.data(), u.data());
        float t1 = rood_math::line_param(d.origin, d.dir, p.data(), u.data());
        float moved = (t1 - t0) * (d.fine ? 0.1f : 1.0f) * sign; /* the edge's move, outward */
        float w0 = d.light.size[axis], w = d.symmetric ? w0 + 2 * moved : w0 + moved;
        if (d.snap)
            w = rood_math::snap_step(w, 8);
        w = fminf(fmaxf(w, rood_transform::RECT_SIZE_MIN), rood_transform::RECT_SIZE_MAX);
        rood_handles::set(r, field, Value::scalar(w));
        if (!d.symmetric) { /* the opposite edge stays: the centre moves half the change toward this edge */
            V3 c = along(p, u, sign * (w - w0) * 0.5f);
            rood_handles::set(r, "position", Value::vec(c[0], c[1], c[2]));
        }
    };
    h.readout = [r, field]() { return rood_handles::readout(r, field); };
    pc_overlay_view view = v;
    /* where: the symmetric drag's pixel for a width (a plain drag to where(2w - w0) gives w) */
    h.where = [view, p, u, sign](const Value& value, float out[2]) { return pixel(view, along(p, u, sign * value.x() * 0.5f), out); };
    return h;
}
/* Barn doors: the flap on an edge (outward `out`) leans from the rect's
 * facing by the doors' angle. The handles sit at the right and top flaps'
 * tips, a quarter of the rect's smaller side past the doors' length (clear of
 * the edge handles when the doors are short). */
V3 flap_dir(const V3& out, const V3& f, float angle)
{
    float t = fminf(fmaxf(angle, 0.0f), 90.0f) * DEG;
    return { out[0] * sinf(t) + f[0] * cosf(t), out[1] * sinf(t) + f[1] * cosf(t), out[2] * sinf(t) + f[2] * cosf(t) };
}
float barn_clear(const Light& l) { return 0.25f * fminf(l.size[0], l.size[1]); }
/* The angle: the right flap's tip turns about its hinge, in the plane of the
 * rect's right and facing (Ctrl a tenth; Shift 5 degrees). */
Handle barn_angle_handle(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    V3 hinge = along(vec3(l.position), rt, l.size[0] * 0.5f);
    float reach = l.barn_length + barn_clear(l);
    Handle h;
    h.name = "barn_angle", h.label = "Barn door angle";
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(hinge, flap_dir(rt, f, l.barn_angle), reach), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = IM_COL32(200, 200, 215, 255);
    h.drag = [r](const Drag& d) {
        V3 f, rt, up;
        frame_of(d.light, f, rt, up);
        V3 hinge = along(vec3(d.light.position), rt, d.light.size[0] * 0.5f);
        float a[3], b[3];
        if (!rood_math::plane_hit(d.press_origin, d.press_dir, hinge.data(), up.data(), a)
            || !rood_math::plane_hit(d.origin, d.dir, hinge.data(), up.data(), b))
            return;
        auto angle_of = [&](const float* q) {
            float x = (q[0] - hinge[0]) * rt[0] + (q[1] - hinge[1]) * rt[1] + (q[2] - hinge[2]) * rt[2];
            float z = (q[0] - hinge[0]) * f[0] + (q[1] - hinge[1]) * f[1] + (q[2] - hinge[2]) * f[2];
            return atan2f(x, z) / DEG;
        };
        float t = d.light.barn_angle + (angle_of(b) - angle_of(a)) * (d.fine ? 0.1f : 1.0f);
        rood_handles::set(r, "barn_angle", Value::scalar(d.snap ? rood_math::snap_step(t, 5) : t));
    };
    h.readout = [r]() { return rood_handles::readout(r, "barn_angle"); };
    pc_overlay_view view = v;
    h.where = [view, hinge, rt, f, reach](const Value& value, float out[2]) {
        return pixel(view, along(hinge, flap_dir(rt, f, value.x()), reach), out);
    };
    return h;
}
/* The length: the top flap's tip, along the flap (Shift 4 units). */
Handle barn_length_handle(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    V3 dir = flap_dir(up, f, l.barn_angle);
    V3 base = along(along(vec3(l.position), up, l.size[1] * 0.5f), dir, barn_clear(l));
    Handle h = line_dot(v, r, "barn_length", "barn_length", "Barn door length", base, dir, l.barn_length,
        [](const Drag& d, V3& p, V3& u) {
            V3 f, rt, up;
            frame_of(d.light, f, rt, up);
            u = flap_dir(up, f, d.light.barn_angle);
            p = along(along(vec3(d.light.position), up, d.light.size[1] * 0.5f), u, barn_clear(d.light));
        },
        [](const Drag& d) { return d.light.barn_length; }, 4);
    h.colour = IM_COL32(200, 200, 215, 255);
    return h;
}
} // namespace

void rood_handles::light_handles(const pc_overlay_view& v, const Ref& r, int index, std::vector<Handle>& out)
{
    const Light& l = lights[index];
    float c[2];
    if (!pixel(v, vec3(l.position), c))
        return;
    Handle intensity = ring(c[0], c[1], colour_of(l.color), "Intensity", [](const Drag& d) { return d.light.intensity; },
        [r](float x) { rood_handles::set(r, "intensity", Value::scalar(x)); }, [r]() { return rood_handles::get(r, "intensity"); },
        [r]() { return rood_handles::readout(r, "intensity"); });
    out.push_back(intensity);
    if (l.type == 0) {
        out.push_back(radius_dot(v, r, l));
        out.push_back(shadows_toggle(r, c[0], c[1], intensity.radius));
    }
    if (l.type == 1) {
        out.push_back(range_dot(v, r, l));
        out.push_back(cone_handle(v, r, l, false));
        out.push_back(cone_handle(v, r, l, true));
        out.push_back(aim_handle(r, c[0], c[1], intensity.radius));
    }
    if (l.type == 2) {
        for (int axis = 0; axis < 2; axis++)
            for (int sign = 1; sign >= -1; sign -= 2)
                out.push_back(edge_handle(v, r, l, axis, sign));
        out.push_back(range_dot(v, r, l));
        out.push_back(barn_length_handle(v, r, l));
        out.push_back(barn_angle_handle(v, r, l));
    }
    out.push_back(swatch(r, c[0], c[1], intensity.radius, l.color));
}
/* A selected light's shapes besides its handles: a spot's inner cone (its
 * edges inside the cone's), a rect's four barn door flaps. */
void rood_handles::light_extras(const pc_overlay_view& v, int index, void* draw_list)
{
    if (index < 0 || index >= (int)lights.size())
        return;
    const Light& l = lights[index];
    ImDrawList* draw = (ImDrawList*)draw_list;
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float apex[2];
    if (l.type == 1 && pixel(v, vec3(l.position), apex)) {
        float spread = tanf(fminf(cone_angle(l, true), l.cone) * 0.5f * DEG) * l.radius;
        V3 tip = along(vec3(l.position), f, l.radius);
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5707963f, px[2];
            if (pixel(v, along(along(tip, rt, cosf(a) * spread), up, sinf(a) * spread), px))
                draw->AddLine(ImVec2(apex[0], apex[1]), ImVec2(px[0], px[1]), IM_COL32(255, 170, 90, 150), 1.0f);
        }
    }
    if (l.type != 2 || l.barn_length <= 0)
        return;
    for (int e = 0; e < 4; e++) { /* +right, -right, +up, -up */
        const V3& axis = e < 2 ? rt : up;
        const V3& across = e < 2 ? up : rt;
        float sign = e % 2 ? -1.0f : 1.0f, half = l.size[e < 2 ? 0 : 1] * 0.5f, span = l.size[e < 2 ? 1 : 0] * 0.5f;
        V3 outward = { axis[0] * sign, axis[1] * sign, axis[2] * sign };
        V3 hinge = along(vec3(l.position), outward, half), flap = flap_dir(outward, f, l.barn_angle);
        V3 corner[4] = { along(hinge, across, -span), along(hinge, across, span), {}, {} };
        corner[2] = along(corner[1], flap, l.barn_length);
        corner[3] = along(corner[0], flap, l.barn_length);
        ImVec2 q[4];
        bool seen = true;
        for (int k = 0; k < 4; k++) {
            float px[2];
            seen &= pixel(v, corner[k], px);
            q[k] = ImVec2(px[0], px[1]);
        }
        if (seen) {
            draw->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(60, 60, 70, 90));
            draw->AddPolyline(q, 4, IM_COL32(200, 200, 210, 220), ImDrawFlags_Closed, 1.5f);
        }
    }
}
/* The sun on its sky dome (rood_math::dome_centre: ahead of the camera): its
 * disc where it shines from, a dome outline (horizon and its meridian) while
 * selected (rood_math::dome_radius: it fits the view). Registered as an icon: a click selects it (env:sun). */
void rood_handles::sun_icon(const pc_overlay_view& v, void* draw_list)
{
    rood_env::SunState s = rood_env::sun_state();
    Ref r { rood_selection::KIND_SUN, "env:sun" };
    float c[3];
    if (!s.defined || !s.enabled || !rood_env::sun_icon_shown() || rood_selection::hidden(r)
        || !rood_math::dome_centre(v.view, v.projection, v.rect, c))
        return;
    const float radius = rood_math::dome_radius(v.view, v.projection, v.rect);
    ImDrawList* draw = (ImDrawList*)draw_list;
    bool chosen = rood_selection::contains(r);
    if (chosen) {
        ImVec2 horizon[49], meridian[25];
        int n = 0, m = 0;
        for (int k = 0; k <= 48; k++) {
            float a = k * 6.2831853f / 48, px[2];
            V3 d = { cosf(a), 0, sinf(a) };
            if (pixel(v, along(vec3(c), d, radius), px))
                horizon[n++] = ImVec2(px[0], px[1]);
        }
        for (int k = 0; k <= 24; k++) {
            float d[3], px[2];
            rood_math::sun_direction(s.azimuth, k * 90.0f / 24, d);
            if (pixel(v, along(vec3(c), vec3(d), radius), px))
                meridian[m++] = ImVec2(px[0], px[1]);
        }
        if (n > 1)
            draw->AddPolyline(horizon, n, IM_COL32(255, 220, 120, 120), 0, 1.5f);
        if (m > 1)
            draw->AddPolyline(meridian, m, IM_COL32(255, 220, 120, 90), 0, 1.0f);
    }
    float d[3], px[2];
    rood_math::sun_direction(s.azimuth, s.elevation, d);
    if (!pixel(v, along(vec3(c), vec3(d), radius), px))
        return;
    if (px[0] < v.rect[0] || px[1] < v.rect[1] || px[0] >= v.rect[0] + v.rect[2] || px[1] >= v.rect[1] + v.rect[3])
        return; /* off the view (over the panels) */
    ImVec2 b(px[0], px[1]);
    ImU32 col = colour_of(s.color);
    float rr = 9 * em();
    draw->AddCircleFilled(b, rr, col, 24);
    draw->AddCircle(b, rr, chosen ? IM_COL32(255, 255, 0, 255) : IM_COL32(30, 30, 30, 200), 24, 2.0f);
    for (int i = 0; i < 8; i++) { /* rays */
        float t = i * 0.785398f;
        draw->AddLine(ImVec2(b.x + cosf(t) * rr * 1.3f, b.y + sinf(t) * rr * 1.3f),
            ImVec2(b.x + cosf(t) * rr * 1.8f, b.y + sinf(t) * rr * 1.8f), col, 1.5f);
    }
    char label[48];
    snprintf(label, sizeof label, "sun %.0f / %.0f deg", s.azimuth, s.elevation);
    draw->AddText(ImVec2(b.x + rr * 2, b.y - rr), IM_COL32(255, 240, 200, 230), label);
    rood_scene::icon(r, b.x, b.y, rr + 4, true);
}
/* The selected sun: its disc (dragged across the dome: azimuth and elevation
 * together; Ctrl a tenth; Shift 15 / 5 degrees) and its intensity ring (0..4). */
void rood_handles::sun_handles(const pc_overlay_view& v, std::vector<Handle>& out)
{
    rood_env::SunState s = rood_env::sun_state();
    float c[3], d[3], px[2];
    if (!rood_math::dome_centre(v.view, v.projection, v.rect, c))
        return;
    const float radius = rood_math::dome_radius(v.view, v.projection, v.rect);
    rood_math::sun_direction(s.azimuth, s.elevation, d);
    if (!pixel(v, along(vec3(c), vec3(d), radius), px))
        return;
    out.push_back(ring(px[0], px[1], colour_of(s.color), "Sun intensity", [](const Drag& g) { return g.sun.intensity; },
        [](float x) {
            rood_env::SunState t = rood_env::sun_state();
            t.intensity = fminf(x, 4.0f);
            rood_env::set_sun_state(t);
        },
        []() { return rood_env::sun_state().intensity; },
        []() {
            char text[32];
            snprintf(text, sizeof text, "Intensity %.2f", rood_env::sun_state().intensity);
            return std::string(text);
        }));
    out.push_back(swatch(Ref { rood_selection::KIND_SUN, "env:sun" }, px[0], px[1], 22 * em(), s.color));
    Handle h;
    h.name = "sun", h.label = "Sun direction";
    h.shape = rood_handles::SHAPE_DISC;
    h.x = px[0], h.y = px[1], h.radius = 9 * em();
    h.colour = colour_of(s.color);
    h.drag = [](const Drag& g) {
        float centre[3], n0[3], f0[3], n1[3], f1[3], was[3], prev[3], a0, e0, a1, e1;
        if (!rood_math::dome_centre(g.view.view, g.view.projection, g.view.rect, centre))
            return;
        const float radius = rood_math::dome_radius(g.view.view, g.view.projection, g.view.rect);
        rood_env::SunState now = rood_env::sun_state();
        rood_math::sun_direction(g.sun.azimuth, g.sun.elevation, was);
        rood_math::sun_direction(now.azimuth, now.elevation, prev);
        rood_math::sphere_hits(g.press_origin, g.press_dir, centre, radius, n0, f0);
        rood_math::sphere_hits(g.origin, g.dir, centre, radius, n1, f1);
        /* A ray meets the dome twice: the point nearer the sun as it was (the
         * press) and as it is (now), so the disc follows the pointer smoothly. */
        auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        rood_math::sun_angles(dot(n0, was) >= dot(f0, was) ? n0 : f0, &a0, &e0);
        rood_math::sun_angles(dot(n1, prev) >= dot(f1, prev) ? n1 : f1, &a1, &e1);
        float k = g.fine ? 0.1f : 1.0f, da = a1 - a0;
        da = da > 180 ? da - 360 : da < -180 ? da + 360 : da;
        now.azimuth = g.sun.azimuth + da * k;
        now.elevation = g.sun.elevation + (e1 - e0) * k;
        if (g.snap)
            now.azimuth = rood_math::snap_step(now.azimuth, 15), now.elevation = rood_math::snap_step(now.elevation, 5);
        now.azimuth = fmodf(fmodf(now.azimuth, 360.0f) + 360.0f, 360.0f);
        now.elevation = fminf(90.0f, fmaxf(0.0f, now.elevation));
        rood_env::set_sun_state(now);
    };
    h.readout = []() {
        rood_env::SunState t = rood_env::sun_state();
        char text[48];
        snprintf(text, sizeof text, "Sun %.0f / %.0f deg", t.azimuth, t.elevation);
        return std::string(text);
    };
    pc_overlay_view view = v;
    h.where = [view](const Value& value, float px_out[2]) {
        float centre[3], dir[3];
        if (!rood_math::dome_centre(view.view, view.projection, view.rect, centre))
            return false;
        const float radius = rood_math::dome_radius(view.view, view.projection, view.rect);
        rood_math::sun_direction(value.at(0), value.at(1), dir);
        return pixel(view, along(vec3(centre), vec3(dir), radius), px_out);
    };
    out.push_back(h);
}
