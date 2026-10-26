#ifndef ROOD_MATERIAL_GLSL
#define ROOD_MATERIAL_GLSL
// Material response (pc/src/materials.cpp, emissive.cpp write_materials;
// pc/docs/Materials.md): six planes of 512 rows in the emissive slot map
// image from row 1028 (emissive.h PC_MATERIAL_ROW), cell for cell like the
// slots: roughness, specular, metal (x 255), the relief's slopes along u and
// v (0: no relief, else 128 + slope x 63.5: slope in height units per texel,
// 1 height unit = bump_strength texels of depth) and its cavity (how far the
// texel sits below its neighbours: the ambient light it loses, x 255).
// A texel without a material has roughness 255 and the rest 0. Read at the
// texel the colour came from (nearest, texture window applied), written to
// the G-buffer's material target (material_pack.glsl).
#include "material_pack.glsl"
const int MATERIAL_ROW = 1028;
ivec2 material_cell(uint u, uint v, uint tpage, uint win)
{
    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;
    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;
    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;
    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;
    uint depth = min((tpage >> 7) & 3u, 2u);
    return ivec2((tx * 4 + int(u << depth)) & 4095, MATERIAL_ROW + ty + int(v));
}
// Roughness, specular, metal, 1 (all 0: no material). `relief`: the slopes
// along u and v and the cavity, w 1 where the texel has a relief (else 0).
vec4 material_texel(uint u, uint v, uint tpage, uint win, out vec4 relief)
{
    ivec2 cell = material_cell(u, v, tpage, win);
    uint s = texelFetch(emissive_map, cell + ivec2(0, 512), 0).r;
    uint m = texelFetch(emissive_map, cell + ivec2(0, 1024), 0).r;
    uint su = texelFetch(emissive_map, cell + ivec2(0, 1536), 0).r;
    relief = vec4(0.0);
    if (s == 0u && m == 0u && su == 0u)
        return vec4(0.0);
    if (su != 0u) {
        uint sv = texelFetch(emissive_map, cell + ivec2(0, 2048), 0).r;
        uint cv = texelFetch(emissive_map, cell + ivec2(0, 2560), 0).r;
        relief = vec4((float(su) - 128.0) / 63.5, (float(sv) - 128.0) / 63.5, float(cv) / 255.0, 1.0);
    }
    return vec4(float(texelFetch(emissive_map, cell, 0).r) / 255.0, float(s) / 255.0, float(m) / 255.0, 1.0);
}
// The relief's normal for a unit bump_strength: the surface displaced along
// its normal n by the height (in texels of depth), per texel (the slopes are
// constant over a texel: the bumps stay pixel art). dp1, dp2: the position's
// screen derivatives (the space of n), duv1, duv2: the texture coordinates'.
// The gradients of u and v along the surface (Schuler's cotangent frame)
// carry the slopes into that space; a texel's size there turns texels of
// depth into the same units.
vec3 relief_normal(vec3 n, vec2 slope, vec3 dp1, vec3 dp2, vec2 duv1, vec2 duv2)
{
    vec3 c2 = cross(dp2, n), c1 = cross(n, dp1);
    float det = dot(n, cross(dp1, dp2));
    if (abs(det) < 1e-12)
        return n;
    vec3 gu = (c2 * duv1.x + c1 * duv2.x) / det, gv = (c2 * duv1.y + c1 * duv2.y) / det;
    float area = length(cross(gu, gv)); // texels per unit area
    if (area < 1e-12)
        return n;
    vec3 g = (slope.x * gu + slope.y * gv) * inversesqrt(area);
    return normalize(n - g);
}
// How much of the relief a pixel keeps (1: all, 0: flat). The relief is
// sampled nearest, one slope per texel: where the texture coordinates change
// fast over a pixel (a face edge-on to the view, stretched along one axis, or
// many texels to a pixel) the pixel lands on unrelated texels and the normals
// speckle. duv1, duv2: the texture coordinates' screen derivatives (texels per
// pixel); their singular values are the footprint's long and short sides.
// fade.x: the ratio of the long side to the short one (anisotropy) where the
// relief starts to fade (gone at twice), fade.y: the long side in texels per
// pixel where it starts to fade (gone at twice). 0 turns a limit off.
float relief_keep(vec2 duv1, vec2 duv2, vec2 fade)
{
    float e = 0.5 * (duv1.x + duv2.y), f = 0.5 * (duv1.x - duv2.y);
    float g = 0.5 * (duv2.x + duv1.y), h = 0.5 * (duv2.x - duv1.y);
    float q = sqrt(e * e + h * h), r = sqrt(f * f + g * g);
    float longest = q + r, shortest = abs(q - r);
    float keep = 1.0;
    if (fade.x > 0.0)
        keep *= 1.0 - smoothstep(fade.x, 2.0 * fade.x, longest / max(shortest, 1e-6));
    if (fade.y > 0.0)
        keep *= 1.0 - smoothstep(fade.y, 2.0 * fade.y, longest);
    return keep;
}
#endif
