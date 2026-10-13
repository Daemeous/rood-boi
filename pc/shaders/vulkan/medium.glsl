// The room's participating medium (pc/src/atmosphere.cpp pc_medium_gpu):
// the uniform haze inside the room's bounds, the ground fog (density
// falling off with height above the room's floor, animated noise drifting
// with the wind) and the fog volumes (boxes and spheres with soft edges).
// Shared by the traced haze (Vulkan froxels rt_haze.comp and
// rt_haze_integrate.comp, ptgi.c's OpenGL march) and the present passes'
// unshadowed haze (scaled.frag, gl_present.c). The includer defines
// MEDIUM(i), the i-th vec4 of the record. World space, y down.
#ifndef MEDIUM_GLSL
#define MEDIUM_GLSL
#define MEDIUM_VOLUMES 16
#define MEDIUM_VOLUME_BASE 8
// The record (pc_medium_gpu):
//  0: haze density (per world unit, uniform in the room), phase g, time (s), volume count
//  1: fog density at its base, scale height, base y (world, y down), noise amount (0..1)
//  2: fog albedo rgb, 1 / noise feature size
//  3: fog fill rgb (light it scatters from everywhere), on
//  4: wind xyz (world units per second), -
//  5: rgb the sky's light where nothing traces it (mean radiance, half blocked: in-scattered
//     everywhere; 0 in the traced haze, which samples the sky with occlusion), w its strength
//  6, 7: the room's bounds (haze and ground fog fill them only)
//  8 + 5 i: volume i: centre + density, half size + edge, albedo + noise, fill + -, cos/sin yaw + shape (1 sphere)
float medium_haze() { return MEDIUM(0).x; }
float medium_g() { return MEDIUM(0).y; }
int medium_volume_count() { return int(MEDIUM(0).w + 0.5); }
bool medium_on() { return MEDIUM(0).x > 0.0 || MEDIUM(1).x > 0.0 || MEDIUM(0).w > 0.5; }
// Henyey-Greenstein phase function times 4 pi (1 for isotropic scattering).
float medium_phase(float mu, float g) { return (1.0 - g * g) / pow(max(1.0 + g * g + 2.0 * g * mu, 1e-4), 1.5); }
float medium_hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float medium_value_noise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(medium_hash(i), medium_hash(i + vec3(1, 0, 0)), f.x),
                   mix(medium_hash(i + vec3(0, 1, 0)), medium_hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(medium_hash(i + vec3(0, 0, 1)), medium_hash(i + vec3(1, 0, 1)), f.x),
                   mix(medium_hash(i + vec3(0, 1, 1)), medium_hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
// Drifting noise, 0..1: two octaves carried by the wind at different
// speeds (so the wisps change shape as they move), squashed vertically
// (sheets of mist rather than blobs).
float medium_noise(vec3 p) {
    float t = MEDIUM(0).z, s = MEDIUM(2).w;
    vec3 w = MEDIUM(4).xyz;
    vec3 q = (p - w * t) * s * vec3(1.0, 2.2, 1.0);
    vec3 r = (p - w * (t * 1.7) + vec3(0.0, 3.0 * t, 0.0)) * s * vec3(2.03, 4.1, 2.03);
    return 0.65 * medium_value_noise(q) + 0.35 * medium_value_noise(r + vec3(17.3, 5.1, 31.7));
}
// A density's noise factor: mean about 1, 1 - amount .. 1 + amount.
float medium_wisps(float n, float amount) { return mix(1.0, 2.0 * smoothstep(0.2, 0.8, n), amount); }
bool medium_in_room(vec3 x) { return all(greaterThanEqual(x, MEDIUM(6).xyz)) && all(lessThanEqual(x, MEDIUM(7).xyz)); }
// The ground fog's density without noise (y down: height above the base is base y - y).
float medium_fog_base(vec3 x) {
    if (MEDIUM(1).x <= 0.0) return 0.0;
    return MEDIUM(1).x * exp(-max(MEDIUM(1).z - x.y, 0.0) / max(MEDIUM(1).y, 1.0));
}
// Volume i's density at x (its soft-edged shape, its noise; n < 0: not computed yet).
float medium_volume(int i, vec3 x, inout float n) {
    int b = MEDIUM_VOLUME_BASE + 5 * i;
    vec4 a = MEDIUM(b), half_size = MEDIUM(b + 1), e = MEDIUM(b + 4);
    vec3 r = x - a.xyz;
    r = vec3(e.x * r.x - e.y * r.z, r.y, e.y * r.x + e.x * r.z) / max(half_size.xyz, vec3(1.0)); // into its frame, unit size
    float d = e.z > 0.5 ? length(r) : max(max(abs(r.x), abs(r.y)), abs(r.z));
    if (d >= 1.0) return 0.0;
    float w = 1.0 - smoothstep(1.0 - clamp(half_size.w, 0.001, 1.0), 1.0, d);
    float amount = MEDIUM(b + 2).w;
    if (amount > 0.0) {
        if (n < 0.0) n = medium_noise(x);
        w *= medium_wisps(n, amount);
    }
    return a.w * w;
}
// The medium at x: returns the extinction (per world unit); scatter: the
// scattering coefficient per colour (extinction x albedo); emit: the light
// it adds by itself per unit length (its fill light and the sky's light
// scattered toward the eye).
float medium_at(vec3 x, out vec3 scatter, out vec3 emit) {
    scatter = vec3(0.0);
    emit = vec3(0.0);
    float extinction = 0.0, n = -1.0;
    if (medium_in_room(x)) {
        extinction = MEDIUM(0).x;
        scatter = vec3(extinction);
        float f = medium_fog_base(x);
        if (f > 1e-9) {
            if (MEDIUM(1).w > 0.0) {
                n = medium_noise(x);
                f *= medium_wisps(n, MEDIUM(1).w);
            }
            extinction += f;
            scatter += f * MEDIUM(2).rgb;
            emit += f * MEDIUM(3).rgb;
        }
    }
    int count = medium_volume_count();
    for (int i = 0; i < count && i < MEDIUM_VOLUMES; i++) {
        float d = medium_volume(i, x, n);
        if (d <= 0.0) continue;
        int b = MEDIUM_VOLUME_BASE + 5 * i;
        extinction += d;
        scatter += d * MEDIUM(b + 2).rgb;
        emit += d * MEDIUM(b + 3).rgb;
    }
    emit += scatter * MEDIUM(5).rgb;
    return extinction;
}
// The part [enter, leave] of the ray origin + direction t, 0 <= t <= len, that can
// hold the medium: the room's bounds and every volume's bounding box.
vec2 medium_span(vec3 origin, vec3 direction, float len) {
    vec3 inv = 1.0 / direction;
    float enter = len, leave = 0.0;
    if (MEDIUM(0).x > 0.0 || MEDIUM(1).x > 0.0) {
        vec3 t0 = (MEDIUM(6).xyz - origin) * inv, t1 = (MEDIUM(7).xyz - origin) * inv;
        float a = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
        float b = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), len));
        if (b > a) { enter = a; leave = b; }
    }
    int count = medium_volume_count();
    for (int i = 0; i < count && i < MEDIUM_VOLUMES; i++) {
        int b_ = MEDIUM_VOLUME_BASE + 5 * i;
        float r = length(MEDIUM(b_ + 1).xyz);
        vec3 t0 = (MEDIUM(b_).xyz - r - origin) * inv, t1 = (MEDIUM(b_).xyz + r - origin) * inv;
        float a = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
        float b = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), len));
        if (b > a) { enter = min(enter, a); leave = max(leave, b); }
    }
    return vec2(enter, leave);
}
// Interleaved gradient noise (0..1), a cheap blue-ish noise over pixels;
// with an R2 offset per frame its samples stratify over time too.
float medium_ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
// Whether the medium may be present at x (its largest density, noise at
// its peak, is above zero): passes skip the rest.
bool medium_maybe(vec3 x) {
    if (medium_in_room(x) && (MEDIUM(0).x > 0.0 || medium_fog_base(x) * (1.0 + MEDIUM(1).w) > 1e-7)) return true;
    int count = medium_volume_count();
    for (int i = 0; i < count && i < MEDIUM_VOLUMES; i++) {
        int b = MEDIUM_VOLUME_BASE + 5 * i;
        vec3 r = abs(x - MEDIUM(b).xyz);
        float reach = length(MEDIUM(b + 1).xyz);
        if (max(max(r.x, r.y), r.z) < reach && MEDIUM(b).w > 0.0) return true;
    }
    return false;
}
#endif
