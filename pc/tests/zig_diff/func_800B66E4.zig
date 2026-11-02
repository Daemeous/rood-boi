//! zig_diff spec: func_800B66E4 -> func_800B66E4.func_800B66E4 (4A0A8.c).
//! The C body is `return 0;` and never reads its arguments, so the output is
//! a constant. The inputs still vary both arguments: arg0 is null or points at
//! a random arena block (never dereferenced by the C), and arg1 is an edge
//! value or any short. The classes split on arg0 null/non-null and arg1
//! zero/nonzero, so a port or mutation that reads either argument is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B66E4));

pub const classes = [_][:0]const u8{
    "null-arg0-zero-arg1",
    "null-arg0-nonzero-arg1",
    "block-arg0-zero-arg1",
    "block-arg0-nonzero-arg1",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = if (ctx.chance(2)) null else ctx.allocBytes(16, 16);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 255, 256, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const arg0_block: usize = if (args[0] == null) 0 else 2;
    const arg1_nonzero: usize = if (args[1] == 0) 0 else 1;
    return arg0_block + arg1_nonzero;
}
