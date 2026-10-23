//! zig_diff spec: func_800DBFE4 -> func_800DBFE4.func_800DBFE4 (73700.c).
//!
//! Inputs: an actorTimerState in the arena (never null: the C dereferences it
//! unconditionally). timer8E is drawn from edges around 0, 1, 6-8, 14-16,
//! 22-24, 119-121, 0x8000, 0xFFFE and 0xFFFF. The C compares timer8E after
//! its increment, so the pre-increment values 6, 14 and 22 make the `< 8`,
//! `< 16` and `< 24` compares exactly equal on the incremented value (7, 15
//! and 23 are the `<=` forms of those bounds), and 119 does the same for the
//! `<= 120` test. The 0xFFFF edge wraps the increment to 0. timer9A and
//! timer98 are drawn from 0, 1, 2, 0x8000, 0x8001 and 0xFFFF, so the zero
//! tests see nonzero values with and without the sign bit and the decrement
//! reaches 0. The callees are stubs that rewrite the timers and the id the C
//! reads back after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_73700");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DBFE4));

pub const classes = [_][:0]const u8{ "idle", "wrapped", "active", "expired-9A-zero", "expired-9A-nonzero" };

/// The state the stubs write through (set by prepare, same object in both runs).
var st: ?*v.actorTimerState = null;

pub const stubs = struct {
    pub fn func_800D820C(ctx: *zd.Ctx, id: anytype, value: anytype) void {
        _ = id;
        _ = value;
        // The C reads timer98 after this call in the timer9A branch.
        if (st) |s| {
            if (ctx.value(u1) == 1) s.timer98 = ctx.value(u16);
        }
    }

    pub fn func_800E4C8C(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.actorTimerState = @ptrCast(@alignCast(p));
        // The C reads timer9A and timer98 after this call.
        if (ctx.value(u1) == 1) s.timer9A = ctx.value(u16);
        if (ctx.value(u2) == 0) s.timer98 = ctx.value(u16);
    }

    pub fn func_800D82CC(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.actorTimerState = @ptrCast(@alignCast(p));
        // The C reads id (for the call) and timer9A after this call.
        if (ctx.value(u1) == 1) s.id = ctx.value(u8);
        if (ctx.value(u1) == 1) s.timer9A = ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.actorTimerState);
    s.id = ctx.int(u8);
    s.timer8E = ctx.pick(u16, &.{ 0, 1, 6, 7, 8, 14, 15, 16, 22, 23, 24, 119, 120, 121, 0x8000, 0xFFFE, 0xFFFF });
    s.timer9A = ctx.pick(u16, &.{ 0, 1, 2, 0x8000, 0x8001, 0xFFFF });
    s.timer98 = ctx.pick(u16, &.{ 0, 1, 2, 0x8000, 0x8001, 0xFFFF });
    st = s;
    return .{s};
}

/// Classified on the state before the call (timer8E is the pre-increment value).
pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (s.timer8E == 0) return 0;
    if (s.timer8E == 0xFFFF) return 1;
    if (s.timer8E < 120) return 2;
    return if (s.timer9A == 0) 3 else 4;
}
