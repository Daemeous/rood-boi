//! zig_diff spec: func_800B9F90 -> func_800B9F90.func_800B9F90 (4A0A8.c).
//! Inputs: arg0 is null or an arena block and arg1 any short (the C reads
//! neither). The global D_800F4C2C is set to 2 half the time and random
//! otherwise. The callee func_8008F234 is a recording stub that sometimes
//! rewrites D_800F4C2C (to 2 or a random byte) and returns any c_int; the C
//! reads D_800F4C2C after the call, so a port reading it before the call
//! differs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9F90));

pub const classes = [_][:0]const u8{ "global-is-two", "global-other" };

pub const stubs = struct {
    pub fn func_8008F234(ctx: *zd.Ctx) c_int {
        if (ctx.value(u1) == 1) {
            v.g.D_800F4C2C().* = if (ctx.value(u1) == 1) 2 else ctx.value(u8);
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.int(u8);
    var p: [*c]u8 = null;
    if (ctx.chance(2)) p = ctx.allocBytes(16, 16);
    const arg1: c_short = ctx.int(c_short);
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
