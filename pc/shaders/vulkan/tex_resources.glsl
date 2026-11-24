// The texture resources (T5-7b; pc/docs/plans/2026-11-17-vram.md, pc/src/vk_hw.h): a page's texels decoded, 256 x 256 a layer (96 layers, 12 across), the palettes 256 a row and
// the table that names a CLUT's row, all in the VRAM image BELOW the VRAM rectangle (so the binding every VRAM reader has reaches them). Needs usampler2D `vram` (vram.glsl,
// rt_common.glsl, probe_bvh.glsl). This file and the choice below are the whole of the resource path in the shaders: the frame-sampling draws move to captures in a later
// step, and then texel_choice is texel_res and the VRAM path goes.
const uint WINDOW_RESOURCE = 0x08000000u; // the draw's texture is a pure-upload page with current resources (gpu_hw.c tex_note)
#include "tex_pages.glsl"
uint texel_res(uint u, uint v, uint tpage, uint clut, uint win) {
    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;
    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;
    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;
    return texel_page(u, v, tpage, clut);
}
uint texel_choice(uint u, uint v, uint tpage, uint clut, uint win) {
    return (win & WINDOW_RESOURCE) != 0u ? texel_res(u, v, tpage, clut, win) : texel_at(u, v, tpage, clut, win);
}
