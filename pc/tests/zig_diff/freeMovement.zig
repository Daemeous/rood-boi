//! zig_diff spec: func_800E4B70 -> free_movement.freeMovement (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes where the hold byte 0x1A and
//! the gate byte 0x19B are each sometimes nonzero (any nonzero value, the sign
//! bit included), and a heading of any int (edges around the 0xFFF mask). The
//! scratchpad byte 0x3D0 starts random, so its clearing is seen; the callees
//! are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").freeMovement));

pub const classes = [_][:0]const u8{ "held", "gate-held", "free" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x240, 16);
    for (state[0..0x240]) |*byte| byte.* = ctx.int(u8);
    state[0x1a] = if (ctx.chance(3)) ctx.edge(u8, &.{ 1, 2, 0x80, 0xff }) else 0;
    state[0x19b] = if (ctx.chance(3)) ctx.edge(u8, &.{ 1, 2, 0x80, 0xff }) else 0;
    const angle = ctx.pick(c_int, &.{ 0, 1, -1, 0xfff, 0x1000, 0x1fff, 0x12345, 0x7FFFFFFF, -0x80000000 });
    return .{ state, angle };
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (state[0x1a] != 0) return 0;
    return if (state[0x19b] != 0) 1 else 2;
}
