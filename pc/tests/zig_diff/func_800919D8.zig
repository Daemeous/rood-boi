//! zig_diff spec: func_800919D8 -> func_800919D8.func_800919D8 (2842C.c).
//! Inputs: an arbitrary int argument, passed through to func_8009195C, which is
//! stubbed to return either NULL or a pointer to an arena int with random bits,
//! so the `|= 0x100` store is visible in the compared state. Covers the NULL
//! test in both directions and the return value on each path.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
const c = @import("c_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800919D8));

pub const classes = [_][:0]const u8{ "null-result", "valid-result" };

var target: *c_int = undefined;
var want_null: bool = false;

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, arg: anytype) ?*c_int {
        _ = ctx;
        _ = arg;
        if (want_null) return null;
        return target;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    target = ctx.alloc(c_int);
    want_null = ctx.chance(3);
    const arg0 = ctx.pick(c_int, &.{ 0, -1, 1, std.math.maxInt(c_int), std.math.minInt(c_int) });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (want_null) 0 else 1;
}
