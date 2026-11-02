//! zig_diff spec: func_800BB3BC -> func_800BB3BC.func_800BB3BC (4A0A8.c).
//! Inputs: a 4-byte buffer in the arena (byte 0 and 3 unused by the C, any value),
//! byte 1 from the edges around the 0xFF escape and the +4 wrap (0xFC, 0xFD, 0xFE, 0xFF,
//! plus random), byte 2 from the zero test edges (0, 1, 0x7F, 0x80, 0xFF, plus random),
//! and arg1 (unused by the C) from the short edges. The callee func_800BC1CC is a
//! recording void stub; it cannot touch any state the C reads back, and the C's only
//! other effect is the store to D_800F4BB6, which the harness compares as global state.
const std = @import("std");
const zd = @import("zdiff");
const port = @import("port");

pub const Args = std.meta.ArgsTuple(@TypeOf(port.func_800BB3BC));

// Index = (byte1 == 0xFF ? 0 : 2) + (byte2 == 0 ? 1 : 0).
pub const classes = [_][:0]const u8{ "ff1-nonzero2", "ff1-zero2", "other1-nonzero2", "other1-zero2" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x7F, 0x80, 0x03 });
    buf[2] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[3] = ctx.int(u8);
    const arg1 = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    const b = args[0];
    const base: usize = if (b[1] == 0xFF) 0 else 2;
    const zero: usize = if (b[2] == 0) 1 else 0;
    return base + zero;
}
