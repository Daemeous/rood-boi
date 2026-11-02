//! zig_diff spec: func_800BA890 -> func_800BA890.func_800BA890 (4A0A8.c).
//! Inputs: a 4-byte arena buffer whose byte 1 (the one the C reads) is an
//! edge or random value, so both sign-bit states occur, and an arbitrary
//! c_short that the C never reads. The callee func_80044EC8 is a recording
//! stub that sometimes overwrites D_800F4C69 (the port must not re-read it
//! after the call) and returns an arbitrary int that the C discards.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA890));

pub const classes = [_][:0]const u8{ "sign-clear", "sign-set" };

pub const stubs = struct {
    pub fn func_80044EC8(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        if (ctx.value(u1) == 1) v.g.D_800F4C69().* = ctx.value(u8);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    return if ((p[1] & 0x80) != 0) 1 else 0;
}
