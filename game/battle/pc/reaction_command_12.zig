//! Reaction command 12 (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4B18 (pc/src/battle_movement.c)
/// The same with actor command 12.
pub fn reactionCommand12(state: [*]u8) callconv(.c) void {
    psx.byteAt(c_uint, state, 0x228).* = 0;
    psx.byteAt(c_uint, state, 0x170).* = psx.byteAt(c_uint, state, 0x34).*;
    c.func_800DEEFC(state, 12);
}
