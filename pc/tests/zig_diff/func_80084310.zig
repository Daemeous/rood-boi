//! zig_diff spec: func_80084310 -> func_80084310.func_80084310 (146C.c).
//! Inputs: arg4 zero or not (both signs); the rest is unused.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084310));

pub const classes = [_][:0]const u8{ "no-flag", "flag" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

pub fn prepare(ctx: *zd.Ctx) Args {
    const flag = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1, 2 });
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), ctx.alloc(v._hitEntity_t), ctx.int(c_int), flag };
}

pub fn classify(args: Args) usize {
    return if (args[4] == 0) 0 else 1;
}
