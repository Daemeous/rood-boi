//! zig_diff spec: func_800E4FB0 -> beyond_reach.beyondReach (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes; an id -4..20 (the distance
//! word at 0x1E4 + id * 4 stays in the block); that word an edge around 100
//! (99, 100, 101), the unsigned extremes or any value; `range` any int (the C
//! does not read it).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").beyondReach));

pub const classes = [_][:0]const u8{ "within", "beyond" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x300, 16);
    for (state[0..0x300]) |*byte| byte.* = ctx.int(u8);
    const id = ctx.range(c_int, -4, 20);
    const offset: usize = @intCast(0x1e4 + id * 4);
    @as(*align(1) u32, @ptrCast(state + offset)).* = ctx.pick(u32, &.{ 0, 99, 100, 101, 0xffffffff, 0x80000000, 0x7fffffff });
    return .{ state, id, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const offset: usize = @intCast(0x1e4 + args[1] * 4);
    return @intFromBool(100 < @as(*align(1) const u32, @ptrCast(args[0] + offset)).*);
}
