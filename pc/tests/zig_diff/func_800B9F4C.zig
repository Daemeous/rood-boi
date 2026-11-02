//! zig_diff spec: func_800B9F4C -> func_800B9F4C.func_800B9F4C (4A0A8.c).
//! The function has no branches: it reads arg0[1], passes it to
//! func_8008EFCC and returns 0. The inputs are a 2-byte arena block (so
//! arg0[1] is always readable) whose second byte is a random byte biased to
//! the edges 0, 0x7F, 0x80 and 0xFF (the sign bit of a signed read), and a
//! random short arg1 biased to 0, -1 and the i16 extremes (arg1 is unused by
//! the C). The callee is a recording stub that returns any c_int; the C
//! ignores that result and always returns 0.
const std = @import("std");
const zd = @import("zdiff");
const port = @import("port");

pub const Args = std.meta.ArgsTuple(@TypeOf(port.func_800B9F4C));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-sign", "byte-other" };

pub const stubs = struct {
    /// func_8008EFCC's effect on memory is not modelled: its state changes
    /// are covered by the harness's global comparison. It returns any value,
    /// which the C discards.
    pub fn func_8008EFCC(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(2, 1);
    bytes[0] = ctx.int(u8);
    bytes[1] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ bytes, arg1 };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    if (b & 0x80 != 0) return 1;
    return 2;
}
