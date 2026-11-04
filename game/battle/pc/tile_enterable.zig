//! Whether a tile can be entered (D2: 70D4C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

fn scratch8(offset: c_int) *u8 {
    return @ptrFromInt(@as(u32, @bitCast(0x1f800000 +% offset)));
}

/// orig: nav_cell (pc/src/battle_movement.c)
/// The navigation cell of tile (x, z) in the grid cached at scratchpad 0x3C0.
fn navCell(x: c_int, z: c_int) *align(1) u16 {
    const cells: usize = psx.byteAt(u32, scratch8(0x3c0), 0).*;
    return @ptrFromInt(cells +% @as(usize, @bitCast(@as(isize, z *% 64 +% x *% 2))));
}

/// orig: func_800D954C (pc/src/battle_movement.c)
/// Whether `tile` can be entered: 0 for the reserved tile (0x38, compared on
/// its low 24 bits against tile with byte 1 cleared), outside the grid, or
/// blocked cell flags (0x80B0 plus the actor's mask 0x32 in bits 8-15); -1
/// when another actor's bounds overlap the tile centre; else 1.
pub fn tileEnterable(state: [*]u8, tile: c_uint) callconv(.c) c_int {
    if (psx.byteAt(u32, state, 0x38).* << 8 == (tile & 0xffff00ff) << 8) return 0;
    const x: c_int = @intCast(tile & 0xff);
    const z: c_int = @intCast((tile >> 16) & 0xff);
    const height = c.func_800E42EC(x, z);
    const point_x = x *% 128 +% 64;
    const point_z = z *% 128 +% 64;
    if (!(x < scratch8(0x3dc).* and z < scratch8(0x3dd).*)) return 0;
    if (@as(c_int, navCell(x, z).*) & ((@as(c_int, state[0x32]) << 8) | 0x80b0) != 0) return 0;
    const self_size: c_int = psx.byteAt(u16, state, 0x94).*;
    var node: ?[*]u8 = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(first) else null;
    while (node) |n| : (node = @ptrFromInt(psx.byteAt(u32, n, 0).*)) {
        const id = psx.byteAt(c_int, n, 4).*;
        if (id == state[0x88]) continue;
        const model: [*]u8 = @ptrCast(psx.at(v.g.D_800F4538(), id).*.?);
        // An actor without an AI state has a null slot: the PC C then reads
        // PS1 address 0 (RAM start), mapped at 0x80000000.
        const word = psx.byteAt(u32, scratch8(0x37c +% id *% 4), 0).*;
        const other: [*]u8 = if (word != 0) @ptrFromInt(word) else @ptrFromInt(0x80000000);
        var position: c_uint = undefined;
        var depth: c_uint = undefined;
        if (id != 0 and model[0xa] & 0x1f != 0) {
            position = psx.byteAt(u32, other, 0x174).*;
            depth = psx.byteAt(u16, other, 0x178).*;
        } else {
            position = psx.byteAt(u32, model, 0x1c).*;
            depth = psx.byteAt(u16, model, 0x20).*;
        }
        var dx: c_int = @as(c_int, @intCast(position & 0xffff)) -% point_x;
        var dy: c_int = (@as(c_int, @bitCast(position)) >> 16) -% height;
        var dz: c_int = @as(c_int, @intCast(depth)) -% point_z;
        const reach: c_int = @as(c_int, psx.byteAt(u16, other, 0x94).*) +% self_size;
        if (dx < 0) dx = -%dx;
        if (dy < 0) dy = -%dy;
        if (dz < 0) dz = -%dz;
        if (!(reach < dx) and !(0x80 < dy) and !(reach < dz)) return -1;
    }
    return 1;
}
