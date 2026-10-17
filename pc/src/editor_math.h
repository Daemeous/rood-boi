/* Plain maths of the editor's viewport tools
 * (pc/docs/plans/2026-10-15-artist-tools-design.md, "Handle layer"):
 * projecting world points, the view's axes and eye, a light's frame, where a
 * mouse ray meets a handle's line, plane or sphere, how a drag turns into a
 * value (linear or logarithmic, Ctrl fine, Shift snapped), the sun's angles
 * and its sky dome, a spot's automatic inner cone, and colour (Kelvin and
 * tint, hex, display to linear). No Dear ImGui (pc/tests/editor_tools_probe.cpp). */
#ifndef ROOD_EDITOR_MATH_H
#define ROOD_EDITOR_MATH_H
#include <string>

namespace rood_math {
/* ---- the view: column-major view and projection (clip w > 0 in front) and
 * its window rectangle x, y, w, h (pc_overlay_view) ---- */
bool project(const float view[16], const float projection[16], const float rect[4], const float world[3], float out[2]);
/* The view's right, up (on screen) and forward axes, unit length, from rays
 * through its centre (whatever the matrices' conventions). */
bool view_axes(const float view[16], const float projection[16], const float rect[4], float right[3], float up[3],
    float forward[3]);
/* The eye (R e + t = 0; R may carry a scale). False when R does not invert. */
bool view_eye(const float view[16], float eye[3]);
/* An aimed light's frame (lights.cpp): forward = its direction, right and up
 * span a rect (world up is -y). */
void light_frame(const float direction[3], float forward[3], float right[3], float up[3]);

/* ---- where a ray (origin o, unit direction d) meets a handle ---- */
/* The parameter along the line p + s u (u unit) of its point nearest the ray. */
float line_param(const float o[3], const float d[3], const float p[3], const float u[3]);
/* Where the ray meets the plane through p with normal n; false when parallel or behind. */
bool plane_hit(const float o[3], const float d[3], const float p[3], const float n[3], float out[3]);
/* The unit directions from c to where the ray meets the sphere (c, r): its
 * nearer and farther points (2), or both toward the sphere's point nearest
 * the ray when it misses (0). */
int sphere_hits(const float o[3], const float d[3], const float c[3], float r, float near_dir[3], float far_dir[3]);

/* ---- drags ---- */
/* A logarithmic drag (an intensity ring): the value (at least 0.01) doubles
 * every 100 pixels to the right (Ctrl: every 1000). */
float log_drag(float start, float pixels, bool fine);
/* A linear drag: start plus the parameter's change (Ctrl: a tenth of it). */
float linear_drag(float start, float t_press, float t_now, bool fine);
float snap_step(float v, float step); /* the nearest multiple (Shift) */
float snap_125(float v); /* the nearest of 1, 2, 5 x 10^k (Shift on a logarithmic value; 0 stays 0) */

/* ---- the sun (environment.cpp: azimuth from +z toward +x, elevation above
 * the horizon, y down) ---- */
void sun_direction(float azimuth, float elevation, float out[3]);
void sun_angles(const float dir[3], float* azimuth, float* elevation); /* azimuth 0..360 */
/* The sky dome the sun's disc is dragged on: 900 units ahead of the eye,
 * its radius 35% of the view's height there (so the sun at any elevation
 * stays in the view), at most half its distance. */
constexpr float DOME_AHEAD = 900;
bool dome_centre(const float view[16], const float projection[16], const float rect[4], float out[3]);
float dome_radius(const float view[16], const float projection[16], const float rect[4]);

/* ---- spots ---- */
/* The inner cone (full angle, degrees) a spot with inner 0 shows: the outer
 * quarter of its cosine range fades (light_shape.h pc_spot_cos_inner). */
float auto_inner(float cone);
/* ---- colour (the colour popover, editor_colour.cpp) ---- */
/* A blackbody colour (prefab::kelvin, 1000..12000 K) with a tint (-1 magenta
 * .. +1 green: green x (1 + 0.25 tint)), brightest channel 1. */
void kelvin_tint(float kelvin, float tint, float out[3]);
/* The nearest Kelvin (on the strip's grid) and tint for a colour. */
void estimate_kelvin_tint(const float rgb[3], float* kelvin, float* tint);
/* The Kelvin strip: 0 at 1000 K, 1 at 12000 K, even in mired (1e6 / K). */
float kelvin_strip_u(float kelvin);
float kelvin_strip_k(float u);
bool parse_hex(const std::string& text, float out[3]); /* "#4080ff", "4080FF" */
std::string format_hex(const float rgb[3]); /* "#4080FF" (0..1 clamped) */
float srgb_to_linear(float c); /* a display value (sRGB) to linear light */
void normalize_colour(float rgb[3]); /* brightest channel 1 (black stays black) */
/* ---- placing (the library's drops, "fit rect light to window") ---- */
/* A fog volume's footprint (x, z) dropped at `at` wanting full sizes `want`,
 * kept inside a floor's area lo..hi: each size at most the area's, the
 * centre moved in so it fits. An empty area (hi <= lo) keeps both as given. */
void fit_footprint(const float at[2], const float want[2], const float lo[2], const float hi[2], float centre[2],
    float size[2]);
/* A room triangle for fit_pane: its corners and its texture (page << 16 | CLUT). */
struct PaneTriangle {
    float v[3][3];
    unsigned texture;
};
/* A flat pane: its centre, the normal facing the eye, the axes of a rect
 * light facing out of it (light_frame of the normal: right, up), its extent
 * along them and how many triangles make it. */
struct Pane {
    float centre[3] = { 0, 0, 0 }, normal[3] = { 0, 0, 1 }, right[3] = { 1, 0, 0 }, up[3] = { 0, -1, 0 };
    float width = 0, height = 0;
    int triangles = 0;
};
/* The pane around triangle `seed`: the triangles reached from it through
 * shared edges (two corners within 0.5 units) that share its texture and lie
 * in its plane (normals within 8 degrees, corners within 2 units), at most
 * 256. False when `seed` is no triangle (out of range, degenerate). */
bool fit_pane(const PaneTriangle* triangles, int count, int seed, const float eye[3], Pane& out);
} // namespace rood_math
#endif
