//! zig_diff spec: func_800BA2B4 -> func_800BA2B4.func_800BA2B4 (4A0A8.c).
//! Inputs: a 4-byte buffer at arg0 whose bytes 1 and 2 are edge values (0, 1,
//! 0x7F, 0x80, 0xFF, or any byte), so the signed-char reads see both signs and
//! the shifted results cross the sign bit; byte 0 and arg1 are random (the C
//! never reads them). The C has no branches or ordered comparisons. The callee
//! func_8008EAE4 takes no pointers, so it can only change globals the C never
//! reads back; its recording stub is left as the default void stub.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA2B4));

pub const classes = [_][:0]const u8{ "both-negative", "first-negative", "second-negative", "both-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(4, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[2] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[3] = ctx.int(u8);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const neg1 = args[0][1] >= 0x80;
    const neg2 = args[0][2] >= 0x80;
    if (neg1 and neg2) return 0;
    if (neg1) return 1;
    if (neg2) return 2;
    return 3;
}
