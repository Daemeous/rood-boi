//! Whether a tile can be entered (D2: 70D4C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;
const pad = @import("common").pad;

/// orig: nav_cell (pc/src/battle_movement.c)
/// The navigation cell of tile (x, z) in the grid cached at scratchpad 0x3C0.
fn navCell(x: c_int, z: c_int) *align(1) u16 {
    const cells: usize = @intFromPtr(pad.nav.cells.?);
    return @ptrFromInt(cells +% @as(usize, @bitCast(@as(isize, z *% 64 +% x *% 2))));
}

/// orig: vs_battle_aiDestinationOccupancy (pc/src/battle_movement.c)
/// Whether `tile` can be entered: 0 for the reserved tile (0x38, compared on
/// its low 24 bits against tile with byte 1 cleared), outside the grid, or
/// blocked cell flags (0x80B0 plus the actor's mask 0x32 in bits 8-15); -1
/// when another actor's bounds overlap the tile centre; else 1.
pub fn tileEnterable(state: *ai.vs_battle_aiState, tile: c_uint) callconv(.c) c_int {
    if (state.previous.stuckMovementPosition.raw << 8 == (tile & 0xffff00ff) << 8) return 0;
    const x: c_int = @intCast(tile & 0xff);
    const z: c_int = @intCast((tile >> 16) & 0xff);
    const height = c.vs_field_groundHeightAtTile(x, z);
    const point_x = x *% 128 +% 64;
    const point_z = z *% 128 +% 64;
    if (!(x < pad.move.width and z < pad.move.depth)) return 0;
    if (@as(c_int, navCell(x, z).*) & ((@as(c_int, state.f32) << 8) | 0x80b0) != 0) return 0;
    const self_size: c_int = state.collisionRadius;
    var node: ?*ai.vs_battle_actor = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(@alignCast(first)) else null;
    while (node) |n| : (node = @ptrCast(n.next)) {
        const id = n.id;
        if (id == state.id) continue;
        const model: [*]u8 = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), id).*.?);
        // An actor without an AI state has a null slot: the PC C then reads
        // the null page (the original read PS1 address 0).
        const other: *align(4) ai.vs_battle_aiState = if (pad.aiState(id)) |w| @ptrCast(@alignCast(w)) else @alignCast(@ptrCast(psx.nullObject(ai.vs_battle_aiState)));
        var position: c_uint = undefined;
        var depth: c_uint = undefined;
        if (id != 0 and model[0xa] & 0x1f != 0) {
            position = other.unk174;
            depth = other.unk178;
        } else {
            position = psx.byteAt(u32, model, 0x1c).*;
            depth = psx.byteAt(u16, model, 0x20).*;
        }
        var dx: c_int = @as(c_int, @intCast(position & 0xffff)) -% point_x;
        var dy: c_int = (@as(c_int, @bitCast(position)) >> 16) -% height;
        var dz: c_int = @as(c_int, @intCast(depth)) -% point_z;
        const reach: c_int = @as(c_int, other.collisionRadius) +% self_size;
        if (dx < 0) dx = -%dx;
        if (dy < 0) dy = -%dy;
        if (dz < 0) dz = -%dz;
        if (!(reach < dx) and !(0x80 < dy) and !(reach < dz)) return -1;
    }
    return 1;
}
