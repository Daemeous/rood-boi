//! zig_diff spec: func_800BA6B8 -> func_800BA6B8.func_800BA6B8 (4A0A8.c).
//! Inputs: arg0 is unused by the C, so it is null; arg1 is any short (0, -1,
//! the extremes, random), and the C returns 0 whatever it is. The global
//! D_800F4C69 is set to a u8 edge (0x80 and 0xFF test sign extension into the
//! int argument of func_80044DF4) and the callee's return value is random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA6B8));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-non-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C69().* = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ null, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 0) 0 else 1;
}
