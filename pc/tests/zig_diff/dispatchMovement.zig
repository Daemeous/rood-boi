//! zig_diff spec: func_800E50A0 -> movement_dispatch.dispatchMovement (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes whose priority state (0x125)
//! and kind give the table index 0..5 (every index equally often, each split
//! into state and kind every way: state * 2 + kind), and any heading. An index
//! past the table is not generated: the original reports it
//! (pc_unimplemented, which stops the game) and then calls through the
//! missing entry, which no comparison can follow. The six callees are
//! recording stubs, so the entry called and its arguments are compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").dispatchMovement));

pub const classes = [_][:0]const u8{ "index-0", "index-1", "index-2", "index-3", "index-4", "index-5" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x240, 16);
    for (state[0..0x240]) |*byte| byte.* = ctx.int(u8);
    const index = ctx.range(c_int, 0, 5);
    const priority = ctx.range(c_int, 0, @divTrunc(index, 2));
    state[0x125] = @intCast(priority);
    const angle = ctx.pick(c_int, &.{ 0, 1, -1, 0xfff, 0x7FFFFFFF, -0x80000000 });
    return .{ state, index - priority * 2, angle };
}

pub fn classify(args: Args) usize {
    return @intCast(@as(c_int, args[0][0x125]) * 2 + args[1]);
}
