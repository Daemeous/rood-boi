//! zig_diff spec: func_800A0618 -> func_800A0618.func_800A0618 (30DB0.c).
//! Inputs: arg0 is a table index 0..16 whose D_800F4538 entry is a valid arena
//! object (the other entries are never read). The tick speed is small, sometimes
//! odd and negative, so `tick / 2` and `tick >> 1` differ. arg3 is zero, equal to
//! the half tick (the `arg3 > 0` compare then sees equal sides), above or below
//! it, sometimes at 255..512 (the u8 narrowing). arg1 and arg2 are random.
//! func_800A0204 returns negative, zero and positive values and may change the
//! tick speed and the table entry (both read after the call). func_800AFA28
//! rewrites the object's unk5B2 and may change the tick speed.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0618));

pub const classes = [_][:0]const u8{ "arg3-zero", "arg3-equal-half", "arg3-above-half", "arg3-below-half" };

/// The second object the stubs may put into the table.
var second: *v.D_800F4538_t = undefined;

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, a: anytype, b: anytype, c_arg: anytype, d: anytype) c_int {
        _ = b;
        _ = c_arg;
        _ = d;
        if (ctx.value(u4) == 0) v.g.vs_gametime_tickspeed().* = @as(i32, ctx.value(i8));
        if (ctx.value(u4) == 0) v.g.D_800F4538()[@intCast(a)] = second;
        const r = ctx.value(u8);
        if (r < 64) return -1 - @as(c_int, ctx.value(u8));
        if (r < 112) return 0;
        return @as(c_int, ctx.value(u8));
    }

    pub fn func_800AFA28(ctx: *zd.Ctx, p: anytype, q: anytype, r: anytype) void {
        _ = q;
        _ = r;
        const obj: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        obj.unk5B2 = ctx.value(u8);
        if (ctx.value(u4) == 0) v.g.vs_gametime_tickspeed().* = @as(i32, ctx.value(i8));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    second = ctx.alloc(v.D_800F4538_t);
    const arg0: c_int = @as(c_int, ctx.range(u8, 0, 16));
    v.g.D_800F4538()[@intCast(arg0)] = obj;

    const tick: i32 = if (ctx.chance(2))
        ctx.range(i32, -1000, 1000)
    else
        ctx.edge(i32, &.{ 0, 1, -1, 2, -2, 3, -3, 4, -5, 64, 1000, -1000 });
    v.g.vs_gametime_tickspeed().* = tick;

    const half = @divTrunc(tick, 2);
    const arg3: i32 = if (ctx.chance(5))
        0
    else if (ctx.chance(3))
        half +% ctx.range(i32, -2, 2)
    else if (ctx.chance(6))
        ctx.edge(i32, &.{ 255, 256, 257, 511, 512, -256 })
    else
        ctx.range(i32, -600, 600);

    return .{ arg0, ctx.int(c_int), ctx.int(c_int), arg3 };
}

pub fn classify(args: Args) usize {
    const half = @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
    if (args[3] == 0) return 0;
    if (args[3] == half) return 1;
    return if (args[3] > half) 2 else 3;
}
