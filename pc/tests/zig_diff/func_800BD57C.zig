//! zig_diff spec: func_800BD57C -> func_800BD57C.func_800BD57C (4A0A8.c).
//! Inputs: one random func_800BD57C_t in the arena with unkA from 0, +-1, the
//! i16 extremes and typical values, so the active test sees zero, negative and
//! positive values. The global tickspeed comes from edges (0, +-1, +-2, the
//! i32 extremes, 2^16 steps). About a quarter of the time unk8 is set so the
//! update lands exactly on unkA, which gives the `>=` equality case. The
//! callee is pure: it takes its arguments by value and touches no state, so
//! its stub only returns a value. It returns an i16-range value, so unk6 times
//! the result cannot overflow the C's int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BD57C));

pub const classes = [_][:0]const u8{ "inactive", "running", "reached" };

pub const stubs = struct {
    pub fn func_800BFEBC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        return @as(c_int, ctx.value(i16));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800BD57C_t);
    const tick = ctx.pick(i32, &.{ 0, 1, -1, 2, -2, 3, 0x7FFF_FFFF, -0x8000_0000, 0x1_0000, -0x1_0000 });
    v.g.vs_gametime_tickspeed().* = tick;
    obj.unkA = ctx.pick(i16, &.{ 0, 1, -1, 2, 100, 32767, -32768 });
    obj.unk8 = ctx.pick(i16, &.{ 0, 1, -1, 99, 100, 32767, -32768 });
    // Land the update exactly on unkA: unk8 + (tickspeed >> 1) == unkA.
    if (ctx.chance(4)) obj.unk8 = obj.unkA -% @as(i16, @truncate(tick >> 1));
    obj.unk0 = ctx.int(u8);
    obj.unk1 = ctx.int(u8);
    obj.unk2 = ctx.int(i16);
    obj.unk4 = ctx.int(i16);
    obj.unk6 = ctx.int(i16);
    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    if (obj.unkA <= 0) return 0;
    const next = obj.unk8 +% @as(i16, @truncate(v.g.vs_gametime_tickspeed().* >> 1));
    return if (next >= obj.unkA) 2 else 1;
}
