//! zig_diff spec: func_800B6744 -> func_800B6744.func_800B6744 (4A0A8.c).
//! Inputs: arg0 is a 4-byte arena buffer whose byte 1 is an edge value
//! (0, 1, 0x7F, 0x80, 0x81, 0xFF or any byte), so both parities and both
//! sides of the sign bit occur; byte 0 and arg1 are random. D_800F4C68 is
//! zero a quarter of the time and random otherwise, so both sides of its
//! nonzero test occur. The classes split on that test and on the parity of
//! arg0[1]. The function has no callees and no ordered comparisons.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6744));

pub const classes = [_][:0]const u8{
    "zero-68-even",
    "zero-68-odd",
    "nonzero-68-even",
    "nonzero-68-odd",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(4, 1));
    buf[1] = ctx.pick(u8, &.{ 0x00, 0x01, 0x7F, 0x80, 0x81, 0xFF });
    if (ctx.chance(4)) v.g.D_800F4C68().* = 0;
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const zero = v.g.D_800F4C68().* == 0;
    const odd = (args[0][1] & 1) == 1;
    const base: usize = if (zero) 0 else 2;
    return base + @intFromBool(odd);
}
