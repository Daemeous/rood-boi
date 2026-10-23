//! zig_diff spec: func_800A3BC4 -> func_800A3BC4.func_800A3BC4 (3A1A0.c).
//! Inputs: two c_int arguments drawn from edges (0, 1, -1, INT_MAX, INT_MIN,
//! 0x10000) and random values. The callee func_8008DD0C is a stub that returns
//! 0 about one time in eight, the edge shorts 0x8000 and 0x4000 (bit 15 and
//! bit 14 alone, where the sign-extension from bit 14 matters), and otherwise a
//! random 16-bit value, so both signs of a nonzero result occur. The call's
//! arguments are recorded by the harness, so a swapped call is caught.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3BC4));

pub const classes = [_][:0]const u8{ "arg0-nonnegative", "arg0-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{
        ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff_ffff, -0x8000_0000, 0x1_0000 }),
        ctx.pick(c_int, &.{ 0, 1, -1, 5, 0x7fff_ffff, 0x1_0000 }),
    };
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 1 else 0;
}

pub const stubs = struct {
    pub fn func_8008DD0C(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) c_short {
        _ = arg0;
        _ = arg1;
        const r = ctx.value(u8);
        if (r < 32) return 0;
        if (r < 48) return @bitCast(@as(u16, 0x8000));
        if (r < 56) return @bitCast(@as(u16, 0x4000));
        return ctx.value(c_short);
    }
};
