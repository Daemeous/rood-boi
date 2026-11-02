//! zig_diff spec: func_800B7574 -> func_800B7574.func_800B7574 (4A0A8.c).
//! Inputs: a 16-byte random buffer as arg0 (the C reads bytes 1-8 of it, so the
//! two shorts it fetches are random, negative half the time), and any c_short
//! as arg1 (the C never reads it). The C has no branches and no comparisons;
//! the value-level coverage is the sign of each fetched short, which the
//! classes name. vs_battle_getShort is modelled as reading the two bytes at its
//! argument, as the real getter does. func_8007C928 is modelled as copying the
//! 4 flag bytes it is passed into the arena, so the bytes the C stores into its
//! local sp10 are observed. Its return value is an int from ctx.value, which
//! the C discards. func_800BFE50 keeps the default recording stub: its result
//! is passed on to func_8007C928, which the trace compares.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7574));

pub const classes = [_][:0]const u8{ "both-shorts-positive", "second-negative", "first-negative", "both-negative" };

/// Where func_8007C928's stub copies the flag bytes; set by prepare.
var sink: *[4]u8 = undefined;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        const b: [*]const u8 = p;
        const lo: u16 = b[0];
        const hi: u16 = b[1];
        return @bitCast(lo | (hi << 8));
    }

    pub fn func_8007C928(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) c_int {
        _ = arg0;
        _ = arg1;
        const b: [*]const u8 = @ptrCast(arg2.?);
        sink.* = b[0..4].*;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    sink = ctx.alloc([4]u8);
    const buf = ctx.allocBytes(16, 1);
    return .{ buf, ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    const p: [*]const u8 = args[0];
    const a: i16 = @bitCast(@as(u16, p[1]) | (@as(u16, p[2]) << 8));
    const b: i16 = @bitCast(@as(u16, p[3]) | (@as(u16, p[4]) << 8));
    const first_neg: usize = if (a < 0) 2 else 0;
    const second_neg: usize = if (b < 0) 1 else 0;
    return first_neg + second_neg;
}
