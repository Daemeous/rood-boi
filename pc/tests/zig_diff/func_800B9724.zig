//! zig_diff spec: func_800B9724 -> func_800B9724.func_800B9724 (4A0A8.c).
//! Inputs: a 16-byte arena block whose control byte (arg0[1]) takes edge
//! values (every bit-6/7 index, bit 3 set and clear, bit 7 set with bit 3
//! clear) and random bytes, plus any c_short for arg1, which the C never
//! reads. The C writes two bytes of D_800F4B28 at the index taken from bits
//! 6-7 of the control byte; the bytes it does not write keep their random
//! fill and are compared too.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9724));

pub const classes = [_][:0]const u8{ "bit3-set", "bit3-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    buf[1] = ctx.pick(u8, &.{ 0x00, 0x08, 0x38, 0x40, 0x80, 0x88, 0xC0, 0xF8, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 8) != 0) 0 else 1;
}
