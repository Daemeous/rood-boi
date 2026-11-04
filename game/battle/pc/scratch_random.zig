//! Scratchpad random step (D2: 7BDD4.s; oracle pc/src/battle_movement.c).

/// orig: func_800E45D4 (pc/src/battle_movement.c)
/// Scratchpad linear-congruential step (seed half 0x3F8 + increment word
/// 0x3FC, stored back as a word); returns the value & (n - 1).
pub fn scratchRandom(n: c_int) callconv(.c) c_int {
    const seed: *u32 = @ptrFromInt(0x1f8003f8);
    const increment: *const u32 = @ptrFromInt(0x1f8003fc);
    const half: i16 = @bitCast(@as(*const u16, @ptrFromInt(0x1f8003f8)).*);
    const value: c_int = @as(c_int, half) +% @as(c_int, @bitCast(increment.*));
    seed.* = @bitCast(value);
    return value & (n -% 1);
}
