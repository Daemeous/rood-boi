#version 450
// FXAA (3.11-style), as gl_present.c's: find the edge through each pixel
// from luma contrast, search along it for its ends, resample across it.
// Alpha (the depth of field's blur radius) passes through.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D image;
layout(push_constant) uniform Push {
    vec2 texel;
} pc;
float luma(vec2 p) { return dot(texture(image, p).rgb, vec3(0.299, 0.587, 0.114)); }
void main() {
    // GL samples with rows counting from the bottom (st.y = 1 - uv.y, "up" is
    // +texel.y); here rows count from the top, so "up" is -texel.y. Mirroring
    // the y axis keeps the same neighbours in the same roles.
    vec2 st = uv;
    vec2 texel = vec2(pc.texel.x, -pc.texel.y);
    vec4 centre = texture(image, st);
    vec3 middle = centre.rgb;
    float m = dot(middle, vec3(0.299, 0.587, 0.114));
    float n = luma(st + vec2(0.0, texel.y)), s = luma(st - vec2(0.0, texel.y));
    float e = luma(st + vec2(texel.x, 0.0)), w = luma(st - vec2(texel.x, 0.0));
    float high = max(m, max(max(n, s), max(e, w))), low = min(m, min(min(n, s), min(e, w)));
    float range = high - low;
    if (range < max(0.0312, high * 0.125)) { color = vec4(middle, centre.a); return; }
    float nw = luma(st + vec2(-texel.x, texel.y)), ne = luma(st + texel);
    float sw = luma(st - texel), se = luma(st + vec2(texel.x, -texel.y));
    float horizontal = abs(-2.0 * w + nw + sw) + 2.0 * abs(-2.0 * m + n + s) + abs(-2.0 * e + ne + se);
    float vertical = abs(-2.0 * n + nw + ne) + 2.0 * abs(-2.0 * m + w + e) + abs(-2.0 * s + sw + se);
    bool across_y = horizontal >= vertical;
    float l1 = across_y ? s : w, l2 = across_y ? n : e;
    float g1 = l1 - m, g2 = l2 - m;
    bool first = abs(g1) >= abs(g2);
    float gradient = 0.25 * max(abs(g1), abs(g2));
    float step_size = across_y ? texel.y : texel.x;
    float local = 0.5 * ((first ? l1 : l2) + m);
    if (first) step_size = -step_size;
    vec2 edge = st + (across_y ? vec2(0.0, 0.5 * step_size) : vec2(0.5 * step_size, 0.0));
    vec2 along = across_y ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 p1 = edge - along, p2 = edge + along;
    float e1 = luma(p1) - local, e2 = luma(p2) - local;
    bool done1 = abs(e1) >= gradient, done2 = abs(e2) >= gradient;
    const float quality[12] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);
    for (int i = 0; i < 12 && !(done1 && done2); i++) {
        if (!done1) { p1 -= along * quality[i]; e1 = luma(p1) - local; done1 = abs(e1) >= gradient; }
        if (!done2) { p2 += along * quality[i]; e2 = luma(p2) - local; done2 = abs(e2) >= gradient; }
    }
    // Distances along the edge (positive whichever way texel.y points).
    float d1 = across_y ? st.x - p1.x : (st.y - p1.y) / sign(texel.y), d2 = across_y ? p2.x - st.x : (p2.y - st.y) / sign(texel.y);
    bool nearer1 = d1 < d2;
    float offset = 0.5 - min(d1, d2) / (d1 + d2);
    bool correct = ((nearer1 ? e1 : e2) < 0.0) != (m < local);
    offset = correct ? offset : 0.0;
    float average = (2.0 * (n + s + e + w) + nw + ne + sw + se) / 12.0;
    float sub = clamp(abs(average - m) / range, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub;
    offset = max(offset, sub * sub * 0.75);
    vec2 p = st + (across_y ? vec2(0.0, offset * step_size) : vec2(offset * step_size, 0.0));
    color = vec4(texture(image, p).rgb, centre.a); // alpha: the blur radius for depth of field, after
}
