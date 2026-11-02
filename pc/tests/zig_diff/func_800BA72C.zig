//! zig_diff spec: func_800BA72C -> func_800BA72C.func_800BA72C (4A0A8.c).
//! Inputs: arg0 is either null or points at arena bytes, and arg1 is any
//! short, with 0, -1 and the extremes picked often. The C reads neither
//! argument. It calls func_80044DB4 (a void recording stub: nothing the C
//! reads back changes) and returns 0, so the spec checks the call, its
//! count and the return value on every input class.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA72C));

pub const classes = [_][:0]const u8{ "null-arg0", "pointer-arg0" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = if (ctx.chance(4)) null else ctx.allocBytes(16, 16);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
