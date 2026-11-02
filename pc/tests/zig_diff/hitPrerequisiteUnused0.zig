//! zig_diff spec: _hitPrerequisiteUnused0 -> hit_prerequisite_unused0.hitPrerequisiteUnused0 (146C.c).
//! The C has an empty body. Inputs: an action and a 4-byte argument (or
//! null), never read; the state must stay untouched.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").hitPrerequisiteUnused0));
pub const ignore_return = "the C returns no value: its object leaves the caller's %eax (uninitialised in the original), the port 0; the callers (_canPerformAttack through _attackPrerequisiteFunctions) use it, the decompilation names the entry unused";
pub const classes = [_][:0]const u8{ "null", "objects" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) return .{ null, null };
    return .{ ctx.alloc(v.vs_action_t), ctx.allocBytes(4, 4) };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
