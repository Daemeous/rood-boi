#version 450
#extension GL_GOOGLE_include_directive : require
// Native lists (render.c's geometry stage): the PS1's per-polygon rules on
// real view-space positions, then the polygon's triangles projected as the
// GTE projects, with real clipping and exact per-pixel depth.
layout(lines_adjacency) in;
layout(triangle_strip, max_vertices = 6) out;
#include "native_common.glsl"
layout(location = 0) in vec3 g_pos[];
layout(location = 1) in vec3 g_normal[];
layout(location = 2) flat in uvec4 g_color[];
layout(location = 3) flat in uvec4 g_uvf[];
layout(location = 4) flat in uvec2 g_page[];
layout(location = 5) flat in uint g_bias[];
layout(location = 6) flat in uint g_effect[];
layout(location = 0) noperspective centroid out vec3 v_color_affine;
layout(location = 1) noperspective centroid out vec2 v_uv_affine;
layout(location = 2) smooth centroid out vec3 v_color_persp;
layout(location = 3) smooth centroid out vec2 v_uv_persp;
layout(location = 4) smooth centroid out float v_z;
layout(location = 5) flat out uvec4 v_state; // flags, tpage, clut, 0
layout(location = 6) flat out vec3 v_normal;
layout(location = 7) flat out uvec4 v_bounds;
layout(location = 8) smooth centroid out vec3 v_view; // view space, rows' scales divided out (cutaway)
layout(location = 9) flat out int v_cut; // bit 0: the polygon stands between the eye and the cutaway focus;
                                         // bits 1-3: the triangle's real edges (opposite corner 0, 1, 2)
layout(location = 10) noperspective out vec3 v_bary; // the corner weights (a cutaway ghost's edges)
layout(location = 11) flat out uint v_order; // room polygons: PS1 draw order key (bucket << 10 | sequence; smaller paints later)
vec3 p[4];
vec2 uv[4];
vec3 col[4];
vec2 s[4];
uint tpage, clut, pcode;
uint effect = 0u, halved = 0u;
vec3 n;
float z_near = 0.0; // sprites: tested and written at their nearest point
int cut = 0;
int tri_edges = 7, corner = 0; // a quad's diagonal is no edge
uint order_key = 0u;
vec2 screen(vec3 q) { return vec2(u.offset.x + u.projection.x * q.x / q.z, u.offset.y + u.projection.y * q.y / q.z); }
float nclip(vec2 a, vec2 b, vec2 c) { return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y); }
void emit(int i) {
    vec3 q = p[i];
    // Vulkan clip depth: 0 at the near plane, w at the far plane (the
    // fragment stage writes the real depth).
    gl_Position = vec4((u.projection.z * q.z + u.projection.x * q.x) / 512.0 - q.z,
                       (u.projection.w * q.z + u.projection.y * q.y) / 256.0 - q.z,
                       u.clip_z.y * (q.z - u.clip_z.x) / (u.clip_z.y - u.clip_z.x), q.z);
    v_color_affine = v_color_persp = col[i] / 255.0;
    v_uv_affine = v_uv_persp = uv[i];
    v_z = q.z - z_near;
    v_state = uvec4(pcode & 7u, tpage, clut, effect | (halved << 8));
    v_normal = n;
    v_view = q * u.cut_scale.xyz;
    v_cut = cut | (tri_edges << 1);
    v_bary = vec3(corner == 0 ? 1.0 : 0.0, corner == 1 ? 1.0 : 0.0, corner == 2 ? 1.0 : 0.0);
    v_order = order_key;
    EmitVertex();
}
void triangle(int a, int b, int c) {
    vec2 lo = min(min(uv[a], uv[b]), uv[c]), hi = max(max(uv[a], uv[b]), uv[c]);
    v_bounds = uvec4(clamp(lo, 0.0, 255.0), clamp(hi, 0.0, 255.0));
    corner = 0; emit(a); corner = 1; emit(b); corner = 2; emit(c);
    EndPrimitive();
}
// Facing of triangle a, b, c: NCLIP on the GTE's whole-pixel screen
// positions; behind the camera, the sign of det(p0, p1, p2).
float facing(int a, int b, int c, float zmin) {
    return zmin > 0.0 ? nclip(s[a], s[b], s[c]) : dot(cross(p[b] - p[a], p[c] - p[a]), p[a]);
}
// The editor's cutaway (room lists, free camera): a polygon whose plane
// separates the eye from the focus stands between them; the fragment
// stage cuts it inside the cone from the eye to the focus. Floors
// under the focus and walls behind it never separate the two.
void cutaway_test(bool quad) {
    if (u.cut_focus.w <= 0.0) return;
    vec3 k = u.cut_scale.xyz;
    vec3 a = p[0] * k, b = p[1] * k, c = p[2] * k;
    vec3 pn = cross(b - a, c - a);
    if (quad && dot(pn, pn) < 1e-3) pn = cross(p[3] * k - b, c - b);
    float l = length(pn);
    if (l < 1e-6) return;
    pn /= l;
    float eye = dot(pn, -a), focus = dot(pn, u.cut_focus.xyz - a);
    if (eye * focus < 0.0 && abs(focus) > 8.0) cut = 1;
}
int sz(int i) { return int(clamp(p[i].z, 0.0, 65535.0)); }
// Object rules (BATTLE/4B16C.s func_800B3CF0/func_800B4594).
void object_triangle(int a, int b, int c, float zmin, int bias) {
    if (facing(a, b, c, zmin) <= 0.0) return;
    if (((u.zsf3 * (sz(a) + sz(b) + sz(c))) >> 12) + bias >= u.rules.y) return;
    triangle(a, b, c);
}
void objects_main(uint flags, bool quad, bool twosided) {
    if (u.override_color.w != 0) for (int i = 0; i < 4; i++) col[i] = vec3(u.override_color.rgb);
    float zmin = min(min(p[0].z, p[1].z), min(p[2].z, quad ? p[3].z : p[2].z));
    for (int i = 0; i < 4; i++) s[i] = zmin > 0.0 ? floor(screen(p[i])) : vec2(0.0);
    // Normal: Newell's over the unrounded view positions (0, 1, 3, 2).
    vec3 q[4] = vec3[4](g_pos[0], g_pos[1], quad ? g_pos[3] : g_pos[2], g_pos[2]);
    int count = quad ? 4 : 3;
    n = vec3(0.0);
    for (int i = 0; i < count; i++) {
        vec3 a = q[i], b = q[(i + 1) % count];
        n += vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    }
    float length_ = length(n);
    n = length_ > 1e-6 ? n / length_ : vec3(0.0, 0.0, -1.0);
    if (dot(n, p[0]) > 0.0) n = -n;
    int bias = int(g_bias[0]);
    if ((flags & 4u) != 0u) {
        // The PS1 picks the diagonal from the GTE's OP of the corners' screen
        // positions and depths: (v3 - v0) . ((v2 - v0) x (v1 - v0)).
        vec3 c0 = vec3(s[0], float(sz(0)));
        float d = dot(vec3(s[3], float(sz(3))) - c0, cross(vec3(s[2], float(sz(2))) - c0, vec3(s[1], float(sz(1))) - c0));
        if (d < 0.0) { object_triangle(0, 1, 2, zmin, bias); object_triangle(1, 3, 2, zmin, bias); }
        else { object_triangle(0, 1, 3, zmin, bias); object_triangle(0, 3, 2, zmin, bias); }
        return;
    }
    if (!twosided && facing(0, 1, 2, zmin) <= 0.0) return;
    int depth = quad ? (sz(0) + sz(1) + sz(2) + sz(3)) >> 4 : (u.zsf3 * (sz(0) + sz(1) + sz(2))) >> 12;
    if (depth + bias >= u.rules.y) return;
    triangle(0, 1, 2);
    if (quad) triangle(1, 3, 2);
}
// A screen-aligned sprite (RENDER_POLY_SPRITE): the centre projected as the
// GTE projects it, the corners at their screen offsets scaled by the depth
// they were measured at over the centre's depth, all at the centre's depth;
// tested against the surfaces at the sprite's nearest point.
void sprite_main() {
    vec3 c = floor(g_pos[0]);
    if (c.z < 1.0) return;
    vec2 sc = floor(screen(c));
    float k = g_normal[0].z > 0.0 ? g_normal[0].z / c.z : 1.0, r = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 d = g_normal[i].xy * k;
        r = max(r, length(d));
        d.x *= u.projection.x / u.projection.y;
        vec2 q = sc + d;
        p[i] = vec3((q.x - u.offset.x) * c.z / u.projection.x, (q.y - u.offset.y) * c.z / u.projection.y, c.z);
    }
    z_near = min(r * c.z / u.projection.y, c.z - 1.0);
    n = vec3(0.0, 0.0, -1.0);
    triangle(0, 1, 2);
    triangle(1, 3, 2);
}
void main() {
    uint code = g_color[0].a, flags = g_uvf[0].z;
    // A blended instance: textured, modulated, semi-transparent.
    if (u.instance_blend.x != 0) code = (code | 6u) & ~1u;
    // Semi-transparent polygons draw at their translucent markers.
    if (u.translucent != 2 && ((code & 2u) != 0u) != (u.translucent != 0)) return;
    pcode = code;
    bool quad = (flags & 1u) != 0u, twosided = (flags & 2u) != 0u;
    for (int i = 0; i < 4; i++) {
        p[i] = floor(g_pos[i]);
        uv[i] = vec2(g_uvf[i].xy);
        col[i] = vec3(g_color[i].rgb);
    }
    // An animated material: the offset the game's effect writes into the
    // polygon's UV bytes this frame, added per vertex with the bytes' wrap.
    effect = g_effect[0];
    if (effect != 0u && int(effect) < u.uv_count) {
        for (int i = 0; i < 4; i++) uv[i] = vec2((g_uvf[i].xy + uvec2(uv_effects[effect].offset.xy)) & 255u);
    } else effect = 0u;
    tpage = g_page[0].x;
    clut = g_page[0].y;
    // A blended instance's mode replaces the polygon's own bits.
    if (u.instance_blend.x != 0) tpage = (tpage & ~0x60u) | uint(u.instance_blend.y);
    if (u.override_texture.z != 0) { tpage |= uint(u.override_texture.x); clut = (uint(u.override_texture.y) + clut) & 0xffffu; }
    if ((flags & 8u) != 0u) { sprite_main(); return; }
    if (u.objects != 0) { objects_main(flags, quad, twosided); return; }
    // Sort depth: the largest vertex SZ (the GTE saturates to 0..65535).
    float zmax = max(max(p[0].z, p[1].z), p[2].z);
    if (quad) zmax = max(zmax, p[3].z);
    int sz = int(clamp(floor(zmax), 0.0, 65535.0));
    float zmin = min(min(p[0].z, p[1].z), min(p[2].z, quad ? p[3].z : p[2].z));
    for (int i = 0; i < 4; i++) s[i] = zmin > 0.0 ? floor(screen(p[i])) : vec2(0.0);
    // Translucent room polygons the game already kept (its back-face and
    // ordering-table tests, its UV halving decision).
    if (u.forced.x != 0) {
        if (u.forced.y != 0) {
            for (int i = 0; i < 4; i++) uv[i] = floor(uv[i] * 0.5);
            tpage = (tpage - 1u) & 0xffffu;
            halved = 1u;
        }
    } else {
        float a0 = facing(0, 1, 2, zmin);
        float a1 = !quad ? a0 : facing(3, 2, 1, zmin);
        if (!twosided && a0 < 0.0 && (!quad || a1 < 0.0)) return;
        // Ordering-table range.
        int bucket = sz >> 2;
        if (bucket < u.rules.x) return;
        bucket += u.rules.w;
        if (bucket < u.rules.x || bucket >= u.rules.y) return;
        // The PS1 paints the room back to front by bucket; within a bucket
        // the polygon linked first paints last (linking at the bucket's head).
        order_key = (uint(clamp(bucket, 0, 16383)) << 10)
            | min(uint(((u.order & 0xffffff) + gl_PrimitiveIDIn) >> (u.order >> 24)), 1023u);
        // Distance UV halving, except for polygons the PS1 subdivides.
        bool subdivided = false;
        if (u.split.x != 0 && u.split.y - (sz >> 9) > 0 && zmin > 0.0) {
            float area = a0;
            if (!quad && area < 0.0) area = -area;
            if (quad && area < 0.0 && !twosided) area = a1;
            subdivided = area < 0.0 || area >= 512.0;
        }
        if (!subdivided && bucket >= u.rules.z) {
            for (int i = 0; i < 4; i++) uv[i] = floor(uv[i] * 0.5);
            tpage = (tpage - 1u) & 0xffffu;
            halved = 1u;
        }
    }
    if (u.tint != vec4(0.0)) {
        for (int i = 0; i < 4; i++) {
            vec3 c = col[i];
            if (u.tint.w != 0.0) {
                float grey = (c.r * 5.0 + c.g * 9.0 + c.b * 2.0) / 16.0;
                c = clamp(floor(c + (grey - c) * u.tint.w / 4096.0), 0.0, 255.0);
            }
            col[i] = clamp(c + u.tint.xyz, 0.0, 255.0);
        }
    }
    // View-space normal facing the camera, as gpu_hw.c reconstructs it.
    n = u.normal_matrix * g_normal[0];
    float length_ = length(n);
    n = length_ > 1e-6 ? n / length_ : vec3(0.0, 0.0, -1.0);
    if (dot(n, p[0]) > 0.0) n = -n;
    cutaway_test(quad);
    tri_edges = quad ? 6 : 7;
    triangle(0, 1, 2);
    tri_edges = 5;
    if (quad) triangle(1, 3, 2);
}
