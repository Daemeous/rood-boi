// The particles' pass on Vulkan (pc/src/vk_particles.inc; particles.h): per
// mote its closed form (particles_sim.glsl), the light reaching it (its
// strongest lights and glows and the sun: with PARTICLES_RT and this frame's
// acceleration structure a shadow ray each; else those that cast shadows give none), its colour
// into the colour image and, coloured, its game pixels into the key image
// (depth << 16 | mote: the nearest wins). particles.cpp's CPU path is the
// same for OpenGL; keep the two the same.
layout(local_size_x = 64) in;
layout(std140, binding = 0) uniform Particles { // particle_math.h pc_particles_gpu
    ivec4 info; // motes, emitters, display w, h
    ivec4 info2; // lights, glows, sources per mote, shadow rays
    vec4 time_; // t, glow scale, debug, glow shadows
    vec4 projection; // hx, hy, ofx, ofy
    vec4 rotation[3]; // rows of R, w the translation
    vec4 env_[5];
    vec4 floor_info; // corner x, z, 1 / cell x, 1 / cell z
    vec4 floor_n; // x cells per side (0 none)
    vec4 floor_[144];
    vec4 glow_pos[32];
    vec4 glow_col[32];
    vec4 emitters[128]; // 16 x 8
} pt;
struct Light { vec4 pos, col, right, up, dir, world; ivec4 info; }; // light_list.h pc_light_gpu; info.x casts
layout(std430, binding = 4) readonly buffer LightList { Light lights[]; };
layout(binding = 5, r32ui) uniform uimage2D keys;
layout(binding = 6, rgba32f) uniform writeonly image2D colours;
#define PARTICLE_EMITTER(e, k) pt.emitters[(e) * 8 + (k)]
#define PARTICLE_FLOOR(i) pt.floor_[i]
#define PARTICLE_FLOOR_INFO pt.floor_info
#define PARTICLE_FLOOR_N int(pt.floor_n.x)
#include "particles_sim.glsl"
#ifdef PARTICLES_RT
#include "rt_common.glsl"
bool blocked(vec3 o, vec3 d, float t) { float th; int hit; vec2 bc; return t > 0.0 && trace(o, d, t, true, d, 0.0, th, hit, bc); }
#else
uint rng = 0u;
float rand() { rng = rng * 747796405u + 2891336453u; return float(rng >> 8) / 16777216.0; }
bool blocked(vec3 o, vec3 d, float t) { return false; }
#endif
#define env_sun_dir pt.env_[0]
#define env_sun_col pt.env_[1]
#define env_zenith pt.env_[2]
#define env_horizon pt.env_[3]
#define env_ground pt.env_[4]
#include "environment.glsl"
#include "light_shape.glsl"
float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// The light reaching x (particles.cpp light_at): the strongest sources, a shadow ray each, and the sun.
vec3 mote_light(vec3 x) {
    int keep = clamp(pt.info2.z, 1, 4), kept = 0;
    vec3 rgb[4], dir[4];
    float reach[4], weight[4];
    bool casts[4];
    for (int i = 0; i < pt.info2.x + pt.info2.y; i++) {
        vec3 col, l;
        float d, a, skip;
        bool casting;
        if (i < pt.info2.x) {
            vec3 c = lights[i].pos.xyz, rel = x - c, r = lights[i].right.xyz, u = lights[i].up.xyz;
            if (lights[i].col.w > 1.5) { // a rect: its nearest point
                float rr = dot(r, r), uu = dot(u, u);
                c += r * (rr > 1e-6 ? clamp(dot(rel, r) / rr, -1.0, 1.0) : 0.0) + u * (uu > 1e-6 ? clamp(dot(rel, u) / uu, -1.0, 1.0) : 0.0);
            }
            vec3 to = c - x;
            d = length(to), l = to / max(d, 1e-3);
            float f = min(d / max(lights[i].pos.w, 1e-3), 1.0);
            a = (1.0 - f * f) * (1.0 - f * f)
                * light_shape(lights[i].col.w, lights[i].pos.xyz, lights[i].dir, lights[i].right, lights[i].up, x, l);
            col = lights[i].col.rgb * a, skip = 2.0, casting = lights[i].info.x != 0;
        } else {
            int j = i - pt.info2.x;
            vec3 to = pt.glow_pos[j].xyz - x;
            d = length(to), l = to / max(d, 1e-3);
            float f = min(d / max(pt.glow_pos[j].w, 1e-3), 1.0);
            a = (1.0 - f * f) * (1.0 - f * f) * pt.time_.y;
            col = pt.glow_col[j].rgb * a, skip = max(abs(pt.glow_col[j].w), 2.0), casting = pt.time_.w > 0.5;
        }
        if (!(a > 0.0)) continue;
        float w = lum(col);
        int at = kept < keep ? kept++ : keep;
        if (at == keep) {
            if (!(w > weight[keep - 1])) continue;
            at = keep - 1;
        }
        for (; at > 0 && weight[at - 1] < w; at--) {
            rgb[at] = rgb[at - 1], dir[at] = dir[at - 1], reach[at] = reach[at - 1];
            weight[at] = weight[at - 1], casts[at] = casts[at - 1];
        }
        rgb[at] = col, dir[at] = l, reach[at] = d - skip, weight[at] = w, casts[at] = casting;
    }
    // Without a tracer a source that casts shadows gives none: a mote in light only never
    // glints where it could be in shadow.
    bool traced = pt.info2.w != 0;
    vec3 sum = vec3(0.0);
    for (int k = 0; k < kept; k++)
        if (!(casts[k] && (!traced || blocked(x, dir[k], reach[k])))) sum += rgb[k];
    if (env_sun_on() && !(env_sun_col.w > 0.5 && (!traced || blocked(x, env_sun_dir.xyz, ENV_SUN_DISTANCE))))
        sum += env_sun_col.rgb;
    return sum;
}
void main() {
    int g = int(gl_GlobalInvocationID.x);
    if (g >= pt.info.x) return;
    int e = 0;
    for (int k = 1; k < pt.info.y; k++)
        if (g >= int(PARTICLE_EMITTER(k, 7).z)) e = k;
    Mote m = particle_mote(e, g - int(PARTICLE_EMITTER(e, 7).z), pt.time_.x);
    vec4 v5 = PARTICLE_EMITTER(e, 5), v6 = PARTICLE_EMITTER(e, 6), v7 = PARTICLE_EMITTER(e, 7);
    vec3 l = (pt.time_.z > 0.5 ? vec3(1.0) : mote_light(m.pos)) + (v6.w > 0.5 ? vec3(v7.y) : vec3(0.0));
    vec3 colour = l * v5.rgb * v5.w * m.fade * m.gain;
    imageStore(colours, ivec2(g & 255, g >> 8), vec4(colour, lum(l)));
    if (dot(colour, vec3(1.0)) <= 1e-4) return; // a dark mote takes no pixel
    vec3 v = vec3(dot(pt.rotation[0].xyz, m.pos) + pt.rotation[0].w, dot(pt.rotation[1].xyz, m.pos) + pt.rotation[1].w,
        dot(pt.rotation[2].xyz, m.pos) + pt.rotation[2].w);
    if (!(v.z >= 16.0)) return; // PC_PARTICLE_NEAR
    vec2 s = pt.projection.zw + pt.projection.xy * v.xy / v.z;
    uint key = (uint(clamp(v.z, 1.0, 65535.0)) << 16) | uint(g & 0xffff);
    ivec2 a = ivec2(floor(s - 0.5 * float(m.size - 1)));
    for (int dy = 0; dy < m.size; dy++)
        for (int dx = 0; dx < m.size; dx++) {
            ivec2 c = a + ivec2(dx, dy);
            if (all(greaterThanEqual(c, ivec2(0))) && all(lessThan(c, pt.info.zw))) imageAtomicMin(keys, c, key);
        }
}
