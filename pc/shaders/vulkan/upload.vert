#version 450
// A native VRAM rectangle into the scaled target (uploads, LoadImage).
layout(push_constant) uniform Push {
    ivec4 rect;
    float scale;
} pc;
void main() {
    vec2 p = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    vec2 xy = vec2(pc.rect.xy) + p * vec2(pc.rect.zw);
    gl_Position = vec4(xy.x / 512.0 - 1.0, xy.y / 256.0 - 1.0, 0.0, 1.0);
}
