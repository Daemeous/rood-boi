// The lights, emitters and settings of the path tracer (vk_rt.inc vrt_trace_ubo),
// shared by the trace and the haze volume.
layout(std140, binding = 0) uniform Trace {
    vec4 eye_;
    vec4 light_pos[16]; // world xyz, radius
    vec4 light_col[16]; // rgb * intensity, type
    vec4 light_dir[16]; // direction, cos(half cone)
    vec4 light_right[16];
    vec4 light_up[16];
    ivec4 light_shadow_[4]; // casts shadows
    vec4 emitter_pos[32]; // xyz, radius of influence
    vec4 emitter_col[32]; // rgb, emitting sphere radius (< 0: baked light, shadows only)
    vec4 room_lo_, room_hi_;
    ivec4 i0; // light_count, indirect, shadows, bounces
    ivec4 i1; // rays, emitter_count, baked_shadows, shadow_samples
    ivec4 i2; // glow_shadows, fog_steps, trace_debug, frame
    vec4 f0; // light_size, baked_bounce, emitter_light, contact_radius
    vec4 f1; // baked_ambient, fog_density, fog_anisotropy, fog_glow
    vec4 f2; // ao_radius (0: no traced AO), ao_strength
    ivec4 i3; // glow_casts: emitters that may cast shadows (bit i; transient effects never do)
    vec4 env_[5]; // the room's sun and sky (environment.h pc_env_gpu; environment.glsl)
    vec4 emitter_shape_[32]; // xyz half axis, w radius of a column glow (> 0 its side, < 0 solid); 0: the sphere
    vec4 emitter_shadow_[32]; // a column's shadows (glows.c): x core radius, y weight in the baked light's shadows (0: its colour's), z clear radius
} u;
#define env_sun_dir u.env_[0]
#define env_sun_col u.env_[1]
#define env_zenith u.env_[2]
#define env_horizon u.env_[3]
#define env_ground u.env_[4]
#include "environment.glsl"
#include "light_shape.glsl"
#define eye u.eye_.xyz
#define light_pos u.light_pos
#define light_col u.light_col
#define light_dir u.light_dir
#define light_right u.light_right
#define light_up u.light_up
#define emitter_pos u.emitter_pos
#define emitter_col u.emitter_col
#define room_lo u.room_lo_.xyz
#define room_hi u.room_hi_.xyz
#define light_count u.i0.x
#define indirect u.i0.y
#define shadows u.i0.z
#define bounces u.i0.w
#define rays u.i1.x
#define emitter_count u.i1.y
#define baked_shadows u.i1.z
#define shadow_samples u.i1.w
#define glow_shadows u.i2.x
#define fog_steps u.i2.y
#define trace_debug u.i2.z
#define frame uint(u.i2.w)
#define light_size u.f0.x
#define baked_bounce u.f0.y
#define emitter_light u.f0.z
#define contact_radius u.f0.w
#define baked_ambient u.f1.x
#define fog_density u.f1.y
#define fog_anisotropy u.f1.z
#define fog_glow u.f1.w
#define ao_radius u.f2.x
#define ao_strength u.f2.y
#define glow_casts u.i3.x
// Emitter i's light may be shadowed (glows.c pc_glow_casts: not a transient effect's).
bool glow_casts_shadow(int i) { return ((glow_casts >> i) & 1) != 0; }
// Emitter i's emitting shape (glows.c; pc/docs/Glows.md, "Enclosing glows"):
// a small sphere (radius emitter_col.w) at emitter_pos, or a column around
// it (emitter_shape_: half axis, radius): the glowing side of a column a
// character can stand in (the save point), or a solid disc / cylinder (w < 0,
// a glowing circle on the floor). A character inside a column hides only the
// part of it behind them, where a point at its centre was hidden from
// everywhere (the room went dark around Ashley standing in a save point).
bool emitter_is_column(int i) { return u.emitter_shape_[i].w != 0.0; }
// A point of emitter i's shape from four uniform random numbers.
vec3 emitter_sample(int i, vec4 r) {
    vec4 shape = u.emitter_shape_[i];
    if (shape.w == 0.0) {
        vec3 v = r.xyz * 2.0 - 1.0;
        return emitter_pos[i].xyz + normalize(v + 1e-4) * pow(r.w, 0.333) * abs(emitter_col[i].w);
    }
    float h = length(shape.xyz);
    vec3 a = h > 1e-3 ? shape.xyz / h : vec3(0.0, 1.0, 0.0);
    vec3 t = normalize(cross(abs(a.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), a)), b = cross(a, t);
    float phi = 6.2831853 * r.x, radius = shape.w > 0.0 ? shape.w : -shape.w * sqrt(r.y);
    return emitter_pos[i].xyz + shape.xyz * (2.0 * r.z - 1.0) + (t * cos(phi) + b * sin(phi)) * radius;
}
// Shadows of a column's side (> 0; pc/docs/Glows.md "Enclosing glows"): the
// light comes from the whole side (its cosine and falloff), but its shadow
// rays aim at the same point of a thinner core (emitter_shadow_.x, a quarter
// of the radius), so a character beside the save point casts a soft shadow
// that is clearly there (rays to the whole 128-unit-wide side only greyed
// the floor), and they stop where they enter the clear middle of the column
// (emitter_shadow_.z, 0.4 of the radius): a character standing in the
// middle hides none of the light (the room stays lit), one anywhere else
// shadows the floor beyond him.
// How far a ray from p along dir runs before it enters column i's clear
// middle (0: p is inside it; huge: it never does).
float emitter_column_entry(int i, vec3 p, vec3 dir) {
    vec4 shape = u.emitter_shape_[i];
    float h = length(shape.xyz);
    vec3 a = h > 1e-3 ? shape.xyz / h : vec3(0.0, 1.0, 0.0), v = p - emitter_pos[i].xyz;
    vec3 pp = v - a * dot(v, a), dd = dir - a * dot(dir, a);
    float A = dot(dd, dd), B = dot(pp, dd), C = dot(pp, pp) - u.emitter_shadow_[i].z * u.emitter_shadow_[i].z;
    if (C <= 0.0) return 0.0;
    float disc = B * B - A * C;
    if (A < 1e-8 || B >= 0.0 || disc < 0.0) return 1e30;
    return (-B - sqrt(disc)) / A;
}
// The shadow ray of the sample emitter_sample(i, r) seen from p: its
// direction, and how far it may be blocked (<= 0: by nothing).
float emitter_shadow(int i, vec3 p, vec4 r, out vec3 dir) {
    vec4 shape = u.emitter_shape_[i];
    if (shape.w <= 0.0) { // a sphere (as before), or a solid column
        vec3 to = emitter_sample(i, r) - p;
        float d = length(to);
        dir = to / max(d, 1e-3);
        return d - (shape.w == 0.0 ? abs(emitter_col[i].w) : 2.0);
    }
    float h = length(shape.xyz);
    vec3 a = h > 1e-3 ? shape.xyz / h : vec3(0.0, 1.0, 0.0);
    vec3 t = normalize(cross(abs(a.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), a)), b = cross(a, t);
    float phi = 6.2831853 * r.x;
    vec3 to = emitter_pos[i].xyz + shape.xyz * (2.0 * r.z - 1.0) + (t * cos(phi) + b * sin(phi)) * u.emitter_shadow_[i].x - p;
    float d = length(to);
    dir = to / max(d, 1e-3);
    return min(d, emitter_column_entry(i, p, dir)) - 2.0;
}
// The same, noise-free (the lighting signature's rays): to the sphere's
// centre, or the core's nearest point.
float emitter_shadow_nearest(int i, vec3 p, out vec3 dir) {
    vec4 shape = u.emitter_shape_[i];
    vec3 target = emitter_pos[i].xyz;
    if (shape.w > 0.0) {
        vec3 v = p - target;
        float h2 = dot(shape.xyz, shape.xyz);
        vec3 on_axis = target + shape.xyz * (h2 > 1e-6 ? clamp(dot(v, shape.xyz) / h2, -1.0, 1.0) : 0.0);
        vec3 out_ = p - on_axis;
        float r = length(out_);
        target = r > 1e-3 ? on_axis + out_ / r * min(u.emitter_shadow_[i].x, r) : on_axis;
    }
    vec3 to = target - p;
    float d = length(to);
    dir = to / max(d, 1e-3);
    if (shape.w > 0.0)
        return min(d, emitter_column_entry(i, p, dir)) - 2.0;
    return d - (shape.w == 0.0 ? abs(emitter_col[i].w) : 2.0);
}
#ifdef LIGHT_LIST
// Every dynamic light (vk_rt.inc vrt_light_list, any number: light_count of
// them), in a storage buffer: the path tracer samples them per pixel instead
// of looping over a fixed few (pc/docs/Vulkan_Renderer.md, many lights).
// Field for field light_list.h's pc_light_gpu (as scaled.frag's): any other
// order or size reads every light after the first from the wrong bytes.
struct Light { vec4 pos, col, right, up, dir, world; ivec4 info; }; // info.x: casts shadows
layout(std430, binding = LIGHT_LIST) readonly buffer LightList { Light lights_[]; };
vec4 lpos(int i) { return lights_[i].pos; }
vec4 lcol(int i) { return lights_[i].col; }
vec4 ldir(int i) { return lights_[i].dir; }
vec4 lright(int i) { return lights_[i].right; }
vec4 lup(int i) { return lights_[i].up; }
int light_shadow(int i) { return lights_[i].info.x; }
#else
vec4 lpos(int i) { return light_pos[i]; }
vec4 lcol(int i) { return light_col[i]; }
vec4 ldir(int i) { return light_dir[i]; }
vec4 lright(int i) { return light_right[i]; }
vec4 lup(int i) { return light_up[i]; }
int light_shadow(int i) { return u.light_shadow_[i >> 2][i & 3]; }
#endif
// A point or spot light's emitting radius: its own (light_right.w, light actors) or the room's light size.
float point_size(int i) { return lright(i).w > 0.0 ? lright(i).w : light_size; }
// Light i's unshadowed light at p (normal n, centre `centre`): falloff, cosine, spot cone / rect facing.
float light_falloff(int i, vec3 centre, vec3 p, vec3 n, out vec3 l, out float d) {
    vec3 to = centre - p; d = length(to); l = to / max(d, 1e-3);
    float x = clamp(d / lpos(i).w, 0.0, 1.0);
    float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);
    a *= light_shape(lcol(i).w, lpos(i).xyz, ldir(i), lright(i), lup(i), p, l); // spot edge, rect face and barn doors
    return a;
}
