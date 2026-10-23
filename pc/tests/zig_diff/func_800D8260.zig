//! zig_diff spec: func_800D8260 -> func_800D8260.func_800D8260 (6FA60.c).
//! The C stores arg1 as a u16 at unk138 and arg2 as a u8 at unk12A, with no
//! test and no call. Inputs: a non-null func_800DEEA4_t2 in the arena (the C
//! dereferences it unconditionally) with random bytes in every field, and
//! arguments drawn from truncation edges (0, -1, 0xFF, 0x100, 0xFFFF, 0x10000,
//! INT_MIN, INT_MAX) and random values. Classes split on whether arg1 fits in
//! 16 bits, so the truncation path and the wide path both run.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6FA60");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D8260));

pub const classes = [_][:0]const u8{ "arg1-fits-u16", "arg1-wide" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800DEEA4_t2);
    const a1 = ctx.pick(c_int, &.{ 0, 1, -1, 0xFF, 0x100, 0xFFFF, 0x10000, -32768, 0x7FFFFFFF, -2147483648 });
    const a2 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7F, 0x80, 0xFF, 0x100, 0x7FFFFFFF, -2147483648 });
    return .{ obj, a1, a2 };
}

pub fn classify(args: Args) usize {
    return if (args[1] >= 0 and args[1] <= 0xFFFF) 0 else 1;
}
