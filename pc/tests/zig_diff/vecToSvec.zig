//! zig_diff spec: vs_battle_vecToSvec -> vec_to_svec.vecToSvec (5BF94.c).
//! Inputs: an arena VECTOR at the int edges and past 16 bits (truncated) and
//! an arena SVECTOR (its pad keeps its bytes).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").vecToSvec));

pub const classes = [_][:0]const u8{ "fits", "truncated" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.alloc(v.VECTOR);
    from.vx = ctx.pick(i32, &.{ 0, 1, -1, 32767, -32768, 0x10000, 0x18000 });
    from.vy = ctx.pick(i32, &.{ 0, 1, -1, 32767, -32768, 0x10000, 0x18000 });
    from.vz = ctx.pick(i32, &.{ 0, 1, -1, 32767, -32768, 0x10000, 0x18000 });
    return .{ from, ctx.alloc(v.SVECTOR) };
}

pub fn classify(args: Args) usize {
    const f = args[0].?;
    for ([_]i32{ f.vx, f.vy, f.vz }) |x| if (x > 32767 or x < -32768) return 1;
    return 0;
}
