#version 450
#extension GL_GOOGLE_include_directive : require
// Brightness (vk_present.c): the post chain's last image (16-bit float, the
// view's size) through brightness.glsl into the window's view rectangle.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D image;
layout(push_constant) uniform Push {
    ivec2 origin; // the view's top-left corner in the window
    float factor; // the curve's slope at black (brightness.glsl)
} pc;
#include "brightness.glsl"
void main() {
    color = vec4(brightness(texelFetch(image, ivec2(gl_FragCoord.xy) - pc.origin, 0).rgb, pc.factor), 1.0);
}
