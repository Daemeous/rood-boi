//! Choosing an actor's movement mode (D2: 70D4C.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: func_800D9DD8 (pc/src/battle_helpers.c)
/// Chooses the movement mode at 0x12E: mode 1 for an actor already running
/// func_800DAA6C (its callback at 0x68) or whose best candidate (pointer at
/// 0x430) has no flag of its word 4 in the mask at 0x2D; otherwise mode 2 and
/// a waypoint search into 0x134.
pub fn chooseMovementMode(state: [*]u8) callconv(.c) void {
    const best: ?[*]align(1) c_uint = @ptrFromInt(@as(usize, psx.byteAt(u32, state, 0x430).*));
    if (@as(usize, psx.byteAt(u32, state, 0x68).*) == @intFromPtr(&c.func_800DAA6C) or best.?[4] & state[0x2d] == 0) {
        state[0x12e] = 1;
        return;
    }
    state[0x12e] = 2;
    c.func_800DD7CC(state, psx.byteAt(u8, state, 0x134));
}
