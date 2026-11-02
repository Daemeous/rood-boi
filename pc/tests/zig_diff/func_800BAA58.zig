//! zig_diff spec: func_800BAA58 -> func_800BAA58.func_800BAA58 (4A0A8.c).
//! Inputs: D_800F4BB9 zero, with the sign bit set, and nonzero without it, so
//! every path of the C is reached. arg0 is null or an arena block (the C never
//! reads it) and arg1 is any short. func_8007BEE4 returns 0, 5 or another value
//! per call, so the first-call-zero, second-call-five and other paths are all
//! taken. func_800CF1A8 rewrites D_800F4BB9 (the C then sets it to 1), which
//! catches a store moved across the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAA58));

pub const classes = [_][:0]const u8{ "flag-zero", "flag-high-bit", "flag-low-nonzero" };

pub const stubs = struct {
    pub fn func_800CF1A8(ctx: *zd.Ctx) void {
        v.g.D_800F4BB9().* = ctx.value(u8);
    }

    pub fn func_8007BEE4(ctx: *zd.Ctx) c_int {
        const r = ctx.value(u8);
        if (r < 64) return 0;
        if (r < 128) return 5;
        return @bitCast(ctx.value(u32));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const flag = v.g.D_800F4BB9();
    flag.* = ctx.pick(u8, &.{ 0, 1, 0x7f, 0x80, 0xff });
    var arg0: [*c]u8 = null;
    if (!ctx.chance(4)) arg0 = ctx.allocBytes(16, 16);
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    const f = v.g.D_800F4BB9().*;
    if (f == 0) return 0;
    if ((f & 0x80) != 0) return 1;
    return 2;
}
