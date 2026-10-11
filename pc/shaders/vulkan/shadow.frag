#version 450
// Distance to the light over its radius (shadows.c's cube maps).
layout(location = 0) in vec3 v_world;
layout(push_constant) uniform Push {
    mat4 face;
    vec4 light;
} pc;
layout(location = 0) out vec4 distance_;
void main() { distance_ = vec4(length(v_world - pc.light.xyz) / pc.light.w); }
