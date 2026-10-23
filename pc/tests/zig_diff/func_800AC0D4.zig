//! zig_diff spec: func_800AC0D4 -> func_800AC0D4.func_800AC0D4 (40564.c).
//! Inputs: two 3-byte buffers in the arena and an arg2 from the edges (0, +-1,
//! +-255, -256, -1000, INT_MIN and the largest value that cannot overflow
//! `*arg0 += arg2`) or any int. Each element pair is equal, related by
//! `arg1 = arg0 + arg2` (so the store after the add lands on equal bytes), or
//! random, so the byte compares see equal sides often and both branches of
//! the sign test, the inner tests and the clamps. Classes split by the sign of
//! arg2 and whether any element pair triggers an update.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AC0D4));

pub const classes = [_][:0]const u8{ "neg-changed", "neg-unchanged", "nonneg-changed", "nonneg-unchanged" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.allocBytes(3, 1);
    const a1 = ctx.allocBytes(3, 1);
    const arg2 = ctx.pick(c_int, &.{
        0,      1,       -1,         2,          -2,
        254,    -254,    255,        -255,       -256,
        -1000,  0x7FFF_FF00, -0x8000_0000,
    });
    const low: u8 = @truncate(@as(u32, @bitCast(arg2)));
    for (0..3) |i| {
        a0[i] = ctx.int(u8);
        a1[i] = switch (ctx.range(u8, 0, 3)) {
            0 => a0[i],
            1 => a0[i] +% low,
            else => ctx.int(u8),
        };
    }
    return .{ a0, a1, arg2 };
}

pub fn classify(args: Args) usize {
    const a0 = args[0];
    const a1 = args[1];
    const neg = args[2] < 0;
    var any = false;
    for (0..3) |i| {
        const hit = if (neg) a1[i] < a0[i] else a0[i] < a1[i];
        if (hit) any = true;
    }
    const base: usize = if (neg) 0 else 2;
    return base + (if (any) @as(usize, 0) else 1);
}
