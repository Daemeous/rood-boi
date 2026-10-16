/* A spot's edge and a rect's barn doors as every renderer evaluates them
 * (pc/docs/Vulkan_Renderer.md "Many lights"; lighting/README.md light
 * fields), and how lights.cpp packs them into slots the lights already have
 * free (light_list.h):
 *   spot: extent_right.x = cos(inner half angle) (direction.w: cos(cone / 2));
 *   rect: direction.w = barn length x cos(barn angle),
 *         extent_right.w = barn length x sin(barn angle) (0 0: no doors).
 * The doors: per axis of the rect, a point p in front sees the part of its
 * width past the two doors on that axis; the factor is the product of both
 * axes' shares (Unreal's barn doors: four flaps of one angle and length).
 * PC_LIGHT_SHAPE_GLSL is the OpenGL shaders' copy of
 * pc/shaders/vulkan/light_shape.glsl (keep the two the same). Plain C
 * (pc/tests/editor_tools_probe.cpp). */
#ifndef ROOD_LIGHT_SHAPE_H
#define ROOD_LIGHT_SHAPE_H
#include <math.h>

/* cos of a spot's inner half angle: its `inner` (full angle, degrees), or
 * with 0 the old soft edge (the outer quarter of the cosine range fades);
 * always above cos(cone / 2) (smoothstep needs its edges in order). */
static inline float pc_spot_cos_inner(float cone, float inner)
{
    float co = cosf(cone * 0.5f * 3.14159265f / 180.0f);
    float ci = inner > 0 ? cosf(fminf(inner, cone) * 0.5f * 3.14159265f / 180.0f) : co + (1.0f - co) * 0.25f;
    return fmaxf(ci, co + 1e-4f);
}
/* A rect's barn doors as two numbers: a = length x cos(angle), b = length x
 * sin(angle) (angle from its facing, 0..90 degrees). */
static inline void pc_barn_pack(float angle, float length, float* a, float* b)
{
    float t = fminf(fmaxf(angle, 0.0f), 90.0f) * 3.14159265f / 180.0f, l = fmaxf(length, 0.0f);
    *a = l * cosf(t);
    *b = l * sinf(t);
}
/* The share of a rect light (centre, half extents right / up, unit facing
 * dir, doors a / b) a point p in front of it sees: the shaders' barn_doors. */
static inline float pc_barn_doors(const float centre[3], const float right[3], const float up[3], const float dir[3],
    float a, float b, const float p[3])
{
    if (a <= 0)
        return 1;
    float v[3] = { p[0] - centre[0], p[1] - centre[1], p[2] - centre[2] };
    float z = fmaxf(v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2], 1e-3f);
    float w = fmaxf(sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]), 1e-3f);
    float h = fmaxf(sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]), 1e-3f);
    float tx = (v[0] * right[0] + v[1] * right[1] + v[2] * right[2]) / (w * z);
    float ty = (v[0] * up[0] + v[1] * up[1] + v[2] * up[2]) / (h * z);
    float fx = 1 - (fmaxf(a * tx - b, 0) + fmaxf(-a * tx - b, 0)) / (2 * w);
    float fy = 1 - (fmaxf(a * ty - b, 0) + fmaxf(-a * ty - b, 0)) / (2 * h);
    return fminf(fmaxf(fx, 0), 1) * fminf(fmaxf(fy, 0), 1);
}

/* light_shape(type, centre, dir, right, up, p, l): the factor on a light's
 * falloff at p (l: the unit direction toward the light): a spot's edge, a
 * rect's front face times its barn doors; 1 for a point light. */
#define PC_LIGHT_SHAPE_GLSL                                                                                            \
    "float barn_doors(vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p) {\n"                                        \
    "    if (dir.w <= 0.0) return 1.0;\n"                                                                              \
    "    vec3 v = p - centre;\n"                                                                                       \
    "    float z = max(dot(v, dir.xyz), 1e-3), w = max(length(right.xyz), 1e-3), h = max(length(up.xyz), 1e-3);\n"   \
    "    float tx = dot(v, right.xyz) / (w * z), ty = dot(v, up.xyz) / (h * z);\n"                                    \
    "    float fx = clamp(1.0 - (max(dir.w * tx - right.w, 0.0) + max(-dir.w * tx - right.w, 0.0)) / (2.0 * w), 0.0, 1.0);\n" \
    "    float fy = clamp(1.0 - (max(dir.w * ty - right.w, 0.0) + max(-dir.w * ty - right.w, 0.0)) / (2.0 * h), 0.0, 1.0);\n" \
    "    return fx * fy;\n"                                                                                            \
    "}\n"                                                                                                               \
    "float light_shape(float type, vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p, vec3 l) {\n"                   \
    "    float c = dot(-l, dir.xyz);\n"                                                                                \
    "    if (type > 1.5) return max(c, 0.0) * barn_doors(centre, dir, right, up, p);\n"                               \
    "    if (type > 0.5) return smoothstep(dir.w, right.x, c);\n"                                                      \
    "    return 1.0;\n"                                                                                                \
    "}\n"
#endif
