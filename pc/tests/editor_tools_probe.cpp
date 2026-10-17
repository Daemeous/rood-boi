// The editor's viewport tools without the game: projection and the view's
// axes (pc/src/editor_math.cpp), where a mouse ray meets a handle, drags
// (logarithmic, linear, Ctrl, Shift), the sun's angles and dome; later the
// scale table (editor_transform.cpp), the renderers' light shapes
// (light_shape.h) and colour.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "editor_math.h"
#include "editor_transform.h"
#include "light_shape.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool near(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }
static bool near3(const float* a, float x, float y, float z, float e = 1e-3f)
{
    return near(a[0], x, e) && near(a[1], y, e) && near(a[2], z, e);
}

/* A camera at the origin looking along +z, y down (lights.cpp gizmo_matrices'
 * convention: clip w = eye z), 100 x 100 pixels. */
static void test_view(float view[16], float projection[16], float rect[4])
{
    memset(view, 0, 16 * sizeof(float));
    view[0] = view[5] = view[10] = view[15] = 1;
    memset(projection, 0, 16 * sizeof(float));
    float n = 16, f = 65536;
    projection[0] = 1, projection[5] = -1, projection[10] = (f + n) / (f - n), projection[14] = -2 * f * n / (f - n);
    projection[11] = 1;
    rect[0] = rect[1] = 0, rect[2] = rect[3] = 100;
}

static void view_maths()
{
    float view[16], projection[16], rect[4], px[2];
    test_view(view, projection, rect);
    const float ahead[3] = { 0, 0, 100 }, right_of[3] = { 10, 0, 100 }, below[3] = { 0, 10, 100 }, behind[3] = { 0, 0, -100 };
    check(rood_math::project(view, projection, rect, ahead, px) && near(px[0], 50) && near(px[1], 50),
        "a point ahead is the view's centre");
    check(rood_math::project(view, projection, rect, right_of, px) && near(px[0], 55) && near(px[1], 50), "+x is to the right");
    check(rood_math::project(view, projection, rect, below, px) && near(px[1], 55), "+y (down) is down");
    check(!rood_math::project(view, projection, rect, behind, px), "behind the camera: no pixel");
    float r[3], u[3], f[3], eye[3];
    check(rood_math::view_axes(view, projection, rect, r, u, f) && near3(r, 1, 0, 0) && near3(u, 0, -1, 0)
            && near3(f, 0, 0, 1),
        "the view's axes: right +x, up -y, forward +z");
    check(rood_math::view_eye(view, eye) && near3(eye, 0, 0, 0), "the eye");
    view[12] = -5, view[13] = 2, view[14] = 7; /* R e + t = 0: the eye moves to (5, -2, -7) */
    check(rood_math::view_eye(view, eye) && near3(eye, 5, -2, -7), "the eye of a moved camera");
    float c[3];
    check(rood_math::dome_centre(view, projection, rect, c) && near3(c, 5, -2, 893, 0.5f), "the sky dome 900 units ahead");
    check(near(rood_math::dome_radius(view, projection, rect), 450, 0.5f),
        "a wide view's dome: at most half as wide as it is far");
    float narrow[16];
    memcpy(narrow, projection, sizeof narrow);
    narrow[0] = 4, narrow[5] = -4;
    check(near(rood_math::dome_radius(view, narrow, rect), 157.5f, 0.5f),
        "a narrow view's dome: its radius 35% of the view's height (the sun stays in view)");
    const float down[3] = { 0, 1, 0 };
    float lf[3], lr[3], lu[3];
    rood_math::light_frame(down, lf, lr, lu);
    check(near3(lf, 0, 1, 0) && fabsf(lr[0] * lf[0] + lr[1] * lf[1] + lr[2] * lf[2]) < 1e-5f
            && near(sqrtf(lu[0] * lu[0] + lu[1] * lu[1] + lu[2] * lu[2]), 1),
        "a light's frame: forward its direction, right and up across it");
}

static void rays()
{
    const float o[3] = { 3, 0, -10 }, d[3] = { 0, 0, 1 }, p[3] = { 0, 0, 0 }, x[3] = { 1, 0, 0 };
    check(near(rood_math::line_param(o, d, p, x), 3), "the line point nearest a ray (3 along x)");
    const float py[3] = { 0, 5, 0 };
    check(near(rood_math::line_param(o, d, py, x), 3), "a line passing beside the ray");
    const float n[3] = { 0, 0, 1 }, at[3] = { 0, 0, 20 };
    float hit[3];
    check(rood_math::plane_hit(o, d, at, n, hit) && near3(hit, 3, 0, 20), "a ray meets a plane");
    const float back[3] = { 0, 0, -1 };
    check(!rood_math::plane_hit(o, back, at, n, hit), "not behind it");
    const float c[3] = { 0, 0, 0 }, o2[3] = { 0, 0, -10 };
    float nd[3], fd[3];
    check(rood_math::sphere_hits(o2, d, c, 2, nd, fd) == 2 && near3(nd, 0, 0, -1) && near3(fd, 0, 0, 1),
        "a sphere: its near and far points");
    const float miss[3] = { 5, 0, -10 };
    check(rood_math::sphere_hits(miss, d, c, 2, nd, fd) == 0 && near3(nd, 1, 0, 0) && near3(fd, 1, 0, 0),
        "a ray missing it: toward its nearest point");
}

static void drags()
{
    check(near(rood_math::log_drag(1, 100, false), 2) && near(rood_math::log_drag(1, -100, false), 0.5f),
        "the ring doubles every 100 pixels");
    check(near(rood_math::log_drag(1, 100, true), powf(2, 0.1f)), "Ctrl: a tenth as fast");
    check(near(rood_math::log_drag(0, 100, false), 0.02f), "from 0 it starts at 0.01");
    check(near(rood_math::linear_drag(600, 10, 310, false), 900) && near(rood_math::linear_drag(600, 10, 310, true), 630),
        "linear drags (Ctrl: a tenth)");
    check(near(rood_math::snap_step(613, 16), 608) && near(rood_math::snap_step(-7, 5), -5), "Shift: multiples of the step");
    check(near(rood_math::snap_125(1.4f), 1) && near(rood_math::snap_125(1.5f), 2) && near(rood_math::snap_125(3.3f), 5)
            && near(rood_math::snap_125(0.27f), 0.2f) && near(rood_math::snap_125(7.2f), 10) && rood_math::snap_125(0) == 0,
        "Shift on a logarithmic value: 1, 2, 5 x 10^k");
}

static void sun()
{
    float d[3], az, el;
    rood_math::sun_direction(90, 0, d);
    check(near3(d, 1, 0, 0), "azimuth 90: from +x");
    rood_math::sun_direction(180, 45, d);
    check(near3(d, 0, -0.70711f, -0.70711f), "elevation is up (y down)");
    rood_math::sun_angles(d, &az, &el);
    check(near(az, 180, 1e-2f) && near(el, 45, 1e-2f), "back to the angles");
    rood_math::sun_direction(350, 10, d);
    rood_math::sun_angles(d, &az, &el);
    check(near(az, 350, 1e-2f) && near(el, 10, 1e-2f), "azimuth wraps to 0..360");
    float expect = 2 * acosf(cosf(30 * 3.14159265f / 180) * 0.75f + 0.25f) * 180 / 3.14159265f;
    check(near(rood_math::auto_inner(60), expect) && rood_math::auto_inner(60) < 60,
        "a spot's automatic inner cone: the outer quarter of its cosine fades");
}

/* R (pc/docs/plans/2026-10-15-artist-tools-design.md, "Scale semantics"):
 * shape only, never brightness. */
static void scale_table()
{
    const float twice_z[3] = { 1, 1, 2 }, half_x[3] = { 0.5f, 1, 1.2f }, even[3] = { 1.5f, 1.5f, 1.5f };
    check(near(rood_transform::uniform_ratio(twice_z), 2) && near(rood_transform::uniform_ratio(half_x), 0.5f)
            && near(rood_transform::uniform_ratio(even), 1.5f),
        "a uniform shape takes the axis that moved most");
    rood_lights::Light point, out;
    point.intensity = 1.5f;
    rood_transform::scale_light(point, twice_z, out);
    check(near(out.radius, 1200) && out.intensity == 1.5f && !memcmp(out.color, point.color, sizeof out.color),
        "a point light: radius x2; intensity and colour kept");
    rood_lights::Light spot = point;
    spot.type = 1, spot.cone = 50;
    const float range[3] = { 1, 1, 1.5f };
    rood_transform::scale_light(spot, range, out);
    check(near(out.radius, 900) && out.cone == 50, "a spot: its range x1.5; the cone has its own handle");
    rood_lights::Light rect = point;
    rect.type = 2, rect.size[0] = 128, rect.size[1] = 64;
    const float per_axis[3] = { 2, 0.5f, 1.25f };
    rood_transform::scale_light(rect, per_axis, out);
    check(near(out.size[0], 256) && near(out.size[1], 32) && near(out.radius, 750) && out.intensity == 1.5f,
        "a rect: width x2, height x0.5, range x1.25");
    point.radius = 4000;
    rood_transform::scale_light(point, twice_z, out);
    check(near(out.radius, rood_transform::RADIUS_MAX), "clamped to the schema's range");
    prefab::Actor a, b;
    rood_transform::scale_actor(a, even, b);
    check(near(b.scale, 1.5f), "an actor: its scale x1.5");
    const float huge[3] = { 100, 100, 100 };
    rood_transform::scale_actor(a, huge, b);
    check(near(b.scale, rood_transform::SCALE_MAX), "an actor's scale clamped");
    const float size[3] = { 160, 128, 160 }, by[3] = { 2, 1, 0.5f };
    float sized[3];
    rood_transform::scale_volume(size, by, sized);
    check(near3(sized, 320, 128, 80), "a fog volume: its size per axis");
}

/* The renderers' spot edge and barn doors (light_shape.h). */
static void light_shapes()
{
    float co = cosf(30 * 3.14159265f / 180);
    check(pc_spot_cos_inner(60, 0) == fmaxf(co + (1.0f - co) * 0.25f, co + 1e-4f), "inner 0: the old soft edge exactly");
    check(near(pc_spot_cos_inner(60, 20), cosf(10 * 3.14159265f / 180), 1e-5f), "inner 20: cos 10 degrees");
    check(pc_spot_cos_inner(60, 80) > co, "an inner cone wider than the cone stays inside it");
    float a, b;
    pc_barn_pack(90, 50, &a, &b);
    check(near(a, 0, 1e-4f) && near(b, 50), "doors folded flat (90): no reach forward");
    pc_barn_pack(0, 50, &a, &b);
    check(near(a, 50) && near(b, 0, 1e-4f), "doors straight ahead (0)");
    const float c[3] = { 0, 0, 0 }, r[3] = { 32, 0, 0 }, u[3] = { 0, 0, 32 }, f[3] = { 0, 1, 0 };
    const float under[3] = { 0, 96, 0 }, side[3] = { 128, 96, 0 }, part[3] = { 16, 96, 0 };
    check(near(pc_barn_doors(c, r, u, f, 96, 0, under), 1), "straight under the light: all of it");
    check(near(pc_barn_doors(c, r, u, f, 96, 0, side), 0), "far to the side: the doors hide it all");
    check(near(pc_barn_doors(c, r, u, f, 96, 0, part), 0.75f), "a little to the side: a quarter hidden");
    check(near(pc_barn_doors(c, r, u, f, 0, 50, side), 1), "doors folded flat hide nothing");
}

/* The colour popover's maths (editor_math.h). */
static void colours()
{
    float c[3], k, t;
    rood_math::kelvin_tint(6500, 0, c);
    check(fmaxf(c[0], fmaxf(c[1], c[2])) == 1 && c[2] > 0.9f, "6500 K is near white, brightest channel 1");
    rood_math::kelvin_tint(1900, 0, c);
    check(c[0] == 1 && c[1] < 0.7f && c[2] < 0.3f, "1900 K is orange");
    rood_math::kelvin_tint(2700, 0.4f, c);
    rood_math::estimate_kelvin_tint(c, &k, &t);
    check(fabsf(k - 2700) < 80 && fabsf(t - 0.4f) < 0.05f, "Kelvin and tint back from the colour");
    float g[3], m[3];
    rood_math::kelvin_tint(5600, 1, g);
    rood_math::kelvin_tint(5600, -1, m);
    check(g[1] > m[1] && m[0] >= g[0] - 1e-6f, "tint: +1 green, -1 magenta");
    check(near(rood_math::kelvin_strip_u(1000), 0) && near(rood_math::kelvin_strip_u(12000), 1)
            && near(rood_math::kelvin_strip_k(rood_math::kelvin_strip_u(3200)), 3200, 0.5f),
        "the Kelvin strip: 1000 K to 12000 K");
    float h[3];
    check(rood_math::parse_hex("#4080FF", h) && near3(h, 64 / 255.0f, 128 / 255.0f, 1) && rood_math::parse_hex("4080ff", h)
            && !rood_math::parse_hex("#40", h),
        "hex parses with or without #");
    check(rood_math::format_hex(h) == "#4080FF", "and formats");
    check(near(rood_math::srgb_to_linear(0.5f), 0.21404f, 1e-4f) && rood_math::srgb_to_linear(0) == 0
            && near(rood_math::srgb_to_linear(1), 1),
        "display to linear (sRGB)");
    float n[3] = { 0.2f, 0.4f, 0.1f };
    rood_math::normalize_colour(n);
    check(near3(n, 0.5f, 1, 0.25f), "brightest channel 1");
}

/* A fog volume dropped on a floor keeps its footprint inside the floor's
 * area; "fit to window" takes the pane around the clicked triangle. */
static void placement()
{
    const float lo[2] = { 0, 0 }, hi[2] = { 400, 1000 };
    float c[2], s[2];
    const float at[2] = { 380, 500 }, want[2] = { 600, 300 };
    rood_math::fit_footprint(at, want, lo, hi, c, s);
    check(near(s[0], 400) && near(s[1], 300) && near(c[0], 200) && near(c[1], 500),
        "a footprint wider than the floor: its width the floor's, centred on it; its depth kept");
    const float corner[2] = { 390, 990 }, small_want[2] = { 100, 100 };
    rood_math::fit_footprint(corner, small_want, lo, hi, c, s);
    check(near(s[0], 100) && near(c[0], 350) && near(c[1], 950), "near a corner: moved in so it fits");
    const float no_lo[2] = { 0, 0 }, no_hi[2] = { 0, 0 };
    rood_math::fit_footprint(at, want, no_lo, no_hi, c, s);
    check(near(s[0], 600) && near(c[0], 380), "no floor area known: as wanted, where dropped");

    /* A 100 x 60 quad in the plane z = 0 (two triangles, texture 7), seen from -z. */
    rood_math::PaneTriangle t[5] = {
        { { { 0, 0, 0 }, { 100, 0, 0 }, { 100, -60, 0 } }, 7 },
        { { { 0, 0, 0 }, { 100, -60, 0 }, { 0, -60, 0 } }, 7 },
        { { { 500, 0, 0 }, { 600, 0, 0 }, { 600, -60, 0 } }, 7 }, /* the same texture and plane, apart */
        { { { 100, 0, 0 }, { 160, 0, 0 }, { 100, -60, 0 } }, 8 }, /* beside it, another texture */
        { { { 0, 0, 0 }, { 0, -60, 0 }, { 0, 0, 80 } }, 7 }, /* beside it, another plane */
    };
    const float eye[3] = { 50, -30, -200 };
    rood_math::Pane p;
    bool ok = rood_math::fit_pane(t, 5, 0, eye, p);
    check(ok && p.triangles == 2 && near(p.width, 100) && near(p.height, 60) && near3(p.centre, 50, -30, 0)
            && near3(p.normal, 0, 0, -1),
        "the pane: its two triangles, 100 x 60, centred, facing the eye");
    check(near3(p.right, 1, 0, 0) && near3(p.up, 0, -1, 0), "its axes: a rect light's own frame (right +x, up -y)");
    const float behind[3] = { 50, -30, 200 };
    rood_math::fit_pane(t, 5, 1, behind, p);
    check(near3(p.normal, 0, 0, 1) && p.triangles == 2, "seen from the other side: facing that way");
    check(!rood_math::fit_pane(t, 5, 9, eye, p), "no such triangle: no pane");
}

int main()
{
    view_maths();
    rays();
    drags();
    sun();
    scale_table();
    light_shapes();
    colours();
    placement();
    printf("%s\n", failures ? "FAIL" : "PASS: editor tools maths");
    return failures ? 1 : 0;
}
