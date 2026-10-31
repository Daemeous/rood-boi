//! zig_diff spec: func_80095B7C -> func_80095B7C.func_80095B7C (2842C.c).
//! Inputs: arg0 is 0 (the memset of D_800F2260's 16 bytes) or an index 1-15
//! into it, so the C never writes outside the table; arg1 is any int (random
//! bits, the sign bit, values above 0xFF that memset truncates, -1, and the
//! 0x40/0x80 masks). Together these reach each branch: bit 7 set, bit 6 set,
//! neither, and the zero-index memset. The table starts as random bytes, so
//! the read-modify-write of each byte is checked against every old value.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80095B7C));

pub const classes = [_][:0]const u8{ "zero-index-memset", "bit7-set", "bit6-set", "merge-low" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = if (ctx.chance(6)) 0 else ctx.range(c_int, 1, 15);
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 0x80, 0x40, 0xC0, 0x7F, 0x100, -1, 0x180 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    if (args[1] & 0x80 != 0) return 1;
    if (args[1] & 0x40 != 0) return 2;
    return 3;
}
