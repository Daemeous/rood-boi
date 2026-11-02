//! zig_diff spec: func_800B822C -> func_800B822C.func_800B822C (4A0A8.c).
//! Inputs: a 6-byte command buffer in the arena. Byte 5 is drawn from the
//! quadrant edges (0, 63, 64, 127, 128, 191, 192, 255) half the time, so both
//! sides of each bit-6 and bit-7 split are hit, and byte 3 is 0 or 255 half
//! the time so the addition with the table entry sees both extremes. The
//! 4-entry table D_800E9B58 is set to 0 or 255 entries. The callees
//! (vs_battle_getShort, func_800BFE50, func_800A01C8) are recording stubs
//! with default returns: the C never tests their results. The C has no
//! branches; the classes are the table quadrant `arg0[5] >> 6` selects.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B822C));

pub const classes = [_][:0]const u8{ "table-0", "table-1", "table-2", "table-3" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(8, 1);
    buf[1] = ctx.int(u8);
    buf[2] = ctx.int(u8);
    buf[3] = ctx.pick(u8, &.{ 0, 255 });
    buf[4] = ctx.int(u8);
    buf[5] = ctx.pick(u8, &.{ 0, 63, 64, 127, 128, 191, 192, 255 });
    const t = v.g.D_800E9B58();
    for (0..4) |i| t[i] = ctx.pick(u8, &.{ 0, 255 });
    return .{buf};
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][5] >> 6);
}
