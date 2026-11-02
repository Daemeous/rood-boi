//! zig_diff spec: _vectorFixedPointMult -> vector_fixed_point_mult.vectorFixedPointMult (4A0A8.c).
//! Inputs: two VECTORs in the arena, the destination either the source itself
//! (a quarter of the runs, the in-place case) or a separate object. Components
//! and the scale are drawn from edge values (0, +-1, +-4096, the i32 extremes)
//! half the time and from any i32 otherwise, so the 64-bit product shifted by
//! 12 and truncated to a 32-bit field both wraps and stays small. The C has no
//! branches and makes no external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").vectorFixedPointMult));

pub const classes = [_][:0]const u8{ "aliased", "wrapping", "small" };

const component_edges = [_]i32{ 0, 1, -1, 4096, -4096, 0x7FFF_FFFF, -0x8000_0000, 0x1_0000, 0xFFF, 0x1000 };
const scale_edges = [_]c_int{ 0, 1, -1, 4096, 2048, 0x7FFF_FFFF, -0x8000_0000, 0x1_0000 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const src = ctx.alloc(v.VECTOR);
    const dst = if (ctx.chance(4)) src else ctx.alloc(v.VECTOR);
    src.vx = ctx.pick(i32, &component_edges);
    src.vy = ctx.pick(i32, &component_edges);
    src.vz = ctx.pick(i32, &component_edges);
    const k = ctx.pick(c_int, &scale_edges);
    return .{ dst, src, k };
}

fn fits(a: i32, k: c_int) bool {
    const p = (@as(i64, a) * @as(i64, k)) >> 12;
    return p >= std.math.minInt(i32) and p <= std.math.maxInt(i32);
}

pub fn classify(args: Args) usize {
    const dst = args[0].?;
    const src = args[1].?;
    const k = args[2];
    if (dst == src) return 0;
    if (!fits(src.vx, k) or !fits(src.vy, k) or !fits(src.vz, k)) return 1;
    return 2;
}
