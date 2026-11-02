//! zig_diff spec: func_800B9770 -> func_800B9770.func_800B9770 (4A0A8.c).
//! Inputs: a 4-byte block in the arena whose bytes 1 and 2 (the ones the C
//! reads) are edge values (0, 1, 0x7F, 0x80, 0xFF or any byte), plus any
//! `arg1` (a short the C never reads). Byte 3 is in bounds too, so a port that
//! reads it stays inside memory the harness owns. The C has no branches, so
//! the classes split on whether the high bit of arg0[1] is set (the
//! callee's `c_int` argument is zero-extended, not sign-extended).
//! The callee `func_8008E224` is a void recording stub: the harness hashes
//! the state at each call and compares the two arguments.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9770));

pub const classes = [_][:0]const u8{ "byte1-high", "byte1-low" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const base: [*]u8 = ctx.allocBytes(4, 1);
    base[0] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    base[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    base[2] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    base[3] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    const arg1: c_short = ctx.pick(c_short, &.{ 0, -1, 0x7FFF, -0x8000 });
    return .{ @ptrCast(base), arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 0x80) 0 else 1;
}
