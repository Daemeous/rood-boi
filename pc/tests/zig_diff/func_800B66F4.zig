//! zig_diff spec: func_800B66F4 -> func_800B66F4.func_800B66F4 (4A0A8.c).
//! Inputs: D_800F4BBC null (20%), or pointing at an arena i16 whose first
//! element is 4 (40%) or a value that is not 4 (40%: 0, 3, 5, -1, 32767,
//! -32768). The C reads only that first element. arg0 is null or a buffer
//! the C never touches; arg1 is a short that is often 0 (half the draws), so
//! a test of arg1 against zero shows up in the result. Other values are
//! ignored by the C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B66F4));

pub const classes = [_][:0]const u8{ "global-null", "value-4", "value-not-4" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const g = v.g.D_800F4BBC();
    const mode = ctx.range(u8, 0, 9);
    if (mode < 2) {
        g.* = null;
    } else {
        const p: [*]i16 = @ptrCast(@alignCast(ctx.allocBytes(4, 2)));
        p[0] = if (mode < 6) 4 else ctx.edge(i16, &.{ 0, 3, 5, -1, 32767, -32768 });
        g.* = p;
    }

    var a0: [*c]u8 = null;
    if (!ctx.chance(2)) a0 = ctx.allocBytes(16, 16);
    // Zero half the time: the C ignores arg1, a port that reads it must differ on 0.
    const a1 = ctx.edge(i16, &.{ 0, 0, 1, -1, 32767, -32768 });
    return .{ a0, a1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    const p = v.g.D_800F4BBC().* orelse return 0;
    return if (p[0] == 4) 1 else 2;
}
