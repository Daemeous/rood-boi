//! zig_diff spec: func_800DC30C -> pc.step_clamped.stepClamped (D2: pc/src/battle_movement.c).
//! Inputs: a short value and a short limit in the arena (edges or any short,
//! the limit sometimes negative), a step (edges, or chosen so the next value
//! is exactly 0 or exactly the limit: the boundaries of both clamps) and a
//! mode (2 steps up, anything else steps down). The next value is computed in
//! int, so edge steps wrap there.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").stepClamped));

pub const classes = [_][:0]const u8{ "floor-zero", "limit", "in-range", "negative-limit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block: [*]i16 = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    const value = &block[0];
    const limit = &block[4];
    value.* = ctx.pick(i16, &.{ 0, 1, -1, 100, 0x7FFF, -0x8000 });
    limit.* = ctx.pick(i16, &.{ 0, 1, -1, 100, 0x7FFF, -0x8000 });
    const mode = ctx.edge(c_int, &.{ 2, 2, 0, 1, 3, -1 });
    var step = ctx.pick(c_int, &.{ 0, 1, -1, 50, 0x7FFFFFFF, -0x80000000 });
    if (ctx.chance(4)) step = if (mode == 2) -@as(c_int, value.*) else value.*; // next == 0
    if (ctx.chance(4)) { // next == limit
        const next = if (mode == 2) @as(c_int, value.*) +% step else @as(c_int, value.*) -% step;
        if (next >= -0x8000 and next <= 0x7FFF) limit.* = @intCast(next);
    }
    return .{ value, limit, step, mode };
}

pub fn classify(args: Args) usize {
    const value: c_int = args[0].*;
    const next = if (args[3] == 2) value +% args[2] else value -% args[2];
    if (next < 0) return 0;
    if (args[1].* < next) return if (args[1].* < 0) 3 else 1;
    return 2;
}
