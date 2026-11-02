//! zig_diff spec: _vecToRotMatrix -> vec_to_rot_matrix.vecToRotMatrix (4A0A8.c).
//! The C has no branches and no comparisons, so the inputs cover the value
//! edges of its arithmetic: the >>12 narrowing to i16 (wrapping past 2^27),
//! negative components (arithmetic shift), zero vectors, and the square sum
//! overflowing int when both narrowed components are -32768 (the port must
//! wrap, as the C does). ratan2 and vs_gte_rsqrt return any int; RotMatrix_gte
//! writes the matrix from the rotation it is given (so a store moved or
//! changed before the call shows) and fills the matrix with stub values.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").vecToRotMatrix));

pub const classes = [_][:0]const u8{ "zero", "sum-overflow", "wide", "negative", "positive" };

fn component(ctx: *zd.Ctx) i32 {
    return switch (ctx.range(u8, 0, 4)) {
        0 => ctx.edge(i32, &.{ 0, -1, 1, 0xFFF, -0xFFF, 0x1000, -0x1000 }),
        1 => ctx.edge(i32, &.{ 0x7FFF_FFFF, -0x8000_0000, 0x7FFF_F000, -0x7FFF_F001, 0x7FF_FFFF }),
        2 => ctx.edge(i32, &.{ -0x800_0000, 0x800_0000, -0x800_0001, 0x800_0FFF, 0x7FF_FFFF }),
        3 => ctx.range(i32, -0x800_0000, 0x800_0000),
        else => ctx.int(i32),
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const vec = ctx.alloc(v.VECTOR);
    const mat = ctx.alloc(v.MATRIX);
    if (ctx.chance(20)) {
        vec.vx = 0;
        vec.vy = 0;
        vec.vz = 0;
    } else if (ctx.chance(6)) {
        // Both narrowed components at -32768: the square sum overflows int.
        vec.vx = -0x800_0000 + ctx.range(i32, 0, 0xFFF);
        vec.vz = -0x800_0000 + ctx.range(i32, 0, 0xFFF);
        vec.vy = component(ctx);
    } else {
        vec.vx = component(ctx);
        vec.vy = component(ctx);
        vec.vz = component(ctx);
    }
    return .{ vec, mat };
}

fn wide(x: i32) bool {
    const s = x >> 12;
    return s > 32767 or s < -32768;
}

pub fn classify(args: Args) usize {
    const vec = args[0].?;
    if (vec.vx == 0 and vec.vy == 0 and vec.vz == 0) return 0;
    const a: i64 = @as(i16, @truncate(vec.vx >> 12));
    const b: i64 = @as(i16, @truncate(vec.vz >> 12));
    if (a * a + b * b > 0x7FFF_FFFF) return 1;
    if (wide(vec.vx) or wide(vec.vy) or wide(vec.vz)) return 2;
    if (vec.vx < 0 or vec.vy < 0 or vec.vz < 0) return 3;
    return 4;
}

pub const stubs = struct {
    // The real RotMatrix_gte writes the matrix from the rotation it is given
    // and leaves its translation to the caller's stub values.
    pub fn RotMatrix_gte(ctx: *zd.Ctx, r: anytype, m: anytype) [*c]c.MATRIX {
        const rot: *const v.SVECTOR = @ptrCast(@alignCast(r));
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        mat.m[0] = .{ rot.vx, rot.vy, rot.vz };
        mat.m[1] = .{ rot.vz, rot.vx, rot.vy };
        mat.m[2] = .{ ctx.value(i16), ctx.value(i16), ctx.value(i16) };
        for (&mat.t) |*t| t.* = ctx.value(i32);
        return null;
    }
};
