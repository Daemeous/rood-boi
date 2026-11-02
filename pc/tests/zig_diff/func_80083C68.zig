//! zig_diff spec: func_80083C68 -> func_80083C68.func_80083C68 (146C.c).
//! Inputs: an arena target entity (the C writes it when the call reports
//! statuses) and the stub's result, chosen by prepare: 0, small, negative
//! and large values, so the `!= 0` test is taken both ways. The stub also
//! writes the entity's status masks. The other arguments are unused.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80083C68));

pub const classes = [_][:0]const u8{ "none", "some" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

var result: c_int = 0;

pub const stubs = struct {
    pub fn _setApplicableStatuses(ctx: *zd.Ctx, id: anytype, ent: anytype) c_int {
        _ = id;
        if (ent) |p| {
            const h: *v._hitEntity_t = @ptrCast(@alignCast(p));
            h.inflictStatuses = ctx.value(i32);
            h.removeStatuses = ctx.value(i32);
        }
        return result;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    result = ctx.edge(c_int, &.{ 0, 0, 1, 2, 3, -1, 0x100, 0x7FFF_FFFF, -0x8000_0000 });
    const a0: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    const a1: ?*v._hitEntity_t = if (ctx.chance(4)) null else ctx.alloc(v._hitEntity_t);
    return .{ a0, a1, ctx.alloc(v._hitEntity_t), ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return if (result == 0) 0 else 1;
}
