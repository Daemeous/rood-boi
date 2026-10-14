#ifndef ROOD_TEXT_DIST_GLSL
#define ROOD_TEXT_DIST_GLSL
// Distance texels (pc/src/ui_text.h): text and icons drawn from fonts, as a
// signed distance to their edges (PS1 pixels) and how to colour them, drawn
// anti-aliased at whatever resolution they land on (gpu_hw.c's packets into
// the scaled target, the text overlay at the window's resolution). Decoded
// as pc_ui_dist_colour does. Needs fetch() and rgb() (vram.glsl, or the
// including shader) for the CLUT. OpenGL: spliced in as PC_GLSL_TEXT_DIST.

// A texel's distance (PS1 pixels; far outside for any other texel).
float text_dist_at(usampler2D atlas, ivec2 q) {
    uint u = texelFetch(atlas, q, 0).r;
    return (u & 0x80000000u) != 0u ? float(int(u << 18u) >> 18) / 256.0 : -32.0;
}

// The distance at point p (atlas texels) of a region whose texels are
// [lo, hi] (inclusive): bilinear between the four nearest.
float text_distance(usampler2D atlas, vec2 p, ivec2 lo, ivec2 hi) {
    p -= 0.5;
    ivec2 b = ivec2(floor(p));
    vec2 f = p - vec2(b);
    float d00 = text_dist_at(atlas, clamp(b, lo, hi)), d10 = text_dist_at(atlas, clamp(b + ivec2(1, 0), lo, hi));
    float d01 = text_dist_at(atlas, clamp(b + ivec2(0, 1), lo, hi));
    float d11 = text_dist_at(atlas, clamp(b + ivec2(1, 1), lo, hi));
    return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}

// Texel t at distance d drawn with edges w PS1 pixels wide (a pixel's
// footprint) through CLUT `clut`: its coverage (returned), its colour (5-bit
// rgb, blended between the CLUT's) and the PS1 word that stands for it
// (semi-transparency bit; 0: the colour key, nothing drawn).
float text_colour(uint t, float d, float w, uint clut, out vec3 colour, out uint word) {
    float body = clamp(0.5 + d / w, 0.0, 1.0), outer = clamp(0.5 + (d + 1.0) / w, 0.0, 1.0);
    if ((t & 0x40000000u) != 0u) { // a direct colour (icons)
        word = ((t >> 14) & 0x7fffu) | (((t >> 29) & 1u) << 15);
        colour = rgb(word);
        return word == 0u ? 0.0 : body;
    }
    uint mode = (t >> 28) & 3u, hi = (t >> 23) & 15u, lo = (t >> 19) & 15u;
    int cx = int(clut & 63u) * 16, cy = int((clut >> 6) & 511u);
    if (mode == 2u) { // a label: its outline colour (lo) into its lettering's (hi)
        uint b = fetch(cx + int(hi), cy);
        if ((t & 0x08000000u) == 0u) {
            word = b;
            colour = rgb(b);
            return b == 0u ? 0.0 : body;
        }
        uint o = fetch(cx + int(lo), cy);
        word = body >= 0.5 ? b : o;
        colour = mix(rgb(o), rgb(b), body);
        return word == 0u ? 0.0 : outer;
    }
    // The original glyph's ramp: index lo at the edge up to hi inside.
    uint b = fetch(cx + int(lo + uint(floor(body * float(hi - lo) + 0.5))), cy);
    if (mode == 0u) {
        word = b;
        colour = rgb(b);
        return b == 0u ? 0.0 : body;
    }
    uint o = fetch(cx + 1, cy); // outlined: CLUT index 1 around it
    word = body >= 0.5 ? b : o;
    colour = mix(rgb(o), rgb(b), body);
    return word == 0u ? 0.0 : outer;
}
#endif
