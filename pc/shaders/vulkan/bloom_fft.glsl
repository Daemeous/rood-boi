// The convolution bloom's FFT (vk_present.c; pc/docs/Post_Processing.md):
// radix-2 Cooley-Tukey over one row or column of an n x n domain (n 256 or
// 512) in shared memory, one workgroup per line, n / 2 threads each doing a
// butterfly per stage. Every element is two complex numbers (xy, zw): the
// picture's red + i green and blue + i 0, so three real channels take two
// transforms; the kernel's spectrum multiplies both.
#ifndef ROOD_BLOOM_FFT_GLSL
#define ROOD_BLOOM_FFT_GLSL
layout(local_size_x = 256) in;
layout(push_constant) uniform Push {
    int n, log2n, mode; // mode: cols 0 kernel spectrum, 1 convolve
    float scale; // rows inverse: 1 / n^2
} pc;
shared vec4 line[512];

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
uint reversed(uint i) { return bitfieldReverse(i) >> uint(32 - pc.log2n); }

// The line in bit-reversed order -> its transform in natural order.
void fft(bool inverse)
{
    uint t = gl_LocalInvocationID.x;
    for (int s = 1; s <= pc.log2n; s++) {
        uint m = 1u << uint(s - 1);
        if (t < uint(pc.n / 2)) {
            uint j = t & (m - 1u), i0 = ((t >> uint(s - 1)) << uint(s)) + j, i1 = i0 + m;
            float a = (inverse ? 3.14159265358979 : -3.14159265358979) * float(j) / float(m);
            vec2 w = vec2(cos(a), sin(a));
            vec4 x = line[i0], y = line[i1];
            vec4 yw = vec4(cmul(y.xy, w), cmul(y.zw, w));
            line[i0] = x + yw;
            line[i1] = x - yw;
        }
        barrier();
    }
}
#endif
