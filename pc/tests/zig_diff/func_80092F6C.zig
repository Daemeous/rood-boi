//! zig_diff spec: func_80092F6C -> func_80092F6C.func_80092F6C (2842C.c).
//! The C body returns 0 and reads nothing: both pointer arguments are unused.
//! The inputs vary whether each argument is null and, when not, point at a
//! random object in the arena. A port that returns anything but 0, reads an
//! argument, or stores through one differs on some input class.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092F6C));

pub const classes = [_][:0]const u8{ "both-null", "arg0-only", "arg1-only", "both-valid" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0: ?*v.func_80092F74_t = if (ctx.chance(4)) null else ctx.alloc(v.func_80092F74_t);
    const a1: ?*v.func_80092F74_t2 = if (ctx.chance(4)) null else ctx.alloc(v.func_80092F74_t2);
    return .{ a0, a1 };
}

pub fn classify(args: Args) usize {
    const has0 = args[0] != null;
    const has1 = args[1] != null;
    if (!has0 and !has1) return 0;
    if (has0 and !has1) return 1;
    if (!has0 and has1) return 2;
    return 3;
}
