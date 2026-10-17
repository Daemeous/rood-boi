/* The viewport's handles (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Handle layer" and "Viewport handles per type"): small controls drawn in
 * screen space at world positions on the selection's primary (a light, the
 * sun), each changing one property as it is dragged or clicked. Shared by
 * every handle: Ctrl drags finely, Shift snaps to round values, the value
 * shows by the pointer while dragging, Escape puts it back, a drag is one
 * undo step (pc_undo_begin with its name), and no handle shows while the
 * camera flies or for anything but the primary. ImGuizmo keeps move, rotate
 * and scale. editor_light_handles.cpp builds each kind's handles from its
 * schema fields (editor_schema.h). Editor internals; RPC editor_handles. */
#ifndef ROOD_EDITOR_HANDLES_H
#define ROOD_EDITOR_HANDLES_H
#include <stdint.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "editor_overlays.h"
#include "editor_selection.h"
#include "environment_internal.h"
#include "lights_internal.h"

namespace rood_handles {
enum Shape { SHAPE_DOT, SHAPE_RING, SHAPE_SQUARE, SHAPE_TOGGLE, SHAPE_CROSS, SHAPE_DISC };
/* A drag in progress, as a handle's drag function sees it. */
struct Drag {
    float press[2] = { 0, 0 }, now[2] = { 0, 0 }; /* window pixels */
    float press_origin[3] = { 0, 0, 0 }, press_dir[3] = { 0, 0, 1 }; /* the mouse ray at the press */
    float origin[3] = { 0, 0, 0 }, dir[3] = { 0, 0, 1 }; /* and now */
    bool fine = false, snap = false, symmetric = false; /* Ctrl, Shift, Alt */
    pc_overlay_view view; /* as the drag began */
    rood_lights::Light light; /* the light as the drag began (Escape puts it back) */
    rood_env::SunState sun; /* the sun as the drag began */
    /* A fog volume's schema fields as the drag began (Escape puts them back; its handles read their start here). */
    std::vector<std::pair<std::string, prefab::Value>> fields;
    prefab::Value field(const char* name) const; /* one of them (a zero scalar when absent) */
};
struct Handle {
    std::string name; /* "intensity", "radius", "range", "cone", "inner", "width+", ...: the RPC's and the tests' */
    std::string label; /* by the pointer and in the undo step ("Edit Intensity") */
    Shape shape = SHAPE_DOT;
    float x = 0, y = 0; /* where it is grabbed (window pixels) */
    float cx = 0, cy = 0; /* a ring's centre */
    float radius = 6; /* its hit radius; a ring's radius (grabbed within 5 pixels of it) */
    uint32_t colour = 0xffffffff; /* IM_COL32 */
    std::function<void(const Drag&)> drag; /* null: a click handle */
    std::function<void()> click; /* a press and release without a drag */
    std::function<std::string()> readout; /* "Intensity 2.00" */
    /* The pixel to drag it to for a value (RPC where; tests): a number, or [azimuth, elevation] for the sun. */
    std::function<bool(const prefab::Value& value, float out[2])> where;
};

/* editor_light_handles.cpp: the primary's handles in this view by its kind,
 * and what a selected light draws besides (a spot's inner cone, a rect's
 * barn door flaps). */
void light_handles(const pc_overlay_view& v, const rood_selection::Ref& r, int index, std::vector<Handle>& out);
void light_extras(const pc_overlay_view& v, int index, void* draw_list);
void sun_handles(const pc_overlay_view& v, std::vector<Handle>& out);
/* Every frame the sun shows: its disc on the sky dome, pickable (env:sun). */
void sun_icon(const pc_overlay_view& v, void* draw_list);
/* editor_volume_handles.cpp: a fog volume's handles (the density ring, the
 * soft edge dot, the colour swatch) and its inner outline (the full-density core). */
void volume_handles(const pc_overlay_view& v, const rood_selection::Ref& r, int index, std::vector<Handle>& out);
void volume_extras(const pc_overlay_view& v, int index, void* draw_list);
/* The colour swatch below right of a ring centred at (cx, cy): a click opens (or closes) the colour popover. */
Handle colour_swatch(const rood_selection::Ref& r, float cx, float cy, float ring_radius, const float colour[3]);

/* Each viewport frame (lights.cpp, with the editor's view and surface
 * picker): draws the primary's handles and runs their mouse; true when a
 * handle has the mouse (hovered or dragged): the gizmo and the view's clicks
 * leave it alone. */
bool frame(const pc_overlay_view& v, pc_overlay_pick pick, void* draw_list);
bool dragging();
const char* hot(); /* the handle dragged or under the pointer (its name), or null */
bool handle_pixel(const char* name, float out[2]); /* where a handle was last drawn */
/* Escape: a drag's values back (no undo step), else aiming disarmed; true when it did. */
bool cancel();
void finish(); /* the editor closes: a drag keeps its value, its undo step closes; aiming is disarmed */
/* A spot's "aim at surface" (its handle): the next click on a surface points it there. */
void arm_aim(const rood_selection::Ref& r);
bool viewport_click(float x, float y); /* aiming: this click aims; true when it took it */
/* A schema field of an object (editor_schema.h), read and set as Details does. */
prefab::Value get_value(const rood_selection::Ref& r, const char* field);
float get(const rood_selection::Ref& r, const char* field);
void set(const rood_selection::Ref& r, const char* field, const prefab::Value& v);
std::string readout(const rood_selection::Ref& r, const char* field); /* "Radius 640 u" */
std::string state_json(); /* RPC editor_handles {action "state"} */
} // namespace rood_handles

extern "C" int pc_editor_handles_rpc(const char* params, char* out, int size);
extern "C" int pc_editor_handle_drag(void); /* editor_view.cpp: no Alt orbit while a handle drags */
#endif
