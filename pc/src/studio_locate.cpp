#include "studio_locate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace studio_locate {

namespace {

struct P2 {
    float x, y;
};

// Sutherland-Hodgman: a convex polygon cut by the half-plane a*x + b*y <= c (keeping the inside), carrying each corner's barycentre.
struct Corner {
    float x, y, b[3];
};
void clip(std::vector<Corner>& poly, float a, float b, float c)
{
    std::vector<Corner> out;
    size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const Corner& p = poly[i];
        const Corner& q = poly[(i + 1) % n];
        float dp = a * p.x + b * p.y - c, dq = a * q.x + b * q.y - c;
        if (dp <= 0)
            out.push_back(p);
        if ((dp < 0 && dq > 0) || (dp > 0 && dq < 0)) {
            float t = dp / (dp - dq);
            Corner m;
            m.x = p.x + (q.x - p.x) * t, m.y = p.y + (q.y - p.y) * t;
            for (int k = 0; k < 3; k++)
                m.b[k] = p.b[k] + (q.b[k] - p.b[k]) * t;
            out.push_back(m);
        }
    }
    poly.swap(out);
}
float area(const std::vector<Corner>& poly)
{
    float s = 0;
    for (size_t i = 0; i < poly.size(); i++) {
        const Corner& p = poly[i];
        const Corner& q = poly[(i + 1) % poly.size()];
        s += p.x * q.y - q.x * p.y;
    }
    return fabsf(s) * 0.5f;
}

} // namespace

std::vector<Hit> locate(const std::vector<Tri>& tris, int page, int clut, int x, int y)
{
    std::vector<Hit> hits;
    for (size_t i = 0; i < tris.size(); i++) {
        const Tri& t = tris[i];
        if ((t.page & 31) != (page & 31) || (clut >= 0 && t.clut != clut))
            continue;
        float u0 = t.uv[0][0], v0 = t.uv[0][1], u1 = t.uv[1][0], v1 = t.uv[1][1], u2 = t.uv[2][0], v2 = t.uv[2][1];
        float det = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0);
        if (fabsf(det) < 1e-6f)
            continue; // (a sliver in the texture: it samples a line of texels, none of which has an area)
        if (std::max({ u0, u1, u2 }) <= x || std::min({ u0, u1, u2 }) >= x + 1 || std::max({ v0, v1, v2 }) <= y
            || std::min({ v0, v1, v2 }) >= y + 1)
            continue;
        std::vector<Corner> poly = { { u0, v0, { 1, 0, 0 } }, { u1, v1, { 0, 1, 0 } }, { u2, v2, { 0, 0, 1 } } };
        clip(poly, -1, 0, -(float)x);      // x >= x0
        clip(poly, 1, 0, (float)x + 1);    // x <= x0 + 1
        clip(poly, 0, -1, -(float)y);      // y >= y0
        clip(poly, 0, 1, (float)y + 1);    // y <= y0 + 1
        if (poly.size() < 3)
            continue;
        float a = area(poly);
        if (a < 1e-5f)
            continue;
        Hit h;
        h.tri = (int)i;
        h.area = a;
        float cx = 0, cy = 0;
        for (const Corner& c : poly) {
            float w[3] = { 0, 0, 0 };
            for (int k = 0; k < 3; k++)
                for (int a2 = 0; a2 < 3; a2++)
                    w[a2] += c.b[k] * t.p[k][a2];
            h.ring.push_back(w[0]), h.ring.push_back(w[1]), h.ring.push_back(w[2]);
            cx += c.x, cy += c.y;
        }
        // the centre: the middle of the footprint (the texel's own centre when it is wholly inside the triangle)
        cx /= (float)poly.size(), cy /= (float)poly.size();
        float l1 = ((cx - u0) * (v2 - v0) - (u2 - u0) * (cy - v0)) / det;
        float l2 = ((u1 - u0) * (cy - v0) - (cx - u0) * (v1 - v0)) / det;
        float l0 = 1 - l1 - l2;
        for (int a2 = 0; a2 < 3; a2++)
            h.centre[a2] = l0 * t.p[0][a2] + l1 * t.p[1][a2] + l2 * t.p[2][a2];
        hits.push_back(std::move(h));
    }
    return hits;
}

bool project(const View& v, const float w[3], float out[2], float* depth)
{
    float e[4], c[4];
    for (int r = 0; r < 4; r++)
        e[r] = v.view[r] * w[0] + v.view[4 + r] * w[1] + v.view[8 + r] * w[2] + v.view[12 + r];
    for (int r = 0; r < 4; r++)
        c[r] = v.projection[r] * e[0] + v.projection[4 + r] * e[1] + v.projection[8 + r] * e[2] + v.projection[12 + r] * e[3];
    if (c[3] < 1e-3f)
        return false;
    out[0] = v.rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * v.rect[2];
    out[1] = v.rect[1] + (0.5f - c[1] / c[3] * 0.5f) * v.rect[3];
    if (depth)
        *depth = c[3];
    return true;
}

namespace {

struct Screen {
    bool ok = false;
    float x[3], y[3], w[3];
};
Screen screen_of(const Tri& t, const View& v)
{
    Screen s;
    s.ok = true;
    for (int k = 0; k < 3 && s.ok; k++) {
        float o[2];
        s.ok = project(v, t.p[k], o, &s.w[k]);
        s.x[k] = o[0], s.y[k] = o[1];
    }
    return s;
}
// The triangle's perspective-correct barycentre at pixel (px, py); false when the pixel is outside it.
bool barycentre(const Screen& s, float px, float py, float l[3])
{
    float det = (s.x[1] - s.x[0]) * (s.y[2] - s.y[0]) - (s.x[2] - s.x[0]) * (s.y[1] - s.y[0]);
    if (fabsf(det) < 1e-9f)
        return false;
    float b1 = ((px - s.x[0]) * (s.y[2] - s.y[0]) - (s.x[2] - s.x[0]) * (py - s.y[0])) / det;
    float b2 = ((s.x[1] - s.x[0]) * (py - s.y[0]) - (px - s.x[0]) * (s.y[1] - s.y[0])) / det;
    float b0 = 1 - b1 - b2;
    const float e = -1e-5f;
    if (b0 < e || b1 < e || b2 < e)
        return false;
    float q0 = b0 / s.w[0], q1 = b1 / s.w[1], q2 = b2 / s.w[2], sum = q0 + q1 + q2;
    l[0] = q0 / sum, l[1] = q1 / sum, l[2] = q2 / sum;
    return true;
}
bool faces_eye(const Screen& s)
{
    // (the PS1's rule: the screen winding of the corners; the mirror of a model's own is not told, so the nearest face of either wins below)
    return (s.x[1] - s.x[0]) * (s.y[2] - s.y[0]) - (s.x[2] - s.x[0]) * (s.y[1] - s.y[0]) > 0;
}

} // namespace

Pick pick(const std::vector<Tri>& tris, const View& v, float px, float py)
{
    Pick best, back;
    float best_depth = 1e30f, back_depth = 1e30f;
    for (size_t i = 0; i < tris.size(); i++) {
        Screen s = screen_of(tris[i], v);
        float l[3];
        if (!s.ok || !barycentre(s, px, py, l))
            continue;
        float depth = l[0] * s.w[0] + l[1] * s.w[1] + l[2] * s.w[2];
        bool front = faces_eye(s);
        Pick& slot = front ? best : back;
        float& slot_depth = front ? best_depth : back_depth;
        if (depth >= slot_depth)
            continue;
        const Tri& t = tris[i];
        slot_depth = depth;
        slot.hit = true, slot.tri = (int)i, slot.depth = depth;
        slot.page = t.page & 31, slot.clut = t.clut;
        slot.u = l[0] * t.uv[0][0] + l[1] * t.uv[1][0] + l[2] * t.uv[2][0];
        slot.v = l[0] * t.uv[0][1] + l[1] * t.uv[1][1] + l[2] * t.uv[2][1];
        slot.x = std::min(255, std::max(0, (int)floorf(slot.u))), slot.y = std::min(255, std::max(0, (int)floorf(slot.v)));
        for (int a = 0; a < 3; a++)
            slot.world[a] = l[0] * t.p[0][a] + l[1] * t.p[1][a] + l[2] * t.p[2][a];
    }
    // The nearer of the two kinds wins; a front face is preferred when it is about as near (a back face seen through nothing is a cloth
    // the game draws two-sided).
    if (best.hit && (!back.hit || best_depth <= back_depth * 1.02f + 1e-3f))
        return best;
    return back.hit ? back : best;
}

void shade(const std::vector<Tri>& tris, const View& v, std::vector<Hit>& hits, float tolerance)
{
    for (Hit& h : hits) {
        h.on_screen = project(v, h.centre, h.screen, &h.depth);
        h.visible = false;
        if (!h.on_screen)
            continue;
        // visible: its triangle faces the eye and no triangle is nearer at its pixel (by more than the tolerance)
        if (!faces_eye(screen_of(tris[(size_t)h.tri], v)))
            continue;
        float nearest = 1e30f;
        for (const Tri& t : tris) {
            Screen s = screen_of(t, v);
            float l[3];
            if (s.ok && barycentre(s, h.screen[0], h.screen[1], l))
                nearest = std::min(nearest, l[0] * s.w[0] + l[1] * s.w[1] + l[2] * s.w[2]);
        }
        h.visible = nearest >= h.depth - tolerance;
    }
}

std::string page_label(int page, int clut)
{
    char text[32];
    snprintf(text, sizeof text, "_p%d_c%04x", page & 31, clut & 0xffff);
    return text;
}

bool parse_label(const std::string& label, int* page, int* clut, uint64_t* model)
{
    size_t at = label.find("_p");
    if (at == std::string::npos)
        return false;
    int p = 0, c = 0, used = 0;
    if (sscanf(label.c_str() + at, "_p%d_c%x%n", &p, &c, &used) != 2)
        return false;
    *page = p & 31, *clut = c & 0xffff;
    *model = 0;
    if (at >= 2 && label[0] == 'm') {
        char* end = nullptr;
        unsigned long long m = strtoull(label.substr(1, at - 1).c_str(), &end, 16);
        if (end && !*end)
            *model = m;
    }
    return true;
}

} // namespace studio_locate
