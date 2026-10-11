// Ray casting for the probe GI without ray tracing hardware (Vulkan
// compute, OpenGL 4.3): ptgi.c's BVH (binned SAH, leaves of up to four
// triangles in leaf order; node lo.w = first child or triangle, hi.w =
// triangle count, 0 for an interior node) over the room (and, on OpenGL,
// this frame's casters), with rt_common.glsl's interface: tri_at, trace,
// surface_texel, vertex_rgb, rand. Bindings: 2 triangles, 10 nodes, 3 VRAM;
// the roots come from the probe volume (pv_bvh).
struct Tri { vec4 a, b, c; uvec4 info; };
struct Node { vec4 lo, hi; };
layout(std430, binding = 2) readonly buffer Tris { Tri tris[]; };
layout(std430, binding = 10) readonly buffer Nodes { Node nodes[]; };
layout(binding = 3) uniform usampler2D vram;
const int MOVER_BIT = 1 << 23; // no movers here (rt_common.glsl's split)
Tri tri_at(int i) { return tris[i]; }
uint rng = 0u;
float rand() {
    rng = rng * 747796405u + 2891336453u;
    uint w = ((rng >> ((rng >> 28u) + 4u)) ^ rng) * 277803737u;
    return float((w >> 22u) ^ w) / 4294967296.0;
}
uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }
uint texel_at(uint u, uint v, uint tpage, uint clut) {
    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;
    int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);
    uint depth = (tpage >> 7) & 3u;
    if (depth == 0u) return fetch(cx + int((fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u), cy);
    if (depth == 1u) return fetch(cx + int((fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u), cy);
    return fetch(tx + int(u), ty + int(v));
}
uint surface_texel(Tri t, vec2 bc) {
    vec2 uv0 = vec2(t.info.x & 255u, (t.info.x >> 8) & 255u);
    vec2 uv1 = vec2((t.info.x >> 16) & 255u, t.info.x >> 24);
    vec2 uv2 = vec2(t.info.y & 255u, (t.info.y >> 8) & 255u);
    vec2 uv = uv0 * (1.0 - bc.x - bc.y) + uv1 * bc.x + uv2 * bc.y;
    return texel_at(uint(uv.x) & 255u, uint(uv.y) & 255u, t.info.y >> 16, t.info.z & 0xffffu);
}
vec3 vertex_rgb(float w) { uint c = floatBitsToUint(w); return vec3(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u) / 255.0; }
// Nearest hit (or any hit) along o + d t, t < t_max; clear and translucent
// texels let rays through; layers within skip_t nearly parallel to skip_n
// are ignored (decals).
bool trace(vec3 o, vec3 d, float t_max, bool any_hit, vec3 skip_n, float skip_t, out float t_hit, out int hit,
    out vec2 bc_hit) {
    vec3 inv = 1.0 / d;
    int stack[32]; int sp = 0;
    if (pv_bvh.x >= 0) stack[sp++] = pv_bvh.x;
    if (pv_bvh.y >= 0) stack[sp++] = pv_bvh.y;
    t_hit = t_max; hit = -1; bc_hit = vec2(0.0);
    while (sp > 0) {
        Node n = nodes[stack[--sp]];
        vec3 t0 = (n.lo.xyz - o) * inv, t1 = (n.hi.xyz - o) * inv;
        vec3 tn = min(t0, t1), tf = max(t0, t1);
        float enter = max(max(tn.x, tn.y), max(tn.z, 0.0)), leave = min(min(tf.x, tf.y), min(tf.z, t_hit));
        if (enter > leave) continue;
        int first = floatBitsToInt(n.lo.w), count = floatBitsToInt(n.hi.w);
        if (count == 0) { // the nearer child is visited first
            if (sp < 31) {
                Node l = nodes[first], r = nodes[first + 1];
                float dl = dot(0.5 * (l.lo.xyz + l.hi.xyz) - o, d), dr = dot(0.5 * (r.lo.xyz + r.hi.xyz) - o, d);
                if (dl < dr) { stack[sp++] = first + 1; stack[sp++] = first; }
                else { stack[sp++] = first; stack[sp++] = first + 1; }
            }
            continue;
        }
        for (int i = first; i < first + count; i++) {
            Tri tri = tris[i];
            vec3 e1 = tri.b.xyz - tri.a.xyz, e2 = tri.c.xyz - tri.a.xyz, p = cross(d, e2);
            float det = dot(e1, p);
            if (abs(det) < 1e-9) continue;
            float f = 1.0 / det; vec3 s = o - tri.a.xyz;
            float u = f * dot(s, p); if (u < 0.0 || u > 1.0) continue;
            vec3 q = cross(s, e1); float v = f * dot(d, q); if (v < 0.0 || u + v > 1.0) continue;
            float t = f * dot(e2, q); if (t <= 0.0 || t >= t_hit) continue;
            if (t < skip_t && abs(dot(normalize(cross(e1, e2)), skip_n)) > 0.98) continue;
            if ((tri.info.w & 1u) != 0u) { // the PS1's per-texel rule
                uint c = surface_texel(tri, vec2(u, v));
                if (c == 0u || ((tri.info.w & 4u) != 0u && (c & 0x8000u) != 0u)) continue;
            }
            t_hit = t; hit = i; bc_hit = vec2(u, v);
            if (any_hit) return true;
        }
    }
    return hit >= 0;
}
