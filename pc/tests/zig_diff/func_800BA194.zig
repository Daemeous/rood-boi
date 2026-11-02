//! zig_diff spec: func_800BA194 -> func_800BA194.func_800BA194 (4A0A8.c).
//! Inputs: a 4-byte arena block whose byte 1 selects the switch arm (0, 1, or
//! another value) and whose byte 2 is any u8 (so the signed read of byte 2 is
//! exercised), and the global D_800F4C2C at 2, below 2, and above 2. The
//! callee func_80093764 returns 0 about a third of the time, any c_int
//! otherwise, and may rewrite D_800F4C2C. arg1 is unused and set to any short.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA194));

pub const classes = [_][:0]const u8{
    "case0-flag-2",
    "case0-flag-above-2",
    "case0-flag-below-2",
    "case1",
    "other",
};

pub const stubs = struct {
    pub fn func_80093764(ctx: *zd.Ctx, t: anytype) c_int {
        _ = t;
        // The callee may change the flag; the C never reads it again after the calls.
        if (ctx.value(u8) < 64) v.g.D_800F4C2C().* = ctx.value(u8);
        // Zero about a third of the time: the C's `!= 0` test sees both outcomes.
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(4, 1);
    bytes[0] = ctx.int(u8);
    bytes[1] = ctx.edge(u8, &.{ 0, 1, 2, 3, 0xFF });
    bytes[2] = ctx.int(u8);
    bytes[3] = ctx.int(u8);
    v.g.D_800F4C2C().* = ctx.edge(u8, &.{ 0, 1, 2, 2, 3, 0x80, 0xFF });
    return .{ bytes, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    const d = v.g.D_800F4C2C().*;
    return switch (p[1]) {
        0 => if (d == 2) 0 else if (d > 2) 1 else 2,
        1 => 3,
        else => 4,
    };
}
