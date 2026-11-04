//! Finding an actor by relation (D2: 72B70.s; oracle pc/src/battle_movement.c).
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DBB2C (pc/src/battle_movement.c)
/// First other actor whose relation bits 6-7 equal `relation`; stores its
/// object's tile in *tile when given. Returns its id or -1.
pub fn findActorByRelation(state: [*]u8, tile: ?*u32, relation: c_int) callconv(.c) c_int {
    var node: ?[*]u8 = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(first) else null;
    while (node) |n| : (node = @ptrFromInt(psx.byteAt(u32, n, 0).*)) {
        const id = psx.byteAt(c_int, n, 4).*;
        if (id == state[0x88]) continue;
        if ((psx.byteAt(u32, state, 0x1a4 +% @as(isize, id *% 4)).* & 0xc0) != psx.conv(c_uint, relation) << 6) continue;
        if (tile) |out| {
            const object: [*]u8 = @ptrFromInt(psx.byteAt(u32, n, 0x44).*);
            out.* = psx.byteAt(u32, object, 0x5c).*;
        }
        return id;
    }
    return -1;
}
