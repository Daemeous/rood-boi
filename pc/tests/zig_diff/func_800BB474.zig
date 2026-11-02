//! zig_diff spec: func_800BB474 -> func_800BB474.func_800BB474 (4A0A8.c).
//! Inputs: an arena byte block for arg0 (byte 1 is the value stored, drawn
//! from 0, 0x80, 0xFF or random), and a table index 0-3 into D_800F4C58 whose
//! entry is set from edge values: 0 (the store branch), 1 (decrements to 0,
//! the `<= 0` equality), 2 (decrements to 1), -1 and -2 (negative, the
//! `<= 0` branch with a negative value), -32768 (the decrement wraps to
//! 32767), 32767, 100 and -100. The C takes no callees, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB474));

pub const classes = [_][:0]const u8{ "zero", "one", "min", "negative", "positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p = ctx.allocBytes(2, 1);
    p[1] = ctx.pick(u8, &.{ 0, 0x80, 0xFF });

    const tbl = v.g.D_800F4C58();
    for (0..4) |i| {
        tbl[i] = ctx.edge(i16, &.{ 0, 1, 2, -1, -2, -32768, 32767, 100, -100 });
    }

    const idx = ctx.edge(c_short, &.{ 0, 1, 2, 3 });
    if (ctx.chance(4)) tbl[@intCast(idx)] = ctx.int(i16);
    return .{ p, idx };
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4C58();
    const d = tbl[@intCast(args[1])];
    if (d == 0) return 0;
    if (d == 1) return 1;
    if (d == -32768) return 2;
    if (d < 0) return 3;
    return 4;
}
