#version 450
// Shadow maps without ray tracing (vk_shadows.c): world-space room and
// caster triangles into one cube face of a light (GL cube conventions; the
// atlas tile is chosen by the viewport).
layout(location = 0) in vec3 world;
layout(push_constant) uniform Push {
    mat4 face; // projection * face view * translate(-light), GL depth range
    vec4 light; // xyz, radius
} pc;
layout(location = 0) out vec3 v_world;
void main() {
    v_world = world;
    gl_Position = pc.face * vec4(world, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5; // GL -1..1 depth to Vulkan 0..1
}
