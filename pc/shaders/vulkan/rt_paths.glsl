#ifndef ROOD_RT_PATHS_GLSL
#define ROOD_RT_PATHS_GLSL
// The path tracer's light transport (rt_trace.comp: indirect light, shadows,
// baked-light visibility, signature; rt_reflect.comp: the material
// response's reflections), shared by both passes: the lights, the glows, the
// sun and sky at a point, with shadow rays. Bindings are the trace set's
// (vk_rt.inc vrt_init).
#define LIGHT_LIST 11
#include "rt_lights.glsl"
#include "rt_common.glsl"
layout(binding = 4, rgba32f) uniform readonly image2D position_in; // world xyz, w 1 = surface, 2 = moving
layout(binding = 5, rgba16f) uniform readonly image2D normal_in;
layout(binding = 6, rgba16f) uniform writeonly image2D gi_out;
layout(binding = 7, rgba16f) uniform writeonly image2D extra_out; // r: baked light visibility, gba glow light
layout(binding = 8, r32ui) uniform writeonly uimage2D signature_out;
layout(binding = 10, r16f) uniform writeonly image2D hit_out; // mean first-bounce hit distance (NRD)
// A moving blocker (character, prop) shadowed this pixel's visibility or
// baked-light visibility: its history is kept short (signature bit 31).
bool touched = false;
vec3 cosine_direction(vec3 n) {
    float r1 = rand(), r2 = rand(), phi = 6.2831853 * r1, s = sqrt(r2);
    vec3 t = normalize(cross(abs(n.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), n)), b = cross(n, t);
    return normalize(t * cos(phi) * s + b * sin(phi) * s + n * sqrt(1.0 - r2));
}
// The last emitter_falloff's shadow ray (rt_lights.glsl emitter_shadow).
vec3 shadow_dir; float shadow_reach;
// A point of emitter i's emitting shape (rt_lights.glsl emitter_sample): its
// light at p, and the shadow ray that goes with it.
float emitter_falloff(int i, vec3 p, vec3 n, out vec3 l, out float d) {
    float a = rand(), b = rand(), c = rand();
    vec4 r = vec4(a, b, c, rand());
    vec3 to = emitter_sample(i, r) - p; d = length(to); l = to / max(d, 1e-3);
    shadow_reach = emitter_shadow(i, p, r, shadow_dir);
    float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);
    return (1.0 - x * x) * (1.0 - x * x) * max(dot(n, l), 0.0);
}
// Light from the emitters, shadowed by everything: one glow, picked in
// proportion to its unshadowed light.
vec3 emitted(vec3 p, vec3 n) {
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
    float a = emitter_falloff(i, p, n, l, d);
    if (a <= 0.0) return vec3(0.0);
    if (glow_shadows != 0 && glow_casts_shadow(i) && shadow_reach > 2.0 && trace(p + n * 1.5, shadow_dir, shadow_reach - 2.0, true, n, 0.0, th, hit, bc)) {
        touched = touched || hit >= MOVER_BIT;
        return vec3(0.0);
    }
    return emitter_col[i].rgb * a * emitter_light * all_ / weights[i];
}
// Many lights (pc/docs/Vulkan_Renderer.md): up to EXACT_LIGHTS lights that
// reach a point are each traced, as before; past that, the shadow rays go to
// lights picked in proportion to their unshadowed light there (weighted
// reservoir sampling in one pass over the list), so the rays per pixel stay
// flat however many lights the room has, and the denoisers (temporal
// accumulation, NRD) average the rest.
const int EXACT_LIGHTS = 8;
vec3 jittered_centre(int i, vec3 p) {
    if (window_aperture(i)) return window_sample(i, p, vec2(rand(), rand()));
    vec3 centre = lpos(i).xyz;
    if (lcol(i).w > 1.5) return rect_source(i, vec2(rand(), rand()));
    vec3 r = vec3(rand(), rand(), rand()) * 2.0 - 1.0;
    return centre + normalize(r + 1e-4) * pow(rand(), 0.333) * point_size(i);
}
// Light i at p unshadowed, as luminance (the sampling weight).
float light_weight(int i, vec3 p, vec3 n) {
    vec3 l; float d;
    return light_falloff(i, window_aperture(i) ? window_centre(i, p) : lpos(i).xyz, p, n, l, d) * dot(lcol(i).rgb, vec3(0.299, 0.587, 0.114));
}
// Direct light from one light at p through its shadow ray (0 when blocked).
vec3 light_through(int i, vec3 p, vec3 n) {
    vec3 l; float d, th; int hit; vec2 bc;
    float a = light_falloff(i, jittered_centre(i, p), p, n, l, d);
    if (a <= 0.0 || trace(p + n * 1.5, l, d - max(3.0, lup(i).w), true, n, 0.0, th, hit, bc)) return vec3(0.0);
    return lcol(i).rgb * a;
}
// Direct light from the dynamic lights at a world point (GI bounces), with shadow rays.
vec3 direct(vec3 p, vec3 n) {
    vec3 sum = vec3(0.0);
    if (light_count <= EXACT_LIGHTS) {
        for (int i = 0; i < light_count; i++) sum += light_through(i, p, n);
    } else { // one light, picked in proportion to its unshadowed light
        float all_ = 0.0, w_pick = 0.0; int pick = -1;
        for (int i = 0; i < light_count; i++) {
            float w = light_weight(i, p, n);
            if (w <= 0.0) continue;
            all_ += w;
            if (rand() * all_ < w) { pick = i; w_pick = w; }
        }
        if (pick >= 0) sum = light_through(pick, p, n) * (all_ / w_pick);
    }
    return sum + emitted(p, n);
}
// One shadow ray from p to a point of light i: 1 lit, 0 blocked.
float shadow_ray(int i, vec3 p, vec3 n) {
    vec3 centre = jittered_centre(i, p);
    vec3 to = centre - p; float d = length(to); vec3 l = to / max(d, 1e-3);
    float th; int hit; vec2 bc;
    if (dot(n, l) <= 0.0) return 0.0;
    if (trace(p, l, d - max(3.0, lup(i).w), true, n, 16.0, th, hit, bc)) {
        touched = touched || hit >= MOVER_BIT;
        return 0.0;
    }
    return 1.0;
}
// Shadow visibility of the shadow-casting lights at a G-buffer surface:
// each light's unshadowed contribution weights its traced visibility (the
// present shader multiplies the lights' unshadowed sum by it). Few lights
// reaching p: each traced shadow_samples times; many: EXACT_LIGHTS x
// shadow_samples rays to lights picked in proportion to their weight (an
// unbiased estimate of the same ratio).
float visibility(vec3 p, vec3 n) {
    float all_ = 0.0; int reaching = 0;
    for (int i = 0; i < light_count; i++) {
        if (light_shadow(i) == 0) continue;
        float w = light_weight(i, p, n);
        if (w > 0.0) { all_ += w; reaching++; }
    }
    if (all_ <= 0.0) return 1.0;
    float lit = 0.0;
    if (reaching <= EXACT_LIGHTS) {
        for (int i = 0; i < light_count; i++) {
            if (light_shadow(i) == 0) continue;
            float w = light_weight(i, p, n);
            if (w <= 0.0) continue;
            float v = 0.0;
            for (int s = 0; s < shadow_samples; s++) v += shadow_ray(i, p, n);
            lit += w * v / float(shadow_samples);
        }
        return lit / all_;
    }
    int picks = EXACT_LIGHTS * shadow_samples;
    for (int s = 0; s < picks; s++) { // a light by inversion of the weights' running sum
        float pick = rand() * all_; int i = 0, last = -1;
        for (; i < light_count; i++) {
            if (light_shadow(i) == 0) continue;
            float w = light_weight(i, p, n);
            if (w <= 0.0) continue;
            last = i;
            pick -= w;
            if (pick <= 0.0) break;
        }
        if (i >= light_count) i = last;
        lit += shadow_ray(i, p, n);
    }
    return lit / float(picks);
}
// How much of the baked light reaches p past characters and props: their
// contact occlusion, and their shadows from the emitters. 1 = all of it.
float baked_visibility(vec3 p, vec3 n, vec3 skip_n) {
    moving_only = true;
    float occlusion = 0.0;
    for (int k = 0; k < 2; k++) {
        float t; int hit; vec2 bc;
        if (contact_radius > 0.0 && trace(p, cosine_direction(n), contact_radius, false, skip_n, 0.0, t, hit, bc)) {
            occlusion += 1.0 - 0.5 * t / contact_radius;
            touched = true;
        }
    }
    float ao = 1.0 - 0.5 * occlusion;
    float weights[32]; float all_ = 0.0;
    for (int i = 0; i < emitter_count; i++) {
        vec3 to = emitter_pos[i].xyz - p; float d = length(to);
        if (emitter_col[i].w >= 0.0 && (glow_shadows == 0 || !glow_casts_shadow(i))) { weights[i] = 0.0; continue; }
        float x = clamp(d / emitter_pos[i].w, 0.0, 1.0);
        weights[i] = (1.0 - x * x) * (1.0 - x * x) * max(dot(n, to / max(d, 1e-3)), 0.0) * (u.emitter_shadow_[i].y > 0.0 ? u.emitter_shadow_[i].y : u.emitter_shadow_[i].y < 0.0 ? 0.0 : dot(emitter_col[i].rgb, vec3(0.299, 0.587, 0.114))); // a column's share of the baked light: its own weight; < 0 none, not the baked light's (an emissive object; glows.c)
        all_ += weights[i];
    }
    float shadowed = 0.0;
    if (all_ > 0.0) {
        for (int k = 0; k < shadow_samples; k++) {
            float pick = rand() * all_; int i = 0;
            for (; i < emitter_count - 1; i++) { pick -= weights[i]; if (pick <= 0.0) break; }
            vec3 l; float d, th; int hit; vec2 bc;
            if (emitter_falloff(i, p, n, l, d) > 0.0 && shadow_reach > 0.0 && trace(p, shadow_dir, shadow_reach, true, skip_n, 0.0, th, hit, bc)) {
                shadowed += 1.0;
                touched = true;
            }
        }
        shadowed /= float(shadow_samples);
    }
    moving_only = false;
    float near = all_ / (all_ + 0.1);
    return ao * (1.0 - (1.0 - baked_ambient) * near * shadowed);
}
// The lighting signature: noise-free centre rays to the shadow-casting
// lights and (for the baked light) the emitters, one bit each.
uint lighting_signature(vec3 p, vec3 n) {
    uint sig = 0u; int bit = 0;
    // (with many lights only those reaching p take a bit: the first 12 in the
    // list's stable order, so a pixel keeps its bits from frame to frame)
    for (int i = 0; i < light_count && bit < 12; i++) {
        if (light_shadow(i) == 0) continue;
        vec3 to = lpos(i).xyz - p; float d = length(to);
        float th; int hit; vec2 bc;
        bool reaches = d < lpos(i).w && dot(n, to) > 0.0;
        if (reaches) {
            if (!trace(p, to / max(d, 1e-3), d - max(3.0, lup(i).w), true, n, 16.0, th, hit, bc)) sig |= 1u << bit;
            else touched = touched || hit >= MOVER_BIT;
        }
        if (reaches || light_count <= 12) bit++;
    }
    if (baked_shadows != 0) {
        moving_only = true;
        for (int i = 0; i < emitter_count && bit < 18; i++) {
            vec3 to = emitter_pos[i].xyz - p; float d = length(to);
            if (d >= emitter_pos[i].w || dot(n, to) <= 0.0 || !glow_casts_shadow(i)) continue;
            bit++;
            float th; int hit; vec2 bc;
            vec3 dir;
            float reach = emitter_shadow_nearest(i, p, dir);
            if (reach <= 0.0 || !trace(p, dir, reach, true, n, 0.0, th, hit, bc))
                sig |= 1u << bit;
        }
        moving_only = false;
    }
    return sig;
}
// Ambient occlusion of the room's (baked and indirect) light by everything
// within ao_radius (lighting quality "ao" with ray tracing): two short
// cosine rays per update, accumulated with the baked-light visibility.
float traced_ao(vec3 p, vec3 n) {
    float occlusion = 0.0;
    for (int k = 0; k < 2; k++) {
        float t; int hit; vec2 bc;
        if (trace(p, cosine_direction(n), ao_radius, false, n, 0.0, t, hit, bc))
            occlusion += 1.0 - t / ao_radius;
    }
    return 1.0 - ao_strength * 0.5 * occlusion;
}
// ---- the room's sun and sky (environment.glsl; pc/docs/Vulkan_Renderer.md "Sun and sky") ----
// Kept apart from the light list: the sun always gets its own rays (it never
// competes with the lights in their sampling), and the sky is what a GI ray
// that leaves the room brings back.
// The sun's light at a path vertex, through one shadow ray to a point of its disc.
vec3 sun_light(vec3 p, vec3 n) {
    if (!env_sun_on()) return vec3(0.0);
    vec3 l = env_sun_sample(rand(), rand());
    float c = dot(n, l);
    if (c <= 0.0) return vec3(0.0);
    float th; int hit; vec2 bc;
    if (env_sun_col.w > 0.5 && trace(p + n * 1.5, l, ENV_SUN_DISTANCE, true, n, 0.0, th, hit, bc)) return vec3(0.0);
    return env_sun_col.rgb * c;
}
// The sun's visibility at a G-buffer surface: shadow_samples rays to its disc.
float sun_visibility(vec3 p, vec3 n) {
    float lit = 0.0;
    for (int s = 0; s < shadow_samples; s++) {
        vec3 l = env_sun_sample(rand(), rand());
        float th; int hit; vec2 bc;
        if (dot(n, l) <= 0.0) continue;
        if (trace(p, l, ENV_SUN_DISTANCE, true, n, 16.0, th, hit, bc)) {
            touched = touched || hit >= MOVER_BIT;
            continue;
        }
        lit += 1.0;
    }
    return lit / float(shadow_samples);
}
// visibility() with a shadow-casting sun: the same weighted ratio
// sum(w_i V_i) / sum(w_i), the sun one more term (its unshadowed luminance
// at p; the present multiplies the sun's light by the ratio too).
float visibility_with_sun(vec3 p, vec3 n) {
    float v = visibility(p, n);
    float ws = env_sun_col.w > 0.5 ? max(dot(n, env_sun_dir.xyz), 0.0) * dot(env_sun_col.rgb, vec3(0.299, 0.587, 0.114)) : 0.0;
    if (ws <= 0.0) return v;
    float all_ = 0.0;
    for (int i = 0; i < light_count; i++)
        if (light_shadow(i) != 0) all_ += light_weight(i, p, n);
    return (v * all_ + sun_visibility(p, n) * ws) / (all_ + ws);
}
// The sun's bit of the lighting signature (bit 19): its centre ray.
uint sun_signature(vec3 p, vec3 n) {
    if (env_sun_col.w < 0.5 || !env_sun_on() || dot(n, env_sun_dir.xyz) <= 0.0) return 0u;
    float th; int hit; vec2 bc;
    if (trace(p, env_sun_dir.xyz, ENV_SUN_DISTANCE, true, n, 16.0, th, hit, bc)) {
        touched = touched || hit >= MOVER_BIT;
        return 0u;
    }
    return 1u << 19;
}
#endif
