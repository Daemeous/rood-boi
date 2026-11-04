//! zig_diff spec: func_80099514 -> pc.room_height_tile.heightTileValue (D2: pc/src/battle_helpers.c).
//! Inputs: a room map in the arena that vs_battle_roomData + 0x68 points at:
//! a header of four halfwords (the row width in cells at 0, the cells per
//! layer at 3), two layers of packed cells (a tile index in bits 0-10, a
//! height offset in bits 11-15) and the 16-byte tile patterns after them
//! (signed bytes, edges included). (x, z) address a cell of the map and a
//! byte of its tile; the layer is 0 or 1.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").heightTileValue));

pub const classes = [_][:0]const u8{ "tile-negative", "carry-masked", "plain-sum" };

var which: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const width = ctx.range(u32, 1, 4);
    const rows = ctx.range(u32, 1, 3);
    const cells = width * rows;
    const tiles = ctx.range(u32, 1, 4);
    const map = ctx.allocBytes(0x200, 16);
    for (map[0..0x200]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(i16, map[0..2], @intCast(width), .little);
    std.mem.writeInt(i16, map[6..8], @intCast(cells), .little);
    var cell: usize = 0;
    while (cell < 2 * cells) : (cell += 1) {
        const tile: u16 = @intCast(ctx.range(u32, 0, tiles - 1));
        const height: u16 = @as(u16, ctx.range(u8, 0, 31)) << 11;
        std.mem.writeInt(u16, map[8 + cell * 2 ..][0..2], height | tile, .little);
    }
    const tile_area = 8 + cells * 4;
    for (map[tile_area..][0 .. tiles * 16]) |*byte| byte.* = ctx.pick(u8, &.{ 0, 1, 0x7f, 0x80, 0xff, 0xf0, 0x0f, 0x40 });
    std.mem.writeInt(u32, v.g.vs_battle_roomData()[0x68..][0..4], @intCast(@intFromPtr(map)), .little);
    const x = ctx.range(c_uint, 0, 4 * width - 1);
    const z = ctx.range(c_uint, 0, 4 * rows - 1);
    const layer = ctx.range(c_uint, 0, 1);

    const index = layer * cells + width * (z >> 2) + (x >> 2);
    const packed_cell = std.mem.readInt(u16, map[8 + index * 2 ..][0..2], .little);
    const value: i8 = @bitCast(map[tile_area + (packed_cell & 0x7ff) * 16 + (z & 3) * 4 + (x & 3)]);
    const bias: c_int = (packed_cell & 0xf800) >> 9;
    which = if (value < 0) 0 else if (value + bias > 0x7f) 1 else 2;
    return .{ x, z, layer };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
