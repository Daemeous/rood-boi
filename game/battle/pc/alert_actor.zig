//! Alerting an actor (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E4C64 (pc/src/battle_movement.c)
/// Sets actor `id`'s 0x13F countdown to 30 and continues into func_800E4C8C
/// with that actor; id 0 and empty slots do nothing.
pub fn alertActor(id: c_int) callconv(.c) void {
    if (id == 0) return;
    const state: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id).* orelse return);
    state[0x13f] = 30;
    c.func_800E4C8C(state);
}
