//! zig_diff spec: func_800BFE34 -> func_800BFE34.func_800BFE34 (573B8.c).
//! Inputs: one byte in the arena, any value 0-255 (edges 0, 1, 127, 128, 129,
//! 255 half the time). The C reads arg0[0] once and returns the table entry
//! D_800E9C30[arg0[0]], a u8 widened to int. The table is random bytes from the
//! harness fill, so every index reads a distinct, unpredictable entry. Classes
//! split the index below and at or above 128, the signed-char boundary.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BFE34));

pub const classes = [_][:0]const u8{ "index-below-128", "index-at-or-above-128" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const byte = ctx.alloc(u8);
    byte.* = ctx.pick(u8, &.{ 0, 1, 127, 128, 129, 255 });
    return .{byte};
}

pub fn classify(args: Args) usize {
    return if (args[0][0] < 128) 0 else 1;
}
