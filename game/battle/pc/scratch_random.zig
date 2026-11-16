//! Scratchpad random step (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const pad = @import("common").pad;

/// orig: vs_battle_aiNextActorRandomMasked (pc/src/battle_movement.c)
/// Scratchpad linear-congruential step (seed half 0x3F8 + increment word
/// 0x3FC, stored back as a word); returns the value & (n - 1).
pub fn scratchRandom(n: c_int) callconv(.c) c_int {
    const seed: *u32 = @ptrFromInt(pad.addr(0x3F8));
    const increment: *const u32 = @ptrFromInt(pad.addr(0x3FC));
    const half: i16 = @bitCast(@as(*const u16, @ptrFromInt(pad.addr(0x3F8))).*);
    const value: c_int = @as(c_int, half) +% @as(c_int, @bitCast(increment.*));
    seed.* = @bitCast(value);
    return value & (n -% 1);
}
