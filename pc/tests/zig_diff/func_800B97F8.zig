//! zig_diff spec: func_800B97F8 -> func_800B97F8.func_800B97F8 (4A0A8.c).
//! The C body calls func_8007C31C and returns 0; neither argument is read.
//! Inputs: arg0 is null or a fresh arena byte, arg1 is any short with the
//! edge values included. The callee is a void recording stub that changes
//! nothing the C can read back, so the call trace and the return value are
//! what the port must match.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B97F8));

pub const classes = [_][:0]const u8{ "null-arg0", "arena-arg0" };

pub const stubs = struct {
    pub fn func_8007C31C(ctx: *zd.Ctx) void {
        _ = ctx;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = if (ctx.chance(4)) null else ctx.alloc(u8);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
