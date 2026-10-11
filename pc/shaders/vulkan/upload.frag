#version 450
// A native VRAM rectangle into the scaled target (uploads, LoadImage).
layout(push_constant) uniform Push {
    ivec4 rect;
    float scale;
} pc;
layout(binding = 0) uniform usampler2D vram;
layout(location = 0) out vec4 color;
void main() {
    ivec2 p = ivec2(floor(gl_FragCoord.xy / pc.scale));
    uint t = texelFetch(vram, ivec2(p.x & 1023, p.y & 511), 0).r;
    color = vec4(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u), 31.0 * float(t >> 15)) / 31.0;
}
