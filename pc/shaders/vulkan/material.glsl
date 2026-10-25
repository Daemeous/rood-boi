#ifndef ROOD_MATERIAL_GLSL
#define ROOD_MATERIAL_GLSL
// Material response (pc/src/materials.cpp, emissive.cpp write_materials;
// pc/docs/Materials.md): three planes of 512 rows in the emissive slot map
// image from row 1028 (emissive.h PC_MATERIAL_ROW), cell for cell like the
// slots: roughness, specular, metal (x 255). A texel without a material has
// roughness 255 and specular = metal = 0. Read at the texel the colour came
// from (nearest, texture window applied), written to the G-buffer's material
// target: rgb roughness, specular, metal, a 1 (else all 0: no material, and
// the present pass adds nothing).
const int MATERIAL_ROW = 1028;
vec4 material_texel(uint u, uint v, uint tpage, uint win)
{
    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;
    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;
    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;
    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;
    uint depth = min((tpage >> 7) & 3u, 2u);
    ivec2 cell = ivec2((tx * 4 + int(u << depth)) & 4095, MATERIAL_ROW + ty + int(v));
    uint s = texelFetch(emissive_map, cell + ivec2(0, 512), 0).r;
    uint m = texelFetch(emissive_map, cell + ivec2(0, 1024), 0).r;
    if (s == 0u && m == 0u)
        return vec4(0.0);
    return vec4(float(texelFetch(emissive_map, cell, 0).r) / 255.0, float(s) / 255.0, float(m) / 255.0, 1.0);
}
#endif
