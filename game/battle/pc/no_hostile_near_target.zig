//! Whether no hostile actor stands near a target (D2: 73460.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiHasNearbyEnemy (pc/src/battle_movement.c)
/// 1 unless some hostile actor (relation >= 3) stands within two tiles of
/// the engaged target of `id`. The original starts its list walk at
/// &vs_battle_actors itself, so the first "node" has id vs_battle_actors[1]
/// (a pointer, or 0); its relation word (0x1A4 + id * 4, wrapping) and, when
/// that reads as hostile, its model vs_actors_actorRecords[id] (0x800F4538 + id * 4) and
/// the model's tile (+0x5C) are read through guest RAM as the PS1 does.
pub fn noHostileNearTarget(state: *ai.vs_battle_aiState, id: c_int) callconv(.c) c_int {
    var node: ?*ai.vs_battle_actor = @ptrCast(@alignCast(v.g.vs_battle_actors()));
    while (node) |n| : (node = @ptrCast(n.next)) {
        const other: c_uint = @bitCast(n.id);
        // An id past the list reads PS1 RAM beyond the record (the C's guest_word): never observed, and unimplemented here.
        if (other >= 16) c.pc_unimplemented("actor engagement read outside the actor list");
        const relation: c_uint = psx.fieldAt(u32, &state.tbl.t.targets.bits32, @as(isize, @intCast(other * 4))).*;
        if (((relation >> 6) & 3) < 3) continue;
        const tile: c_uint = psx.byteAt(u32, @as([*]u8, @ptrCast(v.g.vs_actors_actorRecords()[other].?)), 0x5c).*;
        const target: [*]u8 = @ptrCast(psx.at(v.g.vs_field_gimmickInstances(), id).*.?);
        if (c.vs_core_mathSquaredTileDistance(tile, psx.byteAt(u32, target, 0x5c).*) < 3) return 0;
    }
    return 1;
}
