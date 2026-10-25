#ifndef ROOD_MATERIAL_PACK_GLSL
#define ROOD_MATERIAL_PACK_GLSL
// The G-buffer's material target (RGBA16 UNORM, vk_hw.h VK_MATERIAL_FORMAT;
// pc/docs/Materials.md "The data path"), 16-bit words:
//   r  roughness << 8 | specular            (x 255)
//   g  metal << 8 | cavity (7 bits) << 1 | 1  (bit 0: the pixel has a material)
//   b, a  the relief normal for a unit bump_strength, octahedral, 1..65535
//         each (0: no relief), in the space of the normal target
// All 0: no material (the present pass adds nothing there).
vec2 oct_wrap(vec2 v) { return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }
vec4 material_pack(vec4 mat, vec4 relief, vec3 bent)
{
    if (mat.a < 0.5)
        return vec4(0.0);
    uint r = uint(round(clamp(mat.r, 0.0, 1.0) * 255.0)) << 8 | uint(round(clamp(mat.g, 0.0, 1.0) * 255.0));
    uint g = uint(round(clamp(mat.b, 0.0, 1.0) * 255.0)) << 8 | uint(round(clamp(relief.z, 0.0, 1.0) * 127.0)) << 1 | 1u;
    uvec2 o = uvec2(0u);
    if (relief.w > 0.5) {
        vec3 b = bent / (abs(bent.x) + abs(bent.y) + abs(bent.z));
        vec2 e = b.z >= 0.0 ? b.xy : oct_wrap(b.xy);
        o = uvec2(round((clamp(e, -1.0, 1.0) * 0.5 + 0.5) * 65534.0)) + 1u;
    }
    return vec4(uvec4(r, g, o)) / 65535.0;
}
struct Material {
    vec4 surface; // roughness, specular, metal, 1 (else all 0)
    float cavity; // 0..1
    bool relief;
    vec3 bent; // the relief normal (unit bump_strength), where relief
};
Material material_unpack(vec4 t)
{
    uvec4 w = uvec4(round(t * 65535.0));
    Material m;
    m.surface = vec4(0.0);
    m.cavity = 0.0;
    m.relief = false;
    m.bent = vec3(0.0, 0.0, 1.0);
    if ((w.g & 1u) == 0u)
        return m;
    m.surface = vec4(float(w.r >> 8) / 255.0, float(w.r & 255u) / 255.0, float(w.g >> 8) / 255.0, 1.0);
    m.cavity = float((w.g >> 1) & 127u) / 127.0;
    if (w.b != 0u && w.a != 0u) {
        vec2 e = (vec2(w.ba - 1u) / 65534.0) * 2.0 - 1.0;
        vec3 b = vec3(e, 1.0 - abs(e.x) - abs(e.y));
        if (b.z < 0.0) b.xy = oct_wrap(b.xy);
        m.bent = normalize(b);
        m.relief = true;
    }
    return m;
}
#endif
