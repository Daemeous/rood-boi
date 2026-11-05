//! Entering the engaged state (D2: 7399C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DC210 (pc/src/battle_movement.c)
/// Enters the engaged state (0x13) when forced, or on the second of two
/// `rand() & 0x7F < 0x4C` rolls (counted in 0x126): saves the movement
/// callback (0x68 -> 0x6C) and installs behaviour D_800DBC0C.
pub fn enterEngaged(state: [*]u8, force: c_int) callconv(.c) void {
    if (force == 0) {
        if ((@as(c_uint, @bitCast(c.rand())) & 0x7f) >= 0x4c) return;
        state[0x126] +%= 1;
        if (state[0x126] < 2) return;
    }
    state[0x13] = 1;
    const callback: *align(1) u32 = @ptrCast(state + 0x68);
    const saved = callback.*;
    callback.* = psx.addr(&c.behaviour_800DBC0C);
    @as(*align(1) u32, @ptrCast(state + 0x6c)).* = saved;
    c.func_800D820C(state[0x88], 0x80);
}
