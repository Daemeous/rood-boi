//! zig_diff spec: func_80091314 -> func_80091314.func_80091314 (2842C.c).
//! The C body is one store: the low byte of arg0 goes to the global D_800F1DCB.
//! Inputs: arg0 drawn from edge values (0, 1, 127, 128, 255, 256, -1, -128, and the
//! i32 extremes) half the time and any i32 otherwise, so the stored byte covers
//! every bit pattern and the sign bit both set and clear. The global starts as random
//! bytes, so a dropped or misplaced store shows in the compared state.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091314));

pub const classes = [_][:0]const u8{ "negative", "non-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.pick(c_int, &.{ 0, 1, 127, 128, 255, 256, -1, -128, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{arg};
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 0 else 1;
}
