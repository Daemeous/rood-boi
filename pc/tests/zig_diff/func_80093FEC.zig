//! zig_diff spec: func_80093FEC -> func_80093FEC.func_80093FEC (2842C.c).
//! Inputs: arg2 is the bit set the loop walks. Bits 0-28 are tested (one call
//! per set bit); bits 29-31 never are. The classes cover no tested bits, all
//! tested bits, the sign bit set with other tested bits, and other words.
//! arg0 and arg3 are passed to the callee, arg1 is never read, and the callee
//! is the default recording stub, whose result the C discards.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093FEC));

pub const classes = [_][:0]const u8{ "no-tested-bits", "all-tested-bits", "sign-bit", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.int(c_int);
    const arg1: c_int = ctx.int(c_int);
    const arg3: c_int = ctx.pick(c_int, &.{ 0, -1, std.math.minInt(c_int), 1 });
    const bits: c_int = switch (ctx.range(u8, 0, 3)) {
        0 => 0,
        1 => 0x1FFF_FFFF,
        2 => @bitCast(@as(u32, 0x8000_0000) | @as(u32, ctx.int(u29))),
        else => ctx.pick(c_int, &.{ 0x1000_0000, 1, 0x0800_0000, 0x7FFF_FFFF, -2, 0x2000_0000 }),
    };
    return .{ arg0, arg1, bits, arg3 };
}

pub fn classify(args: Args) usize {
    const bits: u32 = @bitCast(args[2]);
    const tested = bits & 0x1FFF_FFFF;
    if (tested == 0) return 0;
    if (tested == 0x1FFF_FFFF) return 1;
    if ((bits & 0x8000_0000) != 0) return 2;
    return 3;
}
