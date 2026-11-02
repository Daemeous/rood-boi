//! zig_diff spec: _vectorAdd -> vector_add.vectorAdd (4A0A8.c).
//! Inputs: three VECTORs in the arena with random fill, each component an
//! edge value (0, +-1, the i32 extremes, +-2^30) or any i32, so the sums
//! wrap. The destination is a fresh object, or aliases the first source,
//! the second source, or both (all three the same object), so the aliasing
//! cases are covered as well as the distinct ones.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").vectorAdd));

pub const classes = [_][:0]const u8{ "distinct", "dst-is-src1", "dst-is-src2", "all-same" };

const edges = [_]i32{ 0, 1, -1, 0x7fffffff, -0x80000000, 0x40000000, -0x40000000 };

fn fillVec(ctx: *zd.Ctx, p: *v.VECTOR) void {
    p.vx = ctx.pick(i32, &edges);
    p.vy = ctx.pick(i32, &edges);
    p.vz = ctx.pick(i32, &edges);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const p0 = ctx.alloc(v.VECTOR);
    const p1 = ctx.alloc(v.VECTOR);
    const p2 = ctx.alloc(v.VECTOR);
    fillVec(ctx, p0);
    fillVec(ctx, p1);
    fillVec(ctx, p2);

    const r = ctx.range(u8, 0, 9);
    var dst = p0;
    const src1 = p1;
    var src2 = p2;
    switch (r) {
        6 => dst = src1,
        7 => dst = src2,
        8 => {
            dst = src1;
            src2 = src1;
        },
        else => {},
    }
    return .{ dst, src1, src2 };
}

pub fn classify(args: Args) usize {
    const d = args[0].?;
    const s1 = args[1].?;
    const s2 = args[2].?;
    if (d == s1 and s1 == s2) return 3;
    if (d == s1) return 1;
    if (d == s2) return 2;
    return 0;
}
