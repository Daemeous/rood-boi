//! Step plan reset (D2: 7B3C8.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E3CDC (pc/src/battle_movement.c)
/// Forgets (0x194/0x198/0x19A) a tile the actor keeps revisiting (5 frames
/// running blocks it for 120), clears 0x156, and stores the step count (0x7E,
/// or 0x1F at random for non-flagged actors) in flags bits 5-11.
pub fn resetStepPlan(state: [*]u8, flags: c_uint) callconv(.c) c_uint {
    const tile = @as(*align(1) u32, @ptrCast(state + 0x34)).*;
    const last = @as(*align(1) u32, @ptrCast(state + 0x194));
    var repeats: c_int = if ((last.* & 0xff00ff) == (tile & 0xff00ff)) @as(c_int, state[0x198]) +% 1 else 0;
    if (repeats >= 5) {
        state[0x19a] = 120;
        repeats = 0;
    }
    last.* = tile;
    state[0x198] = @truncate(@as(c_uint, @bitCast(repeats)));
    @as(*align(1) u16, @ptrCast(state + 0x156)).* = 0;
    var steps: c_uint = 0x7e;
    if (state[0x23] == 0 and c.func_800E45D4(4) == 0) steps = 0x7e >> 2;
    return (flags & 0xfffff01f) | (steps << 5);
}
