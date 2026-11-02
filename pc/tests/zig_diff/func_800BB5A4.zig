//! zig_diff spec: func_800BB5A4 -> func_800BB5A4.func_800BB5A4 (4A0A8.c).
//! Inputs: arg0 is null on about a quarter of the runs and otherwise points at
//! four arena bytes; arg1 takes the edges 0, 1, -1, 32767 and -32768 as well as
//! any short. The C body is a constant return, so the spec checks the return
//! value against null-ness of arg0 and the sign of arg1, the two inputs a port
//! could mistakenly use.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB5A4));

pub const classes = [_][:0]const u8{ "arg0-null", "arg0-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var p: [*c]u8 = null;
    if (!ctx.chance(4)) p = ctx.allocBytes(4, 1);
    const a1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ p, a1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
