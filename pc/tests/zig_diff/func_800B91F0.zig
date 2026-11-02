//! zig_diff spec: func_800B91F0 -> func_800B91F0.func_800B91F0 (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena at a random 0-3 byte offset, so
//! `arg0 + 1` and `arg0[3]` read random bytes at every alignment, and an edge
//! short for arg1 (the C ignores it). The C has no branches or comparisons.
//! The callees are default stubs: vs_battle_getShort returns a random short,
//! func_800BFE50 returns null (the default pointer stub), and func_8009DBDC
//! records its arguments, which the trace compares.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B91F0));

/// The class is the buffer's offset from the 16-aligned arena start.
pub const classes = [_][:0]const u8{ "offset-0", "offset-1", "offset-2", "offset-3" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    const off = ctx.range(usize, 0, 3);
    const p: [*c]u8 = buf + off;
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    return @intFromPtr(args[0]) & 3;
}
