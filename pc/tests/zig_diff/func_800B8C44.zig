//! zig_diff spec: func_800B8C44 -> func_800B8C44.func_800B8C44 (4A0A8.c).
//! Inputs: arg0 null or a live byte block (the C never reads it), arg1 any
//! short with the edges 0, -1 and the extremes, and the global D_800F4C64 at
//! zero, -1, the 16-bit boundaries, the 32-bit extremes, or any i32. The C has
//! no branch: it returns the global, so the spec checks that read, the
//! narrowing of the result, and that arg1 is not used.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8C44));

pub const classes = [_][:0]const u8{ "negative", "zero", "positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var p: [*c]u8 = null;
    if (!ctx.chance(4)) p = ctx.allocBytes(4, 1);
    v.g.D_800F4C64().* = ctx.pick(i32, &.{ 0, 1, -1, 0x7FFF, 0x8000, -0x8000, 0x7FFF_FFFF, -0x8000_0000 });
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    const x = v.g.D_800F4C64().*;
    if (x < 0) return 0;
    if (x == 0) return 1;
    return 2;
}
