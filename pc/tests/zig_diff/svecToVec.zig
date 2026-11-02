//! zig_diff spec: vs_battle_svecToVec -> svec_to_vec.svecToVec (5BF94.c).
//! Inputs: an arena SVECTOR at the i16 edges (negative ones sign-extend) and
//! an arena VECTOR.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").svecToVec));

pub const classes = [_][:0]const u8{ "non-negative", "negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.alloc(v.SVECTOR);
    from.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    from.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    from.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    return .{ from, ctx.alloc(v.VECTOR) };
}

pub fn classify(args: Args) usize {
    const f = args[0].?;
    return if (f.vx < 0 or f.vy < 0 or f.vz < 0) 1 else 0;
}
