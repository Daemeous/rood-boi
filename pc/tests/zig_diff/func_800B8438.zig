//! zig_diff spec: func_800B8438 -> func_800B8438.func_800B8438 (4A0A8.c).
//! Inputs: a 16-byte arena buffer whose bytes 3, 4 and 6 are the C's reads
//! (byte 6 is the table selector and the call's third argument, bytes 3 and 4
//! feed the table sum and the fourth argument). Byte 6 takes the edges 0, 63,
//! 64, 127, 128, 191, 192 and 255 half the time, so every quarter of
//! D_800E9B58 and both sides of the 0x3F mask are hit. Bytes 3 and 4 take 0,
//! 128 and 255 often, so the sum with the u8 table entry sees its extremes.
//! The C has no comparisons: the classes are the four table selectors.
//! vs_battle_getShort is a stub returning any c_short; the C never reads
//! the buffer through it.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8438));

pub const classes = [_][:0]const u8{ "quarter-0", "quarter-1", "quarter-2", "quarter-3" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    buf[3] = ctx.pick(u8, &.{ 0, 128, 255 });
    buf[4] = ctx.pick(u8, &.{ 0, 128, 255 });
    buf[6] = ctx.pick(u8, &.{ 0, 63, 64, 127, 128, 191, 192, 255 });
    return .{buf};
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][6] >> 6);
}
