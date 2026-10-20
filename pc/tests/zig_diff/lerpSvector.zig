//! zig_diff spec: vs_battle_lerpSvector -> vector_lerp.lerpSvector (5BF94.c).
//! Inputs: three (start, end) i16 pairs in the arena at the i16 edges, t in
//! 0-128 (the callers' range) or outside it (negative, large, the int edges:
//! the product wraps and the shift of a negative value rounds down), and an
//! arena SVECTOR.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").lerpSvector));

pub const classes = [_][:0]const u8{ "t-0-128", "t-outside", "product-wraps" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const src: [*]i16 = @ptrCast(@alignCast(ctx.allocBytes(12, 2)));
    for (0..6) |i| src[i] = ctx.pick(i16, &.{ 0, 1, -1, 127, 128, 32767, -32768 });
    const t: c_int = switch (ctx.range(u8, 0, 3)) {
        0, 1 => ctx.pick(c_int, &.{ 0, 1, 64, 127, 128 }) & 0xFF,
        2 => ctx.pick(c_int, &.{ -1, -128, 129, 255, 256, 65536 }),
        else => ctx.pick(c_int, &.{ std.math.maxInt(c_int), std.math.minInt(c_int), 0x10000, -0x10000 }),
    };
    return .{ src, if (t > 128 and t <= 255) @rem(t, 129) else t, ctx.alloc(v.SVECTOR) };
}

pub fn classify(args: Args) usize {
    const t: i64 = args[1];
    for (0..3) |i| {
        const d: i64 = @as(i64, args[0][2 * i + 1]) - args[0][2 * i];
        if (d * t > std.math.maxInt(i32) or d * t < std.math.minInt(i32)) return 2;
    }
    return if (t >= 0 and t <= 128) 0 else 1;
}
