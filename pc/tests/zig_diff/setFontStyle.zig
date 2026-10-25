//! zig_diff spec: vs_battle_setFontStyle -> set_font_style.setFontStyle (5BF94.c).
//! The C has no branches and no calls: it zeroes _fontTable, sets
//! _fontBrightness to 384 and stores arg0 into D_800F4CB8 (a u8). The inputs
//! are edge values of arg0 (zero, signs, the byte boundary, the int extremes),
//! so the narrowing to a byte and the sign of the argument are both exercised.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setFontStyle));

pub const classes = [_][:0]const u8{ "negative", "nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{ 0, 1, -1, 127, 128, 255, 256, 0x7fff_ffff, -0x8000_0000 })};
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 0 else 1;
}
