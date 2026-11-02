//! zig_diff spec: func_8008DDA8 -> func_8008DDA8.func_8008DDA8 (146C.c).
//! Inputs: no collision map, or an arena map whose width and height are
//! small, zero or negative (the C compares them unsigned); points inside,
//! at and past the edges and negative; the tile stub returns heights with
//! and without bit 7, and the base unk4 puts the ceiling above, at and
//! below 0.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008DDA8));

pub const classes = [_][:0]const u8{ "no-map", "outside", "inside" };

var tiles: [2]u8 = .{ 0, 0 };

pub const stubs = struct {
    pub fn func_80099514(_: *zd.Ctx, x: anytype, z: anytype, layer: anytype) u8 {
        _ = x;
        _ = z;
        return tiles[@intCast(layer & 1)];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    tiles[0] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF, 0x40 });
    tiles[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF, 0x40 });
    if (ctx.chance(8)) {
        room.collisionMapSection.raw = 0;
        return .{ ctx.int(c_int), ctx.int(c_int) };
    }
    const map = ctx.alloc(v._mpdRoomCollisionMapSection);
    map.width = ctx.edge(i16, &.{ 1, 4, 16, 0, -1 });
    map.height = ctx.edge(i16, &.{ 1, 4, 16, 0, -1 });
    // the ceiling (unk4 - 2 * height, times 8) above, at and below 0
    map.unk4 = ctx.pick(i16, &.{ 0, 1, 2 * @as(i16, tiles[1] & 0x7F), 2 * @as(i16, tiles[1] & 0x7F) - 1, 0x7FFF, -0x8000 });
    psx.put(&room.collisionMapSection, map);
    const w: c_int = @as(c_int, map.width) << 7;
    const h: c_int = @as(c_int, map.height) << 7;
    const x = ctx.pick(c_int, &.{ 0, 0x7F, w - 1, w, w + 1, -1, 200 });
    const z = ctx.pick(c_int, &.{ 0, 0x7F, h - 1, h, h + 1, -1, 200 });
    return .{ x, z };
}

pub fn classify(args: Args) usize {
    const map = psx.get(v.g.vs_battle_roomData().collisionMapSection) orelse return 0;
    const in_x = @as(u32, @bitCast(args[0] >> 7)) < @as(u32, @bitCast(@as(i32, map.width)));
    const in_z = @as(u32, @bitCast(args[1] >> 7)) < @as(u32, @bitCast(@as(i32, map.height)));
    return if (in_x and in_z) 2 else 1;
}
