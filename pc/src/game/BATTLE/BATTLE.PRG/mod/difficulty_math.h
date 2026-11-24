/* Bounded PS1 integer arithmetic: no 64-bit runtime helpers are needed. */
#ifndef VS_DIFFICULTY_MATH_H
#define VS_DIFFICULTY_MATH_H
static int vs_diff_percent(int v) { return v < 0 ? 0 : v > 10000 ? 10000 : v; }
static int vs_diff_scale(int v, int extra, int cap) {
    if (v <= 0) return 0;
    if ((unsigned int)v > (unsigned int)(cap * 100) / (100 + extra)) return cap;
    return v * (100 + extra) / 100;
}
static int vs_diff_ratio(int v, int n, int d, int cap) {
    unsigned int scaled;
    if (v <= 0 || n <= 0 || d <= 0) return 0;
    scaled = (unsigned int)v * n / d;
    return scaled > (unsigned int)cap ? cap : scaled < 1 ? 1 : (int)scaled;
}
static int vs_diff_limb(int v, int original, int scaled_hp) {
    unsigned int scaled;
    if (v <= 0) return 0;
    if (original <= 0) return v > 32767 ? 32767 : v;
    scaled = ((unsigned int)v * scaled_hp + original / 2) / original;
    return scaled > 32767 ? 32767 : scaled;
}
#endif
