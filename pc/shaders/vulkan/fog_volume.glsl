// Hand-placed fog volumes' own renderer (atmosphere.h pc_fogvol_gpu;
// pc/docs/plans/2026-10-14-fog-volume-renderer.md): a ray march over each
// volume's bounds (box or sphere, turned), its density the volume's shape
// and height profile times the interactive fog's 2D field times the baked
// 3D detail noise carried by the field's flow (medium.glsl), lit per sample
// by the few lights that matter to it (lights, glows, the save point's
// column), the sun or moon and an ambient term (the room's baked light
// around it), each through the volume's own shadow (fv_shadows, the
// includer's: Vulkan samples a low-resolution transmittance grid, OpenGL an
// analytic estimate), with a two-lobe phase function and a cheap multiple-
// scattering octave; its own glow (lit from within). Shared by the Vulkan
// passes (fogvol_march.comp, fogvol_light.glsl) and OpenGL's present.
//
// The includer includes medium.glsl first (MEDIUM, MEDIUM_TEX) and defines
// FOGVOL(i), the i-th vec4 of the record:
//  0: x the volumes (<= FV_MAX), y the lights' halo (extra light near a source), z the frame, w -
//  1: the sun's (or moon's) light rgb, w 1 on
//  2: the direction toward it, w the multiple-scattering octave's weight
//  3 + FV_STRIDE v: volume v (its shape: the medium record's volume v):
//     +0: x the most opacity along a ray (0..1), y brightness, z phase g, w its lights
//     +1: glow rgb (light it emits per unit density), w how much it gathers at its base (0 even)
//     +2: ambient light rgb (around it: the room's baked light, the sky), w taper (0 a box's
//         even density .. 1 thinning toward its top)
//     +3: x 1 / its detail period (world units), y self-shadowing strength, z -, w its grid slab (-1 none)
//     +4 + 3 i: light i: xyz position, w radius; rgb colour x intensity, w type (0 point, 1 spot,
//         2 rect, 3 glow, 4 a glow's column: dir = its half axis); xyz direction, w cos(half cone)
#ifndef FOG_VOLUME_GLSL
#define FOG_VOLUME_GLSL
#define FV_MAX 8
#define FV_LIGHTS 6
#define FV_STRIDE (4 + 3 * FV_LIGHTS)
#define FV_HEADER 3
#define FV_VEC4S (FV_HEADER + FV_MAX * FV_STRIDE)
// The includer's: volume v's shadow at x (unit-frame r) toward its lights 0-3 (ta), 4, 5, the sun and
// the ambient term (tb), 0 dark .. 1 lit.
void fv_shadows(int v, vec3 x, vec3 r, out vec4 ta, out vec4 tb);
int fv_count() { return int(FOGVOL(0).x + 0.5); }
int fv_base(int v) { return FV_HEADER + FV_STRIDE * v; }
// Volume v's first vec4 in the medium record (medium.glsl: centre + density, ...).
int fv_medium(int v) { return MEDIUM_VOLUME_BASE + 5 * v; }
// The phase function (x 4 pi; Schlick's fit of Henyey-Greenstein, no pow): mu = dot(view
// direction, toward the light), forward scattering (g > 0) brightest looking toward the light.
float fv_phase(float mu, float g) {
    float k = 1.55 * g - 0.55 * g * g * g, d = 1.0 - k * mu;
    return min((1.0 - k * k) / max(d * d, 1e-4), 12.0);
}
// Into volume v's unit frame (r) a point and a direction.
vec3 fv_local(int b, vec3 p) {
    vec4 e = MEDIUM(b + 4);
    return vec3(e.x * p.x - e.y * p.z, p.y, e.y * p.x + e.x * p.z) / max(MEDIUM(b + 1).xyz, vec3(1.0));
}
// The part [enter, leave] of the ray o + d t (t in [0, t_max]) inside volume v (enter > leave: none).
vec2 fv_hit(int v, vec3 o, vec3 d, float t_max) {
    int b = fv_medium(v);
    vec3 ro = fv_local(b, o - MEDIUM(b).xyz), rd = fv_local(b, d);
    float a, z;
    if (medium_volume_sphere(b)) {
        float A = max(dot(rd, rd), 1e-12), B = dot(ro, rd), C = dot(ro, ro) - 1.0;
        float disc = B * B - A * C;
        if (disc <= 0.0) return vec2(1.0, 0.0);
        float s = sqrt(disc);
        a = (-B - s) / A, z = (-B + s) / A;
    } else {
        vec3 safe = vec3(abs(rd.x) < 1e-9 ? 1e-9 : rd.x, abs(rd.y) < 1e-9 ? 1e-9 : rd.y, abs(rd.z) < 1e-9 ? 1e-9 : rd.z);
        vec3 inv = 1.0 / safe;
        vec3 t0 = (-1.0 - ro) * inv, t1 = (1.0 - ro) * inv;
        vec3 lo = min(t0, t1), hi = max(t0, t1);
        a = max(max(lo.x, lo.y), lo.z), z = min(min(hi.x, hi.y), hi.z);
    }
    return vec2(max(a, 0.0), min(z, t_max));
}
// The volumes' wisps (0..1, mean about 0.5): the baked noise at the wisp size (four wisps a period),
// drifting with the wind, squashed vertically; one texture fetch where medium_noise costs four
// octaves of gradient noise (the haze keeps that).
float fv_noise(vec3 x) {
#ifdef MEDIUM_TEX
    if (MEDIUM(MEDIUM_SIM).w > 0.5) {
        vec3 q = (x - MEDIUM(4).xyz * MEDIUM(0).z) * (MEDIUM(2).w * 0.25) * vec3(1.0, 2.2, 1.0);
        return medium_detail_tex(q + vec3(0.37, 0.11, 0.73), 1);
    }
#endif
    return medium_noise(x);
}
// Volume v's density at x (n: fv_noise at x, < 0 not computed yet), r its unit-frame position.
float fv_density(int v, vec3 x, inout float n, out vec3 r) {
    int b = fv_medium(v), f = fv_base(v);
    float d = medium_volume_frame(b, x, r);
    if (d >= 1.0) return 0.0;
    if (!medium_volume_sphere(b)) { // a box's footprint with rounded corners (a bank of fog, not a slab)
        vec2 h = r.xz * r.xz;
        h = h * h * h;
        d = max(pow(h.x + h.y, 1.0 / 6.0), abs(r.y));
        if (d >= 1.0) return 0.0;
    }
    float w = 1.0 - smoothstep(1.0 - clamp(MEDIUM(b + 1).w, 0.001, 1.0), 1.0, d);
    // "Confine to room": the room's interior only (medium.glsl medium_confine; the haze's soft edge and
    // under-floor fade)
    if (medium_volume_confined(b)) { w *= medium_confine(x); if (w <= 0.0) return 0.0; }
    // the height profile: thinning toward the top (y down: r.y -1 the top, +1 the base)
    float taper = FOGVOL(f + 2).w;
    if (taper > 0.0) {
        float up = clamp(0.5 - 0.5 * r.y, 0.0, 1.0);
        w *= mix(1.0, (1.0 - up) * (1.0 - up), taper);
    }
    float amount = MEDIUM(b + 2).w;
    if (amount > 0.0) {
        if (n < 0.0) n = fv_noise(x);
        w *= medium_wisps(n, amount);
    }
#ifdef MEDIUM_TEX
    int layer = medium_volume_layer(b);
    vec3 field = layer > 0 ? medium_sim(layer - 1, r.xz) : vec3(1.0, 0.0, 0.0);
    float m = field.x, detail = MEDIUM(b + 4).w;
    if (detail > 0.0 && MEDIUM(MEDIUM_SIM).w > 0.5) {
        // two octaves of the baked Perlin-Worley noise, carried along by the field's flow
        float s = FOGVOL(f + 3).x;
        float dn = mix(medium_detail_s(x, field.yz, s), medium_detail_s(x + vec3(37.0, 11.0, 53.0) / s, field.yz, s * 2.7), 0.35);
        w = medium_erode(w, dn, detail);
        m = m < 1.0 ? medium_erode(m, dn, detail) : m;
        w *= mix(1.0, 0.08 + 1.84 * smoothstep(0.25, 0.75, dn), detail * 0.9); // billows (mean about 1)
    }
    w *= m;
#endif
    return MEDIUM(b).w * w;
}
// Light slot i of volume v at x: its unshadowed light, the direction toward it, its distance.
vec3 fv_light(int v, int i, vec3 x, out vec3 l, out float dist) {
    int k = fv_base(v) + 4 + 3 * i;
    vec4 p = FOGVOL(k), c = FOGVOL(k + 1), dr = FOGVOL(k + 2);
    vec3 centre = p.xyz;
    if (c.w > 3.5) // a glow's column (the save point): its nearest point on the axis
        centre += dr.xyz * clamp(dot(x - p.xyz, dr.xyz) / max(dot(dr.xyz, dr.xyz), 1e-6), -1.0, 1.0);
    vec3 to = centre - x;
    dist = length(to);
    l = to / max(dist, 1e-3);
    float f = clamp(dist / max(p.w, 1.0), 0.0, 1.0), near = dist * (1.0 / 96.0);
    // the lights' falloff, with a halo near the source (FOGVOL(0).y): a fire glows in the smoke around it
    float a = (1.0 - f * f) * (1.0 - f * f) * (1.0 + FOGVOL(0).y / (1.0 + near * near));
    if (c.w > 1.5 && c.w < 2.5) a *= max(dot(-l, dr.xyz), 0.0); // rect: its front
    else if (c.w > 0.5 && c.w < 1.5) a *= smoothstep(dr.w, mix(dr.w, 1.0, 0.25), dot(-l, dr.xyz)); // spot
    return c.rgb * a;
}
// The light volume v scatters toward the eye per unit length at x (density d, view direction dir),
// its shadows ta (lights 0-3), tb (lights 4, 5, the sun, the ambient term).
vec3 fv_shade(int v, vec3 x, vec3 dir, float d, vec3 r, vec4 ta, vec4 tb) {
    int b = fv_medium(v), f = fv_base(v);
    float g = FOGVOL(f).z, ms = FOGVOL(2).w;
    vec3 lit = vec3(0.0);
    int lights = int(FOGVOL(f).w + 0.5);
    for (int i = 0; i < lights && i < FV_LIGHTS; i++) {
        vec3 l; float dist;
        vec3 c = fv_light(v, i, x, l, dist);
        if (c.r + c.g + c.b <= 0.0) continue;
        float t = i < 4 ? ta[i] : tb[i - 4], mu = dot(dir, l);
        lit += c * (fv_phase(mu, g) * t + ms * fv_phase(mu, g * 0.5) * sqrt(t));
    }
    if (FOGVOL(1).w > 0.5) {
        float mu = dot(dir, FOGVOL(2).xyz);
        lit += FOGVOL(1).rgb * (fv_phase(mu, g) * tb.z + ms * fv_phase(mu, g * 0.5) * sqrt(tb.z));
    }
    lit += FOGVOL(f + 2).rgb * (0.5 + 0.5 * tb.w); // ambient (isotropic), a little dimmer in its dense core
    vec3 glow = FOGVOL(f + 1).rgb;
    if (glow.r + glow.g + glow.b > 0.0) {
        vec3 base = r - vec3(0.0, 1.0, 0.0); // gathered at its base (y down)
        glow *= mix(1.0, 2.2 * exp(-2.0 * dot(base, base)), FOGVOL(f + 1).w);
    }
    return d * (MEDIUM(b + 2).rgb * lit + MEDIUM(b + 3).rgb + glow) * FOGVOL(f).y;
}
// The march: from eye along dir (unit) up to t_max (the scene's depth), `steps` samples spread over
// the parts of the ray inside volumes (empty space skipped), offset by jitter (0..1); the volumes
// confined to the room only up to t_confined (medium.glsl medium_confined_reach: a void pixel's reach).
// Returns the light scattered toward the eye (rgb) and the transmittance (a); fog_t: where its light
// comes from (transmittance-weighted distance; the reprojection's depth), t_max when it has none.
vec4 fv_march(vec3 eye, vec3 dir, float t_max, float t_confined, float jitter, int steps, out float fog_t) {
    int count = min(fv_count(), FV_MAX);
    float enter[FV_MAX], leave[FV_MAX];
    int spans = 0;
    float least = 1.0;
    for (int v = 0; v < count; v++) {
        vec2 s = fv_hit(v, eye, dir, medium_volume_confined(fv_medium(v)) ? min(t_max, t_confined) : t_max);
        if (!(s.y > s.x)) continue;
        // sorted by entry, overlapping spans merged
        int i = spans;
        while (i > 0 && enter[i - 1] > s.x) { enter[i] = enter[i - 1]; leave[i] = leave[i - 1]; i--; }
        enter[i] = s.x, leave[i] = s.y;
        spans++;
        least = min(least, 1.0 - clamp(FOGVOL(fv_base(v)).x, 0.0, 1.0));
    }
    fog_t = t_max;
    if (spans == 0) return vec4(0.0, 0.0, 0.0, 1.0);
    int merged = 0;
    for (int i = 0; i < spans; i++) {
        if (merged > 0 && enter[i] <= leave[merged - 1]) leave[merged - 1] = max(leave[merged - 1], leave[i]);
        else { enter[merged] = enter[i]; leave[merged] = leave[i]; merged++; }
    }
    float total = 0.0;
    for (int i = 0; i < merged; i++) total += leave[i] - enter[i];
    float step_ = max(total / float(max(steps, 1)), 1.0);
    vec3 light = vec3(0.0);
    float transmittance = 1.0, weight = 0.0, weighted_t = 0.0;
    int budget = steps + merged;
    for (int s = 0; s < merged && budget > 0 && transmittance > 0.003; s++) {
        float len = leave[s] - enter[s];
        int n = max(int(ceil(len / step_ - 1e-3)), 1);
        float ds = len / float(n);
        for (int k = 0; k < n && budget > 0; k++, budget--) {
            float t = enter[s] + (float(k) + jitter) * ds;
            vec3 x = eye + dir * t;
            float noise = -1.0, extinction = 0.0;
            vec3 scattered = vec3(0.0);
            for (int v = 0; v < count; v++) {
                if (t > t_confined && medium_volume_confined(fv_medium(v))) continue;
                vec3 r;
                float d = fv_density(v, x, noise, r);
                if (d <= 0.0) continue;
                vec4 ta, tb;
                fv_shadows(v, x, r, ta, tb);
                extinction += d;
                scattered += fv_shade(v, x, dir, d, r, ta, tb);
            }
            if (extinction <= 1e-7) continue;
            float a = exp(-extinction * ds);
            if (least < 1.0) a = max(a, min(least / max(transmittance, 1e-6), 1.0)); // its own opacity cap
            light += transmittance * scattered * ((1.0 - a) / extinction);
            weight += transmittance * (1.0 - a);
            weighted_t += transmittance * (1.0 - a) * t;
            transmittance *= a;
            if (transmittance <= 0.003) break;
        }
    }
    if (weight > 1e-4) fog_t = weighted_t / weight;
    if (any(isnan(light)) || any(isinf(light))) light = vec3(0.0);
    return vec4(max(light, vec3(0.0)), clamp(transmittance, 0.0, 1.0));
}
#endif
