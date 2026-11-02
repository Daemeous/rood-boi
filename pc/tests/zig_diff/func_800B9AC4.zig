//! zig_diff spec: func_800B9AC4 -> func_800B9AC4.func_800B9AC4 (4A0A8.c).
//! Inputs: a 4-byte arena buffer whose byte 1 (the one the C reads) is
//! zero, a low byte with no sign bit, or a sign-bit byte (0x80..0xFF), and
//! an arbitrary short for the unused arg1. The C has no branches and no
//! ordered comparisons; the callee is a void call that changes nothing the
//! C reads back.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9AC4));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-low", "byte-sign" };

pub const stubs = struct {
    pub fn func_8007C460(ctx: *zd.Ctx, a: anytype) void {
        // Records only; it writes no state the C or the port reads back.
        _ = ctx;
        _ = a;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x0F, 0x7F, 0x80, 0xFF });
    const arg1 = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000, 0x100 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    if (b >= 0x80) return 2;
    return 1;
}
