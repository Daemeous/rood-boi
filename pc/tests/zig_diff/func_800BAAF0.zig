//! zig_diff spec: func_800BAAF0 -> func_800BAAF0.func_800BAAF0 (4A0A8.c).
//! The C reads one byte, arg0[1], and passes it to func_80044DD4, then returns 0.
//! Inputs: an arena buffer whose byte 1 is 0, 0x7F, 0x80 or 0xFF (or any byte),
//! and an arg1 that the C never reads (any short, including the extremes).
//! func_80044DD4 is void and the C does not read anything it could change
//! afterwards (no globals, no out-parameters, no return value), so its stub
//! is the default recording stub. The C has no branches and no comparisons.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAAF0));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-high", "byte-low" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(16, 16);
    buf[1] = ctx.pick(u8, &.{ 0, 0x7f, 0x80, 0xff });
    const arg1 = ctx.pick(i16, &.{ 0, 1, -1, 0x7fff, -0x8000 });
    const p: [*c]u8 = buf;
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    if (b & 0x80 != 0) return 1;
    return 2;
}
