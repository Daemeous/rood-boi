#ifndef TEX_PAGES_GLSL
#define TEX_PAGES_GLSL
// A page's texel through its resources (T5-7b; tex_resources.glsl is the draw path's use of it, rt_common.glsl and probe_bvh.glsl the ray readers'): the layer, the CLUT's
// palette slot and the palette entry, all in the VRAM image below the VRAM rectangle (vk_hw.h). Needs usampler2D `vram`.
const int TEX_PAGES_Y = 512, TEX_PALETTES_Y = 2560, TEX_TABLE_Y = 3584; // vk_hw.h
// The texel (u, v) of the page's resource, through its palette (no texture window).
uint texel_page(uint u, uint v, uint tpage, uint clut) {
    uint depth = min((tpage >> 7) & 3u, 2u);
    uint layer = depth * 32u + (tpage & 31u);
    uint t = texelFetch(vram, ivec2(int(layer % 12u * 256u + u), TEX_PAGES_Y + int(layer / 12u * 256u + v)), 0).r;
    if (depth == 2u)
        return t;
    uint slot = texelFetch(vram, ivec2(int((clut & 63u) + depth * 64u), TEX_TABLE_Y + int((clut >> 6) & 511u)), 0).r;
    return texelFetch(vram, ivec2(int(t), TEX_PALETTES_Y + int(slot) - 1), 0).r;
}
#endif
