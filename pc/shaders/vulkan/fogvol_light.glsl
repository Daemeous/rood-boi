// Fog volumes' light grid (fog_volume.glsl; vk_fogvol.inc): per volume a
// G^3 grid over its box (its own turned frame) of the transmittance toward
// each of its lights (grid a: lights 0-3; grid b: lights 4, 5, the sun or
// moon, the ambient term), so the march's samples get the volume's own
// shadow (dark cores, lit rims) and, with ray queries (FOGVOL_RT), the
// scene's (walls, Ashley: a fire behind her casts her shadow into the
// smoke). Updated incrementally: half the cells a frame (a checkerboard,
// alternating), each at a point jittered in its cell and blended into what
// it held, so the grid is smooth and stable; a light that changes slot or a
// volume that changes restarts its cells.
//   with FOGVOL_RT: fogvol_light_rt.comp (rt_common.glsl first); else fogvol_light.comp
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(std140, binding = 0) uniform FogvolLight {
    ivec4 info; // x G (cells per side), y volumes, z frame, w 1: blend everything (a reset)
    ivec4 reset_[2]; // per volume (one int each): the slots (bits 0..7) to restart
    vec4 params; // x the blend weight of a new estimate, y ray-traced shadows on
    vec4 medium_[167]; // the medium record (medium.glsl): the volumes' shapes
    vec4 fogvol_[FV_VEC4S_]; // the volumes' record (fog_volume.glsl)
} fl;
#define MEDIUM(i) fl.medium_[i]
layout(binding = 4) uniform sampler3D medium_tex;
#define MEDIUM_TEX medium_tex
#define FOGVOL(i) fl.fogvol_[i]
layout(binding = 5, rgba16f) uniform image3D grid_a;
layout(binding = 6, rgba16f) uniform image3D grid_b;
#include "medium.glsl"
#include "fog_volume.glsl"
void fv_shadows(int v, vec3 x, vec3 r, out vec4 ta, out vec4 tb) { ta = vec4(1.0); tb = vec4(1.0); }
uint fl_hash(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}
// The optical depth from x toward l over len world units through volume v (its full density).
float fl_depth(int v, vec3 x, vec3 l, float len, int steps, float jitter) {
    float ds = len / float(steps), depth = 0.0, n = -1.0;
    vec3 r;
    for (int k = 0; k < steps; k++) {
        n = -1.0;
        depth += fv_density(v, x + l * ((float(k) + jitter) * ds), n, r);
    }
    return depth * ds;
}
void main() {
    int G = fl.info.x;
    ivec3 c = ivec3(gl_GlobalInvocationID);
    int v = c.z / G;
    if (c.x >= G || c.y >= G || v >= fl.info.y) return;
    int z = c.z - v * G;
    int resets = fl.reset_[v / 4][v & 3];
    bool all_ = fl.info.w != 0 || resets != 0;
    uint frame = uint(fl.info.z);
    if (!all_ && ((c.x + c.y + z + int(frame)) & 1) != 0) return; // the other half's frame
    uint h = fl_hash(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u ^ (frame & 15u) * 2654435761u);
    vec3 jitter = vec3(h & 1023u, (h >> 10) & 1023u, (h >> 20) & 1023u) / 1023.0 - 0.5;
    vec3 r = ((vec3(c.x, c.y, z) + 0.5 + jitter * 0.7) / float(G)) * 2.0 - 1.0;
    // into the world: the inverse of medium_volume_frame's turn
    int b = fv_medium(v), f = fv_base(v);
    vec4 e = MEDIUM(b + 4);
    vec3 q = r * max(MEDIUM(b + 1).xyz, vec3(1.0));
    vec3 x = MEDIUM(b).xyz + vec3(e.x * q.x + e.y * q.z, q.y, -e.y * q.x + e.x * q.z);
    float strength = FOGVOL(f + 3).y, march_jitter = float((h >> 7) & 255u) / 255.0;
    float t[8];
    int lights = int(FOGVOL(f).w + 0.5);
    for (int i = 0; i < 7; i++) {
        t[i] = 1.0;
        vec3 l; float dist;
        if (i < 6) {
            if (i >= lights) continue;
            vec3 col = fv_light(v, i, x, l, dist);
            if (col.r + col.g + col.b <= 0.0) continue;
        } else {
            if (FOGVOL(1).w < 0.5) continue;
            l = FOGVOL(2).xyz; dist = 1e6;
        }
        float len = min(fv_hit(v, x, l, dist).y, dist);
        if (len > 0.5) t[i] = exp(-strength * fl_depth(v, x, l, len, 8, march_jitter));
#ifdef FOGVOL_RT
        if (fl.params.y > 0.5 && t[i] > 0.01) {
            float th; int hit; vec2 bc;
            int type = int(FOGVOL(f + 5 + 3 * min(i, 5)).w + 0.5);
            float reach = i == 6 ? 30000.0 : dist - (type >= 3 ? 32.0 : 24.0); // not its own fixture
            if (reach > 1.0 && trace(x, l, reach, true, l, 0.0, th, hit, bc)) t[i] = 0.0;
        }
#endif
    }
    // the ambient term: two directions a frame (mostly from above), over eight frames
    float amb = 0.0;
    for (int j = 0; j < 2; j++) {
        int k = int((frame * 2u + uint(j)) & 7u);
        float a = float(k) * 2.39996323 + float(h & 255u) * 0.0245;
        float y = -0.25 - 0.7 * fract(float(k) * 0.618034 + 0.31); // y down: up is negative
        vec3 d = normalize(vec3(cos(a) * sqrt(1.0 - y * y), y, sin(a) * sqrt(1.0 - y * y)));
        float len = fv_hit(v, x, d, 1e6).y;
        amb += len > 0.5 ? exp(-strength * fl_depth(v, x, d, len, 6, march_jitter)) : 1.0;
    }
    t[7] = amb * 0.5;
    ivec3 at = ivec3(c.x, c.y, c.z);
    vec4 old_a = imageLoad(grid_a, at), old_b = imageLoad(grid_b, at);
    float w = fl.params.x;
    vec4 wa = vec4(w), wb = vec4(w, w, w, w * 0.5);
    if (fl.info.w != 0) wa = wb = vec4(1.0);
    for (int i = 0; i < 4; i++) {
        if (((resets >> i) & 1) != 0) wa[i] = 1.0;
        if (((resets >> (i + 4)) & 1) != 0) wb[i] = 1.0;
    }
    vec4 na = mix(old_a, vec4(t[0], t[1], t[2], t[3]), wa), nb = mix(old_b, vec4(t[4], t[5], t[6], t[7]), wb);
    if (any(isnan(na))) na = vec4(1.0);
    if (any(isnan(nb))) nb = vec4(1.0);
    imageStore(grid_a, at, na);
    imageStore(grid_b, at, nb);
}
