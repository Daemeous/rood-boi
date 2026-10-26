// The material survey (material_survey.h): which textures the view shows and
// how much of it each covers, from a coarse CPU rasterisation of the native
// lists' textured triangles.
#include "material_survey.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <unordered_map>

#include "editor_overlays.h"
#include "emissive.h"
#include "emissive_internal.h"
#include "materials.h"
#include "render.h"

extern "C" {
int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick) __attribute__((weak));
int pc_hw_present_all_lists(uint32_t* lists, int max) __attribute__((weak));
}

namespace material_survey {
namespace {

const float NEAR_W = 1.0f; /* clip w below this is behind the near plane (world units in front of the eye) */

struct Vec4 {
    float x, y, z, w;
};
struct Raster {
    float m[16]; /* projection x view, column-major */
    float eye[3];
    int gw, gh;
    int cull; /* 1, -1 or 0 */
    float hand; /* sign relating the world-space facing to the screen's (nclip, y down) */
    std::vector<float> depth; /* 1/w, larger is nearer */
    std::vector<int> use; /* per cell: index into uses, -1 nothing, -2 untextured */
    std::unordered_map<uint64_t, int> use_index;
    struct Use {
        unsigned page, clut;
        uint8_t box[4];
    };
    std::vector<Use> uses;
};

Vec4 transform(const float m[16], const float p[3])
{
    Vec4 o;
    o.x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    o.y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    o.z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
    o.w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    return o;
}
void multiply(const float a[16], const float b[16], float out[16])
{
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}
/* The eye of a view matrix (world): -R^-1 t, R its upper 3 x 3. */
bool view_eye(const float v[16], float eye[3])
{
    float a = v[0], b = v[4], c = v[8], d = v[1], e = v[5], f = v[9], g = v[2], h = v[6], i = v[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f)
        return false;
    float inv[9] = { (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
        (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
        (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
    for (int r = 0; r < 3; r++)
        eye[r] = -(inv[r * 3] * v[12] + inv[r * 3 + 1] * v[13] + inv[r * 3 + 2] * v[14]);
    return true;
}
float world_facing(const float* a, const float* b, const float* c, const float eye[3])
{
    float e1[3], e2[3], n[3];
    for (int k = 0; k < 3; k++)
        e1[k] = b[k] - a[k], e2[k] = c[k] - a[k];
    n[0] = e1[1] * e2[2] - e1[2] * e2[1], n[1] = e1[2] * e2[0] - e1[0] * e2[2], n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    return n[0] * (eye[0] - a[0]) + n[1] * (eye[1] - a[1]) + n[2] * (eye[2] - a[2]);
}
/* Grid position of a clip-space point in front of the near plane. */
void to_grid(const Raster& r, const Vec4& p, float* x, float* y)
{
    *x = (p.x / p.w * 0.5f + 0.5f) * r.gw;
    *y = (0.5f - p.y / p.w * 0.5f) * r.gh;
}

void fill(Raster& r, const Vec4* v, int value)
{
    /* Clip against w >= NEAR_W (a triangle becomes up to a quad), then fan. */
    Vec4 poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const Vec4& a = v[i];
        const Vec4& b = v[(i + 1) % 3];
        bool ain = a.w >= NEAR_W, bin = b.w >= NEAR_W;
        if (ain)
            poly[n++] = a;
        if (ain != bin) {
            float t = (NEAR_W - a.w) / (b.w - a.w);
            poly[n++] = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, NEAR_W };
        }
    }
    for (int k = 1; k + 1 < n; k++) {
        const Vec4* t[3] = { &poly[0], &poly[k], &poly[k + 1] };
        float x[3], y[3], iw[3];
        for (int j = 0; j < 3; j++)
            to_grid(r, *t[j], &x[j], &y[j]), iw[j] = 1.0f / t[j]->w;
        float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
        if (fabsf(area) < 1e-9f)
            continue;
        int x0 = std::max(0, (int)floorf(std::min({ x[0], x[1], x[2] })));
        int x1 = std::min(r.gw - 1, (int)ceilf(std::max({ x[0], x[1], x[2] })));
        int y0 = std::max(0, (int)floorf(std::min({ y[0], y[1], y[2] })));
        int y1 = std::min(r.gh - 1, (int)ceilf(std::max({ y[0], y[1], y[2] })));
        for (int py = y0; py <= y1; py++)
            for (int px = x0; px <= x1; px++) {
                float cx = px + 0.5f, cy = py + 0.5f;
                float w0 = ((x[1] - cx) * (y[2] - cy) - (x[2] - cx) * (y[1] - cy)) / area;
                float w1 = ((x[2] - cx) * (y[0] - cy) - (x[0] - cx) * (y[2] - cy)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                float d = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
                size_t cell = (size_t)py * r.gw + px;
                if (d <= r.depth[cell])
                    continue;
                r.depth[cell] = d;
                r.use[cell] = value;
            }
    }
}

void sink(void* context, const float w[9], const uint8_t* /*rgb*/, const uint8_t uv[6], unsigned code,
    unsigned tpage, unsigned clut)
{
    Raster& r = *(Raster*)context;
    const render_polygon* poly = render_emit_polygon();
    if (poly && (poly->flags & RENDER_POLY_SPRITE))
        return; /* effect sprites face the camera wherever it is: not surfaces */
    if (r.cull && !(poly && (poly->flags & RENDER_POLY_DOUBLE_SIDED))
        && world_facing(&w[0], &w[3], &w[6], r.eye) * r.hand * (float)r.cull <= 0)
        return;
    Vec4 v[3];
    for (int i = 0; i < 3; i++)
        v[i] = transform(r.m, &w[i * 3]);
    if (v[0].w < NEAR_W && v[1].w < NEAR_W && v[2].w < NEAR_W)
        return;
    int value = -2;
    if (code & 4) {
        Raster::Use u;
        u.page = tpage & 0x19f, u.clut = clut;
        u.box[0] = u.box[1] = 255, u.box[2] = u.box[3] = 0;
        int corners = poly && (poly->flags & RENDER_POLY_QUAD) ? 4 : 3;
        for (int c = 0; c < corners; c++) {
            const uint8_t* q = poly ? poly->uv[c] : &uv[c * 2];
            u.box[0] = std::min(u.box[0], q[0]), u.box[1] = std::min(u.box[1], q[1]);
            u.box[2] = std::max(u.box[2], q[0]), u.box[3] = std::max(u.box[3], q[1]);
        }
        uint64_t k = (uint64_t)u.page << 48 | (uint64_t)u.clut << 32 | (uint64_t)u.box[0] << 24
            | (uint64_t)u.box[1] << 16 | (uint64_t)u.box[2] << 8 | u.box[3];
        auto it = r.use_index.find(k);
        if (it == r.use_index.end()) {
            it = r.use_index.emplace(k, (int)r.uses.size()).first;
            r.uses.push_back(u);
        }
        value = it->second;
    }
    fill(r, v, value);
}

} // namespace

bool survey(Result& out, int grid_w, int cull)
{
    out = Result();
    pc_overlay_view view;
    if (!pc_lights_tools_view || !pc_hw_present_all_lists || !pc_lights_tools_view(&view, nullptr))
        return false;
    if (view.rect[2] < 1 || view.rect[3] < 1)
        return false;
    Raster r;
    r.gw = std::clamp(grid_w, 16, 640);
    r.gh = std::clamp((int)lroundf(r.gw * view.rect[3] / view.rect[2]), 8, 640);
    r.cull = cull > 0 ? 1 : cull < 0 ? -1 : 0;
    multiply(view.projection, view.view, r.m);
    if (!view_eye(view.view, r.eye))
        return false;
    /* The view's forward: where clip w grows fastest. */
    float f[3] = { r.m[3], r.m[7], r.m[11] };
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-9f)
        return false;
    for (float& c : f)
        c /= fl;
    /* How the world-space facing relates to the screen's: a small triangle
     * straight ahead, its screen winding against its world one. */
    {
        float up[3] = { 0, 1, 0 };
        if (fabsf(f[1]) > 0.9f)
            up[0] = 1, up[1] = 0;
        float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
        float t[3] = { f[1] * s[2] - f[2] * s[1], f[2] * s[0] - f[0] * s[2], f[0] * s[1] - f[1] * s[0] };
        float p[3][3];
        for (int k = 0; k < 3; k++) {
            p[0][k] = r.eye[k] + f[k] * 256;
            p[1][k] = p[0][k] + s[k] * 16;
            p[2][k] = p[0][k] + t[k] * 16;
        }
        float x[3], y[3];
        for (int i = 0; i < 3; i++) {
            Vec4 c = transform(r.m, p[i]);
            if (c.w <= 0)
                return false;
            to_grid(r, c, &x[i], &y[i]);
        }
        float nclip = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
        float world = world_facing(p[0], p[1], p[2], r.eye);
        r.hand = (nclip > 0) == (world > 0) ? 1.0f : -1.0f;
    }
    r.depth.assign((size_t)r.gw * r.gh, 0.0f);
    r.use.assign((size_t)r.gw * r.gh, -1);
    uint32_t lists[64];
    int n = pc_hw_present_all_lists(lists, 64);
    for (int i = 0; i < n; i++) {
        int kind = render_list_objects(lists[i]);
        if (kind < 0 || kind == 3)
            continue; /* gone, or an effects list (shadows, sprites) */
        render_list_triangles(lists[i], sink, &r);
    }
    emissive::current_room(&out.zone, &out.room);
    out.valid = true;
    out.grid_w = r.gw, out.grid_h = r.gh;
    memcpy(out.eye, r.eye, sizeof out.eye);
    memcpy(out.forward, f, sizeof out.forward);
    /* Cells per use, then per texture (key, size, palette). */
    std::vector<int> per_use(r.uses.size(), 0);
    for (int u : r.use) {
        if (u >= 0)
            per_use[(size_t)u]++, out.textured++;
        else if (u == -2)
            out.untextured++;
    }
    std::map<std::string, int> by_texture;
    std::vector<int> use_texture(r.uses.size(), -1);
    std::vector<int> best_box(0);
    for (size_t i = 0; i < r.uses.size(); i++) {
        const Raster::Use& u = r.uses[i];
        if (!per_use[i])
            continue; /* hidden everywhere: not in view */
        std::string key = emissive::content_key(u.page, u.clut, u.box[0], u.box[1], u.box[2], u.box[3]);
        std::string ck = emissive::clut_key(u.page, u.clut);
        int w = u.box[2] - u.box[0] + 1, h = u.box[3] - u.box[1] + 1;
        char id[96];
        snprintf(id, sizeof id, "%s %dx%d %s", key.c_str(), w, h, ck.c_str());
        auto it = by_texture.find(id);
        if (it == by_texture.end()) {
            it = by_texture.emplace(id, (int)out.textures.size()).first;
            Texture t;
            t.key = key, t.clut_key = ck, t.w = w, t.h = h;
            t.tpage = u.page, t.clut = u.clut;
            memcpy(t.box, u.box, 4);
            const materials::Entry* e = materials::find_match(key, w, h, ck, out.zone, out.room);
            t.entry = e ? e->id : "";
            out.textures.push_back(t);
            best_box.push_back(0);
        }
        Texture& t = out.textures[(size_t)it->second];
        t.boxes.push_back({ u.box[0], u.box[1], u.box[2], u.box[3] });
        t.cells += per_use[i];
        if (per_use[i] > best_box[(size_t)it->second]) { /* where it shows most */
            best_box[(size_t)it->second] = per_use[i];
            t.tpage = u.page, t.clut = u.clut;
            memcpy(t.box, u.box, 4);
        }
        use_texture[i] = it->second;
    }
    std::vector<int> order(out.textures.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(),
        [&](int a, int b) { return out.textures[(size_t)a].cells > out.textures[(size_t)b].cells; });
    std::vector<int> rank(order.size());
    std::vector<Texture> sorted;
    for (size_t i = 0; i < order.size(); i++) {
        rank[(size_t)order[i]] = (int)i;
        sorted.push_back(out.textures[(size_t)order[i]]);
    }
    out.textures.swap(sorted);
    out.map.resize(r.use.size());
    for (size_t c = 0; c < r.use.size(); c++) {
        int u = r.use[c];
        out.map[c] = u >= 0 ? rank[(size_t)use_texture[(size_t)u]] : u;
    }
    return true;
}

} // namespace material_survey
