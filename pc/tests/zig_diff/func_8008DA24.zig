//! zig_diff spec: func_8008DA24 -> func_8008DA24.func_8008DA24 (146C.c).
//! Inputs: no collision map, or an arena map 1, 2 or 4 tiles across (0 and
//! -1 too: the C compares unsigned); points on the edge tiles and past
//! them, at the fractions that decide the side (0, 15, 16, 31); tile
//! heights from a 4 x 4 table whose neighbours differ by 0, 4 (the 0x40
//! cut-off exactly) and more, with and without bit 7. NormalClip's stub is
//! libgte's formula (the cross product of the 16-bit halves), so the
//! weights sum to 0x400 or less as in the game.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008DA24));

pub const classes = [_][:0]const u8{ "no-map", "outside", "inside" };

var heights: [16]u8 = @splat(0);

pub const stubs = struct {
    pub fn func_80099514(_: *zd.Ctx, x: anytype, z: anytype, layer: anytype) u8 {
        _ = layer;
        return heights[(@as(u32, x) & 3) + 4 * (@as(u32, @bitCast(z)) & 3)];
    }
    pub fn NormalClip(_: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        const ax: i32 = @as(i16, @truncate(a));
        const ay: i32 = @as(i16, @truncate(a >> 16));
        const bx: i32 = @as(i16, @truncate(b));
        const by: i32 = @as(i16, @truncate(b >> 16));
        const cx: i32 = @as(i16, @truncate(cc));
        const cy: i32 = @as(i16, @truncate(cc >> 16));
        return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    }
};

fn coordinate(ctx: *zd.Ctx, tiles: i16) c_int {
    const span: c_int = @as(c_int, tiles) * 4;
    const tile = ctx.edge(c_int, &.{ 0, 1, span - 1, span - 2, span, -1, 2 });
    const fraction = ctx.edge(c_int, &.{ 0, 1, 15, 16, 17, 31, 8, 24 });
    return tile *% 32 +% fraction;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    const base = ctx.range(u8, 0, 0x60);
    for (&heights) |*h| h.* = (base + ctx.edge(u8, &.{ 0, 0, 4, 3, 5, 8, 0x1F })) | (if (ctx.chance(3)) @as(u8, 0x80) else 0);
    if (ctx.chance(10)) {
        room.collisionMapSection.raw = 0;
        return .{ ctx.int(c_int), ctx.int(c_int) };
    }
    const map = ctx.alloc(v._mpdRoomCollisionMapSection);
    map.width = ctx.edge(i16, &.{ 1, 2, 4, 4, 0, -1 });
    map.height = ctx.edge(i16, &.{ 1, 2, 4, 4, 0, -1 });
    map.unk4 = ctx.pick(i16, &.{ 0, 0x100, 0x7FFF, -0x8000, 0xC0 });
    psx.put(&room.collisionMapSection, map);
    return .{ coordinate(ctx, map.width), coordinate(ctx, map.height) };
}

pub fn classify(args: Args) usize {
    const map = psx.get(v.g.vs_battle_roomData().collisionMapSection) orelse return 0;
    const in_x = @as(u32, @bitCast(args[0] >> 7)) < @as(u32, @bitCast(@as(i32, map.width)));
    const in_z = @as(u32, @bitCast(args[1] >> 7)) < @as(u32, @bitCast(@as(i32, map.height)));
    return if (in_x and in_z) 2 else 1;
}
