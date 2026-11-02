//! zig_diff spec: func_800BDBB4 -> func_800BDBB4.func_800BDBB4 (4A0A8.c).
//! Inputs: an arena object with random fields, a random 32-bit timer step
//! (vs_gametime_tickspeed, negative and wrapping values included) and
//! edge values for the timer, the end time, the speed fields and the
//! 1E-zero early exit. Half the time the timer is set so that the advanced
//! timer lands exactly on the end time, so the reset branch is taken often.
//! The callee func_800BFEBC is a pure lookup: it writes nothing the C reads
//! back, and returns any c_int, which the C stores into the scratchpad and
//! multiplies by the speed fields.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BDBB4));

pub const classes = [_][:0]const u8{ "timer-zero", "timer-reaches-end", "timer-running" };

pub const stubs = struct {
    pub fn func_800BFEBC(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = ctx.alloc(v.func_800BDBB4_t);
    const ts = v.g.vs_gametime_tickspeed();
    ts.* = ctx.pick(i32, &.{ 0, 1, -1, 2, -2, 3, 0x7FFF_FFFF, -0x8000_0000, 0x1_0000, 0xFFFF });

    t.unk1 = ctx.int(u8);
    t.unkA = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
    t.unkC = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
    t.unkE = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
    t.unk12 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
    t.unk14 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
    t.unk16 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });

    // The end time: zero (the early exit), signs both ways, and a 16-bit wrap.
    t.unk1E = ctx.pick(i16, &.{ 0, 1, -1, 2, 255, 256, 32767, -32768 });
    if (ctx.chance(4)) t.unk1E = 0;
    t.unk1C = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });

    // Half the time the advanced timer lands exactly on the end time.
    if (ctx.chance(2)) {
        const step: i32 = ts.* >> 1;
        t.unk1C = @truncate(@as(i32, t.unk1E) - step);
    }
    return .{t};
}

pub fn classify(args: Args) usize {
    const t = args[0].?;
    if (t.unk1E == 0) return 0;
    const step: i32 = v.g.vs_gametime_tickspeed().* >> 1;
    const next: i16 = @truncate(@as(i32, t.unk1C) + step);
    return if (next == t.unk1E) 1 else 2;
}
