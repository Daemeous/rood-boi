//! Leaving the engaged state (D2: 7399C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800DC19C (pc/src/battle_movement.c)
/// Leaves the engaged state (0x13): restores the saved movement callback
/// (0x6C -> 0x68) and, unless 0x30 is set, queues the recovery motion.
pub fn leaveEngaged(state: [*]u8) callconv(.c) void {
    state[0x126] = 0;
    if (state[0x13] == 0) return;
    state[0x13] = 0;
    c.func_800D820C(state[0x88], 0x40);
    c.func_800E7370(state);
    const saved = @as(*align(1) u32, @ptrCast(state + 0x6c));
    const callback = saved.*;
    saved.* = 0;
    if (callback != 0) @as(*align(1) u32, @ptrCast(state + 0x68)).* = callback;
    if (state[0x30] != 0) return;
    c.func_800D8260(state, 6, 0x28);
    c.func_800E48A8(state, 2, 0x80);
}
