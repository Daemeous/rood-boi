#version 450
// The text overlay (gpu_hw.c, ui_text_overlay): text and icons from distance
// texels drawn at the window's resolution over the presented picture. The
// vertices are gpu_hw.c's (VRAM pixels, widescreen placement applied); the
// presented area `rect` maps onto the viewport.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in uvec4 a_color;
layout(location = 2) in uvec2 a_uv;
layout(location = 3) in uvec2 a_page;
layout(location = 4) in uint a_window;
layout(location = 6) in uvec4 a_bounds;
layout(location = 7) in ivec2 a_ui;
layout(push_constant) uniform Push {
    vec4 rect; // the presented area: x, y, width, height (VRAM pixels)
    int pass, ui_scale; // 4: opaque texels "over", 5: translucent texels by the PS1 mode
    float text_factor; // pass 5: the PS1 mode's alpha factor
} pc;
layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out uvec4 v_state;
layout(location = 3) flat out uvec4 v_bounds;
layout(location = 4) flat out ivec2 v_ui;
void main() {
    v_color = vec3(a_color.rgb) / 255.0;
    v_uv = vec2(a_uv);
    v_state = uvec4(a_color.a, a_page, a_window);
    v_bounds = a_bounds;
    v_ui = a_ui;
    vec2 p = (a_pos.xy - pc.rect.xy) / pc.rect.zw;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); // (Vulkan: y down, the presented area's top row first)
}
