#ifndef ROOD_DOWNSAMPLE_GLSL
#define ROOD_DOWNSAMPLE_GLSL
// Image reduction for supersampling (downsample.frag; the OpenGL present path
// embeds this file too). st: the destination pixel's centre in the source's
// image coordinates (0..1); sizes in pixels. Filters:
//   0 area: every source pixel weighted by how much of it the destination
//     pixel covers (an exact box filter for any ratio: 2x is a 2x2 average,
//     1.5x weighs the shared pixels by half);
//   1 Lanczos-3, widened by the ratio (a low-pass at the destination's
//     resolution: sharper than area; its ringing at hard edges is clamped to
//     the range of the source pixels under the destination pixel);
//   2 bilinear (one tap: aliases when the ratio passes 2);
//   3 nearest.
// When the source is not larger than the destination every filter is
// bilinear (magnifying).
float lanczos3(float x) {
    x = abs(x);
    if (x < 1e-4) return 1.0;
    if (x >= 3.0) return 0.0;
    float p = 3.14159265 * x;
    return 3.0 * sin(p) * sin(p / 3.0) / (p * p);
}
vec3 downsample(sampler2D src, vec2 st, vec2 src_size, vec2 dst_size, int filter_) {
    vec2 ratio = src_size / dst_size;
    vec2 center = st * src_size; // source pixels
    ivec2 last = ivec2(src_size) - 1;
    if (filter_ == 3)
        return texelFetch(src, clamp(ivec2(floor(center)), ivec2(0), last), 0).rgb;
    if (filter_ == 2 || (ratio.x <= 1.0 && ratio.y <= 1.0))
        return textureLod(src, st, 0.0).rgb;
    vec2 s = max(ratio, vec2(1.0));
    if (filter_ == 0) {
        vec2 lo = center - 0.5 * s, hi = center + 0.5 * s;
        ivec2 a = ivec2(floor(lo)), b = ivec2(ceil(hi)) - 1;
        b = min(b, a + 15);
        vec3 sum = vec3(0.0);
        float weight = 0.0;
        for (int y = a.y; y <= b.y; y++) {
            float wy = min(hi.y, float(y + 1)) - max(lo.y, float(y));
            for (int x = a.x; x <= b.x; x++) {
                float w = wy * (min(hi.x, float(x + 1)) - max(lo.x, float(x)));
                sum += texelFetch(src, clamp(ivec2(x, y), ivec2(0), last), 0).rgb * w;
                weight += w;
            }
        }
        return sum / max(weight, 1e-6);
    }
    vec2 support = 3.0 * s;
    ivec2 a = ivec2(floor(center - support + 0.5)), b = ivec2(floor(center + support - 0.5));
    b = min(b, a + 23);
    vec3 sum = vec3(0.0), lo3 = vec3(1e9), hi3 = vec3(-1e9);
    float weight = 0.0;
    vec2 near = 0.5 * s + 0.5; // the source pixels under the destination pixel
    for (int y = a.y; y <= b.y; y++) {
        float dy = float(y) + 0.5 - center.y, wy = lanczos3(dy / s.y);
        for (int x = a.x; x <= b.x; x++) {
            float dx = float(x) + 0.5 - center.x, w = wy * lanczos3(dx / s.x);
            vec3 t = texelFetch(src, clamp(ivec2(x, y), ivec2(0), last), 0).rgb;
            sum += t * w;
            weight += w;
            if (abs(dx) < near.x && abs(dy) < near.y) {
                lo3 = min(lo3, t);
                hi3 = max(hi3, t);
            }
        }
    }
    // Within the range of the pixels underneath: no halos at hard edges.
    return clamp(sum / max(weight, 1e-6), lo3, hi3);
}
#endif
