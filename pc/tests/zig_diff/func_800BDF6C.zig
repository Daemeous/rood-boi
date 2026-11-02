//! zig_diff spec: func_800BDF6C -> func_800BDF6C.func_800BDF6C (4A0A8.c).
//! Inputs: one random object in the arena (unk0..unkB random, unk9 zero about
//! one time in eight, unk8 from the exact-step, edge or random families so the
//! add wraps and the clamp and the exact match both happen), the global
//! vs_gametime_tickspeed at edges including the sign bit and INT_MIN, and
//! unk4/unk6 at the i16 edges. func_800BFEBC is a recording stub returning
//! i16-range or extreme values; through the `target` pointer it can rewrite
//! unk6, unk8 and unk9 before returning, which the C re-reads.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BDF6C));

/// The object the last prepare built: the callee writes through it.
var target: ?*v.func_800BDF6C_unk180_t = null;

pub const classes = [_][:0]const u8{ "idle", "clamped", "exact", "running" };

pub const stubs = struct {
    pub fn func_800BFEBC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (target) |t| {
            if (ctx.value(u2) == 0) t.unk6 = ctx.value(i16);
            if (ctx.value(u2) == 0) t.unk8 = ctx.value(u8);
            if (ctx.value(u3) == 0) t.unk9 = ctx.value(u8);
        }
        const sel = ctx.value(u8);
        if (sel < 32) return -32768;
        if (sel < 64) return 32767;
        if (sel < 80) return 0;
        if (sel < 96) return -1;
        return @as(c_int, ctx.value(i16));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.func_800BDF6C_unk180_t);
    const tick = v.g.vs_gametime_tickspeed();
    tick.* = ctx.pick(i32, &.{ 0, 1, 2, 3, 4, 6, 16, -1, -2, -3, 0x7FFF_FFFF, -0x8000_0000 });
    const step: u8 = @truncate(@as(u32, @bitCast(tick.* >> 1)));

    arg.unk1 = ctx.int(u8);
    arg.unk4 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    arg.unk6 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    arg.unk9 = ctx.pick(u8, &.{ 0, 1, 2, 127, 128, 255 });
    if (ctx.chance(8)) arg.unk9 = 0;
    switch (ctx.range(u8, 0, 2)) {
        0 => arg.unk8 = arg.unk9 -% step,
        1 => arg.unk8 = ctx.pick(u8, &.{ 0, 1, 254, 255 }),
        else => arg.unk8 = ctx.int(u8),
    }
    target = arg;
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    if (arg.unk9 == 0) return 0;
    const step: u8 = @truncate(@as(u32, @bitCast(v.g.vs_gametime_tickspeed().* >> 1)));
    const next = arg.unk8 +% step;
    if (arg.unk9 < next) return 1;
    if (arg.unk9 == next) return 2;
    return 3;
}
