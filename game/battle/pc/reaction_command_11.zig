//! Reaction command 11 (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4B10 (pc/src/battle_movement.c)
/// Clears the reaction word (0x228), copies the tile at 0x34 to 0x170 and
/// issues actor command 11.
pub fn reactionCommand11(state: [*]u8) callconv(.c) void {
    psx.byteAt(c_uint, state, 0x228).* = 0;
    psx.byteAt(c_uint, state, 0x170).* = psx.byteAt(c_uint, state, 0x34).*;
    c.func_800DEEFC(state, 11);
}
