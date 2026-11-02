//! zig_diff spec: func_80083F50 -> func_80083F50.func_80083F50 (146C.c).
//! The C is one call, `_setApplicableStatuses(0x16, arg2)`. arg2 is null or
//! an arena entity (the C passes it on untested); the stub writes the
//! entity's status masks. The other arguments are filled but unused.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80083F50));

pub const classes = [_][:0]const u8{ "null-target", "entity-target" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

pub const stubs = struct {
    pub fn _setApplicableStatuses(ctx: *zd.Ctx, id: anytype, ent: anytype) c_int {
        _ = id;
        if (ent) |p| {
            const h: *v._hitEntity_t = @ptrCast(@alignCast(p));
            h.inflictStatuses = ctx.value(i32);
            h.removeStatuses = ctx.value(i32);
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    const a1: ?*v._hitEntity_t = if (ctx.chance(4)) null else ctx.alloc(v._hitEntity_t);
    const a2: ?*v._hitEntity_t = if (ctx.chance(3)) null else ctx.alloc(v._hitEntity_t);
    return .{ a0, a1, a2, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    return if (args[2] == null) 0 else 1;
}
