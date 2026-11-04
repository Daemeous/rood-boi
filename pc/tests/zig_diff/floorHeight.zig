//! zig_diff spec: func_800E4320 -> pc.floor_height.floorHeight (D2: pc/src/battle_movement.c).
//! Inputs: a floor grid (64-byte rows of random 16-bit cells) in the arena
//! whose address is the scratchpad word at 0x3BC, with room for a row and a
//! column before it (tiles -2..30 by -2..13 are read); cells with and without
//! bit 9 (negative raw height) and with every bits 10-12 value.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").floorHeight));

pub const classes = [_][:0]const u8{ "positive", "negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x600, 16);
    for (block[0..0x600]) |*byte| byte.* = ctx.int(u8);
    const grid = block + 0x100; // two rows of room before the grid
    @as(*u32, @ptrFromInt(0x1F8003BC)).* = @intCast(@intFromPtr(grid));
    return .{ ctx.range(c_int, -2, 30), ctx.range(c_int, -2, 13) };
}

pub fn classify(args: Args) usize {
    const grid: isize = @intCast(@as(*const u32, @ptrFromInt(0x1F8003BC)).*);
    const address: usize = @intCast(grid + @as(isize, args[1]) * 64 + @as(isize, args[0]) * 2);
    return (@as(*align(1) const u16, @ptrFromInt(address)).* >> 9) & 1;
}
