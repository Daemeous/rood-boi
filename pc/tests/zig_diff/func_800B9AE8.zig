//! zig_diff spec: func_800B9AE8 -> func_800B9AE8.func_800B9AE8 (4A0A8.c).
//! Inputs: a 4-byte block in the arena whose bytes 1-3 are drawn from edge
//! values (0, 1, 0x7F, 0x80, 0xFF) so the call arguments cover their full
//! unsigned range (arg0[3] * 0x10 reaches 0xFF0), with byte 0 and the short
//! arg1 random (the C ignores both). The C has no branches; it always
//! returns 0 and makes one call to func_8007C444, which is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9AE8));

pub const classes = [_][:0]const u8{ "byte3-low", "byte3-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(4, 1);
    const edges = [_]u8{ 0, 1, 0x7F, 0x80, 0xFF };
    bytes[0] = ctx.int(u8);
    bytes[1] = ctx.pick(u8, &edges);
    bytes[2] = ctx.pick(u8, &edges);
    bytes[3] = ctx.pick(u8, &edges);
    return .{ bytes, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if ((args[0][3] & 0x80) != 0) 1 else 0;
}
