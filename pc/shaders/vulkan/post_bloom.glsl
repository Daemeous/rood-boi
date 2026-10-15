// Bloom's image passes and auto exposure's metering (pc/docs/Post_Processing.md),
// shared by bloom.frag (Vulkan) and gl_present.c (OpenGL through gl_shaders.py).
// Coordinates are image coordinates (st) of the sampled textures.
//
// The Gaussian bloom (any renderer): the lit image's bright light (exposed,
// in linear light, above a soft threshold; 2D/UI pixels give none) filtered
// down to half size with 13 taps (Karis-weighted against fireflies), down a
// mip chain with the same filter, then back up, each level adding a 3 x 3
// tent of the smaller one. The convolution bloom (Vulkan) takes the second
// level into its FFT domain (bloom_fft_*.comp).
#ifndef ROOD_POST_BLOOM_GLSL
#define ROOD_POST_BLOOM_GLSL

float pb_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Exposed linear light above the threshold (a quadratic soft knee).
vec3 pb_bright(vec4 s, float exposure, float threshold, float knee)
{
    if (s.a > 9000.0)
        return vec3(0.0);
    vec3 c = pow(max(s.rgb, vec3(0.0)), vec3(2.2)) * exposure;
    float peak = max(c.r, max(c.g, c.b));
    float k = threshold * knee + 1e-5;
    float soft = clamp(peak - threshold + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k);
    return c * (max(soft, peak - threshold) / max(peak, 1e-5));
}

// One tap of the 13 (weight k); the prefilter (bright) takes the bright part and a Karis weight.
void pb_tap(sampler2D t, vec2 st, float k, bool bright, vec3 params, inout vec3 sum, inout float weight)
{
    vec4 s = texture(t, st);
    if (!bright) {
        sum += s.rgb * k;
        weight += k;
        return;
    }
    vec3 c = pb_bright(s, params.x, params.y, params.z);
    float w = k / (1.0 + pb_luma(c)); // one bright texel cannot flicker the whole bloom
    sum += c * w;
    weight += w;
}
// 13 taps over 4 x 4 source texels (Jimenez 2014): a 0.5 inner box and four 0.125 outer boxes.
// params: exposure, threshold, knee (the prefilter).
vec3 pb_down(sampler2D t, vec2 st, vec2 texel, bool bright, vec3 params)
{
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    pb_tap(t, st, 0.125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(-2.0, -2.0), 0.03125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(2.0, -2.0), 0.03125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(-2.0, 2.0), 0.03125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(2.0, 2.0), 0.03125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(0.0, -2.0), 0.0625, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(-2.0, 0.0), 0.0625, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(2.0, 0.0), 0.0625, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(0.0, 2.0), 0.0625, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(-1.0, -1.0), 0.125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(1.0, -1.0), 0.125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(-1.0, 1.0), 0.125, bright, params, sum, weight);
    pb_tap(t, st + texel * vec2(1.0, 1.0), 0.125, bright, params, sum, weight);
    return sum / max(weight, 1e-6);
}
// A 3 x 3 tent of the smaller level (texel: its texel size), `radius` texels wide.
vec3 pb_up(sampler2D t, vec2 st, vec2 texel, float radius)
{
    vec2 d = texel * radius;
    vec3 s = texture(t, st).rgb * 4.0;
    s += (texture(t, st + vec2(d.x, 0.0)).rgb + texture(t, st - vec2(d.x, 0.0)).rgb
        + texture(t, st + vec2(0.0, d.y)).rgb + texture(t, st - vec2(0.0, d.y)).rgb) * 2.0;
    s += texture(t, st + d).rgb + texture(t, st - d).rgb + texture(t, st + vec2(d.x, -d.y)).rgb
        + texture(t, st + vec2(-d.x, d.y)).rgb;
    return s / 16.0;
}
// Auto exposure's meter: 4 x 4 taps of the lit image over this output texel's footprint
// (texel: the footprint's size): r the mean log2 luminance of 3D taps x their share, g their share.
vec2 pb_meter(sampler2D t, vec2 st, vec2 footprint)
{
    float sum = 0.0, n = 0.0;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            vec4 s = texture(t, st + (vec2(x, y) - 1.5) / 4.0 * footprint);
            if (s.a > 9000.0)
                continue;
            sum += log2(pb_luma(pow(max(s.rgb, vec3(0.0)), vec3(2.2))) + 1e-4);
            n += 1.0;
        }
    return vec2(sum, n) / 16.0;
}
#endif
