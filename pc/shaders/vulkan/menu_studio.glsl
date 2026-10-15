// The modern menu's model view (gpu_hw.c pc_hw_present_menu_studio): the
// model is lit by a studio rig of its own, the same in every room and on
// every screen that shows it, instead of the room's lights, GI and haze and
// the game's colour for it (the vertex colours, and the offset copy the
// game draws behind it as a rim, which ui/ashley_bridge.c leaves out). The
// texture's colour (albedo, texels as the PS1 shows them at vertex colour
// 128) under a warm key from the upper left in front, a cool fill from the
// right, a sky/ground ambient and a rim from behind on the upper right.
// View space: x right, y down, z away from the eye; n faces the eye.
// OpenGL splices this into gl_present.c's present shader (gl_shaders.py),
// Vulkan includes it in scaled.frag.
#ifndef MENU_STUDIO_GLSL
#define MENU_STUDIO_GLSL
vec3 menu_studio(vec3 albedo, vec3 n, vec3 p) {
    vec3 v = normalize(-p);
    vec3 key = normalize(vec3(-0.45, -0.5, -0.74));
    vec3 fill = normalize(vec3(0.85, -0.05, -0.52));
    vec3 rim = normalize(vec3(0.5, -0.55, 0.67));
    // Gentle: the texture carries the picture (as on the PS1), the light
    // only shapes it (front about 1, the far side about 0.7), so the
    // low-poly faces never turn into hard facets.
    float k = max(dot(n, key) * 0.6 + 0.4, 0.0);
    float f = max(dot(n, fill), 0.0);
    float up = 0.5 - 0.5 * n.y;
    float r = max(dot(n, rim) * 0.8 + 0.2, 0.0) * pow(1.0 - max(dot(n, v), 0.0), 2.0);
    vec3 light = vec3(1.0, 0.97, 0.92) * 0.5 * k + vec3(0.9, 0.95, 1.0) * 0.15 * f
               + mix(vec3(0.5, 0.5, 0.52), vec3(0.56, 0.56, 0.58), up);
    return albedo * light + vec3(0.85, 0.9, 1.0) * 0.45 * r * (albedo + 0.1);
}
#endif
