// Scene and ray queries for the Vulkan path tracer (vk_rt.inc), the
// counterpart of ptgi.c's BVH traversal. TLAS instances: the room's opaque
// triangles (forced opaque: no shader work), its cut-outs (mask 1) and one
// per mover (characters, weapons, props: mask 2, custom index MOVER_BIT |
// first triangle in the mover buffer). Cut-outs and movers run the PS1's
// per-texel rule (clear and translucent texels let rays through) and the
// decal skip on each candidate.
// (The including shader enables GL_EXT_ray_query.)
struct Tri { vec4 a, b, c; uvec4 info; };
layout(binding = 1) uniform accelerationStructureEXT tlas;
layout(std430, binding = 2) readonly buffer Tris { Tri tris[]; };
layout(binding = 3) uniform usampler2D vram;
#ifndef MOVERS_BINDING
#define MOVERS_BINDING 9
#endif
layout(std430, binding = MOVERS_BINDING) readonly buffer Movers { Tri movers[]; };
const int MOVER_BIT = 1 << 23;
Tri tri_at(int i) { return i >= MOVER_BIT ? movers[i - MOVER_BIT] : tris[i]; }
uint rng = 0u;
bool moving_only = false;
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
// The surface texel at barycentric (u, v); 0 = transparent.
uint surface_texel(Tri t, vec2 bc) {
    vec2 uv0 = vec2(t.info.x & 255u, (t.info.x >> 8) & 255u);
    vec2 uv1 = vec2((t.info.x >> 16) & 255u, t.info.x >> 24);
    vec2 uv2 = vec2(t.info.y & 255u, (t.info.y >> 8) & 255u);
    vec2 uv = uv0 * (1.0 - bc.x - bc.y) + uv1 * bc.x + uv2 * bc.y;
    return texel_at(uint(uv.x) & 255u, uint(uv.y) & 255u, t.info.y >> 16, t.info.z & 0xffffu);
}
vec3 vertex_rgb(float w) { uint c = floatBitsToUint(w); return vec3(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u) / 255.0; }
// Nearest hit (or any hit, for shadows) along o + d t, t < t_max; layers
// within skip_t nearly parallel to skip_n are ignored (the game floats
// decals a few units off walls and floors): candidates of cut-outs and
// movers are skipped in the query, an opaque one restarts the query past it.
bool skipped(Tri tri, float t, vec3 skip_n, float skip_t) {
    return t < skip_t && abs(dot(normalize(cross(tri.b.xyz - tri.a.xyz, tri.c.xyz - tri.a.xyz)), skip_n)) > 0.98;
}
bool trace(vec3 o, vec3 d, float t_max, bool any_hit, vec3 skip_n, float skip_t, out float t_hit, out int hit,
    out vec2 bc_hit) {
    t_hit = t_max; hit = -1; bc_hit = vec2(0.0);
    if (!(t_max > 0.0)) return false;
    float t_min = 0.0;
    for (int pass = 0; pass < 4; pass++) {
        rayQueryEXT q;
        rayQueryInitializeEXT(q, tlas, any_hit ? gl_RayFlagsTerminateOnFirstHitEXT : gl_RayFlagsNoneEXT,
            moving_only ? 2u : 0xffu, o, t_min, d, t_max);
        while (rayQueryProceedEXT(q)) {
            if (rayQueryGetIntersectionTypeEXT(q, false) != gl_RayQueryCandidateIntersectionTriangleEXT) continue;
            int i = rayQueryGetIntersectionInstanceCustomIndexEXT(q, false) + rayQueryGetIntersectionPrimitiveIndexEXT(q, false);
            float t = rayQueryGetIntersectionTEXT(q, false);
            if (t <= 0.0) continue;
            Tri tri = tri_at(i);
            if (skipped(tri, t, skip_n, skip_t)) continue;
            if ((tri.info.w & 1u) != 0u) { // the PS1's per-texel rule
                uint c = surface_texel(tri, rayQueryGetIntersectionBarycentricsEXT(q, false));
                if (c == 0u || ((tri.info.w & 4u) != 0u && (c & 0x8000u) != 0u)) continue;
            }
            rayQueryConfirmIntersectionEXT(q);
        }
        if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT) return false;
        float t = rayQueryGetIntersectionTEXT(q, true);
        int i = rayQueryGetIntersectionInstanceCustomIndexEXT(q, true) + rayQueryGetIntersectionPrimitiveIndexEXT(q, true);
        if (skip_t > 0.0 && skipped(tri_at(i), t, skip_n, skip_t)) { t_min = t + 0.01; continue; }
        t_hit = t; hit = i;
        bc_hit = rayQueryGetIntersectionBarycentricsEXT(q, true);
        return true;
    }
    return false;
}
