// The post stack's grade pass (pc/docs/Post_Processing.md), shared by
// post_grade.frag (Vulkan) and gl_present.c (OpenGL through gl_shaders.py).
//
// The includer defines PG(i), the parameter block (post.h pc_post_gpu,
// packed by post_profile.cpp pc_post_gpu_pack), and the functions
//   vec4 pg_input(vec2 st)       the lit image (rgb, a: 9999 on 2D/UI pixels), bilinear
//   vec3 pg_bloom(vec2 uv)       the bloom result (linear, exposed)
//   vec3 pg_lut(vec3 rgb)        the LUT (display rgb in and out)
// where uv runs 0..1 across the view with v = 0 at the top, and st is the
// image coordinate the includer samples with (GL counts rows from the bottom).
//
// Order (physical, as Unreal's tonemapper pass): sharpen and chromatic
// aberration on the lit image, then in linear light exposure, bloom, white
// balance, colour grading (Unreal's ColorCorrect per shadows / midtones /
// highlights), vignette, tone mapping, display encoding, the LUT, and film
// grain last. 2D/UI pixels pass through untouched.
#ifndef ROOD_POST_GRADE_GLSL
#define ROOD_POST_GRADE_GLSL

#define PG_EXPOSURE PG(0).x
#define PG_BLOOM PG(0).y
#define PG_CA PG(0).z /* lit pixels at the corners */
#define PG_CA_START PG(0).w
#define PG_VIGNETTE PG(1).x
#define PG_SHARPEN PG(1).y
#define PG_GRAIN PG(1).z
#define PG_GRAIN_SIZE PG(1).w /* view pixels */
#define PG_SEED PG(3).x
#define PG_VIEW PG(3).yz
#define PG_ASPECT PG(3).w
#define PG_TONE_A PG(4).x
#define PG_TONE_B PG(4).y
#define PG_TONE_MIX PG(4).z
#define PG_WHITE PG(4).w
#define PG_SHADOWS_MAX PG(8).x
#define PG_HIGHLIGHTS_MIN PG(8).y
#define PG_LUT_INTENSITY PG(8).z
#define PG_LUT_SIZE PG(8).w
#define PG_BLOOM_TINT PG(24).xyz
#define PG_LIT PG(26).xy
#define PG_TEXEL PG(26).zw
#define PG_GRADING (PG(27).x != 0.0)
#define PG_HDR (PG(27).y != 0.0)
#define PG_WB (PG(27).z != 0.0)
#define PG_CA_ON (PG(27).w != 0.0)

const vec3 PG_LUMA = vec3(0.2126, 0.7152, 0.0722);

// The game's picture is display-referred (the PS1's colours): a 2.2 power
// between it and linear light, exactly undone on the way back.
vec3 pg_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 pg_display(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }

// ---- tone mapping: linear scene light in, linear display light out (0..1) ----
// ACES fitted (the RRT + ODT fit with its sRGB input and output matrices).
vec3 pg_aces_curve(vec3 v)
{
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
vec3 pg_aces(vec3 c, float white)
{
    const mat3 to_aces = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 from_aces = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    vec3 v = pg_aces_curve(to_aces * c);
    float w = pg_aces_curve(vec3(white)).x;
    return clamp(from_aces * (v / w), 0.0, 1.0);
}
// AgX (Troy Sobotka's; the polynomial fit of its default contrast), white at `white`.
vec3 pg_agx_contrast(vec3 x)
{
    vec3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 pg_agx(vec3 c, float white)
{
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
        0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
        1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float lo = -12.47393, hi = 4.026069;
    c *= exp2(hi) / white; // `white` maps to the top of AgX's range
    vec3 v = clamp(log2(max(inset * c, vec3(1e-10))), lo, hi);
    v = pg_agx_contrast((v - lo) / (hi - lo));
    return clamp(pow(max(outset * v, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
}
// Filmic (John Hable's curve), white point normalised.
vec3 pg_hable_curve(vec3 x)
{
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return (x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F) - E / F;
}
vec3 pg_filmic(vec3 c, float white) { return clamp(pg_hable_curve(c * 2.0) / pg_hable_curve(vec3(white * 2.0)), 0.0, 1.0); }
// Reinhard (extended, on luminance: hues kept).
vec3 pg_reinhard(vec3 c, float white)
{
    float l = dot(c, PG_LUMA);
    float m = l * (1.0 + l / (white * white)) / (1.0 + l);
    return clamp(c * (m / max(l, 1e-6)), 0.0, 1.0);
}
// Khronos PBR Neutral: linear up to 0.76, then a soft roll-off that desaturates toward white.
vec3 pg_neutral(vec3 c, float white)
{
    c *= 4.0 / white;
    const float start = 0.8 - 0.04, desaturation = 0.15;
    float x = min(c.r, min(c.g, c.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    c -= offset;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < start)
        return c;
    const float d = 1.0 - start;
    float new_peak = 1.0 - d * d / (peak + d - start);
    c *= new_peak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return clamp(mix(c, vec3(new_peak), g), 0.0, 1.0);
}
// None: the original picture's soft shoulder above 0.8 (display light; gl_present.c's lit pixels),
// clamped. Without HDR input (PG_HDR off) the picture is already at most 1: a clamp.
vec3 pg_none(vec3 c)
{
    if (PG_HDR) {
        vec3 d = pg_display(c);
        d = mix(d, 1.0 - 0.2 * exp(-(d - 0.8) / 0.2), step(0.8, d));
        return pg_linear(min(d, vec3(1.0)));
    }
    return min(c, vec3(1.0));
}
vec3 pg_tonemap(vec3 c, float mode, float white)
{
    if (mode < 0.5) return pg_none(c);
    if (mode < 1.5) return pg_aces(c, white);
    if (mode < 2.5) return pg_agx(c, white);
    if (mode < 3.5) return pg_filmic(c, white);
    if (mode < 4.5) return pg_reinhard(c, white);
    return pg_neutral(c, white);
}

// ---- colour grading (Unreal's ColorCorrect, per region) ----
vec3 pg_correct(vec3 c, int base)
{
    float l = dot(c, PG_LUMA);
    c = max(vec3(0.0), mix(vec3(l), c, PG(base).rgb));
    c = pow(c * (1.0 / 0.18), PG(base + 1).rgb) * 0.18;
    c = pow(c, 1.0 / max(PG(base + 2).rgb, vec3(1e-3)));
    return c * PG(base + 3).rgb + PG(base + 4).rgb;
}
vec3 pg_grade(vec3 c)
{
    float l = dot(c, PG_LUMA);
    float s = 1.0 - smoothstep(0.0, max(PG_SHADOWS_MAX, 1e-4), l);
    float h = smoothstep(PG_HIGHLIGHTS_MIN, 1.0, l);
    return pg_correct(c, 9) * s + pg_correct(c, 14) * max(1.0 - s - h, 0.0) + pg_correct(c, 19) * h;
}

// ---- film grain: value noise on a lattice of PG_GRAIN_SIZE view pixels, seeded per game frame ----
float pg_hash(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}
float pg_noise(vec2 p, float channel)
{
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    float z = PG_SEED * 7.0 + channel * 131.0;
    float a = pg_hash(vec3(i, z)), b = pg_hash(vec3(i + vec2(1, 0), z));
    float c = pg_hash(vec3(i + vec2(0, 1), z)), d = pg_hash(vec3(i + vec2(1, 1), z));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y) * 2.0 - 1.0;
}

// The grade of one (3D) pixel: its lit colour `centre`; uv: 0..1 over the view (v down); st: where the
// includer samples it, st_per_uv its direction ((1, 1) Vulkan, (1, -1) OpenGL).
vec3 pg_pixel(vec4 centre, vec2 uv, vec2 st, vec2 st_per_uv)
{
    vec3 c = centre.rgb;
    // Sharpen (contrast-adaptive: never past the neighbours' range).
    if (PG_SHARPEN > 0.0) {
        vec2 dx = vec2(PG_TEXEL.x, 0.0), dy = vec2(0.0, PG_TEXEL.y);
        vec4 n0 = pg_input(st + dx), n1 = pg_input(st - dx), n2 = pg_input(st + dy), n3 = pg_input(st - dy);
        vec3 a = n0.a > 9000.0 ? c : n0.rgb, b = n1.a > 9000.0 ? c : n1.rgb;
        vec3 e = n2.a > 9000.0 ? c : n2.rgb, f = n3.a > 9000.0 ? c : n3.rgb;
        vec3 lo = min(c, min(min(a, b), min(e, f))), hi = max(c, max(max(a, b), max(e, f)));
        vec3 sharp = c + PG_SHARPEN * (c - (a + b + e + f) * 0.25) * 1.5;
        c = clamp(sharp, lo, hi);
    }
    // Chromatic aberration: red outward, blue inward, growing past the start offset.
    if (PG_CA_ON) {
        vec2 d = (uv - 0.5) * PG_LIT; // lit pixels from the centre
        float r = length(d) / length(0.5 * PG_LIT);
        float amount = PG_CA * max(r - PG_CA_START, 0.0) / max(1.0 - PG_CA_START, 1e-3);
        vec2 shift = (r > 1e-5 ? normalize(d) : vec2(0.0)) * amount * PG_TEXEL * st_per_uv;
        vec4 red = pg_input(st + shift), blue = pg_input(st - shift);
        if (red.a < 9000.0) c.r = red.r;
        if (blue.a < 9000.0) c.b = blue.b;
    }
    vec3 lin = pg_linear(c) * PG_EXPOSURE;
    if (PG_BLOOM > 0.0)
        lin += pg_bloom(uv) * PG_BLOOM * PG_BLOOM_TINT;
    if (PG_WB)
        lin = max(vec3(dot(PG(5).xyz, lin), dot(PG(6).xyz, lin), dot(PG(7).xyz, lin)), vec3(0.0)); // rows
    if (PG_GRADING)
        lin = max(pg_grade(lin), vec3(0.0));
    if (PG_VIGNETTE > 0.0) {
        vec2 d = (uv - 0.5) * vec2(PG_ASPECT, 1.0);
        float r = length(d) / length(vec2(PG_ASPECT, 1.0) * 0.5);
        float k = r * PG_VIGNETTE * 1.2;
        lin *= 1.0 / ((1.0 + k * k) * (1.0 + k * k));
    }
    vec3 mapped = pg_tonemap(lin, PG_TONE_B, PG_WHITE);
    if (PG_TONE_MIX < 1.0)
        mapped = mix(pg_tonemap(lin, PG_TONE_A, PG_WHITE), mapped, PG_TONE_MIX);
    vec3 outc = pg_display(mapped);
    if (PG_LUT_INTENSITY > 0.0)
        outc = mix(outc, pg_lut(clamp(outc, 0.0, 1.0)), PG_LUT_INTENSITY);
    if (PG_GRAIN > 0.0) {
        vec2 p = uv * PG_VIEW / max(PG_GRAIN_SIZE, 0.25);
        float mono = pg_noise(p, 0.0);
        vec3 n = mix(vec3(mono), vec3(mono, pg_noise(p, 1.0), pg_noise(p, 2.0)), PG(2).w);
        float l = dot(outc, PG_LUMA);
        float s = 1.0 - smoothstep(0.0, 0.4, l), h = smoothstep(0.6, 1.0, l);
        float response = PG(2).x * s + PG(2).z * h + PG(2).y * max(1.0 - s - h, 0.0);
        outc += n * PG_GRAIN * response * 0.12;
    }
    // The game's screen fade (a full-screen subtractive quad, gpu_hw.c BLEND_FADE), as the PS1 subtracts it.
    // Where it covers: the whole view, or (widescreen without the cover fix) the 4:3 middle.
    if (PG(28).x > 0.0 && all(greaterThanEqual(uv, PG(29).xy)) && all(lessThanEqual(uv, PG(29).zw)))
        outc = max(outc - PG(28).x, vec3(0.0));
    return clamp(outc, 0.0, 1.0);
}
#endif
