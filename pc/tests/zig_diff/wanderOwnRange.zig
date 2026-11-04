//! zig_diff spec: func_800DB5B0 -> pc.wander_own_range.wanderOwnRange (D2: pc/src/battle_movement.c).
//! Inputs: a random-filled state block whose tile word (0xCC) and range word
//! (0xC8) are edges or any value. The stub of func_800DB5B8 records (state,
//! tile, range), so swapped or mis-sized fields show.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").wanderOwnRange));

pub const classes = [_][:0]const u8{ "tile-zero", "tile-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(u32, block[0xcc..][0..4], ctx.pick(u32, &.{ 0, 0x00FF00FF, 0xFFFFFFFF, 0x80000000 }), .little);
    std.mem.writeInt(i32, block[0xc8..][0..4], ctx.pick(i32, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 }), .little);
    return .{block};
}

pub fn classify(args: Args) usize {
    return @intFromBool(std.mem.readInt(u32, args[0][0xcc..][0..4], .little) != 0);
}
