// The viewport's handles (editor_handles.h): drawing, hover, presses, drags
// with the shared modifiers, the readout, Escape, one undo step per drag,
// spot aiming, and the RPC editor_handles.
#include "editor_handles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_math.h"
#include "editor_colour.h"
#include "editor_fit_window.h"
#include "editor_picking.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_undo.h"

extern "C" int pc_editor_flying(void); /* editor_view.cpp */
extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */

using prefab::Value;
using rood_handles::Drag;
using rood_handles::Handle;
using rood_selection::Ref;

namespace {
std::vector<Handle> shown; /* the primary's, as last drawn */
Ref subject;
const char* hidden_reason = "nothing"; /* why none show: nothing, unsupported, hidden, flying */
int shown_frame = -100;
int hovered = -1;
struct Active {
    bool on = false, moved = false;
    std::string name;
    Ref subject;
    Drag drag;
} active;
Ref aim_target; /* a spot waiting for the click that aims it */
pc_overlay_pick aim_pick;

int frame_now() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1; }
const rood_schema::Field* field_of(const Ref& r, const std::string& name, int* index)
{
    const rood_schema::Kind* k = rood_schema::kind_of(r, index);
    return k ? k->field(name) : nullptr;
}
const char* shape_name(rood_handles::Shape s)
{
    static const char* const names[] = { "dot", "ring", "square", "toggle", "cross", "disc" };
    return names[s];
}
const Handle* find(const std::string& name)
{
    for (const Handle& h : shown)
        if (h.name == name)
            return &h;
    return nullptr;
}
bool over(const Handle& h, float x, float y)
{
    bool ring = h.shape == rood_handles::SHAPE_RING;
    float dx = x - (ring ? h.cx : h.x), dy = y - (ring ? h.cy : h.y), d = sqrtf(dx * dx + dy * dy);
    return ring ? fabsf(d - h.radius) < 5 : d < h.radius + 3;
}
/* The primary's handles in this view, or why it has none. */
const char* build(const pc_overlay_view& v, std::vector<Handle>& out, Ref* who)
{
    out.clear();
    Ref p = rood_selection::primary();
    *who = p;
    if (!p)
        return "nothing";
    if (rood_selection::hidden(p))
        return "hidden";
    if (p.kind == rood_selection::KIND_LIGHT) {
        int i = rood_scene::index_of(p);
        if (i < 0)
            return "nothing";
        rood_handles::light_handles(v, p, i, out);
        return nullptr;
    }
    if (p.kind == rood_selection::KIND_VOLUME) {
        int i = rood_scene::index_of(p);
        if (i < 0)
            return "nothing";
        rood_handles::volume_handles(v, p, i, out);
        return nullptr;
    }
    if (p.kind == rood_selection::KIND_SUN && rood_env::sun_state().defined) {
        rood_handles::sun_handles(v, out);
        return nullptr;
    }
    return "unsupported";
}
void restore(const Active& a)
{
    int i = rood_scene::index_of(a.subject);
    if (a.subject.kind == rood_selection::KIND_LIGHT && i >= 0)
        rood_lights::lights[i] = a.drag.light;
    else if (a.subject.kind == rood_selection::KIND_SUN)
        rood_env::set_sun_state(a.drag.sun);
    else if (a.subject.kind == rood_selection::KIND_VOLUME && i >= 0) {
        for (const auto& f : a.drag.fields) {
            int index;
            /* only what changed: a disturb weight's get gives the global weight, set would make it its own */
            const rood_schema::Field* field = field_of(a.subject, f.first, &index);
            if (field && !rood_schema::same(*field, field->get(index), f.second))
                field->set(index, f.second);
        }
        rood_lights::dirty = true;
    }
}
void end(bool keep)
{
    if (!active.on)
        return;
    if (!keep)
        restore(active);
    active.on = false;
    pc_undo_end();
}
void draw_handle(ImDrawList* d, const Handle& h, bool hot)
{
    ImU32 lit = IM_COL32(255, 255, 0, 255), edge = hot ? lit : IM_COL32(0, 0, 0, 220);
    ImVec2 p(h.x, h.y);
    switch (h.shape) {
    case rood_handles::SHAPE_RING:
        d->AddCircle(ImVec2(h.cx, h.cy), h.radius, IM_COL32(0, 0, 0, 160), 48, 4.0f);
        d->AddCircle(ImVec2(h.cx, h.cy), h.radius, hot ? lit : h.colour, 48, 2.0f);
        d->AddCircleFilled(p, 3.5f, hot ? lit : h.colour);
        break;
    case rood_handles::SHAPE_SQUARE:
        d->AddRectFilled(ImVec2(p.x - h.radius, p.y - h.radius), ImVec2(p.x + h.radius, p.y + h.radius), h.colour, 2);
        d->AddRect(ImVec2(p.x - h.radius, p.y - h.radius), ImVec2(p.x + h.radius, p.y + h.radius), edge, 2, 0, 1.5f);
        break;
    case rood_handles::SHAPE_TOGGLE:
        d->AddCircleFilled(p, h.radius, h.colour, 16);
        d->AddCircle(p, h.radius, edge, 16, 1.5f);
        break;
    case rood_handles::SHAPE_CROSS:
        d->AddCircle(p, h.radius, hot ? lit : h.colour, 16, 1.5f);
        d->AddLine(ImVec2(p.x - h.radius - 3, p.y), ImVec2(p.x + h.radius + 3, p.y), hot ? lit : h.colour, 1.5f);
        d->AddLine(ImVec2(p.x, p.y - h.radius - 3), ImVec2(p.x, p.y + h.radius + 3), hot ? lit : h.colour, 1.5f);
        break;
    case rood_handles::SHAPE_DISC:
        d->AddCircleFilled(p, h.radius, h.colour, 24);
        d->AddCircle(p, h.radius, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(30, 30, 30, 200), 24, 2.0f);
        break;
    default:
        d->AddCircleFilled(p, h.radius, h.colour, 16);
        d->AddCircle(p, h.radius, edge, 16, 1.5f);
    }
}
} // namespace

bool rood_handles::frame(const pc_overlay_view& v, pc_overlay_pick pick, void* draw_list)
{
    ImDrawList* draw = (ImDrawList*)draw_list;
    ImGuiIO& io = ImGui::GetIO();
    shown_frame = frame_now();
    aim_pick = pick;
    hovered = -1;
    sun_icon(v, draw); /* the sun on its dome, selected or not */
    if (pc_editor_flying()) { /* the camera flies: no handles; a drag in progress is put back */
        end(false);
        shown.clear();
        hidden_reason = "flying";
        return false;
    }
    hidden_reason = build(v, shown, &subject);
    if (hidden_reason) {
        end(false);
        return false;
    }
    if (subject.kind == rood_selection::KIND_LIGHT)
        light_extras(v, rood_scene::index_of(subject), draw);
    if (subject.kind == rood_selection::KIND_VOLUME)
        volume_extras(v, rood_scene::index_of(subject), draw);
    float mx = io.MousePos.x, my = io.MousePos.y;
    if (active.on) {
        const Handle* h = find(active.name);
        if (!h || active.subject != subject) { /* the selection changed under the drag */
            end(false);
            return false;
        }
        Drag& d = active.drag;
        d.now[0] = mx, d.now[1] = my;
        rood_pick::ray(d.view.view, d.view.projection, d.view.rect, mx, my, d.origin, d.dir);
        d.fine = io.KeyCtrl, d.snap = io.KeyShift, d.symmetric = io.KeyAlt;
        if (fabsf(mx - d.press[0]) + fabsf(my - d.press[1]) >= 2)
            active.moved = true;
        if (active.moved && h->drag)
            h->drag(d);
        for (const Handle& k : shown)
            draw_handle(draw, k, k.name == active.name);
        if (active.moved && h->readout) { /* the value by the pointer */
            std::string text = h->readout();
            ImDrawList* front = ImGui::GetForegroundDrawList();
            ImVec2 size = ImGui::CalcTextSize(text.c_str()), at(mx + 16, my - size.y - 6);
            front->AddRectFilled(ImVec2(at.x - 4, at.y - 2), ImVec2(at.x + size.x + 4, at.y + size.y + 2),
                IM_COL32(20, 20, 24, 220), 3);
            front->AddText(at, IM_COL32(255, 230, 120, 255), text.c_str());
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            std::function<void()> click = active.moved ? std::function<void()>() : h->click;
            end(true);
            if (click)
                click();
        }
        return true;
    }
    /* Hover: not while the gizmo drags or a panel is under the pointer. */
    if (!ImGuizmo::IsUsing() && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        for (int i = (int)shown.size() - 1; i >= 0 && hovered < 0; i--)
            if (over(shown[i], mx, my))
                hovered = i;
    for (int i = 0; i < (int)shown.size(); i++)
        draw_handle(draw, shown[i], i == hovered);
    if (hovered < 0)
        return false;
    const Handle& h = shown[hovered];
    ImGui::SetTooltip("%s", h.label.c_str());
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        active = Active();
        active.on = true;
        active.name = h.name;
        active.subject = subject;
        Drag& d = active.drag;
        d.press[0] = d.now[0] = mx, d.press[1] = d.now[1] = my;
        d.view = v;
        rood_pick::ray(v.view, v.projection, v.rect, mx, my, d.press_origin, d.press_dir);
        memcpy(d.origin, d.press_origin, sizeof d.origin);
        memcpy(d.dir, d.press_dir, sizeof d.dir);
        int i = rood_scene::index_of(subject);
        if (subject.kind == rood_selection::KIND_LIGHT && i >= 0)
            d.light = rood_lights::lights[i];
        d.sun = rood_env::sun_state();
        d.fields.clear();
        int vi;
        const rood_schema::Kind* vk = subject.kind == rood_selection::KIND_VOLUME ? rood_schema::kind_of(subject, &vi) : nullptr;
        for (size_t f = 0; vk && f < vk->fields.size(); f++)
            d.fields.push_back({ vk->fields[f].name, vk->fields[f].get(vi) });
        pc_undo_begin(("Edit " + h.label).c_str()); /* the whole drag is one undo step */
    }
    return true;
}
bool rood_handles::dragging() { return active.on; }
prefab::Value rood_handles::Drag::field(const char* name) const
{
    for (const auto& f : fields)
        if (f.first == name)
            return f.second;
    return prefab::Value();
}
const char* rood_handles::hot()
{
    if (active.on)
        return active.name.c_str();
    return hovered >= 0 && hovered < (int)shown.size() && shown_frame >= frame_now() - 1 ? shown[hovered].name.c_str()
                                                                                           : nullptr;
}
bool rood_handles::handle_pixel(const char* name, float out[2])
{
    const Handle* h = find(name);
    if (!h)
        return false;
    out[0] = h->x, out[1] = h->y;
    return true;
}
bool rood_handles::cancel()
{
    if (active.on) {
        end(false);
        return true;
    }
    if (aim_target) {
        aim_target = Ref();
        return true;
    }
    return false;
}
void rood_handles::finish()
{
    end(true);
    aim_target = Ref();
}
void rood_handles::arm_aim(const Ref& r) { aim_target = r; }
bool rood_handles::viewport_click(float x, float y)
{
    if (!aim_target)
        return false;
    Ref r = aim_target;
    aim_target = Ref();
    int i = rood_scene::index_of(r);
    float w[3], n[3];
    if (i < 0 || !aim_pick || !aim_pick(x, y, w, n))
        return true; /* the click was the aim's; on nothing it does nothing */
    const float* p = rood_lights::lights[i].position;
    float d[3] = { w[0] - p[0], w[1] - p[1], w[2] - p[2] }, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-3f)
        return true;
    set(r, "direction", Value::vec(d[0] / len, d[1] / len, d[2] / len));
    pc_undo_hint("Aim at surface");
    return true;
}

Value rood_handles::get_value(const Ref& r, const char* field)
{
    int i;
    const rood_schema::Kind* k = rood_schema::kind_of(r, &i);
    const rood_schema::Field* f = k ? k->field(field) : nullptr;
    return f ? f->get(i) : Value();
}
float rood_handles::get(const Ref& r, const char* field) { return get_value(r, field).x(); }
void rood_handles::set(const Ref& r, const char* field, const Value& v)
{
    int i;
    const rood_schema::Kind* k = rood_schema::kind_of(r, &i);
    const rood_schema::Field* f = k ? k->field(field) : nullptr;
    if (f)
        rood_schema::apply(*k, *f, r, v);
}
std::string rood_handles::readout(const Ref& r, const char* field)
{
    int i;
    const rood_schema::Kind* k = rood_schema::kind_of(r, &i);
    const rood_schema::Field* f = k ? k->field(field) : nullptr;
    return f ? f->label + " " + rood_schema::format(*f, f->get(i)) : std::string();
}

std::string rood_handles::state_json()
{
    auto q = [](const std::string& s) { return prefab::json_quote(s); };
    bool current = shown_frame >= 0 && shown_frame >= frame_now() - 1;
    const char* why = current ? hidden_reason : "editor";
    std::string s = "{\"subject\":" + (why ? std::string("null") : q(subject.id)) + ",\"hidden\":"
        + (why ? q(why) : std::string("null")) + ",\"handles\":[";
    for (size_t i = 0; !why && i < shown.size(); i++) {
        const Handle& h = shown[i];
        bool ring = h.shape == SHAPE_RING;
        char geo[200];
        snprintf(geo, sizeof geo, ",\"screen\":[%.2f,%.2f],\"centre\":[%.2f,%.2f],\"radius\":%.2f", h.x, h.y,
            ring ? h.cx : h.x, ring ? h.cy : h.y, h.radius);
        s += std::string(i ? "," : "") + "{\"name\":" + q(h.name) + ",\"label\":" + q(h.label) + ",\"shape\":\""
            + shape_name(h.shape) + "\"" + geo + "}";
    }
    const Handle* h = active.on ? find(active.name) : nullptr;
    s += "],\"drag\":{\"active\":" + std::string(active.on ? "true" : "false") + ",\"handle\":"
        + (active.on ? q(active.name) : std::string("null")) + ",\"moved\":" + (active.moved ? "true" : "false")
        + ",\"readout\":" + (h && h->readout ? q(h->readout()) : std::string("null")) + "},\"aim\":"
        + (aim_target ? q(aim_target.id) : std::string("null")) + ",\"fit\":" + rood_fit::state_json()
        + ",\"colour\":" + rood_colour::state_json() + "}";
    return s;
}

/* RPC editor_handles (dev_rpc.c hands over the params object's text): action
 * state (default): the primary's handles as last drawn (name, label, shape,
 * screen: where to press, a ring's centre and radius), why none show
 * (nothing, unsupported, hidden, flying, editor), the drag in progress (its
 * readout) and the spot waiting to be aimed; where {handle, value}: the
 * window pixel to drag the handle to for that value (the sun's: [azimuth,
 * elevation]); colour {open, tab, kelvin, tint, hex, rgb, palette, recent}:
 * the colour popover (editor_colour.h); fit_window {x, y | u, v, id, new,
 * offset, kelvin}: fit a rect light to the pane there now; fit_arm {id,
 * new}: the next click in the view does (editor_fit_window.h). */
extern "C" int pc_editor_handles_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_scene::frame();
    const prefab::Json* a = params.get("action");
    std::string action = a && a->type == prefab::Json::String ? a->string : "state";
    std::string text;
    if (action == "where") {
        const prefab::Json *name = params.get("handle"), *value = params.get("value");
        pc_overlay_view v;
        std::vector<Handle> hs;
        Ref who;
        if (!name || !value || !pc_lights_tools_view(&v, nullptr) || build(v, hs, &who)) {
            snprintf(out, size, "where needs handle and value, an editor view and a light, the sun or a fog volume selected");
            return -1;
        }
        const Handle* h = nullptr;
        for (const Handle& k : hs)
            if (k.name == name->string)
                h = &k;
        if (!h || !h->where) {
            snprintf(out, size, "no handle %s with a position for a value", name->string.c_str());
            return -1;
        }
        Value val = value->type == prefab::Json::Array && value->items.size() >= 2
            ? Value::vec((float)value->items[0].number, (float)value->items[1].number,
                  value->items.size() > 2 ? (float)value->items[2].number : 0.0f)
            : Value::scalar((float)value->number);
        float px[2];
        if (!h->where(val, px)) {
            snprintf(out, size, "that value is off the view");
            return -1;
        }
        char buf[64];
        snprintf(buf, sizeof buf, "{\"screen\":[%.2f,%.2f]}", px[0], px[1]);
        text = buf;
    } else if (action == "colour") { /* the colour popover (editor_colour.h rpc) */
        if (!rood_colour::rpc(params, &error)) {
            snprintf(out, size, "%s", error.c_str());
            return -1;
        }
        text = rood_handles::state_json();
    } else if (action == "fit_window" || action == "fit_arm") {
        /* offset, kelvin: the tool's settings; fit_window {x, y | u, v}: fit there now; fit_arm: the next click.
         * The light: id, else (new: true) a new one, else the selection's primary when it is a rect light. */
        if (const prefab::Json* o = params.get("offset"))
            rood_fit::offset = fminf(fmaxf((float)o->number, 0.0f), 32.0f);
        if (const prefab::Json* k = params.get("kelvin"))
            rood_fit::kelvin = fminf(fmaxf((float)k->number, 1500.0f), 12000.0f);
        Ref light;
        const prefab::Json* fresh = params.get("new");
        if (const prefab::Json* id = params.get("id"))
            light = rood_selection::ref_of(id->string);
        else if (!(fresh && fresh->boolean)) {
            Ref p = rood_selection::primary();
            int i = rood_scene::index_of(p);
            if (p.kind == rood_selection::KIND_LIGHT && i >= 0 && rood_lights::lights[i].type == 2)
                light = p;
        }
        int index = -1;
        std::string message;
        const prefab::Json *x = params.get("x"), *y = params.get("y"), *u = params.get("u"), *w = params.get("v");
        if (action == "fit_arm") {
            rood_fit::arm(light);
        } else if ((x && y) || (u && w)) {
            float px = x ? (float)x->number : 0, py = y ? (float)y->number : 0;
            pc_overlay_view v;
            if (!x && pc_lights_tools_view(&v, nullptr))
                px = v.rect[0] + (float)u->number * v.rect[2], py = v.rect[1] + (float)w->number * v.rect[3];
            index = rood_fit::fit_at(px, py, light, &message);
            if (index < 0) {
                snprintf(out, size, "%s", message.c_str());
                return -1;
            }
        }
        text = "{\"index\":" + std::to_string(index) + ",\"light\":"
            + (index >= 0 ? prefab::json_quote(rood_scene::light_ref(index).id) : std::string("null")) + ",\"message\":"
            + prefab::json_quote(message) + ",\"fit\":" + rood_fit::state_json() + "}";
    } else if (action == "state") {
        text = rood_handles::state_json();
    } else {
        snprintf(out, size, "action: state, where, colour, fit_window or fit_arm");
        return -1;
    }
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
extern "C" int pc_editor_handle_drag(void) { return active.on ? 1 : 0; }
