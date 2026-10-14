// The room's participating medium (pc/src/atmosphere.cpp pc_medium_gpu):
// the uniform haze inside the room's bounds, the ground fog (density
// falling off with height above the room's floor, animated noise drifting
// with the wind) and the fog volumes (boxes and spheres with soft edges).
// Shared by the traced haze (Vulkan froxels rt_haze.comp and
// rt_haze_integrate.comp, ptgi.c's OpenGL march) and the present passes'
// unshadowed haze (scaled.frag, gl_present.c). The includer defines
// MEDIUM(i), the i-th vec4 of the record, and MEDIUM_TEX, the medium's 3D
// texture (atmosphere.h pc_medium_texture: the detail noise, the
// interactive fog's fields), where it samples the volumes (without it they
// have neither). World space, y down.
#ifndef MEDIUM_GLSL
#define MEDIUM_GLSL
#define MEDIUM_VOLUMES 16
#define MEDIUM_VOLUME_BASE 8
#define MEDIUM_LIMITS 88
#define MEDIUM_FLOOR 89
#define MEDIUM_FLOOR_DATA 91
#define MEDIUM_SIM 163
#define MEDIUM_DETAIL 164
// The record (pc_medium_gpu):
//  0: haze density (per world unit, uniform in the room), phase g, time (s), volume count
//  1: fog density at its base, scale height, base y (world, y down; with a floor grid: its offset above
//     the floor), noise amount (0..1)
//  2: fog albedo rgb, 1 / noise feature size
//  3: fog fill rgb (light it scatters from everywhere), on
//  4: wind xyz (world units per second), -
//  5: rgb the sky's light where nothing traces it (mean radiance, half blocked: in-scattered
//     everywhere; 0 in the traced haze, which samples the sky with occlusion), w its strength
//  6, 7: the room's bounds (haze and ground fog fill them only)
//  8 + 5 i: volume i: centre + density, half size + edge, albedo + noise, fill + its field's layer + 1 (0
//      none; + 8 for a sphere), cos/sin yaw + self-shadowing strength + detail (0..1)
//  88: limits (0: none): x the least transmittance along a ray (1 - the most opacity), y the most light it
//      scatters toward the eye (luminance, a soft knee), z the sun's in-scattering in shadowed air (1 - the
//      shafts' contrast), w the phase function's largest value (forward scattering toward the moon)
//  89, 90: the ground fog's floor, a top-down grid over the room: x, z of its corner, cells per unit in x, z;
//      y of height 0, units per step, cells per side, on
//  91..162: the grid's heights, two 12-bit steps per float (a * 4096 + b), row by row (z), x within a row
//  163: the medium's texture: x the interactive fog's cells per side, y its width and height (texels),
//      z its depth, w 1 (0: none). Slices 0..64: tileable detail noise (64 a period, the first repeated);
//      then one per simulated volume: its field (fog_sim.h) over its footprint, density / 2, in the corner
//  164: x 1 / the detail noise's period (world units), yzw the main light's direction (toward it: the
//      sun or moon, else up): the volumes' self-shadowing
float medium_haze() { return MEDIUM(0).x; }
float medium_g() { return MEDIUM(0).y; }
int medium_volume_count() { return int(MEDIUM(0).w + 0.5); }
bool medium_on() { return MEDIUM(0).x > 0.0 || MEDIUM(1).x > 0.0 || MEDIUM(0).w > 0.5; }
// Henyey-Greenstein phase function times 4 pi (1 for isotropic scattering),
// at most the record's cap: looking toward the moon through forward-
// scattering haze must not blow out the picture.
float medium_phase(float mu, float g) {
    float p = (1.0 - g * g) / pow(max(1.0 + g * g + 2.0 * g * mu, 1e-4), 1.5);
    return MEDIUM(MEDIUM_LIMITS).w > 0.0 ? min(p, MEDIUM(MEDIUM_LIMITS).w) : p;
}
// The sun's in-scattering at a point of the air with the sun's visibility
// v (0 shadowed .. 1 lit): the shafts' contrast is limited (the record's
// floor: shadowed air keeps some of it, as multiple scattering would).
float medium_sun_visibility(float v) { return mix(MEDIUM(MEDIUM_LIMITS).z, 1.0, v); }
// One march step's transmittance e^(-extinction len), never letting the
// ray's transmittance (so far: t) fall below the record's least: the
// atmosphere can never hide the scene. Its light (1 - step) / extinction
// stays consistent with it (the step ends where the limit is reached).
float medium_step(float extinction, float len, float t) {
    float s = exp(-extinction * len);
    float least = MEDIUM(MEDIUM_LIMITS).x;
    return least > 0.0 ? max(s, min(least / max(t, 1e-6), 1.0)) : s;
}
// The light the medium scatters toward the eye, its luminance limited with
// a soft knee (half the cap passes unchanged, the rest compresses toward it).
vec3 medium_limit_light(vec3 c) {
    float cap = MEDIUM(MEDIUM_LIMITS).y;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    if (cap <= 0.0 || l <= cap * 0.5) return c;
    float k = cap * 0.5;
    return c * ((k + k * (1.0 - exp(-(l - k) / k))) / l);
}
// A ray's transmittance, the same limit (for passes that integrate it alone).
float medium_limit_transmittance(float t) { return max(t, MEDIUM(MEDIUM_LIMITS).x); }
// A random gradient per lattice point (an integer hash: no visible pattern,
// no precision loss far from the origin).
vec3 medium_gradient(ivec3 c) {
    uvec3 v = uvec3(c + 65536) * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return vec3(v & 0xFFFFu) * (2.0 / 65535.0) - 1.0;
}
// Gradient (Perlin) noise with a quintic fade, about -0.7..0.7: smooth,
// without the blotches and grid of value noise.
float medium_perlin(vec3 x) {
    ivec3 i = ivec3(floor(x));
    vec3 f = x - floor(x), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n000 = dot(medium_gradient(i), f), n100 = dot(medium_gradient(i + ivec3(1, 0, 0)), f - vec3(1, 0, 0));
    float n010 = dot(medium_gradient(i + ivec3(0, 1, 0)), f - vec3(0, 1, 0));
    float n110 = dot(medium_gradient(i + ivec3(1, 1, 0)), f - vec3(1, 1, 0));
    float n001 = dot(medium_gradient(i + ivec3(0, 0, 1)), f - vec3(0, 0, 1));
    float n101 = dot(medium_gradient(i + ivec3(1, 0, 1)), f - vec3(1, 0, 1));
    float n011 = dot(medium_gradient(i + ivec3(0, 1, 1)), f - vec3(0, 1, 1));
    float n111 = dot(medium_gradient(i + ivec3(1, 1, 1)), f - vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}
// Drifting wisps, 0..1 (mean about 0.5): three octaves of gradient noise
// (fBm), the coordinates warped by one more slow octave (curling, wispy
// shapes rather than round blobs), squashed vertically (sheets of mist).
// The whole field drifts with the wind (world units per second); the finer
// octaves drift a little faster and the warp evolves slowly, so the wisps
// change shape gently as they move. Time is game time (atmosphere.cpp: it
// stops with the game, follows fast-forward, frame-rate independent).
float medium_noise(vec3 p) {
    float t = MEDIUM(0).z, s = MEDIUM(2).w;
    vec3 w = MEDIUM(4).xyz;
    vec3 q = (p - w * t) * s * vec3(1.0, 2.2, 1.0);
    float warp = medium_perlin(q * 0.5 + vec3(11.3, 5.7, 27.1) + vec3(0.0, 0.0, t * 0.015));
    q += warp * vec3(0.9, 0.35, 0.7);
    float n = 0.55 * medium_perlin(q) + 0.3 * medium_perlin(q * 2.03 - w * (t * 0.15 * s) + vec3(17.3, 5.1, 31.7))
        + 0.15 * medium_perlin(q * 4.11 - w * (t * 0.3 * s) + vec3(3.7, 41.9, 13.3));
    return clamp(0.5 + 1.1 * n, 0.0, 1.0);
}
// A density's noise factor: mean about 1, 1 - amount .. 1 + amount.
float medium_wisps(float n, float amount) { return mix(1.0, 2.0 * smoothstep(0.2, 0.8, n), amount); }
bool medium_in_room(vec3 x) { return all(greaterThanEqual(x, MEDIUM(6).xyz)) && all(lessThanEqual(x, MEDIUM(7).xyz)); }
// One height of the floor grid (cell i, j), world y.
float medium_floor_cell(int i, int j) {
    int n = int(MEDIUM(MEDIUM_FLOOR + 1).z + 0.5);
    int k = clamp(j, 0, n - 1) * n + clamp(i, 0, n - 1);
    float v = MEDIUM(MEDIUM_FLOOR_DATA + k / 8)[(k / 2) & 3];
    float a = floor(v / 4096.0);
    float q = (k & 1) == 0 ? a : v - a * 4096.0;
    return MEDIUM(MEDIUM_FLOOR + 1).x + q * MEDIUM(MEDIUM_FLOOR + 1).y;
}
// The ground fog's floor under x (world y, y down): the room's lowest floor
// there (scene_capture.c pc_scene_floor_grid), bilinear between cells; the
// room's main floor without a grid.
float medium_floor(vec3 x) {
    if (MEDIUM(MEDIUM_FLOOR + 1).w < 0.5) return MEDIUM(1).z;
    vec2 c = (x.xz - MEDIUM(MEDIUM_FLOOR).xy) * MEDIUM(MEDIUM_FLOOR).zw - 0.5;
    vec2 f = c - floor(c);
    ivec2 i = ivec2(floor(c));
    float a = mix(medium_floor_cell(i.x, i.y), medium_floor_cell(i.x + 1, i.y), f.x);
    float b = mix(medium_floor_cell(i.x, i.y + 1), medium_floor_cell(i.x + 1, i.y + 1), f.x);
    return mix(a, b, f.y);
}
// The ground fog's density without noise: thickest at its base, falling
// off by e every scale height up (y down: the height above the base is
// base y - y). With the floor grid its base is the floor under x raised by
// its offset (MEDIUM(1).z), and under its base (under the lowest floor: the
// void past a ledge, a pit the grid smooths over) it fades within one more
// scale height, so no column holds an unbounded depth of it; without one
// (RPC environment {floor_grid: 0}, no level face) the base is the room's
// main floor and everything under it has the full density, as before.
float medium_fog_base(vec3 x) {
    if (MEDIUM(1).x <= 0.0) return 0.0;
    float height = max(MEDIUM(1).y, 1.0);
    if (MEDIUM(MEDIUM_FLOOR + 1).w < 0.5) return MEDIUM(1).x * exp(-max(MEDIUM(1).z - x.y, 0.0) / height);
    float up = medium_floor(x) - MEDIUM(1).z - x.y;
    return MEDIUM(1).x * exp(up >= 0.0 ? -up / height : -max(-up - height, 0.0) / height);
}
// A volume's field layer + 1 (0: none) and its shape (record: fill.w =
// layer + 1, + 8 for a sphere).
int medium_volume_layer(int b) { float w = MEDIUM(b + 3).w; return int((w > 7.5 ? w - 8.0 : w) + 0.5); }
bool medium_volume_sphere(int b) { return MEDIUM(b + 3).w > 7.5; }
#ifdef MEDIUM_TEX
// The interactive fog of layer `layer` at unit coordinates foot (-1..1 over
// the volume's footprint), bilinear: x its density multiplier (0..2), yz
// its velocity (world x, z units per second).
vec3 medium_sim(int layer, vec2 foot) {
    vec4 t = MEDIUM(MEDIUM_SIM);
    vec2 c = clamp((foot * 0.5 + 0.5) * t.x, vec2(0.5), vec2(t.x - 0.5));
    vec3 f = textureLod(MEDIUM_TEX, vec3(c / t.y, (65.0 + float(layer) + 0.5) / t.z), 0.0).rgb;
    return vec3(f.r * 2.0, (f.gb * 255.0 - 128.0) * (160.0 / 127.0));
}
float medium_detail_tex(vec3 q, int channel) {
    vec4 t = MEDIUM(MEDIUM_SIM);
    vec2 v = textureLod(MEDIUM_TEX, vec3((fract(q.xy) * 64.0 + 0.5) / t.y, (fract(q.z) * 64.0 + 0.5) / t.z), 0.0).rg;
    return channel == 0 ? v.x : v.y;
}
// The detail noise at x (0..1, mean 0.5): the baked Perlin-Worley texture
// drifting with the wind, evolving slowly; carried along by the fog's own
// flow (world x, z units per second) as a flow map does: two samples
// advected over staggered 2.5 s cycles, cross-faded, so the billows follow
// the wake and the swirls without stretching forever.
float medium_detail(vec3 x, vec2 flow) {
    float s = MEDIUM(MEDIUM_DETAIL).x, time = MEDIUM(0).z;
    vec3 q = (x - MEDIUM(4).xyz * time * 1.3) * s * vec3(1.0, 2.0, 1.0) + vec3(0.0, 0.0, time * 0.01);
    if (dot(flow, flow) < 0.25) return medium_detail_tex(q, 0);
    const float cycle = 2.5;
    float p0 = fract(time / cycle), p1 = fract(time / cycle + 0.5);
    vec3 f = vec3(flow.x, 0.0, flow.y) * s * cycle;
    float a = medium_detail_tex(q - f * p0, 0), b = medium_detail_tex(q - f * p1, 1);
    return mix(a, b, abs(2.0 * p0 - 1.0));
}
#endif
// A value 0..1 (a soft edge's, a cleared footprint's) eroded by the detail
// noise d (0..1) where it is in between: fine wisps on a coarse shape.
float medium_erode(float v, float d, float amount) {
    float c = clamp(v, 0.0, 1.0);
    return max(v + (d - 0.5) * amount * 3.0 * c * (1.0 - c), 0.0);
}
// Volume i's frame: x into its unit frame (r), the distance measure (> 1 outside).
float medium_volume_frame(int b, vec3 x, out vec3 r) {
    vec4 a = MEDIUM(b), half_size = MEDIUM(b + 1), e = MEDIUM(b + 4);
    r = x - a.xyz;
    r = vec3(e.x * r.x - e.y * r.z, r.y, e.y * r.x + e.x * r.z) / max(half_size.xyz, vec3(1.0)); // into its frame, unit size
    return medium_volume_sphere(b) ? length(r) : max(max(abs(r.x), abs(r.y)), abs(r.z));
}
// Volume i's density at x (its soft-edged shape, its noise, the interactive
// fog's field, the detail; n < 0: not computed yet).
float medium_volume(int i, vec3 x, inout float n) {
    int b = MEDIUM_VOLUME_BASE + 5 * i;
    vec3 r;
    float d = medium_volume_frame(b, x, r);
    if (d >= 1.0) return 0.0;
    float w = 1.0 - smoothstep(1.0 - clamp(MEDIUM(b + 1).w, 0.001, 1.0), 1.0, d);
    float amount = MEDIUM(b + 2).w;
    if (amount > 0.0) {
        if (n < 0.0) n = medium_noise(x);
        w *= medium_wisps(n, amount);
    }
#ifdef MEDIUM_TEX
    int layer = medium_volume_layer(b);
    vec3 f = layer > 0 ? medium_sim(layer - 1, r.xz) : vec3(1.0, 0.0, 0.0);
    float m = f.x, detail = MEDIUM(b + 4).w;
    if (detail > 0.0 && MEDIUM(MEDIUM_SIM).w > 0.5) {
        float dn = medium_detail(x, f.yz);
        w = medium_erode(w, dn, detail);
        m = m < 1.0 ? medium_erode(m, dn, detail) : m;
        w *= mix(1.0, 0.15 + 1.7 * smoothstep(0.2, 0.8, dn), detail * 0.8); // billows (mean about 1)
    }
    w *= m;
#endif
    return MEDIUM(b).w * w;
}
// Volume i's density without its noise and detail (its self-shadowing).
float medium_volume_coarse(int b, vec3 x) {
    vec3 r;
    float d = medium_volume_frame(b, x, r);
    if (d >= 1.0) return 0.0;
    float w = 1.0 - smoothstep(1.0 - clamp(MEDIUM(b + 1).w, 0.001, 1.0), 1.0, d);
#ifdef MEDIUM_TEX
    int layer = medium_volume_layer(b);
    if (layer > 0) w *= medium_sim(layer - 1, r.xz).x;
#endif
    return MEDIUM(b).w * w;
}
// The share of the main light (the sun or moon, else from above) that
// reaches x through volume i's own fog (its `shadow` strength): four steps
// toward the light inside it. Dense parts get dark cores, their rims stay lit.
float medium_volume_light(int b, vec3 x, float strength) {
    vec3 l = MEDIUM(MEDIUM_DETAIL).yzw;
    vec3 h = MEDIUM(b + 1).xyz;
    float reach = min(max(max(h.x, h.y), h.z), h.y * 2.4 / max(abs(l.y), 0.2));
    float step_ = reach * 0.25, depth = 0.0;
    for (int k = 0; k < 4; k++)
        depth += medium_volume_coarse(b, x + l * (step_ * (float(k) + 0.5)));
    return exp(-depth * step_ * strength);
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
        float lit = MEDIUM(b + 4).z > 0.0 ? medium_volume_light(b, x, MEDIUM(b + 4).z) : 1.0;
        extinction += d;
        scatter += d * MEDIUM(b + 2).rgb * lit;
        emit += d * MEDIUM(b + 3).rgb * mix(1.0, lit, 0.6);
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
