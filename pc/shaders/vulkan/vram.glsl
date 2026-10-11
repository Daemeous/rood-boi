// Native VRAM texel decoding, shared by the packet and native-list shaders
// (as gpu_hw.c's and render.c's GL shaders decode it). Needs a usampler2D
// named `vram` (R16_UINT, 1024 x 512).
uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }
// One texel through the texture window and the page's colour depth.
uint texel_at(uint u, uint v, uint tpage, uint clut, uint win) {
    uint mx = (win & 31u) * 8u, my = ((win >> 5) & 31u) * 8u;
    u = ((u & ~mx) | ((((win >> 10) & 31u) * 8u) & mx)) & 255u;
    v = ((v & ~my) | ((((win >> 15) & 31u) * 8u) & my)) & 255u;
    int tx = int(tpage & 15u) * 64, ty = int((tpage >> 4) & 1u) * 256;
    int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);
    uint depth = (tpage >> 7) & 3u;
    if (depth == 0u)
        return fetch(cx + int((fetch(tx + int(u >> 2), ty + int(v)) >> ((u & 3u) * 4u)) & 15u), cy);
    if (depth == 1u)
        return fetch(cx + int((fetch(tx + int(u >> 1), ty + int(v)) >> ((u & 1u) * 8u)) & 255u), cy);
    return fetch(tx + int(u), ty + int(v));
}
vec3 rgb(uint t) { return vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)); }
