//! zig_diff spec: func_80091998 -> func_80091998.func_80091998 (2842C.c).
//! Inputs: an arbitrary int arg0 and the callee func_8009195C, stubbed to
//! return either null (the 0 path) or a c_int in the arena. The stub sometimes
//! rewrites that int, and the int's bit 8 is set or clear by prepare, so the
//! clear is observed both ways and the return value is checked on both paths.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091998));

pub const classes = [_][:0]const u8{ "null", "bit-set", "bit-clear" };

var g_null: bool = false;
var g_target: *c_int = undefined;

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, arg0: anytype) ?*c_int {
        _ = arg0;
        if (g_null) return null;
        if (ctx.value(u1) == 1) g_target.* = ctx.value(c_int);
        return g_target;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    g_target = ctx.alloc(c_int);
    g_target.* = ctx.pick(c_int, &.{ 0, -1, 0x100, 0x0FF, 0x7FFFFF00, -2147483392 });
    g_null = ctx.chance(4);
    return .{ctx.int(c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (g_null) return 0;
    return if ((g_target.* & 0x100) != 0) 1 else 2;
}
