//! zig_diff spec: vs_battle_clamp -> clamp.clamp (2842C.c).
//! Inputs: arg1 and arg2 are ints picked from edges (0, +-1, the 16-bit wrap
//! points, the int extremes) or random. Half the runs give arg2 the same low
//! half as arg1, so the shifted compare sees equal sides; a few runs give arg0
//! the truncated value of arg1 or arg2, so the bound compares see equal sides.
//! arg0 is a short from edges or random. The classes name the path the result
//! takes: below the low bound, above the high bound, or inside the bounds.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").clamp));

pub const classes = [_][:0]const u8{ "below-low", "above-high", "inside" };

const edges_i32 = [_]c_int{
    0,       1,        -1,     0x7fff,     0x8000, -0x8000, -0x8001,
    0xffff,  0x10000,  -0x10000, 0x7fffffff, -0x7fffffff - 1, 0x12345,
};
const edges_i16 = [_]c_short{ 0, 1, -1, 0x7fff, -0x8000, 0x100, -0x100 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a1 = ctx.pick(c_int, &edges_i32);
    var a2 = ctx.pick(c_int, &edges_i32);
    if (ctx.chance(2)) {
        a2 = @bitCast((@as(u32, @bitCast(a1)) & 0xffff) | (@as(u32, ctx.int(u16)) << 16));
    }
    var a0 = ctx.pick(c_short, &edges_i16);
    if (ctx.chance(3)) a0 = @truncate(a1);
    if (ctx.chance(3)) a0 = @truncate(a2);
    return .{ a0, a1, a2 };
}

/// The bounds the C picks: [low, high] after the shifted compare.
fn bounds(a1: c_int, a2: c_int) [2]c_short {
    const s1: c_int = @bitCast(@as(u32, @bitCast(a1)) << 16);
    const s2: c_int = @bitCast(@as(u32, @bitCast(a2)) << 16);
    if (s1 < s2) return .{ @truncate(a1), @truncate(a2) };
    return .{ @truncate(a2), @truncate(a1) };
}

pub fn classify(args: Args) usize {
    const b = bounds(args[1], args[2]);
    const a0 = args[0];
    if (a0 < b[0]) return 0;
    if (b[1] < a0) return 1;
    return 2;
}
