//! zig_diff spec: func_800BCA8C -> func_800BCA8C.func_800BCA8C (4A0A8.c).
//! Inputs: a random camera object (mode, ease selectors, time, duration, follow
//! flags, vectors and angles), with `mode` drawn from the switch's cases half the
//! time and from any byte otherwise, so 15 (the early return), 11 (no time step),
//! the listed cases and the default are all reached. The time step is 0 or 1 often
//! (so `time` stays put and equals `duration` for the `>` and the end test), and
//! `other` is sometimes null (the C never reads it). The scratchpad is random; the
//! callees write their outputs with `ctx.value`, and `_vectorSubtract` returns its
//! first argument, as the C's `_vectorMagnitude` call needs a valid pointer.
//! VectorNormal writes a mix of its input's components to its output, so the
//! integer parts _toNormalIntegerVector hands it are compared; func_800BFEBC
//! returns 0, 4096 or any value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BCA8C));

pub const classes = [_][:0]const u8{
    "mode-15-return", "mode-11", "mode-2", "mode-3", "mode-4", "mode-5", "mode-8", "mode-9", "mode-other",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const c = ctx.alloc(v.D_800F4BA4_t2);
    const other = ctx.alloc(v.D_800F4BA4_t2);

    const tick = v.g.vs_gametime_tickspeed();
    tick.* = ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -2, -3, 0x7FFF_FFFF, -0x8000_0000 });
    if (ctx.chance(2)) tick.* = ctx.edge(i32, &.{ 0, 1 });

    c.mode = ctx.pick(u8, &.{ 2, 3, 4, 5, 8, 9, 11, 15 });
    c.time = ctx.int(u8);
    c.duration = ctx.int(u8);
    if (ctx.chance(3)) c.time = c.duration;
    c.ease1 = ctx.int(u8);
    c.ease2 = ctx.int(u8);
    c.ease3 = ctx.int(u8);
    c.follow = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    c.following = ctx.pick(u8, &.{ 0, 1, 2, 0x80, 0xFF });
    c.angle = ctx.int(i16);
    c.angleDelta = ctx.int(i16);
    c.value = ctx.int(i32);
    c.delta = ctx.int(i32);

    return .{ c, if (ctx.chance(2)) null else other };
}

pub fn classify(args: Args) usize {
    const c = args[0].?;
    return switch (c.mode) {
        15 => 0,
        11 => 1,
        2 => 2,
        3 => 3,
        4 => 4,
        5 => 5,
        8 => 6,
        9 => 7,
        else => 8,
    };
}

/// Writes random components into a VECTOR-shaped destination and returns it.
fn writeVec(ctx: *zd.Ctx, p: anytype) *v.VECTOR {
    const d: *v.VECTOR = @ptrCast(@alignCast(p));
    d.vx = ctx.value(i32);
    d.vy = ctx.value(i32);
    d.vz = ctx.value(i32);
    return d;
}

pub const stubs = struct {
    pub fn _vectorAdd(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype) *v.VECTOR {
        _ = b;
        _ = d;
        return writeVec(ctx, a);
    }

    pub fn _vectorSubtract(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype) *v.VECTOR {
        _ = b;
        _ = d;
        // Returned pointer is fed to _vectorMagnitude, so it must be valid.
        return writeVec(ctx, a);
    }

    pub fn _vectorFixedPointMult(ctx: *zd.Ctx, dst: anytype, src: anytype, scale: anytype) *v.VECTOR {
        _ = src;
        _ = scale;
        return writeVec(ctx, dst);
    }

    pub fn RotMatrix_gte(ctx: *zd.Ctx, r: anytype, m: anytype) *v.MATRIX {
        _ = r;
        const mx: *v.MATRIX = @ptrCast(@alignCast(m));
        for (0..3) |i| {
            for (0..3) |j| {
                mx.m[i][j] = ctx.value(i16);
            }
        }
        for (0..3) |i| {
            mx.t[i] = ctx.value(i32);
        }
        return mx;
    }

    pub fn ApplyMatrixLV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) *v.VECTOR {
        _ = m;
        _ = v0;
        return writeVec(ctx, v1);
    }

    pub fn func_800A190C(ctx: *zd.Ctx, value: anytype, delta: anytype, s: anytype, n: anytype) void {
        _ = value;
        _ = delta;
        _ = n;
        const sv: *v.SVECTOR = @ptrCast(@alignCast(s));
        sv.vx = ctx.value(i16);
        sv.vy = ctx.value(i16);
        sv.vz = ctx.value(i16);
    }

    pub fn VectorNormal(ctx: *zd.Ctx, src: anytype, dst: anytype) c_int {
        const a: *v.VECTOR = @ptrCast(@alignCast(src));
        const d: *v.VECTOR = @ptrCast(@alignCast(dst));
        const x = a.vx;
        const y = a.vy;
        const z = a.vz;
        d.vx = x +% y *% 3;
        d.vy = y +% z *% 5;
        d.vz = z +% x *% 7;
        return ctx.value(c_int);
    }

    pub fn func_800BFEBC(ctx: *zd.Ctx, ease: anytype, time: anytype, duration: anytype) c_int {
        _ = ease;
        _ = time;
        _ = duration;
        return switch (ctx.value(u2)) {
            0 => 0,
            1 => 4096,
            else => ctx.value(c_int),
        };
    }

    pub fn func_800BCFB4(ctx: *zd.Ctx, c: anytype) void {
        const cc: *v.D_800F4BA4_t2 = @ptrCast(@alignCast(c));
        _ = writeVec(ctx, &cc.computed);
        if (ctx.value(u1) == 1) cc.current.vx = ctx.value(i32);
    }
};
