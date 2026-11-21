//! Scratchpad random step (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const pad = @import("common").pad;

/// orig: vs_battle_aiNextActorRandomMasked (pc/src/battle_movement.c)
/// Scratchpad linear-congruential step (the movement frame's seed half + increment word, stored back as a word); returns the value & (n - 1).
pub fn scratchRandom(n: c_int) callconv(.c) c_int {
    const frame = pad.move;
    const half: i16 = @truncate(@as(i32, @bitCast(frame.randomState)));
    const value: c_int = @as(c_int, half) +% @as(c_int, @bitCast(frame.randomIncrement));
    frame.randomState = @bitCast(value);
    return value & (n -% 1);
}
