//! Dropping targets on an actor (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E7608 (pc/src/battle_movement.c)
/// Walks the actor list from vs_battle_actors[0]: every actor whose 0x16
/// target is `id` drops it (0x10); then releases actor `id`'s reservation
/// through func_800D82CC.
pub fn clearActorTargets(id: c_int) callconv(.c) void {
    var node: [*]u8 = @ptrCast(v.g.vs_battle_actors()[0].?);
    while (true) {
        const state: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), psx.byteAt(c_uint, node, 4).*).*.?);
        if (@as(c_uint, state[0x16]) == @as(c_uint, @bitCast(id))) state[0x16] = 0x10;
        const next = psx.byteAt(c_uint, node, 0).*;
        if (next == 0) break;
        node = @ptrFromInt(next);
    }
    c.func_800D82CC(psx.at(v.g.D_800F5878(), id).*);
}
