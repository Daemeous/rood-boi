//! zig_diff spec: vs_battle_splineInterpolate -> spline_interpolate.splineInterpolate (5BF94.c).
//! Inputs: four arena SVECTORs at the i16 edges and t in the callers'
//! -4096..8192 (func_800D037C's range) or anywhere (every product wraps);
//! the signed shifts round toward minus infinity, as the C's do.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").splineInterpolate));

pub const classes = [_][:0]const u8{ "between", "outside", "far" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var p: [4]*v.SVECTOR = undefined;
    for (&p) |*q| {
        q.* = ctx.alloc(v.SVECTOR);
        q.*.vx = ctx.pick(i16, &.{ 0, 1, -1, 4096, 32767, -32768 });
        q.*.vy = ctx.pick(i16, &.{ 0, 1, -1, 4096, 32767, -32768 });
        q.*.vz = ctx.pick(i16, &.{ 0, 1, -1, 4096, 32767, -32768 });
    }
    const t: c_int = switch (ctx.range(u8, 0, 2)) {
        0 => ctx.range(c_int, 0, 4096),
        1 => ctx.range(c_int, -4096, 8192),
        else => ctx.int(c_int),
    };
    return .{ p[0], p[1], p[2], p[3], t, ctx.alloc(v.SVECTOR) };
}

pub fn classify(args: Args) usize {
    const t = args[4];
    if (t >= 0 and t <= 4096) return 0;
    return if (t >= -4096 and t <= 8192) 1 else 2;
}
