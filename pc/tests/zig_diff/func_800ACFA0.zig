//! zig_diff spec: func_800ACFA0 -> func_800ACFA0.func_800ACFA0 (40564.c).
//! Inputs: an SVECTOR in a 1 KB arena block (the C also writes the element 84
//! slots on, at byte 672, so the block must hold it), and a 6-byte source.
//! Each source byte is an edge (0x00, 0x7F, 0x80, 0xFF), a uniform random
//! byte, or a high byte (0x80..0xFF), so the sign of every low byte
//! (`(char)` in the second, fourth and sixth reads) and every 16-bit value
//! that wraps an i16 after the doubling both occur. The selector arg2 is 0xFF
//! half the time and otherwise an edge or a random int, so both sides of the
//! `!= 0xFF` test are taken.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800ACFA0));

pub const classes = [_][:0]const u8{ "arg2-ff", "arg2-not-ff" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const out: *v.SVECTOR = @ptrCast(@alignCast(ctx.allocBytes(1024, 16)));
    const src = ctx.allocBytes(8, 1);
    for (src[0..6], 0..) |*b, i| {
        if (ctx.chance(3)) {
            // The high-bit low bytes (the second, fourth and sixth reads)
            // are where a sign-extended read differs from a zero-extended one.
            b.* = if (i % 2 == 1) ctx.range(u8, 0x80, 0xFF) else ctx.int(u8);
        } else if (ctx.chance(2)) {
            b.* = ctx.int(u8);
        } else {
            b.* = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
        }
    }
    const arg2: c_int = if (ctx.chance(2))
        0xFF
    else
        ctx.pick(c_int, &.{ 0, 1, 0xFE, 0x100, -1 });
    return .{ out, src, arg2 };
}

pub fn classify(args: Args) usize {
    return if (args[2] == 0xFF) 0 else 1;
}
