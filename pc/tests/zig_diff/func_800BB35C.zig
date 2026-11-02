//! zig_diff spec: func_800BB35C -> func_800BB35C.func_800BB35C (4A0A8.c).
//! Inputs: a 16-byte arena block as arg0, whose byte 1 takes every value of its
//! low two bits, with random high bits (sign bit included half the time), and
//! any arg1. The C has no branch and no ordered comparison. The `new_var != 0`
//! test sees 0 and 2 only, since new_var is masked to bit 1, so its sign-bit
//! flag is unreachable (waived). The callee sometimes overwrites byte 1, so a
//! port that rereads it after the call differs.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB35C));

pub const classes = [_][:0]const u8{ "low-00", "low-01", "low-10", "low-11" };

/// The block the stub writes into (set by prepare).
var cur: [*c]u8 = undefined;

pub const stubs = struct {
    pub fn func_800BE36C(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) cur[1] = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(16, 16);
    buf[0] = ctx.int(u8);
    buf[1] = (ctx.int(u8) & 0xFC) | ctx.range(u8, 0, 3);
    cur = buf;
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][1] & 3);
}
