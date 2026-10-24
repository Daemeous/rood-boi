// Included after Light; WINDOW_LIGHT(i) names the SSBO element. Aperture
// mode is an incident distant source, uniform in a finite rectangle of
// pane-relative angular slopes, NOT a diffuse emitter pretending to project.
// A receiver's ray cone cuts a rectangle on the pane. Integrate the runtime
// piecewise-constant mask exactly over it; condition shadow samples on the
// bright intersections. The primary lighting and tracing use the same weight.
#ifndef ROOD_WINDOW_PATTERN_GLSL
#define ROOD_WINDOW_PATTERN_GLSL
bool window_aperture(int i) { return WINDOW_LIGHT(i).info.z != 0; }
float window_cell(int i, int cell) {
    float next = WINDOW_LIGHT(i).pattern_cdf[cell >> 2][cell & 3];
    float prev = cell == 0 ? 0.0 : WINDOW_LIGHT(i).pattern_cdf[(cell - 1) >> 2][(cell - 1) & 3];
    return max(next - prev, 0.0);
}
bool window_footprint(int i, vec3 p, out vec2 lo, out vec2 hi) {
    vec3 normal = WINDOW_LIGHT(i).dir.xyz, incident = WINDOW_LIGHT(i).pattern_dir.xyz;
    vec3 rel = p - WINDOW_LIGHT(i).pos.xyz;
    float z = dot(rel, normal), facing = dot(incident, normal);
    if (z <= 0.0 || facing <= 0.001) { lo = hi = vec2(0.0); return false; }
    vec3 on_plane = rel - incident * (z / facing);
    vec3 right = WINDOW_LIGHT(i).right.xyz, up = WINDOW_LIGHT(i).up.xyz;
    vec2 half_size = max(vec2(length(right), length(up)), vec2(0.001));
    vec2 uv = 0.5 + 0.5 * vec2(dot(on_plane, right), dot(on_plane, up)) / (half_size * half_size);
    vec2 extent = max(vec2(0.000001), 0.5 * z * WINDOW_LIGHT(i).pattern_dir.w / half_size);
    lo = uv - extent; hi = uv + extent;
    return true;
}
// Integral in UV area. The CDF is normalized total flux, so cell density is
// probability * side^2 (uniform unmasked aperture = 1 everywhere inside).
float window_integral(int i, vec2 lo, vec2 hi) {
    int side = WINDOW_LIGHT(i).info.y;
    if (side <= 0) return 0.0;
    ivec2 first = max(ivec2(floor(lo * side)), ivec2(0));
    ivec2 last = min(ivec2(floor(hi * side)), ivec2(side - 1));
    float sum = 0.0;
    for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x) {
        vec2 a = max(lo, vec2(x, y) / float(side));
        vec2 b = min(hi, vec2(x + 1, y + 1) / float(side));
        vec2 extent = max(b - a, vec2(0.0));
        sum += window_cell(i, y * side + x) * float(side * side) * extent.x * extent.y;
    }
    return sum;
}
float window_transmission(int i, vec3 p) {
    vec2 lo, hi;
    if (!window_footprint(i, p, lo, hi)) return 0.0;
    return window_integral(i, lo, hi) / max((hi.x - lo.x) * (hi.y - lo.y), 1e-20);
}
vec3 window_centre(int i, vec3 p) {
    vec3 incident = WINDOW_LIGHT(i).pattern_dir.xyz;
    float z = dot(p - WINDOW_LIGHT(i).pos.xyz, WINDOW_LIGHT(i).dir.xyz);
    return p - incident * (z / max(dot(incident, WINDOW_LIGHT(i).dir.xyz), 0.001));
}
vec3 window_sample(int i, vec3 p, vec2 random_) {
    vec2 lo, hi;
    if (!window_footprint(i, p, lo, hi)) return WINDOW_LIGHT(i).pos.xyz;
    float total = window_integral(i, lo, hi), pick = random_.x * total;
    int side = WINDOW_LIGHT(i).info.y;
    if (side <= 0 || total <= 0.0) return window_centre(i, p);
    ivec2 first = max(ivec2(floor(lo * side)), ivec2(0));
    ivec2 last = min(ivec2(floor(hi * side)), ivec2(side - 1));
    for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x) {
        vec2 a = max(lo, vec2(x, y) / float(side));
        vec2 b = min(hi, vec2(x + 1, y + 1) / float(side));
        vec2 extent = max(b - a, vec2(0.0));
        float weight = window_cell(i, y * side + x) * float(side * side) * extent.x * extent.y;
        if (weight > 0.0 && pick < weight) {
            vec2 xy = (a + extent * vec2(pick / weight, random_.y)) * 2.0 - 1.0;
            return WINDOW_LIGHT(i).pos.xyz + WINDOW_LIGHT(i).right.xyz * xy.x + WINDOW_LIGHT(i).up.xyz * xy.y;
        }
        pick -= weight;
    }
    return window_centre(i, p);
}
#endif
