//! Finding an actor by relation (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiFindActorByRelationship (pc/src/battle_movement.c)
/// First other actor whose relation bits 6-7 equal `relation`; stores its
/// object's tile in *tile when given. Returns its id or -1.
pub fn findActorByRelation(state: *ai.vs_battle_aiState, tile: ?*u32, relation: c_int) callconv(.c) c_int {
    var node: ?*ai.vs_battle_actor = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(@alignCast(first)) else null;
    while (node) |n| : (node = @ptrCast(n.next)) {
        const id = n.id;
        if (id == state.id) continue;
        if ((psx.fieldAt(u32, &state.tbl.t.targets.bits32, @as(isize, id *% 4)).* & 0xc0) != psx.conv(c_uint, relation) << 6) continue;
        if (tile) |out| {
            const object: [*]u8 = @ptrCast(n.unk44);
            out.* = psx.byteAt(u32, object, 0x5c).*;
        }
        return id;
    }
    return -1;
}
