//! zig_diff spec: func_80084228 -> func_80084228.func_80084228 (146C.c).
//! Inputs: an arena source entity (the C writes its unk1C_26 bits; the
//! random fill keeps the neighbouring bits of its byte varied); the other
//! arguments are filled but unused.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084228));

pub const classes = [_][:0]const u8{ "any" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    return .{ a0, ctx.alloc(v._hitEntity_t), ctx.alloc(v._hitEntity_t), ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return 0;
}
