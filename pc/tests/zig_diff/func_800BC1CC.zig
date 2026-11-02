//! zig_diff spec: func_800BC1CC -> func_800BC1CC.func_800BC1CC (4A0A8.c).
//! Inputs: arg0 and arg1 at their edges (0, -1, the short and int extremes,
//! 65536, which truncates to 0 as a short), the early-return flag
//! D_800F4BA0 zero or nonzero (1, 0x80 and 0xFF, with and without the sign
//! bit), and a camera object in the arena. The camera-setup and vector
//! helpers are stubs that write the vectors they produce, and the rsqrt stub
//! either returns 0 (so the magnitude sum is zero and the result is zero) or
//! any 32-bit value (so the sum wraps and the quotient goes negative).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BC1CC));

pub const classes = [_][:0]const u8{ "early-return", "zero-magnitude", "nonzero-result" };

/// Set by prepare: the rsqrt stub returns 0, so the computed result is 0.
var rsqrt_zero: bool = false;

fn fillVector(ctx: *zd.Ctx, p: anytype) void {
    const d: *v.VECTOR = @ptrCast(@alignCast(p));
    d.vx = ctx.value(i32);
    d.vy = ctx.value(i32);
    d.vz = ctx.value(i32);
    d.pad = ctx.value(i32);
}

pub const stubs = struct {
    /// Writes both camera vectors it is given.
    pub fn vs_battle_initialiseCameraFromSpherical(ctx: *zd.Ctx, a: anytype, b: anytype, x: anytype, y: anytype) void {
        _ = x;
        _ = y;
        fillVector(ctx, a);
        fillVector(ctx, b);
    }

    /// The real copy: the C reads the destination back.
    pub fn copyVector(ctx: *zd.Ctx, dst: anytype, src: anytype) void {
        _ = ctx;
        const d: *v.VECTOR = @ptrCast(@alignCast(dst));
        const s: *const v.VECTOR = @ptrCast(@alignCast(src));
        d.* = s.*;
    }

    /// The real difference, returning its output (the C passes it on).
    pub fn _vectorSubtract(ctx: *zd.Ctx, out: anytype, a: anytype, b: anytype) @TypeOf(out) {
        _ = ctx;
        const d: *v.VECTOR = @ptrCast(@alignCast(out));
        const x: *const v.VECTOR = @ptrCast(@alignCast(a));
        const y: *const v.VECTOR = @ptrCast(@alignCast(b));
        d.vx = x.vx -% y.vx;
        d.vy = x.vy -% y.vy;
        d.vz = x.vz -% y.vz;
        d.pad = 0;
        return out;
    }

    /// Either 0 (the zero class) or any 32-bit value (wrapping sums).
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        if (rsqrt_zero) return 0;
        return ctx.value(c_int);
    }

    /// Writes a byte of the table it is handed; returns the int the C prototype gives.
    pub fn func_800BC2E8(ctx: *zd.Ctx, p: anytype, k: anytype) c_int {
        _ = k;
        const bytes: [*]u8 = @ptrCast(p);
        bytes[0] = ctx.value(u8);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const cam = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = cam;
    v.g.D_800F4BA0().* = if (ctx.chance(3)) 0 else ctx.edge(u8, &.{ 1, 0x80, 0xFF });
    rsqrt_zero = ctx.chance(3);
    const arg0 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    const arg1 = ctx.pick(i32, &.{ 0, 1, -1, 65535, 65536, 0x7FFF_FFFF, -0x8000_0000 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F4BA0().* != 0) return 0;
    return if (rsqrt_zero) 1 else 2;
}
