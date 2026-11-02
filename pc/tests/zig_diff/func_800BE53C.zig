//! zig_diff spec: func_800BE53C -> func_800BE53C.func_800BE53C (4A0A8.c).
//! Inputs: one D_800F4BA4_t2 in the arena with current and position per axis.
//! The delta (current - position) is small, so the C's squares never overflow,
//! while position is sometimes near INT_MAX so current wraps past it (the
//! wrapped-delta classes). Duration covers 0, 1, 127, 128 and 255. The
//! magnitude stub returns an i16-range value, so the magnitude is negative
//! about half the time and the signed `>> 12` and `/ 1000` see negatives.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE53C));

pub const classes = [_][:0]const u8{
    "zero-delta",
    "negative-delta",
    "negative-delta-wrapped",
    "positive-delta",
    "positive-delta-wrapped",
};

fn setAxis(ctx: *zd.Ctx, pos: *i32, cur: *i32) void {
    const base: i32 = if (ctx.chance(3))
        ctx.range(i32, std.math.maxInt(i32) - (1 << 27), std.math.maxInt(i32))
    else
        ctx.int(i32);
    const delta: i32 = if (ctx.chance(4))
        ctx.edge(i32, &.{ 0, 1, -1, 1 << 27, -(1 << 27) })
    else
        ctx.range(i32, -(1 << 27), 1 << 27);
    pos.* = base;
    cur.* = base +% delta;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800F4BA4_t2);
    setAxis(ctx, &d.position.vx, &d.current.vx);
    setAxis(ctx, &d.position.vy, &d.current.vy);
    setAxis(ctx, &d.position.vz, &d.current.vz);
    d.duration = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{d};
}

pub fn classify(args: Args) usize {
    const d = args[0].?;
    const dx = d.current.vx -% d.position.vx;
    if (dx == 0) return 0;
    const neg = dx < 0;
    const wrapped = neg != (d.current.vx < d.position.vx);
    var class: usize = if (neg) 1 else 3;
    if (wrapped) class += 1;
    return class;
}

pub const stubs = struct {
    // Writes the difference a - b into the out-parameter, as the real call does.
    pub fn _vectorSubtract(ctx: *zd.Ctx, out: anytype, a: anytype, b: anytype) @TypeOf(out) {
        _ = ctx;
        const o: *v.VECTOR = @ptrCast(@alignCast(out));
        const x: *const v.VECTOR = @ptrCast(@alignCast(a));
        const y: *const v.VECTOR = @ptrCast(@alignCast(b));
        o.vx = x.vx -% y.vx;
        o.vy = x.vy -% y.vy;
        o.vz = x.vz -% y.vz;
        return out;
    }

    // A plain three-word copy into the destination.
    pub fn copyVector(ctx: *zd.Ctx, dst: anytype, src: anytype) void {
        _ = ctx;
        const o: *v.VECTOR = @ptrCast(@alignCast(dst));
        const s: *const v.VECTOR = @ptrCast(@alignCast(src));
        o.* = s.*;
    }

    // The reciprocal square root: any i16-range value, so the magnitude
    // (times ONE) never overflows and is negative about half the time.
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, sum: anytype) i32 {
        _ = sum;
        return @as(i32, ctx.value(i16));
    }
};
