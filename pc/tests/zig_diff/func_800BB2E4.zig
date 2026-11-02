//! zig_diff spec: func_800BB2E4 -> func_800BB2E4.func_800BB2E4 (4A0A8.c).
//! The C ignores both arguments: it calls func_800CB79C (a void callee that
//! touches nothing the C reads back) and returns 0. The inputs vary arg0
//! between null and a block in the arena, and arg1 over any short, so a port
//! that reads either argument, or returns something else, is caught.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB2E4));

pub const classes = [_][:0]const u8{ "arg0-null", "arg0-valid" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = if (ctx.chance(4)) null else ctx.allocBytes(4, 1);
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
