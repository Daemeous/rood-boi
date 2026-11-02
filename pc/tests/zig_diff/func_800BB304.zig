//! zig_diff spec: func_800BB304 -> func_800BB304.func_800BB304 (4A0A8.c).
//! Inputs: arg0 is null or a block in the arena, arg1 is an edge short or any
//! short. The C body is `return 0;` and reads neither argument, so the spec
//! checks that the port returns 0 for every argument pair and writes nothing.
//! The arena block is the only memory an argument points at, so the mutations
//! that touch arg0 are caught by the state comparison.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB304));

pub const classes = [_][:0]const u8{ "null-arg0", "arg0-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var p: [*c]u8 = null;
    if (!ctx.chance(4)) p = ctx.allocBytes(8, 1);
    const a: c_short = ctx.pick(c_short, &.{ 0, 1, -1, 255, 256, 32767, -32768 });
    return .{ p, a };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
