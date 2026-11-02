//! zig_diff spec: func_80084FF0 -> func_80084FF0.func_80084FF0 (146C.c).
//! The C's body is empty. Inputs: arena entities and random integers it
//! must leave alone.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084FF0));

pub const classes = [_][:0]const u8{ "any" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), ctx.alloc(v._hitEntity_t), ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return 0;
}
