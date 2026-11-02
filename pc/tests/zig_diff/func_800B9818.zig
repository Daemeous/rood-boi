//! zig_diff spec: func_800B9818 -> func_800B9818.func_800B9818 (4A0A8.c).
//! Inputs: a buffer in the arena whose byte 1 is an edge value (0x00, 0x7F,
//! 0x80, 0xFF) so the unsigned read of arg0[1] is checked against a signed
//! one, and arg1 any short (the C never reads it). The callee's int result
//! is ignored by the C, so the stub returns any value and the port must not
//! depend on it.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9818));

pub const classes = [_][:0]const u8{ "byte-high", "byte-low" };

pub const stubs = struct {
    /// The callee's only effect the C can see is its argument; its result is
    /// discarded by the C, so any value is returned.
    pub fn func_80091998(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(8, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 0x80) 0 else 1;
}
