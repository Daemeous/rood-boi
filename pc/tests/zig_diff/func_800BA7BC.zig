//! zig_diff spec: func_800BA7BC -> func_800BA7BC.func_800BA7BC (4A0A8.c).
//! Inputs: an arena pointer for arg0 (the C only passes arg0 + 1 on, never
//! dereferences it) and a random arg1 (the C ignores it). The high half of
//! func_80044D94's return is compared signed against vs_battle_getShort's
//! result. A per-run plan (equal, less, greater) makes the stubs pick the
//! getShort result relative to that high half, so the `<` sees equal sides
//! often and both outcomes. The first return's sign bit is random, so
//! negative values reach the signed compare.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA7BC));

pub const classes = [_][:0]const u8{ "equal", "less", "greater" };

/// The relation the stubs make getShort bear to func_80044D94's high half.
var plan: u8 = 0;
/// The high half of the last func_80044D94 return (the C calls it first).
var last_hi: i32 = 0;

pub const stubs = struct {
    pub fn func_80044D94(ctx: *zd.Ctx) c_int {
        const r = ctx.value(c_int);
        last_hi = @intCast(r >> 16);
        return r;
    }

    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        const hi: i32 = last_hi;
        switch (plan) {
            0 => return @intCast(hi),
            1 => return @intCast(@min(hi + 1 + @as(i32, ctx.value(u8)), 32767)),
            else => return @intCast(@max(hi - 1 - @as(i32, ctx.value(u8)), -32768)),
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    plan = ctx.random.uintLessThan(u8, 3);
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(16, 1));
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @as(usize, plan);
}
