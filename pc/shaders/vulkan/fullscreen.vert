#version 450
// One triangle over the viewport; uv 0..1 across it with v = 0 at the top
// (Vulkan's framebuffer rows count from the top).
layout(location = 0) out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
