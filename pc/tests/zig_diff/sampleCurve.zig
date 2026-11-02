//! zig_diff spec: vs_battle_sampleCurve -> sample_curve.sampleCurve (5BF94.c).
//! Inputs: D_800F569C an arena P-file whose curve block counts 0, 1, 5 or
//! 32 curves with lengths 1..255, each curve 256 random samples with 256
//! more before them (a negative distance reads there: `%` keeps the sign);
//! the curve id 0, 1..count (count itself often) or count + 1, at most 32.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").sampleCurve));

pub const classes = [_][:0]const u8{ "id-zero", "past-count", "sample", "negative-distance" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const pf = ctx.alloc(v.D_800F569C_t);
    const block: *v.pFileCurveBlock = @ptrCast(@alignCast(ctx.allocBytes(4 + 40, 16)));
    block.count = ctx.edge(u16, &.{ 0, 1, 5, 32 });
    const sizes: [*]u8 = @ptrCast(&block.curveSizes);
    for (0..40) |i| sizes[i] = ctx.pick(u8, &.{ 1, 2, 255 }) | 1;
    psx.put(&pf.curveBlock, block);
    for (0..32) |i| psx.put(&pf.curves[i], @as(*u8, @ptrCast(ctx.allocBytes(512, 16) + 256)));
    v.g.D_800F569C().* = pf;
    const count: c_int = block.count;
    const id: c_int = switch (ctx.range(u8, 0, 4)) {
        0 => 0,
        1 => @min(count + 1, 32),
        2 => count,
        else => if (count == 0) 0 else ctx.range(c_int, 1, count),
    };
    return .{ id, ctx.pick(c_int, &.{ 0, 1, -1, 254, 255, 256, std.math.maxInt(c_int), std.math.minInt(c_int) }) };
}

pub fn classify(args: Args) usize {
    const block = psx.get(v.g.D_800F569C().*.?.curveBlock).?;
    if (args[0] == 0) return 0;
    if (args[0] > block.count) return 1;
    return if (args[1] < 0) 3 else 2;
}
