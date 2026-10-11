// Probe-based GI (DDGI: Majercik et al. 2019, "Dynamic Diffuse Global
// Illumination with Ray-Traced Irradiance Fields"): the probe grid, the
// octahedral atlases and the irradiance lookup with Chebyshev visibility.
// Plain GLSL 3.30 (the OpenGL present shader embeds it too): the includer
// declares pv_origin, pv_spacing, pv_counts and pv_bias (probe_volume.glsl
// or plain uniforms) and, with PROBE_SAMPLING defined (the lookup), the
// samplers probe_irradiance, probe_depth and probe_data (all clamp to edge;
// the atlases linear, the data nearest).
//
// Layout: probe p = x + nx * (y + ny * z) sits in atlas tile (p % columns,
// p / columns). Irradiance tiles are 8 x 8 octahedral texels plus a one-texel
// border (10 x 10), visibility tiles 16 x 16 plus border (18 x 18): rg the
// mean and mean square of the distance to the nearest surface, in units of
// pv_spacing.w (clamped at 2). probe_data holds one texel per probe: xyz its
// offset from the grid point (relocation), w its per-frame light scale
// (flicker), 0 when the probe is inactive (inside geometry, or nothing near).
#ifndef PROBE_COMMON_GLSL
#define PROBE_COMMON_GLSL
#define PROBE_IRRADIANCE 8
#define PROBE_DEPTH 16
#define PV_RELOCATE 1
#define PV_CLASSIFY 2
#define PV_RESET 4
#define PV_FRONT_CCW 8

float probe_sign_not_zero(float v) { return v >= 0.0 ? 1.0 : -1.0; }
vec2 probe_sign_not_zero(vec2 v) { return vec2(probe_sign_not_zero(v.x), probe_sign_not_zero(v.y)); }
// Unit direction -> octahedral [-1, 1]^2.
vec2 probe_oct_encode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.xy;
    if (n.z < 0.0) e = (1.0 - abs(e.yx)) * probe_sign_not_zero(e);
    return e;
}
vec3 probe_oct_decode(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * probe_sign_not_zero(n.xy);
    return normalize(n);
}
int probe_count() { return pv_counts.x * pv_counts.y * pv_counts.z; }
ivec3 probe_coords(int p) { return ivec3(p % pv_counts.x, (p / pv_counts.x) % pv_counts.y, p / (pv_counts.x * pv_counts.y)); }
int probe_index(ivec3 c) { return c.x + pv_counts.x * (c.y + pv_counts.y * c.z); }
ivec2 probe_tile(int p) { return ivec2(p % pv_counts.w, p / pv_counts.w); }
vec3 probe_grid_position(ivec3 c) { return pv_origin.xyz + vec3(c) * pv_spacing.xyz; }
// Atlas uv of direction d in probe p's tile (texel size `tile` incl. border).
vec2 probe_atlas_uv(int p, vec3 d, int interior, vec2 atlas_size) {
    vec2 corner = vec2(probe_tile(p) * (interior + 2)) + 1.0;
    return (corner + (probe_oct_encode(d) * 0.5 + 0.5) * float(interior)) / atlas_size;
}
#ifdef PROBE_SAMPLING
// Irradiance at world point p with surface normal n, seen along view (unit,
// from the eye to p): the eight probes of its cell, weighted trilinearly, by
// a wrapped cosine toward the probe and by the Chebyshev visibility of the
// biased point from each probe; inactive probes are left out.
vec3 probe_irradiance_at(vec3 p, vec3 n, vec3 view) {
    vec3 biased = p + n * pv_bias.x - view * pv_bias.y;
    vec3 cell = clamp((biased - pv_origin.xyz) / pv_spacing.xyz, vec3(0.0), vec3(pv_counts.xyz - 1));
    ivec3 base = min(ivec3(cell), pv_counts.xyz - 2); // counts >= 2 on every axis
    vec3 alpha = cell - vec3(base);
    vec2 irradiance_texel = 1.0 / vec2(textureSize(probe_irradiance, 0));
    vec2 depth_texel = 1.0 / vec2(textureSize(probe_depth, 0));
    vec2 normal_uv = (probe_oct_encode(n) * 0.5 + 0.5) * float(PROBE_IRRADIANCE) + 1.0; // in a tile
    int base_index = probe_index(base);
    float depth_scale = 1.0 / pv_spacing.w;
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(i, i >> 1, i >> 2) & 1;
        int index = base_index + offset.x + pv_counts.x * (offset.y + pv_counts.y * offset.z);
        ivec2 tile = ivec2(index % pv_counts.w, index / pv_counts.w);
        vec4 data = texelFetch(probe_data, tile, 0);
        if (data.w <= 0.0) continue; // inactive
        vec3 from_probe = biased - (pv_origin.xyz + vec3(base + offset) * pv_spacing.xyz + data.xyz);
        float distance = length(from_probe);
        vec3 direction = from_probe / max(distance, 1e-4);
        // Smooth backface test: probes behind the surface count less.
        float wrap = (0.5 - 0.5 * dot(direction, n));
        float weight = wrap * wrap + 0.2;
        // Visibility: the biased point's distance against the probe's depth moments.
        vec2 depth_uv = (vec2(tile * (PROBE_DEPTH + 2)) + 1.0 + (probe_oct_encode(direction) * 0.5 + 0.5) * float(PROBE_DEPTH)) * depth_texel;
        vec2 moments = textureLod(probe_depth, depth_uv, 0.0).rg;
        distance *= depth_scale;
        if (distance > moments.x) {
            float variance = abs(moments.y - moments.x * moments.x);
            float d = distance - moments.x;
            float chebyshev = variance / (variance + d * d);
            weight *= max(chebyshev * chebyshev * chebyshev, 0.05);
        }
        weight = max(weight, 1e-6);
        // Crush tiny weights (light leaks through thin walls).
        const float crush = 0.2;
        if (weight < crush) weight *= weight * weight / (crush * crush);
        vec3 trilinear = mix(1.0 - alpha, alpha, vec3(offset));
        weight *= trilinear.x * trilinear.y * trilinear.z;
        vec3 irradiance = textureLod(probe_irradiance, (vec2(tile * (PROBE_IRRADIANCE + 2)) + normal_uv) * irradiance_texel, 0.0).rgb;
        sum += irradiance * data.w * weight;
        weights += weight;
    }
    return weights > 0.0 ? sum / weights : vec3(0.0);
}
#endif
#endif
