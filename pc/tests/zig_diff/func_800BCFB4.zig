//! zig_diff spec: func_800BCFB4 -> func_800BCFB4.func_800BCFB4 (4A0A8.c).
//! Inputs: a random D_800F4BA4_t2 (position, target and computed are any i32,
//! so the >> 12 and the subtractions wrap) and a D_800F4BA4 in the arena whose
//! unk27C the final compare reads. The step count is the spec's `rsq` (what
//! vs_gte_rsqrt returns) over `value`, a divisor that is never 0 or -1 with a
//! dividend of INT_MIN: short (steps < 2), clamped (steps > 10) and in between.
//! Heights and candidate.vy are both drawn from -4..3, so the loop's
//! `height < candidate.vy` test sees equal sides often. The _vectorAdd stub
//! copies unk27C into candidate in part or in whole, so the final compare
//! takes each of its partial and full outcomes. VectorNormal writes a mix of
//! its input's components to its output, so the integer parts
//! _toNormalIntegerVector hands it are compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BCFB4));

pub const classes = [_][:0]const u8{ "short", "clamped", "mid" };

/// What vs_gte_rsqrt returns this iteration (set by prepare, read by the stub).
var rsq: i32 = 0;

fn small(ctx: *zd.Ctx) i32 {
    return @as(i32, @intCast(ctx.value(u32) % 8)) - 4;
}

pub const stubs = struct {
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = ctx;
        _ = x;
        return rsq;
    }

    pub fn func_800BD2B8(ctx: *zd.Ctx, m: anytype) c_int {
        _ = m;
        return small(ctx);
    }

    pub fn _vectorSubtract(ctx: *zd.Ctx, dst: anytype, a: anytype, b: anytype) [*c]v.VECTOR {
        _ = a;
        _ = b;
        const d: *v.VECTOR = @ptrCast(@alignCast(dst));
        d.vx = ctx.value(i32);
        d.vy = ctx.value(i32);
        d.vz = ctx.value(i32);
        return d;
    }

    pub fn _vectorAdd(ctx: *zd.Ctx, dst: anytype, a: anytype, b: anytype) [*c]v.VECTOR {
        _ = a;
        _ = b;
        const d: *v.VECTOR = @ptrCast(@alignCast(dst));
        d.vx = ctx.value(i32);
        d.vy = small(ctx);
        d.vz = ctx.value(i32);
        // The candidate (0x1F8000A8): copy unk27C into any subset of its components,
        // so the final compare sees equal and unequal components.
        if (@intFromPtr(d) == zd.scratchpad + 0xA8) {
            const src = &v.g.D_800F4BA4().*.?.unk27C;
            const m = ctx.value(u32);
            if (m & 1 != 0) d.vx = src.vx;
            if (m & 2 != 0) d.vy = src.vy;
            if (m & 4 != 0) d.vz = src.vz;
        }
        return d;
    }

    pub fn _vectorFixedPointMult(ctx: *zd.Ctx, dst: anytype, src: anytype, k: anytype) [*c]v.VECTOR {
        _ = src;
        _ = k;
        const d: *v.VECTOR = @ptrCast(@alignCast(dst));
        d.vx = ctx.value(i32);
        d.vy = ctx.value(i32);
        d.vz = ctx.value(i32);
        return d;
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

    pub fn _vecToRotMatrix(ctx: *zd.Ctx, src: anytype, dst: anytype) void {
        _ = src;
        const m: *v.MATRIX = @ptrCast(@alignCast(dst));
        for (&m.m) |*row| {
            for (row) |*e| e.* = ctx.value(i16);
        }
        for (&m.t) |*e| e.* = ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const c = ctx.alloc(v.D_800F4BA4_t2);
    const g = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = g;
    g.unk27C.vx = ctx.int(i32);
    g.unk27C.vy = ctx.range(i32, -4, 3);
    g.unk27C.vz = ctx.int(i32);
    // A divisor: never 0, and never a value that makes the division trap.
    c.value = ctx.edge(i32, &.{ 4096, 4096, 4096, 4096, 1, 2, 8192, -1, -4096, 3000 });
    // With value 4096 the step count is rsq itself: short, clamped and each edge.
    rsq = ctx.edge(i32, &.{ -5, -1, 0, 1, 2, 3, 4, 5, 9, 10, 11, 12, 40 });
    return .{c};
}

pub fn classify(args: Args) usize {
    const c = args[0].?;
    const steps = @divTrunc(@as(i64, rsq) * 4096, @as(i64, c.value));
    if (steps < 2) return 0;
    if (steps > 10) return 1;
    return 2;
}
