//! zig_diff spec: func_800B8C54 -> func_800B8C54.func_800B8C54 (4A0A8.c).
//! Inputs: a 3-byte record in the arena. Byte 1 is either 0xFF (the C skips
//! the store) or an index 0-15 into D_800F4C10, its 16 entries; no index
//! past the table is built, since the C would write outside it. Byte 0 and
//! byte 2 are any u8, so the stored value and the wrong-field mutations
//! differ. arg1 is any short; the C ignores it and returns 0.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8C54));

pub const classes = [_][:0]const u8{ "skip-0xFF", "store" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const rec = ctx.allocBytes(3, 1);
    rec[0] = ctx.int(u8);
    rec[1] = if (ctx.chance(3)) 0xFF else ctx.range(u8, 0, 15);
    rec[2] = ctx.int(u8);
    return .{ rec, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] == 0xFF) 0 else 1;
}
