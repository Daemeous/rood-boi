#ifndef PC_ENEMY_DIFFICULTY_MATH_H
#define PC_ENEMY_DIFFICULTY_MATH_H
/* Whole percentages; bounds also protect settings loaded from a file or RPC. */
static inline int pc_difficulty_percent(int value)
{
    return value < 0 ? 0 : value > 10000 ? 10000 : value;
}
static inline int pc_difficulty_scale(int value, int extra, int cap)
{
    long long scaled;
    if (value <= 0) return 0;
    scaled = (long long)value * (100 + (long long)extra) / 100;
    return scaled > cap ? cap : (int)scaled;
}
static inline int pc_difficulty_ratio(int value, int numerator, int denominator, int cap)
{
    long long scaled;
    if (value <= 0 || numerator <= 0 || denominator <= 0) return 0;
    scaled = (long long)value * numerator / denominator;
    /* Quantisation must never turn a living enemy or intact limb into zero. */
    return scaled > cap ? cap : scaled < 1 ? 1 : (int)scaled;
}
/* Preserve a limb's original fraction of total HP, including when total HP
 * reaches its cap. Zero-HP actors have no meaningful ratio: keep their limbs. */
static inline int pc_difficulty_limb(int limb, int original_hp, int scaled_hp)
{
    long long scaled;
    if (limb <= 0) return 0;
    if (original_hp <= 0) return limb > 32767 ? 32767 : limb;
    scaled = ((long long)limb * scaled_hp + original_hp / 2) / original_hp;
    return scaled > 32767 ? 32767 : (int)scaled;
}
#endif
