#version 450
// Native lists (render.c): each vertex into GTE view space through its
// bone's model-view transform; the geometry stage applies the PS1 rules.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in uvec4 a_color; // r, g, b, GPU code
layout(location = 3) in uvec4 a_uvf; // u, v, polygon flags, bone
layout(location = 4) in uvec2 a_page; // tpage, clut
layout(location = 5) in uint a_bias; // ordering-table bias (objects)
layout(location = 6) in uint a_effect; // animated material (uv_effects entry, 0: none)
// Model -> GTE view per bone: three rotation columns, then the translation.
layout(std430, binding = 1) readonly buffer Bones {
    vec4 bones[];
};
layout(location = 0) out vec3 g_pos;
layout(location = 1) out vec3 g_normal;
layout(location = 2) flat out uvec4 g_color;
layout(location = 3) flat out uvec4 g_uvf;
layout(location = 4) flat out uvec2 g_page;
layout(location = 5) flat out uint g_bias;
layout(location = 6) flat out uint g_effect;
void main() {
    uint b = min(a_uvf.w, 63u);
    mat3 rotation = mat3(bones[b * 4u].xyz, bones[b * 4u + 1u].xyz, bones[b * 4u + 2u].xyz);
    g_pos = rotation * a_pos + bones[b * 4u + 3u].xyz;
    g_normal = a_normal;
    g_color = a_color;
    g_uvf = a_uvf;
    g_page = a_page;
    g_bias = a_bias;
    g_effect = a_effect;
}
