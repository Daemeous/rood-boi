//! zig_diff spec: func_800B71C4 -> func_800B71C4.func_800B71C4 (4A0A8.c).
//! Inputs: any arg0 (null or an arena block, the C never reads it) and any arg1.
//! The global D_800F4C2C is random, set to 2 about a third of the time in
//! prepare, and the callee stub rewrites it with an arbitrary byte about a
//! third of the time, so the callee's write is seen by the C's read after the
//! call. The result is 4 when the global is 2 and 0 otherwise, so both classes
//! occur and the equal side of `==` is hit often.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B71C4));

pub const classes = [_][:0]const u8{ "not-two", "two" };

pub const stubs = struct {
    /// The real callee may change D_800F4C2C, which the C reads back after the call.
    pub fn func_800C86AC(ctx: *zd.Ctx) void {
        if (ctx.value(u8) < 96) v.g.D_800F4C2C().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(3)) v.g.D_800F4C2C().* = 2;
    const arg0: [*c]u8 = if (ctx.chance(4)) null else @ptrCast(ctx.allocBytes(4, 1));
    const arg1: c_short = ctx.int(c_short);
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 1 else 0;
}
