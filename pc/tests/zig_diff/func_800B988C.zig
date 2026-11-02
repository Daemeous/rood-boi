//! zig_diff spec: func_800B988C -> func_800B988C.func_800B988C (4A0A8.c).
//! Inputs: a 16-byte arena buffer whose bytes are random, so the four bytes the
//! C passes (indices 1-4) are any u8, including values with the sign bit set
//! (the signed-slip case), and arg1 is any c_short (the C never reads it).
//! The callee is a recording stub that returns a value the C ignores; the C
//! has no branches and no comparisons, so the classes only split the sign-bit case.
const std = @import("std");
const zd = @import("zdiff");
const port = @import("port");
pub const Args = std.meta.ArgsTuple(@TypeOf(port.func_800B988C));

pub const classes = [_][:0]const u8{ "sign-bit-in-args", "no-sign-bit" };

pub const stubs = struct {
    pub fn func_8008B808(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    const p: [*c]u8 = buf;
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    return if (p[1] >= 0x80 or p[2] >= 0x80 or p[3] >= 0x80 or p[4] >= 0x80) 0 else 1;
}
