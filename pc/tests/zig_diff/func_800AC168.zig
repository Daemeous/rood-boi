//! zig_diff spec: func_800AC168 -> func_800AC168.func_800AC168 (40564.c).
//! Inputs: 0-5 pixels (count -1..5, so `count > 0` and the loop exit both see
//! equal sides), every switch mode 0-6 plus the default (-1, 7, 8), amounts
//! from 0 and the 1/16/31/32 edges to +-2^28 and small random values, so the
//! clamps go both ways. Mode 3 sometimes gets a first pixel whose r, g and b
//! reach exactly 32 (the `>= 32` equal case). Mode 4 sometimes copies colors
//! into reference with amount 0 (the `< baseR` and `!=` equal cases). Random
//! reference words cover bit 15 set and clear. The function has no callees.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AC168));

pub const classes = [_][:0]const u8{ "no-pixels", "mode0", "mode1", "mode2", "mode3", "mode4", "mode5", "mode6", "other-mode" };

fn pack(r: c_int, g: c_int, b: c_int, hi: c_int) c_ushort {
    return @intCast(r | (g << 5) | (b << 10) | hi);
}

fn randomAmount(ctx: *zd.Ctx) c_int {
    if (ctx.chance(2)) return ctx.edge(c_int, &.{ 0, 1, -1, 2, -2, 15, 16, 31, 32, -31, -32, 1 << 28, -(1 << 28) });
    return ctx.range(c_int, -40, 40);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const count = ctx.edge(c_int, &.{ -1, 0, 1, 2, 3, 4, 5 });
    const mode = ctx.edge(c_int, &.{ -1, 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    const n: usize = if (count > 0) @intCast(count) else 1;
    const colors: [*]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(2 * n, 2)));
    const reference: [*]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(2 * n, 2)));
    var amount = randomAmount(ctx);
    if (mode == 3 and ctx.chance(3)) {
        const a = ctx.range(c_int, 1, 16);
        amount = a;
        colors[0] = pack(32 - a, 32 - 2 * a, 32 - 2 * a, ctx.range(c_int, 0, 1) << 15);
    }
    if (mode == 4 and ctx.chance(3)) {
        amount = 0;
        for (0..n) |k| reference[k] = colors[k];
    }
    return .{ colors, count, amount, mode, reference };
}

pub fn classify(args: Args) usize {
    if (args[1] <= 0) return 0;
    return switch (args[3]) {
        0 => 1,
        1 => 2,
        2 => 3,
        3 => 4,
        4 => 5,
        5 => 6,
        6 => 7,
        else => 8,
    };
}
