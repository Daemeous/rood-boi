#version 450
// Native VRAM to the window (no hardware target, or the 24-bit movie mode):
// BGR555 with the mask bit on top; in 24-bit mode R, G, B bytes packed
// across the halfwords, starting at the display's left edge.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform usampler2D vram;
layout(push_constant) uniform Push {
    ivec4 rect; // display x, y, width, height in VRAM pixels
    int rgb24;
} pc;
void main() {
    ivec2 p = ivec2(floor(uv * vec2(pc.rect.zw)));
    p = clamp(p, ivec2(0), pc.rect.zw - 1);
    if (pc.rgb24 != 0) {
        int row = (pc.rect.y + p.y) & 511, byte = pc.rect.x * 2 + p.x * 3;
        vec3 c;
        for (int i = 0; i < 3; i++, byte++) {
            uint v = texelFetch(vram, ivec2((byte >> 1) & 1023, row), 0).r;
            c[i] = float((byte & 1) != 0 ? v >> 8 : v & 255u) / 255.0;
        }
        color = vec4(c, 1.0);
        return;
    }
    uint v = texelFetch(vram, ivec2((pc.rect.x + p.x) & 1023, (pc.rect.y + p.y) & 511), 0).r;
    color = vec4(float(v & 31u), float((v >> 5) & 31u), float((v >> 10) & 31u), 31.0) / 31.0;
}
