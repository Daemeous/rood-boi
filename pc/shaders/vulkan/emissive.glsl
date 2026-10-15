#ifndef ROOD_EMISSIVE_GLSL
#define ROOD_EMISSIVE_GLSL
// Emissive texels (pc/src/emissive.cpp, pc/docs/Emissive.md), shared by the
// packet and native-list shaders of both renderers (the OpenGL ones splice
// it in as PC_GLSL_EMISSIVE). Needs `fetch` (vram.glsl or the GL twin) and a
// usampler2D named `emissive_map`: R8UI, 4096 x 516.
//  - rows 0-511: one cell per texel position of VRAM (x = page x * 4 + u
//    shifted by the page's colour depth, y = page y + v): 0 none, else the
//    slot (catalog entry + 1) whose mask covers the texel. The CPU writes the
//    masks' shapes (rects, polygons) where the catalog's textures were found.
//  - rows 512-515: per slot 64 parameter bytes (slot s at byte s * 64):
//    0-2 tint, 3 strength x 63.75 (the setting folded in), 4-6 / 7-9 colour
//    range (5-bit), 10-11 luminance range x 255, 12 flags (1 CLUT index set,
//    2 colour range, 4 a light), 13 scope: 0 everywhere the cells are
//    marked, else bit + 1 of the polygon's scope bits that must be set (the
//    entry is limited to some surfaces or models: emissive.cpp
//    render_mesh_emissive_scope), 16-47 the index set (256 bits).
//  - rows 516-1027: per cell its amount (a painted map's value, else 255),
//    the emission's factor / 255.
// Texels the shapes cover still pass the slot's filters here, per drawn
// texel: CLUT animations and palette swaps keep exact masks.
uint em_param(uint s, uint i)
{
    uint a = s * 64u + i;
    return texelFetch(emissive_map, ivec2(int(a & 4095u), 512 + int(a >> 12)), 0).r;
}
// The emission of texel (u, v) of `tpage` (texture window `win` applied as
// texel_at applies it), whose colour is `t` (0x7fff-style 15-bit), in display
// light (1 = the texel's own colour), drawn by a polygon with these scope
// bits; zero where nothing glows.
vec3 emissive_texel(uint u, uint v, uint tpage, uint win, uint t, uint scope)
{
    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;
    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;
    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;
    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;
    uint depth = min((tpage >> 7) & 3u, 2u);
    uint s = texelFetch(emissive_map, ivec2((tx * 4 + int(u << depth)) & 4095, ty + int(v)), 0).r;
    if (s == 0u)
        return vec3(0.0);
    uint flags = em_param(s, 12u), bit = em_param(s, 13u);
    if (bit != 0u && ((scope >> (bit - 1u)) & 1u) == 0u)
        return vec3(0.0);
    if ((flags & 1u) != 0u && depth < 2u) {
        uint index = depth == 0u ? (fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u
                                 : (fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u;
        if (((em_param(s, 16u + (index >> 3)) >> (index & 7u)) & 1u) == 0u)
            return vec3(0.0);
    }
    uvec3 c = uvec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u);
    if ((flags & 2u) != 0u) {
        uvec3 lo = uvec3(em_param(s, 4u), em_param(s, 5u), em_param(s, 6u));
        uvec3 hi = uvec3(em_param(s, 7u), em_param(s, 8u), em_param(s, 9u));
        if (any(lessThan(c, lo)) || any(greaterThan(c, hi)))
            return vec3(0.0);
        float luma = dot(vec3(c), vec3(0.299, 0.587, 0.114)) * (255.0 / 31.0);
        if (luma < float(em_param(s, 10u)) - 0.5 || luma > float(em_param(s, 11u)) + 0.5)
            return vec3(0.0);
    }
    vec3 tint = vec3(em_param(s, 0u), em_param(s, 1u), em_param(s, 2u)) / 255.0;
    float amount = float(texelFetch(emissive_map, ivec2((tx * 4 + int(u << depth)) & 4095, 516 + ty + int(v)), 0).r) / 255.0;
    return vec3(c) / 31.0 * tint * (float(em_param(s, 3u)) / 63.75 * amount);
}
#endif
