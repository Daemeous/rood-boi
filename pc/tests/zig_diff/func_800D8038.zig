//! zig_diff spec: func_800D8038 -> func_800D8038.func_800D8038 (6F014.c).
//! Inputs: any c_int argument, biased toward the edges a narrowing, sign or
//! constant slip would show (0, +-1, INT_MAX, INT_MIN, 0x7FFF, 0xFFFF, 0x8000,
//! 0x10000, 255, 256) plus random values. The unit has no branches and no
//! calls: its only effect is the store of arg0 to D_800F5680, which the
//! harness compares byte for byte with the global's random starting bytes.
//! Classes split the arguments by sign and by whether they fit in 16 bits.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D8038));

pub const classes = [_][:0]const u8{ "negative", "zero", "fits-16-bits", "above-16-bits" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000, 0x7FFF, 0xFFFF, 0x8000, 0x10000, 255, 256 })};
}

pub fn classify(args: Args) usize {
    const x = args[0];
    if (x < 0) return 0;
    if (x == 0) return 1;
    if (x <= 0xFFFF) return 2;
    return 3;
}
