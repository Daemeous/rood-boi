//! zig_diff spec: func_800882F4 -> func_800882F4.func_800882F4 (146C.c).
//! Inputs: the timer's mode 0, 1 or another value; each field at the edges
//! its carries and borrows test (0, 59, 60, -1, 127, -128 and the tick
//! landing on them), the tick speed 1, 2, 3, 0 or -1; rand's stub any int;
//! func_800BEC58's stub 1 or not.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800882F4));

pub const classes = [_][:0]const u8{ "off", "up", "up-stopped", "down", "down-done" };

pub const stubs = struct {
    pub fn func_800BEC58(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) c_short {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        return ctx.edge(c_short, &.{ 0, 1, 1, 2, -1 });
    }
};

fn field(ctx: *zd.Ctx) i8 {
    return ctx.pick(i8, &.{ 0, 0, 1, 58, 59, 59, 60, -1, 127, -128 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const timer = v.g.D_80050468();
    timer.unk0 = ctx.edge(u8, &.{ 0, 1, 1, 1, 2, 2, 0x80, 0xFF });
    timer.frames = field(ctx);
    timer.seconds = field(ctx);
    timer.minutes = field(ctx);
    timer.hours = field(ctx);
    if (ctx.chance(4)) {
        timer.seconds = 0;
        timer.minutes = 0;
        timer.hours = 0;
        if (ctx.chance(2)) timer.frames = 0;
    }
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 1, 2, 2, 3, 0, -1, 60 });
    return .{};
}

pub fn classify(_: Args) usize {
    const timer = v.g.D_80050468();
    if (timer.unk0 == 0) return 0;
    if (timer.unk0 == 1) return if (timer.hours <= 0) 1 else 2;
    return if ((@as(i32, timer.frames) | timer.seconds | timer.minutes | timer.hours) != 0) 3 else 4;
}
