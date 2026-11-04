//! zig_diff spec: func_800DB5DC -> pc.start_walk_to_tile.startWalkToTile (D2: pc/src/battle_movement.c).
//! Inputs: a state block with the position halfwords (0x4C, 0x4E, 0x50) near
//! the tile centre or anywhere, a tile (low 24 bits used, also edges) and a
//! range (small, edges, large: the square wraps). The stub of func_800DB4AC
//! rewrites the tile word at 0x168 (the walk reads it back after the call);
//! func_800E42E0 answers a height and func_800E4660 a squared distance, both
//! drawn so the comparison lands on each side and on equality.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startWalkToTile));

pub const classes = [_][:0]const u8{ "arrived", "moving", "equal-distance" };

var height: c_int = 0;
var distance: c_int = 0;
var new_tile: u32 = 0;
var scenario: usize = 0;

pub const stubs = struct {
    pub fn func_800DB4AC(ctx: *zd.Ctx, state: anytype, mode: anytype) c_int {
        _ = ctx;
        _ = mode;
        const bytes: [*]u8 = @ptrCast(state);
        std.mem.writeInt(u32, bytes[0x168..][0..4], new_tile, .little);
        return 0;
    }
    pub fn func_800E42E0(ctx: *zd.Ctx, tile: anytype) c_int {
        _ = ctx;
        _ = tile;
        return height;
    }
    pub fn func_800E4660(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype) c_int {
        _ = ctx;
        _ = x;
        _ = y;
        _ = z;
        return distance;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    const tile = ctx.pick(c_uint, &.{ 0, 0x00FF00FF, 0xFFFFFFFF, 0x00030002, 0x80000000 });
    new_tile = ctx.pick(u32, &.{ 0, 0x00FF00FF, 0xFFFFFFFF, 0x00030002, 0x00010001 });
    height = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    const range = ctx.pick(c_int, &.{ 0, 1, 2, -2, 0x100, 0x10000, 0x7FFFFFFF, -0x80000000 });
    const square: c_uint = @bitCast(range *% range);
    scenario = ctx.range(usize, 0, 2);
    distance = switch (scenario) {
        0 => @bitCast(square -% 1), // below: arrived (when the square is nonzero)
        1 => @bitCast(square +% 1),
        else => @bitCast(square),
    };
    if (ctx.chance(5)) distance = ctx.int(c_int);
    return .{ block, tile, range };
}

pub fn classify(args: Args) usize {
    const square: c_uint = @bitCast(args[2] *% args[2]);
    const d: c_uint = @bitCast(distance);
    if (d == square) return 2;
    return if (d < square) 0 else 1;
}
