//! The actor blocking a direction (D2: 70D4C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

fn scratch8(offset: c_int) *u8 {
    return @ptrFromInt(@as(u32, @bitCast(0x1f800000 +% offset)));
}

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

/// orig: func_800D98E8 (pc/src/battle_movement.c)
/// id + 1 of the first other actor whose bounds overlap the point one
/// radius ahead of `state` in direction `dir`, else 0; cached per direction
/// in scratchpad 0x3D2 (valid bits) / 0x3D4 + dir.
pub fn blockingActor(state: [*]u8, dir: c_int) callconv(.c) c_int {
    // Shift counts are directions 0..7; the x86 shift takes them modulo 32.
    if ((@as(c_int, scratch8(0x3d2).*) >> @truncate(@as(c_uint, @bitCast(dir)))) & 1 != 0) return scratch8(0x3d4 +% dir).*;
    const packed_direction: c_int = scratch8(0x3ec +% dir).*;
    const dx = directionDx(packed_direction);
    const dz = directionDz(packed_direction);
    const radius: c_int = state[0xb3];
    const x: c_uint = @bitCast(@as(c_int, psx.byteAt(u16, state, 0x4c).*) +% radius *% dx);
    const z: c_uint = @bitCast(@as(c_int, psx.byteAt(u16, state, 0x50).*) +% radius *% dz);
    const tile = psx.byteAt(u32, state, 0x34).*;
    const tile_x: c_int = @intCast(tile & 0xff);
    const tile_z: c_int = @intCast((tile >> 16) & 0xff);
    const neighbour: c_int = @bitCast(((z >> 7) -% @as(c_uint, @bitCast(tile_z)) +% 1) *% 4 +%
        ((x >> 7) -% @as(c_uint, @bitCast(tile_x)) +% 1));
    const lookup: c_int = @as(i8, @bitCast(scratch8(0x3e0 +% neighbour).*));
    var open: c_int = 1;
    if (lookup >= 0) {
        if (psx.byteAt(u32, scratch8(0x3f4), 0).* != 0) {
            const bitmap: [*]u8 = @ptrFromInt(psx.byteAt(u32, scratch8(0x3c4), 0).*);
            const index: c_int = @as(c_int, scratch8(0x3dc).*) *% tile_z +% tile_x;
            open = (@as(c_int, psx.at(bitmap, index).*) >> @truncate(@as(c_uint, @bitCast(lookup)))) & 1;
        } else {
            open = c.func_800E4690(tile, lookup);
        }
    }
    const height: c_int = if (open != 0) psx.byteAt(i16, state, 0x4e).* else c.func_800E42D4(@bitCast(x), @bitCast(z));
    const self_size: c_uint = psx.byteAt(u16, state, 0x94).*;
    const self_height: c_uint = psx.byteAt(u16, state, 0xb8).*;
    var result: c_int = 0;
    var node: ?[*]u8 = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(first) else null;
    while (node) |n| : (node = @ptrFromInt(psx.byteAt(u32, n, 0).*)) {
        const id = psx.byteAt(c_int, n, 4).*;
        if (id == state[0x88]) continue;
        const model: [*]u8 = @ptrCast(psx.at(v.g.D_800F4538(), id).*.?);
        const other: [*]u8 = @ptrFromInt(psx.byteAt(u32, scratch8(0x37c +% id *% 4), 0).*);
        var position: c_uint = undefined;
        var depth: c_uint = undefined;
        if (id != 0 and model[0xa] & 0x1f != 0) {
            position = psx.byteAt(u32, other, 0x174).*;
            depth = psx.byteAt(u16, other, 0x178).*;
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
        const reach: c_int = @as(c_int, psx.byteAt(u16, other, 0x94).*) +% other[0xb3] +% @as(c_int, @bitCast(self_size));
        if (ox -% reach > 0 or oz -% reach > 0) continue;
        if (oy -% @as(c_int, @bitCast((self_height +% psx.byteAt(u16, other, 0xb8).*) >> 1)) > 0) continue;
        result = id +% 1;
        break;
    }
    scratch8(0x3d4 +% dir).* = psx.conv(u8, result);
    scratch8(0x3d2).* |= psx.conv(u8, @as(c_uint, 1) << @truncate(@as(c_uint, @bitCast(dir))));
    return result;
}
