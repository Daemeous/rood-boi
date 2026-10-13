// Brightness (settings brightness; gl_present.c and vk_present.c): a curve on
// the window's (sRGB-encoded) colour, f(c) = m c / (1 + (m - 1) c). It keeps
// black and white where they are and has a finite slope everywhere: m at
// black, 1/m at white (no posterized darks, as a power curve with its
// infinite slope at 0 gives). m above 1 brightens, below 1 darkens; f with m
// and with 1/m undo each other. The presenters feed it the post chain's
// 16-bit float image, so it runs before the window's 8-bit rounding.
#ifndef ROOD_BRIGHTNESS_GLSL
#define ROOD_BRIGHTNESS_GLSL
vec3 brightness(vec3 c, float m)
{
    c = clamp(c, 0.0, 1.0);
    return m * c / (1.0 + (m - 1.0) * c);
}
#endif
