//! Approach point refresh while approaching (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E48F8 (pc/src/battle_movement.c)
/// Refreshes the approach point (func_800E4904) while in approach mode (0x89 == 1).
pub fn refreshApproachPointIfApproaching(state: [*]u8) callconv(.c) void {
    if (state[0x89] == 1) c.func_800E4904(state);
}
