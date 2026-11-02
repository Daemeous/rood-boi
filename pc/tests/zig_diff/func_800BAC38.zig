//! zig_diff spec: func_800BAC38 -> func_800BAC38.func_800BAC38 (4A0A8.c).
//! Inputs: a 16-byte arena buffer whose byte 1 is an edge value (0, 1, 127,
//! 128, 255) or random, and any c_short for arg1, which the C never reads.
//! The callee vs_battle_setDoorEntered2 is a recording stub: it returns void
//! and the C reads nothing back from it, so it needs no state model. The
//! only observable behaviour is the argument it receives (arg0[1], zero- or
//! sign-extended), the call itself, and the constant return value.
//! Both classes are split on whether byte 1 has its high bit set, which is
//! where a signed-char read differs from the C's unsigned read.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAC38));

pub const classes = [_][:0]const u8{ "byte1-low", "byte1-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(16, 1));
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] < 128) 0 else 1;
}
