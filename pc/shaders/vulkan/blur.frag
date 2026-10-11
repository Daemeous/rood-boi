#version 450
// Depth of field / tilt-shift blur over the lit image (gl_present.c's blur
// pass): rgb, a = signed blur radius in pixels, 9999 for UI.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D lit;
layout(push_constant) uniform Push {
    vec2 view;
    float max_radius;
} pc;
const float golden = 2.39996323;
void main() {
    vec2 st = uv; // the intermediate image's rows count from the top, as uv does
    vec4 c0 = texture(lit, st);
    if (c0.a > 9000.0) { color = vec4(c0.rgb, 1.0); return; }
    float r0 = abs(c0.a), radius = r0;
    for (int i = 0; i < 12; i++) {
        float a = float(i) * 0.5236;
        vec4 t = texture(lit, st + vec2(cos(a), -sin(a)) * pc.max_radius * (i % 2 == 0 ? 0.5 : 1.0) / pc.view);
        if (t.a < 0.0 && t.a < c0.a) radius = max(radius, -t.a);
    }
    if (radius < 0.5) { color = vec4(c0.rgb, 1.0); return; }
    vec3 sum = c0.rgb;
    float weight = 1.0;
    for (int i = 0; i < 48; i++) {
        float r = sqrt((float(i) + 0.5) / 48.0) * radius, a = float(i) * golden;
        vec2 o = vec2(cos(a), -sin(a)) * r; // y mirrored: GL's rows count from the bottom
        vec4 t = texture(lit, st + o / pc.view);
        if (t.a > 9000.0) continue;
        float rt = abs(t.a);
        if (t.a > c0.a) rt = min(rt, r0);
        float w = clamp(rt - r + 1.0, 0.0, 1.0);
        sum += t.rgb * w;
        weight += w;
    }
    color = vec4(sum / weight, 1.0);
}
