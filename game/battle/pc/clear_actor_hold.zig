//! Clearing an actor hold (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E6B24 (pc/src/battle_movement.c)
/// Clears actor `id & 0xFF`'s hold flag (0x1A); an empty slot does nothing.
pub fn clearActorHold(id: c_int) callconv(.c) void {
    const state: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id & 0xff).* orelse return);
    state[0x1a] = 0;
}
