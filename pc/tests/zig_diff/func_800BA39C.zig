//! zig_diff spec: func_800BA39C -> func_800BA39C.func_800BA39C (4A0A8.c).
//! Inputs: an arena block of 8 random bytes as arg0, with arg0[1] (the index
//! into D_800E9B34's six entries) set to 0..5, so the C never reads past the
//! table. arg0[2..4] are random bytes, which are the three arguments passed
//! through. arg1 is unused by the C, so it is any short. The C has no
//! branches. func_80045BFC is a void callee that the C never reads back
//! after the call, so its default recording stub is the whole model.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA39C));

pub const classes = [_][:0]const u8{ "index-0", "index-1-4", "index-5" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(8, 1);
    bytes[1] = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5 });
    return .{ bytes, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return switch (args[0][1]) {
        0 => 0,
        5 => 2,
        else => 1,
    };
}
