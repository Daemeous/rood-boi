#version 450
// Packet primitives into the scaled VRAM target (gpu_hw.c's draw shader).
layout(location = 0) in vec3 a_pos;
layout(location = 1) in uvec4 a_color;
layout(location = 2) in uvec2 a_uv;
layout(location = 3) in uvec2 a_page;
layout(location = 4) in uint a_window;
layout(location = 5) in vec4 a_normal;
layout(location = 6) in uvec4 a_bounds;
layout(location = 7) in ivec2 a_ui;
layout(push_constant) uniform Push {
    vec4 occlusion_rect;
    int perspective, filtering, occlusion, pass;
    int ui_scale;
} pc;
layout(location = 0) noperspective out vec3 v_color_affine;
layout(location = 1) noperspective out vec2 v_uv_affine;
layout(location = 2) smooth out vec3 v_color_persp;
layout(location = 3) smooth out vec2 v_uv_persp;
layout(location = 4) flat out uvec4 v_state;
layout(location = 5) flat out vec4 v_normal;
layout(location = 6) flat out uvec4 v_bounds;
layout(location = 7) smooth out float v_z_persp;
layout(location = 8) noperspective out float v_z_affine;
layout(location = 9) flat out ivec2 v_ui;
void main() {
    v_z_persp = v_z_affine = a_pos.z;
    v_normal = a_normal;
    v_bounds = a_bounds;
    v_ui = a_ui;
    v_color_affine = v_color_persp = vec3(a_color.rgb) / 255.0;
    v_uv_affine = v_uv_persp = vec2(a_uv);
    v_state = uvec4(a_color.a, a_page, a_window);
    // Depth: GTE SZ (0..65535) for 3D, nearest (0) for 2D/UI. The clip w only
    // changes interpolation (positions are divided back out).
    float depth = a_pos.z > 0.0 ? a_pos.z / 65536.0 : 0.0;
    float w = (pc.perspective != 0 && a_pos.z > 0.0) ? a_pos.z / 1024.0 : 1.0;
    gl_Position = vec4((a_pos.x / 512.0 - 1.0) * w, (a_pos.y / 256.0 - 1.0) * w, depth * w, w);
}
