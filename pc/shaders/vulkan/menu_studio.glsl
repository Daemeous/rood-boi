// The modern menu's model view (gpu_hw.c pc_hw_present_menu_studio): the
// model is lit by a studio rig of its own, the same in every room and on
// every screen that shows it, instead of the room's lights, GI and haze and
// the game's colour for it (the vertex colours, and the offset copy the
// game draws behind it as a rim, which ui/ashley_bridge.c leaves out). The
// texture's colour (albedo, texels as the PS1 shows them at vertex colour
// 128) under a key, a fill, a sky/ground ambient and a rim light.
// The rig is data: the "menu_model_light" block of pc/assets/ui/theme.json
// (ui/menu_light.h; shipped: a warm key from the upper left in front, a cool
// fill from the right, a rim from behind on the upper right), live through
// RPC graphics menu_light and the dev tools (Graphics > Menu model light).
// The includer defines MENU_STUDIO(i), the rig's PC_MENU_LIGHT_VEC4S vec4s
// (ui/menu_light.h pc_menu_light_pack): 0-1 the key (direction, intensity;
// colour, wrap), 2-3 the fill, 4-5 the rim, 6 ground + rim power, 7 sky +
// rim lift, 8 ambient intensity, exposure, saturation.
// View space: x right, y down, z away from the eye; n faces the eye.
// OpenGL splices this into gl_present.c's present shader (gl_shaders.py),
// Vulkan includes it in scaled.frag.
#ifndef MENU_STUDIO_GLSL
#define MENU_STUDIO_GLSL
// A light's share on normal n: n.l wrapped (w: the far side keeps w of it).
float menu_studio_wrap(vec3 n, int i) {
    float w = MENU_STUDIO(i + 1).w;
    return max(dot(n, normalize(MENU_STUDIO(i).xyz)) * (1.0 - w) + w, 0.0);
}
vec3 menu_studio(vec3 albedo, vec3 n, vec3 p) {
    vec3 v = normalize(-p);
    // Shipped gently: the texture carries the picture (as on the PS1), the
    // light only shapes it (front about 1, the far side about 0.7), so the
    // low-poly faces never turn into hard facets.
    float k = menu_studio_wrap(n, 0);
    float f = menu_studio_wrap(n, 2);
    float up = 0.5 - 0.5 * n.y;
    float r = menu_studio_wrap(n, 4) * pow(1.0 - max(dot(n, v), 0.0), MENU_STUDIO(6).w);
    vec3 light = MENU_STUDIO(1).rgb * MENU_STUDIO(0).w * k + MENU_STUDIO(3).rgb * MENU_STUDIO(2).w * f
               + mix(MENU_STUDIO(6).rgb, MENU_STUDIO(7).rgb, up) * MENU_STUDIO(8).x;
    vec3 c = albedo * light + MENU_STUDIO(5).rgb * MENU_STUDIO(4).w * r * (albedo + MENU_STUDIO(7).w);
    if (MENU_STUDIO(8).y != 1.0)
        c *= MENU_STUDIO(8).y;
    if (MENU_STUDIO(8).z != 1.0)
        c = max(mix(vec3(dot(c, vec3(0.2126, 0.7152, 0.0722))), c, MENU_STUDIO(8).z), 0.0);
    return c;
}
#endif
