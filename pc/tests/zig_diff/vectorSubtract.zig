//! zig_diff spec: _vectorSubtract -> vector_subtract.vectorSubtract (4A0A8.c).
//! Inputs: three VECTORs in the arena with random components, where each
//! component is an i32 edge value half the time (0, 1, -1, the extremes), so
//! the differences wrap. The destination may alias either source, or the two
//! sources may alias each other. The C has no branch and no call: its three
//! component stores and its returned pointer are compared, and the pad word
//! is left as the random fill.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").vectorSubtract));

pub const classes = [_][:0]const u8{ "distinct", "dst-is-src1", "dst-is-src2", "srcs-alias" };

const edges = [_]i32{ 0, 1, -1, 0x7fffffff, -0x80000000, 12345 };

fn fill(ctx: *zd.Ctx, a: *v.VECTOR) void {
    a.vx = ctx.pick(i32, &edges);
    a.vy = ctx.pick(i32, &edges);
    a.vz = ctx.pick(i32, &edges);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const dst = ctx.alloc(v.VECTOR);
    var src1 = ctx.alloc(v.VECTOR);
    var src2 = ctx.alloc(v.VECTOR);
    fill(ctx, dst);
    fill(ctx, src1);
    fill(ctx, src2);
    switch (ctx.range(u8, 0, 3)) {
        0 => {},
        1 => src1 = dst,
        2 => src2 = dst,
        else => src2 = src1,
    }
    return .{ dst, src1, src2 };
}

pub fn classify(args: Args) usize {
    const d = args[0].?;
    const s1 = args[1].?;
    const s2 = args[2].?;
    if (d == s1) return 1;
    if (d == s2) return 2;
    if (s1 == s2) return 3;
    return 0;
}
