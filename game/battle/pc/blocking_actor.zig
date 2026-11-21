//! The actor blocking a direction (D2: 70D4C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;
const pad = @import("common").pad;

/// orig: direction_dx (pc/src/battle_movement.c)
/// The signed X step in bits 0-1 of a packed direction.
fn directionDx(packed_direction: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_direction)) << 30)) >> 30;
}

/// orig: direction_dz (pc/src/battle_movement.c)
/// The signed Z step in bits 4-5 of a packed direction.
fn directionDz(packed_direction: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_direction)) << 26)) >> 30;
}

/// orig: vs_battle_aiDirectionalBlockingActor (pc/src/battle_movement.c)
/// id + 1 of the first other actor whose bounds overlap the point one
/// radius ahead of `state` in direction `dir`, else 0; cached per direction
/// in scratchpad 0x3D2 (valid bits) / 0x3D4 + dir.
pub fn blockingActor(state: *ai.vs_battle_aiState, dir: c_int) callconv(.c) c_int {
    // Shift counts are directions 0..7; the x86 shift takes them modulo 32.
    if ((@as(c_int, pad.move.cache.bits.blockTested) >> @truncate(@as(c_uint, @bitCast(dir)))) & 1 != 0) return pad.blockerAt(dir).*;
    const packed_direction: c_int = pad.directionAt(dir).*;
    const dx = directionDx(packed_direction);
    const dz = directionDz(packed_direction);
    const radius: c_int = state.step;
    const x: c_uint = @bitCast(@as(c_int, state.position.xyz[0]) +% radius *% dx);
    const z: c_uint = @bitCast(@as(c_int, state.position.xyz[2]) +% radius *% dz);
    const tile = state.tile.raw;
    const tile_x: c_int = @intCast(tile & 0xff);
    const tile_z: c_int = @intCast((tile >> 16) & 0xff);
    const neighbour: c_int = @bitCast(((z >> 7) -% @as(c_uint, @bitCast(tile_z)) +% 1) *% 4 +%
        ((x >> 7) -% @as(c_uint, @bitCast(tile_x)) +% 1));
    const lookup: c_int = @as(i8, @bitCast(pad.lookupAt(neighbour).*));
    var open: c_int = 1;
    if (lookup >= 0) {
        if (pad.nav.exits != null) {
            const bitmap: [*]u8 = pad.nav.bitmap.?;
            const index: c_int = @as(c_int, pad.move.width) *% tile_z +% tile_x;
            open = (@as(c_int, psx.at(bitmap, index).*) >> @truncate(@as(c_uint, @bitCast(lookup)))) & 1;
        } else {
            open = c.vs_battle_aiIsAdjacentTileStepBlocked(tile, lookup);
        }
    }
    const height: c_int = if (open != 0) state.position.xyz[1] else c.vs_field_groundHeightAtWorldPosition(@bitCast(x), @bitCast(z));
    const self_size: c_uint = state.collisionRadius;
    const self_height: c_uint = @as(u16, @bitCast(state.extent));
    var result: c_int = 0;
    var node: ?*ai.vs_battle_actor = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(@alignCast(first)) else null;
    while (node) |n| : (node = @ptrCast(n.next)) {
        const id = n.id;
        if (id == state.id) continue;
        const model: [*]u8 = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), id).*.?);
        const other: ?*align(4) ai.vs_battle_aiState = @ptrCast(@alignCast(pad.aiState(id)));
        var position: c_uint = undefined;
        var depth: c_uint = undefined;
        if (id != 0 and model[0xa] & 0x1f != 0) {
            position = other.?.unk174;
            depth = other.?.unk178;
        } else {
            position = psx.byteAt(u32, model, 0x1c).*;
            depth = psx.byteAt(u16, model, 0x20).*;
        }
        var ox: c_int = @as(c_int, @intCast(position & 0xffff)) -% @as(c_int, @bitCast(x));
        var oy: c_int = (@as(c_int, @bitCast(position)) >> 16) -% height;
        var oz: c_int = @as(c_int, @intCast(depth)) -% @as(c_int, @bitCast(z));
        if ((dx > 0 and ox < 0) or (dx < 0 and ox > 0) or (dz > 0 and oz < 0) or (dz < 0 and oz > 0)) continue;
        if (ox <= 0) ox = -%ox;
        if (oy <= 0) oy = -%oy;
        if (oz <= 0) oz = -%oz;
        const reach: c_int = @as(c_int, other.?.collisionRadius) +% other.?.step +% @as(c_int, @bitCast(self_size));
        if (ox -% reach > 0 or oz -% reach > 0) continue;
        if (oy -% @as(c_int, @bitCast((self_height +% @as(u16, @bitCast(other.?.extent))) >> 1)) > 0) continue;
        result = id +% 1;
        break;
    }
    pad.blockerAt(dir).* = psx.conv(u8, result);
    pad.move.cache.bits.blockTested |= psx.conv(u8, @as(c_uint, 1) << @truncate(@as(c_uint, @bitCast(dir))));
    return result;
}
