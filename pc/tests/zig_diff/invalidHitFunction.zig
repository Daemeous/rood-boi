//! zig_diff spec: _invalidHitFunction -> invalid_hit_function.invalidHitFunction (146C.c).
//! The C has an empty body. Inputs: an action, two hit entities (or null)
//! and two ints, never read; the state must stay untouched.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").invalidHitFunction));
pub const ignore_return = "the C returns no value: its object leaves the caller's %eax (uninitialised in the original), the port 0; the caller (_doesAttackHit through _hitFunctions) uses it, the decompilation names entry 0 invalid";

pub const classes = [_][:0]const u8{ "null", "objects" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) return .{ null, null, null, ctx.int(c_int), ctx.int(c_int) };
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), ctx.alloc(v._hitEntity_t), ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
