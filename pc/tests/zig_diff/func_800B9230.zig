//! zig_diff spec: func_800B9230 -> func_800B9230.func_800B9230 (4A0A8.c).
//! Inputs: arg0 is a 16-byte arena block whose byte 1 is the only byte the C
//! reads; it takes the edges 0, 1, 127, 128 and 255 (half the time) or any
//! value, so both signs of the u8 are seen. arg1 is unused by the C and takes
//! edge values. The callee func_8007D15C is a recording stub that returns
//! a value the C ignores; it cannot change anything the C reads back.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9230));

pub const classes = [_][:0]const u8{ "high-bit", "low-bit" };

pub const stubs = struct {
    pub fn func_8007D15C(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(16, 16);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    const arg1 = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 0x80) != 0) 0 else 1;
}
