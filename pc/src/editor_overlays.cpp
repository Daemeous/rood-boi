// Editor viewport overlays and snapping (editor_overlays.h).
//
// The room's geometry comes from the scene capture (scene_capture.c: world
// space triangles, a quad as two halves) welded into points and edges (a
// quad's diagonal is not an edge, as in Houdini). Which of them the view
// shows follows what the view draws:
//
// - groups: with the free camera live, the editor's view into rooms
//   (room_editor.h: groups hidden in the Outliner, black shells, the facing
//   cutaway for the free camera's yaw); with the game camera, the game's own
//   facing cutaway; groups the room hides (flags 0x1100) never;
// - the cut to focus (render.c): points and edge pieces inside the cone from
//   the eye to the focus on polygons that separate the two are left out, and
//   those polygons hide nothing there;
// - depth: a point or edge piece hidden by a nearer opaque room polygon
//   (ray cast against a BVH of the shown polygons, back faces culled like the
//   PS1 unless double-sided) or lying only on back faces is drawn dimmed, not
//   hidden (as Houdini and UE draw hidden lines).
//
// The occlusion test is progressive: a few milliseconds of rays per frame,
// round robin over every point and edge piece, restarted when the view
// changes, so the overlay is cheap to leave on; a still view costs nothing.
// Drawing goes to the ImGui background draw list, so it works over the
// OpenGL and the Vulkan presentation alike.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <initializer_list>
#include <chrono>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "editor_overlays.h"
#include "room_editor.h"
#include "scene_capture.h"

extern "C" {
int pc_live_camera_active(void) __attribute__((weak));
int pc_editor_cutaway_focus(float out[3]) __attribute__((weak));
int pc_editor_cutaway_state(int* hidden, float* amount) __attribute__((weak));
const float* pc_hw_casters(int* count, int* serial) __attribute__((weak));
int pc_room_editor_groups(pc_room_group_info* out, int max) __attribute__((weak));
extern int pc_editor_hide_masks __attribute__((weak));
extern int pc_editor_cutaway __attribute__((weak));
extern float pc_editor_cutaway_radius __attribute__((weak));
}

extern "C" {
int pc_overlay_wire = 0, pc_overlay_points = 0, pc_overlay_actor_wire = 0, pc_overlay_snap_show = 1;
float pc_overlay_occluded = 0.25f;
int pc_snap_point = 0, pc_snap_edge = 0, pc_snap_midpoint = 0, pc_snap_surface = 0;
float pc_snap_radius = 14, pc_snap_highlight = 128, pc_snap_offset = 64;
}

namespace {
typedef std::chrono::steady_clock Clock;
float ms_since(Clock::time_point t) { return std::chrono::duration<float, std::milli>(Clock::now() - t).count(); }

void sub(const float a[3], const float b[3], float out[3])
{
    for (int k = 0; k < 3; k++)
        out[k] = a[k] - b[k];
}
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
float length(const float a[3]) { return sqrtf(dot(a, a)); }

/* ---- the room as points and edges ---- */
struct Geometry {
    int serial = -1;
    int triangles = 0;
    std::vector<float> points; // xyz per point
    std::vector<int> edges; // two point indices per edge
    std::vector<int> tri_points; // 3 per triangle
    std::vector<int> tri_edges; // 3 per triangle (0-1, 1-2, 2-0), -1 for a quad's diagonal
    std::vector<int> partner; // the other half of a quad, else -1
    std::vector<int> edge_sample; // first occlusion sample of each edge (pieces along it)
    std::vector<uint8_t> edge_pieces;
    int samples = 0; // points, then the edge pieces
};
Geometry geo;

/* Per frame: what the view shows. */
std::vector<uint8_t> tri_shown, tri_front, tri_cut;
std::vector<uint8_t> point_flags, edge_flags; // 1 shown, 2 on a front face, 4 on a polygon that is not cut
unsigned shown_hash;

/* Occlusion: one state per sample (0 visible, 1 hidden, 2 not tested yet). */
std::vector<uint8_t> occluded;
std::vector<float> sample_pos;
int cursor, remaining;
uint64_t view_key;

/* The view's eye, matrices and the cutaway. */
struct Frame {
    float eye[3];
    float clip[16]; // projection * view (column-major)
    float rect[4];
    bool cut;
    float focus[3], radius;
    float facing_sign; // front faces: sign * dot(cross(b - a, c - a), a - eye) > 0
} frame;

/* BVH of the shown opaque room triangles. */
struct Node {
    float lo[3], hi[3];
    int first, count; // count 0: inner node, children first and first + 1
};
std::vector<Node> nodes;
std::vector<int> bvh_tris;
uint64_t bvh_hash = ~0ull;

/* Stats and the held snap result (RPC). */
pc_overlay_stats stats;
bool held;
pc_snap_result held_result;
float character_segments; // last frame's count, for the stats

const scene_triangle* scene(int* count) { return pc_scene_triangle_data(count); }

void build_geometry()
{
    int count;
    const scene_triangle* tris = scene(&count);
    int serial = pc_scene_serial();
    if (serial == geo.serial && count == geo.triangles)
        return;
    geo = Geometry();
    geo.serial = serial;
    geo.triangles = count;
    geo.tri_points.resize(count * 3);
    geo.tri_edges.resize(count * 3);
    geo.partner.assign(count, -1);
    /* Weld at half a world unit. */
    std::unordered_map<uint64_t, int> point_of;
    auto weld = [&](const float* p) {
        int64_t x = llroundf(p[0] * 2), y = llroundf(p[1] * 2), z = llroundf(p[2] * 2);
        uint64_t key = ((uint64_t)(x & 0x1fffff) << 42) | ((uint64_t)(y & 0x1fffff) << 21) | (uint64_t)(z & 0x1fffff);
        auto it = point_of.find(key);
        if (it != point_of.end())
            return it->second;
        int index = (int)geo.points.size() / 3;
        geo.points.insert(geo.points.end(), p, p + 3);
        point_of.emplace(key, index);
        return index;
    };
    std::unordered_map<uint64_t, int> edge_of;
    for (int t = 0; t < count; t++) {
        const scene_triangle& tri = tris[t];
        for (int j = 0; j < 3; j++)
            geo.tri_points[t * 3 + j] = weld(tri.vertex[j].position);
        if (tri.quad == 1 && t + 1 < count && tris[t + 1].quad == 2)
            geo.partner[t] = t + 1, geo.partner[t + 1] = t;
        for (int j = 0; j < 3; j++) {
            /* A quad's diagonal: vertex 1-2 of its first half, 2-0 of its second. */
            if ((tri.quad == 1 && j == 1) || (tri.quad == 2 && j == 2)) {
                geo.tri_edges[t * 3 + j] = -1;
                continue;
            }
            int a = geo.tri_points[t * 3 + j], b = geo.tri_points[t * 3 + (j + 1) % 3];
            if (a == b) {
                geo.tri_edges[t * 3 + j] = -1;
                continue;
            }
            uint64_t key = a < b ? ((uint64_t)a << 32) | (uint32_t)b : ((uint64_t)b << 32) | (uint32_t)a;
            auto it = edge_of.find(key);
            if (it == edge_of.end()) {
                int index = (int)geo.edges.size() / 2;
                geo.edges.push_back(a);
                geo.edges.push_back(b);
                it = edge_of.emplace(key, index).first;
            }
            geo.tri_edges[t * 3 + j] = it->second;
        }
    }
    /* Occlusion samples: every point, then each edge in pieces of up to 48
     * world units (1 to 16), tested at their middles. */
    int points = (int)geo.points.size() / 3, edges = (int)geo.edges.size() / 2;
    geo.samples = points;
    geo.edge_sample.resize(edges);
    geo.edge_pieces.resize(edges);
    sample_pos.assign(geo.points.begin(), geo.points.end());
    for (int e = 0; e < edges; e++) {
        const float *a = &geo.points[geo.edges[e * 2] * 3], *b = &geo.points[geo.edges[e * 2 + 1] * 3];
        float d[3];
        sub(b, a, d);
        int pieces = (int)ceilf(length(d) / 48);
        pieces = pieces < 1 ? 1 : pieces > 16 ? 16 : pieces;
        geo.edge_sample[e] = geo.samples;
        geo.edge_pieces[e] = (uint8_t)pieces;
        for (int k = 0; k < pieces; k++) {
            float t = (k + 0.5f) / pieces;
            for (int c = 0; c < 3; c++)
                sample_pos.push_back(a[c] + d[c] * t);
        }
        geo.samples += pieces;
    }
    occluded.assign(geo.samples, 2);
    point_flags.assign(points, 0);
    edge_flags.assign(edges, 0);
    cursor = 0;
    remaining = geo.samples;
    bvh_hash = ~0ull;
    shown_hash = 0;
    fprintf(stderr, "[overlay] room: %d triangles, %d points, %d edges, %d occlusion samples\n", count, points, edges,
        geo.samples);
}

/* ---- the view ---- */
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
bool invert3(const float m[3][3], float out[3][3])
{
    float c[3][3] = {
        { m[1][1] * m[2][2] - m[1][2] * m[2][1], m[0][2] * m[2][1] - m[0][1] * m[2][2], m[0][1] * m[1][2] - m[0][2] * m[1][1] },
        { m[1][2] * m[2][0] - m[1][0] * m[2][2], m[0][0] * m[2][2] - m[0][2] * m[2][0], m[0][2] * m[1][0] - m[0][0] * m[1][2] },
        { m[1][0] * m[2][1] - m[1][1] * m[2][0], m[0][1] * m[2][0] - m[0][0] * m[2][1], m[0][0] * m[1][1] - m[0][1] * m[1][0] },
    };
    float det = m[0][0] * c[0][0] + m[0][1] * c[1][0] + m[0][2] * c[2][0];
    if (fabsf(det) < 1e-12f)
        return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            out[i][j] = c[i][j] / det;
    return true;
}
void setup_frame(const pc_overlay_view* v)
{
    multiply(v->projection, v->view, frame.clip);
    memcpy(frame.rect, v->rect, sizeof frame.rect);
    float r[3][3], inv[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i][j] = v->view[j * 4 + i];
    if (invert3(r, inv))
        for (int i = 0; i < 3; i++)
            frame.eye[i] = -(inv[i][0] * v->view[12] + inv[i][1] * v->view[13] + inv[i][2] * v->view[14]);
    /* The PS1's facing test (render.c facing(): NCLIP on the game camera's
     * screen) in world space: the game camera's handedness gives the sign. */
    frame.facing_sign = 1;
    float gr[3][3], gt[3], h, ox, oy;
    if (pc_scene_camera_get(gr, gt, &h, &ox, &oy)) {
        float det = gr[0][0] * (gr[1][1] * gr[2][2] - gr[1][2] * gr[2][1])
            - gr[0][1] * (gr[1][0] * gr[2][2] - gr[1][2] * gr[2][0]) + gr[0][2] * (gr[1][0] * gr[2][1] - gr[1][1] * gr[2][0]);
        frame.facing_sign = det < 0 ? -1.0f : 1.0f;
    }
    bool live = pc_live_camera_active && pc_live_camera_active();
    /* The cut as drawn: only while it hides the focus (faded in past half;
     * a ghost hides nothing behind it either). */
    float amount = 1;
    if (pc_editor_cutaway_state)
        pc_editor_cutaway_state(nullptr, &amount);
    frame.cut = live && &pc_editor_cutaway && pc_editor_cutaway && pc_editor_cutaway_radius > 0 && amount > 0.5f
        && pc_editor_cutaway_focus && pc_editor_cutaway_focus(frame.focus);
    frame.radius = frame.cut ? pc_editor_cutaway_radius : 0;
}

/* Clip coordinates of a world point. */
void to_clip(const float w[3], float out[4])
{
    const float* m = frame.clip;
    for (int r = 0; r < 4; r++)
        out[r] = m[r] * w[0] + m[4 + r] * w[1] + m[8 + r] * w[2] + m[12 + r];
}
const float near_w = 8;
ImVec2 to_screen(const float c[4])
{
    return ImVec2(frame.rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * frame.rect[2],
        frame.rect[1] + (0.5f - c[1] / c[3] * 0.5f) * frame.rect[3]);
}
bool project(const float w[3], ImVec2& out, float* depth = nullptr)
{
    float c[4];
    to_clip(w, c);
    if (c[3] < near_w)
        return false;
    out = to_screen(c);
    if (depth)
        *depth = c[3];
    return true;
}
/* A world segment clipped to the near plane: window end points and the
 * kept parameter range. */
bool project_segment(const float a[3], const float b[3], ImVec2& pa, ImVec2& pb, float* t0 = nullptr,
    float* t1 = nullptr, float* wa = nullptr, float* wb = nullptr)
{
    float ca[4], cb[4];
    to_clip(a, ca);
    to_clip(b, cb);
    float s0 = 0, s1 = 1;
    if (ca[3] < near_w && cb[3] < near_w)
        return false;
    if (ca[3] < near_w || cb[3] < near_w) {
        float t = (near_w - ca[3]) / (cb[3] - ca[3]);
        float c[4];
        for (int k = 0; k < 4; k++)
            c[k] = ca[k] + (cb[k] - ca[k]) * t;
        if (ca[3] < near_w)
            memcpy(ca, c, sizeof c), s0 = t;
        else
            memcpy(cb, c, sizeof c), s1 = t;
    }
    pa = to_screen(ca);
    pb = to_screen(cb);
    if (t0)
        *t0 = s0, *t1 = s1;
    if (wa)
        *wa = ca[3], *wb = cb[3];
    return true;
}

/* The cut to focus at a world point: true when inside the solid half of the
 * cone's dithered rim (render.c's fragment test, keep < 1/2). */
bool in_cut_cone(const float p[3])
{
    float v[3], f[3];
    sub(p, frame.eye, v);
    sub(frame.focus, frame.eye, f);
    float ff = dot(f, f);
    float t = ff > 1e-6f ? dot(v, f) / ff : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float d[3] = { v[0] - f[0] * t, v[1] - f[1] * t, v[2] - f[2] * t };
    float keep = (length(d) / fmaxf(frame.radius * t, 1e-3f) - 0.75f) * 4;
    return keep < 0.5f;
}

/* ---- which triangles the view shows ---- */
void update_flags()
{
    int count;
    const scene_triangle* tris = scene(&count);
    if (count != geo.triangles)
        return;
    tri_shown.assign(count, 0);
    tri_front.assign(count, 0);
    tri_cut.assign(count, 0);
    bool live = pc_live_camera_active && pc_live_camera_active();
    static pc_room_group_info groups[4096];
    int group_count = live && pc_room_editor_groups ? pc_room_editor_groups(groups, 4096) : 0;
    unsigned facing = pc_scene_facing();
    uint64_t hash = 1469598103934665603ull;
    for (int t = 0; t < count; t++) {
        const scene_triangle& tri = tris[t];
        bool shown;
        if (tri.flags & 0x1100)
            shown = false;
        else if (live && tri.group < group_count) {
            const pc_room_group_info& g = groups[tri.group];
            shown = !g.hidden && !g.cutaway && !(g.black && &pc_editor_hide_masks && pc_editor_hide_masks);
        } else
            shown = (facing >> 16) || !((tri.flags ^ 255) & facing & 255);
        tri_shown[t] = shown;
        if (shown)
            hash = (hash ^ (uint64_t)t) * 1099511628211ull;
    }
    shown_hash = (unsigned)(hash ^ (hash >> 32));
    for (int t = 0; t < count; t++) {
        if (!tri_shown[t])
            continue;
        const float *a = tris[t].vertex[0].position, *b = tris[t].vertex[1].position, *c = tris[t].vertex[2].position;
        float e1[3], e2[3], n[3], to[3];
        sub(b, a, e1);
        sub(c, a, e2);
        cross(e1, e2, n);
        sub(a, frame.eye, to);
        tri_front[t] = (tris[t].code & 1) || frame.facing_sign * dot(n, to) > 0;
        if (frame.cut) {
            float l = length(n), side[3], focus[3];
            if (l > 1e-6f) {
                sub(frame.eye, a, side);
                sub(frame.focus, a, focus);
                float e = dot(n, side) / l, f = dot(n, focus) / l;
                tri_cut[t] = e * f < 0 && fabsf(f) > 8;
            }
        }
    }
    /* A quad is front facing when either half is (render.c). */
    for (int t = 0; t < count; t++)
        if (geo.partner[t] >= 0 && tri_shown[t] && tri_front[geo.partner[t]])
            tri_front[t] = 1;
    std::fill(point_flags.begin(), point_flags.end(), 0);
    std::fill(edge_flags.begin(), edge_flags.end(), 0);
    for (int t = 0; t < count; t++) {
        if (!tri_shown[t])
            continue;
        uint8_t f = 1 | (tri_front[t] ? 2 : 0) | (tri_cut[t] ? 0 : 4);
        for (int j = 0; j < 3; j++) {
            point_flags[geo.tri_points[t * 3 + j]] |= f;
            if (geo.tri_edges[t * 3 + j] >= 0)
                edge_flags[geo.tri_edges[t * 3 + j]] |= f;
        }
    }
}
/* Shown and not inside the cut: drawn and snappable. */
bool point_drawn(int p)
{
    uint8_t f = point_flags[p];
    return (f & 1) && ((f & 4) || !frame.cut || !in_cut_cone(&geo.points[p * 3]));
}

/* ---- the BVH and the occlusion rays ---- */
void build_bvh()
{
    int count;
    const scene_triangle* tris = scene(&count);
    bvh_tris.clear();
    nodes.clear();
    for (int t = 0; t < count; t++)
        if (tri_shown[t] && !(tris[t].code & 2)) /* translucent polygons hide nothing */
            bvh_tris.push_back(t);
    stats.bvh_triangles = (int)bvh_tris.size();
    if (bvh_tris.empty())
        return;
    std::vector<float> centre(count * 3);
    for (int t : bvh_tris)
        for (int k = 0; k < 3; k++)
            centre[t * 3 + k] = (tris[t].vertex[0].position[k] + tris[t].vertex[1].position[k]
                                    + tris[t].vertex[2].position[k]) / 3;
    struct Task {
        int node, first, count;
    };
    std::vector<Task> stack;
    nodes.push_back(Node());
    stack.push_back({ 0, 0, (int)bvh_tris.size() });
    while (!stack.empty()) {
        Task task = stack.back();
        stack.pop_back();
        Node node;
        float clo[3] = { 1e30f, 1e30f, 1e30f }, chi[3] = { -1e30f, -1e30f, -1e30f };
        for (int k = 0; k < 3; k++)
            node.lo[k] = 1e30f, node.hi[k] = -1e30f;
        for (int i = task.first; i < task.first + task.count; i++) {
            int t = bvh_tris[i];
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) {
                    node.lo[k] = fminf(node.lo[k], tris[t].vertex[j].position[k]);
                    node.hi[k] = fmaxf(node.hi[k], tris[t].vertex[j].position[k]);
                }
            for (int k = 0; k < 3; k++)
                clo[k] = fminf(clo[k], centre[t * 3 + k]), chi[k] = fmaxf(chi[k], centre[t * 3 + k]);
        }
        int axis = 0;
        for (int k = 1; k < 3; k++)
            if (chi[k] - clo[k] > chi[axis] - clo[axis])
                axis = k;
        if (task.count <= 4 || chi[axis] - clo[axis] < 1e-3f) {
            node.first = task.first, node.count = task.count;
            nodes[task.node] = node;
            continue;
        }
        int mid = task.first + task.count / 2;
        std::nth_element(bvh_tris.begin() + task.first, bvh_tris.begin() + mid, bvh_tris.begin() + task.first + task.count,
            [&](int a, int b) { return centre[a * 3 + axis] < centre[b * 3 + axis]; });
        node.first = (int)nodes.size(), node.count = 0;
        nodes[task.node] = node;
        nodes.push_back(Node());
        nodes.push_back(Node());
        stack.push_back({ node.first, task.first, mid - task.first });
        stack.push_back({ node.first + 1, mid, task.first + task.count - mid });
    }
}
bool box_hit(const Node& n, const float o[3], const float inv[3], float tmax)
{
    float t0 = 0, t1 = tmax;
    for (int k = 0; k < 3; k++) {
        float a = (n.lo[k] - o[k]) * inv[k], b = (n.hi[k] - o[k]) * inv[k];
        if (a > b)
            std::swap(a, b);
        t0 = a > t0 ? a : t0;
        t1 = b < t1 ? b : t1;
        if (t0 > t1)
            return false;
    }
    return true;
}
/* Anything opaque between the eye and the point (back faces and the cut
 * pass rays through). */
bool blocked(const float p[3])
{
    if (nodes.empty())
        return false;
    int count;
    const scene_triangle* tris = scene(&count);
    float d[3];
    sub(p, frame.eye, d);
    float dist = length(d);
    if (dist < 1e-3f)
        return false;
    for (int k = 0; k < 3; k++)
        d[k] /= dist;
    float tmax = dist * 0.995f - 1.0f, inv[3];
    if (tmax <= 0)
        return false;
    for (int k = 0; k < 3; k++)
        inv[k] = 1.0f / (fabsf(d[k]) > 1e-9f ? d[k] : 1e-9f);
    int stack[64], top = 0;
    stack[top++] = 0;
    while (top) {
        const Node& n = nodes[stack[--top]];
        if (!box_hit(n, frame.eye, inv, tmax))
            continue;
        if (!n.count) {
            if (top < 62) {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
            continue;
        }
        for (int i = n.first; i < n.first + n.count; i++) {
            int t = bvh_tris[i];
            if (!tri_front[t])
                continue;
            const float* a = tris[t].vertex[0].position;
            float e1[3], e2[3], pv[3], tv[3], qv[3];
            sub(tris[t].vertex[1].position, a, e1);
            sub(tris[t].vertex[2].position, a, e2);
            cross(d, e2, pv);
            float det = dot(e1, pv);
            if (fabsf(det) < 1e-8f)
                continue;
            float id = 1 / det;
            sub(frame.eye, a, tv);
            float u = dot(tv, pv) * id;
            if (u < 0 || u > 1)
                continue;
            cross(tv, e1, qv);
            float v = dot(d, qv) * id;
            if (v < 0 || u + v > 1)
                continue;
            float t_hit = dot(e2, qv) * id;
            if (t_hit <= 0.5f || t_hit >= tmax)
                continue;
            if (tri_cut[t]) {
                float hit[3] = { frame.eye[0] + d[0] * t_hit, frame.eye[1] + d[1] * t_hit, frame.eye[2] + d[2] * t_hit };
                if (in_cut_cone(hit))
                    continue;
            }
            return true;
        }
    }
    return false;
}

/* Bring flags, BVH and (within the budget, or all of it) occlusion up to date. */
void refresh(const pc_overlay_view* v, float budget_ms)
{
    build_geometry();
    setup_frame(v);
    update_flags();
    uint64_t bvh_key = shown_hash ^ ((uint64_t)geo.serial << 32);
    if (bvh_key != bvh_hash) {
        bvh_hash = bvh_key;
        build_bvh();
    }
    /* The view: eye, matrices, cut. */
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](const void* data, size_t size) {
        const uint8_t* b = (const uint8_t*)data;
        for (size_t i = 0; i < size; i++)
            key = (key ^ b[i]) * 1099511628211ull;
    };
    mix(frame.eye, sizeof frame.eye);
    mix(frame.clip, sizeof frame.clip);
    mix(&shown_hash, sizeof shown_hash);
    mix(&frame.cut, sizeof frame.cut);
    if (frame.cut) {
        mix(frame.focus, sizeof frame.focus);
        mix(&frame.radius, sizeof frame.radius);
    }
    if (key != view_key) {
        view_key = key;
        remaining = geo.samples;
    }
    Clock::time_point start = Clock::now();
    int done = 0;
    while (remaining > 0 && geo.samples > 0) {
        if (cursor >= geo.samples)
            cursor = 0;
        occluded[cursor] = blocked(&sample_pos[cursor * 3]);
        ++cursor;
        --remaining;
        if (++done % 128 == 0 && budget_ms > 0 && ms_since(start) > budget_ms)
            break;
    }
    stats.occlusion_ms = ms_since(start);
    stats.pending_samples = remaining;
}

ImU32 with_alpha(ImU32 c, float a)
{
    int alpha = (int)(((c >> IM_COL32_A_SHIFT) & 0xff) * (a < 0 ? 0 : a > 1 ? 1 : a));
    return (c & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
}
ImU32 lerp_color(ImU32 a, ImU32 b, float t)
{
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t,
        x.w + (y.w - x.w) * t));
}
const ImU32 wire_color = IM_COL32(150, 190, 255, 150), point_color = IM_COL32(255, 214, 110, 235),
            actor_color = IM_COL32(120, 255, 170, 110), hot_color = IM_COL32(255, 255, 140, 255);
float ui_scale() { return ImGui::GetStyle().FontScaleMain > 0 ? ImGui::GetStyle().FontScaleMain : 1.0f; }
} // namespace

extern "C" unsigned pc_snap_modes(int toggle_point)
{
    unsigned modes = 0;
    if ((pc_snap_point != 0) != (toggle_point != 0))
        modes |= PC_SNAP_MODE_POINT;
    if (pc_snap_edge)
        modes |= PC_SNAP_MODE_EDGE;
    if (pc_snap_edge && pc_snap_midpoint)
        modes |= PC_SNAP_MODE_MIDPOINT;
    if (pc_snap_surface)
        modes |= PC_SNAP_MODE_SURFACE;
    return modes;
}

extern "C" void pc_overlay_update(const pc_overlay_view* v) { refresh(v, 0); }

extern "C" void pc_overlay_draw(const pc_overlay_view* v, void* draw_list, int snapping)
{
    ImDrawList* draw = (ImDrawList*)draw_list;
    bool auto_show = snapping && pc_overlay_snap_show;
    bool wire = pc_overlay_wire || auto_show, points = pc_overlay_points || auto_show;
    stats.drawn_lines = stats.drawn_points = 0;
    if (!wire && !points && !pc_overlay_actor_wire && !held) {
        stats.occlusion_ms = stats.draw_ms = 0;
        return;
    }
    refresh(v, 2.5f);
    Clock::time_point start = Clock::now();
    draw->PushClipRect(ImVec2(v->rect[0], v->rect[1]), ImVec2(v->rect[0] + v->rect[2], v->rect[1] + v->rect[3]), true);
    float s = ui_scale(), hidden = pc_overlay_occluded;
    /* Snap drags with the overlays off show them a little fainter. */
    float strength = (pc_overlay_wire || pc_overlay_points) ? 1.0f : 0.7f;
    int edges = (int)geo.edges.size() / 2, point_count = (int)geo.points.size() / 3;
    if (wire) {
        ImU32 seen = with_alpha(wire_color, strength), unseen = with_alpha(wire_color, strength * hidden);
        for (int e = 0; e < edges; e++) {
            uint8_t f = edge_flags[e];
            if (!(f & 1))
                continue;
            const float *a = &geo.points[geo.edges[e * 2] * 3], *b = &geo.points[geo.edges[e * 2 + 1] * 3];
            int pieces = geo.edge_pieces[e], first = geo.edge_sample[e];
            bool cut_here = frame.cut && !(f & 4);
            /* Runs of pieces in the same state make one line each. */
            int k = 0;
            while (k < pieces) {
                int state = cut_here && in_cut_cone(&sample_pos[(first + k) * 3]) ? 3 : (!(f & 2) || occluded[first + k] == 1);
                int end = k + 1;
                while (end < pieces) {
                    int next = cut_here && in_cut_cone(&sample_pos[(first + end) * 3]) ? 3
                                                                                      : (!(f & 2) || occluded[first + end] == 1);
                    if (next != state)
                        break;
                    ++end;
                }
                if (state != 3) {
                    float pa[3], pb[3], t0 = (float)k / pieces, t1 = (float)end / pieces;
                    for (int c = 0; c < 3; c++)
                        pa[c] = a[c] + (b[c] - a[c]) * t0, pb[c] = a[c] + (b[c] - a[c]) * t1;
                    ImVec2 sa, sb;
                    if (project_segment(pa, pb, sa, sb)) {
                        draw->AddLine(sa, sb, state ? unseen : seen, 1.0f);
                        ++stats.drawn_lines;
                    }
                }
                k = end;
            }
        }
    }
    if (pc_overlay_actor_wire && pc_hw_casters) {
        int count, serial;
        const float* p = pc_hw_casters(&count, &serial);
        stats.character_triangles = count;
        for (int t = 0; t < count; t++)
            for (int j = 0; j < 3; j++) {
                ImVec2 sa, sb;
                if (project_segment(p + t * 9 + j * 3, p + t * 9 + ((j + 1) % 3) * 3, sa, sb)) {
                    draw->AddLine(sa, sb, actor_color, 1.0f);
                    ++stats.drawn_lines;
                }
            }
    }
    if (points) {
        float big = 1.6f * s, small = 1.1f * s;
        ImU32 seen = with_alpha(point_color, strength), unseen = with_alpha(point_color, strength * hidden);
        for (int p = 0; p < point_count; p++) {
            if (!point_drawn(p))
                continue;
            ImVec2 q;
            if (!project(&geo.points[p * 3], q))
                continue;
            bool hide = !(point_flags[p] & 2) || occluded[p] == 1;
            float r = hide ? small : big;
            draw->AddRectFilled(ImVec2(q.x - r, q.y - r), ImVec2(q.x + r, q.y + r), hide ? unseen : seen);
            ++stats.drawn_points;
        }
    }
    draw->PopClipRect();
    if (held)
        pc_overlay_draw_snap(v, draw, &held_result);
    stats.draw_ms = ms_since(start);
}

extern "C" int pc_overlay_neighbours(const float position[3], float radius, float (*out)[4], int max)
{
    int n = 0, count = (int)geo.points.size() / 3;
    if (radius <= 0 || (int)point_flags.size() != count)
        return 0;
    struct Near {
        float d;
        int p;
    };
    std::vector<Near> found;
    for (int p = 0; p < count; p++) {
        float d[3];
        sub(&geo.points[p * 3], position, d);
        float l = length(d);
        if (l < radius && point_drawn(p))
            found.push_back({ l, p });
    }
    std::sort(found.begin(), found.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
    for (const Near& f : found) {
        if (n == max)
            break;
        float w = 1 - f.d / radius;
        memcpy(out[n], &geo.points[f.p * 3], 3 * sizeof(float));
        out[n][3] = w * w;
        ++n;
    }
    return n;
}

extern "C" int pc_overlay_snap(const pc_overlay_view* v, float x, float y, unsigned modes, pc_overlay_pick pick,
    pc_snap_result* out)
{
    memset(out, 0, sizeof *out);
    out->index = -1;
    if (modes & (PC_SNAP_MODE_POINT | PC_SNAP_MODE_EDGE | PC_SNAP_MODE_MIDPOINT))
        refresh(v, 2.5f);
    float radius = pc_snap_radius * ui_scale();
    /* Candidates: visible ones first, then nearest the cursor; within 2
     * pixels of each other, nearest the camera. */
    struct Best {
        bool found = false;
        int hidden = 0;
        float d = 0, depth = 0;
        bool better(int h, float dd, float z) const
        {
            if (!found)
                return true;
            if (h != hidden)
                return h < hidden;
            if (fabsf(dd - d) <= 2)
                return z < depth;
            return dd < d;
        }
    };
    auto finish = [&](int type, const float p[3], ImVec2 q, float d, float z, int index, int hidden) {
        out->type = type;
        memcpy(out->position, p, sizeof out->position);
        out->screen[0] = q.x, out->screen[1] = q.y;
        out->distance = d;
        out->depth = z;
        out->index = index;
        out->occluded = hidden;
        return type;
    };
    int point_count = (int)geo.points.size() / 3, edges = (int)geo.edges.size() / 2;
    if (modes & PC_SNAP_MODE_POINT) {
        Best best;
        int pick_point = -1;
        ImVec2 at;
        for (int p = 0; p < point_count; p++) {
            ImVec2 q;
            float z;
            if (!point_drawn(p) || !project(&geo.points[p * 3], q, &z))
                continue;
            float d = hypotf(q.x - x, q.y - y);
            if (d > radius)
                continue;
            int hidden = !(point_flags[p] & 2) || occluded[p] == 1;
            if (best.better(hidden, d, z)) {
                best.found = true, best.hidden = hidden, best.d = d, best.depth = z;
                pick_point = p, at = q;
            }
        }
        if (pick_point >= 0)
            return finish(PC_SNAP_POINT, &geo.points[pick_point * 3], at, best.d, best.depth, pick_point, best.hidden);
    }
    auto edge_hidden = [&](int e, float t) {
        int k = (int)(t * geo.edge_pieces[e]);
        k = k < 0 ? 0 : k >= geo.edge_pieces[e] ? geo.edge_pieces[e] - 1 : k;
        return (int)(!(edge_flags[e] & 2) || occluded[geo.edge_sample[e] + k] == 1);
    };
    auto edge_cut = [&](int e, const float p[3]) { return frame.cut && !(edge_flags[e] & 4) && in_cut_cone(p); };
    if (modes & PC_SNAP_MODE_MIDPOINT) {
        Best best;
        int pick_edge = -1;
        float mid[3];
        ImVec2 at;
        for (int e = 0; e < edges; e++) {
            if (!(edge_flags[e] & 1))
                continue;
            const float *a = &geo.points[geo.edges[e * 2] * 3], *b = &geo.points[geo.edges[e * 2 + 1] * 3];
            float m[3] = { (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 };
            ImVec2 q;
            float z;
            if (edge_cut(e, m) || !project(m, q, &z))
                continue;
            float d = hypotf(q.x - x, q.y - y);
            if (d > radius)
                continue;
            int hidden = edge_hidden(e, 0.5f);
            if (best.better(hidden, d, z)) {
                best.found = true, best.hidden = hidden, best.d = d, best.depth = z;
                pick_edge = e, at = q;
                memcpy(mid, m, sizeof mid);
            }
        }
        if (pick_edge >= 0)
            return finish(PC_SNAP_MIDPOINT, mid, at, best.d, best.depth, pick_edge, best.hidden);
    }
    if (modes & PC_SNAP_MODE_EDGE) {
        Best best;
        int pick_edge = -1;
        float on[3];
        ImVec2 at;
        for (int e = 0; e < edges; e++) {
            if (!(edge_flags[e] & 1))
                continue;
            const float *a = &geo.points[geo.edges[e * 2] * 3], *b = &geo.points[geo.edges[e * 2 + 1] * 3];
            ImVec2 sa, sb;
            float t0, t1, wa, wb;
            if (!project_segment(a, b, sa, sb, &t0, &t1, &wa, &wb))
                continue;
            float dx = sb.x - sa.x, dy = sb.y - sa.y, ll = dx * dx + dy * dy;
            float s = ll > 1e-6f ? ((x - sa.x) * dx + (y - sa.y) * dy) / ll : 0;
            s = s < 0 ? 0 : s > 1 ? 1 : s;
            ImVec2 q(sa.x + dx * s, sa.y + dy * s);
            float d = hypotf(q.x - x, q.y - y);
            if (d > radius)
                continue;
            /* Screen parameter to world (perspective-correct). */
            float u = (s / wb) / ((1 - s) / wa + s / wb);
            float t = t0 + (t1 - t0) * u, p[3];
            for (int c = 0; c < 3; c++)
                p[c] = a[c] + (b[c] - a[c]) * t;
            if (edge_cut(e, p))
                continue;
            float z = wa + (wb - wa) * u;
            int hidden = edge_hidden(e, t);
            if (best.better(hidden, d, z)) {
                best.found = true, best.hidden = hidden, best.d = d, best.depth = z;
                pick_edge = e, at = q;
                memcpy(on, p, sizeof on);
            }
        }
        if (pick_edge >= 0)
            return finish(PC_SNAP_EDGE, on, at, best.d, best.depth, pick_edge, best.hidden);
    }
    if ((modes & PC_SNAP_MODE_SURFACE) && pick) {
        float world[3], normal[3];
        if (pick(x, y, world, normal)) {
            float p[3];
            for (int c = 0; c < 3; c++)
                p[c] = world[c] + normal[c] * pc_snap_offset;
            memcpy(out->surface, world, sizeof out->surface);
            memcpy(out->normal, normal, sizeof out->normal);
            setup_frame(v);
            ImVec2 q(x, y);
            float z = 0;
            project(world, q, &z);
            return finish(PC_SNAP_SURFACE, p, q, hypotf(q.x - x, q.y - y), z, -1, 0);
        }
    }
    return PC_SNAP_NONE;
}

extern "C" void pc_overlay_draw_snap(const pc_overlay_view* v, void* draw_list, const pc_snap_result* r)
{
    if (!r || r->type == PC_SNAP_NONE)
        return;
    ImDrawList* draw = (ImDrawList*)draw_list;
    setup_frame(v);
    float s = ui_scale();
    draw->PushClipRect(ImVec2(v->rect[0], v->rect[1]), ImVec2(v->rect[0] + v->rect[2], v->rect[1] + v->rect[3]), true);
    if (r->type != PC_SNAP_SURFACE) {
        /* Neighbours light up with a falloff, edges between them too. */
        float radius = pc_snap_highlight > 0 ? pc_snap_highlight : 128;
        int count = (int)geo.points.size() / 3;
        std::vector<float> weight(count, 0.0f);
        bool any = false;
        if ((int)point_flags.size() == count)
            for (int p = 0; p < count; p++) {
                float d[3];
                sub(&geo.points[p * 3], r->position, d);
                float l = length(d);
                if (l < radius && point_drawn(p)) {
                    float w = 1 - l / radius;
                    weight[p] = w * w;
                    any = true;
                }
            }
        /* Edges glow where they pass within the radius, piece by piece. */
        int edges = (int)edge_flags.size();
        for (int e = 0; e < edges; e++) {
            if (!(edge_flags[e] & 1))
                continue;
            const float *a = &geo.points[geo.edges[e * 2] * 3], *b = &geo.points[geo.edges[e * 2 + 1] * 3];
            float ab[3], ap[3];
            sub(b, a, ab);
            sub(r->position, a, ap);
            float ll = dot(ab, ab), t = ll > 1e-6f ? dot(ap, ab) / ll : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float closest[3] = { a[0] + ab[0] * t - r->position[0], a[1] + ab[1] * t - r->position[1],
                a[2] + ab[2] * t - r->position[2] };
            if (length(closest) >= radius)
                continue;
            const int pieces = 8;
            for (int k = 0; k < pieces; k++) {
                float p0[3], p1[3], mid[3];
                for (int c = 0; c < 3; c++) {
                    p0[c] = a[c] + ab[c] * k / pieces;
                    p1[c] = a[c] + ab[c] * (k + 1) / pieces;
                    mid[c] = (p0[c] + p1[c]) * 0.5f - r->position[c];
                }
                float w = 1 - length(mid) / radius;
                if (w <= 0)
                    continue;
                w *= w;
                int sample = geo.edge_sample[e] + (k * geo.edge_pieces[e]) / pieces;
                bool hide = !(edge_flags[e] & 2) || occluded[sample] == 1;
                ImVec2 sa, sb;
                if (project_segment(p0, p1, sa, sb))
                    draw->AddLine(sa, sb, with_alpha(lerp_color(wire_color, hot_color, w), (hide ? 0.3f : 0.45f) + 0.55f * w),
                        1.0f + 1.5f * w * s);
            }
        }
        if (any) {
            for (int p = 0; p < count; p++) {
                if (weight[p] <= 0)
                    continue;
                ImVec2 q;
                if (!project(&geo.points[p * 3], q))
                    continue;
                float w = weight[p], rad = (1.8f + 2.6f * w) * s;
                bool hide = !(point_flags[p] & 2) || occluded[p] == 1;
                ImU32 c = with_alpha(lerp_color(point_color, hot_color, w), (hide ? 0.45f : 0.7f) + 0.3f * w);
                draw->AddRectFilled(ImVec2(q.x - rad, q.y - rad), ImVec2(q.x + rad, q.y + rad), c);
            }
        }
        if (r->type == PC_SNAP_EDGE || r->type == PC_SNAP_MIDPOINT) {
            int e = r->index;
            if (e >= 0 && e < (int)geo.edges.size() / 2) {
                ImVec2 sa, sb;
                if (project_segment(&geo.points[geo.edges[e * 2] * 3], &geo.points[geo.edges[e * 2 + 1] * 3], sa, sb))
                    draw->AddLine(sa, sb, hot_color, 2.5f * s);
            }
        }
    }
    ImVec2 q;
    if (!project(r->type == PC_SNAP_SURFACE ? r->surface : r->position, q))
        q = ImVec2(r->screen[0], r->screen[1]);
    float ring = 7 * s;
    ImU32 target = r->occluded ? IM_COL32(255, 170, 60, 255) : IM_COL32(255, 255, 60, 255);
    if (r->type == PC_SNAP_SURFACE) {
        ImVec2 p;
        if (project(r->position, p)) /* the offset along the normal */
            draw->AddLine(q, p, target, 1.5f * s);
        draw->AddCircle(q, ring, target, 20, 2 * s);
    } else if (r->type == PC_SNAP_EDGE) {
        draw->AddCircle(q, ring, target, 4, 2 * s);
    } else if (r->type == PC_SNAP_MIDPOINT) {
        ImVec2 tri[3] = { ImVec2(q.x, q.y - ring), ImVec2(q.x + ring * 0.87f, q.y + ring * 0.5f),
            ImVec2(q.x - ring * 0.87f, q.y + ring * 0.5f) };
        draw->AddPolyline(tri, 3, target, ImDrawFlags_Closed, 2 * s);
    } else {
        draw->AddRect(ImVec2(q.x - ring, q.y - ring), ImVec2(q.x + ring, q.y + ring), target, 0, 0, 2 * s);
    }
    draw->AddRectFilled(ImVec2(q.x - 2.5f * s, q.y - 2.5f * s), ImVec2(q.x + 2.5f * s, q.y + 2.5f * s), IM_COL32_WHITE);
    /* Readout. */
    static const char* names[] = { "", "Point", "Edge midpoint", "Edge", "Surface" };
    char text[96];
    snprintf(text, sizeof text, "%s%s  %.0f  %.0f  %.0f", names[r->type], r->occluded ? " (hidden)" : "", r->position[0],
        r->position[1], r->position[2]);
    ImVec2 size = ImGui::CalcTextSize(text), at(q.x + ring + 6 * s, q.y + ring);
    if (at.x + size.x + 8 * s > v->rect[0] + v->rect[2])
        at.x = q.x - ring - 6 * s - size.x - 8 * s;
    draw->AddRectFilled(ImVec2(at.x - 4 * s, at.y - 2 * s), ImVec2(at.x + size.x + 4 * s, at.y + size.y + 2 * s),
        IM_COL32(10, 12, 18, 210), 3 * s);
    draw->AddText(at, IM_COL32(255, 245, 200, 255), text);
    draw->PopClipRect();
}

extern "C" void pc_overlay_hold_snap(const pc_snap_result* r)
{
    held = r && r->type != PC_SNAP_NONE;
    if (held)
        held_result = *r;
}

extern "C" void pc_overlay_get_stats(pc_overlay_stats* out)
{
    stats.points = (int)geo.points.size() / 3;
    stats.edges = (int)geo.edges.size() / 2;
    stats.shown_points = stats.shown_edges = stats.visible_samples = stats.occluded_samples = 0;
    for (size_t p = 0; p < point_flags.size(); p++)
        stats.shown_points += point_drawn((int)p);
    for (uint8_t f : edge_flags)
        stats.shown_edges += f & 1;
    for (uint8_t o : occluded) {
        stats.visible_samples += o == 0;
        stats.occluded_samples += o == 1;
    }
    *out = stats;
}

extern "C" int pc_overlay_sample_points(const pc_overlay_view* v, float (*world)[3], float (*screen)[2], int* hidden, int max)
{
    refresh(v, 0);
    int n = 0, count = (int)geo.points.size() / 3;
    for (int p = 0; p < count && n < max; p++) {
        ImVec2 q;
        if (!point_drawn(p) || !project(&geo.points[p * 3], q))
            continue;
        if (q.x < v->rect[0] || q.y < v->rect[1] || q.x >= v->rect[0] + v->rect[2] || q.y >= v->rect[1] + v->rect[3])
            continue;
        memcpy(world[n], &geo.points[p * 3], 3 * sizeof(float));
        screen[n][0] = q.x, screen[n][1] = q.y;
        hidden[n] = !(point_flags[p] & 2) || occluded[p] == 1;
        ++n;
    }
    return n;
}

extern "C" int pc_overlay_sample_edges(const pc_overlay_view* v, float (*world)[6], float (*screen)[4], int* hidden, int max)
{
    refresh(v, 0);
    int n = 0, edges = (int)geo.edges.size() / 2;
    for (int e = 0; e < edges && n < max; e++) {
        if (!(edge_flags[e] & 1))
            continue;
        const float *a = &geo.points[geo.edges[e * 2] * 3], *b = &geo.points[geo.edges[e * 2 + 1] * 3];
        ImVec2 sa, sb;
        float t0, t1;
        if (!project_segment(a, b, sa, sb, &t0, &t1) || t0 != 0 || t1 != 1)
            continue;
        bool inside = true;
        for (const ImVec2& q : { sa, sb })
            inside &= q.x >= v->rect[0] && q.y >= v->rect[1] && q.x < v->rect[0] + v->rect[2] && q.y < v->rect[1] + v->rect[3];
        if (!inside)
            continue;
        memcpy(world[n], a, 3 * sizeof(float));
        memcpy(world[n] + 3, b, 3 * sizeof(float));
        screen[n][0] = sa.x, screen[n][1] = sa.y, screen[n][2] = sb.x, screen[n][3] = sb.y;
        int h = 0;
        for (int k = 0; k < geo.edge_pieces[e]; k++)
            h |= !(edge_flags[e] & 2) || occluded[geo.edge_sample[e] + k] == 1;
        hidden[n] = h;
        ++n;
    }
    return n;
}
