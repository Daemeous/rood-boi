//! zig_diff spec: func_800BA61C -> func_800BA61C.func_800BA61C (4A0A8.c).
//! Inputs: a 3-byte buffer in the arena, so arg0[1] and arg0[2] are always
//! readable. Both bytes are biased toward 0, 127, 128 and 255 so both sides of
//! the sign bit occur and the signed/unsigned reads differ. arg1 is any
//! c_short, which the C never reads. The callee func_8004552C is a recording
//! stub whose int result the C discards. It writes no state the C reads back,
//! so the default stub is the model.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA61C));

pub const classes = [_][:0]const u8{ "arg0-1-low", "arg0-1-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(3, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0, 127, 128, 255 });
    buf[2] = ctx.pick(u8, &.{ 0, 127, 128, 255 });
    // The global passed as the call's first argument: sometimes an edge value.
    if (ctx.chance(4)) v.g.D_800F4C69().* = ctx.pick(u8, &.{ 0, 255 });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 0x80) != 0) 1 else 0;
}
