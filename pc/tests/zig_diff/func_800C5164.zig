//! zig_diff spec: func_800C5164 -> func_800C5164.func_800C5164 (5BF94.c).
//! Inputs: three random c_ints, with arg0 drawn from edge values that cover
//! the mask 0xF0 both ways: nonzero with the sign bit set and clear (-16,
//! 0x10, 0xF0), zero with the sign bit set (minInt, -256), and single bits
//! just outside the mask (0x08, 0x100). Random arg0 covers the rest. The
//! callee's return value is ignored by the C, so its default recording stub
//! (which returns ctx.value) is enough.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C5164));

pub const classes = [_][:0]const u8{ "masked", "unmasked" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.pick(c_int, &.{
        0, 0x08, 0x0F, 0x10, 0x20, 0x40, 0x70, 0x80, 0xF0, 0x100,
        -1, -16, -256, std.math.minInt(c_int), std.math.maxInt(c_int),
    });
    const a1 = ctx.int(c_int);
    const a2 = ctx.int(c_int);
    return .{ a0, a1, a2 };
}

pub fn classify(args: Args) usize {
    return if ((args[0] & 0xF0) != 0) 0 else 1;
}
