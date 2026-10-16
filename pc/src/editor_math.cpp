// Plain maths of the editor's viewport tools (editor_math.h).
#include "editor_math.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "editor_picking.h"
#include "light_prefabs.h" /* prefab::kelvin */

namespace rood_math {
namespace {
const float DEG = 3.14159265f / 180.0f;
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void normalize(float v[3])
{
    float l = sqrtf(dot(v, v));
    for (int k = 0; k < 3; k++)
        v[k] = l > 1e-9f ? v[k] / l : 0;
}
} // namespace

bool project(const float view[16], const float projection[16], const float rect[4], const float w[3], float out[2])
{
    float e[4], c[4];
    for (int r = 0; r < 4; r++)
        e[r] = view[r] * w[0] + view[4 + r] * w[1] + view[8 + r] * w[2] + view[12 + r];
    for (int r = 0; r < 4; r++)
        c[r] = projection[r] * e[0] + projection[4 + r] * e[1] + projection[8 + r] * e[2] + projection[12 + r] * e[3];
    if (c[3] < 1e-3f)
        return false;
    out[0] = rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * rect[2];
    out[1] = rect[1] + (0.5f - c[1] / c[3] * 0.5f) * rect[3];
    return true;
}
bool view_axes(const float view[16], const float projection[16], const float rect[4], float right[3], float up[3],
    float forward[3])
{
    float cx = rect[0] + rect[2] * 0.5f, cy = rect[1] + rect[3] * 0.5f;
    float o[3], d[3], ox[3], dx[3], oy[3], dy[3];
    if (!rood_pick::ray(view, projection, rect, cx, cy, o, d) || !rood_pick::ray(view, projection, rect, cx + 1, cy, ox, dx)
        || !rood_pick::ray(view, projection, rect, cx, cy - 1, oy, dy))
        return false;
    for (int k = 0; k < 3; k++) {
        float p = o[k] + d[k] * 1000;
        right[k] = ox[k] + dx[k] * 1000 - p;
        up[k] = oy[k] + dy[k] * 1000 - p;
        forward[k] = d[k];
    }
    normalize(forward);
    /* across the central ray only (the neighbouring rays lean outward) */
    float r_on = dot(right, forward), u_on = dot(up, forward);
    for (int k = 0; k < 3; k++)
        right[k] -= forward[k] * r_on, up[k] -= forward[k] * u_on;
    normalize(right);
    normalize(up);
    return true;
}
bool view_eye(const float view[16], float eye[3])
{
    float m[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r][c] = view[c * 4 + r];
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabsf(det) < 1e-12f)
        return false;
    for (int k = 0; k < 3; k++) { /* Cramer's rule on R e = -t */
        float a[3][3];
        memcpy(a, m, sizeof a);
        for (int r = 0; r < 3; r++)
            a[r][k] = -view[12 + r];
        eye[k] = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                     + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
            / det;
    }
    return true;
}
void light_frame(const float direction[3], float forward[3], float right[3], float up[3])
{
    float len = sqrtf(dot(direction, direction));
    for (int k = 0; k < 3; k++)
        forward[k] = len > 1e-6f ? direction[k] / len : (k == 1);
    float world_up[3] = { 0, -1, 0 };
    if (fabsf(forward[1]) > 0.95f)
        world_up[1] = 0, world_up[2] = 1;
    right[0] = world_up[1] * forward[2] - world_up[2] * forward[1];
    right[1] = world_up[2] * forward[0] - world_up[0] * forward[2];
    right[2] = world_up[0] * forward[1] - world_up[1] * forward[0];
    normalize(right);
    up[0] = forward[1] * right[2] - forward[2] * right[1];
    up[1] = forward[2] * right[0] - forward[0] * right[2];
    up[2] = forward[0] * right[1] - forward[1] * right[0];
}

float line_param(const float o[3], const float d[3], const float p[3], const float u[3])
{
    float w[3] = { o[0] - p[0], o[1] - p[1], o[2] - p[2] };
    float b = dot(d, u), denom = 1 - b * b;
    if (denom < 1e-6f) /* parallel: where the ray's origin is along the line */
        return dot(w, u);
    return (dot(u, w) - b * dot(d, w)) / denom;
}
bool plane_hit(const float o[3], const float d[3], const float p[3], const float n[3], float out[3])
{
    float denom = dot(d, n);
    if (fabsf(denom) < 1e-6f)
        return false;
    float w[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
    float t = dot(w, n) / denom;
    if (t < 0)
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = o[k] + d[k] * t;
    return true;
}
int sphere_hits(const float o[3], const float d[3], const float c[3], float r, float near_dir[3], float far_dir[3])
{
    float oc[3] = { o[0] - c[0], o[1] - c[1], o[2] - c[2] };
    float b = dot(oc, d), disc = b * b - (dot(oc, oc) - r * r);
    if (disc < 0) {
        float t = fmaxf(-b, 0.0f);
        for (int k = 0; k < 3; k++)
            near_dir[k] = far_dir[k] = oc[k] + d[k] * t;
        normalize(near_dir);
        normalize(far_dir);
        return 0;
    }
    float s = sqrtf(disc), t0 = -b - s, t1 = -b + s;
    for (int k = 0; k < 3; k++)
        near_dir[k] = oc[k] + d[k] * t0, far_dir[k] = oc[k] + d[k] * t1;
    normalize(near_dir);
    normalize(far_dir);
    return 2;
}

float log_drag(float start, float pixels, bool fine) { return fmaxf(start, 0.01f) * exp2f(pixels / (fine ? 1000.0f : 100.0f)); }
float linear_drag(float start, float t_press, float t_now, bool fine) { return start + (t_now - t_press) * (fine ? 0.1f : 1.0f); }
float snap_step(float v, float step) { return step > 0 ? roundf(v / step) * step : v; }
float snap_125(float v)
{
    if (v <= 0)
        return 0;
    float e = floorf(log10f(v)), scale = powf(10, e), m = v / scale, best = 1, best_d = 1e30f;
    for (float c : { 1.0f, 2.0f, 5.0f, 10.0f }) {
        float dist = fabsf(logf(m / c));
        if (dist < best_d)
            best_d = dist, best = c;
    }
    return best * scale;
}

void sun_direction(float azimuth, float elevation, float out[3])
{
    float a = azimuth * DEG, e = elevation * DEG;
    out[0] = cosf(e) * sinf(a);
    out[1] = -sinf(e); /* y is down */
    out[2] = cosf(e) * cosf(a);
}
void sun_angles(const float dir[3], float* azimuth, float* elevation)
{
    *elevation = asinf(fminf(1.0f, fmaxf(-1.0f, -dir[1]))) / DEG;
    float a = atan2f(dir[0], dir[2]) / DEG;
    *azimuth = a < 0 ? a + 360 : a;
}
bool dome_centre(const float view[16], const float projection[16], const float rect[4], float out[3])
{
    float eye[3], r[3], u[3], f[3];
    if (!view_eye(view, eye) || !view_axes(view, projection, rect, r, u, f))
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = eye[k] + f[k] * DOME_AHEAD;
    return true;
}

float dome_radius(const float view[16], const float projection[16], const float rect[4])
{
    float c[3], r[3], u[3], f[3], a[2], b[2];
    if (!dome_centre(view, projection, rect, c) || !view_axes(view, projection, rect, r, u, f))
        return DOME_AHEAD * 0.5f;
    float above[3] = { c[0] + u[0] * 10, c[1] + u[1] * 10, c[2] + u[2] * 10 };
    if (!project(view, projection, rect, c, a) || !project(view, projection, rect, above, b))
        return DOME_AHEAD * 0.5f;
    float per_unit = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1])) / 10;
    return per_unit > 1e-6f ? fminf(0.35f * rect[3] / per_unit, DOME_AHEAD * 0.5f) : DOME_AHEAD * 0.5f;
}

float auto_inner(float cone)
{
    float co = cosf(cone * 0.5f * DEG), ci = co + (1 - co) * 0.25f;
    return 2 * acosf(ci) / DEG;
}
void normalize_colour(float rgb[3])
{
    float m = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
    if (m > 1e-6f)
        for (int k = 0; k < 3; k++)
            rgb[k] /= m;
}
void kelvin_tint(float kelvin, float tint, float out[3])
{
    prefab::kelvin(fminf(fmaxf(kelvin, 1000.0f), 12000.0f), out);
    out[1] *= 1 + 0.25f * fminf(fmaxf(tint, -1.0f), 1.0f);
    normalize_colour(out);
}
float kelvin_strip_u(float kelvin) { return (1000.0f - 1e6f / kelvin) / (1000.0f - 1e6f / 12000.0f); }
float kelvin_strip_k(float u) { return 1e6f / (1000.0f - u * (1000.0f - 1e6f / 12000.0f)); }
void estimate_kelvin_tint(const float rgb[3], float* kelvin, float* tint)
{
    float c[3] = { rgb[0], rgb[1], rgb[2] };
    normalize_colour(c);
    float best = 1e30f;
    *kelvin = 6500, *tint = 0;
    for (int i = 0; i <= 400; i++) {
        float k = kelvin_strip_k(i / 400.0f), b[3];
        prefab::kelvin(k, b);
        float rb = b[0] * b[0] + b[2] * b[2];
        if (rb < 1e-9f || b[1] < 1e-6f)
            continue;
        float s = (c[0] * b[0] + c[2] * b[2]) / rb; /* red and blue set the scale, green the tint */
        if (s <= 0)
            continue;
        float t = fminf(fmaxf((c[1] / (s * b[1]) - 1) / 0.25f, -1.0f), 1.0f), g = s * b[1] * (1 + 0.25f * t);
        float e = (c[0] - s * b[0]) * (c[0] - s * b[0]) + (c[2] - s * b[2]) * (c[2] - s * b[2]) + (c[1] - g) * (c[1] - g);
        if (e < best)
            best = e, *kelvin = k, *tint = t;
    }
}
bool parse_hex(const std::string& text, float out[3])
{
    const char* p = text.c_str();
    while (*p == ' ' || *p == '#')
        p++;
    int digits = 0;
    while (isxdigit((unsigned char)p[digits]))
        digits++;
    unsigned v[3];
    char rest[2];
    if (digits != 6 || sscanf(p, "%2x%2x%2x%1s", &v[0], &v[1], &v[2], rest) != 3)
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = v[k] / 255.0f;
    return true;
}
std::string format_hex(const float rgb[3])
{
    int c[3];
    for (int k = 0; k < 3; k++)
        c[k] = (int)lroundf(fminf(fmaxf(rgb[k], 0.0f), 1.0f) * 255);
    char text[16];
    snprintf(text, sizeof text, "#%02X%02X%02X", c[0], c[1], c[2]);
    return text;
}
float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
} // namespace rood_math
