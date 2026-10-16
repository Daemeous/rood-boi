#ifndef ROOD_HD_TEXTURES_GLSL
#define ROOD_HD_TEXTURES_GLSL
// Bindings are declared by the packet/native fragment entry points.
#ifdef ROOD_GL
uniform usampler2D hd_words;
uniform usampler2D hd_palettes;
uniform usampler2D hd_table;
uniform sampler2D hd_atlas;
#endif
// Beetle upload rectangles, CRC32(raw little endian words)-CRC32(CLUT).
// The word map retains source offsets through partial overwrites and copies.
bool hd_texel(vec2 uv, uint page, uint clut, uint win, out vec4 color) {
    if (textureSize(hd_words, 0).x != 1024) return false;
    vec2 f = fract(uv);
    uvec2 q = uvec2(ivec2(floor(uv))) & 255u;
    uvec2 mask = uvec2(win & 31u, (win >> 5) & 31u) * 8u;
    q = (q & ~mask) | ((uvec2((win >> 10) & 31u, (win >> 15) & 31u) * 8u) & mask);
    uint depth = (page >> 7) & 3u;
    uint ppw = depth == 0u ? 4u : depth == 1u ? 2u : 1u;
    ivec2 at = ivec2((page & 15u) * 64u + q.x / ppw, ((page >> 4) & 1u) * 256u + q.y);
    uvec4 word = texelFetch(hd_words, at & ivec2(1023,511), 0);
    if (word.z == 0u) return false;
    uint pal = depth < 2u ? texelFetch(hd_palettes, ivec2(int(clut & 63u), int((clut >> 6) & 511u) + (depth == 1u ? 512 : 0)), 0).r : 0u;
    uint slot = (word.x * 0x9e3779b1u ^ pal * 0x85ebca6bu) & 4095u;
    for (int probe = 0; probe < 4096; ++probe, slot = (slot + 1u) & 4095u) {
        uvec4 size = texelFetch(hd_table, ivec2(int(slot),1),0);
        if (size.x == 0u) return false;
        uvec4 key = texelFetch(hd_table, ivec2(int(slot),0),0);
        if (key.x != word.x || key.y != pal) continue;
        vec2 source = vec2(float((word.y & 65535u) * ppw + q.x % ppw), float(word.y >> 16)) + f;
        vec2 point = floor(source * vec2(size.xy) / vec2(word.z * ppw, word.w));
        point = clamp(point, vec2(0), vec2(size.xy)-1.0);
        color = texelFetch(hd_atlas, ivec2(key.zw) + ivec2(point), 0);
        return true;
    }
    return false;
}
// PNG alpha follows Beetle: 0 clear, 255 opaque, 1..254 PS1 semi-transparent.
void hd_apply(vec4 hd, inout vec3 texel, inout uint t) {
    texel = hd.rgb * 31.0;
    t = hd.a == 0.0 ? 0u : hd.a < 1.0 ? 0x8000u : 1u;
}
#endif
