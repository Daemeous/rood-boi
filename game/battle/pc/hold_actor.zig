//! Holding an actor (D2: 7DE94.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E6764 (pc/src/battle_movement.c)
/// Releases actor `id`'s tile reservation (func_800E678C) and marks it held
/// (0x1A = 1); an empty slot does nothing.
pub fn holdActor(id: c_int) callconv(.c) void {
    const state: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id).* orelse return);
    c.func_800E678C(state);
    state[0x1a] = 1;
}
