//! zig_diff spec: func_800BB450 -> func_800BB450.func_800BB450 (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena, random bytes, so arg0[1] takes every
//! value (both below and above 0x80, which the C passes to func_8007DD50 as an
//! unsigned byte). arg1 is 0..15: the C never reads it, and the mutation that
//! indexes by it stays inside the buffer. The only callee, func_8007DD50, is a
//! void recording stub that needs no state change, and the C has no branches.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB450));

pub const classes = [_][:0]const u8{ "byte-low", "byte-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(16, 1);
    const arg1: c_short = @intCast(ctx.range(u4, 0, 15));
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 128) 1 else 0;
}
