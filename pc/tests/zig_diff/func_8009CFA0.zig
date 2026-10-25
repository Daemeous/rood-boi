//! zig_diff spec: func_8009CFA0 -> func_8009CFA0.func_8009CFA0 (30DB0.c).
//! The function takes no arguments and has no branches: it returns the global
//! D_800E8F2C (a u8) plus one as an int. The inputs set that global to the
//! edge bytes 0, 1, 127, 128, 254 and 255, so the result covers the zero case,
//! the sign-bit case (128 and up, where a signed read would differ) and the
//! wrap at 255 -> 256 (where a u8 result would differ). Random fill covers the rest.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009CFA0));

pub const classes = [_][:0]const u8{ "zero", "sign-bit", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800E8F2C().* = ctx.pick(u8, &.{ 0, 1, 127, 128, 254, 255 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const x = v.g.D_800E8F2C().*;
    if (x == 0) return 0;
    if (x & 0x80 != 0) return 1;
    return 2;
}
