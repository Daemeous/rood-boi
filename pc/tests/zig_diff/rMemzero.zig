//! zig_diff spec: vs_battle_rMemzero -> r_memzero.rMemzero (5BF94.c).
//! Inputs: an arena block of 256 random bytes, the base 0..12 bytes into it
//! (also unaligned), the size a positive multiple of 4 up to 240 (the C
//! loops past the base for any other size).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").rMemzero));

pub const classes = [_][:0]const u8{ "one-word", "words", "unaligned" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(256, 16);
    const off = ctx.pick(usize, &.{ 0, 1, 4, 12 }) % 13;
    const words = ctx.pick(c_int, &.{ 1, 2, 60 });
    const size: c_int = 4 * (if (words >= 1 and words <= 60) words else @mod(words, 60) + 1);
    return .{ @as(*anyopaque, @ptrCast(block + off)), size };
}

pub fn classify(args: Args) usize {
    if (@intFromPtr(args[0]) & 3 != 0) return 2;
    return if (args[1] == 4) 0 else 1;
}
