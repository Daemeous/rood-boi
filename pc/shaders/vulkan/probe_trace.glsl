// Probe GI trace pass: pv_info.y rays from each probe (spherical Fibonacci,
// rotated every update), shaded at the nearest hit as rt_trace.comp shades
// a path vertex: the dynamic lights with shadow rays, the glows, the baked
// bounce, plus the probe field itself at the hit (multi-bounce, last
// update's atlases). One invocation per ray: x ray, y probe. Output per
// ray: rgb radiance, a signed hit distance (miss: the max distance; a back
// face: -0.2 x its distance, as DDGI, for relocation and classification).
// The includer provides the scene (rt_common.glsl ray queries, or
// probe_bvh.glsl) after rt_lights.glsl (lights, emitters, settings at the
// probes' base intensities: flicker is applied per frame, probe_state.comp).
layout(local_size_x = 32, local_size_y = 1) in;
#define PROBE_SAMPLING
layout(binding = 13) uniform sampler2D probe_irradiance;
layout(binding = 14) uniform sampler2D probe_depth;
layout(binding = 15) uniform sampler2D probe_data;
#include "probe_update.glsl"
layout(binding = 4, rgba16f) uniform writeonly image2D ray_out;

bool touched = false; // rt_trace.comp's moving-blocker flag (unused here)
vec3 probe_emitter_point(int i) {
    float a = rand(), b = rand(), c = rand();
    return emitter_sample(i, vec4(a, b, c, rand())); // rt_lights.glsl: its sphere or column
}
float probe_emitter_falloff(int i, vec3 p, vec3 n, out vec3 l, out float d) {
    vec3 to = probe_emitter_point(i) - p; d = length(to); l = to / max(d, 1e-3);
    float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);
    return (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);
}
// rt_trace.comp emitted(): one glow, picked in proportion to its unshadowed light.
vec3 probe_emitted(vec3 p, vec3 n) {
    if (emitter_light <= 0.0 || emitter_count == 0) return vec3(0.0);
    float weights[32]; float all_ = 0.0;
    for (int i = 0; i < emitter_count; i++) {
        weights[i] = 0.0;
        if (emitter_col[i].w < 0.0) continue; // a baked light: already in the vertex colours
        vec3 to = emitter_pos[i].xyz - p; float d = length(to);
        float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);
        weights[i] = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, to / max(d, 1e-3)), 0.0) * dot(emitter_col[i].rgb, vec3(0.299, 0.587, 0.114));
        all_ += weights[i];
    }
    if (all_ <= 0.0) return vec3(0.0);
    float pick = rand() * all_; int i = 0;
    for (; i < emitter_count - 1; i++) { pick -= weights[i]; if (pick <= 0.0 && weights[i] > 0.0) break; }
    if (weights[i] <= 0.0) return vec3(0.0);
    vec3 l; float d, th; int hit; vec2 bc;
    float a = probe_emitter_falloff(i, p, n, l, d);
    if (a <= 0.0) return vec3(0.0);
    if (glow_shadows != 0 && glow_casts_shadow(i) && trace(p + n * 1.5, l, d - emitter_gap(i) - 2.0, true, n, 0.0, th, hit, bc)) return vec3(0.0);
    return emitter_col[i].rgb * a * emitter_light * all_ / weights[i];
}
// rt_trace.comp direct(): the dynamic lights with a shadow ray each, plus the glows.
vec3 probe_direct(vec3 p, vec3 n) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < light_count; i++) {
        vec3 centre = light_pos[i].xyz;
        if (light_col[i].w > 1.5) centre += light_right[i].xyz * (rand() * 2.0 - 1.0) + light_up[i].xyz * (rand() * 2.0 - 1.0);
        else centre += (vec3(rand(), rand(), rand()) - 0.5) * 2.0 * point_size(i);
        vec3 to = centre - p; float d = length(to); vec3 l = to / max(d, 1e-3);
        float x = clamp(d / light_pos[i].w, 0.0, 1.0);
        float a = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);
        if (light_col[i].w > 1.5) a *= max(dot(-l, light_dir[i].xyz), 0.0);
        else if (light_col[i].w > 0.5) a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));
        if (a <= 0.0) continue;
        float th; int hit; vec2 bc;
        if (trace(p + n * 1.5, l, d - max(3.0, light_up[i].w), true, n, 0.0, th, hit, bc)) continue;
        sum += light_col[i].rgb * a;
    }
    // The room's sun (environment.glsl): its own shadow ray to a point of its disc.
    if (env_sun_on()) {
        vec3 l = env_sun_sample(rand(), rand());
        float c = dot(n, l), th; int hit; vec2 bc;
        if (c > 0.0 && !(env_sun_col.w > 0.5 && trace(p + n * 1.5, l, ENV_SUN_DISTANCE, true, n, 0.0, th, hit, bc)))
            sum += env_sun_col.rgb * c;
    }
    return sum + probe_emitted(p, n);
}
void main() {
    int r = int(gl_GlobalInvocationID.x), p = int(gl_GlobalInvocationID.y), n_rays = pv_info.y;
    if (r >= n_rays || p >= pv_info.x) return;
    ivec2 at = ivec2(r, p);
    float max_t = pv_bias.z;
    if (probe_state[p].w <= 0.0 && (pv_info.w & (PV_RELOCATE | PV_CLASSIFY)) == 0) {
        imageStore(ray_out, at, vec4(0.0, 0.0, 0.0, max_t));
        return;
    }
    vec3 o = probe_position(p), d = probe_ray_direction(r, n_rays);
    rng = uint(r) * 1973u + uint(p) * 9277u + uint(pv_info.z) * 26699u;
    float t; int hit; vec2 bc;
    if (!trace(o, d, max_t, false, d, 0.0, t, hit, bc)) {
        imageStore(ray_out, at, vec4(env_sky(d), max_t)); // the sky's light (none in a roofed room)
        return;
    }
    Tri tri = tri_at(hit);
    vec3 ng = cross(tri.b.xyz - tri.a.xyz, tri.c.xyz - tri.a.xyz);
    bool facing = (pv_info.w & PV_FRONT_CCW) != 0 ? dot(ng, d) < 0.0 : dot(ng, d) > 0.0;
    if (!facing) { // a back face: the probe may be inside geometry
        imageStore(ray_out, at, vec4(0.0, 0.0, 0.0, -0.2 * t));
        return;
    }
    vec3 y = o + d * t, ny = normalize(ng);
    if (dot(ny, d) > 0.0) ny = -ny;
    vec3 vc = vertex_rgb(tri.a.w) * (1.0 - bc.x - bc.y) + vertex_rgb(tri.b.w) * bc.x + vertex_rgb(tri.c.w) * bc.y;
    vec3 albedo = vc;
    if ((tri.info.w & 1u) != 0u) {
        uint c = surface_texel(tri, bc);
        albedo = vec3(c & 31u, (c >> 5) & 31u, (c >> 10) & 31u) / 31.0;
    }
    vec3 baked = (tri.info.w & 2u) != 0u ? albedo : min(albedo * vc * 2.0, 1.0);
    vec3 radiance = albedo * probe_direct(y + ny * 0.5, ny) + baked * baked_bounce;
    if (pv_blend.w > 0.0 && pv_origin.w > 0.5)
        radiance += albedo * pv_blend.w * probe_irradiance_at(y, ny, d);
    if (any(isnan(radiance)) || any(isinf(radiance))) radiance = vec3(0.0);
    imageStore(ray_out, at, vec4(radiance, t));
}
