#version 450
// A state file's picture of the target (snapshot.c: 8 bits a channel at the 1x grid) into the scaled target.
layout(push_constant) uniform Push {
    ivec4 rect;
    float scale;
} pc;
layout(binding = 0) uniform sampler2D picture;
layout(location = 0) out vec4 color;
void main() {
    ivec2 p = ivec2(floor(gl_FragCoord.xy / pc.scale));
    color = texelFetch(picture, ivec2(p.x & 1023, p.y & 511), 0);
}
