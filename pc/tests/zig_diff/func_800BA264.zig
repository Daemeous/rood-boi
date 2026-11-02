//! zig_diff spec: func_800BA264 -> func_800BA264.func_800BA264 (4A0A8.c).
//! Inputs: a 4-byte buffer in the arena whose byte 1 is zero, a nonzero byte
//! without the sign bit (0x01, 0x7F), or one with it (0x80, 0xFF). The C tests
//! byte 1 against zero, so both sides of that test are covered. Byte 0 and the
//! arg1 short are random: the C never reads them. func_8007BDD8 is a recording
//! stub whose return value the C discards.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA264));

pub const classes = [_][:0]const u8{ "byte1-zero", "byte1-low", "byte1-sign" };

pub const stubs = struct {
    pub fn func_8007BDD8(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    return if ((b & 0x80) != 0) 2 else 1;
}
