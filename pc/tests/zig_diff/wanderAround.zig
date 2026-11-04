//! zig_diff spec: func_800DB5B8 -> pc.wander_around.wanderAround (D2: pc/src/battle_movement.c).
//! Inputs: a state block, a tile (edges or any word) and a range (edges or any
//! int). The stubs of func_800DB4AC and func_800E1EAC record their arguments:
//! the mode (3) and the order of the two calls show, and the tile and range
//! must reach the second call unchanged.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").wanderAround));

pub const classes = [_][:0]const u8{ "tile-zero", "tile-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x100, 16);
    for (block[0..0x100]) |*byte| byte.* = ctx.int(u8);
    return .{
        block,
        ctx.pick(c_uint, &.{ 0, 0x00FF00FF, 0xFFFFFFFF, 0x80000000 }),
        ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 }),
    };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[1] != 0);
}
