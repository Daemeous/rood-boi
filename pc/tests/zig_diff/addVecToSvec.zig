//! zig_diff spec: vs_battle_addVecToSvec -> add_vec_to_svec.addVecToSvec (5BF94.c).
//! Inputs: two random-filled vectors (a VECTOR of i32 and an SVECTOR of i16) and an
//! output that is either a separate arena object or the same object as the first
//! input (the aliasing case). Component values come from edge sets (0, +-1, the
//! i32/i16 extremes) or any value, so sums wrap and go negative. The C has no
//! branches, so the coverage is the straight-line body; the classes separate
//! aliasing, i32 overflow of the sums, negative SVECTOR components, and plain sums.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").addVecToSvec));

pub const classes = [_][:0]const u8{ "alias-out-arg0", "sum-overflows-i32", "negative-svec", "plain" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.VECTOR);
    const b = ctx.alloc(v.SVECTOR);
    const out = if (ctx.chance(8)) a else ctx.alloc(v.VECTOR);

    a.vx = ctx.pick(i32, &.{ 0, 1, -1, 0x7fffffff, -0x80000000, 0x7fff0000 });
    a.vy = ctx.pick(i32, &.{ 0, 1, -1, 0x7fffffff, -0x80000000, 0x7fff0000 });
    a.vz = ctx.pick(i32, &.{ 0, 1, -1, 0x7fffffff, -0x80000000, 0x7fff0000 });
    b.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    b.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    b.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    return .{ a, b, out };
}

fn overflows(x: i64) bool {
    return x > std.math.maxInt(i32) or x < std.math.minInt(i32);
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const b = args[1].?;
    if (args[2].? == a) return 0;
    const sx = @as(i64, a.vx) + @as(i64, b.vx);
    const sy = @as(i64, a.vy) + @as(i64, b.vy);
    const sz = @as(i64, a.vz) + @as(i64, b.vz);
    if (overflows(sx) or overflows(sy) or overflows(sz)) return 1;
    if (b.vx < 0 or b.vy < 0 or b.vz < 0) return 2;
    return 3;
}
