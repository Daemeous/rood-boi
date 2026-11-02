//! zig_diff spec: vs_battle_uiGradientStop -> ui_gradient_stop.uiGradientStop (5BF94.c).
//! Inputs: the colour index 0..8 (the fixed blends) or 16..31 (the two-entry
//! tables; 9..15 index them at 0xFFFFFFFF in the C, out of the image), the
//! gradient and brightness any; _lerpColor is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").uiGradientStop));

pub const classes = [_][:0]const u8{ "blend", "table" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const index: c_uint = if (ctx.random.boolean()) ctx.range(c_uint, 0, 8) else ctx.range(c_uint, 16, 31);
    return .{ ctx.pick(c_uint, &.{ 0, 1, 0x80, 0xFFFF_FFFF }), index, ctx.pick(c_int, &.{ 0, 0x80, -1 }) };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 9) 0 else 1;
}
