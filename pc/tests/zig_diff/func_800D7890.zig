//! zig_diff spec: func_800D7890 -> func_800D7890.func_800D7890 (6F014.c).
//! Inputs: arg0 as zero, positive, negative with the sign bit set, and the
//! edge values around the sign bit. The C only tests arg0 against zero and
//! stores one of two global addresses into D_800F569C, so the state compared
//! after the call is that pointer global (random before the call). Some runs
//! start it as null; the store overwrites it either way.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7890));

pub const classes = [_][:0]const u8{ "zero", "nonzero-sign-clear", "nonzero-sign-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x40000000, 2 });
    if (ctx.chance(4)) v.g.D_800F569C().* = null;
    return .{arg};
}

pub fn classify(args: Args) usize {
    const a = args[0];
    if (a == 0) return 0;
    return if (a < 0) 2 else 1;
}
