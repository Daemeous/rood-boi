#ifndef ROOD_AA_GEOMETRY_GLSL
#define ROOD_AA_GEOMETRY_GLSL
// Geometric edges for the post-process anti-aliasing (smaa_edges.frag; the
// OpenGL present path embeds this file too). The lit image is drawn from the
// scaled VRAM target, so each of its pixels has the target's depth (GTE SZ /
// 65536: 0 2D/UI, 1 far) and normal (a > 0.5: a lit 3D surface) behind it.
// An edge between a pixel and its neighbour counts when the geometry breaks
// there: a silhouette (3D against the far background, or a jump in depth off
// the plane through the pixel and its other neighbour) or a crease (normals
// apart). Texel boundaries inside a surface are not edges, so the PS1's
// nearest-sampled textures stay crisp; 2D/UI pixels never have edges, so the
// HUD and text stay exactly as drawn.
//
// The includer defines AA_RECT (ivec4: display area, VRAM pixels), AA_SCALE
// (int: the target's scale), aa_depth_tex and aa_normal_tex (the target's
// depth and normals), and aa_picture(st): the picture's uv (0,0 top-left) of
// image coordinates st (identity on Vulkan; OpenGL's images count rows from
// the bottom).

// The target texel shown at picture uv (as the present shader's texel()).
ivec2 aa_texel(vec2 uv) {
    vec2 size = vec2(AA_RECT.zw * AA_SCALE);
    ivec2 p = clamp(ivec2(floor(uv * size)), ivec2(0), ivec2(size) - 1);
    return ivec2((AA_RECT.x * AA_SCALE + p.x) % (1024 * AA_SCALE), (AA_RECT.y * AA_SCALE + p.y) % (512 * AA_SCALE));
}
float aa_sz(ivec2 q) { return texelFetch(aa_depth_tex, q, 0).r * 65536.0; }
// Whether the geometry breaks between texels a and b (neighbours in the
// picture); c is a's texel on the far side from b (planarity).
bool aa_break(ivec2 a, ivec2 b, ivec2 c) {
    if (a == b) return false; // the view magnifies the target: one texel
    float za = aa_sz(a), zb = aa_sz(b);
    if (za < 0.5 || zb < 0.5) return false; // 2D / UI: never
    bool fa = za >= 65535.0, fb = zb >= 65535.0; // nothing drawn (fills, the void)
    if (fa || fb) return fa != fb;
    vec4 na = texelFetch(aa_normal_tex, a, 0), nb = texelFetch(aa_normal_tex, b, 0);
    if (na.a > 0.5 && nb.a > 0.5 && dot(normalize(na.xyz * 2.0 - 1.0), normalize(nb.xyz * 2.0 - 1.0)) < 0.9)
        return true; // a crease (about 25 degrees)
    // Inverse depth is linear across a plane in screen space: b is off the
    // plane through a and c when 1/zb misses 2/za - 1/zc.
    float zc = aa_sz(clamp(c, ivec2(0), ivec2(1024, 512) * AA_SCALE - 1));
    float predicted = zc > 0.5 && zc < 65535.0 ? 2.0 / za - 1.0 / zc : 1.0 / za;
    return abs(1.0 / zb - predicted) * za > 0.02;
}
// Edges at the left (x) and top (y) of image pixel st (the post-process's
// own convention: SMAA's left/top neighbours), as 0 or 1.
vec2 aa_geometry_edges(vec2 st, vec2 texel_size) {
    ivec2 here = aa_texel(aa_picture(st));
    ivec2 left = aa_texel(aa_picture(st - vec2(texel_size.x, 0.0)));
    ivec2 top = aa_texel(aa_picture(st - vec2(0.0, texel_size.y)));
    return vec2(aa_break(here, left, here + (here - left)) ? 1.0 : 0.0,
        aa_break(here, top, here + (here - top)) ? 1.0 : 0.0);
}
#endif
